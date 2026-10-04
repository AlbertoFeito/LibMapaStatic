#include "tiles/RMapsTileSource.h"

#include "core/Logging.h"
#include "db/SqliteConnectionPool.h"
#include "geo/TileMatrix.h"

// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que
// hay que pedirlo explicitamente: sin esto, bindValue() no compila en
// Qt 5.14 porque QVariant es un tipo incompleto y no hay conversion
// posible desde int, QString ni QByteArray.
#include <QVariant>
#include <QSqlError>
#include <QSqlRecord>
#include <algorithm>

namespace libmapa {

// Guarda el descriptor de la BD; no abre nada todavia (eso ocurre en open()).
RMapsTileSource::RMapsTileSource(TileDataset dataset)
    : m_ds(std::move(dataset))
{
}

RMapsTileSource::~RMapsTileSource()
{
    // Las QSqlQuery deben morir ANTES de que nadie cierre la conexion.
    m_qSingle.reset();
    m_qRange.reset();
    m_qAvailable.reset();
    m_qCount.reset();
}

// Construye el WHERE comun de las consultas de rango, usando los NOMBRES de
// columna reales del dataset e incluyendo la columna 's' solo si existe.
QString RMapsTileSource::whereClause() const
{
    QString w = QStringLiteral("WHERE %1 = :z AND %2 BETWEEN :x1 AND :x2 "
                               "AND %3 BETWEEN :y1 AND :y2")
                    .arg(m_ds.colZ, m_ds.colX, m_ds.colY);
    if (m_ds.hasSColumn)
        w += QStringLiteral(" AND %1 = :s").arg(m_ds.colS);
    return w;
}

// Rellena los parametros del WHERE (z guardado, rango de x/y, y 's' si toca).
void RMapsTileSource::bindCommon(QSqlQuery &q, int storedZ,
                                 int xMin, int xMax,
                                 int yMinStored, int yMaxStored) const
{
    q.bindValue(QStringLiteral(":z"),  storedZ);
    q.bindValue(QStringLiteral(":x1"), xMin);
    q.bindValue(QStringLiteral(":x2"), xMax);
    q.bindValue(QStringLiteral(":y1"), yMinStored);
    q.bindValue(QStringLiteral(":y2"), yMaxStored);
    if (m_ds.hasSColumn)
        q.bindValue(QStringLiteral(":s"), m_ds.sValue);
}

// Traduce un rango de Y LOGICO (XYZ) al rango de Y de ALMACENAMIENTO. En TMS la
// conversion invierte el orden, por eso hay que recalcular min y max.
void RMapsTileSource::storageYRange(int z, int yMin, int yMax,
                                    int *outMin, int *outMax) const
{
    const int a = TileMatrix::toStorageY(yMin, z, m_ds.scheme);
    const int b = TileMatrix::toStorageY(yMax, z, m_ds.scheme);
    *outMin = std::min(a, b);
    *outMax = std::max(a, b);
}

// Abre la BD (solo lectura, via el pool por hilo) y PREPARA las cuatro consultas
// reutilizables: una tesela, un rango (con imagen), disponibilidad (sin imagen) y
// conteo por zoom. Resuelve aqui los nombres de columna reales. Idempotente.
bool RMapsTileSource::open()
{
    if (m_open)
        return true;

    if (!m_ds.isValid()) {
        m_lastError = QStringLiteral("TileDataset invalido (id=%1)").arg(m_ds.id);
        qCCritical(lcMapaTiles) << m_lastError;
        return false;
    }

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        m_ds.id, m_ds.filePath, SqliteConnectionPool::Mode::ReadOnly);

    if (!db.isOpen()) {
        m_lastError = QStringLiteral("No se pudo abrir %1").arg(m_ds.filePath);
        return false;
    }

    const QString where = whereClause();

    m_qRange = std::make_unique<QSqlQuery>(db);
    if (!m_qRange->prepare(QStringLiteral("SELECT %1, %2, %3 FROM %4 %5")
                               .arg(m_ds.colX, m_ds.colY, m_ds.colImage,
                                    m_ds.tableName, where))) {
        m_lastError = m_qRange->lastError().text();
        qCCritical(lcMapaTiles) << "prepare(range) fallo:" << m_lastError;
        return false;
    }

    m_qAvailable = std::make_unique<QSqlQuery>(db);
    if (!m_qAvailable->prepare(QStringLiteral("SELECT %1, %2 FROM %3 %4")
                                   .arg(m_ds.colX, m_ds.colY,
                                        m_ds.tableName, where))) {
        m_lastError = m_qAvailable->lastError().text();
        qCCritical(lcMapaTiles) << "prepare(available) fallo:" << m_lastError;
        return false;
    }

    QString singleWhere = QStringLiteral("WHERE %1 = :z AND %2 = :x AND %3 = :y")
                              .arg(m_ds.colZ, m_ds.colX, m_ds.colY);
    if (m_ds.hasSColumn)
        singleWhere += QStringLiteral(" AND %1 = :s").arg(m_ds.colS);

    m_qSingle = std::make_unique<QSqlQuery>(db);
    if (!m_qSingle->prepare(QStringLiteral("SELECT %1 FROM %2 %3 LIMIT 1")
                                .arg(m_ds.colImage, m_ds.tableName, singleWhere))) {
        m_lastError = m_qSingle->lastError().text();
        qCCritical(lcMapaTiles) << "prepare(single) fallo:" << m_lastError;
        return false;
    }

    m_qCount = std::make_unique<QSqlQuery>(db);
    if (!m_qCount->prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 = :z")
                               .arg(m_ds.tableName, m_ds.colZ))) {
        m_lastError = m_qCount->lastError().text();
        qCCritical(lcMapaTiles) << "prepare(count) fallo:" << m_lastError;
        return false;
    }

    m_open = true;
    qCInfo(lcMapaTiles) << "Fuente lista:" << m_ds.id
                        << "z[" << m_ds.minZoom << ".." << m_ds.maxZoom << "]"
                        << tileSchemeToString(m_ds.scheme)
                        << "tile" << m_ds.tileSize;
    return true;
}

// UNA tesela: traduce el TileKey logico a la fila real (storedZ, Y del esquema,
// s) y devuelve el BLOB de imagen, o vacio si no existe/da error.
QByteArray RMapsTileSource::fetch(const TileKey &key)
{
    if (!m_open && !open())
        return {};
    if (!m_ds.zoomInRange(key.z) || !key.isValid())
        return {};

    const int storedY = TileMatrix::toStorageY(key.y, key.z, m_ds.scheme);

    m_qSingle->bindValue(QStringLiteral(":z"), m_ds.storedZ(key.z));
    m_qSingle->bindValue(QStringLiteral(":x"), key.x);
    m_qSingle->bindValue(QStringLiteral(":y"), storedY);
    if (m_ds.hasSColumn)
        m_qSingle->bindValue(QStringLiteral(":s"), m_ds.sValue);

    if (!m_qSingle->exec()) {
        m_lastError = m_qSingle->lastError().text();
        qCWarning(lcMapaTiles) << "fetch fallo:" << m_lastError;
        return {};
    }
    if (!m_qSingle->next())
        return {};

    const QByteArray blob = m_qSingle->value(0).toByteArray();
    m_qSingle->finish();
    return blob;
}

// MUCHAS teselas de un golpe (un viewport): una sola consulta por rango. Devuelve
// un mapa TileKey(logico) -> imagen. Resuelve las columnas por nombre (no por
// posicion) y reconvierte la Y almacenada a logica.
QHash<TileKey, QByteArray> RMapsTileSource::fetchRange(int z,
                                                       int xMin, int xMax,
                                                       int yMin, int yMax)
{
    QHash<TileKey, QByteArray> out;

    if (!m_open && !open())
        return out;
    if (!m_ds.zoomInRange(z) || xMax < xMin || yMax < yMin)
        return out;

    int sy1 = 0, sy2 = 0;
    storageYRange(z, yMin, yMax, &sy1, &sy2);

    bindCommon(*m_qRange, m_ds.storedZ(z), xMin, xMax, sy1, sy2);

    if (!m_qRange->exec()) {
        m_lastError = m_qRange->lastError().text();
        qCWarning(lcMapaTiles) << "fetchRange fallo:" << m_lastError;
        return out;
    }

    // Indices resueltos por NOMBRE una sola vez, no por posicion fija.
    const QSqlRecord rec = m_qRange->record();
    const int iX   = rec.indexOf(m_ds.colX);
    const int iY   = rec.indexOf(m_ds.colY);
    const int iImg = rec.indexOf(m_ds.colImage);
    if (iX < 0 || iY < 0 || iImg < 0) {
        m_lastError = QStringLiteral("Columnas no encontradas en el resultado");
        qCCritical(lcMapaTiles) << m_lastError;
        return out;
    }

    out.reserve((xMax - xMin + 1) * (yMax - yMin + 1));
    while (m_qRange->next()) {
        const int storedY = m_qRange->value(iY).toInt();
        out.insert(TileKey{z,
                           m_qRange->value(iX).toInt(),
                           TileMatrix::fromStorageY(storedY, z, m_ds.scheme)},
                   m_qRange->value(iImg).toByteArray());
    }
    m_qRange->finish();

    qCDebug(lcMapaTiles) << m_ds.id << "z" << z
                         << "pedidas" << (xMax - xMin + 1) * (yMax - yMin + 1)
                         << "obtenidas" << out.size();
    return out;
}

// QUE teselas EXISTEN en un rango, SIN traer la imagen (solo x,y). Mas barato que
// fetchRange; sirve para saber huecos y precargar la "escalera de respaldo".
QSet<TileKey> RMapsTileSource::available(int z,
                                         int xMin, int xMax,
                                         int yMin, int yMax)
{
    QSet<TileKey> out;

    if (!m_open && !open())
        return out;
    if (!m_ds.zoomInRange(z) || xMax < xMin || yMax < yMin)
        return out;

    int sy1 = 0, sy2 = 0;
    storageYRange(z, yMin, yMax, &sy1, &sy2);

    bindCommon(*m_qAvailable, m_ds.storedZ(z), xMin, xMax, sy1, sy2);

    if (!m_qAvailable->exec()) {
        m_lastError = m_qAvailable->lastError().text();
        qCWarning(lcMapaTiles) << "available fallo:" << m_lastError;
        return out;
    }

    const QSqlRecord rec = m_qAvailable->record();
    const int iX = rec.indexOf(m_ds.colX);
    const int iY = rec.indexOf(m_ds.colY);
    if (iX < 0 || iY < 0)
        return out;

    while (m_qAvailable->next()) {
        const int storedY = m_qAvailable->value(iY).toInt();
        out.insert(TileKey{z,
                           m_qAvailable->value(iX).toInt(),
                           TileMatrix::fromStorageY(storedY, z, m_ds.scheme)});
    }
    m_qAvailable->finish();
    return out;
}

// Histograma de cobertura: una sola consulta que, agrupando por la tesela
// RESUMEN (x>>shift, y>>shift) a un zoom dado, cuenta cuantas teselas hay en cada
// celda gruesa. El 'shift' se inyecta como literal (es un entero propio, no texto
// de usuario) para no repetir el mismo parametro con nombre en dos sitios. Las
// coords salen en almacenamiento (la conversion a y logico la hace el llamador,
// pues un bloque de 2^shift y almacenadas corresponde al bloque de la celda
// resumen tanto en XYZ como en TMS).
QVector<RMapsTileSource::CoverageCell>
RMapsTileSource::coverageHistogram(int logicalZ, int shift)
{
    QVector<CoverageCell> out;
    if (!m_open && !open())
        return out;
    if (!m_ds.zoomInRange(logicalZ))
        return out;
    if (shift < 0)
        shift = 0;

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        m_ds.id, m_ds.filePath, SqliteConnectionPool::Mode::ReadOnly);
    if (!db.isOpen())
        return out;

    QString sql = QStringLiteral(
        "SELECT (%1 >> %5) AS bx, (%2 >> %5) AS by, COUNT(*) AS c "
        "FROM %3 WHERE %4 = :z")
        .arg(m_ds.colX, m_ds.colY, m_ds.tableName, m_ds.colZ)
        .arg(shift);
    if (m_ds.hasSColumn)
        sql += QStringLiteral(" AND %1 = :s").arg(m_ds.colS);
    sql += QStringLiteral(" GROUP BY bx, by");

    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        m_lastError = q.lastError().text();
        qCWarning(lcMapaTiles) << "coverageHistogram prepare fallo:" << m_lastError;
        return out;
    }
    q.bindValue(QStringLiteral(":z"), m_ds.storedZ(logicalZ));
    if (m_ds.hasSColumn)
        q.bindValue(QStringLiteral(":s"), m_ds.sValue);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qCWarning(lcMapaTiles) << "coverageHistogram fallo:" << m_lastError;
        return out;
    }
    while (q.next()) {
        CoverageCell c;
        c.bx = q.value(0).toInt();
        c.by = q.value(1).toInt();
        c.count = q.value(2).toInt();
        out.append(c);
    }
    return out;
}

// Cuantas teselas hay guardadas a un zoom (COUNT por z). -1 si error. Lo usan la
// sonda y las estadisticas de relleno.
qint64 RMapsTileSource::tileCount(int z)
{
    if (!m_open && !open())
        return -1;

    m_qCount->bindValue(QStringLiteral(":z"), m_ds.storedZ(z));
    if (!m_qCount->exec() || !m_qCount->next()) {
        m_lastError = m_qCount->lastError().text();
        return -1;
    }
    const qint64 n = m_qCount->value(0).toLongLong();
    m_qCount->finish();
    return n;
}

// COUNT(*) sobre el mismo WHERE que fetchRange/available (incluida 's'), asi
// que usa el indice (z,x,y,s). No se guarda preparada: es una consulta de
// diagnostico (check_data), no del camino caliente del dibujo.
qint64 RMapsTileSource::countInRange(int z, int xMin, int xMax, int yMin, int yMax)
{
    if (!m_open && !open())
        return -1;
    if (xMax < xMin || yMax < yMin)
        return 0;

    int sy1 = 0, sy2 = 0;
    storageYRange(z, yMin, yMax, &sy1, &sy2);

    QSqlQuery q(SqliteConnectionPool::connectionFor(
        m_ds.id, m_ds.filePath, SqliteConnectionPool::Mode::ReadOnly));
    if (!q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 %2")
                       .arg(m_ds.tableName, whereClause()))) {
        m_lastError = q.lastError().text();
        return -1;
    }
    bindCommon(q, m_ds.storedZ(z), xMin, xMax, sy1, sy2);
    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.value(0).toLongLong();
}

// Primera tesela que encuentre SQLite en ese zoom (con la 's' del dataset si la
// hay). LIMIT 1: no importa cual, solo que exista y se pueda decodificar.
QByteArray RMapsTileSource::anyTile(int z)
{
    if (!m_open && !open())
        return QByteArray();

    QString where = QStringLiteral("WHERE %1 = :z").arg(m_ds.colZ);
    if (m_ds.hasSColumn)
        where += QStringLiteral(" AND %1 = :s").arg(m_ds.colS);

    QSqlQuery q(SqliteConnectionPool::connectionFor(
        m_ds.id, m_ds.filePath, SqliteConnectionPool::Mode::ReadOnly));
    if (!q.prepare(QStringLiteral("SELECT %1 FROM %2 %3 LIMIT 1")
                       .arg(m_ds.colImage, m_ds.tableName, where))) {
        m_lastError = q.lastError().text();
        return QByteArray();
    }
    q.bindValue(QStringLiteral(":z"), m_ds.storedZ(z));
    if (m_ds.hasSColumn)
        q.bindValue(QStringLiteral(":s"), m_ds.sValue);
    if (!q.exec() || !q.next())
        return QByteArray();
    return q.value(0).toByteArray();
}

} // namespace libmapa
