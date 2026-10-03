#include "io/DataPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;

// Pruebas del lector del manifiesto de paquete (mapa.json). Todo se monta en
// un temporal: el manifiesto y unos ficheros vacios que hacen de bases de datos
// (el lector solo comprueba que EXISTEN; abrirlas es cosa del TileService).
class TstDataPackage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void fullManifest();            // todos los bloques, rutas relativas resueltas
    void folderOrFileAndDefaults(); // carpeta o fichero; valores por defecto
    void version1StillLoads();      // un datasets.json antiguo es un paquete minimo
    void rejectsForeignOrNewer();   // otro "format" o version futura -> error claro
    void missingFilesAreWarnings(); // lo que falte avisa, no impide abrir
    void featuresGoToUserData();    // entidades en AppData/<id>/ y copia de la semilla

private:
    // Escribe 'json' como mapa.json dentro de 'dir' (crea la carpeta).
    static QString writeManifest(const QString &dir, const QByteArray &json);
    // Crea un fichero (vacio salvo 'content') para que exista.
    static void touch(const QString &path, const QByteArray &content = QByteArray());

    QTemporaryDir m_tmp;
};

// Temporal valido y QStandardPaths en modo test: sin esto, AppDataLocation
// apuntaria a la carpeta real del usuario.
void TstDataPackage::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    QStandardPaths::setTestModeEnabled(true);
}

// Escribe 'json' como mapa.json dentro de 'dir' y devuelve su ruta.
QString TstDataPackage::writeManifest(const QString &dir, const QByteArray &json)
{
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("mapa.json"));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(json);
    return path;
}

// Crea 'path' (y su carpeta) con 'content', para que el fichero exista.
void TstDataPackage::touch(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(content);
}

// Manifiesto completo: comprueba cada bloque y que TODAS las rutas relativas
// salen resueltas contra la carpeta del paquete.
void TstDataPackage::fullManifest()
{
    const QString dir = m_tmp.filePath(QStringLiteral("completo"));
    touch(dir + QStringLiteral("/teselas/osm.sqlitedb"));
    touch(dir + QStringLiteral("/teselas/sat.sqlitedb"));
    touch(dir + QStringLiteral("/dem.sqlitedb"));
    touch(dir + QStringLiteral("/aguas.geo"));

    writeManifest(dir, R"({
        "format": "libmapa-package", "version": 2,
        "package": { "id": "cuba", "name": "Cuba", "dataVersion": "2026.10",
                     "created": "2026-10-03", "attribution": "OSM",
                     "bounds": { "north": 23.3, "west": -85.0, "south": 19.7, "east": -74.0 } },
        "start": { "layer": "sat", "center": [23.1, -82.4], "zoom": 8 },
        "datasets": [
            { "id": "osm", "displayName": "Open Street Map",
              "filePath": "teselas/osm.sqlitedb", "zOffset": 1, "minZoom": 3, "maxZoom": 16 },
            { "id": "sat", "filePath": "teselas/sat.sqlitedb",
              "zFactor": -1, "zOffset": 17, "maxZoom": 18 }
        ],
        "elevation": { "file": "dem.sqlitedb" },
        "overlays": [ { "id": "aguas", "name": "Aguas", "file": "aguas.geo", "zOrder": 5,
                        "style": { "lineColor": "#1565c0", "fillColor": "#331565c0",
                                   "lineWidth": 3 } } ],
        "features": { "file": "entidades.db" }
    })");

    QString error;
    const auto p = DataPackage::load(dir, &error);
    QVERIFY2(p, qPrintable(error));
    QVERIFY2(p->warnings.isEmpty(), qPrintable(p->warnings.join(QLatin1Char('\n'))));

    const QDir base(dir);
    QCOMPARE(p->info.id, QStringLiteral("cuba"));
    QCOMPARE(p->info.name, QStringLiteral("Cuba"));
    QCOMPARE(p->info.dataVersion, QStringLiteral("2026.10"));
    QCOMPARE(p->info.attribution, QStringLiteral("OSM"));
    QVERIFY(p->info.bounds.isValid());
    QCOMPARE(p->info.bounds.topLeft().latitude(), 23.3);
    QCOMPARE(p->info.bounds.bottomRight().longitude(), -74.0);
    QCOMPARE(QFileInfo(p->info.manifestPath),
             QFileInfo(base.filePath(QStringLiteral("mapa.json"))));

    QCOMPARE(p->startLayer, QStringLiteral("sat"));
    QCOMPARE(p->startZoom, 8);
    QCOMPARE(p->startCenter.latitude(), 23.1);

    QCOMPARE(p->datasets.size(), 2);
    QCOMPARE(QFileInfo(p->datasets[0].filePath),
             QFileInfo(base.filePath(QStringLiteral("teselas/osm.sqlitedb"))));
    QVERIFY(QFileInfo(p->datasets[0].filePath).isAbsolute());
    QCOMPARE(p->datasets[0].displayName, QStringLiteral("Open Street Map"));
    QCOMPARE(p->datasets[0].zOffset, 1);
    QCOMPARE(p->datasets[0].tableName, QStringLiteral("tiles"));   // por defecto
    QCOMPARE(p->datasets[1].zFactor, -1);

    QCOMPARE(QFileInfo(p->elevationFile),
             QFileInfo(base.filePath(QStringLiteral("dem.sqlitedb"))));
    QVERIFY(p->elevationDir.isEmpty());

    QCOMPARE(p->overlays.size(), 1);
    const DataPackage::Overlay &ov = p->overlays.first();
    QCOMPARE(ov.id, QStringLiteral("aguas"));
    QCOMPARE(ov.zOrder, 5);
    QCOMPARE(ov.style.lineColor, QColor(0x15, 0x65, 0xc0));
    QCOMPARE(ov.style.fillColor.alpha(), 0x33);
    QCOMPARE(ov.style.lineWidth, 3.0);
    QCOMPARE(QFileInfo(ov.file), QFileInfo(base.filePath(QStringLiteral("aguas.geo"))));

    QCOMPARE(p->featuresFile, QStringLiteral("entidades.db"));
}

// Se puede pasar la carpeta o el fichero; lo que no se declara queda "sin
// declarar" (zoom -1, centro invalido) para que el widget aplique los suyos, y el
// id del paquete, si falta, sale del nombre de la carpeta.
void TstDataPackage::folderOrFileAndDefaults()
{
    const QString dir = m_tmp.filePath(QStringLiteral("Minimo"));
    touch(dir + QStringLiteral("/a.sqlitedb"));
    const QString json = writeManifest(dir, R"({
        "format": "libmapa-package", "version": 2,
        "datasets": [ { "id": "a", "filePath": "a.sqlitedb" } ]
    })");

    const auto porCarpeta = DataPackage::load(dir);
    const auto porFichero = DataPackage::load(json);
    QVERIFY(porCarpeta);
    QVERIFY(porFichero);
    QCOMPARE(porCarpeta->datasets.first().filePath, porFichero->datasets.first().filePath);

    QCOMPARE(porCarpeta->info.id, QStringLiteral("minimo"));
    QCOMPARE(porCarpeta->startZoom, -1);
    QVERIFY(!porCarpeta->startCenter.isValid());
    QVERIFY(!porCarpeta->info.bounds.isValid());
    QVERIFY(porCarpeta->overlays.isEmpty());
    QVERIFY(porCarpeta->resolveFeaturesPath().isEmpty());   // sin entidades declaradas
}

// Un datasets.json de la version 1 (sin "format") se abre como paquete minimo:
// asi el cambio no rompe nada de lo que ya estaba configurado.
void TstDataPackage::version1StillLoads()
{
    const QString dir = m_tmp.filePath(QStringLiteral("v1"));
    touch(dir + QStringLiteral("/osm.sqlitedb"));
    const QString json = QDir(dir).filePath(QStringLiteral("datasets.json"));
    touch(json, R"({ "version": 1,
        "datasets": [ { "id": "osm", "filePath": "osm.sqlitedb" } ] })");

    QString error;
    const auto p = DataPackage::load(json, &error);
    QVERIFY2(p, qPrintable(error));
    QCOMPARE(p->datasets.size(), 1);
    QVERIFY(QFileInfo(p->datasets.first().filePath).isAbsolute());
}

// Otro formato, una version que esta libreria no conoce, JSON roto o sin
// datasets: no se abre, y el mensaje dice por que.
void TstDataPackage::rejectsForeignOrNewer()
{
    QString error;

    const QString otro = m_tmp.filePath(QStringLiteral("otro"));
    writeManifest(otro, R"({ "format": "otra-cosa",
                             "datasets": [ { "id": "a", "filePath": "a" } ] })");
    QVERIFY(!DataPackage::load(otro, &error));
    QVERIFY(error.contains(QStringLiteral("otra-cosa")));

    const QString futuro = m_tmp.filePath(QStringLiteral("futuro"));
    writeManifest(futuro, R"({ "format": "libmapa-package", "version": 99,
                               "datasets": [ { "id": "a", "filePath": "a" } ] })");
    QVERIFY(!DataPackage::load(futuro, &error));
    QVERIFY(error.contains(QStringLiteral("99")));

    const QString roto = m_tmp.filePath(QStringLiteral("roto"));
    writeManifest(roto, "{ esto no es json");
    QVERIFY(!DataPackage::load(roto, &error));

    const QString vacio = m_tmp.filePath(QStringLiteral("vacio"));
    writeManifest(vacio, R"({ "format": "libmapa-package", "version": 2, "datasets": [] })");
    QVERIFY(!DataPackage::load(vacio, &error));

    QVERIFY(!DataPackage::load(m_tmp.filePath(QStringLiteral("no_existe")), &error));
    QVERIFY(error.contains(QStringLiteral("mapa.json")));
}

// Un fichero que falta (una capa base, la elevacion, una capa fija) se avisa
// pero no impide abrir el paquete: el resto del mapa debe seguir funcionando.
void TstDataPackage::missingFilesAreWarnings()
{
    const QString dir = m_tmp.filePath(QStringLiteral("incompleto"));
    touch(dir + QStringLiteral("/osm.sqlitedb"));
    writeManifest(dir, R"({
        "format": "libmapa-package", "version": 2,
        "datasets": [ { "id": "osm", "filePath": "osm.sqlitedb" },
                      { "id": "sat", "filePath": "no_esta.sqlitedb" } ],
        "elevation": { "file": "dem_ausente.sqlitedb" },
        "overlays": [ { "id": "fir", "file": "FIR.geo" } ],
        "features": { "file": "e.db", "seed": "semilla_ausente.db" }
    })");

    const auto p = DataPackage::load(dir);
    QVERIFY(p);
    QCOMPARE(p->datasets.size(), 2);              // se conservan: el servicio decide
    QCOMPARE(p->warnings.size(), 4);
    const QString todo = p->warnings.join(QLatin1Char('\n'));
    QVERIFY(todo.contains(QStringLiteral("no_esta.sqlitedb")));
    QVERIFY(todo.contains(QStringLiteral("dem_ausente.sqlitedb")));
    QVERIFY(todo.contains(QStringLiteral("FIR.geo")));
    QVERIFY(todo.contains(QStringLiteral("semilla_ausente.db")));
}

// La BD de entidades NO va a la carpeta del paquete (puede ser de solo lectura)
// sino a AppData/<id del paquete>/. La primera vez se copia la semilla, y la
// copia queda escribible aunque el original fuera de solo lectura.
void TstDataPackage::featuresGoToUserData()
{
    const QString dir = m_tmp.filePath(QStringLiteral("con_semilla"));
    touch(dir + QStringLiteral("/osm.sqlitedb"));
    const QString semilla = dir + QStringLiteral("/entidades_iniciales.db");
    touch(semilla, "SEMILLA");
    QFile::setPermissions(semilla, QFileDevice::ReadOwner | QFileDevice::ReadUser);

    writeManifest(dir, R"({
        "format": "libmapa-package", "version": 2,
        "package": { "id": "prueba_semilla" },
        "datasets": [ { "id": "osm", "filePath": "osm.sqlitedb" } ],
        "features": { "file": "entidades.db", "seed": "entidades_iniciales.db" }
    })");

    const auto p = DataPackage::load(dir);
    QVERIFY(p);

    const QString esperado =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .absoluteFilePath(QStringLiteral("prueba_semilla/entidades.db"));
    QCOMPARE(p->resolveFeaturesPath(), esperado);
    QVERIFY(!p->resolveFeaturesPath().startsWith(p->info.directory));

    QFile::remove(esperado);                       // por si quedo de otra ejecucion
    QString error;
    const QString ruta = p->prepareFeaturesFile(&error);
    QVERIFY2(!ruta.isEmpty(), qPrintable(error));
    QCOMPARE(ruta, esperado);

    QFile copia(ruta);
    QVERIFY(copia.open(QIODevice::ReadOnly));
    QCOMPARE(copia.readAll(), QByteArray("SEMILLA"));
    copia.close();
    QVERIFY(QFileInfo(ruta).isWritable());

    // La segunda vez NO se pisa lo que ya guardo el usuario.
    QVERIFY(copia.open(QIODevice::WriteOnly));
    copia.write("DEL USUARIO");
    copia.close();
    QCOMPARE(p->prepareFeaturesFile(), esperado);
    QVERIFY(copia.open(QIODevice::ReadOnly));
    QCOMPARE(copia.readAll(), QByteArray("DEL USUARIO"));
    copia.close();

    // Se deja la semilla borrable para que QTemporaryDir pueda limpiar.
    QFile::setPermissions(semilla, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::remove(esperado);
}

QTEST_MAIN(TstDataPackage)
#include "tst_datapackage.moc"
