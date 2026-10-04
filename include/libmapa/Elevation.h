#ifndef LIBMAPA_ELEVATION_H_
#define LIBMAPA_ELEVATION_H_

#include <QGeoCoordinate>
#include <QVector>

#include <limits>

namespace libmapa {

//! NaN que marca "sin dato de elevacion" (hueco SRTM, tile ausente o sin origen).
inline double elevationNaN() { return std::numeric_limits<double>::quiet_NaN(); }

/*!
 * \brief Una muestra de elevacion a lo largo de un recorrido o rayo.
 *
 * \c elevation es la cota del terreno en metros, o NaN si no hay dato (comprobar
 * con std::isnan). \c distanceM es la distancia acumulada desde el inicio.
 */
struct ElevationSample
{
    double distanceM = 0.0;
    QGeoCoordinate position;
    double elevation = std::numeric_limits<double>::quiet_NaN();
};

//! Parametros del muestreo de un perfil de elevacion.
struct ElevationProfileParams
{
    //! Separacion entre muestras, en metros. 30 m aprovecha el SRTM de 1" y es
    //! barato; se puede subir para recorridos muy largos.
    double stepMeters = 30.0;
};

/*!
 * \brief Perfil de elevacion del terreno a lo largo de una polilinea (una ruta).
 *
 * Es la cota del terreno EN EL LUGAR, muestreada cada \c stepMeters. No aplica
 * curvatura terrestre (es altura real del terreno, para dibujar el perfil o
 * medir desniveles); la curvatura solo interviene en los calculos de visibilidad.
 * Las estadisticas ignoran las muestras sin dato (NaN).
 */
struct ElevationProfile
{
    QVector<ElevationSample> samples;
    double totalDistanceM = 0.0;
    double minElevation = std::numeric_limits<double>::quiet_NaN();
    double maxElevation = std::numeric_limits<double>::quiet_NaN();
    double gain = 0.0;   //!< Suma de ascensos (m).
    double loss = 0.0;   //!< Suma de descensos (m), en valor positivo.

    bool isValid() const { return !samples.isEmpty(); }
};

} // namespace libmapa

#endif // LIBMAPA_ELEVATION_H_
