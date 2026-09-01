#include "db/VectorRepository.h"

#include "core/Logging.h"
#include "db/Schema.h"
#include "db/SqliteConnectionPool.h"
#include "db/Transaction.h"

#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QVariant>

namespace libmapa {

namespace {

//! Lee un campo por NOMBRE. Nunca por posicion.
//
// El original leia por indice fijo y se descuadro: la tabla puntos tiene
// (no_punto, nombre, descripcion, tipo, simbolo, latitud, longitud,
// fecha_creacion), pero se comento la linea que leia 'tipo' sin corregir los
// indices siguientes. Resultado: el simbolo se leia de 'tipo', la latitud de
// 'simbolo', la longitud de 'latitud' y la fecha de 'longitud'. Los puntos
// guardados salian sin icono y en el sitio equivocado.
QVariant field(const QSqlQuery &q, const char *name)
{
    const int i = q.record().indexOf(QLatin1String(name));
    return i >= 0 ? q.value(i) : QVariant();
}

QByteArray pixmapToPng(const QPixmap &pm)
{
    if (pm.isNull())
        return {};
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    pm.save(&buf, "PNG");
    return out;
}

QPixmap pngToPixmap(const QByteArray &data)
{
    QPixmap pm;
    if (!data.isEmpty())
        pm.loadFromData(data);
    return pm;
}

/*!
 * \brief Convierte una QString nula en cadena vacia.
 *
 * Una QString por defecto es NULA, y QSqlQuery la enlaza como NULL. Contra un
 * "descripcion TEXT NOT NULL DEFAULT ''" eso es una violacion de restriccion,
 * no un valor por defecto: el DEFAULT solo actua si la columna se OMITE, no
 * si se le pasa NULL explicitamente.
 *
 * En el esquema original esto no daba problemas porque el "NOT nullptr" no
 * llegaba a crear restriccion alguna. Ahora que las restricciones existen de
 * verdad, hay que respetarlas.
 */
QString text(const QString &s)
{
    return s.isNull() ? QString::fromLatin1("") : s;
}

qint64 nowUtcMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

VectorRepository::VectorRepository(QObject *parent)
    : QObject(parent)
{
}

VectorRepository::~VectorRepository()
{
    close();
}

QSqlDatabase VectorRepository::db() const
{
    return SqliteConnectionPool::connectionFor(
        m_connectionId, m_filePath, SqliteConnectionPool::Mode::ReadWrite);
}

bool VectorRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
    qCWarning(lcMapaDb) << m_lastError;
    emit const_cast<VectorRepository *>(this)->errorOccurred(context, message);
    return false;
}

bool VectorRepository::open(const QString &filePath)
{
    close();

    m_filePath = filePath;
    m_connectionId = QStringLiteral("vector_%1")
                         .arg(QFileInfo(filePath).fileName());

    QSqlDatabase database = db();
    if (!database.isOpen())
        return fail(QStringLiteral("open"),
                    tr("No se pudo abrir %1").arg(filePath));

    // Sin esto, ON DELETE CASCADE no hace nada: SQLite trae las claves
    // foraneas desactivadas por omision.
    QSqlQuery pragma(database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")))
        return fail(QStringLiteral("open"), pragma.lastError().text());

    m_open = true;
    if (!migrate()) {
        m_open = false;
        return false;
    }
    return true;
}

bool VectorRepository::isOpen() const
{
    return m_open && db().isOpen();
}

void VectorRepository::close()
{
    m_open = false;
    m_filePath.clear();
    m_connectionId.clear();
}

int VectorRepository::schemaVersion() const
{
    if (!m_open)
        return -1;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT version FROM %1")
                    .arg(QLatin1String(schema::versionTable())))
        || !q.next())
        return 0;
    return q.value(0).toInt();
}

bool VectorRepository::migrate()
{
    QSqlDatabase database = db();
    QSqlQuery q(database);

    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 "
                               "(version INTEGER NOT NULL)")
                    .arg(QLatin1String(schema::versionTable()))))
        return fail(QStringLiteral("migrate"), q.lastError().text());

    int desde = 0;
    if (q.exec(QStringLiteral("SELECT version FROM %1")
                   .arg(QLatin1String(schema::versionTable())))
        && q.next())
        desde = q.value(0).toInt();

    if (desde >= schema::kCurrentVersion)
        return true;

    // Todo el salto de version en una transaccion: o se aplica entero o la
    // BD se queda como estaba. Sin esto, un fallo a mitad deja un esquema
    // incoherente y la siguiente ejecucion ni lo detecta.
    Transaction tx(database);
    if (!tx.isActive())
        return fail(QStringLiteral("migrate"),
                    tr("No se pudo iniciar la transaccion"));

    for (const QString &sql : schema::migrations(desde)) {
        if (!q.exec(sql))
            return fail(QStringLiteral("migrate"),
                        QStringLiteral("%1\nSQL: %2")
                            .arg(q.lastError().text(), sql));
    }

    q.exec(QStringLiteral("DELETE FROM %1")
               .arg(QLatin1String(schema::versionTable())));
    q.prepare(QStringLiteral("INSERT INTO %1 (version) VALUES (:v)")
                  .arg(QLatin1String(schema::versionTable())));
    q.bindValue(QStringLiteral(":v"), schema::kCurrentVersion);
    if (!q.exec())
        return fail(QStringLiteral("migrate"), q.lastError().text());

    if (!tx.commit())
        return fail(QStringLiteral("migrate"), database.lastError().text());

    qCInfo(lcMapaDb) << "Esquema migrado de la version" << desde << "a la"
                     << schema::kCurrentVersion;
    return true;
}

// ---------------------------------------------------------------- puntos ---

std::optional<qint64> VectorRepository::insertPoint(const MapPoint &p)
{
    if (!m_open) {
        fail(QStringLiteral("insertPoint"), tr("Repositorio cerrado"));
        return std::nullopt;
    }
    if (p.name.isEmpty()) {
        fail(QStringLiteral("insertPoint"), tr("El punto necesita un nombre"));
        return std::nullopt;
    }
    if (!p.position.isValid()) {
        fail(QStringLiteral("insertPoint"),
             tr("Coordenada invalida para '%1'").arg(p.name));
        return std::nullopt;
    }

    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO punto (nombre, descripcion, tipo, simbolo, color_rgb,"
        " etiqueta_visible, latitud, longitud, altura, creado_utc)"
        " VALUES (:n, :d, :t, :s, :c, :e, :la, :lo, :al, :cr)"));
    // bindValue siempre: un punto llamado O'Brien rompia el INSERT
    // concatenado del original.
    q.bindValue(QStringLiteral(":n"), text(p.name));
    q.bindValue(QStringLiteral(":d"), text(p.description));
    q.bindValue(QStringLiteral(":t"), 0);
    q.bindValue(QStringLiteral(":s"), pixmapToPng(p.icon));
    q.bindValue(QStringLiteral(":c"), static_cast<int>(p.color.rgb()));
    q.bindValue(QStringLiteral(":e"), p.labelVisible ? 1 : 0);
    q.bindValue(QStringLiteral(":la"), p.position.latitude());
    q.bindValue(QStringLiteral(":lo"), p.position.longitude());
    q.bindValue(QStringLiteral(":al"), p.altitude);
    q.bindValue(QStringLiteral(":cr"), nowUtcMs());

    if (!q.exec()) {
        fail(QStringLiteral("insertPoint"), q.lastError().text());
        return std::nullopt;
    }
    return q.lastInsertId().toLongLong();
}

bool VectorRepository::updatePoint(const MapPoint &p)
{
    if (!m_open || p.id < 0)
        return fail(QStringLiteral("updatePoint"), tr("Punto sin identificador"));

    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE punto SET nombre=:n, descripcion=:d, simbolo=:s, color_rgb=:c,"
        " etiqueta_visible=:e, latitud=:la, longitud=:lo, altura=:al"
        " WHERE id=:id"));
    q.bindValue(QStringLiteral(":n"), text(p.name));
    q.bindValue(QStringLiteral(":d"), text(p.description));
    q.bindValue(QStringLiteral(":s"), pixmapToPng(p.icon));
    q.bindValue(QStringLiteral(":c"), static_cast<int>(p.color.rgb()));
    q.bindValue(QStringLiteral(":e"), p.labelVisible ? 1 : 0);
    q.bindValue(QStringLiteral(":la"), p.position.latitude());
    q.bindValue(QStringLiteral(":lo"), p.position.longitude());
    q.bindValue(QStringLiteral(":al"), p.altitude);
    q.bindValue(QStringLiteral(":id"), p.id);

    if (!q.exec())
        return fail(QStringLiteral("updatePoint"), q.lastError().text());
    return q.numRowsAffected() > 0;
}

bool VectorRepository::removePoint(qint64 id)
{
    if (!m_open)
        return fail(QStringLiteral("removePoint"), tr("Repositorio cerrado"));

    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM punto WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec())
        return fail(QStringLiteral("removePoint"), q.lastError().text());

    // Trayectoria, vehiculo y AIS se van solos por ON DELETE CASCADE. En el
    // original, la tabla trayectorias_<nombre> quedaba huerfana para siempre.
    return q.numRowsAffected() > 0;
}

QVector<MapPoint> VectorRepository::loadPoints() const
{
    QVector<MapPoint> out;
    if (!m_open)
        return out;

    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT * FROM punto ORDER BY id"))) {
        fail(QStringLiteral("loadPoints"), q.lastError().text());
        return out;
    }

    while (q.next()) {
        MapPoint p;
        p.id = field(q, "id").toLongLong();
        p.name = field(q, "nombre").toString();
        p.description = field(q, "descripcion").toString();
        p.icon = pngToPixmap(field(q, "simbolo").toByteArray());
        p.color = QColor::fromRgb(
            static_cast<QRgb>(field(q, "color_rgb").toUInt()));
        p.labelVisible = field(q, "etiqueta_visible").toInt() != 0;
        p.position = QGeoCoordinate(field(q, "latitud").toDouble(),
                                    field(q, "longitud").toDouble());
        p.altitude = field(q, "altura").toDouble();
        out.append(p);
    }
    return out;
}

std::optional<MapPoint> VectorRepository::findPointByName(const QString &name) const
{
    if (!m_open)
        return std::nullopt;

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM punto WHERE nombre=:n"));
    q.bindValue(QStringLiteral(":n"), name);
    if (!q.exec() || !q.next())
        return std::nullopt;

    MapPoint p;
    p.id = field(q, "id").toLongLong();
    p.name = field(q, "nombre").toString();
    p.description = field(q, "descripcion").toString();
    p.icon = pngToPixmap(field(q, "simbolo").toByteArray());
    p.color = QColor::fromRgb(static_cast<QRgb>(field(q, "color_rgb").toUInt()));
    p.labelVisible = field(q, "etiqueta_visible").toInt() != 0;
    p.position = QGeoCoordinate(field(q, "latitud").toDouble(),
                                field(q, "longitud").toDouble());
    p.altitude = field(q, "altura").toDouble();
    return p;
}

// ------------------------------------------------------------- vehiculos ---

std::optional<qint64> VectorRepository::insertVehicle(const MapVehicle &v)
{
    if (!m_open) {
        fail(QStringLiteral("insertVehicle"), tr("Repositorio cerrado"));
        return std::nullopt;
    }

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive()) {
        fail(QStringLiteral("insertVehicle"), tr("Sin transaccion"));
        return std::nullopt;
    }

    MapPoint base;
    base.name = v.name;
    base.position = v.position;
    base.icon = v.icon;
    base.altitude = v.altitude;

    const auto id = insertPoint(base);
    if (!id)
        return std::nullopt;      // rollback automatico al salir del scope

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO vehiculo (punto_id, clase, color_traza, traza_visible,"
        " traza_max_puntos, rumbo, velocidad)"
        " VALUES (:p, :c, :ct, :tv, :tm, :r, :ve)"));
    q.bindValue(QStringLiteral(":p"), *id);
    q.bindValue(QStringLiteral(":c"), static_cast<int>(v.kind));
    q.bindValue(QStringLiteral(":ct"), static_cast<int>(v.trackColor.rgb()));
    q.bindValue(QStringLiteral(":tv"), v.trackVisible ? 1 : 0);
    q.bindValue(QStringLiteral(":tm"), v.trackMaxPoints);
    q.bindValue(QStringLiteral(":r"), v.heading);
    q.bindValue(QStringLiteral(":ve"), v.speed);
    if (!q.exec()) {
        fail(QStringLiteral("insertVehicle"), q.lastError().text());
        return std::nullopt;
    }

    if (v.ais.isValid()) {
        q.prepare(QStringLiteral(
            "INSERT INTO buque_ais (punto_id, mmsi, imo, callsign, nombre_buque,"
            " tipo_embarcacion, bandera, destino, estado_navegacion,"
            " eslora, manga, calado, rumbo, curso, actualizado_utc)"
            " VALUES (:p,:mm,:im,:cs,:nb,:te,:ba,:de,:en,:es,:ma,:ca,:ru,:cu,:ac)"));
        q.bindValue(QStringLiteral(":p"), *id);
        q.bindValue(QStringLiteral(":mm"), text(v.ais.mmsi));
        q.bindValue(QStringLiteral(":im"), text(v.ais.imo));
        q.bindValue(QStringLiteral(":cs"), text(v.ais.callSign));
        q.bindValue(QStringLiteral(":nb"), text(v.ais.shipName));
        q.bindValue(QStringLiteral(":te"), text(v.ais.shipType));
        q.bindValue(QStringLiteral(":ba"), text(v.ais.flag));
        q.bindValue(QStringLiteral(":de"), text(v.ais.destination));
        q.bindValue(QStringLiteral(":en"), text(v.ais.navigationStatus));
        q.bindValue(QStringLiteral(":es"), v.ais.length);
        q.bindValue(QStringLiteral(":ma"), v.ais.beam);
        q.bindValue(QStringLiteral(":ca"), v.ais.draught);
        q.bindValue(QStringLiteral(":ru"), v.ais.heading);
        q.bindValue(QStringLiteral(":cu"), v.ais.course);
        q.bindValue(QStringLiteral(":ac"),
                    v.ais.lastUpdateUtcMs > 0 ? v.ais.lastUpdateUtcMs : nowUtcMs());
        if (!q.exec()) {
            fail(QStringLiteral("insertVehicle/ais"), q.lastError().text());
            return std::nullopt;
        }
    }

    if (!tx.commit()) {
        fail(QStringLiteral("insertVehicle"), database.lastError().text());
        return std::nullopt;
    }
    return id;
}

bool VectorRepository::updateVehicle(const MapVehicle &v)
{
    if (!m_open || v.id < 0)
        return fail(QStringLiteral("updateVehicle"), tr("Vehiculo sin identificador"));

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive())
        return fail(QStringLiteral("updateVehicle"), tr("Sin transaccion"));

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "UPDATE punto SET latitud=:la, longitud=:lo, altura=:al WHERE id=:id"));
    q.bindValue(QStringLiteral(":la"), v.position.latitude());
    q.bindValue(QStringLiteral(":lo"), v.position.longitude());
    q.bindValue(QStringLiteral(":al"), v.altitude);
    q.bindValue(QStringLiteral(":id"), v.id);
    if (!q.exec())
        return fail(QStringLiteral("updateVehicle"), q.lastError().text());

    q.prepare(QStringLiteral(
        "UPDATE vehiculo SET rumbo=:r, velocidad=:ve WHERE punto_id=:id"));
    q.bindValue(QStringLiteral(":r"), v.heading);
    q.bindValue(QStringLiteral(":ve"), v.speed);
    q.bindValue(QStringLiteral(":id"), v.id);
    if (!q.exec())
        return fail(QStringLiteral("updateVehicle"), q.lastError().text());

    return tx.commit();
}

QVector<MapVehicle> VectorRepository::loadVehicles() const
{
    QVector<MapVehicle> out;
    if (!m_open)
        return out;

    // Un JOIN, no una consulta por vehiculo. Con el esquema de una tabla por
    // entidad esto era sencillamente imposible.
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral(
            "SELECT p.id, p.nombre, p.simbolo, p.latitud, p.longitud, p.altura,"
            " v.clase, v.color_traza, v.traza_visible, v.traza_max_puntos,"
            " v.rumbo, v.velocidad,"
            " a.mmsi, a.imo, a.callsign, a.nombre_buque, a.tipo_embarcacion,"
            " a.bandera, a.destino, a.estado_navegacion, a.eslora, a.manga,"
            " a.calado, a.rumbo AS ais_rumbo, a.curso, a.actualizado_utc"
            " FROM vehiculo v"
            " JOIN punto p ON p.id = v.punto_id"
            " LEFT JOIN buque_ais a ON a.punto_id = v.punto_id"
            " ORDER BY p.id"))) {
        fail(QStringLiteral("loadVehicles"), q.lastError().text());
        return out;
    }

    while (q.next()) {
        MapVehicle v;
        v.id = field(q, "id").toLongLong();
        v.name = field(q, "nombre").toString();
        v.icon = pngToPixmap(field(q, "simbolo").toByteArray());
        v.position = QGeoCoordinate(field(q, "latitud").toDouble(),
                                    field(q, "longitud").toDouble());
        v.altitude = field(q, "altura").toDouble();
        v.kind = static_cast<VehicleKind>(field(q, "clase").toInt());
        v.trackColor = QColor::fromRgb(
            static_cast<QRgb>(field(q, "color_traza").toUInt()));
        v.trackVisible = field(q, "traza_visible").toInt() != 0;
        v.trackMaxPoints = field(q, "traza_max_puntos").toInt();
        v.heading = field(q, "rumbo").toDouble();
        v.speed = field(q, "velocidad").toDouble();

        const QVariant mmsi = field(q, "mmsi");
        if (!mmsi.isNull()) {
            v.ais.mmsi = mmsi.toString();
            v.ais.imo = field(q, "imo").toString();
            v.ais.callSign = field(q, "callsign").toString();
            v.ais.shipName = field(q, "nombre_buque").toString();
            v.ais.shipType = field(q, "tipo_embarcacion").toString();
            v.ais.flag = field(q, "bandera").toString();
            v.ais.destination = field(q, "destino").toString();
            v.ais.navigationStatus = field(q, "estado_navegacion").toString();
            v.ais.length = field(q, "eslora").toDouble();
            v.ais.beam = field(q, "manga").toDouble();
            v.ais.draught = field(q, "calado").toDouble();
            v.ais.heading = field(q, "ais_rumbo").toDouble();
            v.ais.course = field(q, "curso").toDouble();
            v.ais.lastUpdateUtcMs = field(q, "actualizado_utc").toLongLong();
        }
        out.append(v);
    }
    return out;
}

QVector<MapVehicle> VectorRepository::loadVesselsUpdatedSince(qint64 since) const
{
    QVector<MapVehicle> out;
    for (const MapVehicle &v : loadVehicles())
        if (v.ais.isValid() && v.ais.lastUpdateUtcMs >= since)
            out.append(v);
    return out;
}

// ---------------------------------------------------------- trayectorias ---

bool VectorRepository::appendTrack(qint64 pointId, const TrackSample &s)
{
    return appendTrackBatch(pointId, {s});
}

bool VectorRepository::appendTrackBatch(qint64 pointId,
                                        const QVector<TrackSample> &samples)
{
    if (!m_open)
        return fail(QStringLiteral("appendTrack"), tr("Repositorio cerrado"));
    if (samples.isEmpty())
        return true;

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive())
        return fail(QStringLiteral("appendTrack"), tr("Sin transaccion"));

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO trayectoria"
        " (punto_id, t_utc, latitud, longitud, altura, rumbo, velocidad)"
        " VALUES (:p, :t, :la, :lo, :al, :r, :v)"));

    for (const TrackSample &s : samples) {
        q.bindValue(QStringLiteral(":p"), pointId);
        q.bindValue(QStringLiteral(":t"),
                    s.timeUtcMs > 0 ? s.timeUtcMs : nowUtcMs());
        q.bindValue(QStringLiteral(":la"), s.position.latitude());
        q.bindValue(QStringLiteral(":lo"), s.position.longitude());
        q.bindValue(QStringLiteral(":al"), s.altitude);
        q.bindValue(QStringLiteral(":r"), s.heading);
        q.bindValue(QStringLiteral(":v"), s.speed);
        if (!q.exec())
            return fail(QStringLiteral("appendTrack"), q.lastError().text());
    }

    return tx.commit();
}

QVector<TrackSample> VectorRepository::loadTrack(qint64 pointId, int limit) const
{
    QVector<TrackSample> out;
    if (!m_open)
        return out;

    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT t_utc, latitud, longitud, altura, rumbo, velocidad"
        " FROM trayectoria WHERE punto_id=:p"
        " ORDER BY t_utc DESC LIMIT :l"));
    q.bindValue(QStringLiteral(":p"), pointId);
    q.bindValue(QStringLiteral(":l"), limit);
    if (!q.exec()) {
        fail(QStringLiteral("loadTrack"), q.lastError().text());
        return out;
    }

    while (q.next()) {
        TrackSample s;
        s.timeUtcMs = field(q, "t_utc").toLongLong();
        s.position = QGeoCoordinate(field(q, "latitud").toDouble(),
                                    field(q, "longitud").toDouble());
        s.altitude = field(q, "altura").toDouble();
        s.heading = field(q, "rumbo").toDouble();
        s.speed = field(q, "velocidad").toDouble();
        out.prepend(s);          // devuelto en orden cronologico
    }
    return out;
}

bool VectorRepository::pruneTrack(qint64 pointId, int keep)
{
    if (!m_open)
        return fail(QStringLiteral("pruneTrack"), tr("Repositorio cerrado"));

    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "DELETE FROM trayectoria WHERE punto_id=:p AND t_utc NOT IN"
        " (SELECT t_utc FROM trayectoria WHERE punto_id=:p2"
        "  ORDER BY t_utc DESC LIMIT :k)"));
    q.bindValue(QStringLiteral(":p"), pointId);
    q.bindValue(QStringLiteral(":p2"), pointId);
    q.bindValue(QStringLiteral(":k"), keep);
    if (!q.exec())
        return fail(QStringLiteral("pruneTrack"), q.lastError().text());
    return true;
}

// ------------------------------------------------------------- poligonos ---

std::optional<qint64> VectorRepository::insertPolygon(const MapPolygon &poly)
{
    if (!m_open) {
        fail(QStringLiteral("insertPolygon"), tr("Repositorio cerrado"));
        return std::nullopt;
    }
    if (poly.vertices.size() < 3) {
        fail(QStringLiteral("insertPolygon"),
             tr("Un poligono necesita al menos tres vertices"));
        return std::nullopt;
    }

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive()) {
        fail(QStringLiteral("insertPolygon"), tr("Sin transaccion"));
        return std::nullopt;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO poligono (nombre, color_linea, color_relleno, relleno,"
        " creado_utc) VALUES (:n, :cl, :cr, :r, :c)"));
    q.bindValue(QStringLiteral(":n"), text(poly.name));
    q.bindValue(QStringLiteral(":cl"), static_cast<int>(poly.lineColor.rgb()));
    q.bindValue(QStringLiteral(":cr"), static_cast<int>(poly.fillColor.rgba()));
    q.bindValue(QStringLiteral(":r"), poly.filled ? 1 : 0);
    q.bindValue(QStringLiteral(":c"), nowUtcMs());
    if (!q.exec()) {
        fail(QStringLiteral("insertPolygon"), q.lastError().text());
        return std::nullopt;
    }

    const qint64 id = q.lastInsertId().toLongLong();

    // Los N vertices en la MISMA transaccion. En el original eran N commits
    // sueltos, cada uno con su fsync.
    q.prepare(QStringLiteral(
        "INSERT INTO poligono_vertice (poligono_id, orden, latitud, longitud)"
        " VALUES (:p, :o, :la, :lo)"));
    for (int i = 0; i < poly.vertices.size(); ++i) {
        q.bindValue(QStringLiteral(":p"), id);
        q.bindValue(QStringLiteral(":o"), i);
        q.bindValue(QStringLiteral(":la"), poly.vertices[i].latitude());
        q.bindValue(QStringLiteral(":lo"), poly.vertices[i].longitude());
        if (!q.exec()) {
            fail(QStringLiteral("insertPolygon/vertice"), q.lastError().text());
            return std::nullopt;
        }
    }

    if (!tx.commit()) {
        fail(QStringLiteral("insertPolygon"), database.lastError().text());
        return std::nullopt;
    }
    return id;
}

bool VectorRepository::removePolygon(qint64 id)
{
    if (!m_open)
        return fail(QStringLiteral("removePolygon"), tr("Repositorio cerrado"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM poligono WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec())
        return fail(QStringLiteral("removePolygon"), q.lastError().text());
    return q.numRowsAffected() > 0;
}

QVector<MapPolygon> VectorRepository::loadPolygons() const
{
    QVector<MapPolygon> out;
    if (!m_open)
        return out;

    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT * FROM poligono ORDER BY id"))) {
        fail(QStringLiteral("loadPolygons"), q.lastError().text());
        return out;
    }

    QVector<qint64> ids;
    while (q.next()) {
        MapPolygon p;
        p.id = field(q, "id").toLongLong();
        p.name = field(q, "nombre").toString();
        p.lineColor = QColor::fromRgb(
            static_cast<QRgb>(field(q, "color_linea").toUInt()));
        p.fillColor = QColor::fromRgba(
            static_cast<QRgb>(field(q, "color_relleno").toUInt()));
        p.filled = field(q, "relleno").toInt() != 0;
        out.append(p);
        ids.append(p.id);
    }

    QSqlQuery qv(db());
    qv.prepare(QStringLiteral(
        "SELECT latitud, longitud FROM poligono_vertice"
        " WHERE poligono_id=:p ORDER BY orden"));
    for (int i = 0; i < out.size(); ++i) {
        qv.bindValue(QStringLiteral(":p"), ids[i]);
        if (!qv.exec())
            continue;
        while (qv.next())
            out[i].vertices.append(QGeoCoordinate(qv.value(0).toDouble(),
                                                  qv.value(1).toDouble()));
    }
    return out;
}

// ----------------------------------------------------------------- rutas ---

std::optional<qint64> VectorRepository::insertRoute(const MapRoute &route)
{
    if (!m_open) {
        fail(QStringLiteral("insertRoute"), tr("Repositorio cerrado"));
        return std::nullopt;
    }

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive()) {
        fail(QStringLiteral("insertRoute"), tr("Sin transaccion"));
        return std::nullopt;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO ruta (nombre, color_rgb, creado_utc)"
        " VALUES (:n, :c, :cr)"));
    q.bindValue(QStringLiteral(":n"), text(route.name));
    q.bindValue(QStringLiteral(":c"), static_cast<int>(route.color.rgb()));
    q.bindValue(QStringLiteral(":cr"), nowUtcMs());
    if (!q.exec()) {
        fail(QStringLiteral("insertRoute"), q.lastError().text());
        return std::nullopt;
    }

    const qint64 id = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral(
        "INSERT INTO ruta_punto (ruta_id, orden, prioridad, descripcion,"
        " latitud, longitud, radio_m) VALUES (:r, :o, :p, :d, :la, :lo, :ra)"));
    for (int i = 0; i < route.points.size(); ++i) {
        const MapRoutePoint &rp = route.points[i];
        q.bindValue(QStringLiteral(":r"), id);
        q.bindValue(QStringLiteral(":o"), i);
        q.bindValue(QStringLiteral(":p"), rp.priority);
        q.bindValue(QStringLiteral(":d"), text(rp.description));
        q.bindValue(QStringLiteral(":la"), rp.position.latitude());
        q.bindValue(QStringLiteral(":lo"), rp.position.longitude());
        q.bindValue(QStringLiteral(":ra"), rp.approachRadiusMeters);
        if (!q.exec()) {
            fail(QStringLiteral("insertRoute/punto"), q.lastError().text());
            return std::nullopt;
        }
    }

    if (!tx.commit()) {
        fail(QStringLiteral("insertRoute"), database.lastError().text());
        return std::nullopt;
    }
    return id;
}

bool VectorRepository::removeRoute(qint64 id)
{
    if (!m_open)
        return fail(QStringLiteral("removeRoute"), tr("Repositorio cerrado"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM ruta WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec())
        return fail(QStringLiteral("removeRoute"), q.lastError().text());
    return q.numRowsAffected() > 0;
}

QVector<MapRoute> VectorRepository::loadRoutes() const
{
    QVector<MapRoute> out;
    if (!m_open)
        return out;

    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT * FROM ruta ORDER BY id"))) {
        fail(QStringLiteral("loadRoutes"), q.lastError().text());
        return out;
    }

    QVector<qint64> ids;
    while (q.next()) {
        MapRoute r;
        r.id = field(q, "id").toLongLong();
        r.name = field(q, "nombre").toString();
        r.color = QColor::fromRgb(
            static_cast<QRgb>(field(q, "color_rgb").toUInt()));
        out.append(r);
        ids.append(r.id);
    }

    QSqlQuery qp(db());
    qp.prepare(QStringLiteral(
        "SELECT prioridad, descripcion, latitud, longitud, radio_m"
        " FROM ruta_punto WHERE ruta_id=:r ORDER BY orden"));
    for (int i = 0; i < out.size(); ++i) {
        qp.bindValue(QStringLiteral(":r"), ids[i]);
        if (!qp.exec())
            continue;
        while (qp.next()) {
            MapRoutePoint rp;
            rp.priority = qp.value(0).toInt();
            rp.description = qp.value(1).toString();
            rp.position = QGeoCoordinate(qp.value(2).toDouble(),
                                         qp.value(3).toDouble());
            rp.approachRadiusMeters = qp.value(4).toDouble();
            out[i].points.append(rp);
        }
    }
    return out;
}

} // namespace libmapa
