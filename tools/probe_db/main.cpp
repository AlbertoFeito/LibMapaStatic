/*!
 * probe_db - Sonda las BD de teselas y genera datasets.json
 *
 * Uso:
 *   probe_db --id osm       --file Recursos/Cuba_OSM_CID3.sqlitedb \
 *            --id satelital --file Recursos/Cuba_Satelital_CID3.sqlitedb \
 *            --ref-bbox 23.3,-85.0,19.7,-74.0 \
 *            --out datasets.json
 *
 * --ref-bbox es latNorte,lonOeste,latSur,lonEste. Sirve para desempatar
 * XYZ vs TMS. Para Cuba: 23.3,-85.0,19.7,-74.0
 *
 * Las rutas se escriben RELATIVAS a la carpeta del --out, para que el JSON se
 * pueda mover junto a los datos (a otro PC, o junto a la aplicacion).
 *
 * Con --package genera el manifiesto completo de un paquete de datos (mapa.json,
 * version 2): ademas de los datasets, el bloque "package" (id, nombre, fecha,
 * zona), "start", y opcionalmente la elevacion (--dem) y capas fijas
 * (--overlay, repetible):
 *
 *   probe_db --package --out Recursos/mapa.json \
 *            --id osm --file Recursos/Cuba_OSM_CID3.sqlitedb ... \
 *            --dem Recursos/cuba_dem.sqlitedb --overlay Recursos/Aguas.geo \
 *            --ref-bbox 23.3,-85.0,19.7,-74.0
 */

#include "tiles/TileDatasetProbe.h"
#include "db/SqliteConnectionPool.h"
#include "tiles/RMapsTileSource.h"
#include "geo/TileMatrix.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGeoRectangle>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace libmapa;

// Parsea un bbox "latN,lonO,latS,lonE" a un QGeoRectangle. Deja *ok en false si el
// texto no tiene 4 numeros validos. Es el rectangulo de referencia con el que la
// sonda desempata el esquema XYZ vs TMS.
static QGeoRectangle parseBBox(const QString &s, bool *ok)
{
    *ok = false;
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() != 4)
        return {};

    bool a = false, b = false, c = false, d = false;
    const double latN = parts[0].toDouble(&a);
    const double lonW = parts[1].toDouble(&b);
    const double latS = parts[2].toDouble(&c);
    const double lonE = parts[3].toDouble(&d);
    if (!a || !b || !c || !d)
        return {};

    *ok = true;
    return QGeoRectangle(QGeoCoordinate(latN, lonW), QGeoCoordinate(latS, lonE));
}

// Ruta de 'file' relativa a la carpeta 'base' (la del JSON de salida), con '/'
// como separador en cualquier sistema: asi el JSON vale tal cual en otro PC.
static QString relativeTo(const QDir &base, const QString &file)
{
    return QDir::fromNativeSeparators(
        base.relativeFilePath(QFileInfo(file).absoluteFilePath()));
}

// Bloque "package" + "start" del manifiesto v2, deducido de lo que se sabe: el
// id y el nombre salen de la carpeta del paquete, la fecha es hoy, la zona es el
// --ref-bbox (o, si no hay, la union de lo que cubren los datasets) y el
// arranque es el centro de esa zona con el primer dataset. Todo es editable
// despues a mano; la idea es no partir de cero.
static void addPackageBlocks(QJsonObject &root, const QDir &base,
                             const QGeoRectangle &zona, const TileDataset &primero)
{
    const QString carpeta = base.dirName();

    QJsonObject pkg;
    pkg[QStringLiteral("id")] = carpeta.toLower();
    pkg[QStringLiteral("name")] = carpeta;
    pkg[QStringLiteral("dataVersion")] = QDate::currentDate().toString(QStringLiteral("yyyy.MM"));
    pkg[QStringLiteral("created")] = QDate::currentDate().toString(Qt::ISODate);
    pkg[QStringLiteral("description")] = QString();
    pkg[QStringLiteral("attribution")] = QString();
    if (zona.isValid()) {
        pkg[QStringLiteral("bounds")] = QJsonObject{
            {QStringLiteral("north"), zona.topLeft().latitude()},
            {QStringLiteral("west"), zona.topLeft().longitude()},
            {QStringLiteral("south"), zona.bottomRight().latitude()},
            {QStringLiteral("east"), zona.bottomRight().longitude()}};
    }
    root[QStringLiteral("package")] = pkg;

    QJsonObject start;
    start[QStringLiteral("layer")] = primero.id;
    if (zona.isValid())
        start[QStringLiteral("center")] = QJsonArray{zona.center().latitude(),
                                                     zona.center().longitude()};
    // Zoom 7 encuadra un pais como Cuba; acotado al rango util del dataset.
    start[QStringLiteral("zoom")] = qBound(primero.minZoom, 7, primero.recommendedMaxZoom);
    root[QStringLiteral("start")] = start;
}

//! Comprueba que la fuente sabe leer de verdad, no solo que la sonda acerto.
static void smokeTest(QTextStream &out, const TileDataset &ds,
                      const ProbeResult &r)
{
    if (r.levels.isEmpty())
        return;

    out << " VERIFICACION DE LECTURA\n";

    RMapsTileSource src(ds);
    if (!src.open()) {
        out << "   FALLO al abrir la fuente: " << src.lastError() << "\n\n";
        return;
    }

    // Se prueba en un nivel intermedio, con una rejilla pequena centrada en
    // el area realmente cubierta.
    const ZoomLevelStats &lvl = r.levels.at(r.levels.size() / 2);
    const int z = lvl.logicalZ;

    const int cx = (lvl.xMin + lvl.xMax) / 2;
    // yMin/yMax vienen en coordenadas ALMACENADAS: hay que pasarlas a logicas.
    const int ly1 = TileMatrix::fromStorageY(lvl.yMin, z, ds.scheme);
    const int ly2 = TileMatrix::fromStorageY(lvl.yMax, z, ds.scheme);
    const int cy  = (ly1 + ly2) / 2;

    const int x1 = qMax(0, cx - 2), x2 = cx + 2;
    const int y1 = qMax(0, cy - 2), y2 = cy + 2;
    const int asked = (x2 - x1 + 1) * (y2 - y1 + 1);

    QElapsedTimer timer;
    timer.start();
    const auto tiles = const_cast<RMapsTileSource &>(src).fetchRange(z, x1, x2, y1, y2);
    const qint64 elapsed = timer.elapsed();

    out << "   z=" << z << "  rejilla x[" << x1 << ".." << x2
        << "] y[" << y1 << ".." << y2 << "]\n";
    out << "   pedidas " << asked << ", obtenidas " << tiles.size()
        << " en " << elapsed << " ms\n";

    qint64 bytes = 0;
    for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it)
        bytes += it.value().size();
    if (!tiles.isEmpty()) {
        out << "   " << QString::number(static_cast<double>(bytes) / 1024.0, 'f', 1)
            << " KiB, media "
            << QString::number(static_cast<double>(bytes)
                                   / static_cast<double>(tiles.size()) / 1024.0,
                               'f', 1)
            << " KiB por tesela\n";
    }

    // Segunda pasada: mide el efecto del page cache de SQLite.
    timer.restart();
    const auto again = const_cast<RMapsTileSource &>(src).fetchRange(z, x1, x2, y1, y2);
    out << "   segunda lectura (cache SQLite caliente): " << timer.elapsed()
        << " ms, " << again.size() << " teselas\n";

    if (tiles.isEmpty()) {
        out << "   *** No se obtuvo ninguna tesela. El mapeo de zoom o el\n"
               "       esquema de Y podrian ser incorrectos. ***\n";
    }
    out << "\n";
}

// Punto de entrada: parsea los pares --id/--file (y el --ref-bbox opcional),
// sonda cada BD con TileDatasetProbe para deducir su codificacion, imprime el
// informe y (salvo --no-test) verifica que la fuente lee de verdad, y finalmente
// vuelca todos los datasets a un datasets.json. Devuelve 1 si alguna sonda fallo.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("probe_db"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Sonda las BD de teselas y genera datasets.json"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption optId(QStringLiteral("id"),
        QStringLiteral("Identificador del dataset (repetible)."),
        QStringLiteral("id"));
    QCommandLineOption optFile(QStringLiteral("file"),
        QStringLiteral("Ruta al .sqlitedb (repetible, en el mismo orden que --id)."),
        QStringLiteral("ruta"));
    QCommandLineOption optName(QStringLiteral("name"),
        QStringLiteral("Nombre visible (repetible, opcional)."),
        QStringLiteral("nombre"));
    QCommandLineOption optBBox(QStringLiteral("ref-bbox"),
        QStringLiteral("latN,lonO,latS,lonE de referencia para desempatar XYZ/TMS."),
        QStringLiteral("bbox"));
    QCommandLineOption optOut(QStringLiteral("out"),
        QStringLiteral("Fichero JSON de salida."),
        QStringLiteral("ruta"), QStringLiteral("datasets.json"));
    QCommandLineOption optNoTest(QStringLiteral("no-test"),
        QStringLiteral("Omitir la verificacion de lectura."));
    QCommandLineOption optPackage(QStringLiteral("package"),
        QStringLiteral("Generar el manifiesto completo de paquete (mapa.json, version 2)."));
    QCommandLineOption optDem(QStringLiteral("dem"),
        QStringLiteral("Con --package: BD de elevacion (.sqlitedb) o carpeta de .hgt."),
        QStringLiteral("ruta"));
    QCommandLineOption optOverlay(QStringLiteral("overlay"),
        QStringLiteral("Con --package: capa fija .geo (repetible)."),
        QStringLiteral("fichero.geo"));
    QCommandLineOption optFeatures(QStringLiteral("features"),
        QStringLiteral("Con --package: nombre de la BD de entidades del usuario."),
        QStringLiteral("nombre"), QStringLiteral("entidades.db"));

    parser.addOption(optId);
    parser.addOption(optFile);
    parser.addOption(optName);
    parser.addOption(optBBox);
    parser.addOption(optOut);
    parser.addOption(optNoTest);
    parser.addOption(optPackage);
    parser.addOption(optDem);
    parser.addOption(optOverlay);
    parser.addOption(optFeatures);
    parser.process(app);

    // Las rutas del JSON se escriben relativas a SU carpeta.
    const QString outPath = parser.value(optOut);
    const QDir outDir = QFileInfo(outPath).absoluteDir();

    const QStringList ids   = parser.values(optId);
    const QStringList files = parser.values(optFile);
    const QStringList names = parser.values(optName);

    if (ids.isEmpty() || ids.size() != files.size()) {
        err << "Hay que pasar --id y --file el mismo numero de veces.\n";
        err << "Ejemplo:\n"
               "  probe_db --id osm --file Recursos/Cuba_OSM_CID3.sqlitedb \\\n"
               "           --id satelital --file Recursos/Cuba_Satelital_CID3.sqlitedb \\\n"
               "           --ref-bbox 23.3,-85.0,19.7,-74.0\n";
        return 2;
    }

    QGeoRectangle reference;
    if (parser.isSet(optBBox)) {
        bool ok = false;
        reference = parseBBox(parser.value(optBBox), &ok);
        if (!ok) {
            err << "--ref-bbox mal formado. Formato: latN,lonO,latS,lonE\n";
            return 2;
        }
    } else {
        out << "AVISO: sin --ref-bbox no se puede distinguir XYZ de TMS con\n"
               "       certeza. Para Cuba usa: --ref-bbox 23.3,-85.0,19.7,-74.0\n\n";
    }

    QJsonArray array;
    int failures = 0;
    TileDataset primero;            // para el bloque "start" del paquete
    QGeoRectangle cubierto;         // union de lo que cubren los datasets

    for (int i = 0; i < ids.size(); ++i) {
        const QString id = ids.at(i);
        const QString file = files.at(i);

        auto result = TileDatasetProbe::probe(file, id, reference);
        if (!result) {
            err << "FALLO al sondear " << id << " (" << file << ")\n\n";
            ++failures;
            continue;
        }

        if (i < names.size())
            result->dataset.displayName = names.at(i);

        out << TileDatasetProbe::formatReport(*result);

        if (!parser.isSet(optNoTest))
            smokeTest(out, result->dataset, *result);

        if (array.isEmpty())
            primero = result->dataset;
        if (result->coverage.isValid())
            cubierto = cubierto.isValid() ? cubierto.united(result->coverage)
                                          : result->coverage;

        TileDataset ds = result->dataset;
        ds.filePath = relativeTo(outDir, ds.filePath);
        array.append(ds.toJson());
    }

    if (!array.isEmpty()) {
        QJsonObject root;
        if (parser.isSet(optPackage)) {
            root[QStringLiteral("format")] = QStringLiteral("libmapa-package");
            root[QStringLiteral("version")] = 2;
            addPackageBlocks(root, outDir, reference.isValid() ? reference : cubierto,
                             primero);

            if (parser.isSet(optDem)) {
                const QString dem = parser.value(optDem);
                const QString clave = QFileInfo(dem).isDir() ? QStringLiteral("dir")
                                                             : QStringLiteral("file");
                root[QStringLiteral("elevation")] =
                    QJsonObject{{clave, relativeTo(outDir, dem)}};
            }

            QJsonArray capas;
            for (const QString &geo : parser.values(optOverlay)) {
                const QString base = QFileInfo(geo).completeBaseName();
                capas.append(QJsonObject{
                    {QStringLiteral("id"), base.toLower()},
                    {QStringLiteral("name"), base},
                    {QStringLiteral("file"), relativeTo(outDir, geo)}});
            }
            if (!capas.isEmpty())
                root[QStringLiteral("overlays")] = capas;

            root[QStringLiteral("features")] =
                QJsonObject{{QStringLiteral("file"), parser.value(optFeatures)}};
        } else {
            root[QStringLiteral("version")] = 1;
        }
        root[QStringLiteral("datasets")] = array;

        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            err << "No se pudo escribir " << outPath << "\n";
            return 1;
        }
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
        out << "Escrito " << outPath << " con " << array.size() << " dataset(s)"
            << (parser.isSet(optPackage) ? " (paquete de datos)" : "") << ".\n";
    }

    SqliteConnectionPool::closeAllForCurrentThread();
    return failures > 0 ? 1 : 0;
}
