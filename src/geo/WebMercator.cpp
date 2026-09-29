#include "geo/WebMercator.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace libmapa {

double WebMercator::clampLatitude(double latitudeDeg)
{
    return std::clamp(latitudeDeg, -kMaxLatitude, kMaxLatitude);
}

double WebMercator::latitudeToMercatorDegrees(double latitudeDeg)
{
    const double lat = qDegreesToRadians(clampLatitude(latitudeDeg));
    return qRadiansToDegrees(std::log(std::tan(M_PI / 4.0 + lat / 2.0)));
}

double WebMercator::mercatorDegreesToLatitude(double mercatorDeg)
{
    const double y = qDegreesToRadians(mercatorDeg);
    return qRadiansToDegrees(2.0 * std::atan(std::exp(y)) - M_PI / 2.0);
}

double WebMercator::normalizeLongitude(double longitudeDeg)
{
    if (!std::isfinite(longitudeDeg))
        return 0.0;
    double lon = std::fmod(longitudeDeg + 180.0, 360.0);
    if (lon < 0.0)
        lon += 360.0;
    return lon - 180.0;
}

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

QPointF WebMercator::forward(double latitudeDeg, double longitudeDeg)
{
    const double lat = clampLatitude(latitudeDeg);

    const double x = qDegreesToRadians(longitudeDeg) * kEarthRadius;
    const double y = std::log(std::tan(M_PI / 4.0 + qDegreesToRadians(lat) / 2.0))
                     * kEarthRadius;
    return {x, y};
}

QPointF WebMercator::forward(const QGeoCoordinate &coord)
{
    return forward(coord.latitude(), coord.longitude());
}

QGeoCoordinate WebMercator::inverse(double x, double y)
{
    const double lon = qRadiansToDegrees(x / kEarthRadius);
    const double lat = qRadiansToDegrees(2.0 * std::atan(std::exp(y / kEarthRadius))
                                         - M_PI / 2.0);
    return {lat, lon};
}

QGeoCoordinate WebMercator::inverse(const QPointF &meters)
{
    return inverse(meters.x(), meters.y());
}

} // namespace libmapa
