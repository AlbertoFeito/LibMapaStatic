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

} // namespace libmapa

#endif // LIBMAPA_DEM_ELEVATIONANALYSIS_H_
