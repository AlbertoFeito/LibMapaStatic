#ifndef LIBMAPA_GEO_GEOMATH_H_
#define LIBMAPA_GEO_GEOMATH_H_

#include <QGeoCoordinate>

namespace libmapa {

/*!
 * \brief Calculos geodesicos sobre el elipsoide.
 *
 * Sustituye a CCalculos::Calcular_Distancia_2Puntos y Calcular_Marcacion, que
 * trabajaban sobre QPointF en grados y aplicaban trigonometria plana: sobre
 * distancias largas y latitudes altas el error es grande, porque un grado de
 * longitud no mide lo mismo en el ecuador que a 23 grados norte.
 *
 * QGeoCoordinate ya trae el calculo correcto; aqui solo se le pone nombre y
 * se normaliza la marcacion a [0, 360).
 */
namespace GeoMath {

//! Distancia sobre la superficie, en metros.
inline double distanceMeters(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    if (!a.isValid() || !b.isValid())
        return 0.0;
    return a.distanceTo(b);
}

//! Marcacion inicial desde \a a hacia \a b, en grados desde el norte [0, 360).
inline double azimuthDegrees(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    if (!a.isValid() || !b.isValid())
        return 0.0;
    double az = a.azimuthTo(b);
    while (az < 0.0)    az += 360.0;
    while (az >= 360.0) az -= 360.0;
    return az;
}

} // namespace GeoMath
} // namespace libmapa

#endif // LIBMAPA_GEO_GEOMATH_H_
