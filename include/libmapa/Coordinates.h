#ifndef LIBMAPA_COORDINATES_H_
#define LIBMAPA_COORDINATES_H_

#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Formatos para mostrar o introducir una coordenada.
 *
 * Geograficos (WGS84) en grados decimales, grados-minutos-segundos o
 * grados-minutos decimales, y las dos proyecciones militares/topograficas
 * habituales: UTM (zona, banda, este, norte) y MGRS (UTM con cuadricula de
 * 100 km con letras: "17Q LF 60056 56750").
 */
enum class CoordinateFormat {
    Decimal,   //!< 23.113600, -82.366600
    DMS,       //!< 23°06'48.96"N 82°21'59.76"W
    DDM,       //!< 23°06.8160'N 82°21.9960'W
    UTM,       //!< 17Q 360057 2556751
    MGRS       //!< 17Q LF 60056 56750
};

/*!
 * \brief Una posicion en UTM (WGS84).
 *
 * \a band es la letra de banda de latitud (C..X, 8° cada una; X llega a 84°N).
 * \a north dice el hemisferio: al sur, el norte lleva sumado el falso norte de
 * 10 000 km. UTM solo cubre de 80°S a 84°N (los polos son UPS, no soportado).
 */
struct UtmCoordinate
{
    int zone = 0;            //!< 1..60 (0 = invalida)
    QChar band;              //!< 'C'..'X'
    bool north = true;
    double easting = 0.0;    //!< metros (500 000 en el meridiano central)
    double northing = 0.0;   //!< metros

    bool isValid() const { return zone >= 1 && zone <= 60; }
};

//! Zona UTM de una coordenada, con las excepciones de Noruega (32V) y
//! Svalbard (31X, 33X, 35X, 37X). 0 si la latitud cae fuera de UTM.
LIBMAPA_EXPORT int utmZoneFor(const QGeoCoordinate &coord);

/*!
 * \brief Geografica -> UTM, en su zona natural (\ref utmZoneFor) o forzada.
 *
 * Series de Kruger de orden 6 (Karney 2011): error sub-milimetrico en toda la
 * zona. \a forcedZone (1..60) permite expresar un punto en una zona vecina,
 * como hacen las cuadriculas que cruzan un limite. Invalida (zone == 0) fuera
 * de 80°S..84°N.
 */
LIBMAPA_EXPORT UtmCoordinate toUtm(const QGeoCoordinate &coord, int forcedZone = 0);

//! UTM -> geografica. Invalida si la zona no vale.
LIBMAPA_EXPORT QGeoCoordinate fromUtm(const UtmCoordinate &utm);

/*!
 * \brief Geografica -> MGRS con \a precision digitos por eje (1..5: 10 km ..
 *        1 m). Los digitos se TRUNCAN, como manda la norma: la referencia
 *        nombra la esquina suroeste de la celda que contiene el punto.
 *
 * \a spaced separa zona/banda, letras y digitos ("17Q LF 60056 56750"); si no,
 * va todo junto ("17QLF6005656750"). Vacio fuera de UTM.
 */
LIBMAPA_EXPORT QString toMgrs(const QGeoCoordinate &coord, int precision = 5,
                              bool spaced = true);

/*!
 * \brief MGRS -> geografica (esquina suroeste de la celda referida).
 *
 * Acepta mayusculas o minusculas, con o sin espacios, zona con o sin cero
 * delante, y de 0 a 5 digitos por eje. \a ok indica si se pudo leer.
 */
LIBMAPA_EXPORT QGeoCoordinate fromMgrs(const QString &mgrs, bool *ok = nullptr);

//! Texto de una coordenada en el formato pedido (vacio si no es valida o, en
//! UTM/MGRS, si cae fuera de su cobertura).
LIBMAPA_EXPORT QString formatCoordinate(const QGeoCoordinate &coord,
                                        CoordinateFormat format);

/*!
 * \brief Lee una coordenada escrita por una persona, en cualquiera de los
 *        formatos: MGRS ("17QLF6005656750"), UTM ("17Q 360057 2556751"),
 *        decimal ("23.1136, -82.3666") o sexagesimal con hemisferio
 *        ("23°06'48.96\"N 82°21'59.76\"W"; tambien O = oeste).
 */
LIBMAPA_EXPORT QGeoCoordinate parseCoordinate(const QString &text, bool *ok = nullptr);

/*!
 * \brief Superficie de un poligono sobre la Tierra, en metros cuadrados.
 *
 * Calculada en la esfera autalica de WGS84 (la de igual superficie, con
 * latitudes autalicas): un trapecio de meridianos y paralelos sale exacto en
 * el elipsoide y cualquier otro poligono queda muy cerca (lados cortos). El
 * anillo puede venir cerrado o no; con menos de 3 vertices validos devuelve 0.
 * No trata poligonos que cruzan +-180.
 */
LIBMAPA_EXPORT double polygonArea(const QVector<QGeoCoordinate> &ring);

//! Longitud de un trazado en metros (ortodromicas entre vertices). Con
//! \a closed = true suma tambien el lado del ultimo al primero (perimetro).
LIBMAPA_EXPORT double pathLength(const QVector<QGeoCoordinate> &path, bool closed = false);

} // namespace libmapa

#endif // LIBMAPA_COORDINATES_H_
