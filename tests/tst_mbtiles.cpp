#include "io/DataPackage.h"
#include "tiles/MBTiles.h"
#include "tiles/RMapsTileSource.h"
#include "tiles/TileDatasetProbe.h"
#include "tiles/TileDataset.h"
#include "tiles/TileService.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest>

using namespace libmapa;

// Pruebas del soporte de MBTiles. Se fabrican MBTiles sintéticos (con las filas en
// TMS, como manda el estándar) cuyas teselas llevan en el color su propia x, y, z,
// y se comprueba que (1) el fichero se entiende solo, (2) cada tesela lógica
// (XYZ) sale de su fila TMS, (3) entra por datasets.json y por un paquete, y
// (4) lo que ya existía -las bases RMaps y su JSON- no cambia ni una coma.
class TstMBTiles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void describesItself();
    void readsTilesWithTheTmsFlip();
    void datasetsJsonAutoDetectsAndRespectsOverrides();
    void packageManifestWithMBTiles();
    void rejectsWhatItCannotUse();
    void rmapsJsonIsUnchanged();
    void probeOutputLoadsUnchanged();

private:
    // Color que identifica a una tesela.
    static QColor colorOf(int z, int x, int y) { return QColor(x * 16, y * 16, z * 40); }

    // Crea un MBTiles con el mundo entero de los zooms [minZ, maxZ] (PNG de 256 px).
    // \a metadata son los pares de la tabla metadata; \a createMetadata false la omite.
    static bool makeMBTiles(const QString &path, int minZ, int maxZ,
                            const QMap<QString, QString> &metadata, bool createMetadata = true);

    QTemporaryDir m_dir;
    QString p(const char *name) { return m_dir.path() + QStringLiteral("/") + QLatin1String(name); }
};

// Prepara el directorio temporal.
void TstMBTiles::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

// Fabrica el MBTiles de prueba: tabla `tiles` en TMS y, opcionalmente, `metadata`.
bool TstMBTiles::makeMBTiles(const QString &path, int minZ, int maxZ,
                             const QMap<QString, QString> &metadata, bool createMetadata)
{
    const QString conn = QStringLiteral("mk_%1").arg(path);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                   "tile_row INTEGER, tile_data BLOB)"))
            || !q.exec(QStringLiteral("CREATE UNIQUE INDEX tile_index ON tiles "
                                      "(zoom_level, tile_column, tile_row)")))
            return false;
        if (createMetadata) {
            if (!q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)")))
                return false;
            for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
                q.prepare(QStringLiteral("INSERT INTO metadata VALUES (:n, :v)"));
                q.bindValue(QStringLiteral(":n"), it.key());
                q.bindValue(QStringLiteral(":v"), it.value());
                if (!q.exec())
                    return false;
            }
        }
        db.transaction();
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = 0; x < (1 << z); ++x)
                for (int y = 0; y < (1 << z); ++y) {
                    QImage img(256, 256, QImage::Format_RGB32);
                    img.fill(colorOf(z, x, y));
                    QByteArray png;
                    QBuffer buf(&png);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
                    q.bindValue(QStringLiteral(":z"), z);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), (1 << z) - 1 - y);        // TMS
                    q.bindValue(QStringLiteral(":d"), png);
                    if (!q.exec())
                        return false;
                }
        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return true;
}

// El fichero se describe solo: zooms, lado de tesela, nombre, esquema y zona.
void TstMBTiles::describesItself()
{
    const QString f = p("mundo.mbtiles");
    QVERIFY(makeMBTiles(f, 1, 3, { { "name", "Mundo de prueba" }, { "format", "png" },
                                   { "minzoom", "1" }, { "maxzoom", "3" },
                                   { "bounds", "-80,10,-70,20" } }));
    QVERIFY(MBTiles::isMBTilesPath(f));
    QVERIFY(MBTiles::isMBTilesPath(QStringLiteral("X.MBTiles")));
    QVERIFY(!MBTiles::isMBTilesPath(QStringLiteral("x.sqlitedb")));

    QString error;
    QGeoRectangle zona;
    const TileDataset ds = MBTiles::fromFile(f, QString(), &error, &zona);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(ds.isValid());
    QVERIFY(ds.mbtiles);
    QCOMPARE(ds.id, QStringLiteral("mundo"));
    QCOMPARE(ds.displayName, QStringLiteral("Mundo de prueba"));
    QCOMPARE(ds.minZoom, 1);
    QCOMPARE(ds.maxZoom, 3);
    QCOMPARE(ds.baseZoom, 1);
    QCOMPARE(ds.recommendedMaxZoom, 3);
    QCOMPARE(ds.tileSize, 256);
    QCOMPARE(ds.scheme, TileScheme::TMS);
    QVERIFY(!ds.hasSColumn);
    QCOMPARE(ds.colZ, QStringLiteral("zoom_level"));
    QCOMPARE(ds.colImage, QStringLiteral("tile_data"));
    QVERIFY(zona.isValid());
    QCOMPARE(zona.topLeft().longitude(), -80.0);
    QCOMPARE(zona.bottomRight().latitude(), 10.0);

    // Sin la tabla metadata (permitido): los zooms salen de las propias teselas.
    const QString g = p("sin_meta.mbtiles");
    QVERIFY(makeMBTiles(g, 2, 3, {}, false));
    const TileDataset sin = MBTiles::fromFile(g, QStringLiteral("capa"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(sin.id, QStringLiteral("capa"));
    QCOMPARE(sin.minZoom, 2);
    QCOMPARE(sin.maxZoom, 3);
    QCOMPARE(sin.displayName, QStringLiteral("sin_meta"));
}

// La tesela lógica (z,x,y en XYZ) sale de su fila TMS: el color lo demuestra. Y
// available()/fetchRange() hablan el mismo idioma.
void TstMBTiles::readsTilesWithTheTmsFlip()
{
    const QString f = p("tms.mbtiles");
    QVERIFY(makeMBTiles(f, 1, 3, { { "format", "png" } }));
    QString error;
    const TileDataset ds = MBTiles::fromFile(f, QStringLiteral("tms"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    RMapsTileSource src(ds);
    QVERIFY2(src.open(), qPrintable(src.lastError()));
    for (const TileKey &k : { TileKey{3, 5, 2}, TileKey{3, 0, 7}, TileKey{2, 3, 0}, TileKey{1, 1, 1} }) {
        const QImage img = QImage::fromData(src.fetch(k));
        QVERIFY2(!img.isNull(), qPrintable(QStringLiteral("sin tesela %1/%2/%3").arg(k.z).arg(k.x).arg(k.y)));
        QCOMPARE(QColor(img.pixel(10, 10)), colorOf(k.z, k.x, k.y));
    }
    QVERIFY(src.fetch(TileKey{4, 0, 0}).isEmpty());        // un zoom que no hay

    const QSet<TileKey> hay = src.available(3, 2, 4, 5, 6);
    QCOMPARE(hay.size(), 6);                      // x 2..4 por y 5..6
    QVERIFY(hay.contains(TileKey{3, 4, 6}));
    const QHash<TileKey, QByteArray> rango = src.fetchRange(3, 2, 3, 0, 1);
    QCOMPARE(rango.size(), 4);
    for (auto it = rango.constBegin(); it != rango.constEnd(); ++it)
        QCOMPARE(QColor(QImage::fromData(it.value()).pixel(3, 3)), colorOf(3, it.key().x, it.key().y));
    QCOMPARE(src.tileCount(3), qint64(64));
}

// Una entrada de datasets.json con solo id y ruta: se completa leyendo el fichero;
// lo que el JSON fija a mano manda sobre lo detectado.
void TstMBTiles::datasetsJsonAutoDetectsAndRespectsOverrides()
{
    QVERIFY(makeMBTiles(p("a.mbtiles"), 1, 3, { { "name", "Capa A" }, { "format", "png" } }));
    const QByteArray json = R"({
        "version": 1,
        "datasets": [
            { "id": "a", "filePath": "a.mbtiles" },
            { "id": "b", "filePath": "a.mbtiles", "displayName": "Mio", "maxZoom": 2,
              "recommendedMaxZoom": 2, "tileSize": 512 },
            { "id": "c", "filePath": "a.mbtiles", "format": "mbtiles", "scheme": "XYZ" }
        ] })";
    QFile out(p("datasets.json"));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(json);
    out.close();

    QString error;
    const QVector<TileDataset> ds = TileService::loadDatasets(p("datasets.json"), &error);
    QVERIFY2(!ds.isEmpty(), qPrintable(error));
    QCOMPARE(ds.size(), 3);

    QVERIFY(ds[0].mbtiles);
    QCOMPARE(ds[0].displayName, QStringLiteral("Capa A"));
    QCOMPARE(ds[0].minZoom, 1);
    QCOMPARE(ds[0].maxZoom, 3);
    QCOMPARE(ds[0].tileSize, 256);
    QCOMPARE(ds[0].scheme, TileScheme::TMS);
    QVERIFY(ds[0].filePath.endsWith(QStringLiteral("/a.mbtiles")) && !QFileInfo(ds[0].filePath).isRelative());

    QCOMPARE(ds[1].displayName, QStringLiteral("Mio"));        // lo escrito manda
    QCOMPARE(ds[1].maxZoom, 2);
    QCOMPARE(ds[1].recommendedMaxZoom, 2);
    QCOMPARE(ds[1].tileSize, 512);
    QCOMPARE(ds[1].minZoom, 1);                                 // lo no escrito, detectado

    QCOMPARE(ds[2].scheme, TileScheme::XYZ);                    // el esquema se puede forzar

    // Ida y vuelta: toJson recuerda que es MBTiles.
    const TileDataset back = TileDataset::fromJson(ds[0].toJson());
    QVERIFY(back.mbtiles);
    QCOMPARE(back.colZ, QStringLiteral("zoom_level"));
    QCOMPARE(back.scheme, TileScheme::TMS);
}

// Un paquete de datos (mapa.json) con un MBTiles: se completa igual, y las capas
// del paquete se listan para copiarlas.
void TstMBTiles::packageManifestWithMBTiles()
{
    const QString dir = p("paquete");
    QDir().mkpath(dir);
    QVERIFY(makeMBTiles(dir + QStringLiteral("/base.mbtiles"), 2, 3, { { "name", "Base" } }));
    QFile m(dir + QStringLiteral("/mapa.json"));
    QVERIFY(m.open(QIODevice::WriteOnly));
    m.write(R"({ "format": "libmapa-package", "version": 2,
                 "package": { "id": "demo", "name": "Demo" },
                 "datasets": [ { "id": "base", "filePath": "base.mbtiles" } ] })");
    m.close();

    QString error;
    const auto pkg = DataPackage::load(dir, &error);
    QVERIFY2(pkg, qPrintable(error));
    QVERIFY2(pkg->warnings.isEmpty(), qPrintable(pkg->warnings.join(QLatin1Char('\n'))));
    QCOMPARE(pkg->datasets.size(), 1);
    QCOMPARE(pkg->datasets[0].minZoom, 2);
    QCOMPARE(pkg->datasets[0].maxZoom, 3);
    QVERIFY(pkg->datasets[0].mbtiles);
    QVERIFY(pkg->files().contains(QFileInfo(dir + QStringLiteral("/base.mbtiles")).absoluteFilePath()));
}

// Lo que no es utilizable se rechaza con un motivo claro: vectorial, sin las
// columnas, ni siquiera SQLite, o inexistente.
void TstMBTiles::rejectsWhatItCannotUse()
{
    QString error;

    const QString vec = p("vector.mbtiles");
    QVERIFY(makeMBTiles(vec, 1, 1, { { "format", "pbf" } }));
    MBTiles::fromFile(vec, QString(), &error);
    QVERIFY2(error.contains(QStringLiteral("vectoriales")), qPrintable(error));

    // Metadata dice png pero las teselas son gzip: vectorial igualmente.
    const QString gz = p("gzip.mbtiles");
    QVERIFY(makeMBTiles(gz, 1, 1, { { "format", "png" } }));
    {
        const QString c = QStringLiteral("gz");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), c);
            db.setDatabaseName(gz);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("UPDATE tiles SET tile_data = x'1f8b0800000000000003'")));
            db.close();
        }
        QSqlDatabase::removeDatabase(c);
    }
    error.clear();
    MBTiles::fromFile(gz, QString(), &error);
    QVERIFY2(error.contains(QStringLiteral("vectoriales")), qPrintable(error));

    // Una base RMaps con extension .mbtiles: no tiene las columnas.
    const QString rm = p("rmaps.mbtiles");
    {
        const QString c = QStringLiteral("rm");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), c);
            db.setDatabaseName(rm);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE tiles (z INTEGER, x INTEGER, y INTEGER, "
                                          "s INTEGER, image BLOB)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(c);
    }
    error.clear();
    MBTiles::fromFile(rm, QString(), &error);
    QVERIFY2(error.contains(QStringLiteral("zoom_level")), qPrintable(error));

    // No es SQLite / no existe.
    const QString txt = p("texto.mbtiles");
    QFile f(txt);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("esto no es sqlite, ni de lejos, ni con buena voluntad");
    f.close();
    error.clear();
    MBTiles::fromFile(txt, QString(), &error);
    QVERIFY(!error.isEmpty());
    error.clear();
    MBTiles::fromFile(p("no_existe.mbtiles"), QString(), &error);
    QVERIFY2(error.contains(QStringLiteral("No existe")), qPrintable(error));

    // Con error, el dataset conserva la disposición MBTiles (para que la capa no
    // se quede a medias y PackageCheck pueda dar el detalle).
    QVERIFY(MBTiles::fromFile(vec).mbtiles);
}

// Compatibilidad: una entrada RMaps de siempre da exactamente lo mismo que antes
// (columnas z/x/y/s/image, XYZ, con 's') y su toJson no escribe "format".
void TstMBTiles::rmapsJsonIsUnchanged()
{
    const QJsonObject o = QJsonDocument::fromJson(R"({
        "id": "sat", "displayName": "Satelital", "filePath": "Sat.sqlitedb",
        "zFactor": -1, "zOffset": 17, "minZoom": 1, "maxZoom": 18, "sValue": 0,
        "scheme": "TMS" })").object();
    const TileDataset d = TileDataset::fromJson(o);
    QVERIFY(!d.mbtiles);
    QCOMPARE(d.colZ, QStringLiteral("z"));
    QCOMPARE(d.colX, QStringLiteral("x"));
    QCOMPARE(d.colY, QStringLiteral("y"));
    QCOMPARE(d.colS, QStringLiteral("s"));
    QCOMPARE(d.colImage, QStringLiteral("image"));
    QVERIFY(d.hasSColumn);
    QCOMPARE(d.zFactor, -1);
    QCOMPARE(d.zOffset, 17);
    QCOMPARE(d.scheme, TileScheme::TMS);
    QVERIFY(!d.toJson().contains(QStringLiteral("format")));

    // Sin "scheme" sigue siendo XYZ, y los valores por defecto no cambian.
    const TileDataset v = TileDataset::fromJson(
        QJsonDocument::fromJson(R"({"id":"x","filePath":"x.sqlitedb"})").object());
    QCOMPARE(v.scheme, TileScheme::XYZ);
    QCOMPARE(v.zFactor, 1);
    QCOMPARE(v.zOffset, 0);
    QCOMPARE(v.maxZoom, 17);
    QCOMPARE(v.tileSize, 256);
    QVERIFY(v.hasSColumn);
    QCOMPARE(TileDataset::fromJson(v.toJson()).toJson(), v.toJson());
}

// Lo que ya se hacia -sondar el MBTiles con probe_db y pegar su bloque en
// datasets.json- se carga EXACTAMENTE igual que antes: el cargador nuevo, que
// completa lo que falte, no cambia nada de lo que el JSON ya trae.
void TstMBTiles::probeOutputLoadsUnchanged()
{
    const QString f = p("sondado.mbtiles");
    QVERIFY(makeMBTiles(f, 2, 4, { { "name", "Sondado" }, { "format", "png" } }));
    const auto sondado = TileDatasetProbe::probe(f, QStringLiteral("sondado"));
    QVERIFY(sondado);
    const TileDataset esperado = sondado->dataset;

    QFile out(p("sondado.json"));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(QJsonDocument(QJsonObject{ { QStringLiteral("version"), 1 },
                                         { QStringLiteral("datasets"),
                                           QJsonArray{ esperado.toJson() } } }).toJson());
    out.close();

    QString error;
    const QVector<TileDataset> cargados = TileService::loadDatasets(p("sondado.json"), &error);
    QVERIFY2(cargados.size() == 1, qPrintable(error));
    const TileDataset &c = cargados.first();
    QCOMPARE(c.id, esperado.id);
    QCOMPARE(c.displayName, esperado.displayName);
    QCOMPARE(c.tableName, esperado.tableName);
    QCOMPARE(c.zFactor, esperado.zFactor);
    QCOMPARE(c.zOffset, esperado.zOffset);
    QCOMPARE(c.minZoom, esperado.minZoom);
    QCOMPARE(c.maxZoom, esperado.maxZoom);
    QCOMPARE(c.baseZoom, esperado.baseZoom);
    QCOMPARE(c.recommendedMaxZoom, esperado.recommendedMaxZoom);
    QCOMPARE(c.typicalFill, esperado.typicalFill);
    QCOMPARE(c.scheme, esperado.scheme);
    QCOMPARE(c.tileSize, esperado.tileSize);
    QCOMPARE(c.colZ, esperado.colZ);
    QCOMPARE(c.colX, esperado.colX);
    QCOMPARE(c.colY, esperado.colY);
    QCOMPARE(c.colImage, esperado.colImage);
    QCOMPARE(c.hasSColumn, esperado.hasSColumn);
}

QTEST_APPLESS_MAIN(TstMBTiles)
#include "tst_mbtiles.moc"
