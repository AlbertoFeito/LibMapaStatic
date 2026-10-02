#ifndef LIBMAPA_DEM_SQLITEELEVATION_H_
#define LIBMAPA_DEM_SQLITEELEVATION_H_

#include "dem/GridElevation.h"

#include <QByteArray>
#include <QString>

namespace libmapa {

/*!
 * \brief Fuente de elevacion que lee los tiles de una base de datos SQLite.
 *
 * Es el mismo dato que `HgtElevation` pero empaquetado en UN solo fichero
 * `.sqlitedb` (comodo para distribuir dentro de una app). La BD la genera la
 * herramienta `dem_to_db` a partir de una carpeta de `.hgt`. El esquema es:
 *
 *   dem_tiles(lat INTEGER, lon INTEGER, side INTEGER, data BLOB, PRIMARY KEY(lat,lon))
 *   dem_meta (key TEXT PRIMARY KEY, value TEXT)
 *
 * donde `data` son las MISMAS muestras int16 big-endian que el `.hgt`, pero
 * `qCompress`-adas. Esta clase solo consulta el blob y lo `qUncompress`; la cache
 * y la interpolacion las pone `GridElevation`, asi que da identica cota que el
 * lector de ficheros.
 */
class SqliteElevation : public GridElevation
{
public:
    SqliteElevation() = default;
    explicit SqliteElevation(const QString &dbFile) { setDatabase(dbFile); }

    //! Fija el fichero `.sqlitedb` de elevacion. Vacia la cache.
    void setDatabase(const QString &dbFile);
    QString database() const { return m_dbFile; }

protected:
    //! Consulta el tile (lat,lon) en dem_tiles y descomprime el blob. false si no
    //! hay fila, no se abre la BD, o el blob esta vacio/corrupto.
    bool loadTile(int latFloor, int lonFloor,
                  QByteArray &data, int &side) const override;

private:
    QString m_dbFile;
    QString m_connId;   //!< Id para SqliteConnectionPool (una conexion por hilo).
};

} // namespace libmapa

#endif // LIBMAPA_DEM_SQLITEELEVATION_H_
