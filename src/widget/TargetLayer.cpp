#include "widget/TargetLayer.h"

#include "geo/MapDecor.h"
#include "widget/MapView.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPolygonF>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace libmapa {

TargetLayer::TargetLayer(QCustomPlot *parent, TargetModel *model)
    : QCPLayerable(parent)
    , m_model(model)
{
    setAntialiased(true);

    // Agrupa las actualizaciones: aunque lleguen decenas de posiciones por
    // segundo, se repinta como mucho una vez cada 33 ms (~30 fps).
    m_repintar.setSingleShot(true);
    m_repintar.setInterval(33);
    connect(&m_repintar, &QTimer::timeout, this, [this] {
        QCPLayer *capa = layer();
        if (!capa)
            return;
        // La capa esta en modo BUFFERED (lo fija MapView): esto repinta SOLO
        // esta capa y recompone, sin rehacer teselas ni entidades.
        capa->replot();
    });

    if (m_model) {
        connect(m_model, &TargetModel::changed,
                this, &TargetLayer::programarRepintado);
    }
}

TargetLayer::~TargetLayer() = default;

// Inyecta la funcion que convierte lat/lon a coordenadas de EJE (la proyeccion
// de Mercator la aporta MapView). Sin ella se usa lon/lat directo como respaldo.
void TargetLayer::setAxisMapper(
    std::function<QPointF(const QGeoCoordinate &)> toAxis)
{
    m_toAxis = std::move(toAxis);
    programarRepintado();
}

// Registra el proveedor de simbolos de la aplicacion. Al cambiarlo se repinta
// para que el nuevo juego de iconos se vea de inmediato.
void TargetLayer::setSymbolProvider(TargetSymbolProvider provider)
{
    m_symbolProvider = std::move(provider);
    programarRepintado();
}

// Fija el objetivo resaltado y repinta.
void TargetLayer::setSelected(qint64 id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    programarRepintado();
}

// Enciende/apaga el historial del objetivo seleccionado y repinta.
void TargetLayer::setSelectedHistoryVisible(bool on)
{
    if (m_historyVisible == on)
        return;
    m_historyVisible = on;
    programarRepintado();
}

// Fija el nivel de detalle para escalar a miles: topes de etiquetas y trazas.
void TargetLayer::setDetailBudget(int maxLabels, int maxTrails)
{
    m_labelBudget = maxLabels;
    m_trailBudget = maxTrails;
    programarRepintado();
}

// Pide un repintado coalescido: arranca el temporizador de 33 ms si no corre ya,
// de modo que una rafaga de actualizaciones se dibuje una sola vez (~30 fps).
void TargetLayer::programarRepintado()
{
    if (!m_repintar.isActive())
        m_repintar.start();
}

// Coordenada geografica -> pixel de pantalla: primero a eje (via m_toAxis) y
// luego a pixel con los ejes de QCustomPlot. Punto nulo si no hay plot o la
// coordenada no es valida.
QPointF TargetLayer::screenPos(const QGeoCoordinate &c) const
{
    QCustomPlot *plot = parentPlot();
    if (!plot || !c.isValid())
        return {};
    const QPointF eje = m_toAxis ? m_toAxis(c)
                                 : QPointF(c.longitude(), c.latitude());
    return QPointF(plot->xAxis->coordToPixel(eje.x()),
                   plot->yAxis->coordToPixel(eje.y()));
}

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void TargetLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Pinta los objetivos visibles en dos pasadas para escalar a MILES:
//   1) culling: recoge solo los que caen en pantalla (con margen) y su posicion.
//   2) nivel de detalle: si hay demasiados visibles, se dejan de dibujar las
//      etiquetas y/o las trazas (que a esa densidad son una mancha ilegible y
//      lo mas caro de pintar); el simbolo se dibuja siempre.
// Asi, pocos objetivos salen con todo el detalle y miles siguen fluidos.
void TargetLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot || !m_model)
        return;

    // --- Pasada 1: culling ---------------------------------------------------
    const QRectF margen = QRectF(plot->viewport()).adjusted(-256, -256, 256, 256);
    m_visibles.clear();
    for (const TargetModel::Entry &e : m_model->entries()) {
        if (!e.target.position.isValid())
            continue;
        if (!m_filterDim && m_filteredOut.contains(e.target.id))
            continue;                          // oculto por el filtro de la app
        const QPointF pos = screenPos(e.target.position);
        if (margen.contains(pos))
            m_visibles.append({&e, pos});
    }

    // --- Agrupacion: los montones se dibujan como un circulo con el numero ---
    // La rejilla se ancla en un punto fijo del mundo (0,0 geografico) pasado a
    // pixeles: al desplazar el mapa los grupos no cambian, solo al cambiar de
    // zoom. El seleccionado y los atenuados quedan siempre sueltos.
    m_clusterHits.clear();
    m_clustered.clear();
    const MapView *vista = qobject_cast<const MapView *>(plot);
    const bool agrupar = m_clustering.enabled
        && (m_clustering.maxZoom < 0 || !vista || vista->zoom() <= m_clustering.maxZoom);
    if (agrupar) {
        QVector<QPointF> pts;
        QVector<int> indices;            // posicion en m_visibles de cada punto
        for (int i = 0; i < m_visibles.size(); ++i) {
            const qint64 id = m_visibles.at(i).first->target.id;
            if (id == m_selected || (m_filterDim && m_filteredOut.contains(id)))
                continue;
            pts.append(m_visibles.at(i).second);
            indices.append(i);
        }
        const double celda = std::clamp(m_clustering.cellPixels, 16, 512);
        const auto grupos = MapDecor::clusterPoints(pts, celda,
                                                    std::max(2, m_clustering.minTargets),
                                                    screenPos(QGeoCoordinate(0.0, 0.0)));
        for (const MapDecor::PointCluster &g : grupos) {
            ClusterHit h;
            h.center = g.center;
            h.radius = 11.0 + 4.0 * std::log10(double(g.members.size()));
            for (int k : g.members) {
                const qint64 id = m_visibles.at(indices.at(k)).first->target.id;
                h.ids.append(id);
                m_clustered.insert(id);
            }
            m_clusterHits.append(h);
        }
        if (!m_clustered.isEmpty()) {
            QVector<std::pair<const TargetModel::Entry *, QPointF>> sueltos;
            sueltos.reserve(m_visibles.size() - m_clustered.size());
            for (const auto &v : m_visibles)
                if (!m_clustered.contains(v.first->target.id))
                    sueltos.append(v);
            m_visibles = sueltos;
        }
    }

    // --- Nivel de detalle segun cuantos hay visibles -------------------------
    const int visibles = int(m_visibles.size());
    const bool conEtiquetas = m_labelBudget > 0 && visibles <= m_labelBudget;
    const bool conTrazas    = m_trailBudget > 0 && visibles <= m_trailBudget;

    m_lastDrawn = visibles;
    m_lastLabels = 0;
    m_lastTrails = 0;
    m_labelCells.clear();
    m_labelBoxes.clear();   // cajas de etiqueta de este frame, para hit-test

    // Historial del seleccionado, debajo de todo y FUERA del culling: aunque el
    // objetivo este fuera de pantalla, su recorrido (y su origen) puede cruzarla.
    if (m_historyVisible && m_selected >= 0) {
        const auto it = m_model->entries().constFind(m_selected);
        if (it != m_model->entries().constEnd())
            drawHistory(painter, *it);
    }

    // --- Pasada 2: dibujo ----------------------------------------------------
    for (const auto &v : m_visibles) {
        const bool sel = (v.first->target.id == m_selected);
        // Atenuado por el filtro: tenue, sin traza ni rotulo (salvo si esta
        // seleccionado, que se ve normal: lo ha elegido el usuario).
        const bool tenue = m_filterDim && !sel && m_filteredOut.contains(v.first->target.id);
        if (tenue) {
            painter->save();
            painter->setOpacity(0.25);
        }
        drawTarget(painter, *v.first, v.second, conTrazas && !tenue, conEtiquetas && !tenue,
                   sel, m_labelCells);
        if (tenue)
            painter->restore();
    }

    // Los grupos, encima de los sueltos (son lo que resume la escena).
    for (const ClusterHit &h : m_clusterHits)
        drawCluster(painter, h.center, int(h.ids.size()));

    // Alertas de proximidad, ENCIMA de los objetivos (son el aviso que no debe
    // quedar tapado) y fuera del culling: el punto de encuentro puede estar en
    // pantalla aunque los dos objetivos aun no lo esten.
    if (!m_proximity.isEmpty())
        drawProximity(painter);
}

// Cambia la agrupacion y repinta.
void TargetLayer::setClustering(const TargetClustering &c)
{
    m_clustering = c;
    programarRepintado();
}

// Grupo bajo el pixel (el mas cercano a su centro si se solapan), o vacio.
QVector<qint64> TargetLayer::clusterAt(const QPoint &pixel) const
{
    const ClusterHit *mejor = nullptr;
    double mejorD = 0.0;
    for (const ClusterHit &h : m_clusterHits) {
        const double d = QLineF(h.center, QPointF(pixel)).length();
        if (d <= h.radius + 2.0 && (!mejor || d < mejorD)) {
            mejor = &h;
            mejorD = d;
        }
    }
    return mejor ? mejor->ids : QVector<qint64>();
}

// Un grupo: circulo del color de la agrupacion con halo claro y el numero de
// objetivos dentro. El radio crece con el logaritmo del numero (10 o 1000 se
// distinguen sin que los grandes tapen el mapa).
void TargetLayer::drawCluster(QPainter *painter, const QPointF &center, int count) const
{
    const double r = 11.0 + 4.0 * std::log10(double(count));
    const QColor c = m_clustering.color;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(c.red(), c.green(), c.blue(), 70));
    painter->drawEllipse(center, r + 5.0, r + 5.0);
    painter->setBrush(c);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->drawEllipse(center, r, r);
    QFont f = painter->font();
    f.setPointSizeF(count >= 1000 ? 7.0 : 8.5);
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(Qt::white);
    painter->drawText(QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
                      QString::number(count));
    painter->restore();
}

// Guarda que objetivos no pasan el filtro y como mostrarlos, y repinta si cambio.
void TargetLayer::setFilteredOut(const QSet<qint64> &ids, bool dim)
{
    if (ids == m_filteredOut && dim == m_filterDim)
        return;
    m_filteredOut = ids;
    m_filterDim = dim;
    programarRepintado();
}

// Guarda las lineas CPA de los pares en alerta y pide repintar.
void TargetLayer::setProximityLines(const QVector<ProximityLine> &lines)
{
    m_proximity = lines;
    programarRepintado();
}

// Cada par en alerta: discontinua de cada objetivo a donde estara en el CPA, la
// distancia minima entre esos dos puntos en continua (con un punto en cada
// extremo) y el rotulo en su centro. Lo que se ve es "por donde se cruzan y a
// cuanto", sin tener que leer numeros.
void TargetLayer::drawProximity(QPainter *painter) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QFont f = painter->font();
    f.setPointSizeF(8.0);
    f.setBold(true);
    painter->setFont(f);
    const QFontMetrics fm(f);
    const QColor c = m_proximityColor;
    QVector<QRectF> ocupadas;          // rotulos ya puestos en este dibujado

    for (const ProximityLine &l : m_proximity) {
        const auto ia = m_model->entries().constFind(l.a);
        const auto ib = m_model->entries().constFind(l.b);
        if (ia == m_model->entries().constEnd() || ib == m_model->entries().constEnd())
            continue;
        const QPointF pa = screenPos(ia->target.position);
        const QPointF pb = screenPos(ib->target.position);
        const QPointF ca = l.cpaA.isValid() ? screenPos(l.cpaA) : pa;
        const QPointF cb = l.cpaB.isValid() ? screenPos(l.cpaB) : pb;

        QPen discontinua(c, 1.6, Qt::DashLine);
        painter->setPen(discontinua);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(pa, ca);
        painter->drawLine(pb, cb);

        painter->setPen(QPen(QColor(255, 255, 255, 190), 5.0, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(ca, cb);
        painter->setPen(QPen(c, 2.6, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(ca, cb);
        painter->setBrush(c);
        painter->setPen(QPen(Qt::white, 1.2));
        painter->drawEllipse(ca, 3.5, 3.5);
        painter->drawEllipse(cb, 3.5, 3.5);

        if (!l.label.isEmpty()) {
            const QPointF medio = (ca + cb) / 2.0;
            const QRectF caja(medio.x() + 6.0, medio.y() - fm.height() - 4.0,
                              fm.horizontalAdvance(l.label) + 8.0, fm.height() + 3.0);
            // Con muchas alertas juntas, un rotulo que pisaria a otro se omite:
            // la linea sigue viendose y el detalle esta en proximityAlerts().
            const QRectF holgada = caja.adjusted(-2.0, -2.0, 2.0, 2.0);
            if (std::any_of(ocupadas.cbegin(), ocupadas.cend(),
                            [&holgada](const QRectF &r) { return r.intersects(holgada); }))
                continue;
            ocupadas.append(caja);
            painter->setPen(QPen(c, 1.0));
            painter->setBrush(QColor(255, 255, 255, 225));
            painter->drawRoundedRect(caja, 3.0, 3.0);
            painter->setPen(c.darker(140));
            painter->drawText(caja, Qt::AlignCenter, l.label);
        }
    }
    painter->restore();
}

// Dibuja el historial completo de un objetivo, del ORIGEN a la posicion actual:
// linea del color del objetivo sobre un halo blanco (se ve sobre cualquier
// fondo), decimada a ~1.5 px, y en el origen un cuadro con la hora en que se vio
// por primera vez. Es lo que permite saber de donde vino.
void TargetLayer::drawHistory(QPainter *painter, const TargetModel::Entry &e) const
{
    if (e.history.isEmpty())
        return;
    QPolygonF linea;
    linea.reserve(e.history.size() + 1);
    QPointF ultimo;
    for (const TargetModel::HistoryPoint &h : e.history) {
        const QPointF p = screenPos(QGeoCoordinate(h.lat, h.lon));
        if (linea.isEmpty() || QLineF(ultimo, p).length() >= 1.5) {
            linea.append(p);
            ultimo = p;
        }
    }
    // Hasta la posicion actual (el ultimo punto anotado puede ir un poco detras).
    linea.append(screenPos(e.target.position));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    if (linea.size() >= 2) {
        QPen halo(QColor(255, 255, 255, 170), 4.5);
        halo.setJoinStyle(Qt::RoundJoin);
        painter->setPen(halo);
        painter->drawPolyline(linea);
        QPen pen(e.target.color, 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->drawPolyline(linea);
    }

    // Marca del origen: cuadro hueco + "origen HH:mm:ss" (hora local).
    const TargetModel::HistoryPoint &o = e.history.first();
    const QPointF po = screenPos(QGeoCoordinate(o.lat, o.lon));
    const QRectF cuadro(po.x() - 5.0, po.y() - 5.0, 10.0, 10.0);
    painter->setPen(QPen(Qt::white, 4.0));
    painter->drawRect(cuadro);
    painter->setPen(QPen(e.target.color.darker(130), 2.0));
    painter->drawRect(cuadro);
    const QString txt = tr("origen %1").arg(
        QDateTime::fromMSecsSinceEpoch(o.msecs).toString(QStringLiteral("HH:mm:ss")));
    QFont f = painter->font();
    f.setPointSizeF(8.0);
    painter->setFont(f);
    const QFontMetrics fm(f);
    const QRectF caja(po.x() + 8.0, po.y() - fm.height() / 2.0 - 1.0,
                      fm.horizontalAdvance(txt) + 6.0, fm.height() + 2.0);
    painter->fillRect(caja, QColor(255, 255, 255, 200));
    painter->setPen(Qt::black);
    painter->drawText(caja, Qt::AlignCenter, txt);
    painter->restore();
}

// Dibuja UN objetivo ya situado en 'pos': su traza (si drawTrail), su simbolo
// (icono de la app o galon por defecto) y su etiqueta (si drawLabel y la celda
// de pantalla esta libre: declutter). El culling ya lo hizo draw().
void TargetLayer::drawTarget(QPainter *painter, const TargetModel::Entry &e,
                             const QPointF &pos, bool drawTrail, bool drawLabel,
                             bool selected, QSet<qint64> &labelCells) const
{
    const MapTarget &t = e.target;

    // Halo del objetivo resaltado: un aro bajo el simbolo para que se vea cual
    // esta seleccionado.
    if (selected) {
        const double rr = m_symbolPx * 1.9;
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 210, 0), 2.2));
        painter->drawEllipse(pos, rr, rr);
    }

    // --- Traza -------------------------------------------------------------
    if (drawTrail && t.trailVisible && e.trail.size() >= 2) {
        // Decimacion: se saltan los puntos que caen a menos de 2 px del ultimo
        // dibujado. Al alejar el zoom una traza larga son muchos puntos pegados;
        // dibujar la mitad no se nota y ahorra la mayor parte del coste.
        QPolygonF linea;
        linea.reserve(e.trail.size());
        QPointF ultimo;
        bool primero = true;
        for (const QGeoCoordinate &c : e.trail) {
            const QPointF p = screenPos(c);
            if (primero || QLineF(ultimo, p).length() >= 2.0) {
                linea.append(p);
                ultimo = p;
                primero = false;
            }
        }
        if (linea.size() >= 2) {
            QColor colorTraza = t.color;
            colorTraza.setAlpha(140);
            QPen pen(colorTraza);
            pen.setWidthF(1.5);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            // La traza sin suavizado: con miles de objetivos es donde mas se nota.
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->drawPolyline(linea);
            painter->setRenderHint(QPainter::Antialiasing, true);
            ++m_lastTrails;
        }
    }

    // --- Simbolo -----------------------------------------------------------
    // Si la app registro un proveedor y devuelve un icono, lo dibuja ella (la
    // libreria solo lo coloca, lo gira por el rumbo si se pide y lo escala). Si
    // no hay proveedor o el icono es nulo, se cae al galon por defecto.
    const double r = m_symbolPx;
    TargetSymbol sym;
    if (m_symbolProvider)
        sym = m_symbolProvider(t);

    if (!sym.icon.isNull()) {
        // Icono de la app. El devicePixelRatio permite iconos nitidos en HiDPI:
        // el tamano logico es width()/dpr.
        const double dpr = sym.icon.devicePixelRatio();
        const double w = sym.icon.width() / dpr * sym.scale;
        const double h = sym.icon.height() / dpr * sym.scale;
        painter->save();
        painter->translate(pos);
        if (sym.rotateWithHeading)
            painter->rotate(t.headingDeg);
        // El anchor (0..1) dice que punto del icono cae sobre la posicion.
        const QRectF destino(-w * sym.anchor.x(), -h * sym.anchor.y(), w, h);
        painter->drawPixmap(destino, sym.icon, QRectF(sym.icon.rect()));
        painter->restore();
    } else {
        // Galon por defecto: apunta al norte y se gira en horario segun el rumbo.
        // En pantalla la Y crece hacia abajo y QPainter::rotate gira en horario
        // para angulos positivos, asi que el norte queda arriba.
        painter->save();
        painter->translate(pos);
        painter->rotate(t.headingDeg);
        QPolygonF simbolo;
        simbolo << QPointF(0.0, -r)              // proa
                << QPointF(r * 0.7, r * 0.8)     // popa derecha
                << QPointF(0.0, r * 0.4)         // muesca
                << QPointF(-r * 0.7, r * 0.8);   // popa izquierda
        painter->setPen(QPen(Qt::black, 0.8));
        painter->setBrush(t.color);
        painter->drawPolygon(simbolo);
        painter->restore();
    }

    // --- Etiqueta (multilinea: un parametro por linea) ---------------------
    // El objetivo seleccionado muestra SIEMPRE su etiqueta (salta el presupuesto
    // y el declutter), para poder leer sus datos aunque este en una zona densa.
    if ((drawLabel || selected) && t.labelVisible && !t.label.isEmpty()) {
        if (!selected) {
            // Declutter: una sola etiqueta por celda de pantalla (~40 px). Si ya
            // hay una etiqueta en la celda de este objetivo, se omite la suya
            // para que a densidad alta no se solapen en una mancha ilegible.
            const double CELDA = 40.0;
            const qint64 celda = qint64(std::floor(pos.x() / CELDA)) * 100000
                               + qint64(std::floor(pos.y() / CELDA));
            if (labelCells.contains(celda))
                return;
            labelCells.insert(celda);
        }
        ++m_lastLabels;

        const QStringList lineas =
            t.label.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QFontMetricsF fm(painter->font());
        const double h = fm.height();
        // El desplazamiento de la etiqueta (relativo al simbolo) permite
        // arrastrarla a voluntad; como es relativo, al mover el objetivo la
        // etiqueta lo sigue.
        const QPointF off = t.labelOffsetPx;
        double y = pos.y() + r * 0.5 + fm.ascent() + off.y();
        const double x = pos.x() + r + 3.0 + off.x();

        // Caja del bloque de texto (para la linea guia y el hit-test).
        double anchoMax = 0.0;
        for (const QString &linea : lineas)
            anchoMax = std::max(anchoMax, double(fm.horizontalAdvance(linea)));
        const QRectF caja(QPointF(x, pos.y() + r * 0.5 + off.y()),
                          QSizeF(anchoMax, h * std::max(1, int(lineas.size()))));
        m_labelBoxes.append({t.id, caja});

        // Linea guia del simbolo a la etiqueta cuando esta desplazada.
        if (off.manhattanLength() > 1.0) {
            painter->setPen(QPen(t.color, 1.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(pos, caja.center());
        }

        for (const QString &linea : lineas) {
            const QPointF a(x, y);
            // Un halo claro detras del texto para que se lea sobre el mapa.
            painter->setPen(QPen(QColor(255, 255, 255, 200)));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx || dy)
                        painter->drawText(a + QPointF(dx, dy), linea);
            painter->setPen(Qt::black);
            painter->drawText(a, linea);
            y += h;
        }
    }
}

// Objetivo cuya etiqueta dibujada contiene el pixel. Se recorre al reves (la
// ultima dibujada queda encima) sobre las cajas del ultimo frame.
qint64 TargetLayer::labelAt(const QPoint &pixel) const
{
    const QPointF p(pixel);
    for (int i = int(m_labelBoxes.size()) - 1; i >= 0; --i)
        if (m_labelBoxes[i].second.contains(p))
            return m_labelBoxes[i].first;
    return -1;
}

} // namespace libmapa
