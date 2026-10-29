#include "io/DataPackage.h"
#include "io/PlaceIndex.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest>

using namespace libmapa;

// Pruebas de la busqueda de lugares. La BD de prueba es lugares_prueba.sqlitedb,
// generada con herramientas/osm_lugares_a_sqlitedb.py desde lugares_prueba.geojson
// (12 lugares: tildes, un nombre repetido, un caracter % en un nombre...).
class TstPlaces : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizationAgreesWithTheScript();   // C++ y Python normalizan igual
    void findsIgnoringCaseAndAccents();
    void ranksPrefixThenWeight();
    void everyWordMustMatch();
    void wildcardsAreLiteral();
    void limitAndEmptyInput();
    void unusableDatabases();
    void typeLabelsAndZoom();
    void packageManifestDeclaresPlaces();

private:
    QString m_db;
    QTemporaryDir m_tmp;
    PlaceIndex m_idx;
};

// Prepara el indice sobre la BD de prueba.
void TstPlaces::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    m_db = QStringLiteral(LIBMAPA_TESTDATA) + QStringLiteral("/places/lugares_prueba.sqlitedb");
    QVERIFY(QFile::exists(m_db));
    m_idx.setDatabase(m_db);
    QVERIFY(m_idx.isUsable());
}

// El script guarda `busqueda` normalizado en Python; la libreria normaliza lo
// que escribe la persona en C++. Si divergieran, un nombre dejaria de encontrarse:
// se comprueba fila por fila sobre la BD generada.
void TstPlaces::normalizationAgreesWithTheScript()
{
    QCOMPARE(PlaceIndex::normalize(QStringLiteral("Núñez  Jiménez")), QStringLiteral("nunez jimenez"));
    QCOMPARE(PlaceIndex::normalize(QStringLiteral("  Báñes ")), QStringLiteral("banes"));

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst_places_raw"));
    db.setDatabaseName(m_db);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    QVERIFY(db.open());
    {
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT nombre, busqueda FROM lugar")));
        int n = 0;
        while (q.next()) {
            QCOMPARE(PlaceIndex::normalize(q.value(0).toString()), q.value(1).toString());
            ++n;
        }
        QCOMPARE(n, 12);
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_places_raw"));
}

// Mayusculas, tildes y la enye no importan, en ninguno de los dos lados.
void TstPlaces::findsIgnoringCaseAndAccents()
{
    for (const QString &t : { QStringLiteral("guantanamo"), QStringLiteral("GUANTÁNAMO"),
                              QStringLiteral("  Guantánamo ") }) {
        const QVector<Place> r = m_idx.search(t);
        QVERIFY2(r.size() >= 2, qPrintable(t));
        QCOMPARE(r.first().name, QStringLiteral("Guantánamo"));
    }
    QCOMPARE(m_idx.search(QStringLiteral("banes")).first().name, QStringLiteral("Báñes"));
    QCOMPARE(m_idx.search(QStringLiteral("nunez")).first().name, QStringLiteral("Núñez Jiménez"));

    const Place p = m_idx.search(QStringLiteral("santiago")).first();
    QCOMPARE(p.name, QStringLiteral("Santiago de Cuba"));
    QCOMPARE(p.type, QStringLiteral("city"));
    QCOMPARE(p.population, qint64(430000));
    QVERIFY(qAbs(p.coordinate.latitude() - 20.0247) < 1e-6);
    QVERIFY(qAbs(p.coordinate.longitude() + 75.8295) < 1e-6);
}

// Primero los que EMPIEZAN por lo escrito; entre ellos, la ciudad antes que la
// aldea del mismo nombre; los que solo lo contienen, al final.
void TstPlaces::ranksPrefixThenWeight()
{
    const QVector<Place> r = m_idx.search(QStringLiteral("guan"));
    QCOMPARE(r.size(), 3);
    QCOMPARE(r.at(0).type, QStringLiteral("city"));
    QCOMPARE(r.at(1).type, QStringLiteral("hamlet"));
    QCOMPARE(r.at(2).name, QStringLiteral("Bahía de Guantánamo"));   // contiene, no empieza
}

// Todas las palabras, en cualquier orden, y cada una puede ser parte de una palabra.
void TstPlaces::everyWordMustMatch()
{
    QCOMPARE(m_idx.search(QStringLiteral("bahia guantanamo")).size(), 1);
    QCOMPARE(m_idx.search(QStringLiteral("guantanamo bahia")).size(), 1);
    QCOMPARE(m_idx.search(QStringLiteral("pico turq")).size(), 1);
    QCOMPARE(m_idx.search(QStringLiteral("pico playa")).size(), 0);
    QCOMPARE(m_idx.search(QStringLiteral("aeropuerto grajales")).first().type, QStringLiteral("airport"));
}

// % y _ del LIKE de SQL son texto normal: no deben encontrarlo todo.
void TstPlaces::wildcardsAreLiteral()
{
    const QVector<Place> pct = m_idx.search(QStringLiteral("%"));
    QCOMPARE(pct.size(), 1);
    QCOMPARE(pct.first().name, QStringLiteral("100% Cubano"));
    QCOMPARE(m_idx.search(QStringLiteral("_")).size(), 0);
    QCOMPARE(m_idx.search(QStringLiteral("gu_ntanamo")).size(), 0);
    QCOMPARE(m_idx.search(QStringLiteral("100%")).size(), 1);
}

// El limite recorta; texto vacio o solo espacios no devuelve nada.
void TstPlaces::limitAndEmptyInput()
{
    QCOMPARE(m_idx.search(QStringLiteral("a"), 3).size(), 3);
    QCOMPARE(m_idx.search(QStringLiteral("a"), 1).size(), 1);
    QVERIFY(m_idx.search(QStringLiteral("a"), 0).isEmpty());
    QVERIFY(m_idx.search(QString()).isEmpty());
    QVERIFY(m_idx.search(QStringLiteral("   ")).isEmpty());
}

// Sin BD, con una ruta inexistente, con un fichero que no es SQLite y con una BD
// sin la tabla `lugar`: nunca revienta, simplemente no hay indice.
void TstPlaces::unusableDatabases()
{
    PlaceIndex vacio;
    QVERIFY(!vacio.isUsable());
    QVERIFY(vacio.search(QStringLiteral("a")).isEmpty());

    PlaceIndex noExiste;
    noExiste.setDatabase(m_tmp.filePath(QStringLiteral("no_existe.sqlitedb")));
    QVERIFY(!noExiste.isUsable());
    QVERIFY(noExiste.search(QStringLiteral("a")).isEmpty());

    const QString basura = m_tmp.filePath(QStringLiteral("basura.sqlitedb"));
    QFile f(basura);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(2000, 'x'));
    f.close();
    PlaceIndex malo;
    malo.setDatabase(basura);
    QVERIFY(!malo.isUsable());
    QVERIFY(malo.search(QStringLiteral("a")).isEmpty());

    // Un SQLite valido pero de otra cosa (sin tabla lugar).
    const QString otra = m_tmp.filePath(QStringLiteral("otra.sqlitedb"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst_places_otra"));
        db.setDatabaseName(otra);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE otra(x)")));
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tst_places_otra"));
    PlaceIndex sinTabla;
    sinTabla.setDatabase(otra);
    QVERIFY(!sinTabla.isUsable());
    QVERIFY(sinTabla.search(QStringLiteral("a")).isEmpty());

    // Reusar la instancia con otra BD no arrastra la conexion anterior.
    PlaceIndex cambia;
    cambia.setDatabase(otra);
    QVERIFY(!cambia.isUsable());
    cambia.setDatabase(m_db);
    QVERIFY(cambia.isUsable());
    QCOMPARE(cambia.search(QStringLiteral("baracoa")).size(), 1);
}

// Etiqueta en español y zoom de encuadre por tipo.
void TstPlaces::typeLabelsAndZoom()
{
    Place p;
    p.type = QStringLiteral("city");
    QCOMPARE(p.typeLabel(), QStringLiteral("ciudad"));
    QCOMPARE(p.suggestedZoom(), 12);
    p.type = QStringLiteral("airport");
    QCOMPARE(p.typeLabel(), QStringLiteral("aeropuerto"));
    QCOMPARE(p.suggestedZoom(), 14);
    p.type = QStringLiteral("island");
    QCOMPARE(p.suggestedZoom(), 10);
    p.type = QStringLiteral("cosa_rara");
    QCOMPARE(p.typeLabel(), QStringLiteral("cosa_rara"));      // desconocido: tal cual
}

// "places" en mapa.json: ruta relativa resuelta, entra en files(), y si falta
// el fichero se avisa sin impedir abrir el paquete.
void TstPlaces::packageManifestDeclaresPlaces()
{
    const QString dir = m_tmp.filePath(QStringLiteral("paquete"));
    QVERIFY(QDir().mkpath(dir));
    for (const QString &n : { QStringLiteral("osm.sqlitedb"), QStringLiteral("lugares.sqlitedb") }) {
        QFile f(dir + QLatin1Char('/') + n);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    auto escribir = [&](const char *json) {
        QFile m(dir + QStringLiteral("/mapa.json"));
        QVERIFY(m.open(QIODevice::WriteOnly | QIODevice::Truncate));
        m.write(json);
    };

    escribir(R"({ "format": "libmapa-package", "version": 2,
        "datasets": [ { "id": "osm", "filePath": "osm.sqlitedb" } ],
        "places": { "file": "lugares.sqlitedb" } })");
    auto p = DataPackage::load(dir);
    QVERIFY(p);
    QCOMPARE(p->placesFile, QDir(dir).filePath(QStringLiteral("lugares.sqlitedb")));
    QVERIFY(p->files().contains(p->placesFile));
    QVERIFY(p->warnings.isEmpty());

    escribir(R"({ "format": "libmapa-package", "version": 2,
        "datasets": [ { "id": "osm", "filePath": "osm.sqlitedb" } ],
        "places": { "file": "falta.sqlitedb" } })");
    p = DataPackage::load(dir);
    QVERIFY(p);
    QCOMPARE(p->warnings.size(), 1);
    QVERIFY(p->warnings.first().contains(QStringLiteral("falta.sqlitedb")));

    escribir(R"({ "format": "libmapa-package", "version": 2,
        "datasets": [ { "id": "osm", "filePath": "osm.sqlitedb" } ] })");
    p = DataPackage::load(dir);
    QVERIFY(p);
    QVERIFY(p->placesFile.isEmpty());                          // sin "places": nada
}

QTEST_GUILESS_MAIN(TstPlaces)
#include "tst_places.moc"
