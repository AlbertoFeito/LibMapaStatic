#include "cgooglemercator.h"
//#include "utiles.h"

static double wgs84_radius = 6378137.0F;
static double orgigin_s = 2.0F * M_PI * wgs84_radius / 2.0F;

CGoogleMercator::CGoogleMercator()
= default;

QPointF CGoogleMercator::forward(QGeoCoordinate coord)
{
    double lat_rs = coord.latitude();
    double lon_rs = coord.longitude();


    double x = lon_rs * orgigin_s / 180.0F;
    double y = log(tan((90.0 + lat_rs) * M_PI / 360.0)) / (M_PI / 180.0);

    y *= orgigin_s / 180.0F;

    return QPointF(x, y);
}

QGeoCoordinate CGoogleMercator::inverse(QPointF xy)
{
    double lon = xy.x() / wgs84_radius;
    double y_tmp = xy.y() / wgs84_radius;

    double lat = 2 * atan(exp(y_tmp)) - M_PI_2;

    return QGeoCoordinate(math_ext::to_degrees(lat), math_ext::to_degrees(lon));
}
