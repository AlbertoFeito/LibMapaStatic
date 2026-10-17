#include "tiles/MBTiles.h"

#include <QFileInfo>
#include <QImage>
// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que hay que
// pedirlo explicitamente: sin esto, bindValue() no compila en Qt 5.14.
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include <atomic>

namespace libmapa {
namespace MBTiles {

namespace {

// Abre un fichero MBTiles en solo lectura con una conexion propia y temporal; la
// cierra y la desregistra al salir (asi no deja el fichero abierto, que en
// Windows impediria borrarlo o moverlo).
class TempConnection
{
public:
    explicit TempConnection(const QString &file)
    {
        static std::atomic<int> counter{0};
        m_name = QStringLiteral("libmapa_mbtiles_%1").arg(++counter);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(file);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        m_open = db.open();
        m_error = m_open ? QString() : db.lastError().text();
    }

    ~TempConnection()
    {
        if (QSqlDatabase::contains(m_name)) {
            {
                QSqlDatabase db = QSqlDatabase::database(m_name, false);
                if (db.isOpen())
                    db.close();
            }
            QSqlDatabase::removeDatabase(m_name);
        }
    }

    bool isOpen() const { return m_open; }
    QString error() const { return m_error; }
    QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }

private:
    QString m_name;
    bool m_open = false;
    QString m_error;
};

// Deja el motivo en *error (si lo piden) y devuelve false.
bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}

// Identificador de capa a partir del nombre del fichero: minusculas y solo
// letras, cifras y guion bajo.
QString idFromFile(const QString &path)
{
    QString id = QFileInfo(path).completeBaseName().toLower();
    for (QChar &c : id)
        if (!c.isLetterOrNumber())
            c = QLatin1Char('_');
    return id.isEmpty() ? QStringLiteral("mbtiles") : id;
}

// Contenido de la tabla `metadata` como pares nombre -> valor.
QHash<QString, QString> readMetadata(const QSqlDatabase &db)
{
    QHash<QString, QString> meta;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT name, value FROM metadata")))
        while (q.next())
            meta.insert(q.value(0).toString().toLower(), q.value(1).toString());
    return meta;
}

// Zona de `metadata.bounds` ("oeste,sur,este,norte") o inválida.
QGeoRectangle parseBounds(const QString &text)
{
    const QStringList p = text.split(QLatin1Char(','));
    if (p.size() != 4)
        return QGeoRectangle();
    bool a = false, b = false, c = false, d = false;
    const double w = p[0].toDouble(&a), s = p[1].toDouble(&b);
    const double e = p[2].toDouble(&c), n = p[3].toDouble(&d);
    if (!(a && b && c && d) || n < s || e < w)
        return QGeoRectangle();
    return QGeoRectangle(QGeoCoordinate(n, w), QGeoCoordinate(s, e));
}

// ¿Los bytes parecen una imagen ráster conocida (PNG, JPEG, WebP, GIF)?
bool looksLikeRaster(const QByteArray &b)
{
    return b.startsWith("\x89PNG") || b.startsWith("\xFF\xD8")
           || (b.size() > 12 && b.startsWith("RIFF") && b.mid(8, 4) == "WEBP")
           || b.startsWith("GIF8");
}

} // namespace

// Ver la cabecera: la extension .mbtiles, sin distinguir mayusculas.
bool isMBTilesPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("mbtiles"), Qt::CaseInsensitive) == 0;
}

// Fija columnas, esquema y mapeo de zoom propios de MBTiles.
void applyLayout(TileDataset &ds)
{
    ds.mbtiles = true;
    ds.tableName = QStringLiteral("tiles");
    ds.colZ = QStringLiteral("zoom_level");
    ds.colX = QStringLiteral("tile_column");
    ds.colY = QStringLiteral("tile_row");
    ds.colImage = QStringLiteral("tile_data");
    ds.hasSColumn = false;
    ds.scheme = TileScheme::TMS;
    ds.zFactor = 1;
    ds.zOffset = 0;
}

// Lee el fichero y completa lo que \a given no fija. El orden: abrir, comprobar la
// tabla y sus columnas, leer metadatos, mirar una tesela real (formato y lado) y
// aplicar.
bool describe(TileDataset &ds, const QJsonObject &given, QString *error)
{
    if (!QFileInfo::exists(ds.filePath))
        return fail(error, QStringLiteral("No existe %1").arg(ds.filePath));

    TempConnection conn(ds.filePath);
    if (!conn.isOpen())
        return fail(error, QStringLiteral("No se pudo abrir %1: %2").arg(ds.filePath, conn.error()));
    const QSqlDatabase db = conn.db();

    // La tabla (o vista: los MBTiles con teselas repetidas la usan) y sus columnas.
    QSqlQuery q(db);
    QStringList columns;
    if (q.exec(QStringLiteral("PRAGMA table_info(tiles)")))
        while (q.next())
            columns << q.value(1).toString().toLower();
    for (const char *needed : { "zoom_level", "tile_column", "tile_row", "tile_data" })
        if (!columns.contains(QLatin1String(needed)))
            return fail(error, QStringLiteral("%1 no es un MBTiles: la tabla `tiles` no tiene la "
                                              "columna %2").arg(ds.filePath, QLatin1String(needed)));

    const QHash<QString, QString> meta = readMetadata(db);
    const QString format = meta.value(QStringLiteral("format")).toLower();
    if (format == QLatin1String("pbf") || format == QLatin1String("mvt"))
        return fail(error, QStringLiteral("%1 contiene teselas vectoriales (%2): solo se admiten "
                                          "MBTiles ráster (png/jpg/webp)").arg(ds.filePath, format));

    // Rango de zooms: lo declarado, o lo que hay.
    bool okMin = false, okMax = false;
    int minZ = meta.value(QStringLiteral("minzoom")).toInt(&okMin);
    int maxZ = meta.value(QStringLiteral("maxzoom")).toInt(&okMax);
    if (!okMin || !okMax) {
        if (q.exec(QStringLiteral("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles")) && q.next()
            && !q.value(0).isNull()) {
            minZ = q.value(0).toInt();
            maxZ = q.value(1).toInt();
        } else {
            return fail(error, QStringLiteral("%1 no tiene teselas").arg(ds.filePath));
        }
    }

    // Una tesela real: confirma que es ráster y da el lado en píxeles.
    int tileSize = 0;
    QString imageProblem;
    q.prepare(QStringLiteral("SELECT tile_data FROM tiles WHERE zoom_level = :z LIMIT 1"));
    q.bindValue(QStringLiteral(":z"), maxZ);
    if (q.exec() && q.next()) {
        const QByteArray bytes = q.value(0).toByteArray();
        if (bytes.startsWith("\x1f\x8b"))
            return fail(error, QStringLiteral("%1 contiene teselas vectoriales (gzip/pbf): solo se "
                                              "admiten MBTiles ráster").arg(ds.filePath));
        const QImage img = QImage::fromData(bytes);
        if (!img.isNull())
            tileSize = img.width();
        else if (looksLikeRaster(bytes))
            imageProblem = QStringLiteral("Qt no puede decodificar las teselas de %1 (¿falta el plugin "
                                          "de imagen para %2?)").arg(ds.filePath, format.isEmpty()
                                                                         ? QStringLiteral("este formato") : format);
        else
            imageProblem = QStringLiteral("Las teselas de %1 no parecen imágenes").arg(ds.filePath);
    }

    // Aplicar solo lo que el usuario no fijó a mano.
    auto free = [&given](const char *key) { return !given.contains(QLatin1String(key)); };
    if (free("minZoom"))
        ds.minZoom = minZ;
    if (free("maxZoom"))
        ds.maxZoom = maxZ;
    if (free("baseZoom"))
        ds.baseZoom = ds.minZoom;
    if (free("recommendedMaxZoom"))
        ds.recommendedMaxZoom = ds.maxZoom;
    if (free("tileSize") && tileSize > 0)
        ds.tileSize = tileSize;
    if (free("scheme") && meta.value(QStringLiteral("scheme")).compare(QLatin1String("xyz"),
                                                                        Qt::CaseInsensitive) == 0)
        ds.scheme = TileScheme::XYZ;
    if (ds.displayName.isEmpty())
        ds.displayName = meta.value(QStringLiteral("name"), QFileInfo(ds.filePath).completeBaseName());

    if (!imageProblem.isEmpty())
        return fail(error, imageProblem);
    return true;
}

// Construye el dataset de un fichero: disposición, id y nombre, y lo detectado.
TileDataset fromFile(const QString &path, const QString &id, QString *error,
                     QGeoRectangle *coverage)
{
    TileDataset ds;
    ds.filePath = QFileInfo(path).absoluteFilePath();
    ds.id = id.isEmpty() ? idFromFile(path) : id;
    applyLayout(ds);
    describe(ds, QJsonObject(), error);
    if (coverage) {
        *coverage = QGeoRectangle();
        TempConnection conn(ds.filePath);
        if (conn.isOpen())
            *coverage = parseBounds(readMetadata(conn.db()).value(QStringLiteral("bounds")));
    }
    return ds;
}

} // namespace MBTiles
} // namespace libmapa
