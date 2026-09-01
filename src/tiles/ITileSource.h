#ifndef LIBMAPA_TILES_ITILESOURCE_H_
#define LIBMAPA_TILES_ITILESOURCE_H_

#include "tiles/TileDataset.h"
#include "tiles/TileKey.h"

#include <QByteArray>
#include <QHash>
#include <QSet>

namespace libmapa {

/*!
 * \brief Origen de teselas. Abstrae de donde salen los bytes de cada tile.
 *
 * Sustituye a CBDatos::Cargar_BD_IMG(quint8 Zoom, bool Tip_BD, ...), cuyo
 * parametro booleano (falla F-5) impedia anadir un tercer origen, pese a que
 * el propio proyecto ya leia relieve, vegetacion, hidrografia y viales desde
 * otras BD en Cargar_BD_OTROS.
 *
 * Implementaciones previstas:
 *   - RMapsTileSource : las dos .sqlitedb actuales (tabla 'tiles').
 *   - MBTilesSource   : formato estandar, si algun dia hace falta.
 *   - HttpTileSource  : servidor remoto.
 */
class ITileSource
{
public:
    virtual ~ITileSource() = default;

    virtual const TileDataset &dataset() const = 0;

    //! Un unico tile. QByteArray vacio si no existe.
    virtual QByteArray fetch(const TileKey &key) = 0;

    /*!
     * \brief Todas las teselas de una rejilla, en una sola consulta.
     *
     * Es la operacion caliente: una llamada por movimiento del mapa en vez
     * de una por tesela.
     */
    virtual QHash<TileKey, QByteArray> fetchRange(int z,
                                                  int xMin, int xMax,
                                                  int yMin, int yMax) = 0;

    /*!
     * \brief Que teselas existen, SIN traer los BLOB.
     *
     * El codigo original hacia "SELECT * FROM tiles", que arrastra la imagen
     * completa aunque solo se quiera saber que hay disponible (falla del
     * apartado 0.1). Con columnas explicitas la consulta es decenas de veces
     * mas barata.
     */
    virtual QSet<TileKey> available(int z,
                                    int xMin, int xMax,
                                    int yMin, int yMax) = 0;

    //! Numero total de teselas en un nivel logico dado (diagnostico).
    virtual qint64 tileCount(int z) = 0;
};

} // namespace libmapa

#endif // LIBMAPA_TILES_ITILESOURCE_H_
