/*
 * dem_to_hillshade - hornea un RELIEVE SOMBREADO (hillshade) del DEM a una
 * pirámide de teselas PNG en un .sqlitedb (formato XYZ que lee la librería).
 *
 * A diferencia de la capa en vivo (que calcula el sombreado al vuelo y solo es
 * práctica al acercar), estas teselas se pintan instantáneas a CUALQUIER zoom
 * -incluida la vista de país entera- y no necesitan el DEM ni CPU en runtime:
 * viajan en el paquete (mapa.json) como una capa base más, 100% sin conexión.
 *
 * El sol y la exageración quedan FIJOS al hornear (cambiarlos = rehornear); para
 * ajustar el sol al vuelo está la capa en vivo de MapWidget.
 *
 * Uso:
 *   dem_to_hillshade --in <carpeta_hgt | dem.sqlitedb> --out relieve.sqlitedb
 *                    [--cuba | --bbox latN,lonO,latS,lonE]
 *                    [--minzoom 6 --maxzoom 13]
 *                    [--sun-az 315 --sun-alt 45 --exag 2 --contrast 2.2]
 *                    [--sea-level 0]   (cotas <= ese valor -> transparente)
 *                    [--id relieve --name "Relieve"] [--overwrite]
 *
 * En un servidor sin pantalla: QT_QPA_PLATFORM=offscreen (QImage necesita Gui).
 */

#include "dem/HgtElevation.h"
#include "dem/IElevationSource.h"
#include "dem/SqliteElevation.h"
#include "geo/TileMatrix.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QtMath>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace libmapa;

namespace {

// Crea la tabla 'tiles' (formato XYZ del lector de la librería, sin columna 's').
bool crearEsquema(QSqlDatabase &db, QString *error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tiles ("
            "  x INTEGER, y INTEGER, z INTEGER,"
            "  image BLOB, PRIMARY KEY (x, y, z))"))) {
        *error = q.lastError().text();
        return false;
    }
    return true;
}

} // namespace

// Punto de entrada: abre el DEM (carpeta .hgt o .sqlitedb), recorre la pirámide de
// teselas que cubre la bbox y, por cada tesela, calcula el sombreado por píxel
// (pendiente/orientación por diferencias centrales sobre una rejilla de 258×258 con
// halo de 1 px para que no haya costuras entre teselas), lo guarda como PNG gris en
// la BD y al final imprime la entrada lista para pegar en datasets.json / mapa.json.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QString in, out, id = QStringLiteral("relieve"), name;
    int minZ = 6, maxZ = 13;
    double sunAz = 315.0, sunAlt = 45.0, exag = 2.0, contrast = 2.2;
    double seaLevel = std::numeric_limits<double>::quiet_NaN();  // NaN = no enmascarar mar
    double latN = 90.0, lonW = 180.0, latS = -90.0, lonE = -180.0;
    bool haveBbox = false, overwrite = false;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &k = args.at(i);
        const auto val = [&]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (k == QLatin1String("--in")) in = val();
        else if (k == QLatin1String("--out")) out = val();
        else if (k == QLatin1String("--id")) id = val();
        else if (k == QLatin1String("--name")) name = val();
        else if (k == QLatin1String("--minzoom")) minZ = val().toInt();
        else if (k == QLatin1String("--maxzoom")) maxZ = val().toInt();
        else if (k == QLatin1String("--sun-az")) sunAz = val().toDouble();
        else if (k == QLatin1String("--sun-alt")) sunAlt = val().toDouble();
        else if (k == QLatin1String("--exag")) exag = val().toDouble();
        else if (k == QLatin1String("--contrast")) contrast = val().toDouble();
        else if (k == QLatin1String("--sea-level")) seaLevel = val().toDouble();
        else if (k == QLatin1String("--overwrite")) overwrite = true;
        else if (k == QLatin1String("--cuba")) {
            latN = 23.3; lonW = -85.0; latS = 19.7; lonE = -74.0; haveBbox = true;
        } else if (k == QLatin1String("--bbox")) {
            const QStringList p = val().split(QLatin1Char(','));
            if (p.size() == 4) {
                latN = p[0].toDouble(); lonW = p[1].toDouble();
                latS = p[2].toDouble(); lonE = p[3].toDouble();
                haveBbox = true;
            }
        } else { qWarning() << "Opcion desconocida:" << k; return 2; }
    }

    QTextStream cout(stdout);
    if (in.isEmpty() || out.isEmpty() || !haveBbox) {
        cout << "Uso: dem_to_hillshade --in <carpeta_hgt|dem.sqlitedb> --out relieve.sqlitedb\n"
                "                      (--cuba | --bbox latN,lonO,latS,lonE)\n"
                "                      [--minzoom 6 --maxzoom 13]\n"
                "                      [--sun-az 315 --sun-alt 45 --exag 2 --contrast 2.2]\n"
                "                      [--sea-level 0]\n"
                "                      [--id relieve --name \"Relieve\"] [--overwrite]\n";
        return 2;
    }
    if (name.isEmpty())
        name = QStringLiteral("Relieve sombreado");
    if (minZ > maxZ) std::swap(minZ, maxZ);

    // Origen de elevación: carpeta de .hgt o una BD .sqlitedb.
    std::unique_ptr<IElevationSource> dem;
    const QFileInfo fi(in);
    if (fi.isDir()) {
        auto h = std::make_unique<HgtElevation>();
        h->setDirectory(in);
        dem = std::move(h);
    } else {
        dem = std::make_unique<SqliteElevation>(in);
    }

    if (QFile::exists(out)) {
        if (!overwrite) {
            qCritical() << out << "ya existe (usa --overwrite para sobrescribir).";
            return 1;
        }
        QFile::remove(out);
    }

    // Sol (convención ESRI): cenit y azimut matemático.
    const double zenith = qDegreesToRadians(90.0 - qBound(1.0, sunAlt, 89.0));
    const double azm = qDegreesToRadians(360.0 - sunAz + 90.0);
    const double cz = std::cos(zenith), sz = std::sin(zenith);
    // El sombreado Lambert del terreno LLANO vale cz = sin(sunAlt): a 45° son
    // ~0.707, un gris claro. Como casi todo cae cerca de ese valor, el PNG crudo
    // salía lavado (poco contraste). Realzamos con un estirado tonal lineal que
    // ANCLA el llano a un gris claro fijo (0.72) y abre el rango 'contrast' veces
    // alrededor de él: las laderas en sombra se oscurecen y las soleadas aclaran,
    // sin salirse del gris. contrast=1 deja el sombreado casi tal cual; >1 realza.
    const double llano = qBound(0.05, cz, 0.95);  // sombreado del terreno plano
    const double grisLlano = 0.72;                // gris objetivo del llano
    const double realce = qMax(0.1, contrast);
    const int kTile = 256;
    const int G = kTile + 2;            // rejilla con halo de 1 px por lado

    qint64 total = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("hs"));
        db.setDatabaseName(out);
        if (!db.open()) {
            qCritical() << "No se pudo crear" << out << ":" << db.lastError().text();
            return 1;
        }
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA journal_mode=OFF"));
        pragma.exec(QStringLiteral("PRAGMA synchronous=OFF"));
        QString error;
        if (!crearEsquema(db, &error)) {
            qCritical() << "Esquema:" << error;
            return 1;
        }

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO tiles (x,y,z,image) VALUES (:x,:y,:z,:img)"));

        std::vector<double> zg(std::size_t(G) * std::size_t(G));

        for (int z = minZ; z <= maxZ; ++z) {
            const int nPerSide = TileMatrix::tilesPerSide(z);
            int tx0 = int(std::floor(TileMatrix::longitudeToTileX(lonW, z)));
            int tx1 = int(std::floor(TileMatrix::longitudeToTileX(lonE, z)));
            int ty0 = int(std::floor(TileMatrix::latitudeToTileY(latN, z)));  // lat alta -> y baja
            int ty1 = int(std::floor(TileMatrix::latitudeToTileY(latS, z)));
            tx0 = qBound(0, tx0, nPerSide - 1); tx1 = qBound(0, tx1, nPerSide - 1);
            ty0 = qBound(0, ty0, nPerSide - 1); ty1 = qBound(0, ty1, nPerSide - 1);

            QSqlQuery tx(db);
            tx.exec(QStringLiteral("BEGIN"));
            qint64 enZ = 0;

            for (int ty = ty0; ty <= ty1; ++ty) {
                // Metros por píxel (Web Mercator es conforme: igual en x e y) a la
                // latitud del centro de la fila de teselas.
                const double latMid = TileMatrix::tileYToLatitude(double(ty) + 0.5, z);
                const double mpp = (2.0 * M_PI * 6378137.0 * std::cos(qDegreesToRadians(latMid)))
                                   / (double(kTile) * double(nPerSide));
                const double paso = qMax(0.5, mpp);

                for (int tx_ = tx0; tx_ <= tx1; ++tx_) {
                    // Rejilla de cotas 258×258 (con halo) de esta tesela.
                    for (int gy = 0; gy < G; ++gy) {
                        const double fy = double(ty) + double(gy - 1) / double(kTile);
                        const double lat = TileMatrix::tileYToLatitude(fy, z);
                        for (int gx = 0; gx < G; ++gx) {
                            const double fx = double(tx_) + double(gx - 1) / double(kTile);
                            const double lon = TileMatrix::tileXToLongitude(fx, z);
                            zg[std::size_t(gy) * std::size_t(G) + std::size_t(gx)] =
                                dem->elevationAt(QGeoCoordinate(lat, lon));
                        }
                    }

                    QImage img(kTile, kTile, QImage::Format_ARGB32_Premultiplied);
                    img.fill(Qt::transparent);
                    bool any = false;
                    auto eg = [&](int i, int j) {   // cota en el píxel de salida (i,j)
                        return zg[std::size_t(j + 1) * std::size_t(G) + std::size_t(i + 1)];
                    };
                    for (int j = 0; j < kTile; ++j) {
                        QRgb *fila = reinterpret_cast<QRgb *>(img.scanLine(j));
                        for (int i = 0; i < kTile; ++i) {
                            const double zc = eg(i, j);
                            if (std::isnan(zc)) continue;          // sin dato -> transparente
                            // Mar: con --sea-level, toda cota <= ese valor se deja
                            // transparente (en DEM con batimetria el fondo marino
                            // tiene cotas negativas; asi no se sombrea y la costa
                            // queda limpia, dejando ver la capa base de debajo).
                            if (!std::isnan(seaLevel) && zc <= seaLevel) continue;
                            auto nz = [&](int ii, int jj) { const double v = eg(ii, jj); return std::isnan(v) ? zc : v; };
                            const double dzdx = (nz(i + 1, j) - nz(i - 1, j)) / (2.0 * paso) * exag;
                            const double dzdy = (nz(i, j - 1) - nz(i, j + 1)) / (2.0 * paso) * exag;  // j-1 = norte
                            const double slope = std::atan(std::sqrt(dzdx * dzdx + dzdy * dzdy));
                            const double aspect = std::atan2(dzdy, -dzdx);
                            double hs = cz * std::cos(slope) + sz * std::sin(slope) * std::cos(azm - aspect);
                            hs = qBound(0.0, hs, 1.0);
                            // Estirado tonal: llano -> grisLlano; el resto se abre
                            // 'realce' veces alrededor del llano y se recorta a [0,1].
                            const double t = qBound(0.0, grisLlano + (hs - llano) * realce, 1.0);
                            const int v = int(t * 255.0 + 0.5);
                            fila[i] = qRgba(v, v, v, 255);
                            any = true;
                        }
                    }
                    if (!any) continue;                             // tesela toda sin dato (mar)

                    QByteArray png;
                    QBuffer buf(&png);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    ins.bindValue(QStringLiteral(":x"), tx_);
                    ins.bindValue(QStringLiteral(":y"), ty);
                    ins.bindValue(QStringLiteral(":z"), z);
                    ins.bindValue(QStringLiteral(":img"), png);
                    if (!ins.exec()) {
                        qCritical() << "INSERT:" << ins.lastError().text();
                        return 1;
                    }
                    ++enZ;
                }
            }
            tx.exec(QStringLiteral("COMMIT"));
            total += enZ;
            qInfo().noquote() << QStringLiteral("  z=%1: %2 teselas (%3x%4 celdas)")
                                     .arg(z).arg(enZ).arg(tx1 - tx0 + 1).arg(ty1 - ty0 + 1);
        }

        qInfo().noquote() << QStringLiteral("Total: %1 teselas en %2").arg(total).arg(out);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("hs"));

    // Bloque listo para pegar en el array "datasets" del mapa.json / datasets.json.
    cout << "\n--- Anade esto al array \"datasets\" de tu mapa.json ---\n";
    cout << QStringLiteral(
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
            .arg(id, name, QFileInfo(out).fileName())
            .arg(minZ).arg(maxZ);
    return 0;
}
