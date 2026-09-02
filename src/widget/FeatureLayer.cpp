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

void FeatureLayer::setAxisMapper(
    std::function<QPointF(const QGeoCoordinate &)> toAxis)
{
    m_toAxis = std::move(toAxis);
    markDirty();
}

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

void FeatureLayer::setDraftCursor(const QPoint &pixel, bool visible)
{
    m_draftCursor = pixel;
    m_draftCursorVisible = visible;
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

void FeatureLayer::markDirty()
{
    m_dirty = true;
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

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

QPolygonF FeatureLayer::screenPolygon(const MapFeature &f) const
{
    QPolygonF poly;
    poly.reserve(f.geometry.size());
    for (const QGeoCoordinate &c : f.geometry)
        poly.append(screenPos(c));
    return poly;
}

void FeatureLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

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

void FeatureLayer::drawFeature(QPainter *painter, const MapFeature &f,
                               bool selected) const
{
    switch (f.kind) {
    case GeometryKind::Point:
        drawPoint(painter, f, selected);
        break;
    case GeometryKind::Polyline:
    case GeometryKind::Polygon:
        drawPath(painter, f, selected);
        break;
    }
}

void FeatureLayer::drawPoint(QPainter *painter, const MapFeature &f,
                             bool selected) const
{
    const QPointF pos = screenPos(f.position());
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

    if (f.style.labelVisible && !f.name.isEmpty())
        drawLabel(painter, f, pos + QPointF(r + 4, -r - 2));
}

void FeatureLayer::drawPath(QPainter *painter, const MapFeature &f,
                            bool selected) const
{
    const QPolygonF poly = screenPolygon(f);
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

    if (selected || f.style.verticesVisible)
        drawVertices(painter, f);

    if (f.style.labelVisible && !f.name.isEmpty()) {
        // La etiqueta va en el centro del rectangulo que contiene la
        // geometria, que es estable al desplazar el mapa.
        drawLabel(painter, f, poly.boundingRect().center());
    }
}

void FeatureLayer::drawVertices(QPainter *painter, const MapFeature &f) const
{
    const QPolygonF poly = screenPolygon(f);
    painter->setPen(QPen(Qt::black, 1));
    painter->setBrush(QBrush(Qt::white));
    for (const QPointF &v : poly)
        painter->drawRect(QRectF(v.x() - 4, v.y() - 4, 8, 8));
}

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
    }

    // Los vertices ya puestos, como tiradores.
    painter->setPen(QPen(Qt::black, 1));
    painter->setBrush(QBrush(Qt::white));
    for (const QPointF &v : poly)
        painter->drawRect(QRectF(v.x() - 4, v.y() - 4, 8, 8));

    painter->restore();
}

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

            if (f.kind == GeometryKind::Point) {
                const QPointF c = screenPos(f.position());
                d = std::hypot(p.x() - c.x(), p.y() - c.y())
                    - f.style.pointRadiusPx;
                d = qMax(0.0, d);
            } else {
                const QPolygonF poly = screenPolygon(f);
                if (f.kind == GeometryKind::Polygon
                    && poly.containsPoint(p, Qt::OddEvenFill)) {
                    d = 0.0;      // dentro del area cuenta como acierto
                } else {
                    for (int j = 0; j + 1 < static_cast<int>(poly.size()); ++j)
                        d = qMin(d, distanceToSegment(p, poly[j], poly[j + 1]));
                    if (f.kind == GeometryKind::Polygon && poly.size() > 2)
                        d = qMin(d, distanceToSegment(p, poly.last(), poly.first()));
                }
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
