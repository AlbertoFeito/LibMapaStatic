#include "widget/GraticuleLayer.h"

#include "geo/MapDecor.h"
#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"

#include <cmath>

namespace libmapa {

namespace {

// Tope de lineas por eje: con el paso elegido no deberia acercarse, pero
// protege de un rango de eje absurdo (p.ej. antes de la primera maquetacion).
constexpr int kMaxLineas = 400;

// Dibuja un rotulo sobre una caja clara semitransparente, para que se lea
// encima de cualquier tesela (mar, ciudad o satelite).
void rotulo(QCPPainter *painter, const QPointF &esquina, const QString &txt,
            const QColor &color)
{
    const QFontMetrics fm(painter->font());
    const QRectF caja(esquina, QSizeF(fm.horizontalAdvance(txt) + 6, fm.height() + 2));
    painter->fillRect(caja, QColor(255, 255, 255, 185));
    painter->setPen(QColor(color.red(), color.green(), color.blue(), 255));
    painter->drawText(caja, Qt::AlignCenter, txt);
}

} // namespace

// Lineas nitidas de 1 px: sin antialiasing (el texto lo pone Qt igualmente).
GraticuleLayer::GraticuleLayer(QCustomPlot *parent)
    : QCPLayerable(parent)
{
    setAntialiased(false);
}

GraticuleLayer::~GraticuleLayer() = default;

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void GraticuleLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Pinta meridianos y paralelos del tramo visible. El paso sale del lado que
// mas lineas pediria (ancho o alto), para que ninguno de los dos se sature, y
// es el mismo en los dos ejes (cuadricula homogenea). Luego rotula: longitudes
// en el borde superior, latitudes en el izquierdo, saltando la esquina donde
// se pisarian.
void GraticuleLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return;
    const QRect vp = plot->viewport();
    if (vp.width() < 20 || vp.height() < 20)
        return;

    const QCPRange rx = plot->xAxis->range();
    const QCPRange ry = plot->yAxis->range();
    const double lonMin = rx.lower, lonMax = rx.upper;
    const double latMin = qMax(-WebMercator::kMaxLatitude, TileMatrix::axisYToLatitude(ry.lower));
    const double latMax = qMin(WebMercator::kMaxLatitude, TileMatrix::axisYToLatitude(ry.upper));
    if (!(lonMax > lonMin) || !(latMax > latMin))
        return;

    const double paso = qMax(
        MapDecor::graticuleStep(lonMax - lonMin, qMax(2, vp.width() / 140)),
        MapDecor::graticuleStep(latMax - latMin, qMax(2, vp.height() / 110)));

    const long long k0x = static_cast<long long>(std::ceil(lonMin / paso));
    const long long k1x = static_cast<long long>(std::floor(lonMax / paso));
    const long long k0y = static_cast<long long>(std::ceil(latMin / paso));
    const long long k1y = static_cast<long long>(std::floor(latMax / paso));
    if (k1x - k0x > kMaxLineas || k1y - k0y > kMaxLineas)
        return;

    painter->save();
    painter->setClipRect(vp);
    painter->setPen(QPen(m_color, 1));

    for (long long k = k0x; k <= k1x; ++k) {
        const double x = std::round(plot->xAxis->coordToPixel(double(k) * paso));
        painter->drawLine(QPointF(x, vp.top()), QPointF(x, vp.bottom()));
    }
    for (long long k = k0y; k <= k1y; ++k) {
        const double lat = double(k) * paso;
        const double y = std::round(plot->yAxis->coordToPixel(TileMatrix::latitudeToAxisY(lat)));
        painter->drawLine(QPointF(vp.left(), y), QPointF(vp.right(), y));
    }

    // Rotulos. La columna de latitudes ocupa ~70 px a la izquierda y la fila de
    // longitudes ~18 px arriba: lo que caeria en esa esquina se omite. Abajo a
    // la izquierda va la escala grafica: tampoco se rotulan latitudes ahi.
    QFont fuente = painter->font();
    fuente.setPointSizeF(8.0);
    painter->setFont(fuente);
    const int altoFila = QFontMetrics(fuente).height() + 4;
    for (long long k = k0x; k <= k1x; ++k) {
        const double lon = double(k) * paso;
        const double x = plot->xAxis->coordToPixel(lon);
        if (x < vp.left() + 72 || x > vp.right() - 20)
            continue;
        rotulo(painter, QPointF(x + 2, vp.top() + 2),
               MapDecor::formatLongitude(lon, paso), m_color);
    }
    for (long long k = k0y; k <= k1y; ++k) {
        const double lat = double(k) * paso;
        const double y = plot->yAxis->coordToPixel(TileMatrix::latitudeToAxisY(lat));
        if (y < vp.top() + altoFila + 4 || y > vp.bottom() - 50)
            continue;
        rotulo(painter, QPointF(vp.left() + 2, y - altoFila + 2),
               MapDecor::formatLatitude(lat, paso), m_color);
    }

    painter->restore();
}

} // namespace libmapa
