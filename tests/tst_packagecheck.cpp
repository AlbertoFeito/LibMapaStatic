#include "SyntheticTileDb.h"

#include "db/SqliteConnectionPool.h"
#include "db/VectorRepository.h"
#include "io/PackageCheck.h"
#include "libmapa/MapFeature.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest>

using namespace libmapa;
using namespace libmapa::test;

// Pruebas del comprobador de paquetes. Una BD sintetica (PNG, z6..z8 sobre el
// recuadro de Cuba) hace de capa base; cada caso monta un mapa.json distinto y
// comprueba que el problema sale con la gravedad que le toca: un fallo que deja
// una capa sin dibujar es ERROR; algo que no rompe el mapa, AVISO.
class TstPackageCheck : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void goodPackageHasNoProblems();     // paquete correcto: 0 errores, 0 avisos
    void measuresCoverageInTheZone();    // cobertura por nivel dentro de "bounds"
    void quickModeSkipsCoverage();       // --quick: sin contar niveles
    void missingFilesAreErrors();        // capa, elevacion o .geo ausentes
    void undecodableImagesAreErrors();   // BLOB que no es imagen -> el mapa en blanco
    void badElevationDbIsAnError();      // una BD que no es de elevacion
    void notPortableAndMetadataWarn();   // fuera de la carpeta, sin atribucion, start
    void emptyLevelInZoneWarns();        // un nivel declarado sin teselas en la zona
    void unreadableManifestIsAnError();  // sin mapa.json: un unico error claro
    void vectorDbOverlayValidates();     // overlay .sqlitedb (entidades) -> ok, no .geo

private:
    // Escribe mapa.json en una carpeta nueva 'name' y devuelve la carpeta.
    QString writePackage(const QString &name, const QByteArray &json);
    // Copia la BD sintetica dentro de la carpeta del paquete con ese nombre.
    void copyDb(const QString &dir, const QString &fileName);
    // .geo valido de dos trazados.
    static void writeGeo(const QString &path);

    QTemporaryDir m_tmp;
    QString m_db;          // BD sintetica de referencia
};

// Construye una vez la BD sintetica que copiaran los paquetes de cada caso.
void TstPackageCheck::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    SyntheticSpec spec;
    spec.path = m_tmp.filePath(QStringLiteral("base.sqlitedb"));
    spec.minLogicalZ = 6;
    spec.maxLogicalZ = 8;
    QVERIFY(buildSyntheticDb(spec) > 0);
    m_db = spec.path;
    SqliteConnectionPool::closeAllForCurrentThread();
}

// Tras cada caso se cierran las conexiones del pool: varios paquetes usan el
// mismo id de dataset ("base") con ficheros distintos.
void TstPackageCheck::cleanup()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

QString TstPackageCheck::writePackage(const QString &name, const QByteArray &json)
{
    const QString dir = m_tmp.filePath(name);
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/mapa.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(json);
    return dir;
}

void TstPackageCheck::copyDb(const QString &dir, const QString &fileName)
{
    QFile::copy(m_db, dir + QLatin1Char('/') + fileName);
}

void TstPackageCheck::writeGeo(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("-81.4,23.0,\n-81.5,21.6,\n0.0,0.0\n-78.8,22.3,\n-78.9,21.4,\n0.0,0.0\n");
}

// Manifiesto de partida con todo bien: se reutiliza en varios casos anadiendo
// solo lo que cada uno quiere romper.
static QByteArray goodManifest(const QByteArray &extra = QByteArray())
{
    return QByteArray(R"({
        "format": "libmapa-package", "version": 2,
        "package": { "id": "prueba", "name": "Prueba", "attribution": "Sintetico",
                     "bounds": { "north": 23.3, "west": -85.0, "south": 19.7, "east": -74.0 } },
        "start": { "layer": "base" },
        "datasets": [ { "id": "base", "filePath": "base.sqlitedb",
                        "minZoom": 6, "maxZoom": 8, "baseZoom": 6 } ],
        "overlays": [ { "id": "rutas", "file": "rutas.geo" } ])")
           + extra + QByteArray(" }");
}

// Todo en su sitio: ningun error ni aviso, la capa abre y su formato es PNG.
void TstPackageCheck::goodPackageHasNoProblems()
{
    const QString dir = writePackage(QStringLiteral("bueno"), goodManifest());
    copyDb(dir, QStringLiteral("base.sqlitedb"));
    writeGeo(dir + QStringLiteral("/rutas.geo"));

    const PackageCheck r = PackageCheck::run(dir, PackageCheck::Options());
    QVERIFY2(r.problems().isEmpty(), qPrintable(r.problems().join(QLatin1Char('\n'))));
    QCOMPARE(r.datasets.size(), 1);
    QVERIFY(r.datasets.first().opened);
    QCOMPARE(r.datasets.first().imageFormat, QStringLiteral("png"));
    QVERIFY(r.totalBytes > 0);
}

// La BD sintetica rellena EXACTAMENTE la zona del paquete: cada nivel debe salir
// completo (presentes == esperadas) y solo hasta el tope de zoom pedido.
void TstPackageCheck::measuresCoverageInTheZone()
{
    const QString dir = writePackage(QStringLiteral("cobertura"), goodManifest());
    copyDb(dir, QStringLiteral("base.sqlitedb"));
    writeGeo(dir + QStringLiteral("/rutas.geo"));

    PackageCheck::Options o;
    o.maxCoverageZoom = 7;
    const PackageCheck r = PackageCheck::run(dir, o);
    const auto &niveles = r.datasets.first().levels;
    QCOMPARE(niveles.size(), 2);                    // z6 y z7 (tope 7)
    for (const PackageCheck::LevelCoverage &lc : niveles) {
        QVERIFY(lc.expected > 0);
        QCOMPARE(lc.present, lc.expected);
        QCOMPARE(lc.fraction(), 1.0);
    }
}

// En modo rapido (el que usa MapWidget al arrancar) no se cuenta nada por nivel.
void TstPackageCheck::quickModeSkipsCoverage()
{
    const QString dir = writePackage(QStringLiteral("rapido"), goodManifest());
    copyDb(dir, QStringLiteral("base.sqlitedb"));
    writeGeo(dir + QStringLiteral("/rutas.geo"));

    PackageCheck::Options o;
    o.coverage = false;
    const PackageCheck r = PackageCheck::run(dir, o);
    QVERIFY(r.datasets.first().opened);
    QVERIFY(r.datasets.first().levels.isEmpty());
    QVERIFY(!r.hasErrors());
}

// Lo que falta deja una parte del mapa sin dibujar: es ERROR, no aviso (al
// contrario que en DataPackage::load, que solo avisa para no impedir abrir).
void TstPackageCheck::missingFilesAreErrors()
{
    const QString dir = writePackage(QStringLiteral("faltan"),
        goodManifest(R"(, "elevation": { "file": "dem.sqlitedb" })"));
    // Ni la BD, ni el .geo, ni la elevacion.

    const PackageCheck r = PackageCheck::run(dir, PackageCheck::Options());
    QCOMPARE(r.count(PackageCheck::Severity::Error), 3);
    const QString todo = r.problems().join(QLatin1Char('\n'));
    QVERIFY(todo.contains(QStringLiteral("base.sqlitedb")));
    QVERIFY(todo.contains(QStringLiteral("rutas.geo")));
    QVERIFY(todo.contains(QStringLiteral("dem.sqlitedb")));
    QVERIFY(!r.datasets.first().opened);
}

// El caso tipico al desplegar: la BD abre, pero sus imagenes no se pueden
// decodificar. Se simula con un BLOB que empieza como un JPEG y no lo es.
void TstPackageCheck::undecodableImagesAreErrors()
{
    const QString dir = writePackage(QStringLiteral("corrupta"), goodManifest());
    copyDb(dir, QStringLiteral("base.sqlitedb"));
    writeGeo(dir + QStringLiteral("/rutas.geo"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("romper"));
        db.setDatabaseName(dir + QStringLiteral("/base.sqlitedb"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("UPDATE tiles SET image = X'FFD8FFE0DEADBEEF'")));
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("romper"));

    PackageCheck::Options o;
    o.coverage = false;
    const PackageCheck r = PackageCheck::run(dir, o);
    QVERIFY(r.hasErrors());
    QCOMPARE(r.datasets.first().imageFormat, QStringLiteral("jpeg"));
    QVERIFY(r.problems().join(QLatin1Char('\n')).contains(QStringLiteral("decodificar")));
}

// Apuntar "elevation" a algo que no es una BD de dem_to_db es un error claro.
void TstPackageCheck::badElevationDbIsAnError()
{
    const QString dir = writePackage(QStringLiteral("dem_malo"),
        goodManifest(R"(, "elevation": { "file": "base.sqlitedb" })"));
    copyDb(dir, QStringLiteral("base.sqlitedb"));
    writeGeo(dir + QStringLiteral("/rutas.geo"));

    PackageCheck::Options o;
    o.coverage = false;
    const PackageCheck r = PackageCheck::run(dir, o);
    QCOMPARE(r.count(PackageCheck::Severity::Error), 1);
    QVERIFY(r.problems().first().startsWith(QStringLiteral("elevation")));
    QVERIFY(r.problems().first().contains(QStringLiteral("dem_tiles")));
}

// Lo que no rompe el mapa pero hay que saber antes de distribuir: un fichero
// fuera de la carpeta (no viajara), sin atribucion, y una capa de arranque que
// no existe.
void TstPackageCheck::notPortableAndMetadataWarn()
{
    const QByteArray fuera = QDir::fromNativeSeparators(m_db).toUtf8();
    const QString dir = writePackage(QStringLiteral("no_portable"), QByteArray(R"({
        "format": "libmapa-package", "version": 2,
        "package": { "id": "np", "bounds": { "north": 23.3, "west": -85.0,
                                             "south": 19.7, "east": -74.0 } },
        "start": { "layer": "no_existe" },
        "datasets": [ { "id": "base", "filePath": ")") + fuera + R"(",
                        "minZoom": 6, "maxZoom": 8, "baseZoom": 6 } ]
    })");

    PackageCheck::Options o;
    o.coverage = false;
    const PackageCheck r = PackageCheck::run(dir, o);
    QVERIFY2(!r.hasErrors(), qPrintable(r.problems().join(QLatin1Char('\n'))));
    QCOMPARE(r.count(PackageCheck::Severity::Warning), 3);
    const QString todo = r.problems().join(QLatin1Char('\n'));
    QVERIFY(todo.contains(QStringLiteral("FUERA")));
    QVERIFY(todo.contains(QStringLiteral("attribution")));
    QVERIFY(todo.contains(QStringLiteral("no_existe")));
}

// Un nivel declarado (z5) del que la BD no tiene ninguna tesela en la zona: el
// mapa funciona, pero a ese zoom se vera el nivel superior escalado o nada.
void TstPackageCheck::emptyLevelInZoneWarns()
{
    const QString dir = writePackage(QStringLiteral("nivel_vacio"), QByteArray(R"({
        "format": "libmapa-package", "version": 2,
        "package": { "id": "nv", "attribution": "Sintetico",
                     "bounds": { "north": 23.3, "west": -85.0, "south": 19.7, "east": -74.0 } },
        "datasets": [ { "id": "base", "filePath": "base.sqlitedb",
                        "minZoom": 5, "maxZoom": 8, "baseZoom": 6 } ]
    })"));
    copyDb(dir, QStringLiteral("base.sqlitedb"));

    const PackageCheck r = PackageCheck::run(dir, PackageCheck::Options());
    QVERIFY2(!r.hasErrors(), qPrintable(r.problems().join(QLatin1Char('\n'))));
    QCOMPARE(r.count(PackageCheck::Severity::Warning), 1);
    QVERIFY(r.problems().first().contains(QStringLiteral("z=5")));
    QCOMPARE(r.datasets.first().levels.first().present, qint64(0));
}

// Sin manifiesto no hay nada que revisar: un unico error que dice cual falta.
void TstPackageCheck::unreadableManifestIsAnError()
{
    const PackageCheck r = PackageCheck::run(m_tmp.filePath(QStringLiteral("nada")),
                                             PackageCheck::Options());
    QCOMPARE(r.count(PackageCheck::Severity::Error), 1);
    QVERIFY(r.problems().first().contains(QStringLiteral("mapa.json")));
    QVERIFY(r.datasets.isEmpty());
}

// Un overlay con fichero .sqlitedb (BD de entidades, p. ej. curvas de nivel) se
// valida como BD vectorial -contando entidades-, NO leyendolo como .geo (que
// daria un error falso "no contiene trazados validos" y un diluvio de avisos).
void TstPackageCheck::vectorDbOverlayValidates()
{
    const QByteArray manifest = R"({
        "format": "libmapa-package", "version": 2,
        "package": { "id": "prueba", "name": "Prueba", "attribution": "Sintetico",
                     "bounds": { "north": 23.3, "west": -85.0, "south": 19.7, "east": -74.0 } },
        "start": { "layer": "base" },
        "datasets": [ { "id": "base", "filePath": "base.sqlitedb",
                        "minZoom": 6, "maxZoom": 8, "baseZoom": 6 } ],
        "overlays": [ { "id": "curvas", "name": "Curvas", "file": "curvas.sqlitedb", "zOrder": 20 } ] })";

    const QString dir = writePackage(QStringLiteral("vector_overlay"), manifest);
    copyDb(dir, QStringLiteral("base.sqlitedb"));
    {
        using namespace libmapa;
        VectorRepository repo;
        QVERIFY(repo.open(dir + QStringLiteral("/curvas.sqlitedb")));
        MapFeature a;
        a.layerId = QStringLiteral("curvas");
        a.kind = GeometryKind::Polyline;
        a.type = QStringLiteral("curva_nivel");
        a.geometry = {QGeoCoordinate(21.0, -80.0), QGeoCoordinate(21.1, -80.1)};
        a.attributes[QStringLiteral("cota")] = 500;
        QVERIFY(repo.saveFeatures({a}));
    }

    const PackageCheck r = PackageCheck::run(dir, PackageCheck::Options());
    // Sin errores ni avisos: la BD vectorial se valida bien (no como .geo).
    QVERIFY2(r.problems().isEmpty(), qPrintable(r.problems().join(QLatin1Char('\n'))));
}

QTEST_MAIN(TstPackageCheck)
#include "tst_packagecheck.moc"
