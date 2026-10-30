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
    //! Nombre, color (incluido el nulo) y anclaje de cada parte (v6) sobreviven.
    void savesAndLoadsPartsInfo();
    //! El desplazamiento de la etiqueta (v3) sobrevive guardar/cargar.
    void savesAndLoadsLabelOffset();
    //! El lado del anclaje y el tamano de fuente del rotulo (v4) sobreviven
    //! guardar/cargar.
    void savesAndLoadsLabelAnchorAndFontSize();
    //! Un circulo y un arco (v5) guardan su centro, radio y azimuts, y al leer
    //! vuelven iguales (el contorno se recalcula, no se guarda).
    void savesAndLoadsCircleAndArc();
    //! Una BD v2 (sin las columnas label_dx/dy) se abre, se migra a v3 y sus
    //! entidades se leen con offset 0, sin romper.
    void migratesV2DatabaseToV3();
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

void TstVectorRepository::savesAndLoadsLabelOffset()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("labeloffset.db"))));

    MapFeature p;
    p.layerId = QStringLiteral("obj");
    p.kind = GeometryKind::Point;
    p.name = QStringLiteral("Objetivo");
    p.geometry.append(QGeoCoordinate(23.1, -82.3));
    p.style.labelOffsetPx = QPointF(40.0, -25.0);
    p.style.labelAnchorVertex = 0;              // como un área objetivo

    const auto id = repo.saveFeature(p);
    QVERIFY(id.has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 1);
    QVERIFY(qAbs(cargadas.first().style.labelOffsetPx.x() - 40.0) < 1e-9);
    QVERIFY(qAbs(cargadas.first().style.labelOffsetPx.y() + 25.0) < 1e-9);
    QCOMPARE(cargadas.first().style.labelAnchorVertex, 0);
}

void TstVectorRepository::savesAndLoadsLabelAnchorAndFontSize()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("labelanchor.db"))));

    MapFeature p;
    p.layerId = QStringLiteral("obj");
    p.kind = GeometryKind::Point;
    p.name = QStringLiteral("Aerodromo");
    p.geometry.append(QGeoCoordinate(23.1, -82.3));
    p.style.labelAnchor = LabelAnchor::BottomLeft;
    p.style.labelFontSize = 14;
    QVERIFY(repo.saveFeature(p).has_value());

    // Una entidad que no toca nada de esto conserva los valores por defecto.
    MapFeature q = p;
    q.name = QStringLiteral("Normal");
    q.style.labelAnchor = LabelAnchor::Auto;
    q.style.labelFontSize = 0;
    QVERIFY(repo.saveFeature(q).has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 2);
    QCOMPARE(cargadas.at(0).style.labelAnchor, LabelAnchor::BottomLeft);
    QCOMPARE(cargadas.at(0).style.labelFontSize, 14);
    QCOMPARE(cargadas.at(1).style.labelAnchor, LabelAnchor::Auto);
    QCOMPARE(cargadas.at(1).style.labelFontSize, 0);
}

void TstVectorRepository::savesAndLoadsCircleAndArc()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("round.db"))));

    MapFeature c;
    c.layerId = QStringLiteral("tma");
    c.kind = GeometryKind::Circle;
    c.name = QStringLiteral("TMA Habana");
    c.geometry = { QGeoCoordinate(23.1136, -82.3666) };
    c.radiusMeters = 55000.0;
    QVERIFY(repo.saveFeature(c).has_value());

    MapFeature a = c;
    a.kind = GeometryKind::Arc;
    a.name = QStringLiteral("Sector norte");
    a.radiusMeters = 90000.0;
    a.arcStartDeg = 315.0;
    a.arcEndDeg = 45.0;
    a.arcSector = true;
    QVERIFY(repo.saveFeature(a).has_value());

    // Un circulo sin radio no es valido y no se guarda.
    MapFeature malo = c;
    malo.radiusMeters = 0.0;
    QVERIFY(!repo.saveFeature(malo).has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 2);
    const MapFeature &c2 = cargadas.at(0);
    QCOMPARE(c2.kind, GeometryKind::Circle);
    QCOMPARE(c2.geometry.size(), 1);                  // solo el centro, no el contorno
    QVERIFY(qAbs(c2.radiusMeters - 55000.0) < 1e-9);
    QVERIFY(c2.isValid());
    QCOMPARE(c2.outlines().first().size(), 180);       // y el contorno se recalcula
    const MapFeature &a2 = cargadas.at(1);
    QCOMPARE(a2.kind, GeometryKind::Arc);
    QCOMPARE(a2.geometry.size(), 1);
    QVERIFY(qAbs(a2.radiusMeters - 90000.0) < 1e-9);
    QVERIFY(qAbs(a2.arcStartDeg - 315.0) < 1e-9 && qAbs(a2.arcEndDeg - 45.0) < 1e-9);
    QVERIFY(a2.isValid());
    QVERIFY(a2.arcSector && a2.isArea());              // el sector (v7) sobrevive
}

void TstVectorRepository::migratesV2DatabaseToV3()
{
    const QString ruta = dbPath(QStringLiteral("legacy_v2.db"));

    // Fabrica a mano una BD en el esquema v2 (sin label_dx/label_dy), como la
    // que generan los scripts de costa/provincias.
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("mkv2"));
        db.setDatabaseName(ruta);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (2)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE entidad (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " capa TEXT NOT NULL, tipo TEXT NOT NULL DEFAULT '',"
            " geometria INTEGER NOT NULL, nombre TEXT NOT NULL DEFAULT '',"
            " descripcion TEXT NOT NULL DEFAULT '', color_linea INTEGER NOT NULL DEFAULT 0,"
            " color_relleno INTEGER NOT NULL DEFAULT 0, ancho_linea REAL NOT NULL DEFAULT 2,"
            " estilo_linea INTEGER NOT NULL DEFAULT 1, radio_px REAL NOT NULL DEFAULT 6,"
            " etiqueta_visible INTEGER NOT NULL DEFAULT 1, visible INTEGER NOT NULL DEFAULT 1,"
            " simbolo BLOB, atributos TEXT NOT NULL DEFAULT '{}', creado_utc INTEGER NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE entidad_vertice (entidad_id INTEGER NOT NULL, parte INTEGER NOT NULL DEFAULT 0,"
            " orden INTEGER NOT NULL, latitud REAL NOT NULL, longitud REAL NOT NULL,"
            " PRIMARY KEY (entidad_id, parte, orden)) WITHOUT ROWID")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO entidad (capa, geometria, nombre, creado_utc)"
            " VALUES ('tierra', 2, 'Isla', 0)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO entidad_vertice VALUES (1,0,0,23.0,-82.0)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO entidad_vertice VALUES (1,0,1,23.1,-82.0)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO entidad_vertice VALUES (1,0,2,23.1,-82.1)")));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("mkv2"));

    VectorRepository repo;
    QVERIFY(repo.open(ruta));                       // open() debe migrar a v3
    QCOMPARE(repo.schemaVersion(), schema::kCurrentVersion);

    const auto cargadas = repo.loadFeatures();      // sin romper por las columnas
    QCOMPARE(cargadas.size(), 1);
    QCOMPARE(cargadas.first().name, QStringLiteral("Isla"));
    QCOMPARE(cargadas.first().style.labelOffsetPx, QPointF(0.0, 0.0));
    QCOMPARE(cargadas.first().style.labelAnchorVertex, -1);   // no 0 por defecto
    // v4: una BD anterior se abre con el rotulo de siempre.
    QCOMPARE(cargadas.first().style.labelAnchor, LabelAnchor::Auto);
    QCOMPARE(cargadas.first().style.labelFontSize, 0);
    // v5: tampoco es redonda.
    QCOMPARE(cargadas.first().radiusMeters, 0.0);
    QCOMPARE(cargadas.first().kind, GeometryKind::Polygon);
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

void TstVectorRepository::savesAndLoadsPartsInfo()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("partsinfo.db"))));

    MapFeature f;
    f.layerId = QStringLiteral("sitios");
    f.kind = GeometryKind::Point;
    f.name = QStringLiteral("Sitios");
    for (int i = 0; i < 3; ++i)
        f.parts.append({ QGeoCoordinate(23.0 + i * 0.1, -82.0) });
    f.geometry = f.parts.first();
    PartInfo a; a.name = QStringLiteral("Uno 'quote'"); a.colorOverride = QColor(10, 20, 30);
    PartInfo b;                                         // sin nada propio
    PartInfo c; c.name = QStringLiteral("Tres"); c.labelAnchor = LabelAnchor::Top;
    f.partsInfo = { a, b, c };
    QVERIFY(repo.saveFeature(f).has_value());

    const auto cargadas = repo.loadFeatures();
    QCOMPARE(cargadas.size(), 1);
    const MapFeature &g = cargadas.first();
    QCOMPARE(g.partsInfo.size(), 3);
    QCOMPARE(g.partsInfo.at(0).name, a.name);
    QCOMPARE(g.partsInfo.at(0).colorOverride, QColor(10, 20, 30));
    QVERIFY(!g.partsInfo.at(1).colorOverride.isValid());
    QCOMPARE(g.partsInfo.at(2).labelAnchor, LabelAnchor::Top);
    QVERIFY(g.isValid());
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
