/*!
 * vector_db - crea una BD vectorial de ejemplo y muestra su contenido.
 *
 * La fase 5 es capa de datos: no se ve nada en pantalla. Esta herramienta
 * existe para poder INSPECCIONAR el resultado, con DB Browser for SQLite o
 * con sqlite3, en vez de tener que fiarse de los tests.
 *
 * Uso:
 *   vector_db --out mapdata.db            crea la BD con datos de ejemplo
 *   vector_db --out mapdata.db --dump     ademas vuelca lo que contiene
 *   vector_db --file mapdata.db --dump    solo vuelca una BD existente
 */

#include "db/SqliteConnectionPool.h"
#include "db/Schema.h"
#include "db/VectorRepository.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSqlQuery>
#include <QTextStream>

using namespace libmapa;

static void poblar(VectorRepository &repo, QTextStream &out)
{
    out << " Creando datos de ejemplo...\n";

    // --- Puntos, incluido uno con comillas ------------------------------
    const struct { const char *nombre; double lat, lon; const char *desc; }
    puntos[] = {
        {"Castillo del Morro", 23.1500, -82.3560, "Faro historico"},
        {"Punta Maisi",        20.2456, -74.1372, "Extremo oriental"},
        {"Cabo San Antonio",   21.8667, -84.9500, "Extremo occidental"},
        // En el original, el INSERT concatenado se rompia con la comilla.
        {"Bahia de O'Brien",   22.5000, -79.0000, "Nombre con comilla '"},
    };

    for (const auto &p : puntos) {
        MapPoint mp;
        mp.name = QString::fromUtf8(p.nombre);
        mp.description = QString::fromUtf8(p.desc);
        mp.position = QGeoCoordinate(p.lat, p.lon);
        if (!repo.insertPoint(mp))
            out << "   FALLO al insertar " << p.nombre << ": "
                << repo.lastError() << "\n";
    }

    // --- Un avion: SIN campos AIS ---------------------------------------
    MapVehicle avion;
    avion.name = QStringLiteral("CU-T1234");
    avion.kind = VehicleKind::Aerial;
    avion.position = QGeoCoordinate(23.0, -82.0);
    avion.altitude = 9500;
    avion.heading = 270;
    avion.speed = 850;
    repo.insertVehicle(avion);

    // --- Un buque: CON campos AIS, en su propia tabla --------------------
    MapVehicle buque;
    buque.name = QStringLiteral("Rio Almendares");
    buque.kind = VehicleKind::Naval;
    buque.position = QGeoCoordinate(23.15, -82.35);
    buque.speed = 12;
    buque.ais.mmsi = QStringLiteral("323456789");
    buque.ais.shipName = QStringLiteral("RIO ALMENDARES");
    buque.ais.flag = QStringLiteral("CU");
    buque.ais.destination = QStringLiteral("HAVANA");
    buque.ais.length = 120;
    buque.ais.beam = 18;
    buque.ais.lastUpdateUtcMs = QDateTime::currentMSecsSinceEpoch();
    const auto idBuque = repo.insertVehicle(buque);

    // --- Trayectoria del buque, toda en una transaccion ------------------
    if (idBuque) {
        QVector<TrackSample> traza;
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch() - 500 * 1000;
        for (int i = 0; i < 500; ++i) {
            TrackSample s;
            s.position = QGeoCoordinate(23.15 + i * 0.0005, -82.35 + i * 0.0005);
            s.timeUtcMs = t0 + i * 1000;
            s.speed = 12.0;
            traza.append(s);
        }
        repo.appendTrackBatch(*idBuque, traza);
    }

    // --- Poligono y ruta -------------------------------------------------
    MapPolygon poly;
    poly.name = QStringLiteral("Zona de vigilancia");
    poly.lineColor = QColor(Qt::red);
    poly.fillColor = QColor(255, 0, 0, 60);
    poly.vertices = {QGeoCoordinate(23.3, -82.6), QGeoCoordinate(23.3, -82.0),
                     QGeoCoordinate(22.9, -82.0), QGeoCoordinate(22.9, -82.6)};
    repo.insertPolygon(poly);

    MapRoute ruta;
    ruta.name = QStringLiteral("Patrulla norte");
    ruta.color = QColor(Qt::blue);
    for (int i = 0; i < 5; ++i) {
        MapRoutePoint rp;
        rp.position = QGeoCoordinate(23.2 + i * 0.05, -82.4 + i * 0.1);
        rp.description = QStringLiteral("Waypoint %1").arg(i + 1);
        rp.priority = i;
        rp.approachRadiusMeters = 50;
        ruta.points.append(rp);
    }
    repo.insertRoute(ruta);

    out << " Listo.\n\n";
}

static void volcar(VectorRepository &repo, const QString &ruta, QTextStream &out)
{
    out << "==========================================================\n";
    out << " ESQUEMA\n";
    out << "==========================================================\n";

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("dump"), ruta, SqliteConnectionPool::Mode::ReadOnly);

    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' "
                          "AND name NOT LIKE 'sqlite_%' ORDER BY name"));
    QStringList tablas;
    while (q.next())
        tablas << q.value(0).toString();

    out << " version del esquema: " << repo.schemaVersion() << "\n";
    out << " tablas (" << tablas.size() << "): " << tablas.join(QStringLiteral(", "))
        << "\n\n";

    // Se comprueba que las restricciones existen DE VERDAD, que era el
    // problema del "NOT nullptr" original.
    out << " Restricciones de la tabla 'punto':\n";
    q.exec(QStringLiteral("PRAGMA table_info(punto)"));
    while (q.next()) {
        out << "   " << q.value(1).toString().leftJustified(18)
            << q.value(2).toString().leftJustified(10)
            << (q.value(3).toInt() ? "NOT NULL " : "         ")
            << (q.value(5).toInt() ? "PRIMARY KEY" : "")
            << "\n";
    }

    out << "\n Claves foraneas declaradas:\n";
    for (const QString &t : tablas) {
        QSqlQuery fk(db);
        fk.exec(QStringLiteral("PRAGMA foreign_key_list(%1)").arg(t));
        while (fk.next())
            out << "   " << t << "." << fk.value(3).toString()
                << " -> " << fk.value(2).toString() << "."
                << fk.value(4).toString()
                << "  ON DELETE " << fk.value(6).toString() << "\n";
    }

    out << "\n==========================================================\n";
    out << " CONTENIDO\n";
    out << "==========================================================\n";

    const auto puntos = repo.loadPoints();
    out << " Puntos (" << puntos.size() << "):\n";
    for (const MapPoint &p : puntos)
        out << "   [" << p.id << "] " << p.name.leftJustified(24)
            << QString::number(p.position.latitude(), 'f', 4) << ", "
            << QString::number(p.position.longitude(), 'f', 4)
            << "   " << p.description << "\n";

    const auto vehiculos = repo.loadVehicles();
    out << "\n Vehiculos (" << vehiculos.size() << "):\n";
    for (const MapVehicle &v : vehiculos) {
        const char *clase = v.kind == VehicleKind::Aerial ? "aereo"
                          : v.kind == VehicleKind::Naval  ? "naval" : "terrestre";
        out << "   [" << v.id << "] " << v.name.leftJustified(20)
            << QString::fromLatin1(clase).leftJustified(11)
            << " rumbo " << v.heading << "  vel " << v.speed;
        if (v.ais.isValid())
            out << "   AIS mmsi=" << v.ais.mmsi
                << " eslora=" << v.ais.length << "m destino=" << v.ais.destination;
        else
            out << "   (sin AIS)";
        out << "\n";

        const auto traza = repo.loadTrack(v.id, 100000);
        if (!traza.isEmpty())
            out << "        trayectoria: " << traza.size() << " muestras, de "
                << QDateTime::fromMSecsSinceEpoch(traza.first().timeUtcMs)
                       .toString(Qt::ISODate)
                << " a "
                << QDateTime::fromMSecsSinceEpoch(traza.last().timeUtcMs)
                       .toString(Qt::ISODate)
                << "\n";
    }

    const auto poligonos = repo.loadPolygons();
    out << "\n Poligonos (" << poligonos.size() << "):\n";
    for (const MapPolygon &p : poligonos)
        out << "   [" << p.id << "] " << p.name << ", " << p.vertices.size()
            << " vertices, relleno alfa " << p.fillColor.alpha() << "\n";

    const auto rutas = repo.loadRoutes();
    out << "\n Rutas (" << rutas.size() << "):\n";
    for (const MapRoute &r : rutas) {
        out << "   [" << r.id << "] " << r.name << ", " << r.points.size()
            << " puntos\n";
        for (const MapRoutePoint &rp : r.points)
            out << "        " << rp.description.leftJustified(14)
                << QString::number(rp.position.latitude(), 'f', 4) << ", "
                << QString::number(rp.position.longitude(), 'f', 4)
                << "  radio " << rp.approachRadiusMeters << " m\n";
    }
    out << "\n";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser p;
    p.setApplicationDescription(
        QStringLiteral("Crea e inspecciona una base de datos vectorial"));
    p.addHelpOption();

    QCommandLineOption oOut(QStringLiteral("out"),
        QStringLiteral("Crear la BD en esta ruta (se borra si existe)"),
        QStringLiteral("ruta"));
    QCommandLineOption oFile(QStringLiteral("file"),
        QStringLiteral("Abrir una BD existente sin tocarla"),
        QStringLiteral("ruta"));
    QCommandLineOption oDump(QStringLiteral("dump"),
        QStringLiteral("Volcar esquema y contenido"));

    p.addOption(oOut);
    p.addOption(oFile);
    p.addOption(oDump);
    p.process(app);

    const bool creando = p.isSet(oOut);
    const QString ruta = creando ? p.value(oOut) : p.value(oFile);

    if (ruta.isEmpty()) {
        err << "Indica --out para crear o --file para inspeccionar.\n";
        return 2;
    }

    if (creando && QFile::exists(ruta))
        QFile::remove(ruta);

    VectorRepository repo;
    QObject::connect(&repo, &VectorRepository::errorOccurred,
                     [&err](const QString &ctx, const QString &msg) {
                         err << "  error en " << ctx << ": " << msg << "\n";
                     });

    if (!repo.open(ruta)) {
        err << "No se pudo abrir " << ruta << ": " << repo.lastError() << "\n";
        return 1;
    }

    out << " Base de datos: " << ruta << "\n\n";

    if (creando)
        poblar(repo, out);

    if (p.isSet(oDump) || creando)
        volcar(repo, ruta, out);

    SqliteConnectionPool::closeAllForCurrentThread();
    return 0;
}
