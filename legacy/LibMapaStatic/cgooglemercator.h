#ifndef CGOOGLEMERCATOR_H
#define CGOOGLEMERCATOR_H

#include <QObject>
#include "cprojection.h"
#include <QtMath>
#define math_to_radian M_PI / 180
#define math_to_degree 180 / M_PI

namespace math_ext
{
    static double to_radians(double value)
    {
        return value * math_to_radian;
    }

    static double to_degrees(double value)
    {
        return value * math_to_degree;
    }

}

class CGoogleMercator : public CProjection
{
public:
    CGoogleMercator();

    QPointF forward(QGeoCoordinate coord);


    /*
     * From projected coordinate to geographic coordinate
     */
    QGeoCoordinate inverse(QPointF xy);

public:
    void LatLongtoMetros(QGeoCoordinate GeoPunto,double &metLat, double &metLong);
    double MetrosAPixel(double metLat, double metLong, int zoom);
private:


};

#endif // CGOOGLEMERCATOR_H
