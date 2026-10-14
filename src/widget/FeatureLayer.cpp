#include "widget/FeatureLayer.h"

#include "core/Logging.h"
#include "widget/MapView.h"

#include <QDateTime>
#include <QPainterPath>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace libmapa {

namespace {
// ¿La capa debe dibujarse/consultarse al zoom actual? Fuera de su rango
// [minZoom, maxZoom] (−1 = sin límite) se omite: así una capa de detalle (p. ej.
// curvas de nivel) no estorba ni se selecciona a vista general.
bool capaVisibleAZoom(const LayerInfo &capa, int zoom)
{
    if (capa.minZoom >= 0 && zoom < capa.minZoom)
        return false;
    if (capa.maxZoom >= 0 && zoom > capa.maxZoom)
        return false;
    return true;
}
} // namespace

FeatureLayer::FeatureLayer(QCustomPlot *parent, OverlayModel *model)
    : QCPLayerable(parent)
    , m_model(model)
{
    setAntialiased(true);   // las geometrias si se benefician del suavizado

    if (m_model) {
        connect(m_model, &OverlayModel::changed,
                this, [this] { markDirty(); });
    }
}

FeatureLayer::~FeatureLayer() = default;

// Inyecta la funcion lat/lon -> eje (proyeccion de Mercator, la aporta MapView) e
// invalida la cache para repintar con la nueva proyeccion.
void FeatureLayer::setAxisMapper(
    std::function<QPointF(const QGeoCoordinate &)> toAxis)
{
    m_toAxis = std::move(toAxis);
    markDirty();
}

// Fija (o quita, con nullptr) la geometria "en construccion" que se dibuja
// discontinua mientras el usuario dibuja una entidad nueva. Pide un repintado.
void FeatureLayer::setDraft(const MapFeature *draft)
{
    if (draft) {
        m_draft = *draft;
        m_hasDraft = true;
    } else {
        m_hasDraft = false;
        m_draftCursorVisible = false;
    }
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

// Actualiza la posicion del cursor durante el dibujo, para pintar el "lado de
// goma" que se anadiria al siguiente clic. 'visible' lo enciende o apaga.
void FeatureLayer::setDraftCursor(const QPoint &pixel, bool visible)
{
    m_draftCursor = pixel;
    m_draftCursorVisible = visible;
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

// Invalida la cache (pixmap) de esta capa y encola un repintado. Se llama cuando
// cambia el modelo o la proyeccion: el proximo draw() reconstruira el pixmap.
void FeatureLayer::markDirty()
{
    m_dirty = true;
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

// Coordenada geografica -> pixel de pantalla (via el mapeador de eje y los ejes
// de QCustomPlot). Punto nulo si no hay plot o la coordenada no es valida.
QPointF FeatureLayer::screenPos(const QGeoCoordinate &c) const
{
    QCustomPlot *plot = parentPlot();
    if (!plot || !c.isValid())
        return {};

    const QPointF eje = m_toAxis ? m_toAxis(c)
                                 : QPointF(c.longitude(), c.latitude());
    return QPointF(plot->xAxis->coordToPixel(eje.x()),
                   plot->yAxis->coordToPixel(eje.y()));
}

// Geometria (primera parte) de una entidad convertida a poligono de pixeles.
QPolygonF FeatureLayer::screenPolygon(const MapFeature &f) const
{
    return screenPolygonOf(f.geometry);
}

// Rectangulo visible actual en coordenadas de EJE (x=longitud, y=eje-Y de
// Mercator), ensanchado un 'marginFrac' por lado para no recortar entidades
// que cruzan justo el borde.
QRectF FeatureLayer::visibleAxisRect(double marginFrac) const
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return {};
    const QCPRange rx = plot->xAxis->range();
    const QCPRange ry = plot->yAxis->range();
    QRectF r(QPointF(rx.lower, ry.lower), QPointF(rx.upper, ry.upper));
    r = r.normalized();
    const double mx = r.width() * marginFrac;
    const double my = r.height() * marginFrac;
    return r.adjusted(-mx, -my, mx, my);
}

// Recorte por viewport: la entidad se pinta/consulta solo si TOCA el rectangulo
// visible. Camino rapido: en cuanto un vertice cae dentro, dibuja; si no, se
// compara la caja envolvente (para no perder un tramo que cruza con los
// extremos fuera). Todo en coordenadas de eje, sin proyectar a pixeles.
bool FeatureLayer::featureInView(const MapFeature &f, const QRectF &axisRect) const
{
    if (axisRect.isNull())
        return true;
    double minx = 0, maxx = 0, miny = 0, maxy = 0;
    bool any = false;
    for (const QVector<QGeoCoordinate> &parte : f.outlines()) {
        for (const QGeoCoordinate &c : parte) {
            if (!c.isValid())
                continue;
            const QPointF e = m_toAxis ? m_toAxis(c) : QPointF(c.longitude(), c.latitude());
            if (axisRect.contains(e))
                return true;                      // un vertice visible -> seguro
            if (!any) { minx = maxx = e.x(); miny = maxy = e.y(); any = true; }
            else {
                minx = qMin(minx, e.x()); maxx = qMax(maxx, e.x());
                miny = qMin(miny, e.y()); maxy = qMax(maxy, e.y());
            }
        }
    }
    if (!any)
        return false;
    // Solape de cajas AABB hecho a mano: QRectF::intersects da falso con cajas
    // degeneradas (una linea horizontal/vertical tiene alto/ancho 0), que SI
    // deben contar como visibles si cruzan el rectangulo.
    return !(maxx < axisRect.left() || minx > axisRect.right()
             || maxy < axisRect.top() || miny > axisRect.bottom());
}

// Convierte una lista de coordenadas a un poligono de pixeles de pantalla.
QPolygonF FeatureLayer::screenPolygonOf(const QVector<QGeoCoordinate> &pts) const
{
    QPolygonF poly;
    poly.reserve(pts.size());
    for (const QGeoCoordinate &c : pts)
        poly.append(screenPos(c));
    return poly;
}

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void FeatureLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Pinta las entidades vectoriales. Clave del rendimiento: dibuja sobre un pixmap
// propio que se REUTILIZA mientras no cambien ni el modelo ni la vista (mismo
// viewport y mismos rangos de eje). Asi los objetivos en movimiento (otra capa)
// no obligan a redibujar las zonas estaticas. Si la cache vale, la vuelca y solo
// repinta el borrador; si no, reconstruye el pixmap capa a capa por zOrder. El
// borrador (draft) se dibuja siempre encima, fuera de la cache.
void FeatureLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot || !m_model)
        return;

    const QRect vp = plot->viewport();
    if (vp.width() <= 0 || vp.height() <= 0)
        return;

    // El pixmap solo vale si no ha cambiado ni el modelo ni la vista. Basta
    // con comparar viewport y rangos de eje: cualquier desplazamiento o zoom
    // los mueve.
    const bool mismaVista = (m_cacheViewport == vp)
                            && qFuzzyCompare(m_cacheX.lower, plot->xAxis->range().lower)
                            && qFuzzyCompare(m_cacheX.upper, plot->xAxis->range().upper)
                            && qFuzzyCompare(m_cacheY.lower, plot->yAxis->range().lower)
                            && qFuzzyCompare(m_cacheY.upper, plot->yAxis->range().upper);

    if (!m_dirty && mismaVista && !m_cache.isNull()) {
        ++m_cacheHits;
        painter->drawPixmap(vp.topLeft(), m_cache);
        drawDraft(painter);
        return;
    }

    // Se dibuja sobre un pixmap transparente, no directamente sobre el
    // lienzo: asi se puede reutilizar mientras nada cambie. Es lo que
    // permitira que los objetivos en movimiento, en su propia capa, no
    // obliguen a redibujar las zonas estaticas.
    m_cache = QPixmap(vp.size() * plot->bufferDevicePixelRatio());
    m_cache.setDevicePixelRatio(plot->bufferDevicePixelRatio());
    m_cache.fill(Qt::transparent);

    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(-vp.topLeft());

    m_lastDrawn = 0;

    const MapView *vista = qobject_cast<MapView *>(plot);
    const int zoomActual = vista ? vista->zoom() : -1;
    const QRectF visible = visibleAxisRect();     // recorte por viewport

    // Las capas se recorren en orden de zOrder: la ultima queda encima.
    for (const LayerInfo &capa : m_model->layers()) {
        if (!capa.visible)
            continue;
        if (zoomActual >= 0 && !capaVisibleAZoom(capa, zoomActual))
            continue;               // capa de detalle fuera de su rango de zoom
        for (const MapFeature &f : m_model->featuresInLayer(capa.id)) {
            if (!f.visible)
                continue;
            if (!featureInView(f, visible))
                continue;           // fuera de pantalla: ni se proyecta
            drawFeature(&p, f, f.id == m_model->selectedId());
            ++m_lastDrawn;
        }
    }

    p.end();

    m_dirty = false;
    m_cacheViewport = vp;
    m_cacheX = plot->xAxis->range();
    m_cacheY = plot->yAxis->range();

    painter->drawPixmap(vp.topLeft(), m_cache);
    drawDraft(painter);
}

// Pinta UNA entidad completa: cada parte de su geometria (punto o trazado), los
// tiradores de vertices si esta seleccionada/editable (solo una parte) y su
// etiqueta (junto al punto, o en el centro del rectangulo que la contiene toda).
void FeatureLayer::drawFeature(QPainter *painter, const MapFeature &f,
                               bool selected) const
{
    if (m_alerted.contains(f.id))
        drawAlert(painter, f);           // por debajo: la entidad queda encima
    const auto partes = f.outlines();

    for (const QVector<QGeoCoordinate> &parte : partes) {
        if (f.kind == GeometryKind::Point)
            drawPointPart(painter, f, parte, selected);
        else
            drawPathPart(painter, f, parte, selected);
    }

    // Los tiradores de edicion solo tienen sentido en una sola parte.
    if ((selected || f.style.verticesVisible) && !f.isMultiPart()
        && f.kind != GeometryKind::Point)
        drawVertices(painter, f);

    // Inicio y fin encima de los tiradores: son lo que se busca en una ruta.
    if (f.kind == GeometryKind::Polyline)
        drawEndpoints(painter, f, selected);

    if (f.style.labelVisible && !f.name.isEmpty()) {
        const QPointF base = labelAnchorScreen(f);
        if (!base.isNull())
            drawLabel(painter, f, base);
    }
}

// Dibuja una entidad de tipo PUNTO: su icono si lo tiene, o un circulo con el
// estilo. Si esta seleccionada anade un resalte en PIXELES (no en grados, para no
// deformarse con el zoom ni la latitud).
void FeatureLayer::drawPointPart(QPainter *painter, const MapFeature &f,
                                 const QVector<QGeoCoordinate> &part,
                                 bool selected) const
{
    if (part.isEmpty())
        return;
    const QPointF pos = screenPos(part.first());
    const double r = f.style.pointRadiusPx;

    if (!f.style.icon.isNull()) {
        // El icono se ancla por 'iconAnchor' (normalizado): asi un pin cuya
        // PUNTA sea (0.5,1.0) queda con la punta justo sobre la coordenada, y la
        // dpr del pixmap se respeta (dibujar en tamano logico, no en pixeles).
        const double dpr = f.style.icon.devicePixelRatio();
        const double w = f.style.icon.width() / (dpr > 0 ? dpr : 1.0);
        const double h = f.style.icon.height() / (dpr > 0 ? dpr : 1.0);
        const QPointF tl(pos.x() - f.style.iconAnchor.x() * w,
                         pos.y() - f.style.iconAnchor.y() * h);
        painter->drawPixmap(tl, f.style.icon);

        if (selected) {
            // Resalte alrededor del icono (en pixeles): recuadro redondeado.
            painter->setBrush(Qt::NoBrush);
            const QRectF caja = QRectF(tl, QSizeF(w, h)).adjusted(-3, -3, 3, 3);
            painter->setPen(QPen(Qt::white, 3, Qt::SolidLine));
            painter->drawRoundedRect(caja, 4, 4);
            painter->setPen(QPen(Qt::black, 1, Qt::DashLine));
            painter->drawRoundedRect(caja, 4, 4);
        }
        return;
    }

    painter->setPen(QPen(f.style.lineColor, f.style.lineWidth));
    painter->setBrush(QBrush(f.style.fillColor));
    painter->drawEllipse(pos, r, r);

    if (selected) {
        // El resalte va en PIXELES alrededor del simbolo, no en grados: si se
        // dibujara en coordenadas geograficas cambiaria de tamano con el zoom
        // y se deformaria con la latitud.
        painter->setPen(QPen(Qt::white, 3, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(pos, r + 5, r + 5);
        painter->setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter->drawEllipse(pos, r + 5, r + 5);
    }
}

// Resalte de alerta de zona, en PIXELES (no cambia con el zoom): un aro grueso
// alrededor de cada punto, y en lineas/poligonos un borde ancho translucido con
// un velo del color de alerta. Se pinta antes que la entidad.
void FeatureLayer::drawAlert(QPainter *painter, const MapFeature &f) const
{
    QColor borde = m_alertColor;
    borde.setAlpha(200);
    QColor velo = m_alertColor;
    velo.setAlpha(45);
    for (const QVector<QGeoCoordinate> &parte : f.outlines()) {
        if (f.kind == GeometryKind::Point) {
            if (parte.isEmpty())
                continue;
            const QPointF pos = screenPos(parte.first());
            const double r = f.style.pointRadiusPx + 8.0;
            painter->setPen(QPen(borde, 4.0));
            painter->setBrush(velo);
            painter->drawEllipse(pos, r, r);
            continue;
        }
        const QPolygonF poly = screenPolygonOf(parte);
        if (poly.size() < 2)
            continue;
        QPen pen(borde, f.style.lineWidth + 6.0);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        if (f.kind == GeometryKind::Polygon) {
            painter->setBrush(velo);
            painter->drawPolygon(poly);
        } else {
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(poly);
        }
    }
}

// Fija las entidades en alerta. Solo invalida el dibujo si el conjunto cambia:
// con objetivos moviendose se llama a menudo con el mismo conjunto.
void FeatureLayer::setAlerted(const QSet<qint64> &ids)
{
    if (ids == m_alerted)
        return;
    m_alerted = ids;
    markDirty();
}

// Cambia el color del resalte de alerta y redibuja.
void FeatureLayer::setAlertColor(const QColor &color)
{
    m_alertColor = color;
    markDirty();
}

// Cambia que lineas marcan su inicio y su fin, y redibuja.
void FeatureLayer::setRouteEndpoints(RouteEndpoints mode)
{
    if (mode == m_endpoints)
        return;
    m_endpoints = mode;
    markDirty();
}

// Lee una hora ISO 8601 de un atributo (con o sin milisegundos y con zona),
// como la escribe la lectura de GPX. Invalida si no hay o no se entiende.
static QDateTime horaIso(const QVariant &v)
{
    const QString txt = v.toString().trimmed();
    if (txt.isEmpty())
        return QDateTime();
    const QDateTime t = QDateTime::fromString(txt, Qt::ISODateWithMs);
    return t.isValid() ? t : QDateTime::fromString(txt, Qt::ISODate);
}

// Hora de un extremo de ruta para el rotulo, en hora local; con fecha si
// \a conFecha (no es de hoy, o la ruta cruza de dia).
static QString textoHora(const QDateTime &t, bool conFecha)
{
    if (!t.isValid())
        return QString();
    return t.toLocalTime().toString(conFecha ? QStringLiteral("dd/MM/yyyy HH:mm:ss")
                                             : QStringLiteral("HH:mm:ss"));
}

// Inicio (circulo verde) y fin (cuadrado rojo) de una linea, segun el modo:
// la seleccionada siempre que no esten ocultos, las rutas GPX (atributo
// gpx_kind) desde Routes, y todas con AllLines. La seleccionada lleva ademas
// los rotulos "inicio"/"fin" con su hora, para saber de un vistazo por donde
// empezo y acabo un recorrido (p. ej. un historial de objetivo importado).
void FeatureLayer::drawEndpoints(QPainter *painter, const MapFeature &f, bool selected) const
{
    const bool ruta = f.attributes.contains(QStringLiteral("gpx_kind"));
    const bool toca = m_endpoints != RouteEndpoints::Hidden
        && (selected || m_endpoints == RouteEndpoints::AllLines
            || (ruta && m_endpoints == RouteEndpoints::Routes));
    if (!toca)
        return;
    const auto partes = f.outlines();
    if (partes.isEmpty() || partes.first().isEmpty() || partes.last().isEmpty())
        return;
    const QGeoCoordinate ini = partes.first().first();
    const QGeoCoordinate fin = partes.last().last();
    const QPointF pi = screenPos(ini), pf = screenPos(fin);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(Qt::white, 2.0));
    painter->setBrush(QColor(0x2e, 0x7d, 0x32));
    painter->drawEllipse(pi, 6.0, 6.0);
    painter->setBrush(QColor(0xc6, 0x28, 0x28));
    painter->drawRect(QRectF(pf.x() - 5.5, pf.y() - 5.5, 11.0, 11.0));

    if (selected) {
        // Fecha completa si no es de hoy o si inicio y fin son de dias distintos.
        const QDateTime ti = horaIso(f.attributes.value(QStringLiteral("time_start")));
        const QDateTime tf = horaIso(f.attributes.value(QStringLiteral("time_end")));
        const QDate hoy = QDate::currentDate();
        const bool conFecha = (ti.isValid() && ti.toLocalTime().date() != hoy)
                           || (tf.isValid() && tf.toLocalTime().date() != hoy);
        const QString hi = textoHora(ti, conFecha);
        const QString hf = textoHora(tf, conFecha);
        const QString txtIni = hi.isEmpty() ? tr("inicio") : tr("inicio %1").arg(hi);
        const QString txtFin = hf.isEmpty() ? tr("fin") : tr("fin %1").arg(hf);

        QFont fnt = painter->font();
        fnt.setPointSizeF(8.0);
        fnt.setBold(true);
        painter->setFont(fnt);
        const QFontMetrics fm(fnt);
        // Cada rotulo va hacia FUERA del recorrido (el de inicio por detras del
        // inicio, el de fin por delante del fin): asi no se pisan entre si ni
        // con la linea aunque la ruta sea corta en pantalla.
        const QPointF dir = pf - pi;
        const bool horizontal = std::abs(dir.x()) >= std::abs(dir.y());
        const auto rotulo = [&](const QPointF &p, const QString &txt, const QColor &c,
                                bool haciaAtras) {
            const double w = fm.horizontalAdvance(txt) + 8.0, h = fm.height() + 2.0;
            QPointF esquina;
            if (horizontal) {
                const bool izquierda = (dir.x() >= 0.0) == haciaAtras;
                esquina = QPointF(izquierda ? p.x() - 10.0 - w : p.x() + 10.0, p.y() - h / 2.0);
            } else {
                const bool arriba = (dir.y() >= 0.0) == haciaAtras;
                esquina = QPointF(p.x() - w / 2.0, arriba ? p.y() - 10.0 - h : p.y() + 10.0);
            }
            const QRectF caja(esquina, QSizeF(w, h));
            painter->setPen(QPen(c, 1.0));
            painter->setBrush(QColor(255, 255, 255, 225));
            painter->drawRoundedRect(caja, 3.0, 3.0);
            painter->setPen(c.darker(130));
            painter->drawText(caja, Qt::AlignCenter, txt);
        };
        rotulo(pi, txtIni, QColor(0x2e, 0x7d, 0x32), true);
        rotulo(pf, txtFin, QColor(0xc6, 0x28, 0x28), false);
    }
    painter->restore();
}

// Dibuja una parte de tipo LINEA o POLIGONO: relleno + contorno para poligonos,
// polilinea para lineas. Si esta seleccionada, primero traza un halo blanco mas
// grueso y luego la linea normal encima. Tiradores y etiqueta los pone
// drawFeature una sola vez (no por parte).
void FeatureLayer::drawPathPart(QPainter *painter, const MapFeature &f,
                                const QVector<QGeoCoordinate> &part,
                                bool selected) const
{
    const QPolygonF poly = screenPolygonOf(part);
    if (poly.size() < 2)
        return;

    if (f.kind == GeometryKind::Polygon) {
        QPainterPath camino;
        camino.addPolygon(poly);
        camino.closeSubpath();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(f.style.fillColor));
        painter->drawPath(camino);

        painter->setPen(QPen(f.style.lineColor, f.style.lineWidth,
                             f.style.lineStyle));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(camino);
    } else {
        painter->setPen(QPen(f.style.lineColor, f.style.lineWidth,
                             f.style.lineStyle));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(poly);
    }

    if (selected) {
        QPen resalte(Qt::white, f.style.lineWidth + 3);
        resalte.setJoinStyle(Qt::RoundJoin);
        painter->setPen(resalte);
        painter->setBrush(Qt::NoBrush);
        if (f.kind == GeometryKind::Polygon)
            painter->drawPolygon(poly);
        else
            painter->drawPolyline(poly);

        painter->setPen(QPen(f.style.lineColor, f.style.lineWidth));
        if (f.kind == GeometryKind::Polygon)
            painter->drawPolygon(poly);
        else
            painter->drawPolyline(poly);
    }
    // Los tiradores y la etiqueta los pinta drawFeature una sola vez, no por
    // parte.
}

// Dibuja los tiradores (cuadraditos blancos) sobre cada vertice de una entidad
// seleccionada/editable, para poder arrastrarlos.
void FeatureLayer::drawVertices(QPainter *painter, const MapFeature &f) const
{
    const QPolygonF poly = screenPolygon(f);
    painter->setPen(QPen(Qt::black, 1));
    painter->setBrush(QBrush(Qt::white));
    for (const QPointF &v : poly)
        painter->drawRect(QRectF(v.x() - 4, v.y() - 4, 8, 8));
}

// Dibuja la geometria "en construccion" mientras el usuario traza una entidad:
// los lados ya fijados (discontinuos), el lado de goma hasta el cursor (y el de
// cierre si es poligono) y un tirador en cada vertice puesto.
void FeatureLayer::drawDraft(QPainter *painter) const
{
    if (!m_hasDraft || m_draft.geometry.isEmpty())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPolygonF poly;
    for (const QGeoCoordinate &c : m_draft.geometry)
        poly.append(screenPos(c));

    QPen lapiz(m_draft.style.lineColor, m_draft.style.lineWidth);
    lapiz.setStyle(Qt::DashLine);
    painter->setPen(lapiz);
    painter->setBrush(Qt::NoBrush);

    if (poly.size() >= 2)
        painter->drawPolyline(poly);

    // El lado que se anadiria al soltar el proximo clic.
    if (m_draftCursorVisible && !poly.isEmpty()) {
        painter->drawLine(poly.last(), QPointF(m_draftCursor));
        if (m_draft.kind == GeometryKind::Polygon && poly.size() >= 2)
            painter->drawLine(QPointF(m_draftCursor), poly.first());
    } else if (m_draft.kind == GeometryKind::Polygon && poly.size() >= 3) {
        // Sin linea de goma (borrador en reposo o seleccion ya cerrada): se
        // cierra el poligono dibujando el lado ultimo->primero.
        painter->drawLine(poly.last(), poly.first());
    }

    // Los vertices ya puestos, como tiradores.
    painter->setPen(QPen(Qt::black, 1));
    painter->setBrush(QBrush(Qt::white));
    for (const QPointF &v : poly)
        painter->drawRect(QRectF(v.x() - 4, v.y() - 4, 8, 8));

    painter->restore();
}

// Punto del que cuelga la etiqueta (en pixeles): el vertice de anclaje si se
// fijo uno (p. ej. 0 para un area objetivo), o el punto, o el centro del
// rectangulo que contiene toda la geometria (estable al desplazar el mapa).
QPointF FeatureLayer::labelAnchorScreen(const MapFeature &f) const
{
    if (f.style.labelAnchorVertex >= 0) {
        const QVector<QVector<QGeoCoordinate>> partes = f.outlines();
        if (!partes.isEmpty()) {
            const QVector<QGeoCoordinate> &pts = partes.first();
            if (f.style.labelAnchorVertex < pts.size())
                return screenPos(pts.at(f.style.labelAnchorVertex));
        }
    }
    if (f.kind == GeometryKind::Point && !f.isMultiPart())
        return screenPos(f.position());

    QRectF caja;
    for (const QVector<QGeoCoordinate> &parte : f.outlines())
        caja = caja.united(screenPolygonOf(parte).boundingRect());
    return caja.isNull() ? QPointF() : caja.center();
}

// Caja de la etiqueta en pixeles. El rotulo se coloca en: anclaje + hueco por
// defecto + desplazamiento del estilo. El hueco separa el texto del punto; el
// desplazamiento es lo que cambia al arrastrar la etiqueta. Multilinea por '\n'.
QRectF FeatureLayer::labelBoxFor(const MapFeature &f, const QPointF &base,
                                 const QFontMetricsF &fm) const
{
    const QStringList lineas = f.name.split(QLatin1Char('\n'));
    double w = 0.0;
    for (const QString &l : lineas)
        w = std::max(w, fm.horizontalAdvance(l));
    const double h = fm.height() * std::max(1, int(lineas.size()));

    // Junto a un punto/vertice el rotulo va arriba-derecha; en el centro de una
    // geometria arranca en el propio centro (como venia saliendo).
    const bool junar = (f.kind == GeometryKind::Point && !f.isMultiPart())
                       || f.style.labelAnchorVertex >= 0;
    const QPointF hueco = junar
        ? QPointF(f.style.pointRadiusPx + 4.0, -f.style.pointRadiusPx - 2.0)
        : QPointF(0.0, 0.0);

    const QPointF tl = base + hueco + f.style.labelOffsetPx;
    return QRectF(tl, QSizeF(w, h)).adjusted(-3, -2, 3, 2);
}

// Dibuja la etiqueta (nombre) de una entidad colgando de 'base', con un fondo
// claro redondeado para que se lea sobre fotografia aerea. Si la etiqueta esta
// desplazada, traza una LINEA GUIA del anclaje a la caja. Multilinea por '\n'.
void FeatureLayer::drawLabel(QPainter *painter, const MapFeature &f,
                             const QPointF &base) const
{
    const QFontMetricsF fm(painter->font());
    const QRectF caja = labelBoxFor(f, base, fm);

    // Linea guia solo cuando el rotulo esta separado del punto: une anclaje y caja.
    if (f.style.labelOffsetPx.manhattanLength() > 1.0) {
        painter->setPen(QPen(f.style.labelColor, 1.0, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(base, caja.center());
    }

    // Fondo semitransparente: sobre fotografia aerea el texto suelto no se lee.
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(QColor(255, 255, 255, 190)));
    painter->drawRoundedRect(caja, 3, 3);

    painter->setPen(QPen(f.style.labelColor));
    painter->drawText(caja, Qt::AlignCenter, f.name);
}

// Entidad cuyo rotulo cae bajo el pixel. Recorre de la capa mas alta a la mas
// baja (lo de encima gana) y respeta visibilidad y rango de zoom, como featureAt.
qint64 FeatureLayer::labelAt(const QPoint &pixel) const
{
    if (!m_model || !m_toAxis)
        return -1;

    const MapView *vista = qobject_cast<MapView *>(parentPlot());
    const int zoomActual = vista ? vista->zoom() : -1;
    const QFontMetricsF fm(parentPlot() ? parentPlot()->font() : QFont());

    const auto capas = m_model->layers();
    for (int i = static_cast<int>(capas.size()) - 1; i >= 0; --i) {
        if (!capas[i].visible)
            continue;
        if (zoomActual >= 0 && !capaVisibleAZoom(capas[i], zoomActual))
            continue;
        for (const MapFeature &f : m_model->featuresInLayer(capas[i].id)) {
            if (!f.visible || !f.selectable || !f.style.labelVisible || f.name.isEmpty())
                continue;
            const QPointF base = labelAnchorScreen(f);
            if (base.isNull())
                continue;
            if (labelBoxFor(f, base, fm).contains(QPointF(pixel)))
                return f.id;
        }
    }
    return -1;
}

// ----------------------------------------------------------- localizacion --

// Distancia (en pixeles) de un punto p al segmento a-b: proyecta p sobre la recta,
// la acota al segmento [a,b] y mide. Base de todas las pruebas de "cerca de".
double FeatureLayer::distanceToSegment(const QPointF &p, const QPointF &a,
                                       const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0.0)
        return std::hypot(p.x() - a.x(), p.y() - a.y());

    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = qBound(0.0, t, 1.0);
    const QPointF proy(a.x() + t * ab.x(), a.y() + t * ab.y());
    return std::hypot(p.x() - proy.x(), p.y() - proy.y());
}

// Entidad "bajo el cursor": id de la entidad seleccionable mas cercana al pixel
// dentro de la tolerancia, o -1. Recorre de la capa mas alta a la mas baja (lo de
// arriba gana) y prueba parte a parte; para poligonos, caer DENTRO del area es un
// acierto directo. En cuanto una capa da un acierto, no baja mas.
qint64 FeatureLayer::featureAt(const QPoint &pixel, double tolerancePx) const
{
    if (!m_model)
        return -1;

    const QPointF p(pixel);
    qint64 mejor = -1;
    double mejorDist = tolerancePx;

    const MapView *vista = qobject_cast<MapView *>(parentPlot());
    const int zoomActual = vista ? vista->zoom() : -1;
    const QRectF vista_ = visibleAxisRect();      // recorte por viewport

    // Se recorre de la capa mas alta a la mas baja: lo que se ve encima es lo
    // que se selecciona.
    const auto capas = m_model->layers();
    for (int i = static_cast<int>(capas.size()) - 1; i >= 0; --i) {
        if (!capas[i].visible)
            continue;
        if (zoomActual >= 0 && !capaVisibleAZoom(capas[i], zoomActual))
            continue;               // no seleccionable fuera de su rango de zoom

        for (const MapFeature &f : m_model->featuresInLayer(capas[i].id)) {
            if (!f.visible || !f.selectable)
                continue;
            if (!featureInView(f, vista_))
                continue;               // fuera de pantalla: no hace falta probar

            double d = std::numeric_limits<double>::max();

            // Se prueba parte a parte: en una entidad multi-parte basta que el
            // cursor caiga cerca de cualquiera de sus trazados.
            for (const QVector<QGeoCoordinate> &parte : f.outlines()) {
                if (f.kind == GeometryKind::Point) {
                    if (parte.isEmpty())
                        continue;
                    const QPointF c = screenPos(parte.first());
                    if (!f.style.icon.isNull()) {
                        // Con icono, el area sensible es TODO el pixmap (anclado),
                        // no solo la punta: asi se puede agarrar el pin entero.
                        const double dpr = f.style.icon.devicePixelRatio();
                        const double w = f.style.icon.width() / (dpr > 0 ? dpr : 1.0);
                        const double h = f.style.icon.height() / (dpr > 0 ? dpr : 1.0);
                        const QRectF caja(QPointF(c.x() - f.style.iconAnchor.x() * w,
                                                  c.y() - f.style.iconAnchor.y() * h),
                                          QSizeF(w, h));
                        const double dx = qMax(qMax(caja.left() - p.x(), 0.0), p.x() - caja.right());
                        const double dy = qMax(qMax(caja.top() - p.y(), 0.0), p.y() - caja.bottom());
                        d = qMin(d, std::hypot(dx, dy));
                    } else {
                        d = qMin(d, qMax(0.0, std::hypot(p.x() - c.x(),
                                                         p.y() - c.y())
                                              - f.style.pointRadiusPx));
                    }
                    continue;
                }
                const QPolygonF poly = screenPolygonOf(parte);
                if (f.kind == GeometryKind::Polygon
                    && poly.containsPoint(p, Qt::OddEvenFill)) {
                    d = 0.0;      // dentro del area cuenta como acierto
                    break;
                }
                for (int j = 0; j + 1 < static_cast<int>(poly.size()); ++j)
                    d = qMin(d, distanceToSegment(p, poly[j], poly[j + 1]));
                if (f.kind == GeometryKind::Polygon && poly.size() > 2)
                    d = qMin(d, distanceToSegment(p, poly.last(), poly.first()));
            }

            if (d <= mejorDist) {
                mejorDist = d;
                mejor = f.id;
            }
        }

        // Si algo se acerto en esta capa, no se sigue bajando.
        if (mejor >= 0)
            break;
    }

    return mejor;
}

// Indice del segmento (lado) de una entidad mas cercano al pixel dentro de la
// tolerancia, o -1. Sirve para insertar un vertice donde el usuario pincha. En un
// poligono, el lado de cierre tambien cuenta.
int FeatureLayer::segmentAt(const MapFeature &feature, const QPoint &pixel,
                            double tolerancePx) const
{
    const QPolygonF poly = screenPolygon(feature);
    if (poly.size() < 2)
        return -1;

    const QPointF p(pixel);
    int mejor = -1;
    double mejorDist = tolerancePx;

    for (int i = 0; i + 1 < static_cast<int>(poly.size()); ++i) {
        const double d = distanceToSegment(p, poly[i], poly[i + 1]);
        if (d <= mejorDist) {
            mejorDist = d;
            mejor = i;
        }
    }

    // En un poligono, el lado de cierre tambien cuenta.
    if (feature.kind == GeometryKind::Polygon && poly.size() > 2) {
        const double d = distanceToSegment(p, poly.last(), poly.first());
        if (d <= mejorDist)
            mejor = static_cast<int>(poly.size()) - 1;
    }

    return mejor;
}

// Indice del vertice de una entidad mas cercano al pixel dentro de la tolerancia,
// o -1. Sirve para saber que tirador esta agarrando el usuario.
int FeatureLayer::vertexAt(qint64 featureId, const QPoint &pixel,
                           double tolerancePx) const
{
    if (!m_model)
        return -1;
    const auto f = m_model->feature(featureId);
    if (!f)
        return -1;

    const QPointF p(pixel);
    int mejor = -1;
    double mejorDist = tolerancePx;

    const QPolygonF poly = screenPolygon(*f);
    for (int i = 0; i < static_cast<int>(poly.size()); ++i) {
        const double d = std::hypot(p.x() - poly[i].x(), p.y() - poly[i].y());
        if (d <= mejorDist) {
            mejorDist = d;
            mejor = i;
        }
    }
    return mejor;
}

} // namespace libmapa
