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
 *               [--style-file estilo.json] [--land-mask cuba_tierra.sqlitedb [--land-layer tierra]]
 *     Pinta la vista (mosaico de teselas de 256 px) con el estilo por defecto
 *     o con el dado, y dice cuánto tarda cada tesela. Con --land-mask (la BD de
 *     costa de osm_costa_a_sqlitedb.py) pinta además el mar. --hide poi,edificios
 *     apaga grupos del estilo (los lista --info). --hybrid usa el estilo del
 *     mapa hibrido (PNG con fondo transparente).
 *
 *   vector_tile --mbtiles cuba-osm-z16.mbtiles --at 23.1383,-82.3589 [--zoom 16] [--radius 15]
 *     «Que hay aqui»: las entidades (calles, edificios, lugares...) a menos de
 *     --radius metros (def. 15) del punto, con sus atributos. Es lo que hace
 *     MapWidget::vectorFeaturesAt al hacer clic.
 *
 *   vector_tile --print-style [--hybrid] > estilo.json
 *     Vuelca el estilo por defecto (OpenMapTiles, o el hibrido), plantilla para uno propio.
 *
 * Los rótulos necesitan QGuiApplication: en un servidor sin pantalla se ejecuta
 * con QT_QPA_PLATFORM=offscreen.
 */

#include "geo/WebMercator.h"
#include "tiles/MBTiles.h"
#include "tiles/RMapsTileSource.h"
#include "vector/FeatureQuery.h"
#include "vector/MvtTile.h"
#include "vector/VectorStyle.h"
#include "vector/VectorTileRenderer.h"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QSet>
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

// «Que hay aqui»: lee la tesela del punto y lista lo que hay a menos de \a radiusM.
int printAt(RMapsTileSource &src, double lat, double lon, int z, double radiusM, QTextStream &out)
{
    const QPointF px = worldPixel(lat, lon, z);
    const TileKey key{ z, int(px.x() / 256.0), int(px.y() / 256.0) };
    mvt::Tile tile;
    QString error;
    if (!mvt::decode(src.fetch(key), &tile, &error)) {
        out << "Sin tesela z" << z << " en ese punto: " << error << "\n";
        return 1;
    }
    const auto hits = featurequery::query(tile, key, lat, lon, radiusM);
    out << hits.size() << " entidades a menos de " << radiusM << " m de " << lat << "," << lon << "\n";
    for (const featurequery::Hit &h : hits) {
        out << "  " << h.layer << " ["
            << (h.type == mvt::GeomType::Point ? "punto"
                : h.type == mvt::GeomType::LineString ? "linea" : "poligono")
            << ", " << (h.distanceM < 0.5 ? QStringLiteral("aqui")
                                          : QStringLiteral("a %1 m").arg(h.distanceM, 0, 'f', 1)) << "]";
        for (auto it = h.attributes.constBegin(); it != h.attributes.constEnd(); ++it)
            if (!it.key().startsWith(QLatin1String("name:")))
                out << " " << it.key() << "=" << it.value().toString();
        out << "\n";
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
    // "style-file" y no "style": Qt se queda con -style/--style (el estilo visual
    // de la aplicacion) antes de que llegue aqui.
    const QCommandLineOption oStyle(QStringLiteral("style-file"), QStringLiteral("Estilo JSON propio"),
                                    QStringLiteral("fichero"));
    const QCommandLineOption oMask(QStringLiteral("land-mask"),
                                   QStringLiteral("Mascara de tierra (BD de entidades) para pintar el mar"),
                                   QStringLiteral("fichero"));
    const QCommandLineOption oMaskLayer(QStringLiteral("land-layer"),
                                        QStringLiteral("Capa de la mascara (def. todas)"),
                                        QStringLiteral("capa"));
    const QCommandLineOption oHide(QStringLiteral("hide"),
                                   QStringLiteral("Grupos del estilo que no se dibujan, separados por comas "
                                                  "(nombres_calles, poi, edificios...)"),
                                   QStringLiteral("grupos"));
    const QCommandLineOption oHybrid(QStringLiteral("hybrid"),
                                     QStringLiteral("Estilo hibrido (fondo transparente, para ir encima "
                                                    "de otra base) en vez del de OpenMapTiles"));
    const QCommandLineOption oInfo(QStringLiteral("info"),
                                   QStringLiteral("Capas y claves de la tesela del centro"));
    const QCommandLineOption oPrint(QStringLiteral("print-style"),
                                    QStringLiteral("Vuelca el estilo por defecto (JSON)"));
    const QCommandLineOption oAt(QStringLiteral("at"),
                                 QStringLiteral("Que hay en este punto (lat,lon): entidades y atributos"),
                                 QStringLiteral("coord"));
    const QCommandLineOption oRadius(QStringLiteral("radius"),
                                     QStringLiteral("Radio de --at en metros"),
                                     QStringLiteral("m"), QStringLiteral("15"));
    p.addOptions({ oAt, oRadius, oFile, oOut, oCenter, oZoom, oSize, oStyle, oMask, oMaskLayer, oHide, oHybrid, oInfo, oPrint });
    p.process(app);

    if (p.isSet(oPrint)) {
        out << (p.isSet(oHybrid) ? VectorStyle::hybridJson() : VectorStyle::openMapTilesJson()) << "\n";
        return 0;
    }
    if (!p.isSet(oFile) || (!p.isSet(oInfo) && !p.isSet(oOut) && !p.isSet(oAt))) {
        err << "Falta --mbtiles y --info, --out o --at. Ver --help.\n";
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
    if (p.isSet(oAt)) {
        const QStringList a = p.value(oAt).split(QLatin1Char(','));
        return printAt(src, a.value(0).toDouble(), a.value(1).toDouble(), z,
                       p.value(oRadius).toDouble(), out);
    }
    VectorStyle style = VectorStyle::defaultFor(p.isSet(oHybrid));
    if (p.isSet(oStyle) && !VectorStyle::fromFile(p.value(oStyle), &style, &error)) {
        err << error << "\n";
        return 1;
    }
    if (p.isSet(oInfo)) {
        out << "Grupos del estilo (para --hide y \"hiddenGroups\"):\n";
        for (const auto &g : style.groups)
            out << "  " << g.first << "  (" << g.second << ")\n";
        return printInfo(src, lat, lon, z, out);
    }

    VectorTileRenderer renderer(style);
    if (p.isSet(oHide)) {
        const QStringList hide = p.value(oHide).split(QLatin1Char(','), Qt::SkipEmptyParts);
        renderer.setHiddenGroups(QSet<QString>(hide.begin(), hide.end()));
    }
    if (p.isSet(oMask)) {
        QElapsedTimer tm;
        tm.start();
        auto mask = LandMask::fromFile(p.value(oMask), p.value(oMaskLayer), &error);
        if (!mask) {
            err << error << "\n";
            return 1;
        }
        out << "Mascara de tierra: " << mask->ringCount() << " anillos, " << mask->vertexCount()
            << " vertices (" << tm.elapsed() << " ms)\n";
        renderer.setLandMask(mask);
    }

    const QStringList wh = p.value(oSize).split(QLatin1Char('x'));
    const int w = qMax(64, wh.value(0).toInt()), h = qMax(64, wh.value(1).toInt());
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    // Donde el MBTiles no tiene tesela (mar abierto) va el fondo; con máscara, el mar.
    img.fill(renderer.landMask() ? style.sea : style.background);
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
        const QImage tileImg = renderer.render(it.value(), it.key(), 256, &error);
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
