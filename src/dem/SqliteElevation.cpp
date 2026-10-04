#include "dem/SqliteElevation.h"

#include "db/SqliteConnectionPool.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>   // Qt5 solo declara QVariant aqui para bindValue/value

namespace libmapa {

// Fija la BD de elevacion y vacia la cache. El id de conexion es unico por
// instancia (el pool abre ademas una conexion por hilo), para no chocar con otras
// fuentes ni con las BD de teselas.
void SqliteElevation::setDatabase(const QString &dbFile)
{
    m_dbFile = dbFile;
    m_connId = QStringLiteral("dem_%1").arg(reinterpret_cast<quintptr>(this));
    clearCache();
}

// Consulta el tile (lat,lon) en la BD y descomprime su blob a las muestras crudas
// (int16 big-endian), que es justo lo que espera GridElevation. Abre la BD en
// solo lectura por el pool (una conexion por hilo, con busy_timeout). Devuelve
// false si no hay BD, no abre, no hay fila, o el blob no descomprime.
bool SqliteElevation::loadTile(int latFloor, int lonFloor,
                               QByteArray &data, int &side) const
{
    if (m_dbFile.isEmpty())
        return false;

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        m_connId, m_dbFile, SqliteConnectionPool::Mode::ReadOnly);
    if (!db.isOpen())
        return false;

    QSqlQuery q(db);
    if (!q.prepare(QStringLiteral(
            "SELECT side, data FROM dem_tiles WHERE lat = :lat AND lon = :lon")))
        return false;
    q.bindValue(QStringLiteral(":lat"), latFloor);
    q.bindValue(QStringLiteral(":lon"), lonFloor);
    if (!q.exec() || !q.next())
        return false;

    const int s = q.value(0).toInt();
    const QByteArray comp = q.value(1).toByteArray();
    const QByteArray raw = qUncompress(comp);
    if (raw.isEmpty() || s < 2)
        return false;

    data = raw;
    side = s;
    return true;
}

} // namespace libmapa
