/*!
 * geo_to_tiles - rasteriza datos vectoriales (.geo o .xyz) a un pirámide de
 * teselas SQLite, para servirlos como una capa base rapida (igual que OSM o el
 * satelital) en vez de dibujar cientos de miles de vertices en cada frame.
 *
 * Por que: un .geo de Cuba con ~380.000 vertices dibujado como entidad vector
 * hace que la aplicacion se arrastre. Convertido a teselas, el motor de mapa lo
 * sirve cacheado y solo pinta los 256x256 visibles.
 *
 * Uso:
 *   geo_to_tiles --in Cuba.geo --out Cuba_Vector.sqlitedb \
 *                --id costas --name "Costas de Cuba" \
 *                --minzoom 4 --maxzoom 11 [--color "#145374"] [--width 1.3] \
 *                [--fill] [--bg "#ffffff"]
 *
 * Acepta .geo (longitud,latitud) y .xyz (metros Web Mercator, EPSG:3857): el
 * orden es x,y por linea y "0.0,0.0" separa trazados. El formato se detecta por
 * la magnitud (|x| > 180 => metros).
 *
 * Al terminar imprime el bloque para pegar en datasets.json.
 */

#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QVector>
#include <QtMath>
#include <algorithm>
#include <cmath>

using namespace libmapa;

namespace {

struct Path {
    QVector<QGeoCoordinate> pts;
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    bool closed = false;
};

//! Lee un .geo/.xyz a trazados (0,0 separa). Convierte metros a lon/lat si hace
//! falta.
QVector<Path> leer(const QString &ruta, QString *error)
{
    QVector<Path> paths;
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("No se pudo abrir %1: %2").arg(ruta, f.errorString());
        return paths;
    }

    QTextStream in(&f);
    Path actual;
    const auto cerrar = [&] {
        if (actual.pts.isEmpty())
            return;
        actual.closed = actual.pts.size() >= 2
            && qFuzzyCompare(actual.pts.first().latitude() + 1.0,
                             actual.pts.last().latitude() + 1.0)
            && qFuzzyCompare(actual.pts.first().longitude() + 1.0,
                             actual.pts.last().longitude() + 1.0);
        paths.append(actual);
        actual = Path();
    };

    while (!in.atEnd()) {
        const QString linea = in.readLine().trimmed();
        if (linea.isEmpty())
            continue;
        const QStringList c = linea.split(QLatin1Char(','));
        if (c.size() < 2)
            continue;
        bool okx = false, oky = false;
        const double x = c.at(0).trimmed().toDouble(&okx);
        const double y = c.at(1).trimmed().toDouble(&oky);
        if (!okx || !oky)
            continue;
        if (x == 0.0 && y == 0.0) {      // separador de trazados
            cerrar();
            continue;
        }

        QGeoCoordinate coord;
        if (qAbs(x) > 180.0 || qAbs(y) > 90.0)        // metros Web Mercator
            coord = WebMercator::inverse(x, y);       // x=este, y=norte
        else
            coord = QGeoCoordinate(y, x);             // el fichero trae x=lon
        if (!coord.isValid())
            continue;

        actual.pts.append(coord);
        actual.minLat = qMin(actual.minLat, coord.latitude());
        actual.maxLat = qMax(actual.maxLat, coord.latitude());
        actual.minLon = qMin(actual.minLon, coord.longitude());
        actual.maxLon = qMax(actual.maxLon, coord.longitude());
    }
    cerrar();

    if (paths.isEmpty())
        *error = QStringLiteral("%1 no contiene trazados validos").arg(ruta);
    return paths;
}

bool crearEsquema(QSqlDatabase &db, QString *error)
{
    QSqlQuery q(db);
    // Formato compatible con el lector RMaps/XYZ de la libreria.
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tiles ("
            "  x INTEGER, y INTEGER, z INTEGER, s INTEGER DEFAULT 0,"
            "  image BLOB, PRIMARY KEY (x, y, z, s))"))) {
        *error = q.lastError().text();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // QGuiApplication: QImage + QPainter necesitan el modulo Gui inicializado.
    // En un servidor sin pantalla, exporta QT_QPA_PLATFORM=offscreen.
    QGuiApplication app(argc, argv);

    QString in, out, id = QStringLiteral("vector"), name;
    int minZ = 4, maxZ = 11;
    double width = 1.3;
    QColor color(0x14, 0x53, 0x74);
    QColor bg(Qt::transparent);
    bool fill = false;

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
        else if (k == QLatin1String("--width")) width = val().toDouble();
        else if (k == QLatin1String("--color")) color = QColor(val());
        else if (k == QLatin1String("--bg")) bg = QColor(val());
        else if (k == QLatin1String("--fill")) fill = true;
        else { qWarning() << "Opcion desconocida:" << k; return 2; }
    }

    QTextStream cout(stdout);
    if (in.isEmpty() || out.isEmpty()) {
        cout << "Uso: geo_to_tiles --in fichero.geo --out salida.sqlitedb"
                " --id costas --name \"Costas\" [--minzoom 4 --maxzoom 11]"
                " [--color #145374] [--width 1.3] [--fill] [--bg #ffffff]\n";
        return 2;
    }
    if (name.isEmpty())
        name = id;

    QString error;
    const QVector<Path> paths = leer(in, &error);
    if (paths.isEmpty()) {
        qCritical() << error;
        return 1;
    }

    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    qint64 nVert = 0;
    for (const Path &p : paths) {
        minLat = qMin(minLat, p.minLat); maxLat = qMax(maxLat, p.maxLat);
        minLon = qMin(minLon, p.minLon); maxLon = qMax(maxLon, p.maxLon);
        nVert += p.pts.size();
    }
    qInfo().noquote()
        << QStringLiteral("Leidos %1 trazados, %2 vertices. BBox lat[%3,%4] lon[%5,%6]")
                .arg(paths.size()).arg(nVert)
                .arg(minLat, 0, 'f', 3).arg(maxLat, 0, 'f', 3)
                .arg(minLon, 0, 'f', 3).arg(maxLon, 0, 'f', 3);

    QFile::remove(out);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("gen"));
        db.setDatabaseName(out);
        if (!db.open()) {
            qCritical() << "No se pudo crear" << out << ":" << db.lastError().text();
            return 1;
        }
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA journal_mode=OFF"));
        pragma.exec(QStringLiteral("PRAGMA synchronous=OFF"));
        if (!crearEsquema(db, &error)) {
            qCritical() << "Esquema:" << error;
            return 1;
        }

        const int kTile = 256;
        qint64 total = 0;
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO tiles (x,y,z,s,image) VALUES (:x,:y,:z,0,:img)"));

        for (int z = minZ; z <= maxZ; ++z) {
            // Rango de teselas que cubre la bbox en este zoom.
            const int nPerSide = TileMatrix::tilesPerSide(z);
            int tx0 = int(std::floor(TileMatrix::longitudeToTileX(minLon, z)));
            int tx1 = int(std::floor(TileMatrix::longitudeToTileX(maxLon, z)));
            int ty0 = int(std::floor(TileMatrix::latitudeToTileY(maxLat, z)));  // lat alta -> y baja
            int ty1 = int(std::floor(TileMatrix::latitudeToTileY(minLat, z)));
            tx0 = qBound(0, tx0, nPerSide - 1); tx1 = qBound(0, tx1, nPerSide - 1);
            ty0 = qBound(0, ty0, nPerSide - 1); ty1 = qBound(0, ty1, nPerSide - 1);

            // Proyeccion a pixeles globales UNA vez por zoom, con DECIMADO de
            // puntos sub-pixel (a bajo zoom, cientos de miles de vertices
            // colapsan a unos pocos visibles). Los segmentos se REPARTEN por
            // tesela (bucketing): asi un trazado gigante -como la costa entera-
            // solo aporta a cada tesela los segmentos que la cruzan, en vez de
            // redibujarse completo en todas.
            QVector<QPolygonF> gpx(paths.size());          // solo se usa con --fill
            QVector<QRectF> cajas(paths.size());
            QHash<qint64, QVector<QLineF>> buckets;
            const auto clave = [&](int tx, int ty) {
                return qint64(ty) * nPerSide + tx;
            };

            for (int i = 0; i < paths.size(); ++i) {
                const Path &p = paths[i];
                QPolygonF poly;
                poly.reserve(p.pts.size());
                for (const QGeoCoordinate &c : p.pts) {
                    const QPointF q(
                        TileMatrix::longitudeToTileX(c.longitude(), z) * kTile,
                        TileMatrix::latitudeToTileY(c.latitude(), z) * kTile);
                    if (poly.isEmpty()
                        || std::abs(q.x() - poly.last().x())
                             + std::abs(q.y() - poly.last().y()) > 0.5)
                        poly.append(q);
                }
                if (fill)
                    gpx[i] = poly;
                const double xa = TileMatrix::longitudeToTileX(p.minLon, z);
                const double xb = TileMatrix::longitudeToTileX(p.maxLon, z);
                const double ya = TileMatrix::latitudeToTileY(p.maxLat, z);
                const double yb = TileMatrix::latitudeToTileY(p.minLat, z);
                cajas[i] = QRectF(QPointF(xa, ya), QPointF(xb, yb)).normalized();

                // Reparte cada segmento a las teselas que cruza su bbox.
                for (int j = 0; j + 1 < poly.size(); ++j) {
                    const QPointF &a = poly[j];
                    const QPointF &b = poly[j + 1];
                    int sx0 = int(std::floor(std::min(a.x(), b.x()) / kTile));
                    int sx1 = int(std::floor(std::max(a.x(), b.x()) / kTile));
                    int sy0 = int(std::floor(std::min(a.y(), b.y()) / kTile));
                    int sy1 = int(std::floor(std::max(a.y(), b.y()) / kTile));
                    sx0 = qBound(tx0, sx0, tx1); sx1 = qBound(tx0, sx1, tx1);
                    sy0 = qBound(ty0, sy0, ty1); sy1 = qBound(ty0, sy1, ty1);
                    const QLineF seg(a, b);
                    for (int ty = sy0; ty <= sy1; ++ty)
                        for (int tx = sx0; tx <= sx1; ++tx)
                            buckets[clave(tx, ty)].append(seg);
                }
            }

            QSqlQuery tx(db);
            tx.exec(QStringLiteral("BEGIN"));
            qint64 enZ = 0;

            for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
                const int tx_ = int(it.key() % nPerSide);
                const int ty  = int(it.key() / nPerSide);

                QImage img(kTile, kTile, QImage::Format_ARGB32_Premultiplied);
                img.fill(bg);
                QPainter pn(&img);
                pn.setRenderHint(QPainter::Antialiasing, true);
                QPen lapiz(color, width);
                lapiz.setJoinStyle(Qt::RoundJoin);
                lapiz.setCapStyle(Qt::RoundCap);
                pn.setPen(lapiz);
                pn.translate(-double(tx_) * kTile, -double(ty) * kTile);

                // Relleno opcional: se pintan los poligonos cerrados que rozan
                // la tesela, por debajo de las lineas.
                if (fill) {
                    pn.setBrush(QBrush(color));
                    const QRectF celda(tx_, ty, 1, 1);
                    for (int i = 0; i < paths.size(); ++i)
                        if (paths[i].closed && gpx[i].size() >= 3
                            && cajas[i].intersects(celda))
                            pn.drawPolygon(gpx[i]);
                    pn.setBrush(Qt::NoBrush);
                }

                pn.drawLines(it.value());
                pn.end();

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
            tx.exec(QStringLiteral("COMMIT"));
            total += enZ;
            // stderr va sin buffer: sirve para ver el progreso en vivo.
            qInfo().noquote() << QStringLiteral("  z=%1: %2 teselas (%3x%4 celdas)")
                                     .arg(z).arg(enZ)
                                     .arg(tx1 - tx0 + 1).arg(ty1 - ty0 + 1);
        }

        qInfo().noquote() << QStringLiteral("Total: %1 teselas en %2").arg(total).arg(out);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("gen"));

    // Bloque listo para pegar en datasets.json.
    const QString rec = QString::number(qMax(minZ, maxZ - 1));
    cout << "\n--- Anade esto al array \"datasets\" de tu datasets.json ---\n";
    cout << QStringLiteral(
        "    {\n"
        "      \"id\": \"%1\",\n"
        "      \"displayName\": \"%2\",\n"
        "      \"filePath\": \"%3\",\n"
        "      \"tableName\": \"tiles\",\n"
        "      \"zFactor\": 1, \"zOffset\": 0,\n"
        "      \"minZoom\": %4, \"maxZoom\": %5, \"recommendedMaxZoom\": %6,\n"
        "      \"typicalFill\": 1.0, \"scheme\": \"XYZ\",\n"
        "      \"sValue\": 0, \"hasSColumn\": true, \"tileSize\": 256,\n"
        "      \"colZ\": \"z\", \"colX\": \"x\", \"colY\": \"y\", \"colS\": \"s\",\n"
        "      \"colImage\": \"image\", \"baseZoom\": %4\n"
        "    }\n")
            .arg(id, name, QFileInfo(out).fileName())
            .arg(minZ).arg(maxZ).arg(rec);
    return 0;
}
