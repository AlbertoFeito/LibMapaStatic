/*!
 * vector_tile - inspecciona y pinta un MBTiles VECTORIAL (pbf / MVT) sin ventana.
 *
 * Sirve para comprobar un MBTiles vectorial (p. ej. el de Cuba de OpenMapTiles) y
 * para afinar un estilo antes de ponerlo en datasets.json, sin abrir la demo.
 *
 * Uso:
 *   vector_tile --mbtiles cuba-osm-z16.mbtiles --info [--zoom 14 --center 23.1136,-82.3666]
 *     Capas, número de entidades y claves de la tesela de ese punto.
 *
 *   vector_tile --mbtiles cuba-osm-z16.mbtiles --out habana.png
 *               [--center 23.1136,-82.3666] [--zoom 14] [--size 1024x768]
 *               [--style estilo.json]
 *     Pinta la vista (mosaico de teselas de 256 px) con el estilo por defecto
 *     o con el dado, y dice cuánto tarda cada tesela.
 *
 *   vector_tile --print-style > estilo.json
 *     Vuelca el estilo por defecto (OpenMapTiles), plantilla para uno propio.
 *
 * Los rótulos necesitan QGuiApplication: en un servidor sin pantalla se ejecuta
 * con QT_QPA_PLATFORM=offscreen.
 */

#include "geo/WebMercator.h"
#include "tiles/MBTiles.h"
#include "tiles/RMapsTileSource.h"
#include "vector/MvtTile.h"
#include "vector/VectorStyle.h"
#include "vector/VectorTileRenderer.h"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QTextStream>

#include <cmath>

using namespace libmapa;

namespace {

// Posición en píxeles del mundo (Web Mercator) a un zoom con teselas de 256 px.
QPointF worldPixel(double lat, double lon, int z)
{
    const double pi = 3.14159265358979323846;
    const double n = 256.0 * double(1 << z);
    const double la = WebMercator::clampLatitude(lat) * pi / 180.0;
    const double x = (lon + 180.0) / 360.0 * n;
    const double y = (1.0 - std::asinh(std::tan(la)) / pi) / 2.0 * n;
    return QPointF(x, y);
}

// Lista capas, entidades por tipo y claves de la tesela que contiene el punto.
int printInfo(RMapsTileSource &src, double lat, double lon, int z, QTextStream &out)
{
    const QPointF px = worldPixel(lat, lon, z);
    const TileKey key{ z, int(px.x() / 256.0), int(px.y() / 256.0) };
    const QByteArray bytes = src.fetch(key);
    out << "Tesela z" << key.z << " x" << key.x << " y" << key.y << " (XYZ): "
        << bytes.size() << " bytes\n";
    if (bytes.isEmpty())
        return 1;
    mvt::Tile tile;
    QString error;
    if (!mvt::decode(bytes, &tile, &error)) {
        out << "No es MVT: " << error << "\n";
        return 1;
    }
    for (const mvt::Layer &l : tile.layers) {
        int pts = 0, lines = 0, polys = 0;
        for (const mvt::Feature &f : l.features) {
            pts += f.type == mvt::GeomType::Point;
            lines += f.type == mvt::GeomType::LineString;
            polys += f.type == mvt::GeomType::Polygon;
        }
        out << "  " << l.name << " (extent " << l.extent << "): " << l.features.size()
            << " entidades [" << pts << " puntos, " << lines << " lineas, " << polys
            << " poligonos]\n      claves: " << l.keys.join(QStringLiteral(", ")) << "\n";
        // Valores distintos de "class", que es por lo que filtra el estilo.
        const int ci = l.keyIndex(QStringLiteral("class"));
        if (ci >= 0) {
            QStringList clases;
            for (const mvt::Feature &f : l.features) {
                const QString c = l.value(f, ci).toString();
                if (!c.isEmpty() && !clases.contains(c))
                    clases << c;
            }
            out << "      class: " << clases.join(QStringLiteral(", ")) << "\n";
        }
    }
    return 0;
}

} // namespace

// Punto de entrada: lee las opciones, abre el MBTiles con el lector de siempre
// (RMapsTileSource con la disposición de MBTiles) y hace lo que se pida.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Inspecciona y pinta un MBTiles vectorial (pbf)"));
    p.addHelpOption();
    const QCommandLineOption oFile(QStringLiteral("mbtiles"), QStringLiteral("MBTiles vectorial"),
                                   QStringLiteral("fichero"));
    const QCommandLineOption oOut(QStringLiteral("out"), QStringLiteral("PNG de salida"),
                                  QStringLiteral("ruta"));
    const QCommandLineOption oCenter(QStringLiteral("center"), QStringLiteral("lat,lon"),
                                     QStringLiteral("coord"), QStringLiteral("23.1136,-82.3666"));
    const QCommandLineOption oZoom(QStringLiteral("zoom"), QStringLiteral("Nivel de zoom"),
                                   QStringLiteral("z"), QStringLiteral("14"));
    const QCommandLineOption oSize(QStringLiteral("size"), QStringLiteral("AnchoxAlto"),
                                   QStringLiteral("px"), QStringLiteral("1024x768"));
    const QCommandLineOption oStyle(QStringLiteral("style"), QStringLiteral("Estilo JSON propio"),
                                    QStringLiteral("fichero"));
    const QCommandLineOption oInfo(QStringLiteral("info"),
                                   QStringLiteral("Capas y claves de la tesela del centro"));
    const QCommandLineOption oPrint(QStringLiteral("print-style"),
                                    QStringLiteral("Vuelca el estilo por defecto (JSON)"));
    p.addOptions({ oFile, oOut, oCenter, oZoom, oSize, oStyle, oInfo, oPrint });
    p.process(app);

    if (p.isSet(oPrint)) {
        out << VectorStyle::openMapTilesJson() << "\n";
        return 0;
    }
    if (!p.isSet(oFile) || (!p.isSet(oInfo) && !p.isSet(oOut))) {
        err << "Falta --mbtiles y --info o --out. Ver --help.\n";
        return 2;
    }

    QString error;
    const TileDataset ds = MBTiles::fromFile(p.value(oFile), QString(), &error);
    if (!error.isEmpty() || !ds.isValid()) {
        err << "No se puede usar " << p.value(oFile) << ": " << error << "\n";
        return 1;
    }
    out << ds.displayName << ": z" << ds.minZoom << "-" << ds.maxZoom
        << (ds.vector ? ", vectorial (MVT)" : ", raster") << "\n";
    if (!ds.vector) {
        err << "No es un MBTiles vectorial: esta herramienta es para pbf/MVT.\n";
        return 1;
    }

    const QStringList c = p.value(oCenter).split(QLatin1Char(','));
    const double lat = c.value(0).toDouble(), lon = c.value(1).toDouble();
    const int z = qBound(ds.minZoom, p.value(oZoom).toInt(), ds.maxZoom);

    RMapsTileSource src(ds);
    if (!src.open()) {
        err << "No se pudo abrir: " << src.lastError() << "\n";
        return 1;
    }
    if (p.isSet(oInfo))
        return printInfo(src, lat, lon, z, out);

    VectorStyle style = VectorStyle::openMapTiles();
    if (p.isSet(oStyle) && !VectorStyle::fromFile(p.value(oStyle), &style, &error)) {
        err << error << "\n";
        return 1;
    }
    const VectorTileRenderer renderer(style);

    const QStringList wh = p.value(oSize).split(QLatin1Char('x'));
    const int w = qMax(64, wh.value(0).toInt()), h = qMax(64, wh.value(1).toInt());
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(style.background);
    QPainter painter(&img);

    // Esquina superior izquierda de la vista en píxeles del mundo y rejilla de
    // teselas que la cubre.
    const QPointF center = worldPixel(lat, lon, z);
    const QPointF origin = center - QPointF(w / 2.0, h / 2.0);
    const int x0 = int(std::floor(origin.x() / 256.0)), x1 = int(std::floor((origin.x() + w) / 256.0));
    const int y0 = int(std::floor(origin.y() / 256.0)), y1 = int(std::floor((origin.y() + h) / 256.0));
    const auto tiles = src.fetchRange(z, x0, x1, qMax(0, y0), qMin((1 << z) - 1, y1));

    QElapsedTimer t;
    qint64 worst = 0, total = 0;
    int n = 0;
    for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it) {
        t.restart();
        const QImage tileImg = renderer.render(it.value(), z, 256, &error);
        const qint64 ms = t.elapsed();
        worst = qMax(worst, ms);
        total += ms;
        ++n;
        if (tileImg.isNull()) {
            err << "Tesela " << it.key().x << "," << it.key().y << ": " << error << "\n";
            continue;
        }
        painter.drawImage(QPointF(it.key().x * 256.0 - origin.x(), it.key().y * 256.0 - origin.y()),
                          tileImg);
    }
    painter.end();

    if (!img.save(p.value(oOut))) {
        err << "No se pudo escribir " << p.value(oOut) << "\n";
        return 1;
    }
    out << n << " teselas z" << z << " pintadas en " << total << " ms (peor " << worst
        << " ms) -> " << p.value(oOut) << "\n";
    return 0;
}
