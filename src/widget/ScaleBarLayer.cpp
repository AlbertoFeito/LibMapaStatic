#include "widget/ScaleBarLayer.h"

#include "geo/MapDecor.h"
#include "geo/TileMatrix.h"

#include <QGeoCoordinate>

namespace libmapa {

namespace {
constexpr double kAnchoMaxPx = 120.0;   // la barra nunca pasa de aqui
constexpr double kMargen = 10.0;        // separacion de los bordes de la vista
} // namespace

// Bordes de la barra nitidos: sin antialiasing.
ScaleBarLayer::ScaleBarLayer(QCustomPlot *parent)
    : QCPLayerable(parent)
{
    setAntialiased(false);
}

ScaleBarLayer::~ScaleBarLayer() = default;

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void ScaleBarLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Resolucion del mapa en el centro de la vista: distancia geodesica entre dos
// puntos separados 100 px en horizontal, dividida entre 100. Medir la
// distancia real (y no derivarla del zoom) la hace correcta a cualquier
// latitud y con cualquier relacion de aspecto.
double ScaleBarLayer::metersPerPixelAtCenter() const
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return 0.0;
    const QRect vp = plot->viewport();
    if (vp.width() < 2 || vp.height() < 2)
        return 0.0;
    const QPointF c = QRectF(vp).center();
    const double lat = TileMatrix::axisYToLatitude(plot->yAxis->pixelToCoord(c.y()));
    const QGeoCoordinate a(lat, plot->xAxis->pixelToCoord(c.x() - 50.0));
    const QGeoCoordinate b(lat, plot->xAxis->pixelToCoord(c.x() + 50.0));
    if (!a.isValid() || !b.isValid())
        return 0.0;
    return a.distanceTo(b) / 100.0;
}

// Pinta la escala abajo a la izquierda: caja clara, rotulo encima y una barra
// partida en dos mitades (negra y blanca) con borde, como en las cartas.
void ScaleBarLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return;
    const MapDecor::ScaleBar barra =
        MapDecor::scaleBar(metersPerPixelAtCenter(), kAnchoMaxPx, m_units);
    if (barra.pixels < 1.0)
        return;

    const QRect vp = plot->viewport();
    painter->save();
    painter->setClipRect(vp);

    QFont fuente = painter->font();
    fuente.setPointSizeF(8.5);
    fuente.setBold(true);
    painter->setFont(fuente);
    const QFontMetrics fm(fuente);

    const double altoBarra = 6.0;
    const double ancho = qMax(barra.pixels, double(fm.horizontalAdvance(barra.label))) + 12.0;
    const double alto = fm.height() + altoBarra + 10.0;
    const QRectF caja(vp.left() + kMargen, vp.bottom() - kMargen - alto, ancho, alto);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 200));
    painter->drawRect(caja);

    const double x0 = caja.left() + 6.0;
    const double yBarra = caja.bottom() - 5.0 - altoBarra;
    painter->setPen(QColor(20, 20, 20));
    painter->drawText(QRectF(x0, caja.top() + 2.0, ancho, fm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, barra.label);

    const double mitad = barra.pixels / 2.0;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(20, 20, 20));
    painter->drawRect(QRectF(x0, yBarra, mitad, altoBarra));
    painter->setBrush(Qt::white);
    painter->drawRect(QRectF(x0 + mitad, yBarra, barra.pixels - mitad, altoBarra));
    painter->setPen(QPen(QColor(20, 20, 20), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(x0, yBarra, barra.pixels, altoBarra));

    painter->restore();
}

} // namespace libmapa
