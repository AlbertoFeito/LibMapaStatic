#include "db/SqliteConnectionPool.h"
#include "db/VectorRepository.h"
#include "db/Schema.h"

#include "libmapa/MapFeature.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;

//! Tests del almacen de entidades de dibujo (la unica cara de VectorRepository
//! tras el corte limpio del modelo vectorial legado): esquema, guardar/cargar
//! Features y capas, multi-parte, seguridad ante comillas y propagacion de
//! errores. Nada de puntos/vehiculos/AIS/poligonos/rutas: eso se retiro.
class TstVectorRepository : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    void createsSchemaAndRecordsVersion();
    void migrationIsIdempotent();

    void savesAndLoadsFeatures();
    void removesAndClearsFeatures();
    void filtersFeaturesByLayer();
    void savesMultiPartFeature();
    void handlesQuotesAndInjectionInNames();
    void savesAndLoadsLayers();
    void rejectsInvalidGeometry();

    /*! Ni una tabla creada en tiempo de ejecucion, guarde lo que guarde. */
    void neverCreatesTablesAtRuntime();
    void reportsErrorsInsteadOfSwallowingThem();
    /*! Una transaccion frente a N commits sueltos, medido. */
    void transactionBeatsLooseCommits();

private:
    QString dbPath(const QString &name) const { return m_dir.filePath(name); }

    //! Entidad valida de ejemplo (polilinea de 'n' vertices) en la capa dada.
    static MapFeature lineFeature(const QString &layer, const QString &name,
                                  int n = 3)
    {
        MapFeature f;
        f.layerId = layer;
        f.kind = GeometryKind::Polyline;
        f.name = name;
        for (int i = 0; i < n; ++i)
            f.geometry.append(QGeoCoordinate(23.0 + i * 0.01, -82.0 + i * 0.01));
        return f;
    }

    QTemporaryDir m_dir;
};

void TstVectorRepository::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TstVectorRepository::cleanup()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

void TstVectorRepository::cleanupTestCase()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

void TstVectorRepository::createsSchemaAndRecordsVersion()
{
    VectorRepository repo;
    QVERIFY2(repo.open(dbPath(QStringLiteral("nuevo.db"))),
             qPrintable(repo.lastError()));
    QVERIFY(repo.isOpen());
    QCOMPARE(repo.schemaVersion(), schema::kCurrentVersion);
}

void TstVectorRepository::migrationIsIdempotent()
{
    const QString ruta = dbPath(QStringLiteral("idem.db"));
    {
        VectorRepository repo;
        QVERIFY(repo.open(ruta));
        QVERIFY(repo.saveFeature(lineFeature(QStringLiteral("capa"),
                                             QStringLiteral("Persistente"))).has_value());
    }
    SqliteConnectionPool::closeAllForCurrentThread();

    // Reabrir no debe volver a crear nada ni perder los datos.
    VectorRepository repo;
    QVERIFY(repo.open(ruta));
    QCOMPARE(repo.schemaVersion(), schema::kCurrentVersion);
    QCOMPARE(repo.loadFeatures().size(), 1);
}

void TstVectorRepository::savesAndLoadsFeatures()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("features.db"))));

    MapFeature pol;
    pol.layerId = QStringLiteral("zonas");
    pol.kind = GeometryKind::Polygon;
    pol.type = QStringLiteral("zona_prohibida");
    pol.name = QStringLiteral("Area 1");
    pol.description = QStringLiteral("prueba");
    pol.style.lineColor = QColor(Qt::red);
    pol.style.fillColor = QColor(255, 0, 0, 60);
    pol.style.lineWidth = 3.5;
    pol.attributes.insert(QStringLiteral("techo_m"), 120);
    pol.attributes.insert(QStringLiteral("vigencia"), QStringLiteral("2026-09-01"));
    for (int i = 0; i < 4; ++i)
        pol.geometry.append(QGeoCoordinate(23.0 + i * 0.01, -82.0));

    const auto id = repo.saveFeature(pol);
    QVERIFY(id.has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 1);
    const MapFeature &f = cargadas.first();
    QCOMPARE(f.layerId, QStringLiteral("zonas"));
    QCOMPARE(f.kind, GeometryKind::Polygon);
    QCOMPARE(f.type, QStringLiteral("zona_prohibida"));
    QCOMPARE(f.name, QStringLiteral("Area 1"));
    QCOMPARE(f.geometry.size(), 4);
    QCOMPARE(f.style.lineColor, QColor(Qt::red));
    QCOMPARE(f.style.fillColor.alpha(), 60);
    QVERIFY(qAbs(f.style.lineWidth - 3.5) < 1e-9);
    // Los atributos de dominio (JSON) sobreviven el viaje.
    QCOMPARE(f.attributes.value(QStringLiteral("techo_m")).toInt(), 120);
    QCOMPARE(f.attributes.value(QStringLiteral("vigencia")).toString(),
             QStringLiteral("2026-09-01"));
}

void TstVectorRepository::removesAndClearsFeatures()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("remove.db"))));

    const auto id = repo.saveFeature(lineFeature(QStringLiteral("c"),
                                                 QStringLiteral("A")));
    QVERIFY(id.has_value());
    QVERIFY(repo.saveFeature(lineFeature(QStringLiteral("c"),
                                         QStringLiteral("B"))).has_value());
    QCOMPARE(repo.loadFeatures().size(), 2);

    QVERIFY(repo.removeFeatureRow(*id));
    QCOMPARE(repo.loadFeatures().size(), 1);

    QVERIFY(repo.clearFeatures());
    QVERIFY(repo.loadFeatures().isEmpty());
}

void TstVectorRepository::filtersFeaturesByLayer()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("capas.db"))));

    QVERIFY(repo.saveFeature(lineFeature(QStringLiteral("rojo"),
                                         QStringLiteral("r1"))).has_value());
    QVERIFY(repo.saveFeature(lineFeature(QStringLiteral("rojo"),
                                         QStringLiteral("r2"))).has_value());
    QVERIFY(repo.saveFeature(lineFeature(QStringLiteral("azul"),
                                         QStringLiteral("a1"))).has_value());

    QCOMPARE(repo.loadFeaturesInLayer(QStringLiteral("rojo")).size(), 2);
    QCOMPARE(repo.loadFeaturesInLayer(QStringLiteral("azul")).size(), 1);
    QVERIFY(repo.loadFeaturesInLayer(QStringLiteral("verde")).isEmpty());
}

void TstVectorRepository::savesMultiPartFeature()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("multiparte.db"))));

    // Un .geo entero (varias polilineas) como UNA sola entidad.
    MapFeature f;
    f.layerId = QStringLiteral("costa");
    f.kind = GeometryKind::Polyline;
    f.name = QStringLiteral("Lineas de costa");
    QVector<QGeoCoordinate> p1{ QGeoCoordinate(23.0, -82.0),
                                QGeoCoordinate(23.1, -82.0) };
    QVector<QGeoCoordinate> p2{ QGeoCoordinate(24.0, -81.0),
                                QGeoCoordinate(24.1, -81.1),
                                QGeoCoordinate(24.2, -81.2) };
    f.parts = { p1, p2 };
    f.geometry = p1;                 // la primera parte coincide con geometry
    QVERIFY(f.isMultiPart());
    QVERIFY(repo.saveFeature(f).has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 1);
    QVERIFY(cargadas.first().isMultiPart());
    QCOMPARE(cargadas.first().parts.size(), 2);
    QCOMPARE(cargadas.first().parts.at(0).size(), 2);
    QCOMPARE(cargadas.first().parts.at(1).size(), 3);
}

void TstVectorRepository::handlesQuotesAndInjectionInNames()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("comillas.db"))));

    // Con un INSERT concatenado esto romperia la sentencia o seria inyeccion.
    // Con bindValue es un valor mas.
    MapFeature f = lineFeature(QStringLiteral("capa"),
        QStringLiteral("O'Brien \"rapido\"; DROP TABLE entidad;--"));
    f.attributes.insert(QStringLiteral("nota"), QStringLiteral("100% v'alido"));
    QVERIFY(repo.saveFeature(f).has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 1);
    QCOMPARE(cargadas.first().name, f.name);
    QCOMPARE(cargadas.first().attributes.value(QStringLiteral("nota")).toString(),
             QStringLiteral("100% v'alido"));
}

void TstVectorRepository::savesAndLoadsLayers()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("layers.db"))));

    LayerInfo a;
    a.id = QStringLiteral("fondo");
    a.displayName = QStringLiteral("Fondo");
    a.zOrder = 0;
    LayerInfo b;
    b.id = QStringLiteral("encima");
    b.displayName = QStringLiteral("Encima");
    b.visible = false;
    b.zOrder = 10;
    QVERIFY(repo.saveLayer(a));
    QVERIFY(repo.saveLayer(b));       // una capa vacia tambien se conserva

    const auto capas = repo.loadLayers();
    QCOMPARE(capas.size(), 2);
    // Ordenadas por z_orden ascendente (orden de pintado).
    QCOMPARE(capas.first().id, QStringLiteral("fondo"));
    QCOMPARE(capas.last().id, QStringLiteral("encima"));
    QCOMPARE(capas.last().visible, false);

    // INSERT OR REPLACE por id: reguardar actualiza, no duplica.
    b.displayName = QStringLiteral("Encima (mod)");
    QVERIFY(repo.saveLayer(b));
    QCOMPARE(repo.loadLayers().size(), 2);
}

void TstVectorRepository::rejectsInvalidGeometry()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("invalida.db"))));
    QSignalSpy errores(&repo, &VectorRepository::errorOccurred);

    MapFeature vacia;
    vacia.layerId = QStringLiteral("c");
    vacia.kind = GeometryKind::Polygon;       // sin vertices -> invalida
    QVERIFY(!repo.saveFeature(vacia).has_value());

    MapFeature corta;
    corta.layerId = QStringLiteral("c");
    corta.kind = GeometryKind::Polygon;
    corta.geometry = { QGeoCoordinate(23.0, -82.0),
                       QGeoCoordinate(23.1, -82.0) };   // 2 < 3
    QVERIFY(!repo.saveFeature(corta).has_value());

    QVERIFY(errores.count() >= 2);            // los errores se PROPAGAN
    QVERIFY(repo.loadFeatures().isEmpty());
}

void TstVectorRepository::neverCreatesTablesAtRuntime()
{
    const QString ruta = dbPath(QStringLiteral("sinddl.db"));
    VectorRepository repo;
    QVERIFY(repo.open(ruta));

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("vector_sinddl.db"), ruta,
        SqliteConnectionPool::Mode::ReadWrite);

    auto tablas = [&db] {
        QStringList t;
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"));
        while (q.next())
            t << q.value(0).toString();
        t.sort();
        return t;
    };

    const QStringList antes = tablas();

    // Se guarda de todo, con nombres y tipos que en el original habrian
    // generado tablas nuevas a partir de texto del usuario.
    MapFeature f = lineFeature(QStringLiteral("Ruta_01_2026"),
                               QStringLiteral("Zona_<nombre>"));
    f.type = QStringLiteral("trayectorias_buque");
    QVERIFY(repo.saveFeature(f).has_value());
    LayerInfo capa;
    capa.id = QStringLiteral("poligono_X");
    QVERIFY(repo.saveLayer(capa));

    QCOMPARE(tablas(), antes);
    qInfo() << "Tablas tras guardar de todo:" << antes.size()
            << "-> las mismas, ninguna creada en tiempo de ejecucion";
}

void TstVectorRepository::reportsErrorsInsteadOfSwallowingThem()
{
    VectorRepository repo;
    QSignalSpy errores(&repo, &VectorRepository::errorOccurred);

    // Directorio inexistente.
    QVERIFY(!repo.open(m_dir.filePath(QStringLiteral("no/existe/x.db"))));
    QVERIFY(!repo.lastError().isEmpty());
    QVERIFY(errores.count() > 0);

    // Y usarlo cerrado no revienta: devuelve fallo y avisa.
    QVERIFY(!repo.saveFeature(lineFeature(QStringLiteral("c"),
                                          QStringLiteral("X"))).has_value());
    QVERIFY(repo.loadFeatures().isEmpty());
    QVERIFY(!repo.clearFeatures());
}

void TstVectorRepository::transactionBeatsLooseCommits()
{
    const int kFilas = 500;

    // --- Como lo hacia el original: un INSERT suelto por vertice ---------
    const QString conn = QStringLiteral("sueltos");
    qint64 msSueltos = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(dbPath(QStringLiteral("sueltos.db")));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE v (o INTEGER, la REAL, lo REAL)")));

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < kFilas; ++i) {
            q.prepare(QStringLiteral("INSERT INTO v VALUES (:o,:la,:lo)"));
            q.bindValue(QStringLiteral(":o"), i);
            q.bindValue(QStringLiteral(":la"), 23.0 + i * 0.001);
            q.bindValue(QStringLiteral(":lo"), -82.0);
            QVERIFY(q.exec());
        }
        msSueltos = t.elapsed();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);

    // --- Como se hace ahora: una entidad de 500 vertices en UNA transaccion.
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("entx.db"))));

    MapFeature f = lineFeature(QStringLiteral("medido"),
                               QStringLiteral("Medido"), kFilas);

    QElapsedTimer t;
    t.start();
    QVERIFY(repo.saveFeature(f).has_value());
    const qint64 msTx = t.elapsed();

    QCOMPARE(repo.loadFeatures().first().geometry.size(), kFilas);

    qInfo() << kFilas << "vertices ->" << msSueltos << "ms sueltos,"
            << msTx << "ms en una transaccion";

    // No se afirma un factor concreto (depende del disco); solo que la
    // transaccion no sea peor.
    QVERIFY2(msTx <= msSueltos + 50,
             qPrintable(QStringLiteral("transaccion %1 ms frente a %2 ms")
                            .arg(msTx).arg(msSueltos)));
}

QTEST_MAIN(TstVectorRepository)
#include "tst_vectorrepository.moc"
