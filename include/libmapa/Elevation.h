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

//! Parametros del viewshed (visibilidad 360 grados desde un punto).
struct ViewshedParams
{
    //! Separacion entre muestras a lo largo de cada rayo, en metros.
    double stepMeters = 30.0;
    //! Separacion angular entre rayos, en grados (1 = 360 rayos).
    double azimuthStepDeg = 1.0;
    //! Alcance maximo de cada rayo, en metros.
    double maxRangeM = 50000.0;
    //! Altura del observador sobre el terreno en el origen, en metros (antena).
    double observerHeight = 0.0;
    //! Altura del objetivo sobre el terreno para la zona de visibilidad, en metros.
    double targetHeight = 0.0;
    //! Si se corrige la curvatura+refraccion de la Tierra (ver \c LineOfSightParams).
    bool curvature = true;
    double k = 4.0 / 3.0;              //!< Radio terrestre efectivo (4/3 estandar).
    double earthRadiusM = 6371000.0;   //!< Radio medio de la Tierra, en metros.
    //! Si cada rayo guarda ademas su perfil completo (\c ViewshedRay::profile).
    //! Por defecto NO, para acotar la memoria con 360 rayos; actívalo para
    //! obtener el corte del terreno de un azimut concreto.
    bool keepProfiles = false;
};

/*!
 * \brief Un pico de la silueta vista desde el origen (define el horizonte).
 *
 * Es la muestra de un rayo que fija un NUEVO maximo del angulo de cierre al
 * alejarse: entre dos picos consecutivos el horizonte es el angulo del ultimo.
 * \c angleDeg es el angulo de elevacion (grados; negativo si cae bajo el plano
 * del observador) y \c tangent su tangente.
 */
struct ClosingAnglePeak
{
    double distanceM = 0.0;
    QGeoCoordinate position;
    double elevation = std::numeric_limits<double>::quiet_NaN();
    double angleDeg = 0.0;
    double tangent = 0.0;
};

/*!
 * \brief Resultado de un rayo (un azimut) del viewshed.
 *
 * \c horizonDeg es el angulo de cierre maximo del rayo (la silueta mas alta en
 * esa direccion). \c peaks son los puntos que van definiendo esa silueta.
 * \c visibilityReachM es la frontera de la zona en la que un objetivo a
 * \c ViewshedParams::targetHeight se ve de forma CONTINUA desde el origen (el
 * borde del poligono ZVD). \c profile es el perfil del terreno del rayo, vacio
 * salvo que se pida con \c keepProfiles.
 */
struct ViewshedRay
{
    double azimuthDeg = 0.0;
    double visibilityReachM = 0.0;
    double horizonDeg = 0.0;
    QVector<ClosingAnglePeak> peaks;
    ElevationProfile profile;
};

/*!
 * \brief Viewshed 360 grados desde un punto: un rayo por azimut.
 *
 * Reune los \c rays (uno por azimut), la \c origin y las alturas usadas. Invalido
 * (rays vacio) si no habia cota en el origen o la geometria era degenerada.
 */
struct Viewshed
{
    QGeoCoordinate origin;
    double observerHeight = 0.0;
    double targetHeight = 0.0;
    QVector<ViewshedRay> rays;

    bool isValid() const { return !rays.isEmpty(); }
};

} // namespace libmapa

#endif // LIBMAPA_ELEVATION_H_
