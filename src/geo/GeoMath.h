#ifndef LIBMAPA_GEO_GEOMATH_H_
#define LIBMAPA_GEO_GEOMATH_H_

#include <QGeoCoordinate>
#include <QVector>

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

//! ¿El punto (lon, lat) cae dentro del poligono? Ray-casting clasico sobre las
//! coordenadas geograficas (lon en X, lat en Y). Suficiente para decidir que
//! teselas descargar en areas del tamano de un pais; no corrige la distorsion
//! de la proyeccion. Con menos de 3 vertices no hay poligono: devuelve true.
inline bool pointInPolygon(double lon, double lat,
                           const QVector<QGeoCoordinate> &poly)
{
    const int n = poly.size();
    if (n < 3)
        return true;
    bool dentro = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].longitude(), yi = poly[i].latitude();
        const double xj = poly[j].longitude(), yj = poly[j].latitude();
        const bool cruza = ((yi > lat) != (yj > lat))
            && (lon < (xj - xi) * (lat - yi) / (yj - yi) + xi);
        if (cruza)
            dentro = !dentro;
    }
    return dentro;
}

} // namespace GeoMath
} // namespace libmapa

#endif // LIBMAPA_GEO_GEOMATH_H_
