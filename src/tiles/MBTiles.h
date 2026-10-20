#ifndef LIBMAPA_TILES_MBTILES_H_
#define LIBMAPA_TILES_MBTILES_H_

#include "tiles/TileDataset.h"

#include <QGeoRectangle>
#include <QJsonObject>
#include <QString>

namespace libmapa {

/*!
 * \brief Soporte del formato estándar MBTiles (teselas ráster en SQLite).
 *
 * MBTiles guarda las teselas en una tabla (o vista) `tiles(zoom_level,
 * tile_column, tile_row, tile_data)` con las filas en esquema TMS, y los datos
 * del mapa en `metadata(name, value)`. Eso es, casi exactamente, lo que ya sabe
 * leer \c RMapsTileSource con otra configuración de columnas, así que MBTiles
 * NO tiene un lector aparte: se describe con un \c TileDataset y entra por el
 * mismo camino que las bases de siempre (caché, escalera de respaldo, cobertura,
 * descarga…). Lo que hay aquí es solo la parte de «entender el fichero»: la
 * disposición de las columnas y la lectura de sus metadatos.
 *
 * Teselas ráster (PNG, JPEG, WebP; WebP necesita el plugin de imágenes de Qt)
 * y, además, VECTORIALES (`format = pbf`, Mapbox Vector Tile, normalmente en
 * gzip, como los de OpenMapTiles): \c describe las reconoce, marca el dataset
 * con \c TileDataset::vector y el \c TileLoader las pinta con
 * \c VectorTileRenderer. Un fichero que dice ser vectorial pero cuyas teselas
 * no se decodifican se rechaza con un mensaje claro.
 */
namespace MBTiles {

//! ¿La ruta es un fichero MBTiles, por su extensión (`.mbtiles`)?
bool isMBTilesPath(const QString &path);

/*!
 * \brief Pone en \a ds la disposición de MBTiles: columnas `zoom_level`,
 *        `tile_column`, `tile_row`, `tile_data`, esquema TMS, sin columna `s` y
 *        zoom lógico = zoom guardado. No toca nada más (zoom mínimo/máximo, etc.).
 */
void applyLayout(TileDataset &ds);

/*!
 * \brief Rellena el resto del dataset leyendo el propio fichero.
 *
 * Toma de `metadata` (y, si falta, de la tabla) el rango de zooms, el nombre y el
 * esquema, y del tamaño de una tesela real el lado en píxeles. Solo se toca lo
 * que NO esté en \a given, el objeto JSON del que salió el dataset: lo que el
 * usuario escribió a mano manda sobre lo detectado. Comprueba además que la
 * tabla tenga las columnas esperadas y si las teselas son imágenes o MVT.
 *
 * \return false si el fichero no se puede abrir, no es un MBTiles o sus
 *         teselas no se pueden leer; el motivo queda en \a error. \a ds conserva la disposición
 *         de MBTiles aunque falle.
 */
bool describe(TileDataset &ds, const QJsonObject &given = QJsonObject(),
              QString *error = nullptr);

/*!
 * \brief Atajo: construye el \c TileDataset de un fichero MBTiles.
 * \param id identificador de la capa; si está vacío sale del nombre del fichero.
 * \param coverage si no es null, recibe la zona que declara `metadata.bounds`
 *        (inválida si no la hay).
 */
TileDataset fromFile(const QString &path, const QString &id = QString(),
                     QString *error = nullptr, QGeoRectangle *coverage = nullptr);

} // namespace MBTiles

} // namespace libmapa

#endif // LIBMAPA_TILES_MBTILES_H_
