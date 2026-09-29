#include "TileFiller.h"

#include "geo/TileMatrix.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace libmapa {

namespace {

//! Sustituye {z} {x} {y} en la plantilla (en cualquier orden).
QString buildUrl(const QString &tpl, int z, int x, int y)
{
    QString u = tpl;
    u.replace(QStringLiteral("{z}"), QString::number(z));
    u.replace(QStringLiteral("{x}"), QString::number(x));
    u.replace(QStringLiteral("{y}"), QString::number(y));
    return u;
}

} // namespace

TileFiller::TileFiller(QObject *parent)
    : QObject(parent)
{
}

TileFiller::~TileFiller()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    delete m_ins;
    if (m_db) {
        const QString name = m_connName;
        m_db->close();
        delete m_db;
        m_db = nullptr;
        if (!name.isEmpty())
            QSqlDatabase::removeDatabase(name);
    }
}

bool TileFiller::prepare(const Params &params, QString *error)
{
    m_p = params;

    // Conexion propia (nombre unico por instancia) para no chocar con otras.
    m_connName = QStringLiteral("tilefiller_%1")
                     .arg(reinterpret_cast<quintptr>(this));
    m_db = new QSqlDatabase(
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName));
    m_db->setDatabaseName(m_p.ds.filePath);
    if (!m_db->open()) {
        if (error) *error = QStringLiteral("No se pudo abrir %1: %2")
                                .arg(m_p.ds.filePath, m_db->lastError().text());
        return false;
    }
    // El mapa puede estar LEYENDO esta misma BD mientras escribimos: con un
    // busy_timeout, la escritura espera al lector en vez de fallar por lock.
    QSqlQuery(*m_db).exec(QStringLiteral("PRAGMA busy_timeout=5000"));

    // Base NUEVA: crea la tabla si no existe, con las columnas del dataset. En
    // una base ya existente esto es un no-op (IF NOT EXISTS).
    if (m_p.createSchema) {
        const TileDataset &d = m_p.ds;
        QString cols = QStringLiteral("%1 INTEGER,%2 INTEGER,%3 INTEGER")
                           .arg(d.colX, d.colY, d.colZ);
        QString pk = QStringLiteral("%1,%2,%3").arg(d.colX, d.colY, d.colZ);
        if (d.hasSColumn) {
            cols += QStringLiteral(",%1 INTEGER").arg(d.colS);
            pk   += QStringLiteral(",%1").arg(d.colS);
        }
        cols += QStringLiteral(",%1 BLOB").arg(d.colImage);
        QSqlQuery cq(*m_db);
        if (!cq.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS %1 (%2, PRIMARY KEY(%3))")
                .arg(d.tableName, cols, pk))) {
            if (error) *error = QStringLiteral("No se pudo crear la tabla: %1")
                                    .arg(cq.lastError().text());
            return false;
        }
    }

    const TileDataset &ds = m_p.ds;
    m_plan.clear();
    m_total = 0;
    m_cells = 0;

    for (int z = m_p.minZoom; z <= m_p.maxZoom; ++z) {
        const int n = TileMatrix::tilesPerSide(z);
        int x0 = int(std::floor(TileMatrix::longitudeToTileX(m_p.lonW, z)));
        int x1 = int(std::floor(TileMatrix::longitudeToTileX(m_p.lonE, z)));
        int y0 = int(std::floor(TileMatrix::latitudeToTileY(m_p.latN, z)));
        int y1 = int(std::floor(TileMatrix::latitudeToTileY(m_p.latS, z)));
        x0 = qBound(0, x0, n - 1); x1 = qBound(0, x1, n - 1);
        y0 = qBound(0, y0, n - 1); y1 = qBound(0, y1, n - 1);
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);

        ZoomPlan zp;
        zp.z = z;
        zp.storedZ = ds.storedZ(z);
        zp.x0 = x0; zp.x1 = x1; zp.y0 = y0; zp.y1 = y1;

        const int sy0 = TileMatrix::toStorageY(y0, z, ds.scheme);
        const int sy1 = TileMatrix::toStorageY(y1, z, ds.scheme);
        const int syMin = std::min(sy0, sy1);
        const int syMax = std::max(sy0, sy1);

        const qint64 celdas = qint64(x1 - x0 + 1) * qint64(y1 - y0 + 1);
        qint64 faltan = celdas;

        if (!m_p.overwrite) {
            QString sql = QStringLiteral(
                "SELECT %1,%2 FROM %3 WHERE %4=:z AND %1 BETWEEN :x0 AND :x1 "
                "AND %2 BETWEEN :y0 AND :y1")
                .arg(ds.colX, ds.colY, ds.tableName, ds.colZ);
            if (ds.hasSColumn)
                sql += QStringLiteral(" AND %1=:s").arg(ds.colS);
            QSqlQuery q(*m_db);
            q.prepare(sql);
            q.bindValue(QStringLiteral(":z"), zp.storedZ);
            q.bindValue(QStringLiteral(":x0"), x0);
            q.bindValue(QStringLiteral(":x1"), x1);
            q.bindValue(QStringLiteral(":y0"), syMin);
            q.bindValue(QStringLiteral(":y1"), syMax);
            if (ds.hasSColumn)
                q.bindValue(QStringLiteral(":s"), ds.sValue);
            if (q.exec()) {
                while (q.next())
                    zp.present.insert(qMakePair(q.value(0).toInt(),
                                                q.value(1).toInt()));
            }
            faltan = celdas - zp.present.size();
            if (faltan < 0) faltan = 0;
        }

        m_plan.append(zp);
        m_perZoom.append(qMakePair(z, faltan));
        m_total += faltan;
        m_cells += celdas;
    }

    return true;
}

void TileFiller::start()
{
    if (m_running || !m_db)
        return;
    m_running = true;
    m_cancelled = false;
    m_done = 0;
    m_zi = 0;
    m_attempt = 0;
    m_stats = Stats{};
    m_minIntervalMs = m_p.rate > 0 ? qint64(1000.0 / m_p.rate) : 0;

    m_nam = new QNetworkAccessManager(this);

    QString sql = QStringLiteral("INSERT OR REPLACE INTO %1 (%2,%3,%4")
                      .arg(m_p.ds.tableName, m_p.ds.colX, m_p.ds.colY, m_p.ds.colZ);
    if (m_p.ds.hasSColumn) sql += QStringLiteral(",%1").arg(m_p.ds.colS);
    sql += QStringLiteral(",%1) VALUES (:x,:y,:z").arg(m_p.ds.colImage);
    if (m_p.ds.hasSColumn) sql += QStringLiteral(",:s");
    sql += QStringLiteral(",:img)");
    m_ins = new QSqlQuery(*m_db);
    m_ins->prepare(sql);

    // m_zcursor: empezamos ANTES del primer nivel; pump() busca la primera que
    // falta y lanza su peticion.
    // m_idx se reinicia por nivel dentro de advanceCursor().
    m_idxInZoom = 0;
    m_clock.start();

    if (m_total == 0) {
        finish(false);
        return;
    }
    // Buscar la primera y lanzar.
    if (!advanceCursor())
        finish(false);
    else
        pump();
}

void TileFiller::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
    // finish se emitira desde onReplyFinished (por el abort) o aqui si no hay
    // peticion en vuelo.
    if (!m_reply)
        finish(true);
}

bool TileFiller::advanceCursor()
{
    while (m_zi < m_plan.size()) {
        ZoomPlan &zp = m_plan[m_zi];
        const qint64 w = zp.x1 - zp.x0 + 1;
        const qint64 h = zp.y1 - zp.y0 + 1;
        while (m_idxInZoom < w * h) {
            const qint64 i = m_idxInZoom++;
            const int x = zp.x0 + int(i / h);
            const int y = zp.y0 + int(i % h);
            const int sy = TileMatrix::toStorageY(y, zp.z, m_p.ds.scheme);
            if (m_p.overwrite || !zp.present.contains(qMakePair(x, sy))) {
                m_cx = x; m_cy = y; m_curStoredY = sy;
                m_attempt = 0;
                return true;
            }
        }
        // Nivel terminado.
        emit zoomFinished(zp.z, zp.added);
        ++m_zi;
        m_idxInZoom = 0;
    }
    return false;
}

void TileFiller::pump()
{
    if (m_cancelled) {
        finish(true);
        return;
    }
    // Lanza la peticion de la tesela actual (m_zi, m_cx, m_cy).
    const ZoomPlan &zp = m_plan[m_zi];
    const QUrl u(buildUrl(m_p.url, zp.z, m_cx, m_cy));

    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::UserAgentHeader, m_p.userAgent);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &TileFiller::onReplyFinished);

    if (!m_timeout) {
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        connect(m_timeout, &QTimer::timeout, this, [this] {
            if (m_reply) m_reply->abort();   // dispara finished con error
        });
    }
    m_timeout->start(m_p.timeoutMs);
}

void TileFiller::onReplyFinished()
{
    if (!m_reply)
        return;
    if (m_timeout)
        m_timeout->stop();

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    // Solo se lee el cuerpo si la respuesta NO dio error: leer un reply
    // abortado/errado dispara el aviso "QIODevice::read: device not open".
    const QByteArray body =
        (netErr == QNetworkReply::NoError) ? reply->readAll() : QByteArray();
    const QString ctype =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QString errStr = reply->errorString();
    reply->deleteLater();

    if (m_cancelled) {
        finish(true);
        return;
    }

    enum { Ok, NotFound, Failed } res;
    if (status == 404 || status == 204)
        res = NotFound;
    else if (netErr != QNetworkReply::NoError
             || (status != 0 && status != 200)
             || body.isEmpty()
             || (!ctype.isEmpty() && !ctype.startsWith(QLatin1String("image"))))
        res = Failed;
    else
        res = Ok;

    bool consume = true;   // ¿pasamos a la siguiente tesela?

    if (res == Ok) {
        insertTile(body);
        ++m_stats.downloaded;
        if (m_zi < m_plan.size())
            ++m_plan[m_zi].added;
    } else if (res == NotFound) {
        ++m_stats.notFound;
    } else {  // Failed
        if (m_attempt < m_p.retries) {
            ++m_attempt;
            consume = false;   // reintentar la MISMA con backoff
            emit message(QStringLiteral("reintento %1 z%2 x%3 y%4: %5")
                             .arg(m_attempt).arg(m_plan[m_zi].z)
                             .arg(m_cx).arg(m_cy).arg(errStr));
        } else {
            ++m_stats.failed;
            emit message(QStringLiteral("fallo z%1 x%2 y%3: %4")
                             .arg(m_plan[m_zi].z).arg(m_cx).arg(m_cy).arg(errStr));
        }
    }

    if (consume) {
        ++m_done;
        const double secs = double(m_clock.elapsed()) / 1000.0;
        const double tps = secs > 0 ? double(m_stats.downloaded) / secs : 0.0;
        emit progress(m_done, m_total, tps);
    }

    // Programa la siguiente accion respetando el ritmo (o backoff si reintenta).
    int delay = int(m_minIntervalMs);
    if (!consume)
        delay = 300 * m_attempt;   // backoff creciente

    QTimer::singleShot(delay, this, [this, consume] {
        if (m_cancelled) { finish(true); return; }
        if (consume) {
            if (!advanceCursor()) { finish(false); return; }
        }
        pump();
    });
}

void TileFiller::insertTile(const QByteArray &image)
{
    m_ins->bindValue(QStringLiteral(":x"), m_cx);
    m_ins->bindValue(QStringLiteral(":y"), m_curStoredY);
    m_ins->bindValue(QStringLiteral(":z"), m_plan[m_zi].storedZ);
    if (m_p.ds.hasSColumn)
        m_ins->bindValue(QStringLiteral(":s"), m_p.ds.sValue);
    m_ins->bindValue(QStringLiteral(":img"), image);
    if (!m_ins->exec()) {
        --m_stats.downloaded;
        ++m_stats.failed;
        emit message(QStringLiteral("INSERT fallo: %1")
                         .arg(m_ins->lastError().text()));
    }
}

void TileFiller::finish(bool cancelled)
{
    if (!m_running)
        return;
    m_running = false;
    // Emite el zoomFinished del ultimo nivel si quedo a medias por cancelacion.
    emit finished(m_stats, cancelled);
}

} // namespace libmapa
