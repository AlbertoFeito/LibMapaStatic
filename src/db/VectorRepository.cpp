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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonObject>
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

// Serializa un icono a PNG para guardarlo como BLOB. Un QPixmap nulo da un
// QByteArray vacio (no NULL), que es lo que espera la columna.
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

// Inverso de pixmapToPng: reconstruye el icono desde el BLOB. Un BLOB vacio
// (o NULL) devuelve un QPixmap nulo, que la vista trata como "sin icono".
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

// Marca de tiempo actual en milisegundos UTC (epoch). Es lo que guardan las
// columnas creado_utc/actualizado_utc: comparable e indexable en SQL.
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

// Conexion de escritura para este repositorio, servida por el pool (una por
// hilo). No abre nada nuevo si ya existe: delega toda la gestion en el pool.
QSqlDatabase VectorRepository::db() const
{
    return SqliteConnectionPool::connectionFor(
        m_connectionId, m_filePath, SqliteConnectionPool::Mode::ReadWrite);
}

// Registra un error (guarda lastError, lo escribe en el log y emite
// errorOccurred) y SIEMPRE devuelve false, para poder escribir
// "return fail(...)" en un solo renglon dentro de las operaciones.
bool VectorRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
    qCWarning(lcMapaDb) << m_lastError;
    emit const_cast<VectorRepository *>(this)->errorOccurred(context, message);
    return false;
}

// Abre (o crea) la BD vectorial de 'filePath': fija el id de conexion, activa
// las claves foraneas (imprescindible para ON DELETE CASCADE) y aplica las
// migraciones pendientes. Devuelve false y deja el repositorio cerrado si algo
// falla. Cierra cualquier BD previa antes de empezar.
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

// ¿Hay una BD abierta y utilizable? Comprueba tanto el flag interno como que la
// conexion subyacente siga viva.
bool VectorRepository::isOpen() const
{
    return m_open && db().isOpen();
}

// Marca el repositorio como cerrado y olvida la ruta/id. No cierra fisicamente la
// conexion del pool: de eso se encarga el pool al terminar el hilo.
void VectorRepository::close()
{
    m_open = false;
    m_filePath.clear();
    m_connectionId.clear();
}

// Version de esquema guardada en la BD (-1 si esta cerrada, 0 si aun no hay tabla
// de version). Sirve para diagnostico y pruebas de migracion.
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

// Lleva el esquema a la version actual. Crea la tabla de version si falta, lee la
// version actual y, si esta atrasada, aplica TODAS las sentencias de
// schema::migrations dentro de una unica transaccion (todo o nada) y actualiza el
// numero de version. Idempotente: si ya esta al dia, no hace nada.
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

// ------------------------------------------------------- entidades dibujo --

// Guarda UNA entidad de dibujo (punto/polilinea/poligono con su estilo y
// atributos) en su propia transaccion. Delega la escritura en writeFeature.
std::optional<qint64> VectorRepository::saveFeature(const MapFeature &f)
{
    if (!m_open) {
        fail(QStringLiteral("saveFeature"), tr("Repositorio cerrado"));
        return std::nullopt;
    }

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive()) {
        fail(QStringLiteral("saveFeature"), tr("Sin transaccion"));
        return std::nullopt;
    }

    const auto id = writeFeature(database, f);
    if (!id)
        return std::nullopt;
    if (!tx.commit()) {
        fail(QStringLiteral("saveFeature"), database.lastError().text());
        return std::nullopt;
    }
    return id;
}

// Escribe una entidad SIN abrir transaccion propia (asume que el llamador ya la
// abrio: asi saveFeature guarda una y saveFeatures guarda muchas atomicamente).
// Inserta la fila 'entidad' (estilo + atributos como JSON) y sus vertices,
// agrupados por 'parte' para soportar geometrias multi-parte. Devuelve el id.
std::optional<qint64> VectorRepository::writeFeature(QSqlDatabase &database,
                                                     const MapFeature &f)
{
    if (!f.isValid()) {
        fail(QStringLiteral("writeFeature"),
             tr("Geometria invalida en '%1'").arg(f.name));
        return std::nullopt;
    }

    QSqlQuery q(database);
    const QString atributos = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(f.attributes))
            .toJson(QJsonDocument::Compact));

    q.prepare(QStringLiteral(
        "INSERT INTO entidad (capa, tipo, geometria, nombre, descripcion,"
        " color_linea, color_relleno, ancho_linea, estilo_linea, radio_px,"
        " etiqueta_visible, visible, simbolo, atributos, creado_utc)"
        " VALUES (:capa,:tipo,:geo,:nom,:desc,:cl,:cr,:anc,:el,:rp,:ev,:vi,"
        " :sim,:atr,:cu)"));
    q.bindValue(QStringLiteral(":capa"), text(f.layerId));
    q.bindValue(QStringLiteral(":tipo"), text(f.type));
    q.bindValue(QStringLiteral(":geo"), static_cast<int>(f.kind));
    q.bindValue(QStringLiteral(":nom"), text(f.name));
    q.bindValue(QStringLiteral(":desc"), text(f.description));
    q.bindValue(QStringLiteral(":cl"), static_cast<int>(f.style.lineColor.rgba()));
    q.bindValue(QStringLiteral(":cr"), static_cast<int>(f.style.fillColor.rgba()));
    q.bindValue(QStringLiteral(":anc"), f.style.lineWidth);
    q.bindValue(QStringLiteral(":el"), static_cast<int>(f.style.lineStyle));
    q.bindValue(QStringLiteral(":rp"), f.style.pointRadiusPx);
    q.bindValue(QStringLiteral(":ev"), f.style.labelVisible ? 1 : 0);
    q.bindValue(QStringLiteral(":vi"), f.visible ? 1 : 0);
    q.bindValue(QStringLiteral(":sim"), pixmapToPng(f.style.icon));
    q.bindValue(QStringLiteral(":atr"), atributos);
    q.bindValue(QStringLiteral(":cu"), nowUtcMs());

    if (!q.exec()) {
        fail(QStringLiteral("writeFeature"), q.lastError().text());
        return std::nullopt;
    }

    const qint64 id = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral(
        "INSERT INTO entidad_vertice (entidad_id, parte, orden, latitud, longitud)"
        " VALUES (:e,:p,:o,:la,:lo)"));
    // Una fila por vertice, agrupadas por parte. Las entidades de una sola
    // parte se guardan como parte 0.
    const auto partes = f.outlines();
    for (int p = 0; p < partes.size(); ++p) {
        const QVector<QGeoCoordinate> &parte = partes[p];
        for (int i = 0; i < parte.size(); ++i) {
            q.bindValue(QStringLiteral(":e"), id);
            q.bindValue(QStringLiteral(":p"), p);
            q.bindValue(QStringLiteral(":o"), i);
            q.bindValue(QStringLiteral(":la"), parte[i].latitude());
            q.bindValue(QStringLiteral(":lo"), parte[i].longitude());
            if (!q.exec()) {
                fail(QStringLiteral("writeFeature/vertice"), q.lastError().text());
                return std::nullopt;
            }
        }
    }

    return id;
}

// Guarda un lote de entidades en UNA sola transaccion: o entran todas o ninguna
// (si una falla, el guard RAII deshace el resto). Util al importar un .geo entero.
bool VectorRepository::saveFeatures(const QVector<MapFeature> &features)
{
    if (!m_open)
        return fail(QStringLiteral("saveFeatures"), tr("Repositorio cerrado"));

    QSqlDatabase database = db();
    Transaction tx(database);
    if (!tx.isActive())
        return fail(QStringLiteral("saveFeatures"), tr("Sin transaccion"));

    for (const MapFeature &f : features) {
        if (!writeFeature(database, f))
            return false;      // rollback al salir del scope
    }
    return tx.commit();
}

// Borra una entidad de dibujo por id; sus vertices caen por ON DELETE CASCADE.
bool VectorRepository::removeFeatureRow(qint64 id)
{
    if (!m_open)
        return fail(QStringLiteral("removeFeatureRow"), tr("Repositorio cerrado"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM entidad WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec())
        return fail(QStringLiteral("removeFeatureRow"), q.lastError().text());
    return q.numRowsAffected() > 0;
}

// Vacia por completo la tabla de entidades (y en cascada sus vertices). Se usa
// antes de reimportar para dejar el lienzo limpio.
bool VectorRepository::clearFeatures()
{
    if (!m_open)
        return fail(QStringLiteral("clearFeatures"), tr("Repositorio cerrado"));
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("DELETE FROM entidad")))
        return fail(QStringLiteral("clearFeatures"), q.lastError().text());
    return true;
}

// Carga todas las entidades de dibujo con su estilo y atributos (JSON), y luego
// sus vertices agrupados por 'parte'. Es tolerante con ficheros antiguos: si la
// columna 'parte' no existe, cae a una consulta de una sola parte. La primera
// parte va a geometry; si hay mas de una, se guarda tambien en parts (multi-parte).
QVector<MapFeature> VectorRepository::loadFeatures() const
{
    QVector<MapFeature> out;
    if (!m_open)
        return out;

    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT * FROM entidad ORDER BY id"))) {
        fail(QStringLiteral("loadFeatures"), q.lastError().text());
        return out;
    }

    QVector<qint64> ids;
    while (q.next()) {
        MapFeature f;
        f.id = field(q, "id").toLongLong();
        f.layerId = field(q, "capa").toString();
        f.type = field(q, "tipo").toString();
        f.kind = static_cast<GeometryKind>(field(q, "geometria").toInt());
        f.name = field(q, "nombre").toString();
        f.description = field(q, "descripcion").toString();
        f.style.lineColor = QColor::fromRgba(
            static_cast<QRgb>(field(q, "color_linea").toUInt()));
        f.style.fillColor = QColor::fromRgba(
            static_cast<QRgb>(field(q, "color_relleno").toUInt()));
        f.style.lineWidth = field(q, "ancho_linea").toDouble();
        f.style.lineStyle = static_cast<Qt::PenStyle>(
            field(q, "estilo_linea").toInt());
        f.style.pointRadiusPx = field(q, "radio_px").toDouble();
        f.style.labelVisible = field(q, "etiqueta_visible").toInt() != 0;
        f.style.icon = pngToPixmap(field(q, "simbolo").toByteArray());
        f.visible = field(q, "visible").toInt() != 0;

        const QJsonDocument doc = QJsonDocument::fromJson(
            field(q, "atributos").toString().toUtf8());
        if (doc.isObject())
            f.attributes = doc.object().toVariantMap();

        out.append(f);
        ids.append(f.id);
    }

    // 'parte' agrupa los vertices de una geometria multi-parte. Un fichero
    // guardado con el esquema anterior no tiene esa columna: se detecta si la
    // consulta con 'parte' falla y se cae a la de una sola parte.
    bool conParte = true;
    {
        QSqlQuery prueba(db());
        conParte = prueba.exec(QStringLiteral(
            "SELECT parte FROM entidad_vertice LIMIT 0"));
    }

    QSqlQuery qv(db());
    qv.prepare(conParte
        ? QStringLiteral("SELECT parte, latitud, longitud FROM entidad_vertice"
                         " WHERE entidad_id=:e ORDER BY parte, orden")
        : QStringLiteral("SELECT 0 AS parte, latitud, longitud FROM entidad_vertice"
                         " WHERE entidad_id=:e ORDER BY orden"));

    for (int i = 0; i < out.size(); ++i) {
        qv.bindValue(QStringLiteral(":e"), ids[i]);
        if (!qv.exec())
            continue;

        QVector<QVector<QGeoCoordinate>> partes;
        int parteActual = -1;
        while (qv.next()) {
            const int parte = qv.value(0).toInt();
            const QGeoCoordinate c(qv.value(1).toDouble(), qv.value(2).toDouble());
            if (parte != parteActual) {
                partes.append(QVector<QGeoCoordinate>());
                parteActual = parte;
            }
            partes.last().append(c);
        }

        if (partes.isEmpty())
            continue;
        out[i].geometry = partes.first();              // la primera parte
        if (partes.size() > 1)
            out[i].parts = partes;                     // multi-parte
    }
    return out;
}

// Entidades de una capa concreta. Filtra en memoria sobre loadFeatures().
QVector<MapFeature> VectorRepository::loadFeaturesInLayer(const QString &capa) const
{
    QVector<MapFeature> out;
    for (const MapFeature &f : loadFeatures())
        if (f.layerId == capa)
            out.append(f);
    return out;
}

// Crea o actualiza una capa (INSERT OR REPLACE por id). Guardar la capa aparte
// permite que una capa vacia conserve su visibilidad, editabilidad y orden Z.
bool VectorRepository::saveLayer(const LayerInfo &capa)
{
    if (!m_open)
        return fail(QStringLiteral("saveLayer"), tr("Repositorio cerrado"));

    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO capa (id, nombre, visible, editable, z_orden)"
        " VALUES (:id,:n,:v,:e,:z)"));
    q.bindValue(QStringLiteral(":id"), text(capa.id));
    q.bindValue(QStringLiteral(":n"), text(capa.displayName));
    q.bindValue(QStringLiteral(":v"), capa.visible ? 1 : 0);
    q.bindValue(QStringLiteral(":e"), capa.editable ? 1 : 0);
    q.bindValue(QStringLiteral(":z"), capa.zOrder);
    if (!q.exec())
        return fail(QStringLiteral("saveLayer"), q.lastError().text());
    return true;
}

// Carga todas las capas ordenadas por orden Z (y luego id): ese orden es el de
// pintado, de abajo a arriba.
QVector<LayerInfo> VectorRepository::loadLayers() const
{
    QVector<LayerInfo> out;
    if (!m_open)
        return out;

    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT * FROM capa ORDER BY z_orden, id")))
        return out;

    while (q.next()) {
        LayerInfo c;
        c.id = field(q, "id").toString();
        c.displayName = field(q, "nombre").toString();
        c.visible = field(q, "visible").toInt() != 0;
        c.editable = field(q, "editable").toInt() != 0;
        c.zOrder = field(q, "z_orden").toInt();
        out.append(c);
    }
    return out;
}

} // namespace libmapa
