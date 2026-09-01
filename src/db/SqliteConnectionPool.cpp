#include "db/SqliteConnectionPool.h"
#include "core/Logging.h"

#include <QDir>
#include <QFileInfo>
// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que
// hay que pedirlo explicitamente: sin esto, bindValue() no compila en
// Qt 5.14 porque QVariant es un tipo incompleto y no hay conversion
// posible desde int, QString ni QByteArray.
#include <QVariant>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QThread>

namespace libmapa {

namespace {

// Registro por hilo de los connectionName abiertos desde este pool.
// Es thread_local, asi que cada hilo lleva su propia contabilidad sin mutex.
thread_local QSet<QString> g_threadConnections;

QString threadTag()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
}

} // namespace

QString SqliteConnectionPool::connectionName(const QString &datasetId)
{
    return QStringLiteral("libmapa_%1_%2").arg(datasetId, threadTag());
}

void SqliteConnectionPool::applyPragmas(QSqlDatabase &db, Mode mode)
{
    QSqlQuery q(db);

    // mmap_size se ajusta al tamano real del fichero. Medido sobre las BD del
    // proyecto, el 91 % del tiempo de una carga en frio se va en leer del
    // disco, no en decodificar: 980 ms de lectura frente a 101 ms de
    // decodificacion en la satelital a zoom 16. Mapear el fichero evita la
    // copia a un buffer intermedio en cada pagina.
    //
    // En 32 bits el espacio de direcciones no da para mapear GiB, asi que se
    // deja un valor conservador.
    qint64 mmapBytes = 128LL * 1024 * 1024;
    if (sizeof(void *) >= 8) {
        const qint64 fileBytes = QFileInfo(db.databaseName()).size();
        // Hasta 1 GiB: mapear mas no compensa el consumo de direcciones.
        mmapBytes = qBound<qint64>(64LL * 1024 * 1024, fileBytes,
                                   1024LL * 1024 * 1024);
    }

    if (mode == Mode::ReadOnly) {
        // La BD no se escribe nunca: no hace falta journal ni fsync.
        q.exec(QStringLiteral("PRAGMA journal_mode = OFF"));
        q.exec(QStringLiteral("PRAGMA synchronous  = OFF"));
    } else {
        // WAL permite lecturas concurrentes mientras se escribe.
        q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
        q.exec(QStringLiteral("PRAGMA synchronous  = NORMAL"));
        q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    }

    // 64 MB de page cache (el valor negativo son KiB, no paginas).
    q.exec(QStringLiteral("PRAGMA cache_size = -65536"));

    q.exec(QStringLiteral("PRAGMA mmap_size = %1").arg(mmapBytes));

    q.exec(QStringLiteral("PRAGMA temp_store = MEMORY"));
}

QSqlDatabase SqliteConnectionPool::connectionFor(const QString &datasetId,
                                                 const QString &filePath,
                                                 Mode mode)
{
    if (datasetId.isEmpty() || filePath.isEmpty()) {
        qCCritical(lcMapaDb) << "connectionFor: datasetId o filePath vacios";
        return {};
    }

    const QString name = connectionName(datasetId);

    // 1. Ya abierta en este hilo: se reutiliza tal cual.
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase db = QSqlDatabase::database(name, /*open=*/true);
        if (db.isOpen())
            return db;
        // Registrada pero caida: se limpia y se reintenta desde cero.
        qCWarning(lcMapaDb) << "Conexion" << name << "registrada pero cerrada;"
                            << "se recrea";
        QSqlDatabase::removeDatabase(name);
        g_threadConnections.remove(name);
    }

    // 2. El fichero tiene que existir antes de intentar abrirlo en solo
    //    lectura: SQLite crearia uno vacio en modo escritura y el error
    //    aparecerian mucho despues, al no encontrar la tabla 'tiles'.
    const QFileInfo fi(filePath);
    if (mode == Mode::ReadOnly && !fi.exists()) {
        qCCritical(lcMapaDb) << "No existe la base de datos:" << filePath;
        return {};
    }
    if (!fi.absoluteDir().exists()) {
        qCCritical(lcMapaDb) << "No existe el directorio:" << fi.absolutePath();
        return {};
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qCCritical(lcMapaDb) << "El driver QSQLITE no esta disponible."
                             << "Drivers:" << QSqlDatabase::drivers();
        return {};
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(filePath);

    QStringList options{QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000")};
    if (mode == Mode::ReadOnly)
        options << QStringLiteral("QSQLITE_OPEN_READONLY");
    db.setConnectOptions(options.join(QLatin1Char(';')));

    if (!db.open()) {
        qCCritical(lcMapaDb) << "Fallo al abrir" << filePath
                             << "-" << db.lastError().text();
        QSqlDatabase::removeDatabase(name);
        return {};
    }

    applyPragmas(db, mode);
    g_threadConnections.insert(name);

    qCDebug(lcMapaDb) << "Abierta" << name << "->" << filePath
                      << (mode == Mode::ReadOnly ? "[ro]" : "[rw]");
    return db;
}

void SqliteConnectionPool::closeAllForCurrentThread()
{
    const QSet<QString> names = g_threadConnections;
    g_threadConnections.clear();

    for (const QString &name : names) {
        if (!QSqlDatabase::contains(name))
            continue;
        {
            // El scope importa: removeDatabase debe ejecutarse cuando ya no
            // queda ninguna copia viva de la QSqlDatabase, o Qt avisa de que
            // hay conexiones en uso.
            QSqlDatabase db = QSqlDatabase::database(name, /*open=*/false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(name);
        qCDebug(lcMapaDb) << "Cerrada" << name;
    }
}

int SqliteConnectionPool::openConnectionCount()
{
    // size() es qsizetype en Qt 6; el numero de conexiones cabe de sobra en int.
    return static_cast<int>(g_threadConnections.size());
}

} // namespace libmapa
