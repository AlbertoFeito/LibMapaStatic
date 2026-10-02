/*!
 * fill_tiles - rellena los huecos de una base de teselas SQLite descargando las
 * teselas que faltan de una fuente XYZ sin clave (por defecto Esri World
 * Imagery, satelite).
 *
 * Es la interfaz de CONSOLA sobre el motor comun TileFiller (tools/common):
 * el mismo motor que usa la version con ventana y mapa, asi que la codificacion
 * (columna z con zFactor/zOffset, esquema XYZ/TMS, columna s) es identica.
 *
 * Uso tipico (rellenar la satelital sobre Cuba, z6..z12):
 *   fill_tiles --datasets datasets.json --id satelital \
 *              --bbox 23.3,-85.0,19.7,-74.0 --minzoom 6 --maxzoom 12
 *
 * Fuente por defecto (sin API key), Esri "Clarity" (mas clara, casa con Google):
 *   https://clarity.maptiles.arcgis.com/arcgis/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}
 *
 * AVISO: respeta los terminos de uso de la fuente que utilices. La descarga
 * masiva desde servidores publicos suele estar limitada o prohibida.
 */

#include "TileFiller.h"
#include "tiles/TileDataset.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QSslSocket>
#include <QGeoCoordinate>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <iostream>
#include <string>

using namespace libmapa;

namespace {

// Flujos de salida estandar y de error, envueltos en QTextStream (una sola
// instancia cada uno) para escribir texto Unicode comodamente.
QTextStream &cout() { static QTextStream s(stdout); return s; }
QTextStream &cerr() { static QTextStream s(stderr); return s; }

//! Lee un datasets.json y devuelve el dataset con ese id (misma lectura que la
//! libreria: TileDataset::fromJson, con ruta relativa resuelta junto al JSON).
bool loadDataset(const QString &jsonPath, const QString &id,
                 TileDataset *out, QString *err)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *err = QStringLiteral("No se pudo abrir %1").arg(jsonPath);
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (doc.isNull()) {
        *err = QStringLiteral("JSON invalido: %1").arg(pe.errorString());
        return false;
    }
    const QDir baseDir = QFileInfo(jsonPath).absoluteDir();
    const QJsonArray arr = doc.object().value(QStringLiteral("datasets")).toArray();
    for (const QJsonValue &v : arr) {
        TileDataset d = TileDataset::fromJson(v.toObject());
        if (d.id != id)
            continue;
        if (!d.filePath.isEmpty() && QFileInfo(d.filePath).isRelative())
            d.filePath = baseDir.absoluteFilePath(d.filePath);
        if (!d.isValid()) {
            *err = QStringLiteral("El dataset '%1' no es valido").arg(id);
            return false;
        }
        *out = d;
        return true;
    }
    *err = QStringLiteral("No hay un dataset con id '%1' en %2").arg(id, jsonPath);
    return false;
}

} // namespace

// Punto de entrada de la herramienta de consola. Flujo: (1) diagnostico TLS;
// (2) parsea los argumentos (--datasets/--id sobre una base existente, o --new
// para crear una base nueva con codificacion limpia); (3) parsea el bbox y arma
// los Params (dataset + zoom + fuente + ritmo/auto-freno); (4) prepare() calcula
// cuanto falta SIN red y lo muestra pidiendo confirmacion (salvo --yes);
// (5) conecta las senales del TileFiller a la salida por consola (barra de
// progreso, fin de nivel, fallos, auto-freno, resumen final) y arranca el bucle de
// eventos. Codigo de salida: 0 ok, 1 error de preparacion, 2 uso, 3 hubo fallos.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    // Diagnostico TLS: sin soporte SSL, TODA descarga HTTPS falla (en Windows
    // suele faltar OpenSSL: libssl-3-x64.dll / libcrypto-3-x64.dll junto al exe).
    if (!QSslSocket::supportsSsl()) {
        cerr() << "AVISO: este Qt NO tiene soporte TLS/SSL, las descargas HTTPS "
                  "van a fallar.\n"
                  "  Qt esperaba: " << QSslSocket::sslLibraryBuildVersionString()
               << "\n  En Windows: copia libssl-3-x64.dll y libcrypto-3-x64.dll "
                  "(OpenSSL 3, 64-bit) junto al .exe o en el PATH.\n";
    }

    QString datasetsPath, id, bbox, poly, newFile, name;
    TileFiller::Params p;
    // Por defecto Esri "Clarity": misma imagen satelital sin clave, pero mas
    // clara y viva que la "World_Imagery" normal -casa mejor con las bases de
    // Google-. Se puede cambiar con --url. (Alternativa mas oscura:
    //  https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x})
    p.url = QStringLiteral(
        "https://clarity.maptiles.arcgis.com/arcgis/rest/services/"
        "World_Imagery/MapServer/tile/{z}/{y}/{x}");
    int minZoom = -1, maxZoom = -1;
    bool assumeYes = false;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &k = args.at(i);
        const auto val = [&]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (k == QLatin1String("--datasets")) datasetsPath = val();
        else if (k == QLatin1String("--id")) id = val();
        else if (k == QLatin1String("--new")) newFile = val();
        else if (k == QLatin1String("--name")) name = val();
        else if (k == QLatin1String("--bbox")) bbox = val();
        else if (k == QLatin1String("--poly")) poly = val();
        else if (k == QLatin1String("--url")) p.url = val();
        else if (k == QLatin1String("--minzoom")) minZoom = val().toInt();
        else if (k == QLatin1String("--maxzoom")) maxZoom = val().toInt();
        else if (k == QLatin1String("--rate")) p.rate = val().toDouble();
        else if (k == QLatin1String("--conns")) p.connections = val().toInt();
        else if (k == QLatin1String("--retries")) p.retries = val().toInt();
        else if (k == QLatin1String("--timeout")) p.timeoutMs = val().toInt();
        else if (k == QLatin1String("--user-agent")) p.userAgent = val().toUtf8();
        else if (k == QLatin1String("--overwrite")) p.overwrite = true;
        else if (k == QLatin1String("--only-missing")) p.overwrite = false;
        else if (k == QLatin1String("--yes") || k == QLatin1String("-y")) assumeYes = true;
        else { cerr() << "Opcion desconocida: " << k << '\n'; return 2; }
    }

    const bool modoNuevo = !newFile.isEmpty();
    // Hace falta zona (bbox O poly) y, si no es base nueva, datasets+id.
    const bool faltanArgs = (bbox.isEmpty() && poly.isEmpty())
        || (modoNuevo ? false : (datasetsPath.isEmpty() || id.isEmpty()));
    if (faltanArgs) {
        cout() << "Uso (rellenar una base existente):\n"
                  "  fill_tiles --datasets datasets.json --id satelital \\\n"
                  "             --bbox latN,lonO,latS,lonE [--minzoom N --maxzoom N]\n\n"
                  "Uso (crear una base NUEVA, toda de la fuente elegida):\n"
                  "  fill_tiles --new Cuba_Clarity.sqlitedb [--id clarity] [--name \"...\"] \\\n"
                  "             --bbox latN,lonO,latS,lonE --minzoom 0 --maxzoom 18\n\n"
                  "  Zona: --bbox latN,lonO,latS,lonE  o bien\n"
                  "        --poly \"lat,lon;lat,lon;lat,lon[;...]\" (solo baja dentro del poligono)\n"
                  "  Comun: [--url \"...{z}/{y}/{x}...\"] (def. Esri Clarity, sin clave)\n"
                  "         [--only-missing (def) | --overwrite]\n"
                  "         [--rate 2] [--conns 2] [--retries 3] [--timeout 20000] [--yes]\n"
                  "         (--rate = lanzamientos/seg; --conns = peticiones en vuelo a la vez)\n\n"
                  "AVISO: respeta los terminos de uso de la fuente que utilices.\n";
        return 2;
    }

    if (!poly.isEmpty()) {
        // "lat,lon;lat,lon;..." -> vertices. El bbox lo calcula TileFiller.
        const QStringList verts = poly.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &v : verts) {
            const QStringList c = v.split(QLatin1Char(','));
            if (c.size() != 2) { cerr() << "poly: cada vertice es lat,lon\n"; return 2; }
            p.polygon.append(QGeoCoordinate(c.at(0).trimmed().toDouble(),
                                            c.at(1).trimmed().toDouble()));
        }
        if (p.polygon.size() < 3) { cerr() << "poly necesita >=3 vertices\n"; return 2; }
    } else {
        const QStringList bp = bbox.split(QLatin1Char(','));
        if (bp.size() != 4) { cerr() << "bbox debe ser latN,lonO,latS,lonE\n"; return 2; }
        p.latN = bp.at(0).trimmed().toDouble();
        p.lonW = bp.at(1).trimmed().toDouble();
        p.latS = bp.at(2).trimmed().toDouble();
        p.lonE = bp.at(3).trimmed().toDouble();
    }

    QString err;
    if (modoNuevo) {
        // Base NUEVA con una codificacion LIMPIA (no la rara de Google):
        // esquema XYZ, z guardado = z logico, sin columna 's'. El motor crea la
        // tabla si el fichero no existe; si ya existe, solo baja lo que falta
        // (reanudable).
        p.ds = TileDataset{};
        p.ds.id = id.isEmpty() ? QStringLiteral("clarity") : id;
        p.ds.displayName = name.isEmpty()
            ? QStringLiteral("Satelital (Esri Clarity)") : name;
        p.ds.filePath = newFile;
        p.ds.tableName = QStringLiteral("tiles");
        p.ds.zFactor = 1; p.ds.zOffset = 0;
        p.ds.scheme = TileScheme::XYZ;
        p.ds.hasSColumn = false;
        p.ds.tileSize = 256;
        p.ds.colX = QStringLiteral("x");
        p.ds.colY = QStringLiteral("y");
        p.ds.colZ = QStringLiteral("z");
        p.ds.colImage = QStringLiteral("image");
        p.ds.minZoom = minZoom < 0 ? 0 : minZoom;
        p.ds.maxZoom = maxZoom < 0 ? 18 : maxZoom;
        p.ds.recommendedMaxZoom = p.ds.maxZoom;
        p.ds.baseZoom = p.ds.minZoom;
        p.createSchema = true;
    } else {
        if (!loadDataset(datasetsPath, id, &p.ds, &err)) { cerr() << err << '\n'; return 1; }
        if (!QFileInfo::exists(p.ds.filePath)) {
            cerr() << "No existe la BD del dataset: " << p.ds.filePath << '\n';
            return 1;
        }
    }

    p.minZoom = minZoom < 0 ? p.ds.minZoom : minZoom;
    p.maxZoom = maxZoom < 0 ? p.ds.maxZoom : maxZoom;
    p.minZoom = qBound(0, p.minZoom, 22);
    p.maxZoom = qBound(p.minZoom, p.maxZoom, 22);

    TileFiller filler;
    if (!filler.prepare(p, &err)) { cerr() << err << '\n'; return 1; }

    cout() << "Dataset '" << p.ds.id << "'  BD: " << p.ds.filePath << '\n';
    cout() << "Esquema " << tileSchemeToString(p.ds.scheme)
           << "  storedZ = " << p.ds.zFactor << "*z + " << p.ds.zOffset
           << (p.ds.hasSColumn ? QStringLiteral("  s=%1").arg(p.ds.sValue) : QString())
           << '\n';
    if (!p.polygon.isEmpty())
        cout() << "Poligono de " << p.polygon.size() << " vertices"
               << "  zoom " << p.minZoom << ".." << p.maxZoom << '\n';
    else
        cout() << "BBox latN=" << p.latN << " lonO=" << p.lonW
               << " latS=" << p.latS << " lonE=" << p.lonE
               << "  zoom " << p.minZoom << ".." << p.maxZoom << '\n';
    const auto porZoom = filler.perZoomMissing();
    for (const auto &pz : porZoom)
        cout() << "  z=" << pz.first << ": "
               << (p.overwrite ? QStringLiteral("reescribir") : QString::number(pz.second) + " faltan")
               << '\n';
    cout() << "TOTAL a descargar: " << filler.totalToDownload()
           << " teselas (de " << filler.totalCells() << " en la zona)\n";
    cout().flush();

    if (filler.totalToDownload() == 0) {
        cout() << "No falta ninguna tesela en esa zona y zoom. Nada que hacer.\n";
        return 0;
    }

    if (!assumeYes) {
        cout() << "Descargar " << filler.totalToDownload() << " teselas de "
               << p.url << " ? [s/N] ";
        cout().flush();
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "s" && resp != "S" && resp != "y" && resp != "Y") {
            cout() << "Cancelado.\n";
            return 0;
        }
    }

    // Progreso: una linea que se refresca; se limita a ~2 por segundo para no
    // inundar la consola.
    QElapsedTimer lastPrint; lastPrint.start();
    QObject::connect(&filler, &TileFiller::progress, &app,
                     [&lastPrint](qint64 done, qint64 total, double tps) {
        if (lastPrint.elapsed() < 500 && done < total)
            return;
        lastPrint.restart();
        const double pct = total > 0 ? 100.0 * double(done) / double(total) : 0.0;
        const double restanSeg = tps > 0 ? double(total - done) / tps : 0.0;
        cout() << QStringLiteral("\r  %1/%2 (%3%)  %4 t/s  ~%5 min restantes    ")
                      .arg(done).arg(total)
                      .arg(pct, 0, 'f', 1).arg(tps, 0, 'f', 1)
                      .arg(restanSeg / 60.0, 0, 'f', 1);
        cout().flush();
    });
    QObject::connect(&filler, &TileFiller::zoomFinished, &app,
                     [](int z, qint64 added) {
        cout() << QStringLiteral("\r  z=%1: +%2 teselas nuevas                    \n")
                      .arg(z).arg(added);
        cout().flush();
    });
    // Motivo real de los fallos (SSL, host, timeout...). Se imprimen los
    // primeros para no inundar, pero bastan para saber que pasa.
    int msgs = 0;
    QObject::connect(&filler, &TileFiller::message, &app,
                     [&msgs](const QString &texto) {
        if (++msgs <= 15) {
            cerr() << "\n  " << texto << '\n';
            if (msgs == 15)
                cerr() << "  (mas fallos; se omiten los siguientes avisos)\n";
        }
    });
    // Auto-freno: la fuente esta limitando; se pausa y reanuda solo.
    QObject::connect(&filler, &TileFiller::throttling, &app,
                     [](int pauseSec, qint64 racha) {
        cout() << QStringLiteral(
                      "\n  [auto-freno] %1 fallos seguidos: la fuente parece "
                      "limitar. Pauso %2 s y reanudo...\n")
                      .arg(racha).arg(pauseSec);
        cout().flush();
    });
    int exitCode = 0;
    QObject::connect(&filler, &TileFiller::finished, &app,
                     [&app, &exitCode, modoNuevo, &p](const TileFiller::Stats &s, bool cancelled) {
        cout() << QStringLiteral("\n%1 Descargadas: %2  ya existian: -  "
                                 "sin origen(404): %3  fallidas: %4\n")
                      .arg(cancelled ? QStringLiteral("Cancelado.") : QStringLiteral("Listo."))
                      .arg(s.downloaded).arg(s.notFound).arg(s.failed);
        // Base nueva: imprime la entrada lista para pegar en datasets.json.
        if (modoNuevo) {
            cout() << "\n--- Anade esto al array \"datasets\" de tu datasets.json ---\n";
            cout() << QStringLiteral(
                "    {\n"
                "      \"id\": \"%1\",\n"
                "      \"displayName\": \"%2\",\n"
                "      \"filePath\": \"%3\",\n"
                "      \"tableName\": \"tiles\",\n"
                "      \"zFactor\": 1, \"zOffset\": 0,\n"
                "      \"minZoom\": %4, \"maxZoom\": %5, \"recommendedMaxZoom\": %5,\n"
                "      \"typicalFill\": 1.0, \"scheme\": \"XYZ\",\n"
                "      \"hasSColumn\": false, \"tileSize\": 256,\n"
                "      \"colZ\": \"z\", \"colX\": \"x\", \"colY\": \"y\",\n"
                "      \"colImage\": \"image\", \"baseZoom\": %4\n"
                "    }\n")
                .arg(p.ds.id, p.ds.displayName, QFileInfo(p.ds.filePath).fileName())
                .arg(p.minZoom).arg(p.maxZoom);
        }
        exitCode = s.failed > 0 ? 3 : 0;
        app.quit();
    });

    QTimer::singleShot(0, &filler, &TileFiller::start);
    app.exec();
    return exitCode;
}
