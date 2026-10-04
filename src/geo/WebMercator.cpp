#include "geo/WebMercator.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace libmapa {

// Acota la latitud al maximo de Web Mercator (~85,05°). Fuera de ahi la
// proyeccion tiende a infinito, asi que se recorta en vez de dejar que estalle.
double WebMercator::clampLatitude(double latitudeDeg)
{
    return std::clamp(latitudeDeg, -kMaxLatitude, kMaxLatitude);
}

// Latitud (grados) -> "grados de Mercator": la Y del eje LINEAL que usa el mapa.
// Formula estandar de Mercator: y = ln(tan(pi/4 + lat/2)).
double WebMercator::latitudeToMercatorDegrees(double latitudeDeg)
{
    const double lat = qDegreesToRadians(clampLatitude(latitudeDeg));
    return qRadiansToDegrees(std::log(std::tan(M_PI / 4.0 + lat / 2.0)));
}

// La inversa de la anterior: "grados de Mercator" -> latitud real.
double WebMercator::mercatorDegreesToLatitude(double mercatorDeg)
{
    const double y = qDegreesToRadians(mercatorDeg);
    return qRadiansToDegrees(2.0 * std::atan(std::exp(y)) - M_PI / 2.0);
}

// Lleva cualquier longitud al rango [-180, 180) dando la vuelta al meridiano
// (p. ej. 200° -> -160°). Protege ademas de valores no finitos (NaN/inf).
double WebMercator::normalizeLongitude(double longitudeDeg)
{
    if (!std::isfinite(longitudeDeg))
        return 0.0;
    double lon = std::fmod(longitudeDeg + 180.0, 360.0);
    if (lon < 0.0)
        lon += 360.0;
    return lon - 180.0;
}

// Devuelve una coordenada "sana": latitud acotada y longitud normalizada.
// Si llega basura (NaN/inf) cae a (0,0) en vez de propagar el error.
QGeoCoordinate WebMercator::sanitized(double latitudeDeg, double longitudeDeg)
{
    if (!std::isfinite(latitudeDeg) || !std::isfinite(longitudeDeg))
        return QGeoCoordinate(0.0, 0.0);
    return QGeoCoordinate(clampLatitude(latitudeDeg),
                          normalizeLongitude(longitudeDeg));
}

QGeoCoordinate WebMercator::sanitized(const QGeoCoordinate &coord)
{
    // Una QGeoCoordinate invalida ya devuelve NaN, asi que no hay nada que
    // recuperar de ella: se cae al valor por defecto.
    if (!coord.isValid())
        return QGeoCoordinate(0.0, 0.0);
    return sanitized(coord.latitude(), coord.longitude());
}

// Proyeccion directa: lat/lon -> METROS Web Mercator (EPSG:3857). x depende
// solo de la longitud; y es la formula de Mercator escalada por el radio de la
// Tierra. Es lo que espera un .xyz en metros.
QPointF WebMercator::forward(double latitudeDeg, double longitudeDeg)
{
    const double lat = clampLatitude(latitudeDeg);

    const double x = qDegreesToRadians(longitudeDeg) * kEarthRadius;
    const double y = std::log(std::tan(M_PI / 4.0 + qDegreesToRadians(lat) / 2.0))
                     * kEarthRadius;
    return {x, y};
}

// Sobrecarga por comodidad: proyecta una QGeoCoordinate.
QPointF WebMercator::forward(const QGeoCoordinate &coord)
{
    return forward(coord.latitude(), coord.longitude());
}

// Proyeccion inversa: METROS Web Mercator -> lat/lon. La usa geo_to_tiles al
// leer ficheros .xyz (coordenadas en metros).
QGeoCoordinate WebMercator::inverse(double x, double y)
{
    const double lon = qRadiansToDegrees(x / kEarthRadius);
    const double lat = qRadiansToDegrees(2.0 * std::atan(std::exp(y / kEarthRadius))
                                         - M_PI / 2.0);
    return {lat, lon};
}

// Sobrecarga por comodidad: invierte un QPointF de metros.
QGeoCoordinate WebMercator::inverse(const QPointF &meters)
{
    return inverse(meters.x(), meters.y());
}

} // namespace libmapa
