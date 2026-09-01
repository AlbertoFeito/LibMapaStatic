#include "db/SqliteConnectionPool.h"
#include "db/VectorRepository.h"
#include "db/Schema.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;

class TstVectorRepository : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    // --- Los fallos del esquema original, demostrados -------------------

    /*! "NOT nullptr" no es SQL valido: la tabla no se crea siquiera. */
    void originalDdlIsASyntaxError();
    /*! "no_punto KEY INTEGER" no declara ninguna clave primaria. */
    void originalPrimaryKeyIsNotAKey();
    /*! Los indices de columna descuadrados al leer puntos. */
    void originalColumnIndicesAreShifted();

    // --- El esquema nuevo -----------------------------------------------

    void createsSchemaAndRecordsVersion();
    void migrationIsIdempotent();

    void insertsAndLoadsPoints();
    void rejectsPointsWithoutNameOrPosition();
    void handlesQuotesInNames();
    void enforcesUniqueName();

    void insertsVehicleWithAisByComposition();
    void loadsVesselsByTimestamp();

    void appendsTrackInOneTransaction();
    /*! Una transaccion frente a N commits sueltos, medido. */
    void transactionBeatsLooseCommits();
    void prunesTrack();

    /*! ON DELETE CASCADE: borrar un punto se lleva su trayectoria. */
    void deletingPointRemovesItsTrack();

    void insertsPolygonAtomically();
    void rollsBackIncompletePolygon();
    void insertsAndLoadsRoutes();

    /*! Ni una tabla creada en tiempo de ejecucion. */
    void neverCreatesTablesAtRuntime();

    void reportsErrorsInsteadOfSwallowingThem();

private:
    QString dbPath(const QString &name) const { return m_dir.filePath(name); }
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

// ------------------------------------------------ el esquema original ------

void TstVectorRepository::originalDdlIsASyntaxError()
{
    const QString conn = QStringLiteral("orig_ddl");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(dbPath(QStringLiteral("orig.sig")));
        QVERIFY(db.open());

        // Literalmente el DDL de CBDatos::guardarNuevoPunto.
        const QString ddl = QStringLiteral(
            "CREATE TABLE IF NOT EXISTS puntos (no_punto KEY INTEGER NOT nullptr "
            "UNIQUE, nombre TEXT NOT nullptr UNIQUE, descripcion TEXT, tipo TEXT, "
            "simbolo BLOB, latitud DOUBLE, longitud DOUBLE, fecha_creacion TEXT)");

        QSqlQuery q(db);

        // El original hace: if (Consulta.prepare(Crea)) Consulta.exec();
        // El prepare falla, asi que el exec ni se intenta.
        const bool preparado = q.prepare(ddl);
        QVERIFY2(!preparado,
                 "Se esperaba que 'NOT nullptr' fuera un error de sintaxis");
        QVERIFY(q.lastError().text().contains(QStringLiteral("nullptr")));

        // Y por tanto la tabla NO existe. En una instalacion nueva, la
        // aplicacion no crea sus tablas: solo funciona sobre ficheros .sig
        // heredados de antes del Find&Replace de NULL por nullptr.
        QSqlQuery comprueba(db);
        QVERIFY(comprueba.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE name='puntos'")));
        QVERIFY(comprueba.next());
        QCOMPARE(comprueba.value(0).toInt(), 0);

        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
}

void TstVectorRepository::originalPrimaryKeyIsNotAKey()
{
    const QString conn = QStringLiteral("orig_pk");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(dbPath(QStringLiteral("origpk.sig")));
        QVERIFY(db.open());

        QSqlQuery q(db);
        // Con NOT NULL arreglado, como estaba antes del reemplazo.
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE puntos (no_punto KEY INTEGER NOT NULL UNIQUE, "
            "nombre TEXT NOT NULL UNIQUE, descripcion TEXT, tipo TEXT, "
            "simbolo BLOB, latitud DOUBLE, longitud DOUBLE, fecha_creacion TEXT)")));

        QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(puntos)")));
        bool hayClavePrimaria = false;
        QString tipoDeNoPunto;
        while (q.next()) {
            if (q.value(1).toString() == QStringLiteral("no_punto"))
                tipoDeNoPunto = q.value(2).toString();
            if (q.value(5).toInt() != 0)
                hayClavePrimaria = true;
        }

        // "KEY INTEGER" es el TIPO de la columna, no una declaracion de clave.
        QCOMPARE(tipoDeNoPunto, QStringLiteral("KEY INTEGER"));
        QVERIFY2(!hayClavePrimaria,
                 "Se esperaba que la tabla no tuviera clave primaria");

        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
}

void TstVectorRepository::originalColumnIndicesAreShifted()
{
    // La tabla tiene 8 columnas; cargarPuntos las lee como si fueran 7,
    // porque se comento la lectura de 'tipo' sin corregir los indices.
    const QStringList columnas{
        QStringLiteral("no_punto"), QStringLiteral("nombre"),
        QStringLiteral("descripcion"), QStringLiteral("tipo"),
        QStringLiteral("simbolo"), QStringLiteral("latitud"),
        QStringLiteral("longitud"), QStringLiteral("fecha_creacion")};

    // Lo que hace el original: value(3) como simbolo, value(4) y value(5)
    // como latitud y longitud, value(6) como fecha.
    QCOMPARE(columnas.at(3), QStringLiteral("tipo"));       // se lee como simbolo
    QCOMPARE(columnas.at(4), QStringLiteral("simbolo"));    // se lee como latitud
    QCOMPARE(columnas.at(5), QStringLiteral("latitud"));    // se lee como longitud
    QCOMPARE(columnas.at(6), QStringLiteral("longitud"));   // se lee como fecha

    // El repositorio nuevo lee por NOMBRE, asi que este descuadre no puede
    // repetirse aunque cambie el orden de las columnas.
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("porNombre.db"))));

    MapPoint p;
    p.name = QStringLiteral("Faro");
    p.position = QGeoCoordinate(23.05, -82.35);
    p.altitude = 42.0;
    QPixmap icono(8, 8);
    icono.fill(Qt::magenta);
    p.icon = icono;

    const auto id = repo.insertPoint(p);
    QVERIFY(id.has_value());

    const auto leido = repo.findPointByName(QStringLiteral("Faro"));
    QVERIFY(leido.has_value());
    QCOMPARE(leido->name, QStringLiteral("Faro"));
    QVERIFY(qAbs(leido->position.latitude() - 23.05) < 1e-9);
    QVERIFY(qAbs(leido->position.longitude() + 82.35) < 1e-9);
    QCOMPARE(leido->altitude, 42.0);
    QVERIFY2(!leido->icon.isNull(), "El simbolo se leyo de la columna correcta");
    QCOMPARE(leido->icon.size(), QSize(8, 8));
}

// --------------------------------------------------- el esquema nuevo ------

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
        MapPoint p;
        p.name = QStringLiteral("Persistente");
        p.position = QGeoCoordinate(23.0, -82.0);
        QVERIFY(repo.insertPoint(p).has_value());
    }
    SqliteConnectionPool::closeAllForCurrentThread();

    // Reabrir no debe volver a crear nada ni perder los datos.
    VectorRepository repo;
    QVERIFY(repo.open(ruta));
    QCOMPARE(repo.schemaVersion(), schema::kCurrentVersion);
    QCOMPARE(repo.loadPoints().size(), 1);
}

void TstVectorRepository::insertsAndLoadsPoints()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("puntos.db"))));

    MapPoint a;
    a.name = QStringLiteral("Morro");
    a.description = QStringLiteral("Castillo");
    a.position = QGeoCoordinate(23.1500, -82.3560);
    a.color = QColor(Qt::green);
    a.labelVisible = false;

    const auto id = repo.insertPoint(a);
    QVERIFY(id.has_value());
    QVERIFY(*id > 0);

    const auto puntos = repo.loadPoints();
    QCOMPARE(puntos.size(), 1);
    QCOMPARE(puntos.first().name, QStringLiteral("Morro"));
    QCOMPARE(puntos.first().description, QStringLiteral("Castillo"));
    QCOMPARE(puntos.first().color, QColor(Qt::green));
    QCOMPARE(puntos.first().labelVisible, false);

    // Modificar y borrar.
    MapPoint mod = puntos.first();
    mod.description = QStringLiteral("Faro del Morro");
    QVERIFY(repo.updatePoint(mod));
    QCOMPARE(repo.loadPoints().first().description,
             QStringLiteral("Faro del Morro"));

    QVERIFY(repo.removePoint(*id));
    QVERIFY(repo.loadPoints().isEmpty());
    QVERIFY(!repo.removePoint(*id));      // ya no esta
}

void TstVectorRepository::rejectsPointsWithoutNameOrPosition()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("valida.db"))));
    QSignalSpy errores(&repo, &VectorRepository::errorOccurred);

    MapPoint sinNombre;
    sinNombre.position = QGeoCoordinate(23.0, -82.0);
    QVERIFY(!repo.insertPoint(sinNombre).has_value());

    MapPoint sinPosicion;
    sinPosicion.name = QStringLiteral("Fantasma");
    QVERIFY(!repo.insertPoint(sinPosicion).has_value());

    // Los errores se PROPAGAN. El original hacia
    // "if (prepare(x)) exec();" y tiraba el resultado.
    QCOMPARE(errores.count(), 2);
    QVERIFY(repo.loadPoints().isEmpty());
}

void TstVectorRepository::handlesQuotesInNames()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("comillas.db"))));

    // Con el INSERT concatenado del original, esto rompia la sentencia.
    MapPoint p;
    p.name = QStringLiteral("O'Brien \"el rapido\"; DROP TABLE punto;--");
    p.description = QStringLiteral("100% v'alido");
    p.position = QGeoCoordinate(22.0, -80.0);

    QVERIFY(repo.insertPoint(p).has_value());

    const auto puntos = repo.loadPoints();
    QCOMPARE(puntos.size(), 1);
    QCOMPARE(puntos.first().name, p.name);
    QCOMPARE(puntos.first().description, p.description);
}

void TstVectorRepository::enforcesUniqueName()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("unico.db"))));

    MapPoint p;
    p.name = QStringLiteral("Repetido");
    p.position = QGeoCoordinate(22.0, -80.0);
    QVERIFY(repo.insertPoint(p).has_value());

    // Ahora la restriccion existe de verdad, no es un "NOT nullptr" inerte.
    QVERIFY(!repo.insertPoint(p).has_value());
    QCOMPARE(repo.loadPoints().size(), 1);
}

void TstVectorRepository::insertsVehicleWithAisByComposition()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("vehiculos.db"))));

    MapVehicle avion;
    avion.name = QStringLiteral("CU-T1234");
    avion.kind = VehicleKind::Aerial;
    avion.position = QGeoCoordinate(23.0, -82.0);
    avion.altitude = 9500.0;
    avion.heading = 270.0;
    avion.speed = 850.0;
    QVERIFY(repo.insertVehicle(avion).has_value());

    MapVehicle buque;
    buque.name = QStringLiteral("Rio Almendares");
    buque.kind = VehicleKind::Naval;
    buque.position = QGeoCoordinate(23.2, -82.4);
    buque.speed = 12.0;
    buque.ais.mmsi = QStringLiteral("323456789");
    buque.ais.shipName = QStringLiteral("RIO ALMENDARES");
    buque.ais.flag = QStringLiteral("CU");
    buque.ais.length = 120.0;
    buque.ais.lastUpdateUtcMs = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(repo.insertVehicle(buque).has_value());

    const auto vehiculos = repo.loadVehicles();
    QCOMPARE(vehiculos.size(), 2);

    for (const MapVehicle &v : vehiculos) {
        if (v.kind == VehicleKind::Aerial) {
            // El avion NO arrastra campos AIS: van por composicion, no por
            // herencia. En el original, CBarco anadia unos cincuenta getters
            // que CAvion heredaba sin poder usarlos.
            QVERIFY(!v.ais.isValid());
            QCOMPARE(v.altitude, 9500.0);
        } else if (v.kind == VehicleKind::Naval) {
            QVERIFY(v.ais.isValid());
            QCOMPARE(v.ais.mmsi, QStringLiteral("323456789"));
            QCOMPARE(v.ais.length, 120.0);
        }
    }
}

void TstVectorRepository::loadsVesselsByTimestamp()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("ais.db"))));

    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const qint64 unDia = 24LL * 3600 * 1000;

    for (int i = 0; i < 3; ++i) {
        MapVehicle b;
        b.name = QStringLiteral("Buque%1").arg(i);
        b.kind = VehicleKind::Naval;
        b.position = QGeoCoordinate(23.0 + i * 0.1, -82.0);
        b.ais.mmsi = QStringLiteral("30000000%1").arg(i);
        b.ais.lastUpdateUtcMs = ahora - i * unDia;   // hoy, ayer, anteayer
        QVERIFY(repo.insertVehicle(b).has_value());
    }

    // Con las fechas como enteros, "del ultimo dia" es una comparacion. En el
    // original eran TEXT "dd/MM/yyyy hh:mm:ss" y habia que comparar dia y mes
    // a mano en C++, con un caso especial para diciembre.
    QCOMPARE(repo.loadVesselsUpdatedSince(ahora - unDia / 2).size(), 1);
    QCOMPARE(repo.loadVesselsUpdatedSince(ahora - unDia - 1000).size(), 2);
    QCOMPARE(repo.loadVesselsUpdatedSince(0).size(), 3);
}

void TstVectorRepository::appendsTrackInOneTransaction()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("traza.db"))));

    MapPoint p;
    p.name = QStringLiteral("Movil");
    p.position = QGeoCoordinate(23.0, -82.0);
    const auto id = repo.insertPoint(p);
    QVERIFY(id.has_value());

    QVector<TrackSample> muestras;
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < 200; ++i) {
        TrackSample s;
        s.position = QGeoCoordinate(23.0 + i * 0.001, -82.0 + i * 0.001);
        s.timeUtcMs = t0 + i * 1000;
        s.speed = 10.0 + i;
        muestras.append(s);
    }

    QElapsedTimer t;
    t.start();
    QVERIFY(repo.appendTrackBatch(*id, muestras));
    const qint64 ms = t.elapsed();

    const auto leidas = repo.loadTrack(*id, 1000);
    QCOMPARE(leidas.size(), 200);
    // Devueltas en orden cronologico.
    QVERIFY(leidas.first().timeUtcMs < leidas.last().timeUtcMs);
    QCOMPARE(leidas.first().speed, 10.0);

    qInfo() << "200 muestras en una transaccion:" << ms << "ms";
}

void TstVectorRepository::prunesTrack()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("poda.db"))));

    MapPoint p;
    p.name = QStringLiteral("Podado");
    p.position = QGeoCoordinate(23.0, -82.0);
    const auto id = repo.insertPoint(p);
    QVERIFY(id.has_value());

    QVector<TrackSample> muestras;
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < 100; ++i) {
        TrackSample s;
        s.position = QGeoCoordinate(23.0, -82.0);
        s.timeUtcMs = t0 + i * 1000;
        muestras.append(s);
    }
    QVERIFY(repo.appendTrackBatch(*id, muestras));

    QVERIFY(repo.pruneTrack(*id, 10));
    const auto quedan = repo.loadTrack(*id, 1000);
    QCOMPARE(quedan.size(), 10);
    // Se conservan las MAS RECIENTES.
    QCOMPARE(quedan.last().timeUtcMs, t0 + 99 * 1000);
}

void TstVectorRepository::deletingPointRemovesItsTrack()
{
    const QString ruta = dbPath(QStringLiteral("cascada.db"));
    VectorRepository repo;
    QVERIFY(repo.open(ruta));

    MapVehicle v;
    v.name = QStringLiteral("Efimero");
    v.kind = VehicleKind::Naval;
    v.position = QGeoCoordinate(23.0, -82.0);
    v.ais.mmsi = QStringLiteral("999888777");
    const auto id = repo.insertVehicle(v);
    QVERIFY(id.has_value());

    TrackSample s;
    s.position = QGeoCoordinate(23.0, -82.0);
    QVERIFY(repo.appendTrack(*id, s));
    QCOMPARE(repo.loadTrack(*id).size(), 1);

    QVERIFY(repo.removePoint(*id));

    // Trayectoria, vehiculo y AIS desaparecen con el punto. En el original,
    // la tabla trayectorias_<nombre> quedaba huerfana para siempre.
    QCOMPARE(repo.loadTrack(*id).size(), 0);
    QVERIFY(repo.loadVehicles().isEmpty());

    QSqlQuery q(SqliteConnectionPool::connectionFor(
        QStringLiteral("vector_cascada.db"), ruta,
        SqliteConnectionPool::Mode::ReadWrite));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM buque_ais")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstVectorRepository::insertsPolygonAtomically()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("poligonos.db"))));

    MapPolygon poly;
    poly.name = QStringLiteral("Zona de exclusion");
    poly.lineColor = QColor(Qt::red);
    poly.fillColor = QColor(255, 0, 0, 60);
    for (int i = 0; i < 200; ++i)
        poly.vertices.append(QGeoCoordinate(23.0 + i * 0.001, -82.0 + i * 0.001));

    QElapsedTimer t;
    t.start();
    const auto id = repo.insertPolygon(poly);
    const qint64 ms = t.elapsed();
    QVERIFY(id.has_value());

    const auto leidos = repo.loadPolygons();
    QCOMPARE(leidos.size(), 1);
    QCOMPARE(leidos.first().name, poly.name);
    QCOMPARE(leidos.first().vertices.size(), 200);
    QCOMPARE(leidos.first().lineColor, QColor(Qt::red));
    QCOMPARE(leidos.first().fillColor.alpha(), 60);
    // El orden de los vertices se conserva.
    QVERIFY(qAbs(leidos.first().vertices.first().latitude() - 23.0) < 1e-9);

    qInfo() << "Poligono de 200 vertices en una transaccion:" << ms << "ms";

    QVERIFY(repo.removePolygon(*id));
    QVERIFY(repo.loadPolygons().isEmpty());
}

void TstVectorRepository::rollsBackIncompletePolygon()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("rollback.db"))));

    MapPolygon valido;
    valido.name = QStringLiteral("Bueno");
    for (int i = 0; i < 4; ++i)
        valido.vertices.append(QGeoCoordinate(23.0 + i * 0.01, -82.0));
    QVERIFY(repo.insertPolygon(valido).has_value());

    // Nombre repetido: el INSERT de la cabecera falla y no debe quedar nada
    // a medias.
    MapPolygon repetido = valido;
    QVERIFY(!repo.insertPolygon(repetido).has_value());

    QCOMPARE(repo.loadPolygons().size(), 1);
    QCOMPARE(repo.loadPolygons().first().vertices.size(), 4);

    // Menos de tres vertices no es un poligono.
    MapPolygon corto;
    corto.name = QStringLiteral("Corto");
    corto.vertices.append(QGeoCoordinate(23.0, -82.0));
    corto.vertices.append(QGeoCoordinate(23.1, -82.0));
    QVERIFY(!repo.insertPolygon(corto).has_value());
    QCOMPARE(repo.loadPolygons().size(), 1);
}

void TstVectorRepository::insertsAndLoadsRoutes()
{
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("rutas.db"))));

    MapRoute r;
    r.name = QStringLiteral("Patrulla norte");
    r.color = QColor(Qt::blue);
    for (int i = 0; i < 5; ++i) {
        MapRoutePoint p;
        p.position = QGeoCoordinate(23.0 + i * 0.05, -82.0 - i * 0.05);
        p.description = QStringLiteral("Waypoint %1").arg(i);
        p.priority = i;
        p.approachRadiusMeters = 50.0 + i;
        r.points.append(p);
    }

    const auto id = repo.insertRoute(r);
    QVERIFY(id.has_value());

    const auto rutas = repo.loadRoutes();
    QCOMPARE(rutas.size(), 1);
    QCOMPARE(rutas.first().points.size(), 5);
    // 'pos' era TEXT en el original: la coordenada iba dentro de una cadena.
    // Aqui son dos numeros y el orden se conserva.
    QCOMPARE(rutas.first().points.at(2).description, QStringLiteral("Waypoint 2"));
    QCOMPARE(rutas.first().points.at(3).priority, 3);
    QVERIFY(qAbs(rutas.first().points.at(4).approachRadiusMeters - 54.0) < 1e-9);

    QVERIFY(repo.removeRoute(*id));
    QVERIFY(repo.loadRoutes().isEmpty());
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

    // Se guarda de todo, con nombres que en el original habrian generado
    // tablas: trayectorias_<nombre>, poligono_<nombre>, Ruta_<fecha>.
    MapVehicle v;
    v.name = QStringLiteral("Buque de prueba");
    v.kind = VehicleKind::Naval;
    v.position = QGeoCoordinate(23.0, -82.0);
    v.ais.mmsi = QStringLiteral("111222333");
    const auto id = repo.insertVehicle(v);
    QVERIFY(id.has_value());

    TrackSample s;
    s.position = QGeoCoordinate(23.0, -82.0);
    QVERIFY(repo.appendTrack(*id, s));

    MapPolygon poly;
    poly.name = QStringLiteral("Area 51");
    for (int i = 0; i < 4; ++i)
        poly.vertices.append(QGeoCoordinate(23.0 + i * 0.01, -82.0));
    QVERIFY(repo.insertPolygon(poly).has_value());

    MapRoute r;
    r.name = QStringLiteral("Ruta_01_01_2026");
    MapRoutePoint rp;
    rp.position = QGeoCoordinate(23.0, -82.0);
    r.points.append(rp);
    QVERIFY(repo.insertRoute(r).has_value());

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
    MapPoint p;
    p.name = QStringLiteral("X");
    p.position = QGeoCoordinate(23.0, -82.0);
    QVERIFY(!repo.insertPoint(p).has_value());
    QVERIFY(repo.loadPoints().isEmpty());
    QVERIFY(!repo.updatePoint(p));
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

    // --- Como se hace ahora: todo en una transaccion ---------------------
    VectorRepository repo;
    QVERIFY(repo.open(dbPath(QStringLiteral("entx.db"))));

    MapPolygon poly;
    poly.name = QStringLiteral("Medido");
    for (int i = 0; i < kFilas; ++i)
        poly.vertices.append(QGeoCoordinate(23.0 + i * 0.001, -82.0));

    QElapsedTimer t;
    t.start();
    QVERIFY(repo.insertPolygon(poly).has_value());
    const qint64 msTx = t.elapsed();

    QCOMPARE(repo.loadPolygons().first().vertices.size(), kFilas);

    qInfo() << kFilas << "filas ->" << msSueltos << "ms sueltas,"
            << msTx << "ms en una transaccion";

    // No se afirma un factor concreto: depende del disco y de si hay fsync
    // real. Lo que si debe cumplirse es que la transaccion no sea peor.
    QVERIFY2(msTx <= msSueltos + 50,
             qPrintable(QStringLiteral("transaccion %1 ms frente a %2 ms")
                            .arg(msTx).arg(msSueltos)));
}

QTEST_MAIN(TstVectorRepository)
#include "tst_vectorrepository.moc"
