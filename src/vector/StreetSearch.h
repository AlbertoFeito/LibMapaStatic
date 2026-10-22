#ifndef LIBMAPA_VECTOR_STREETSEARCH_H_
#define LIBMAPA_VECTOR_STREETSEARCH_H_

#include "libmapa/Places.h"
#include "tiles/TileKey.h"
#include "vector/MvtTile.h"

#include <QGeoCoordinate>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Búsqueda de calles en teselas vectoriales (capa `transportation_name` de
 *        OpenMapTiles). Solo núcleo, sin Gui ni E/S: se le dan teselas ya decodificadas.
 */
namespace streetsearch {

//! Capa del MVT de la que salen los nombres de las vías.
extern const char *const kLayer;

//! Un tramo de calle tal como viene en UNA tesela.
struct Segment
{
    QString name;                       //!< `name:es`, `name` o `name:latin`; vacío si no tiene.
    QString ref;                        //!< `ref`.
    QString type;                       //!< `class`.
    QVector<QGeoCoordinate> line;
};

/*!
 * \brief Los tramos con nombre (o referencia) de la capa `transportation_name` de
 *        \a tile, que es la tesela \a key, con su geometría geográfica.
 */
QVector<Segment> inTile(const mvt::Tile &tile, const TileKey &key);

/*!
 * \brief Calles de \a segments cuyo nombre casa con \a text, las mejores primero,
 *        como mucho \a limit.
 *
 * Cada palabra de \a text (sin tildes ni mayúsculas) tiene que estar en el nombre o en
 * la referencia; las palabras genéricas («calle», «avenida», «carretera»…) son
 * opcionales, para que «calle obispo» encuentre «Obispo». Los tramos del mismo nombre
 * que se tocan (a menos de unos metros) forman UNA calle; si no se tocan son calles
 * distintas. Orden: las que EMPIEZAN por lo escrito, luego las que lo contienen y,
 * dentro de cada grupo, las más cercanas a \a from.
 *
 * Con menos de dos letras (o texto vacío) no devuelve nada.
 */
QVector<Street> search(const QVector<Segment> &segments, const QString &text,
                       const QGeoCoordinate &from, int limit);

} // namespace streetsearch

} // namespace libmapa

#endif // LIBMAPA_VECTOR_STREETSEARCH_H_
