#ifndef LIBMAPA_DB_SQLITECONNECTIONPOOL_H_
#define LIBMAPA_DB_SQLITECONNECTIONPOOL_H_

#include <QSqlDatabase>
#include <QString>

namespace libmapa {

/*!
 * \brief Conexiones SQLite persistentes, una por (dataset, hilo).
 *
 * Corrige tres fallas del codigo original:
 *
 *  F-1: CBDatos::Cargar_BD_IMG hacia open()...close() en CADA pan/zoom sobre
 *       un fichero de 1.4 GB, tirando el page cache de SQLite continuamente.
 *       Aqui la conexion se abre una vez y vive lo que viva el hilo.
 *
 *  F-2: "QSqlDatabase::removeDatabase(\"QSQLITE\")" no borraba nada porque
 *       "QSQLITE" es el DRIVER, no el connectionName; Qt emitia
 *       'connection QSQLITE not found' en cada llamada. Aqui el
 *       connectionName es explicito y removeDatabase solo se invoca en
 *       closeAllForCurrentThread(), con las QSqlQuery ya destruidas.
 *
 *  F-3: bd_Sat y bd_Osm se registraban AMBAS como "Mapas", de modo que el
 *       segundo addDatabase() expulsaba al primero ('duplicate connection
 *       name'). Por eso alternar satelital<->OSM era inestable. Aqui el
 *       nombre incluye el id del dataset y el del hilo: nunca colisiona.
 *
 * Las BD de teselas se abren en SOLO LECTURA: es imposible corromper por
 * accidente 1.4 GB de datos.
 */
class SqliteConnectionPool
{
public:
    enum class Mode {
        ReadOnly,   //!< Teselas. QSQLITE_OPEN_READONLY.
        ReadWrite   //!< Datos vectoriales (puntos, rutas, poligonos).
    };

    /*!
     * \brief Devuelve la conexion del hilo actual para ese dataset,
     *        abriendola la primera vez.
     *
     * Si falla, devuelve una QSqlDatabase invalida (isOpen() == false) y
     * registra el motivo en la categoria libmapa.db.
     */
    static QSqlDatabase connectionFor(const QString &datasetId,
                                      const QString &filePath,
                                      Mode mode = Mode::ReadOnly);

    //! Cierra y desregistra todas las conexiones abiertas por ESTE hilo.
    //! Debe llamarse antes de que el hilo termine.
    static void closeAllForCurrentThread();

    //! Nombre de conexion que se usaria; expuesto para diagnostico y tests.
    static QString connectionName(const QString &datasetId);

    //! Numero de conexiones vivas de este hilo (para tests).
    static int openConnectionCount();

private:
    static void applyPragmas(QSqlDatabase &db, Mode mode);
};

} // namespace libmapa

#endif // LIBMAPA_DB_SQLITECONNECTIONPOOL_H_
