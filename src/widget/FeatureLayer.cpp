#include "widget/FeatureLayer.h"

#include "core/Logging.h"

#include <QPainterPath>
#include <cmath>

namespace libmapa {

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

    // Las capas se recorren en orden de zOrder: la ultima queda encima.
    for (const LayerInfo &capa : m_model->layers()) {
        if (!capa.visible)
            continue;
        for (const MapFeature &f : m_model->featuresInLayer(capa.id)) {
            if (!f.visible)
                continue;
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

    if (f.style.labelVisible && !f.name.isEmpty()) {
        if (f.kind == GeometryKind::Point && !f.isMultiPart()) {
            const QPointF pos = screenPos(f.position());
            const double r = f.style.pointRadiusPx;
            drawLabel(painter, f, pos + QPointF(r + 4, -r - 2));
        } else {
            // La etiqueta va en el centro del rectangulo que contiene TODA la
            // geometria, estable al desplazar el mapa.
            QRectF caja;
            for (const QVector<QGeoCoordinate> &parte : partes)
                caja = caja.united(screenPolygonOf(parte).boundingRect());
            if (!caja.isNull())
                drawLabel(painter, f, caja.center());
        }
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
        const QSize s = f.style.icon.size();
        painter->drawPixmap(QPointF(pos.x() - s.width() / 2.0,
                                    pos.y() - s.height() / 2.0),
                            f.style.icon);
    } else {
        painter->setPen(QPen(f.style.lineColor, f.style.lineWidth));
        painter->setBrush(QBrush(f.style.fillColor));
        painter->drawEllipse(pos, r, r);
    }

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

// Dibuja la etiqueta (nombre) de una entidad en 'anchor', con un fondo claro
// redondeado detras para que se lea sobre fotografia aerea.
void FeatureLayer::drawLabel(QPainter *painter, const MapFeature &f,
                             const QPointF &anchor) const
{
    const QFontMetricsF fm(painter->font());
    const QRectF caja = fm.boundingRect(f.name).adjusted(-3, -2, 3, 2)
                            .translated(anchor);

    // Fondo semitransparente: sobre fotografia aerea el texto suelto no se
    // lee. Es el mismo motivo por el que las etiquetas de OSM llevan halo.
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(QColor(255, 255, 255, 190)));
    painter->drawRoundedRect(caja, 3, 3);

    painter->setPen(QPen(f.style.labelColor));
    painter->drawText(caja, Qt::AlignCenter, f.name);
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

    // Se recorre de la capa mas alta a la mas baja: lo que se ve encima es lo
    // que se selecciona.
    const auto capas = m_model->layers();
    for (int i = static_cast<int>(capas.size()) - 1; i >= 0; --i) {
        if (!capas[i].visible)
            continue;

        for (const MapFeature &f : m_model->featuresInLayer(capas[i].id)) {
            if (!f.visible || !f.selectable)
                continue;

            double d = std::numeric_limits<double>::max();

            // Se prueba parte a parte: en una entidad multi-parte basta que el
            // cursor caiga cerca de cualquiera de sus trazados.
            for (const QVector<QGeoCoordinate> &parte : f.outlines()) {
                if (f.kind == GeometryKind::Point) {
                    if (parte.isEmpty())
                        continue;
                    const QPointF c = screenPos(parte.first());
                    d = qMin(d, qMax(0.0, std::hypot(p.x() - c.x(),
                                                     p.y() - c.y())
                                          - f.style.pointRadiusPx));
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
