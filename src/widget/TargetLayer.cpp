#include "widget/TargetLayer.h"

#include <QPainter>
#include <QPolygonF>
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

void TargetLayer::setAxisMapper(
    std::function<QPointF(const QGeoCoordinate &)> toAxis)
{
    m_toAxis = std::move(toAxis);
    programarRepintado();
}

void TargetLayer::programarRepintado()
{
    if (!m_repintar.isActive())
        m_repintar.start();
}

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

void TargetLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

void TargetLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot || !m_model)
        return;

    const QRect area = plot->viewport();
    m_lastDrawn = 0;
    for (const TargetModel::Entry &e : m_model->entries())
        drawTarget(painter, e, area);
}

void TargetLayer::drawTarget(QPainter *painter, const TargetModel::Entry &e,
                             const QRect &area) const
{
    const MapTarget &t = e.target;
    if (!t.position.isValid())
        return;

    const QPointF pos = screenPos(t.position);

    // Culling: si el objetivo cae muy lejos del area visible se salta entero
    // (traza incluida). El margen deja que una traza que asoma siga viendose.
    const QRectF margen = QRectF(area).adjusted(-256, -256, 256, 256);
    if (!margen.contains(pos))
        return;
    ++m_lastDrawn;

    // --- Traza -------------------------------------------------------------
    if (t.trailVisible && e.trail.size() >= 2) {
        QPolygonF linea;
        linea.reserve(e.trail.size());
        for (const QGeoCoordinate &c : e.trail)
            linea.append(screenPos(c));

        QColor colorTraza = t.color;
        colorTraza.setAlpha(140);
        QPen pen(colorTraza);
        pen.setWidthF(1.5);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        // La traza sin suavizado: con cientos de objetivos es donde mas se nota.
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->drawPolyline(linea);
        painter->setRenderHint(QPainter::Antialiasing, true);
    }

    // --- Simbolo, orientado por el rumbo -----------------------------------
    // Un galon (chevron) que apunta al norte y se gira en sentido horario
    // segun el rumbo. En pantalla la Y crece hacia abajo, y QPainter::rotate
    // gira en horario para angulos positivos: el norte queda arriba.
    const double r = m_symbolPx;
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

    // --- Etiqueta ----------------------------------------------------------
    if (t.labelVisible && !t.label.isEmpty()) {
        const QPointF anchor = pos + QPointF(r + 3.0, r * 0.5);
        // Un halo claro detras del texto para que se lea sobre el mapa.
        painter->setPen(QPen(QColor(255, 255, 255, 200)));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy)
                    painter->drawText(anchor + QPointF(dx, dy), t.label);
        painter->setPen(Qt::black);
        painter->drawText(anchor, t.label);
    }
}

} // namespace libmapa
