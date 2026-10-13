#ifndef LIBMAPA_GEO_MAPDECOR_H_
#define LIBMAPA_GEO_MAPDECOR_H_

#include "libmapa/MapTypes.h"

#include <QString>

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

} // namespace MapDecor
} // namespace libmapa

#endif // LIBMAPA_GEO_MAPDECOR_H_
