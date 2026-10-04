#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace libmapa {

// Longitud -> indice X de tesela (fraccionario). Lineal: -180°=0, +180°=2^z.
double TileMatrix::longitudeToTileX(double lonDeg, int z)
{
    return (lonDeg + 180.0) / 360.0 * tilesPerSide(z);
}

// Latitud -> indice Y de tesela (fraccionario), estandar "slippy map" (Y crece
// hacia el sur). No es lineal: aplica la proyeccion de Mercator.
double TileMatrix::latitudeToTileY(double latDeg, int z)
{
    const double lat = qDegreesToRadians(WebMercator::clampLatitude(latDeg));
    return (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / M_PI)
           / 2.0 * tilesPerSide(z);
}

// Inversa de longitudeToTileX: indice X (borde de la tesela) -> longitud.
double TileMatrix::tileXToLongitude(double x, int z)
{
    return x / tilesPerSide(z) * 360.0 - 180.0;
}

// Inversa de latitudeToTileY: indice Y -> latitud del borde de la tesela.
double TileMatrix::tileYToLatitude(double y, int z)
{
    const double n = M_PI - 2.0 * M_PI * y / tilesPerSide(z);
    return qRadiansToDegrees(std::atan(std::sinh(n)));
}

// Indice Y -> "grados de Mercator" (el eje LINEAL de QCustomPlot). Lineal, a
// diferencia de tileYToLatitude: aqui no hay proyeccion, solo escala.
double TileMatrix::tileYToMercatorDegrees(double y, int z)
{
    return 180.0 - 360.0 * y / tilesPerSide(z);
}

// Inversa de la anterior: "grados de Mercator" -> indice Y de tesela.
double TileMatrix::mercatorDegreesToTileY(double mercatorDeg, int z)
{
    return (180.0 - mercatorDeg) / 360.0 * tilesPerSide(z);
}

// Que tesela (z,x,y) contiene una coordenada. floor + acotado al mundo [0,2^z).
TileKey TileMatrix::tileAt(const QGeoCoordinate &coord, int z)
{
    const int n = tilesPerSide(z);
    const int x = static_cast<int>(std::floor(longitudeToTileX(coord.longitude(), z)));
    const int y = static_cast<int>(std::floor(latitudeToTileY(coord.latitude(), z)));
    return TileKey{z, std::clamp(x, 0, n - 1), std::clamp(y, 0, n - 1)};
}

// Esquina NOROESTE (arriba-izquierda) de una tesela, en lat/lon.
QGeoCoordinate TileMatrix::tileNorthWest(const TileKey &key)
{
    return {tileYToLatitude(key.y, key.z), tileXToLongitude(key.x, key.z)};
}

// Esquina SURESTE (abajo-derecha): la NW de la tesela (x+1, y+1).
QGeoCoordinate TileMatrix::tileSouthEast(const TileKey &key)
{
    return {tileYToLatitude(key.y + 1, key.z), tileXToLongitude(key.x + 1, key.z)};
}

// Rectangulo geografico (lon,lat) que cubre una tesela: util para recortes.
QRectF TileMatrix::tileBounds(const TileKey &key)
{
    const QGeoCoordinate nw = tileNorthWest(key);
    const QGeoCoordinate se = tileSouthEast(key);
    return QRectF(QPointF(nw.longitude(), nw.latitude()),
                  QPointF(se.longitude(), se.latitude()));
}

// Latitud -> Y del eje de QCustomPlot ("grados de Mercator"). Imprescindible
// para colocar overlays (el eje Y es lineal, la latitud no).
double TileMatrix::latitudeToAxisY(double latitudeDeg)
{
    const double lat = qDegreesToRadians(WebMercator::clampLatitude(latitudeDeg));
    return qRadiansToDegrees(std::log(std::tan(M_PI / 4.0 + lat / 2.0)));
}

// Inversa: Y del eje -> latitud real (p. ej. al leer la posicion del raton).
double TileMatrix::axisYToLatitude(double axisY)
{
    const double y = qDegreesToRadians(axisY);
    return qRadiansToDegrees(2.0 * std::atan(std::exp(y)) - M_PI / 2.0);
}

// Convierte la Y LOGICA (XYZ, origen arriba) a la Y de ALMACENAMIENTO de la BD.
// XYZ: igual;  TMS: se invierte (origen abajo)  ->  (2^z - 1) - y.
int TileMatrix::toStorageY(int yXyz, int z, TileScheme scheme)
{
    return scheme == TileScheme::TMS ? (tilesPerSide(z) - 1 - yXyz) : yXyz;
}

int TileMatrix::fromStorageY(int yStored, int z, TileScheme scheme)
{
    // La transformacion es involutiva: aplicarla dos veces devuelve el original.
    return toStorageY(yStored, z, scheme);
}

// Rango de teselas [xMin..xMax]x[yMin..yMax] que cubre un rectangulo geografico
// a un zoom, con 'margin' teselas de colchon alrededor. Es la base para pedir el
// viewport: acota al mundo y ordena min/max (el NW tiene la latitud mayor = y
// menor). Lo usan el widget (viewport) y las herramientas de descarga (bbox).
TileMatrix::TileRange TileMatrix::rangeFor(const QGeoCoordinate &northWest,
                                           const QGeoCoordinate &southEast,
                                           int z,
                                           int margin)
{
    TileRange r;
    r.z = z;

    const int n = tilesPerSide(z);

    double xa = longitudeToTileX(northWest.longitude(), z);
    double xb = longitudeToTileX(southEast.longitude(), z);
    if (xb < xa) std::swap(xa, xb);

    // northWest tiene la latitud MAYOR, que corresponde al y MENOR.
    double ya = latitudeToTileY(northWest.latitude(), z);
    double yb = latitudeToTileY(southEast.latitude(), z);
    if (yb < ya) std::swap(ya, yb);

    r.xMin = static_cast<int>(std::floor(xa)) - margin;
    r.xMax = static_cast<int>(std::floor(xb)) + margin;
    r.yMin = static_cast<int>(std::floor(ya)) - margin;
    r.yMax = static_cast<int>(std::floor(yb)) + margin;

    r.xMin = std::clamp(r.xMin, 0, n - 1);
    r.xMax = std::clamp(r.xMax, 0, n - 1);
    r.yMin = std::clamp(r.yMin, 0, n - 1);
    r.yMax = std::clamp(r.yMax, 0, n - 1);

    return r;
}

// Elige el zoom "natural" para que un ancho de mundo (en grados de longitud)
// ocupe justo el ancho del viewport en pixeles. Deriva de: worldPx = tileSize*2^z.
int TileMatrix::bestZoomFor(double spanLongitudeDeg, int viewportWidthPx,
                            int minZoom, int maxZoom) const
{
    if (spanLongitudeDeg <= 0.0 || viewportWidthPx <= 0)
        return std::clamp(minZoom, minZoom, maxZoom);

    // Anchura del mundo en pixeles necesaria para que spanLongitudeDeg ocupe
    // exactamente viewportWidthPx:
    //     worldPx = viewportWidthPx * 360 / span
    // y worldPx = tileSize * 2^z  =>  z = log2(worldPx / tileSize)
    const double worldPx = viewportWidthPx * 360.0 / spanLongitudeDeg;
    const double zExact  = std::log2(worldPx / m_tileSize);

    int z = static_cast<int>(std::lround(zExact));
    return std::clamp(z, minZoom, maxZoom);
}

} // namespace libmapa
