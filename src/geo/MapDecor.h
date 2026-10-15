#ifndef LIBMAPA_GEO_MAPDECOR_H_
#define LIBMAPA_GEO_MAPDECOR_H_

#include "libmapa/MapTypes.h"

#include <QGeoCoordinate>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Calculos de la escala grafica y de la cuadricula de coordenadas.
 *
 * Funciones puras, sin dibujo: las capas ScaleBarLayer y GraticuleLayer solo
 * pintan lo que sale de aqui. Asi los "numeros redondos" y el formato de las
 * etiquetas se prueban sin ventanas (tst_mapdecor).
 */
namespace MapDecor {

//! Una escala grafica ya resuelta: cuanto mide en la realidad y en pantalla.
struct ScaleBar {
    double meters = 0.0;   //!< Longitud real representada.
    double pixels = 0.0;   //!< Longitud de la barra en pantalla.
    QString label;         //!< "500 m", "2 km", "0.5 NM"...
};

/*!
 * \brief Elige la barra de escala mas larga con un valor redondo (1, 2 o 5 por
 *        una potencia de 10) que quepa en \a maxPixels.
 *
 * \a metersPerPixel es la resolucion del mapa donde se lee la escala. Con un
 * valor no positivo o no finito devuelve una barra vacia (pixels == 0).
 */
ScaleBar scaleBar(double metersPerPixel, double maxPixels,
                  ScaleUnits units = ScaleUnits::Metric);

/*!
 * \brief Separacion de la cuadricula, en grados, para que en \a spanDeg grados
 *        quepan como mucho \a maxLines lineas.
 *
 * Solo usa pasos "de carta" que dividen el grado sexagesimal: 90, 45, 30, 20,
 * 15, 10, 5, 2, 1 grados; 30, 20, 15, 10, 5, 2, 1 minutos; y lo mismo en
 * segundos. Asi las lineas caen en valores que se leen bien (23°30', no 23,5°).
 */
double graticuleStep(double spanDeg, int maxLines);

//! Etiqueta de una latitud con la precision que pide el paso de la cuadricula:
//! grados ("23°N"), minutos ("23°30'N") o segundos ("23°30'15\"N").
QString formatLatitude(double latitudeDeg, double stepDeg);
//! Igual para la longitud ("82°W", "82°15'W"...). Se normaliza a (-180, 180].
QString formatLongitude(double longitudeDeg, double stepDeg);

/*!
 * \brief Distancia para un rotulo: "750 m", "2.5 km", "1.25 NM".
 *
 * Con decimales segun el tamaño (2 por debajo de 10 unidades, 1 hasta 100,
 * ninguno a partir de ahi) y sin ceros sobrantes ("1 NM", no "1.00 NM"). En
 * metrico, por debajo de 1 km va en metros enteros. Vacio si no es finita.
 */
QString formatDistance(double meters, ScaleUnits units);

/*!
 * \brief Superficie para un rotulo: "850 m²", "12.5 ha", "3.42 km²" (metrico)
 *        o "1.25 NM²" (nautico).
 *
 * En metrico: metros cuadrados por debajo de una hectarea, hectareas hasta
 * 1 km² y kilometros cuadrados a partir de ahi. Mismos decimales y sin ceros
 * sobrantes que formatDistance. Vacio si no es finita.
 */
QString formatArea(double squareMeters, ScaleUnits units);

//! Rumbo o marcacion verdadera con tres cifras: "045°", "359°". Se normaliza
//! a [0, 360) y se redondea al grado. Vacio si no es finito.
QString formatBearing(double degrees);

/*!
 * \brief Circulo GEODESICO de radio \a radiusMeters alrededor de \a center:
 *        \a segments puntos equiespaciados en azimut, cerrado (el ultimo
 *        repite el primero).
 *
 * En el mapa (Mercator) un anillo de distancia real no es un circulo de
 * pantalla: se estira hacia los polos. Asi queda donde debe a cualquier latitud.
 * Vacio si el centro no es valido o el radio no es positivo.
 */
QVector<QGeoCoordinate> geodesicCircle(const QGeoCoordinate &center, double radiusMeters,
                                       int segments = 120);

} // namespace MapDecor
} // namespace libmapa

#endif // LIBMAPA_GEO_MAPDECOR_H_
