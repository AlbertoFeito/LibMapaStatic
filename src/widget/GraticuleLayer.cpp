#include "widget/GraticuleLayer.h"

#include "libmapa/Coordinates.h"
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

// Gancho de QCustomPlot: dibuja la cuadricula del modo elegido.
void GraticuleLayer::draw(QCPPainter *painter)
{
    if (m_mode == GraticuleMode::Utm)
        drawUtm(painter);
    else
        drawGeographic(painter);
}

// Cuadricula geografica: meridianos y paralelos del tramo visible. El paso sale
// del lado que mas lineas pediria (ancho o alto), para que ninguno de los dos se
// sature, y es el mismo en los dos ejes (cuadricula homogenea). Luego rotula:
// longitudes en el borde superior, latitudes en el izquierdo, saltando la
// esquina donde se pisarian.
void GraticuleLayer::drawGeographic(QCPPainter *painter)
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

namespace {

// (este, norte "crudo") de una coordenada en una zona UTM dada: el norte va SIN
// falso norte al sur (negativo), para que las lineas crucen el ecuador sin
// salto. Falso si la latitud cae fuera de UTM.
bool utmCrudo(double lat, double lon, int zona, double &e, double &y)
{
    const UtmCoordinate u = toUtm(QGeoCoordinate(lat, lon), zona);
    if (!u.isValid())
        return false;
    e = u.easting;
    y = u.north ? u.northing : u.northing - 10000000.0;
    return true;
}

// Inversa de utmCrudo.
QGeoCoordinate desdeUtmCrudo(double e, double y, int zona)
{
    UtmCoordinate u;
    u.zone = zona;
    u.north = y >= 0.0;
    u.easting = e;
    u.northing = y >= 0.0 ? y : y + 10000000.0;
    return fromUtm(u);
}

// Rotulo de una linea UTM: en km si el paso es de km ("360 km"), si no en
// metros. El norte del hemisferio sur se muestra con su falso norte.
QString textoUtm(double valor, double paso, bool esNorte)
{
    double v = valor;
    if (esNorte && v < 0.0)
        v += 10000000.0;
    if (paso >= 1000.0)
        return QStringLiteral("%1 km%2").arg(std::llround(v / 1000.0))
                                        .arg(esNorte ? QStringLiteral("N") : QStringLiteral("E"));
    return QStringLiteral("%1 m%2").arg(std::llround(v)).arg(esNorte ? QStringLiteral("N") : QStringLiteral("E"));
}

} // namespace

// Cuadricula UTM: por cada zona visible, lineas de este y norte constantes cada
// "paso" metros (100 km .. 100 m segun el zoom), dibujadas como polilineas
// muestreadas (en la proyeccion del mapa una linea UTM es una curva suave) y
// recortadas a los 6° de su zona. Los limites de zona van mas gruesos. A escala
// de continente (mas de 30° de ancho) solo se dibujan los limites de zona. No
// sigue las excepciones de Noruega/Svalbard (las coordenadas si lo hacen).
void GraticuleLayer::drawUtm(QCPPainter *painter)
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return;
    const QRect vp = plot->viewport();
    if (vp.width() < 20 || vp.height() < 20)
        return;

    const QCPRange rx = plot->xAxis->range();
    const QCPRange ry = plot->yAxis->range();
    const double lonMin = qMax(-180.0, rx.lower), lonMax = qMin(180.0, rx.upper);
    const double latMin = qMax(-80.0, TileMatrix::axisYToLatitude(ry.lower));
    const double latMax = qMin(84.0, TileMatrix::axisYToLatitude(ry.upper));
    if (!(lonMax > lonMin) || !(latMax > latMin))
        return;

    painter->save();
    painter->setClipRect(vp);
    QFont fuente = painter->font();
    fuente.setPointSizeF(8.0);
    painter->setFont(fuente);
    const QFontMetrics fm(fuente);
    const int altoFila = fm.height() + 4;
    // Rotulos ya puestos: uno que pisaria a otro se omite (a escala de varias
    // zonas las lineas de 100 km quedan mas juntas que lo que mide su texto).
    QVector<QRectF> ocupados;
    auto rotuloLibre = [&](QPointF esquina, const QString &txt) {
        const QRectF caja(esquina, QSizeF(fm.horizontalAdvance(txt) + 6, fm.height() + 2));
        for (const QRectF &r : ocupados)
            if (r.adjusted(-3, 0, 3, 0).intersects(caja))
                return;
        ocupados.append(caja);
        rotulo(painter, esquina, txt, m_color);
    };

    // Limites de zona (cada 6°) y su numero.
    const int z0 = qBound(1, int(std::floor((lonMin + 180.0) / 6.0)) + 1, 60);
    const int z1 = qBound(1, int(std::floor((lonMax + 180.0) / 6.0)) + 1, 60);
    painter->setPen(QPen(m_color, 2));
    for (int z = z0; z <= z1 + 1; ++z) {
        const double lon = -180.0 + 6.0 * (z - 1);
        if (lon <= lonMin || lon >= lonMax)
            continue;
        const double x = std::round(plot->xAxis->coordToPixel(lon));
        painter->drawLine(QPointF(x, vp.top()), QPointF(x, vp.bottom()));
    }
    for (int z = z0; z <= z1; ++z) {
        const QString txt = QStringLiteral("Zona %1").arg(z);
        const double ancho = fm.horizontalAdvance(txt) + 6;
        const double centro = qBound(lonMin, -183.0 + 6.0 * z, lonMax);
        const double x = qBound(double(vp.left()) + 2, plot->xAxis->coordToPixel(centro) - ancho / 2,
                                double(vp.right()) - ancho - 2);
        rotuloLibre(QPointF(x, vp.bottom() - altoFila - 2), txt);
    }
    if (lonMax - lonMin > 30.0) {
        painter->restore();
        return;
    }

    static const double pasos[] = { 100000.0, 50000.0, 20000.0, 10000.0, 5000.0,
                                     2000.0, 1000.0, 500.0, 200.0, 100.0 };
    constexpr int kMuestras = 40;
    painter->setPen(QPen(m_color, 1));
    bool primeraZona = true;

    for (int z = z0; z <= z1; ++z) {
        const double zl = -180.0 + 6.0 * (z - 1), zr = zl + 6.0;
        const double a = qMax(lonMin, zl), b = qMin(lonMax, zr);
        if (b <= a)
            continue;

        // Extension en (este, norte crudo) del trozo visible de la zona,
        // muestreando su contorno (los extremos no siempre estan en las esquinas).
        double eMin = 1e300, eMax = -1e300, yMin = 1e300, yMax = -1e300;
        for (int i = 0; i <= 10; ++i) {
            const double f = i / 10.0;
            const double lons[4] = { a + (b - a) * f, a + (b - a) * f, a, b };
            const double lats[4] = { latMin, latMax, latMin + (latMax - latMin) * f,
                                     latMin + (latMax - latMin) * f };
            for (int k = 0; k < 4; ++k) {
                double e = 0.0, y = 0.0;
                if (!utmCrudo(lats[k], lons[k], z, e, y))
                    continue;
                eMin = qMin(eMin, e); eMax = qMax(eMax, e);
                yMin = qMin(yMin, y); yMax = qMax(yMax, y);
            }
        }
        if (!(eMax > eMin) || !(yMax > yMin))
            continue;

        // Paso: el mas fino que no satura ni el ancho del trozo ni el alto.
        const double anchoPx = std::fabs(plot->xAxis->coordToPixel(b) - plot->xAxis->coordToPixel(a));
        const int maxE = qMax(2, int(anchoPx / 110.0));
        const int maxN = qMax(2, vp.height() / 90);
        double paso = pasos[0];
        for (double p : pasos) {
            if ((eMax - eMin) / p > maxE || (yMax - yMin) / p > maxN)
                break;
            paso = p;
        }

        // Pixel de una coordenada UTM cruda, o nulo si cae fuera de la zona.
        auto pixel = [&](double e, double y, QPointF &px) {
            const QGeoCoordinate c = desdeUtmCrudo(e, y, z);
            if (!c.isValid() || c.longitude() < zl - 1e-9 || c.longitude() > zr + 1e-9)
                return false;
            px = QPointF(plot->xAxis->coordToPixel(c.longitude()),
                         plot->yAxis->coordToPixel(TileMatrix::latitudeToAxisY(c.latitude())));
            return true;
        };
        // Dibuja una polilinea muestreada cortandola donde sale de la zona, y
        // devuelve su primer punto dentro de la vista (para el rotulo).
        auto trazar = [&](auto puntoI, QPointF &primero) {
            bool hayPrimero = false;
            QPolygonF tramo;
            for (int i = 0; i <= kMuestras; ++i) {
                QPointF px;
                if (puntoI(i, px)) {
                    tramo.append(px);
                    if (!hayPrimero && vp.contains(px.toPoint())) {
                        primero = px;
                        hayPrimero = true;
                    }
                    continue;
                }
                if (tramo.size() > 1)
                    painter->drawPolyline(tramo);
                tramo.clear();
            }
            if (tramo.size() > 1)
                painter->drawPolyline(tramo);
            return hayPrimero;
        };

        const long long e0 = static_cast<long long>(std::ceil(eMin / paso));
        const long long e1 = static_cast<long long>(std::floor(eMax / paso));
        const long long y0 = static_cast<long long>(std::ceil(yMin / paso));
        const long long y1 = static_cast<long long>(std::floor(yMax / paso));
        if (e1 - e0 > 200 || y1 - y0 > 200)
            continue;

        // Lineas de ESTE constante, de norte a sur; rotulo arriba.
        for (long long k = e0; k <= e1; ++k) {
            const double e = double(k) * paso;
            QPointF primero;
            const bool ok = trazar([&](int i, QPointF &px) {
                return pixel(e, yMax - (yMax - yMin) * i / kMuestras, px);
            }, primero);
            if (ok && primero.x() > vp.left() + 72 && primero.x() < vp.right() - 30)
                rotuloLibre(QPointF(primero.x() + 2, qMax(double(vp.top()) + 2, primero.y())),
                            textoUtm(e, paso, false));
        }
        // Lineas de NORTE constante, de oeste a este; rotulo a la izquierda
        // solo en la primera zona (en las demas se repetiria casi igual).
        for (long long k = y0; k <= y1; ++k) {
            const double y = double(k) * paso;
            QPointF primero;
            const bool ok = trazar([&](int i, QPointF &px) {
                return pixel(eMin + (eMax - eMin) * i / kMuestras, y, px);
            }, primero);
            if (ok && primeraZona && primero.y() > vp.top() + altoFila + 4
                && primero.y() < vp.bottom() - 50)
                rotuloLibre(QPointF(qMax(double(vp.left()) + 2, primero.x() + 2),
                                    primero.y() - altoFila + 2),
                            textoUtm(y, paso, true));
        }
        primeraZona = false;
    }
    painter->restore();
}

} // namespace libmapa
