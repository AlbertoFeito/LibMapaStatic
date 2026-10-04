#include "widget/CoverageLayer.h"

#include "geo/TileMatrix.h"

namespace libmapa {

// No se suaviza: son rectangulos alineados a la rejilla; el antialiasing solo
// emborronaria los bordes.
CoverageLayer::CoverageLayer(QCustomPlot *parent)
    : QCPLayerable(parent)
{
    setAntialiased(false);
}

CoverageLayer::~CoverageLayer() = default;

// Guarda los datos a pintar. No repinta: QCustomPlot llamara a draw() en el
// proximo replot (MapWidget lo fuerza tras actualizar).
void CoverageLayer::setData(int targetZoom, int summaryZoom,
                            const QVector<Cell> &cells)
{
    m_zt = targetZoom;
    m_zs = summaryZoom;
    m_cells = cells;
}

// Deja la capa sin datos (no dibuja nada en el proximo repintado).
void CoverageLayer::clearData()
{
    m_cells.clear();
}

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void CoverageLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Rectangulo EN PIXELES de una celda resumen. Identico a TileLayer::screenRectFor
// pero al zoom resumen: en coordenadas de EJE (x = longitud, y = grados de
// Mercator), que son lineales en el indice de tesela, para que no se deforme.
QRectF CoverageLayer::screenRectFor(int sx, int sy, int z) const
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return {};

    const double lonWest = TileMatrix::tileXToLongitude(sx, z);
    const double lonEast = TileMatrix::tileXToLongitude(sx + 1, z);
    const double mercTop = TileMatrix::tileYToMercatorDegrees(sy, z);
    const double mercBot = TileMatrix::tileYToMercatorDegrees(sy + 1, z);

    const double left   = plot->xAxis->coordToPixel(lonWest);
    const double right  = plot->xAxis->coordToPixel(lonEast);
    const double top    = plot->yAxis->coordToPixel(mercTop);
    const double bottom = plot->yAxis->coordToPixel(mercBot);

    return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
}

// Pinta la mancha: una celda traslucida por cada zona con teselas del zoom
// objetivo, con color interpolado ambar(poco)->verde(lleno) segun su fraccion.
// Las celdas grandes llevan ademas un borde fino para distinguirlas, y arriba a
// la izquierda una leyenda con el zoom objetivo.
void CoverageLayer::draw(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot || m_cells.isEmpty() || m_zs < 0)
        return;

    const QRect clip = plot->viewport();
    painter->save();
    painter->setClipRect(clip);

    const QColor ambar(0xff, 0xb3, 0x00);   // celda casi vacia
    const QColor verde(0x2e, 0x7d, 0x32);   // celda llena

    painter->setPen(Qt::NoPen);
    for (const Cell &c : m_cells) {
        const QRectF r = screenRectFor(c.sx, c.sy, m_zs);
        if (!r.intersects(QRectF(clip)))
            continue;
        const double f = qBound(0.0, double(c.frac), 1.0);
        const QColor col(
            int(ambar.red()   + (verde.red()   - ambar.red())   * f),
            int(ambar.green() + (verde.green() - ambar.green()) * f),
            int(ambar.blue()  + (verde.blue()  - ambar.blue())  * f),
            110);
        painter->fillRect(r, col);
    }

    // Borde fino solo en celdas suficientemente grandes, para no ensuciar
    // cuando se ven diminutas a zoom muy bajo.
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(0, 0, 0, 60), 1));
    for (const Cell &c : m_cells) {
        const QRectF r = screenRectFor(c.sx, c.sy, m_zs);
        if (!r.intersects(QRectF(clip)) || r.width() < 6.0 || r.height() < 6.0)
            continue;
        painter->drawRect(r);
    }

    // Leyenda con el zoom objetivo, sobre una banda semitransparente.
    const QString txt = tr("Cobertura z%1").arg(m_zt);
    QFont fuente = painter->font();
    fuente.setBold(true);
    painter->setFont(fuente);
    const QRectF caja(clip.left() + 10, clip.top() + 10, 150, 20);
    painter->fillRect(caja.adjusted(-5, -3, 5, 3), QColor(0, 0, 0, 130));
    painter->setPen(QColor(255, 255, 255, 235));
    painter->drawText(caja, Qt::AlignLeft | Qt::AlignVCenter, txt);

    painter->restore();
}

} // namespace libmapa
