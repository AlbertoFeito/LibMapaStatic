#ifndef CPROJECTION_H
#define CPROJECTION_H

#include <qgeocoordinate.h>
#include <qpoint.h>

class CProjection
{
public:
    CProjection();

    /*
     * From geographic coordinate to projected coordinate
     */
    virtual QPointF forward(QGeoCoordinate coord) = 0;

    virtual QPointF forward(double lat, double lon)
    {
        return this->forward(QGeoCoordinate(lat, lon));
    }


    /*
     * From projected coordinate to geographic coordinate
     */
    virtual QGeoCoordinate inverse(QPointF coord) = 0;

    virtual QGeoCoordinate inverse(double x, double y)
    {
        return this->inverse(QPointF(y, x));
    }

    virtual ~CProjection();
};

#endif // CPROJECTION_H
