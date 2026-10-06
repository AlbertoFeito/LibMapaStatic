#include "widget/HillshadeLayer.h"

#include "geo/TileMatrix.h"

namespace libmapa {

// La imagen ya viene suavizada del calculo; al estirarla conviene interpolar.
HillshadeLayer::HillshadeLayer(QCustomPlot *parent)
    : QCPLayerable(parent)
{
    setAntialiased(true);
}

HillshadeLayer::~HillshadeLayer() = default;

// Guarda la imagen y su extension geografica. No repinta: QCustomPlot llamara a
// draw() en el proximo replot (MapWidget lo fuerza tras recalcular).
void HillshadeLayer::setImage(const QImage &img, double lonWest, double lonEast,
                              double latNorth, double latSouth,
                              QPainter::CompositionMode mode, double opacity)
{
    m_img = img;
    m_lonW = lonWest; m_lonE = lonEast;
    m_latN = latNorth; m_latS = latSouth;
    m_mode = mode;
    m_opacity = opacity;
}

// Deja la capa sin imagen (no dibuja nada en el proximo repintado).
void HillshadeLayer::clear()
{
    m_img = QImage();
}

void HillshadeLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Pinta la imagen estirada entre los pixeles de sus esquinas. El rectangulo se
// obtiene con las MISMAS transformaciones que las teselas (x = longitud lineal,
// y = grados de Mercator), asi que queda alineado con la base.
void HillshadeLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot || m_img.isNull())
        return;

    const double left   = plot->xAxis->coordToPixel(m_lonW);
    const double right  = plot->xAxis->coordToPixel(m_lonE);
    const double top    = plot->yAxis->coordToPixel(TileMatrix::latitudeToAxisY(m_latN));
    const double bottom = plot->yAxis->coordToPixel(TileMatrix::latitudeToAxisY(m_latS));
    const QRectF dest = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
    if (dest.width() < 1.0 || dest.height() < 1.0)
        return;

    painter->save();
    painter->setClipRect(plot->viewport());
    painter->setOpacity(m_opacity);
    painter->setCompositionMode(m_mode);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(dest, m_img, QRectF(m_img.rect()));
    painter->restore();
}

} // namespace libmapa
