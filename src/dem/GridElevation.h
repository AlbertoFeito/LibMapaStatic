#ifndef LIBMAPA_DEM_GRIDELEVATION_H_
#define LIBMAPA_DEM_GRIDELEVATION_H_

#include "dem/IElevationSource.h"

#include <QByteArray>
#include <QGeoCoordinate>
#include <QHash>
#include <QVector>

namespace libmapa {

/*!
 * \brief Base comun para fuentes de elevacion en rejilla tipo SRTM.
 *
 * Toda fuente SRTM es igual de puertas adentro: tiles de 1x1 grado, cada uno una
 * rejilla CUADRADA de muestras `int16` big-endian (fila 0 = norte, columna 0 =
 * oeste). Lo UNICO que cambia entre un lector de ficheros `.hgt` y uno de base
 * de datos es DE DONDE salen los bytes de cada tile. Esta clase concentra todo
 * lo demas -la cache LRU de tiles, la lectura de una muestra, la interpolacion
 * bilineal y el contrato NaN- y deja un unico hueco por rellenar: \c loadTile.
 *
 * Asi `HgtElevation` y `SqliteElevation` comparten EXACTAMENTE la misma
 * matematica (misma cota para la misma coordenada), y solo implementan su
 * `loadTile` (leer el fichero / consultar la BD).
 */
class GridElevation : public IElevationSource
{
public:
    //! Cota interpolada bilinealmente, o NaN. Ver IElevationSource.
    double elevationAt(const QGeoCoordinate &c) const override;

    //! Numero maximo de tiles en memoria a la vez (cache LRU). Por defecto 4.
    void setCacheSize(int tiles);

protected:
    // Un tile ya cargado: muestras crudas (int16 big-endian, lado*lado) y el lado.
    // 'ok' en false marca "no hay tile aqui" (se cachea para no reintentar).
    struct Tile {
        QByteArray data;
        int side = 0;
        bool ok = false;
    };

    //! Rellena \a data (muestras int16 big-endian) y \a side del tile cuya esquina
    //! SO es (latFloor, lonFloor). Devuelve false si no existe. Lo implementa cada
    //! fuente (fichero / BD). No necesita validar el tamano: la base lo comprueba.
    virtual bool loadTile(int latFloor, int lonFloor,
                          QByteArray &data, int &side) const = 0;

    //! Vacia la cache (las fuentes la llaman al cambiar de carpeta/BD).
    void clearCache();

    //! Raiz cuadrada entera exacta de \a v, o 0 si no es cuadrado perfecto. Util
    //! para deducir el lado de un tile `.hgt` a partir del numero de muestras.
    static int isqrtExact(qint64 v);

private:
    //! Clave de cache a partir de la esquina SO (lat/lon enteros no colisionan:
    //! |lon| <= 180 < 1000). Empaqueta en un entero para un QHash simple.
    static qint64 keyFor(int latFloor, int lonFloor)
    { return qint64(latFloor) * 1000 + lonFloor; }

    //! Muestra (metros o hueco) en la fila/columna de un tile. row=0 norte, col=0 oeste.
    static int sampleAt(const Tile &t, int row, int col);

    //! Tile de esa esquina, cargandolo via loadTile() la primera vez. Gestiona la
    //! LRU. Nunca falla hacia fuera: si no existe, Tile con ok=false (cacheado).
    const Tile &tileFor(int latFloor, int lonFloor) const;

    int m_cacheSize = 4;
    mutable QHash<qint64, Tile> m_cache;
    mutable QVector<qint64> m_lru;   // mas reciente al final
};

} // namespace libmapa

#endif // LIBMAPA_DEM_GRIDELEVATION_H_
