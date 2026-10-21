#include "tiles/MBTiles.h"

#include "vector/Gzip.h"
#include "vector/MvtTile.h"

#include <QFile>
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

// Entero big-endian de 16 o 32 bits de la cabecera SQLite.
quint32 beUInt(const QByteArray &b, int at, int bytes)
{
    quint32 v = 0;
    for (int i = 0; i < bytes; ++i)
        v = (v << 8) | quint8(b[at + i]);
    return v;
}

// Comprueba, sin abrir la base, que el fichero sea un SQLite ENTERO: la firma de
// los 16 primeros bytes y que mida lo que dice su cabecera (tamaño de página ×
// número de páginas). Un MBTiles grande suele bajarse en partes y unirse a mano;
// si falta una parte, se cortó la descarga o el servidor devolvió una página web,
// SQLite lo abre igual y solo «no encuentra la tabla», un mensaje que despista.
// Vacío si está bien (o si la cabecera no permite saberlo).
QString sqliteFileProblem(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();                     // lo dirá la apertura con su motivo
    const qint64 size = f.size();
    const QByteArray head = f.read(100);
    if (head.size() < 100 || !head.startsWith(QByteArray("SQLite format 3\0", 16))) {
        const QString start = QString::fromLatin1(head.left(15)).simplified();
        return QStringLiteral("%1 no es una base de datos SQLite (%2 bytes; empieza por «%3»)%4. "
                              "¿Descarga fallida? Vuelve a descargarlo y comprueba su MD5.")
            .arg(path).arg(size).arg(start,
                                     head.trimmed().startsWith('<')
                                         ? QStringLiteral(": parece una página web, no el fichero")
                                         : QString());
    }
    quint64 pageSize = beUInt(head, 16, 2);
    if (pageSize == 1)
        pageSize = 65536;
    const quint32 pages = beUInt(head, 28, 4);
    // El número de páginas solo es fiable si «version-valid-for» coincide con el
    // contador de cambios (ficheros escritos por SQLite 3.7.0 o posterior).
    if (pages == 0 || beUInt(head, 92, 4) != beUInt(head, 24, 4))
        return QString();
    const quint64 expected = pageSize * pages;
    if (quint64(size) < expected)
        return QStringLiteral("%1 está INCOMPLETO: mide %2 bytes y debería medir %3 (faltan %4 MB). "
                              "¿Falta alguna parte al unirlo o se cortó la descarga? Vuelve a "
                              "descargarlo y comprueba su MD5.")
            .arg(path).arg(size).arg(expected)
            .arg(double(expected - quint64(size)) / (1024.0 * 1024.0), 0, 'f', 1);
    return QString();
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
    const QString broken = sqliteFileProblem(ds.filePath);
    if (!broken.isEmpty())
        return fail(error, broken);

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
    const bool declaredVector = format == QLatin1String("pbf") || format == QLatin1String("mvt");

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

    // Una tesela real: dice si es ráster o vectorial y, si es imagen, su lado en
    // píxeles. Una vectorial tiene que decodificarse como MVT de verdad: el
    // `format` de metadata solo no basta (hay ficheros que mienten en los dos
    // sentidos), y así un MBTiles roto se rechaza aquí con su motivo en vez de
    // dejar el mapa en blanco.
    int tileSize = 0;
    bool vector = false;
    QString imageProblem;
    q.prepare(QStringLiteral("SELECT tile_data FROM tiles WHERE zoom_level = :z LIMIT 1"));
    q.bindValue(QStringLiteral(":z"), maxZ);
    if (q.exec() && q.next()) {
        const QByteArray bytes = q.value(0).toByteArray();
        if (declaredVector || Gzip::isGzip(bytes)) {
            mvt::Tile tile;
            QString motivo;
            const bool decoded = !looksLikeRaster(bytes) && mvt::decode(bytes, &tile, &motivo);
            if (!decoded || (tile.layers.isEmpty() && !declaredVector))
                return fail(error, QStringLiteral("%1 dice contener teselas vectoriales (%2) pero no se "
                                                  "pueden decodificar como MVT: %3")
                                       .arg(ds.filePath,
                                            format.isEmpty() ? QStringLiteral("gzip") : format,
                                            motivo.isEmpty() ? QStringLiteral("son imágenes") : motivo));
            vector = true;
        } else if (const QImage img = QImage::fromData(bytes); !img.isNull())
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
    // Las vectoriales no tienen lado propio: se pintan al de la rejilla (256 px
    // por defecto), así la escala coincide con la de cualquier otra capa base.
    if (free("vector"))
        ds.vector = vector;
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
