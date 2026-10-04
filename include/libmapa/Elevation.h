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

//! Parametros de la visibilidad punto a punto.
struct LineOfSightParams
{
    //! Separacion entre muestras del terreno intermedio, en metros.
    double stepMeters = 30.0;
    //! Si se corrige por curvatura+refraccion (abombamiento de la Tierra). Con
    //! \c false el calculo es puramente geometrico (k=1).
    bool curvature = true;
    //! Factor de radio terrestre efectivo. 4/3 es la refraccion estandar; 1 =
    //! geometrico puro. Solo se usa si \c curvature es \c true.
    double k = 4.0 / 3.0;
    //! Radio medio de la Tierra, en metros.
    double earthRadiusM = 6371000.0;
};

/*!
 * \brief Resultado de la linea de vision entre dos puntos (con altura de antena).
 *
 * \c clear indica si hay vision directa (el terreno no corta la recta entre las
 * cimas de las antenas, teniendo en cuenta el abombamiento de la Tierra).
 * \c clearanceM es la holgura MINIMA a lo largo del trayecto: positiva = margen
 * libre; negativa = cuanto se queda corto en el peor punto. \c blockPosition /
 * \c blockDistanceM marcan ese punto critico (donde bloquea si \c clear es
 * \c false; si no, el de menor holgura). \c isValid es \c false si falta dato en
 * algun extremo o la geometria es degenerada.
 */
struct LineOfSightResult
{
    bool clear = false;
    double clearanceM = std::numeric_limits<double>::quiet_NaN();
    QGeoCoordinate blockPosition;
    double blockDistanceM = 0.0;
    double totalDistanceM = 0.0;
    bool valid = false;

    bool isValid() const { return valid; }
};

} // namespace libmapa

#endif // LIBMAPA_ELEVATION_H_
