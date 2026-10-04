#ifndef LIBMAPA_DEM_ELEVATIONANALYSIS_H_
#define LIBMAPA_DEM_ELEVATIONANALYSIS_H_

#include "dem/IElevationSource.h"
#include "libmapa/Elevation.h"

#include <QGeoCoordinate>
#include <QVector>

namespace libmapa {

/*!
 * \brief Perfil de elevacion del terreno a lo largo de una polilinea.
 *
 * Camina la ruta vertice a vertice, muestreando la cota (via \a src) cada
 * \c params.stepMeters metros (reparte el muestreo de forma uniforme a lo largo
 * de toda la ruta, no reinicia en cada vertice), e incluye siempre el ultimo
 * punto. Devuelve las muestras, la distancia total y las estadisticas
 * (min/max/ganancia/perdida), ignorando las muestras sin dato (NaN). Con menos
 * de dos puntos validos devuelve un perfil vacio.
 */
ElevationProfile elevationProfile(const IElevationSource &src,
                                  const QVector<QGeoCoordinate> &path,
                                  const ElevationProfileParams &params = {});

/*!
 * \brief Visibilidad directa entre dos puntos, con altura de antena en cada uno.
 *
 * Comprueba si el terreno corta la recta entre la cima de la antena en \a a
 * (altura \a antennaA sobre el terreno) y la de \a b (\a antennaB), muestreando
 * el terreno intermedio (via \a src) cada \c params.stepMeters. Corrige el
 * abombamiento de la Tierra con radio efectivo k·R (refraccion estandar k=4/3;
 * \c params.curvature=false lo desactiva). Devuelve si hay vision, la holgura
 * minima y el punto critico. Resultado invalido si falta la cota de algun
 * extremo o la geometria es degenerada.
 */
LineOfSightResult lineOfSight(const IElevationSource &src,
                              const QGeoCoordinate &a, const QGeoCoordinate &b,
                              double antennaA, double antennaB,
                              const LineOfSightParams &params = {});

/*!
 * \brief Viewshed 360 grados desde \a origin: un rayo por azimut.
 *
 * Por cada azimut (0..360 a paso \c params.azimuthStepDeg) camina el rayo
 * geodesico hasta \c params.maxRangeM muestreando el terreno (via \a src) cada
 * \c params.stepMeters. Para cada rayo calcula el angulo de cierre del terreno
 * (con curvatura 4/3 por defecto), el horizonte acumulado y sus picos, y hasta
 * donde se ve de forma continua un objetivo a \c params.targetHeight (el borde
 * del poligono de visibilidad). Resultado invalido (rays vacio) si falta la cota
 * en el origen. Es visibilidad DIRECTA, no radar (el horizonte geometrico emerge
 * de la geometria, no se codifica la formula 4.12*raiz(h)).
 */
Viewshed computeViewshed(const IElevationSource &src,
                         const QGeoCoordinate &origin,
                         const ViewshedParams &params = {});

} // namespace libmapa

#endif // LIBMAPA_DEM_ELEVATIONANALYSIS_H_
