#ifndef LIBMAPA_VECTOR_POISEARCH_H_
#define LIBMAPA_VECTOR_POISEARCH_H_

#include "libmapa/Places.h"
#include "tiles/TileKey.h"
#include "vector/MvtTile.h"

#include <QColor>
#include <QGeoCoordinate>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Búsqueda de puntos de interés en teselas vectoriales (capa `poi` de
 *        OpenMapTiles). Solo núcleo, sin Gui ni E/S: se le dan teselas ya decodificadas.
 */
namespace poisearch {

//! Capa del MVT de la que salen los puntos de interés.
extern const char *const kLayer;

/*!
 * \brief Los puntos de la capa `poi` de \a tile (que es la tesela \a key), con su
 *        posición geográfica. Los POI sin nombre también se devuelven: se pueden
 *        buscar por su tipo ("farmacia").
 */
QVector<PointOfInterest> inTile(const mvt::Tile &tile, const TileKey &key);

/*!
 * \brief Los de \a pois que casan con \a text, los mejores primero, como mucho \a limit.
 *
 * Cada palabra de \a text (sin tildes ni mayúsculas) tiene que aparecer en el
 * nombre o en el tipo: sus nombres en español y en inglés y alguna palabra de uso
 * común ("cupet", "botica"). Así «farmacia» da farmacias aunque no tengan nombre y
 * «hospital calixto» da el hospital de ese nombre. Orden: los que EMPIEZAN por lo
 * escrito, luego los que lo contienen en el nombre, luego los que casan solo por
 * el tipo, y dentro de cada grupo los más cercanos a \a from.
 *
 * Con menos de dos letras (o texto vacío) no devuelve nada: «a» casaría con todo.
 * Rellena \c distanceM de cada resultado con la distancia a \a from.
 */
QVector<PointOfInterest> search(const QVector<PointOfInterest> &pois, const QString &text,
                                const QGeoCoordinate &from, int limit);

/*!
 * \brief Todos los de \a pois que casan con \a text (con la misma regla que \c search),
 *        en el mismo orden, sin tope y sin mínimo de letras: texto vacío = todos.
 *
 * Es el filtro de la importación de POI como entidades: «todos los hospitales» no
 * necesita ordenar por cercanía ni quedarse con los diez primeros.
 */
QVector<PointOfInterest> filter(const QVector<PointOfInterest> &pois, const QString &text);

//! Color del tema de un tipo (el de los grupos `poi_*` de los estilos por defecto:
//! salud rojo, seguridad azul, combustible naranja, alojamiento morado, transporte
//! verde) o un gris azulado si no es de ninguno.
QColor themeColor(const QString &type);

} // namespace poisearch

} // namespace libmapa

#endif // LIBMAPA_VECTOR_POISEARCH_H_
