#ifndef LIBMAPA_GEO_WEBMERCATOR_H_
#define LIBMAPA_GEO_WEBMERCATOR_H_

#include <QGeoCoordinate>
#include <QPointF>

namespace libmapa {

/*!
 * \brief Proyeccion Web Mercator (EPSG:3857), la que usan OSM y Google.
 *
 * Corrige F-19 del analisis: en CProjection::inverse(double x, double y) los
 * parametros se llamaban x,y pero por dentro se construia QPointF(y, x), de
 * modo que los llamadores tenian que compensar invirtiendolos. Aqui x es x
 * (Este, metros) e y es y (Norte, metros), sin sorpresas.
 *
 * Todas las funciones son estaticas y puras: no hay estado, son thread-safe.
 */
class WebMercator
{
public:
    /// Radio ecuatorial WGS84, en metros.
    static constexpr double kEarthRadius = 6378137.0;

    /// Latitud maxima representable en Mercator (donde y == +-pi*R).
    static constexpr double kMaxLatitude = 85.05112877980659;

    //! Geografico -> proyectado (metros). Devuelve (x=Este, y=Norte).
    static QPointF forward(const QGeoCoordinate &coord);
    static QPointF forward(double latitudeDeg, double longitudeDeg);

    //! Proyectado (metros) -> geografico.
    static QGeoCoordinate inverse(const QPointF &meters);
    static QGeoCoordinate inverse(double x, double y);

    //! Recorta una latitud al rango representable.
    static double clampLatitude(double latitudeDeg);

    /*!
     * \brief Lleva una longitud a [-180, 180] dando la vuelta al mundo.
     *
     * Necesario porque QGeoCoordinate NO recorta: construida con una longitud
     * de -185 se marca invalida y devuelve NaN tanto en latitude() como en
     * longitude(). Ese NaN se propaga al rango de los ejes y el generador de
     * marcas de QCustomPlot se queda en un bucle del que no sale: la
     * aplicacion se cuelga.
     *
     * Se llega ahi con facilidad: al zoom 3 la pantalla abarca unos 193
     * grados de longitud, asi que un arrastre horizontal corto ya cruza el
     * antimeridiano.
     */
    static double normalizeLongitude(double longitudeDeg);

    /*!
     * \brief Construye una coordenada SIEMPRE valida.
     *
     * Recorta la latitud y da la vuelta a la longitud. Si algun valor llega
     * como NaN o infinito, devuelve una coordenada por defecto en vez de
     * propagar la basura.
     */
    static QGeoCoordinate sanitized(double latitudeDeg, double longitudeDeg);
    static QGeoCoordinate sanitized(const QGeoCoordinate &coord);

    /*!
     * \brief Latitud -> ordenada de Mercator expresada EN GRADOS.
     *
     * Es la coordenada proyectada, normalizada para que valga +-180 en los
     * bordes del mundo, igual que la longitud. Coincide con la latitud cerca
     * del ecuador y se separa al subir.
     *
     * Existe porque el eje vertical del mapa TIENE que ser lineal en la
     * proyeccion, no en la latitud. Con el eje en grados de latitud, a zoom 3
     * la tesela superior abarca 5.88 grados y la central 40.98, pero ambas
     * miden 256 pixeles: el mapa se estira. En grados de Mercator todas
     * abarcan exactamente 45 y salen cuadradas.
     */
    static double latitudeToMercatorDegrees(double latitudeDeg);
    static double mercatorDegreesToLatitude(double mercatorDeg);
};

} // namespace libmapa

#endif // LIBMAPA_GEO_WEBMERCATOR_H_
