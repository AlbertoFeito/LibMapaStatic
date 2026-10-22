#include "io/DataPackage.h"
#include "io/PackageCheck.h"
#include "tiles/MBTiles.h"
#include "tiles/TileDataset.h"
#include "tiles/TileLoader.h"
#include "tiles/TileService.h"
#include "vector/FeatureQuery.h"
#include "vector/Gzip.h"
#include "vector/LandMask.h"
#include "vector/MvtTile.h"
#include "vector/PoiSearch.h"
#include "vector/StreetSearch.h"
#include "vector/VectorStyle.h"
#include "vector/VectorTileRenderer.h"

#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest>

#include <cmath>

using namespace libmapa;

// Pruebas de las teselas vectoriales (MBTiles pbf / Mapbox Vector Tile).
//
// El decodificador se contrasta con los fixtures oficiales de mapbox/mvt-fixtures
// (tests/data/mvt, CC0), que codifican los ejemplos de la especificación: eso es
// la referencia independiente. Luego se prueban el estilo, el dibujo y la entrada
// por un MBTiles hasta el TileLoader, con teselas fabricadas aquí (ida y vuelta),
// y que lo ráster de siempre no cambia.
class TstVectorTiles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void decodesSpecPoint();
    void decodesSpecLineAndPolygon();
    void decodesSpecMultiGeometries();
    void decodesAllValueTypes();
    void inflatesGzipAndZlib();
    void rejectsGarbage();
    void defaultStyleParses();
    void styleFromJson();
    void rendersGeometryWithStyle();
    void labelsStayInsideTheirTile();
    void mbtilesVectorIsDetected();
    void loaderPaintsVectorTiles();
    void datasetsJsonResolvesStyle();
    void packageWithVectorLayer();
    void landMaskPaintsSea();
    void landMaskClipMatchesFullPolygon();
    void styleGroups();
    void hiddenGroupsAreNotDrawn();
    void hybridStyleIsTransparent();
    void namesFollowTheLine();
    void loaderChangesHiddenGroups();
    void packageDeclaresHybridOverlay();
    void queryFindsWhatIsHere();
    void poiGroupsAndShapes();
    void decodeOnlyChosenLayers();
    void searchesPointsOfInterest();
    void searchesStreets();

private:
    QByteArray fixture(const char *name) const;
    static bool makeVectorMBTiles(const QString &path, const QByteArray &tile, int maxZ);

    QTemporaryDir m_dir;
    QString p(const char *name) const { return m_dir.path() + QStringLiteral("/") + QLatin1String(name); }
};

// --- Codificador mínimo de MVT, solo para fabricar teselas de prueba ----------

namespace {

// Escritor de protobuf: lo justo para construir Tile/Layer/Feature.
struct Pbf
{
    QByteArray b;

    // Varint base 128.
    void varint(quint64 v)
    {
        while (v >= 0x80) {
            b.append(char((v & 0x7f) | 0x80));
            v >>= 7;
        }
        b.append(char(v));
    }
    // Cabecera de campo.
    void key(int field, int wire) { varint(quint64((field << 3) | wire)); }
    // Campo varint.
    void uint(int field, quint64 v) { key(field, 0); varint(v); }
    // Campo con longitud (texto o sub-mensaje).
    void bytes(int field, const QByteArray &data)
    {
        key(field, 2);
        varint(quint64(data.size()));
        b.append(data);
    }
    // Campo repeated uint32 empaquetado.
    void packed(int field, const QVector<quint32> &v)
    {
        Pbf inner;
        for (quint32 x : v)
            inner.varint(x);
        bytes(field, inner.b);
    }
};

// Entero de comando de geometría: id en los 3 bits bajos, cuenta en el resto.
quint32 cmd(int id, int count) { return quint32((id & 7) | (count << 3)); }
// Codificación zigzag de un desplazamiento.
quint32 zz(int v) { return quint32((v << 1) ^ (v >> 31)); }

// Comandos de un anillo cerrado (polígono) o de una línea, en coordenadas absolutas.
QVector<quint32> path(const QVector<QPoint> &pts, bool closed)
{
    QVector<quint32> g;
    g << cmd(1, 1) << zz(pts[0].x()) << zz(pts[0].y());
    QPoint cur = pts[0];
    g << cmd(2, int(pts.size()) - 1);
    for (int i = 1; i < pts.size(); ++i) {
        g << zz(pts[i].x() - cur.x()) << zz(pts[i].y() - cur.y());
        cur = pts[i];
    }
    if (closed)
        g << cmd(7, 1);
    return g;
}

struct FeatureSpec
{
    int type;                      // 1 punto, 2 línea, 3 polígono
    QVector<quint32> geometry;
    QVector<QPair<QString, QString>> props;
};

// Una capa con sus entidades; las claves y valores (todos texto) se reparten en
// las tablas de la capa como hace cualquier codificador.
QByteArray layer(const QString &name, const QVector<FeatureSpec> &features)
{
    QStringList keys, values;
    Pbf l;
    l.uint(15, 2);
    l.bytes(1, name.toUtf8());
    for (const FeatureSpec &f : features) {
        QVector<quint32> tags;
        for (const auto &kv : f.props) {
            if (!keys.contains(kv.first)) keys << kv.first;
            if (!values.contains(kv.second)) values << kv.second;
            tags << quint32(keys.indexOf(kv.first)) << quint32(values.indexOf(kv.second));
        }
        Pbf fe;
        if (!tags.isEmpty())
            fe.packed(2, tags);
        fe.uint(3, quint64(f.type));
        fe.packed(4, f.geometry);
        l.bytes(2, fe.b);
    }
    for (const QString &k : keys)
        l.bytes(3, k.toUtf8());
    for (const QString &v : values) {
        Pbf val;
        val.bytes(1, v.toUtf8());
        l.bytes(4, val.b);
    }
    l.uint(5, 4096);
    return l.b;
}

// Una tesela con las capas dadas.
QByteArray tile(const QVector<QByteArray> &layers)
{
    Pbf t;
    for (const QByteArray &l : layers)
        t.bytes(3, l);
    return t.b;
}

// Una tesela de agua entera con una autopista horizontal y un camino vertical.
QByteArray waterAndMotorway()
{
    const QVector<QPoint> square{ { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } };
    return tile({ layer(QStringLiteral("water"), { { 3, path(square, true), {} } }),
                  layer(QStringLiteral("transportation"),
                        { { 2, path({ { -100, 2048 }, { 4196, 2048 } }, false),
                            { { QStringLiteral("class"), QStringLiteral("motorway") } } },
                          { 2, path({ { 2048, -100 }, { 2048, 4196 } }, false),
                            { { QStringLiteral("class"), QStringLiteral("path") } } } }) });
}

// Esquina noroeste (lat, lon) de la tesela XYZ (z, x, y) más una fracción (fx, fy)
// de su lado: para colocar polígonos de máscara en sitios conocidos de la tesela.
QGeoCoordinate tilePoint(int z, int x, int y, double fx, double fy)
{
    const double pi = 3.14159265358979323846;
    const double n = std::ldexp(1.0, z);
    const double lon = (x + fx) / n * 360.0 - 180.0;
    const double lat = std::atan(std::sinh(pi * (1.0 - 2.0 * (y + fy) / n))) * 180.0 / pi;
    return QGeoCoordinate(lat, lon);
}

// zlib a partir de qCompress, quitando los 4 bytes de longitud que antepone Qt.
QByteArray zlib(const QByteArray &raw) { return qCompress(raw).mid(4); }

// Comparación de color con tolerancia (el antialiasing toca los bordes).
bool sameColor(QRgb a, const QColor &b, int tol = 8)
{
    return qAbs(qRed(a) - b.red()) <= tol && qAbs(qGreen(a) - b.green()) <= tol
           && qAbs(qBlue(a) - b.blue()) <= tol;
}

// Estilo de prueba con colores puros, fácil de comprobar píxel a píxel.
VectorStyle testStyle()
{
    const QByteArray json = R"({
      "background": "#000000",
      "rules": [
        { "layer": "water", "fill": "#0000ff" },
        { "layer": "transportation", "filter": { "class": "motorway" }, "line": "#ff0000",
          "width": [[0, 2], [10, 12]] },
        { "layer": "transportation", "exclude": { "class": "motorway" }, "minZoom": 12,
          "line": "#ffff00", "width": 4 },
        { "layer": "place", "label": "name", "textColor": "#00ff00", "halo": "#00000000",
          "textSize": 14 }
      ] })";
    VectorStyle s;
    VectorStyle::fromJson(QJsonDocument::fromJson(json).object(), &s);
    return s;
}

} // namespace

// Directorio temporal y tipos para QSignalSpy.
void TstVectorTiles::initTestCase()
{
    QVERIFY(m_dir.isValid());
    qRegisterMetaType<QVector<LoadedTile>>("QVector<libmapa::LoadedTile>");
    qRegisterMetaType<TileMatrix::TileRange>("libmapa::TileMatrix::TileRange");
}

// Bytes de un fixture de tests/data/mvt.
QByteArray TstVectorTiles::fixture(const char *name) const
{
    QFile f(QStringLiteral(LIBMAPA_TESTDATA "/mvt/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

// MBTiles vectorial: el mismo MVT (comprimido en zlib) en todas las teselas de
// los zooms 0..maxZ, con las filas en TMS.
bool TstVectorTiles::makeVectorMBTiles(const QString &path, const QByteArray &tileBytes, int maxZ)
{
    const QString conn = QStringLiteral("vec_%1").arg(path);
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        ok = q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                   "tile_row INTEGER, tile_data BLOB)"))
             && q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)"))
             && q.exec(QStringLiteral("INSERT INTO metadata VALUES ('format', 'pbf')"))
             && q.exec(QStringLiteral("INSERT INTO metadata VALUES ('name', 'Vector de prueba')"));
        db.transaction();
        for (int z = 0; ok && z <= maxZ; ++z)
            for (int x = 0; x < (1 << z); ++x)
                for (int y = 0; y < (1 << z); ++y) {
                    q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
                    q.bindValue(QStringLiteral(":z"), z);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), (1 << z) - 1 - y);
                    q.bindValue(QStringLiteral(":d"), zlib(tileBytes));
                    ok = ok && q.exec();
                }
        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

// §4.3.5.1: [9, 50, 34] es un punto en (25, 17); la propiedad hello=world.
void TstVectorTiles::decodesSpecPoint()
{
    mvt::Tile t;
    QString error;
    QVERIFY2(mvt::decode(fixture("017.mvt"), &t, &error), qPrintable(error));
    QCOMPARE(t.layers.size(), 1);
    const mvt::Layer &l = t.layers[0];
    QCOMPARE(l.name, QStringLiteral("hello"));
    QCOMPARE(l.version, 2);
    QCOMPARE(l.extent, 4096);
    QCOMPARE(l.features.size(), 1);
    const mvt::Feature &f = l.features[0];
    QVERIFY(f.hasId);
    QCOMPARE(f.id, quint64(1));
    QCOMPARE(f.type, mvt::GeomType::Point);
    QCOMPARE(f.parts.size(), 1);
    QCOMPARE(f.parts[0], QVector<QPoint>({ QPoint(25, 17) }));
    QCOMPARE(l.value(f, QStringLiteral("hello")).toString(), QStringLiteral("world"));
    QVERIFY(!l.value(f, QStringLiteral("no_existe")).isValid());
    QVERIFY(t.layer(QStringLiteral("hello")) == &t.layers[0]);
    QVERIFY(t.layer(QStringLiteral("otra")) == nullptr);
}

// §4.3.5.2 y §4.3.5.4: la línea y el polígono de los ejemplos.
void TstVectorTiles::decodesSpecLineAndPolygon()
{
    mvt::Tile line, poly;
    QVERIFY(mvt::decode(fixture("018.mvt"), &line));
    QCOMPARE(line.layers[0].features[0].type, mvt::GeomType::LineString);
    QCOMPARE(line.layers[0].features[0].parts,
             QVector<QVector<QPoint>>({ { QPoint(2, 2), QPoint(2, 10), QPoint(10, 10) } }));

    QVERIFY(mvt::decode(fixture("019.mvt"), &poly));
    QCOMPARE(poly.layers[0].features[0].type, mvt::GeomType::Polygon);
    QCOMPARE(poly.layers[0].features[0].parts,
             QVector<QVector<QPoint>>({ { QPoint(3, 6), QPoint(8, 12), QPoint(20, 34) } }));
}

// Multipunto, multilínea y multipolígono con hueco (§4.3.5.5), y una capa vacía.
void TstVectorTiles::decodesSpecMultiGeometries()
{
    mvt::Tile mp, ml, mpoly, empty;
    QVERIFY(mvt::decode(fixture("020.mvt"), &mp));
    QCOMPARE(mp.layers[0].features[0].parts,
             QVector<QVector<QPoint>>({ { QPoint(5, 7), QPoint(3, 2) } }));

    QVERIFY(mvt::decode(fixture("021.mvt"), &ml));
    QCOMPARE(ml.layers[0].features[0].parts,
             QVector<QVector<QPoint>>({ { QPoint(2, 2), QPoint(2, 10), QPoint(10, 10) },
                                        { QPoint(1, 1), QPoint(3, 5) } }));

    QVERIFY(mvt::decode(fixture("022.mvt"), &mpoly));
    const QVector<QVector<QPoint>> rings = mpoly.layers[0].features[0].parts;
    QCOMPARE(rings.size(), 3);
    QCOMPARE(rings[0], QVector<QPoint>({ { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } }));
    QCOMPARE(rings[1], QVector<QPoint>({ { 11, 11 }, { 20, 11 }, { 20, 20 }, { 11, 20 } }));
    QCOMPARE(rings[2], QVector<QPoint>({ { 13, 13 }, { 13, 17 }, { 17, 17 }, { 17, 13 } }));

    QVERIFY(mvt::decode(fixture("025.mvt"), &empty));
    QCOMPARE(empty.layers.size(), 1);
    QVERIFY(empty.layers[0].features.isEmpty());
}

// Los siete tipos de Value (texto, float, double, int64, uint64, sint64 y bool).
void TstVectorTiles::decodesAllValueTypes()
{
    mvt::Tile t;
    QVERIFY(mvt::decode(fixture("038.mvt"), &t));
    const mvt::Layer &l = t.layers[0];
    const mvt::Feature &f = l.features[0];
    QCOMPARE(l.value(f, QStringLiteral("string_value")).toString(), QStringLiteral("ello"));
    QCOMPARE(l.value(f, QStringLiteral("bool_value")).toBool(), true);
    QCOMPARE(l.value(f, QStringLiteral("int_value")).toLongLong(), 6LL);
    QCOMPARE(l.value(f, QStringLiteral("double_value")).toDouble(), 1.23);
    QVERIFY(qAbs(l.value(f, QStringLiteral("float_value")).toDouble() - 3.1) < 1e-6);
    QCOMPARE(l.value(f, QStringLiteral("sint_value")).toLongLong(), -87948LL);
    QCOMPARE(l.value(f, QStringLiteral("uint_value")).toULongLong(), 87948ULL);
}

// El mismo polígono comprimido por Python en gzip y en zlib sale igual.
void TstVectorTiles::inflatesGzipAndZlib()
{
    const QByteArray raw = fixture("019.mvt");
    const QByteArray gz = fixture("019.mvt.gz");
    const QByteArray zl = fixture("019.mvt.zlib");
    QVERIFY(Gzip::isGzip(gz));
    QVERIFY(!Gzip::isGzip(raw));
    QVERIFY(Gzip::isZlib(zl));
    QVERIFY(!Gzip::isZlib(raw));

    bool ok = false;
    QCOMPARE(Gzip::inflateIfCompressed(gz, &ok), raw);
    QVERIFY(ok);
    QCOMPARE(Gzip::inflateIfCompressed(zl, &ok), raw);
    QVERIFY(ok);
    QCOMPARE(Gzip::inflateIfCompressed(raw, &ok), raw);       // en claro: intacto
    QVERIFY(ok);

    mvt::Tile t;
    QVERIFY(mvt::decode(gz, &t));
    QCOMPARE(t.layers[0].features[0].parts[0].size(), 3);

    // Tesela VACÍA tal cual viene en el MBTiles de Cuba (OpenMapTiles, mar
    // abierto): gzip de cero bytes. Es válida y no tiene capas.
    const QByteArray vacia = QByteArray::fromHex("1f8b08000000000000ff010000ffff0000000000000000");
    QCOMPARE(Gzip::inflateIfCompressed(vacia, &ok), QByteArray());
    QVERIFY(ok);
    QVERIFY(mvt::decode(vacia, &t));
    QVERIFY(t.layers.isEmpty());
}

// Bytes que no son una tesela: gzip truncado, PNG, basura. Nunca un cuelgue.
void TstVectorTiles::rejectsGarbage()
{
    mvt::Tile t;
    QString error;
    QVERIFY(!mvt::decode(QByteArray::fromHex("1f8b0800000000000003"), &t, &error));
    QVERIFY(!error.isEmpty());

    QByteArray gz = fixture("019.mvt.gz");
    gz.chop(20);
    QVERIFY(!mvt::decode(gz, &t));

    QVERIFY(!mvt::decode(QByteArray("\x1a\xff\xff\xff\x0f", 5), &t));   // longitud imposible
    QVERIFY(!mvt::decode(QByteArray("\x00\x01\x02", 3), &t));            // campo 0

    // Truncar un MVT válido a cualquier longitud nunca lee fuera del buffer.
    const QByteArray full = fixture("038.mvt");
    for (int n = 1; n < full.size(); ++n)
        mvt::decode(full.left(n), &t);

    // Geometrías incoherentes con su tipo.
    QVector<QVector<QPoint>> parts;
    QVERIFY(!mvt::decodeGeometry({ cmd(2, 1), 2, 2 }, mvt::GeomType::LineString, &parts));
    QVERIFY(!mvt::decodeGeometry({ cmd(1, 1), 2, 2 }, mvt::GeomType::LineString, &parts));
    QVERIFY(!mvt::decodeGeometry({ cmd(1, 1), 2, 2, cmd(2, 1), 2, 2 }, mvt::GeomType::Polygon,
                                 &parts));
    QVERIFY(!mvt::decodeGeometry({ cmd(1, 3), 2, 2 }, mvt::GeomType::Point, &parts));
}

// El estilo por defecto (OpenMapTiles) es un JSON válido con las capas clave.
void TstVectorTiles::defaultStyleParses()
{
    QJsonParseError pe{};
    QVERIFY(!QJsonDocument::fromJson(VectorStyle::openMapTilesJson(), &pe).isNull());
    const VectorStyle s = VectorStyle::openMapTiles();
    QVERIFY(s.rules.size() > 30);
    QStringList layers;
    for (const VectorRule &r : s.rules)
        layers << r.layer;
    for (const char *l : { "water", "landcover", "landuse", "park", "building", "transportation",
                           "boundary", "waterway", "place" })
        QVERIFY2(layers.contains(QLatin1String(l)), l);
}

// Lectura de reglas: filtros, exclusiones, geometría, paradas de grosor.
void TstVectorTiles::styleFromJson()
{
    const VectorStyle s = testStyle();
    QCOMPARE(s.background, QColor(Qt::black));
    QCOMPARE(s.rules.size(), 4);
    const VectorRule &motorway = s.rules[1];
    QCOMPARE(motorway.filter.size(), 1);
    QCOMPARE(motorway.filter[0].second, QStringList{ QStringLiteral("motorway") });
    QCOMPARE(motorway.widthAt(0), 2.0);
    QCOMPARE(motorway.widthAt(5), 7.0);
    QCOMPARE(motorway.widthAt(10), 12.0);
    QCOMPARE(motorway.widthAt(18), 12.0);
    QCOMPARE(s.rules[2].widthAt(3), 4.0);
    QVERIFY(!s.rules[2].appliesTo(11, mvt::GeomType::LineString));
    QVERIFY(s.rules[2].appliesTo(12, mvt::GeomType::LineString));
    QCOMPARE(s.rules[3].labelFields, QStringList{ QStringLiteral("name") });

    VectorStyle bad;
    QString error;
    QVERIFY(!VectorStyle::fromJson(QJsonObject(), &bad, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!VectorStyle::fromFile(p("no_existe.json"), &bad, &error));

    // "geometry" limita a un tipo.
    const QByteArray json = R"({ "rules": [ { "layer": "a", "geometry": "polygon", "fill": "red" } ] })";
    VectorStyle g;
    QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(json).object(), &g));
    QVERIFY(g.rules[0].appliesTo(5, mvt::GeomType::Polygon));
    QVERIFY(!g.rules[0].appliesTo(5, mvt::GeomType::LineString));
}

// Agua de fondo, autopista roja por el centro; el camino solo aparece desde z12.
void TstVectorTiles::rendersGeometryWithStyle()
{
    const VectorTileRenderer r(testStyle());
    QString error;
    const QImage z5 = r.render(waterAndMotorway(), 5, 256, &error);
    QVERIFY2(!z5.isNull(), qPrintable(error));
    QCOMPARE(z5.size(), QSize(256, 256));
    QVERIFY(sameColor(z5.pixel(10, 10), Qt::blue));          // agua
    QVERIFY(sameColor(z5.pixel(60, 128), Qt::red));          // autopista
    QVERIFY(sameColor(z5.pixel(60, 110), Qt::blue));         // fuera de su grosor (7 px)
    QVERIFY(sameColor(z5.pixel(128, 60), Qt::blue));         // camino: aún no

    const QImage z12 = r.render(zlib(waterAndMotorway()), 12, 256);
    QVERIFY(sameColor(z12.pixel(128, 60), Qt::yellow));      // camino desde z12

    // Una tesela vacía da solo el fondo; bytes inválidos, imagen nula.
    const QImage vacia = r.render(QByteArray(), 5, 256);
    QVERIFY(sameColor(vacia.pixel(128, 128), Qt::black));
    QVERIFY(r.render(QByteArray("basura!"), 5, 256, &error).isNull());
}

// Un rótulo junto al borde se desplaza para caber; uno del margen (fuera de la
// tesela, que pertenece a la vecina) no se dibuja.
void TstVectorTiles::labelsStayInsideTheirTile()
{
    if (QFontMetricsF(QFont()).horizontalAdvance(QStringLiteral("Habana")) <= 0)
        QSKIP("Sin fuentes en este sistema: no se pueden pintar rótulos");

    auto pointTile = [](int x, int y) {
        return tile({ layer(QStringLiteral("place"),
                            { { 1, { cmd(1, 1), zz(x), zz(y) },
                                { { QStringLiteral("name"), QStringLiteral("Habana") } } } }) });
    };
    auto greenPixels = [](const QImage &img, QRect *box) {
        int n = 0;
        QRect b;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qGreen(img.pixel(x, y)) > 100 && qRed(img.pixel(x, y)) < 100) {
                    ++n;
                    b |= QRect(x, y, 1, 1);
                }
        *box = b;
        return n;
    };

    const VectorTileRenderer r(testStyle());
    QRect box;
    QVERIFY(greenPixels(r.render(pointTile(2048, 2048), 10, 256), &box) > 0);
    QVERIFY(qAbs(box.center().x() - 128) < 8);

    // Ancla a 4 px del borde derecho: el texto entero, dentro, sin cortar.
    QVERIFY(greenPixels(r.render(pointTile(4030, 2048), 10, 256), &box) > 0);
    QVERIFY(box.right() < 255);
    QVERIFY(box.width() > 20);

    // Ancla en el margen (x > extent): es de la tesela vecina.
    QCOMPARE(greenPixels(r.render(pointTile(4200, 2048), 10, 256), &box), 0);
}

// Un MBTiles con format=pbf ya no se rechaza: se marca vectorial, a 256 px.
// (El que dice pbf pero trae PNG se sigue rechazando: lo prueba tst_mbtiles.)
void TstVectorTiles::mbtilesVectorIsDetected()
{
    const QString f = p("vector.mbtiles");
    QVERIFY(makeVectorMBTiles(f, waterAndMotorway(), 2));
    QString error;
    const TileDataset ds = MBTiles::fromFile(f, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(ds.isValid());
    QVERIFY(ds.mbtiles);
    QVERIFY(ds.vector);
    QCOMPARE(ds.tileSize, 256);
    QCOMPARE(ds.maxZoom, 2);
    QCOMPARE(ds.displayName, QStringLiteral("Vector de prueba"));

    // toJson / fromJson conservan la marca y el estilo.
    TileDataset copia = ds;
    copia.vectorStyleFile = QStringLiteral("/x/estilo.json");
    const TileDataset vuelta = TileDataset::fromJson(copia.toJson());
    QVERIFY(vuelta.vector);
    QCOMPARE(vuelta.vectorStyleFile, QStringLiteral("/x/estilo.json"));

    // Un ráster de siempre no se marca ni escribe nada nuevo en su JSON.
    TileDataset raster;
    raster.id = QStringLiteral("osm");
    QVERIFY(!raster.toJson().contains(QStringLiteral("vector")));
    QVERIFY(!raster.toJson().contains(QStringLiteral("vectorStyle")));
    QVERIFY(!TileDataset::fromJson(raster.toJson()).vector);
}

// Extremo a extremo: el TileLoader lee el MBTiles vectorial y entrega imágenes
// pintadas, con el estilo por defecto o con el de vectorStyleFile.
void TstVectorTiles::loaderPaintsVectorTiles()
{
    const QString f = p("loader.mbtiles");
    QVERIFY(makeVectorMBTiles(f, waterAndMotorway(), 1));
    TileDataset ds = MBTiles::fromFile(f, QStringLiteral("vec"));
    QVERIFY(ds.vector);

    auto loadAll = [](const TileDataset &d) {
        TileLoader loader;
        loader.configure({ d });
        QSignalSpy spy(&loader, &TileLoader::tilesLoaded);
        TileRequest req;
        req.datasetId = d.id;
        req.id = 1;
        TileMatrix::TileRange range;
        range.z = 1;
        range.xMin = 0; range.xMax = 1;
        range.yMin = 0; range.yMax = 1;
        req.ranges = { range };
        loader.load(req);
        loader.releaseResources();
        return spy.isEmpty() ? QVector<LoadedTile>()
                             : spy.first().at(3).value<QVector<LoadedTile>>();
    };

    // Color del agua en el estilo por defecto.
    const QColor agua(QStringLiteral("#a5c9ea"));
    const QVector<LoadedTile> porDefecto = loadAll(ds);
    QCOMPARE(porDefecto.size(), 4);
    for (const LoadedTile &t : porDefecto) {
        QCOMPARE(t.image.size(), QSize(256, 256));
        QVERIFY(sameColor(t.image.pixel(10, 10), agua));
    }

    // Estilo propio desde fichero.
    const QString estilo = p("estilo.json");
    {
        QFile out(estilo);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(R"({ "rules": [ { "layer": "water", "fill": "#00ff00" } ] })");
    }
    ds.vectorStyleFile = estilo;
    const QVector<LoadedTile> propio = loadAll(ds);
    QCOMPARE(propio.size(), 4);
    QVERIFY(sameColor(propio.first().image.pixel(10, 10), Qt::green));

    // Un estilo roto no deja la capa en blanco: se usa el de OpenMapTiles.
    ds.vectorStyleFile = p("no_existe.json");
    const QVector<LoadedTile> respaldo = loadAll(ds);
    QCOMPARE(respaldo.size(), 4);
    QVERIFY(sameColor(respaldo.first().image.pixel(10, 10), agua));
}

// datasets.json: "vectorStyle" relativo se resuelve respecto al propio JSON.
void TstVectorTiles::datasetsJsonResolvesStyle()
{
    QVERIFY(makeVectorMBTiles(p("cuba.mbtiles"), waterAndMotorway(), 1));
    QFile json(p("datasets.json"));
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write(R"({ "datasets": [ { "id": "vectorial", "filePath": "cuba.mbtiles",
                                    "vectorStyle": "estilo.json",
                                    "landMask": "costa/tierra.sqlitedb", "landLayer": "tierra" } ] })");
    json.close();

    QString error;
    const QVector<TileDataset> ds = TileService::loadDatasets(p("datasets.json"), &error);
    QCOMPARE(ds.size(), 1);
    QVERIFY(ds[0].vector);
    QCOMPARE(ds[0].vectorStyleFile, p("estilo.json"));
    QCOMPARE(ds[0].vectorLandMask, p("costa/tierra.sqlitedb"));
    QCOMPARE(ds[0].vectorLandLayer, QStringLiteral("tierra"));
    const TileDataset vuelta = TileDataset::fromJson(ds[0].toJson());
    QCOMPARE(vuelta.vectorLandMask, ds[0].vectorLandMask);
    QCOMPARE(vuelta.vectorLandLayer, QStringLiteral("tierra"));
    QCOMPARE(ds[0].maxZoom, 1);
}

// Un paquete (mapa.json) con una capa vectorial y su estilo: el estilo viaja con
// el paquete y check_data no confunde las teselas MVT con imágenes rotas.
void TstVectorTiles::packageWithVectorLayer()
{
    QDir().mkpath(p("paquete"));
    QVERIFY(makeVectorMBTiles(p("paquete/cuba.mbtiles"), waterAndMotorway(), 2));
    {
        QFile estilo(p("paquete/estilo.json"));
        QVERIFY(estilo.open(QIODevice::WriteOnly));
        estilo.write(VectorStyle::openMapTilesJson());
        QFile f(p("paquete/mapa.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({ "format": "libmapa-package", "version": 2,
                     "package": { "id": "vec", "name": "Vectorial",
                                  "bounds": { "north": 23.3, "west": -85.0, "south": 19.7, "east": -74.0 } },
                     "start": { "layer": "cuba" },
                     "datasets": [ { "id": "cuba", "filePath": "cuba.mbtiles",
                                     "vectorStyle": "estilo.json", "landMask": "no_esta.sqlitedb" } ] })");
    }

    QString error;
    const auto pkg = DataPackage::load(p("paquete"), &error);
    QVERIFY2(pkg.has_value(), qPrintable(error));
    QCOMPARE(pkg->datasets.size(), 1);
    QVERIFY(pkg->datasets[0].vector);
    QCOMPARE(pkg->datasets[0].vectorStyleFile, QFileInfo(p("paquete/estilo.json")).absoluteFilePath());
    QVERIFY(pkg->files().contains(QFileInfo(p("paquete/estilo.json")).absoluteFilePath()));

    PackageCheck::Options o;
    o.coverage = false;
    const PackageCheck r = PackageCheck::run(p("paquete"), o);
    QVERIFY2(!r.hasErrors(), qPrintable(r.problems().join(QLatin1Char('\n'))));
    QCOMPARE(r.datasets.size(), 1);
    QCOMPARE(r.datasets.first().imageFormat, QStringLiteral("mvt"));
    // La máscara que falta es un aviso (el mapa se ve, sin mar), no un error.
    QVERIFY2(r.problems().join(QLatin1Char(' ')).contains(QStringLiteral("mascara de tierra")),
             qPrintable(r.problems().join(QLatin1Char(' '))));
}

// Con máscara: fuera de la isla, mar; dentro, tierra; en la laguna (anillo
// interior), mar otra vez; y las reglas (el lago del MVT) encima de la tierra.
// Sin la tesela (variante solo con zoom) o sin máscara, todo como antes.
void TstVectorTiles::landMaskPaintsSea()
{
    const int z = 12, x = 1111, y = 1776;
    // Isla: del 20 % al 80 % de la tesela; laguna: del 40 % al 60 %.
    auto square = [&](double a, double b) {
        return QVector<QGeoCoordinate>{ tilePoint(z, x, y, a, a), tilePoint(z, x, y, b, a),
                                        tilePoint(z, x, y, b, b), tilePoint(z, x, y, a, b) };
    };
    const auto mask = LandMask::fromRings({ square(0.2, 0.8), square(0.4, 0.6) });
    QVERIFY(mask);
    QCOMPARE(mask->ringCount(), 2);
    QCOMPARE(mask->vertexCount(), qint64(8));
    QVERIFY(!LandMask::fromRings({ { tilePoint(z, x, y, 0, 0), tilePoint(z, x, y, 1, 1) } }));

    // Un lago del MVT en la esquina superior izquierda de la isla (25-35 %).
    const QVector<QPoint> lake{ { 1024, 1024 }, { 1434, 1024 }, { 1434, 1434 }, { 1024, 1434 } };
    const QByteArray bytes = tile({ layer(QStringLiteral("water"), { { 3, path(lake, true), {} } }) });

    VectorStyle style;
    QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(R"({ "background": "#00ff00", "sea": "#0000ff",
        "rules": [ { "layer": "water", "fill": "#ff0000" } ] })").object(), &style));
    QCOMPARE(style.sea, QColor(Qt::blue));
    VectorTileRenderer r(style);
    r.setLandMask(mask);
    QCOMPARE(r.landMask(), mask.get());

    const QImage img = r.render(bytes, TileKey{ z, x, y }, 256);
    QVERIFY(!img.isNull());
    QVERIFY(sameColor(img.pixel(10, 10), Qt::blue));         // mar
    QVERIFY(sameColor(img.pixel(245, 128), Qt::blue));
    QVERIFY(sameColor(img.pixel(70, 180), Qt::green));       // tierra
    QVERIFY(sameColor(img.pixel(128, 128), Qt::blue));       // laguna
    QVERIFY(sameColor(img.pixel(76, 76), Qt::red));          // lago del MVT, encima

    // La tesela vecina (toda mar) y una tesela vacía (gzip de nada) también.
    const QImage vecina = r.render(QByteArray(), TileKey{ z, x + 1, y }, 256);
    QVERIFY(sameColor(vecina.pixel(128, 128), Qt::blue));

    // Sin saber qué tesela es: el fondo de siempre.
    const QImage sinClave = r.render(bytes, z, 256);
    QVERIFY(sameColor(sinClave.pixel(10, 10), Qt::green));
}

// El recorte rápido de LandMask::paint (rachas fuera de la tesela reducidas a sus
// extremos, puntos de menos de 0,3 px fuera) tiene que pintar lo MISMO que el
// polígono entero: una «isla» dentada de 20 000 vértices que cruza la tesela por
// varios sitios, comparada píxel a píxel con QPainter sobre todos sus puntos.
void TstVectorTiles::landMaskClipMatchesFullPolygon()
{
    const int z = 14, x = 4443, y = 7110;
    const double pi = 3.14159265358979323846;
    QVector<QGeoCoordinate> ring;
    QPolygonF full;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * pi * i / n;
        // Radio de 0,25 a 0,85 teselas alrededor del centro: entra y sale de la tesela.
        const double rad = 0.55 + 0.28 * std::sin(7.0 * a) + 0.02 * std::sin(400.0 * a);
        const double fx = 0.5 + rad * std::cos(a), fy = 0.5 + rad * std::sin(a);
        ring.append(tilePoint(z, x, y, fx, fy));
        full.append(QPointF(fx * 256.0, fy * 256.0));
    }
    const auto mask = LandMask::fromRings({ ring });
    QVERIFY(mask);

    QImage fast(256, 256, QImage::Format_ARGB32_Premultiplied);
    fast.fill(Qt::black);
    {
        QPainter p(&fast);
        QVERIFY(mask->paint(p, z, x, y, 256, Qt::white));
    }
    QImage ref(256, 256, QImage::Format_ARGB32_Premultiplied);
    ref.fill(Qt::black);
    {
        QPainter p(&ref);
        QPainterPath pp;
        pp.setFillRule(Qt::OddEvenFill);
        pp.addPolygon(full);
        p.fillPath(pp, Qt::white);
    }
    int distintos = 0, blancos = 0;
    for (int j = 0; j < 256; ++j)
        for (int i = 0; i < 256; ++i) {
            const int a = qGray(fast.pixel(i, j)), b = qGray(ref.pixel(i, j));
            distintos += qAbs(a - b) > 64;
            blancos += a > 128;
        }
    QVERIFY2(blancos > 1000 && blancos < 256 * 256 - 1000, qPrintable(QString::number(blancos)));
    QVERIFY2(distintos < 60, qPrintable(QStringLiteral("%1 píxeles distintos").arg(distintos)));

    // Una tesela lejos (fuera del recuadro del anillo) no pinta nada.
    QPainter p(&fast);
    QVERIFY(!mask->paint(p, z, x + 50, y, 256, Qt::white));
}


// Grupos del estilo: los declarados con su título y en su orden, los que solo
// salen en una regla (título = id), y los que arrancan apagados ("visible": false).
void TstVectorTiles::styleGroups()
{
    VectorStyle s;
    QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(R"({
        "groups": [ { "id": "b", "title": "Be" }, { "id": "a", "title": "A", "visible": false } ],
        "rules": [ { "layer": "x", "group": "a", "fill": "red" },
                   { "layer": "y", "group": "c", "fill": "red" },
                   { "layer": "z", "fill": "red" } ] })").object(), &s));
    QCOMPARE(s.groupIds(), QStringList({ "b", "a", "c" }));
    QCOMPARE(s.groups[0].second, QStringLiteral("Be"));
    QCOMPARE(s.groups[2].second, QStringLiteral("c"));
    QCOMPARE(s.defaultHidden, QStringList{ QStringLiteral("a") });
    QCOMPARE(s.rules[2].group, QString());

    // El estilo por defecto ofrece estos grupos; los de puntos de interés por tema
    // (poi_*) arrancan apagados y los demás no. Sin números de portal: no se veían.
    const VectorStyle d = VectorStyle::openMapTiles();
    QCOMPARE(d.groupIds(), QStringList({ "nombres_calles", "lugares", "edificios", "cimas", "limites",
                                         "poi_salud", "poi_seguridad", "poi_combustible",
                                         "poi_alojamiento", "poi_transporte" }));
    QCOMPARE(d.defaultHidden, QStringList({ "poi_salud", "poi_seguridad", "poi_combustible",
                                            "poi_alojamiento", "poi_transporte" }));
    for (const VectorRule &r : d.rules)
        QVERIFY2(r.layer != QLatin1String("housenumber"), qPrintable(r.layer));
}

// Un grupo oculto no se pinta; lo que no tiene grupo (el agua) siempre sí.
void TstVectorTiles::hiddenGroupsAreNotDrawn()
{
    VectorStyle s;
    QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(R"({ "background": "#000000",
        "rules": [ { "layer": "water", "fill": "#0000ff" },
                   { "layer": "transportation", "group": "vias", "line": "#ff0000", "width": 8 } ] })").object(), &s));
    VectorTileRenderer r(s);
    QVERIFY(sameColor(r.render(waterAndMotorway(), 5, 256).pixel(60, 128), Qt::red));
    r.setHiddenGroups({ QStringLiteral("vias") });
    QCOMPARE(r.hiddenGroups(), QSet<QString>{ QStringLiteral("vias") });
    const QImage sin = r.render(waterAndMotorway(), 5, 256);
    QVERIFY(sameColor(sin.pixel(60, 128), Qt::blue));
    QVERIFY(sameColor(sin.pixel(10, 10), Qt::blue));
}

// El estilo híbrido es para ir ENCIMA de otra base: fondo transparente (sin
// agua ni tierra), las vías sí, y sus grupos (los POI por tema, apagados).
void TstVectorTiles::hybridStyleIsTransparent()
{
    QJsonParseError pe{};
    QVERIFY(!QJsonDocument::fromJson(VectorStyle::hybridJson(), &pe).isNull());
    const VectorStyle h = VectorStyle::hybrid();
    QCOMPARE(h.background.alpha(), 0);
    QCOMPARE(h.groupIds(), QStringList({ "vias", "nombres_calles", "lugares", "cimas", "limites",
                                         "poi_salud", "poi_seguridad", "poi_combustible",
                                         "poi_alojamiento", "poi_transporte" }));
    QCOMPARE(h.defaultHidden.size(), 5);                       // los POI por tema, apagados
    QCOMPARE(VectorStyle::defaultFor(true).name, h.name);
    QCOMPARE(VectorStyle::defaultFor(false).name, VectorStyle::openMapTiles().name);

    const QImage img = VectorTileRenderer(h).render(waterAndMotorway(), 12, 256);
    QCOMPARE(qAlpha(img.pixel(10, 10)), 0);                    // el agua no se pinta
    QVERIFY(qAlpha(img.pixel(60, 128)) > 100);                  // la autopista sí
}

// El nombre de una calle va A LO LARGO de ella: horizontal en una calle
// horizontal, vertical en una vertical, y nada si la calle es más corta que el texto.
void TstVectorTiles::namesFollowTheLine()
{
    if (QFontMetricsF(QFont()).horizontalAdvance(QStringLiteral("Obispo")) <= 0)
        QSKIP("Sin fuentes en este sistema: no se pueden pintar rótulos");

    VectorStyle s;
    QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(R"({ "background": "#000000",
        "rules": [ { "layer": "transportation_name", "label": "name", "textColor": "#00ff00",
                     "halo": "#00000000", "textSize": 14 } ] })").object(), &s));
    const VectorTileRenderer r(s);
    auto calle = [](QPoint a, QPoint b) {
        return tile({ layer(QStringLiteral("transportation_name"),
                            { { 2, path({ a, b }, false),
                                { { QStringLiteral("name"), QStringLiteral("Calle Obispo") } } } }) });
    };
    auto caja = [](const QImage &img) {
        QRect b;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qGreen(img.pixel(x, y)) > 100 && qRed(img.pixel(x, y)) < 100)
                    b |= QRect(x, y, 1, 1);
        return b;
    };

    const QRect h = caja(r.render(calle({ 200, 2048 }, { 3900, 2048 }), 15, 256));
    QVERIFY(!h.isEmpty());
    QVERIFY2(h.width() > 3 * h.height(), qPrintable(QStringLiteral("%1x%2").arg(h.width()).arg(h.height())));
    QVERIFY(qAbs(h.center().y() - 128) < 6);                   // sobre la calle

    const QRect v = caja(r.render(calle({ 2048, 200 }, { 2048, 3900 }), 15, 256));
    QVERIFY(!v.isEmpty());
    QVERIFY2(v.height() > 3 * v.width(), qPrintable(QStringLiteral("%1x%2").arg(v.width()).arg(v.height())));
    QVERIFY(qAbs(v.center().x() - 128) < 6);

    QVERIFY(caja(r.render(calle({ 2000, 2048 }, { 2300, 2048 }), 15, 256)).isEmpty());   // no cabe
}

// El TileLoader cambia los grupos ocultos en caliente: lo siguiente que pinta ya
// sale sin ellos (y con ellos al volver a encenderlos).
void TstVectorTiles::loaderChangesHiddenGroups()
{
    const QString f = p("grupos.mbtiles");
    QVERIFY(makeVectorMBTiles(f, waterAndMotorway(), 1));
    TileDataset ds = MBTiles::fromFile(f, QStringLiteral("g"));
    {
        QFile out(p("grupos.json"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(R"({ "background": "#000000", "rules": [ { "layer": "water", "group": "agua", "fill": "#0000ff" } ] })");
    }
    ds.vectorStyleFile = p("grupos.json");

    TileLoader loader;
    loader.configure({ ds });
    auto pinta = [&loader](quint64 id) {
        QSignalSpy spy(&loader, &TileLoader::tilesLoaded);
        TileRequest req;
        req.datasetId = QStringLiteral("g");
        req.id = id;
        TileMatrix::TileRange range;
        range.z = 1;
        range.xMax = 0;
        range.yMax = 0;
        req.ranges = { range };
        loader.load(req);
        const auto t = spy.first().at(3).value<QVector<LoadedTile>>();
        return t.isEmpty() ? QImage() : t.first().image;
    };
    QVERIFY(sameColor(pinta(1).pixel(10, 10), Qt::blue));
    loader.setVectorHiddenGroups(QStringLiteral("g"), { QStringLiteral("agua") });
    QVERIFY(sameColor(pinta(2).pixel(10, 10), Qt::black));
    loader.setVectorHiddenGroups(QStringLiteral("g"), {});
    QVERIFY(sameColor(pinta(3).pixel(10, 10), Qt::blue));
    loader.releaseResources();

    // "hiddenGroups" en el JSON del dataset, ida y vuelta.
    ds.vectorHiddenGroups = QStringList{ QStringLiteral("agua") };
    QCOMPARE(TileDataset::fromJson(ds.toJson()).vectorHiddenGroups, ds.vectorHiddenGroups);

    // Un grupo con "visible": false en el estilo arranca apagado también en una
    // capa BASE, salvo que el dataset diga sus "hiddenGroups" (aunque sea []).
    {
        QFile out(p("grupos.json"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(R"({ "groups": [ { "id": "agua", "title": "Agua", "visible": false } ],
                       "rules": [ { "layer": "water", "group": "agua", "fill": "#0000ff" } ] })");
    }
    TileDataset sinLista = MBTiles::fromFile(f, QStringLiteral("a"));
    sinLista.vectorStyleFile = p("grupos.json");
    TileDataset conLista = MBTiles::fromFile(f, QStringLiteral("b"));
    conLista.vectorStyleFile = p("grupos.json");
    conLista.vectorHiddenGroupsSet = true;                      // "hiddenGroups": []
    TileService service;
    QVERIFY(service.start({ sinLista, conLista }, 16));
    QCOMPARE(service.dataset(QStringLiteral("a"))->vectorHiddenGroups, QStringList{ QStringLiteral("agua") });
    QVERIFY(service.dataset(QStringLiteral("b"))->vectorHiddenGroups.isEmpty());
}


// "vectorOverlay" en mapa.json: el MBTiles vectorial que va encima de la base
// (mapa híbrido), con rutas relativas, sus grupos y si arranca visible. Un
// overlay ráster o que no existe se avisa y no se pone.
void TstVectorTiles::packageDeclaresHybridOverlay()
{
    QDir().mkpath(p("hib"));
    QVERIFY(makeVectorMBTiles(p("hib/calles.mbtiles"), waterAndMotorway(), 1));
    QVERIFY(makeVectorMBTiles(p("hib/base.mbtiles"), waterAndMotorway(), 1));
    auto escribe = [this](const QByteArray &overlay) {
        QFile f(p("hib/mapa.json"));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(R"({ "format": "libmapa-package", "version": 2, "package": { "id": "h", "name": "H" },
                     "datasets": [ { "id": "base", "filePath": "base.mbtiles" } ],
                     "vectorOverlay": )" + overlay + " }");
        return true;
    };

    QVERIFY(escribe(R"({ "filePath": "calles.mbtiles", "hiddenGroups": ["poi"], "visible": false })"));
    QString error;
    auto pkg = DataPackage::load(p("hib"), &error);
    QVERIFY2(pkg.has_value(), qPrintable(error));
    QVERIFY(pkg->vectorOverlay.has_value());
    QVERIFY(pkg->vectorOverlay->dataset.vector);
    QVERIFY(pkg->vectorOverlay->dataset.vectorOverlay);
    QCOMPARE(pkg->vectorOverlay->dataset.id, QStringLiteral("hibrido"));
    QVERIFY(!pkg->vectorOverlay->visible);
    QVERIFY(pkg->vectorOverlay->hiddenGroupsSet);
    QCOMPARE(pkg->vectorOverlay->dataset.vectorHiddenGroups, QStringList{ QStringLiteral("poi") });
    QVERIFY(pkg->files().contains(QFileInfo(p("hib/calles.mbtiles")).absoluteFilePath()));

    QVERIFY(escribe(R"({ "filePath": "no_esta.mbtiles" })"));
    pkg = DataPackage::load(p("hib"), &error);
    QVERIFY(pkg.has_value());
    QVERIFY(!pkg->vectorOverlay.has_value());
    QVERIFY(pkg->warnings.join(QLatin1Char(' ')).contains(QStringLiteral("vectorOverlay")));
}

// «Qué hay aquí»: un edificio con patio, una calle con nombre y un hospital, con la
// geometría puesta a mano en la tesela z12 x1111 y1776. Se consulta cada sitio por
// su posición geográfica (la misma conversión que haría un clic): dentro del
// edificio, en el patio (no hay nada), junto a la calle, junto al hospital y lejos
// de todo. Las distancias se comprueban en metros, no en unidades de tesela.
void TstVectorTiles::queryFindsWhatIsHere()
{
    const int z = 12, x = 1111, y = 1776;
    const TileKey key{ z, x, y };
    // Anillo exterior (área positiva) y patio (negativa), en una sola entidad.
    QVector<quint32> edificio = path({ { 1000, 1000 }, { 2000, 1000 }, { 2000, 2000 }, { 1000, 2000 } }, true);
    // El MoveTo de cada anillo es relativo al cursor, que tras el primero queda en (1000, 2000).
    QVector<quint32> patio = path({ { 1400, 1400 }, { 1400, 1600 }, { 1600, 1600 }, { 1600, 1400 } }, true);
    patio[1] = zz(1400 - 1000);
    patio[2] = zz(1400 - 2000);
    edificio += patio;
    const QByteArray bytes = tile({
        layer(QStringLiteral("building"),
              { { 3, edificio, { { QStringLiteral("render_height"), QStringLiteral("12") } } } }),
        layer(QStringLiteral("transportation"),
              { { 2, path({ { 0, 3000 }, { 4096, 3000 } }, false),
                  { { QStringLiteral("class"), QStringLiteral("primary") },
                    { QStringLiteral("name"), QStringLiteral("Obispo") } } } }),
        layer(QStringLiteral("poi"),
              { { 1, QVector<quint32>{ cmd(1, 1), zz(3000), zz(500) },
                  { { QStringLiteral("class"), QStringLiteral("hospital") } } } }) });
    mvt::Tile t;
    QVERIFY(mvt::decode(bytes, &t));

    auto aqui = [&](double ux, double uy, double radio) {
        const QGeoCoordinate c = tilePoint(z, x, y, ux / 4096.0, uy / 4096.0);
        return featurequery::query(t, key, c.latitude(), c.longitude(), radio);
    };

    // Dentro del edificio, fuera del patio: el polígono, a distancia 0, con su altura.
    auto h = aqui(1200, 1200, 5.0);
    QCOMPARE(h.size(), 1);
    QCOMPARE(h[0].layer, QStringLiteral("building"));
    QCOMPARE(h[0].type, mvt::GeomType::Polygon);
    QCOMPARE(h[0].distanceM, 0.0);
    QCOMPARE(h[0].attributes.value(QStringLiteral("render_height")).toString(), QStringLiteral("12"));

    // En medio del patio: no está «dentro»; el borde queda a ~100 unidades (a z12 son unos 2,2 m cada una).
    QVERIFY(aqui(1500, 1500, 5.0).isEmpty());
    h = aqui(1500, 1500, 500.0);
    QCOMPARE(h.size(), 1);
    QVERIFY(h[0].distanceM > 150.0 && h[0].distanceM < 300.0);

    // Junto a la calle (5 unidades al norte de ella, unos 11 m): la línea, con su nombre y su distancia real.
    h = aqui(500, 2995, 30.0);
    QCOMPARE(h.size(), 1);
    QCOMPARE(h[0].layer, QStringLiteral("transportation"));
    QCOMPARE(h[0].attributes.value(QStringLiteral("name")).toString(), QStringLiteral("Obispo"));
    QVERIFY2(h[0].distanceM > 5.0 && h[0].distanceM < 20.0, qPrintable(QString::number(h[0].distanceM)));

    // Junto al hospital.
    h = aqui(3003, 503, 30.0);
    QCOMPARE(h.size(), 1);
    QCOMPARE(h[0].type, mvt::GeomType::Point);
    QCOMPARE(h[0].attributes.value(QStringLiteral("class")).toString(), QStringLiteral("hospital"));

    // Lejos de todo.
    QVERIFY(aqui(3500, 3500, 5.0).isEmpty());

    // Con radio grande salen varias; el polígono que contiene el punto va primero.
    h = aqui(1200, 1200, 5000.0);
    QVERIFY(h.size() >= 3);
    QCOMPARE(h[0].layer, QStringLiteral("building"));
}

// Puntos de interés por tema: los dos estilos por defecto traen cinco grupos poi_*
// (salud, seguridad, combustible, alojamiento, transporte), apagados de partida, cada
// uno con su color y una forma de marcador distinta y visibles desde z13; y las cinco
// formas se dibujan de verdad, distintas entre sí (se prueba sin rótulos, que
// necesitan fuentes: un punto sin "label" se dibuja tal cual).
void TstVectorTiles::poiGroupsAndShapes()
{
    for (const VectorStyle &st : { VectorStyle::openMapTiles(), VectorStyle::hybrid() }) {
        QSet<int> formas;
        QSet<QRgb> colores;
        int temas = 0;
        for (const VectorRule &r : st.rules) {
            if (!r.group.startsWith(QLatin1String("poi_")))
                continue;
            ++temas;
            QCOMPARE(r.layer, QStringLiteral("poi"));
            QVERIFY(r.minZoom <= 14);
            QVERIFY(r.pointRadius > 0);
            QVERIFY(st.defaultHidden.contains(r.group));
            formas.insert(int(r.pointShape));
            colores.insert(r.pointColor.rgba());
        }
        QCOMPARE(temas, 5);
        QCOMPARE(formas.size(), 5);                          // un símbolo distinto por tema
        QCOMPARE(colores.size(), 5);                         // y un color distinto
    }

    // Una forma desconocida se queda en círculo; las cinco se leen del JSON.
    const QList<QPair<QString, PointShape>> casos = {
        { "circle", PointShape::Circle }, { "square", PointShape::Square },
        { "triangle", PointShape::Triangle }, { "diamond", PointShape::Diamond },
        { "cross", PointShape::Cross }, { "estrella", PointShape::Circle } };
    QVector<QImage> imagenes;
    for (const auto &c : casos) {
        VectorStyle s;
        const QByteArray json = QStringLiteral(R"({ "background": "#000000", "rules": [
            { "layer": "poi", "pointRadius": 6, "pointColor": "#ff0000", "halo": "#00000000",
              "pointShape": "%1", "geometry": "point", "label": "name" } ] })").arg(c.first).toUtf8();
        QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(json).object(), &s));
        QCOMPARE(s.rules[0].pointShape, c.second);
    }
    // Dibujo: sin "label" el punto se pinta directamente (con rótulo haría falta un nombre y fuentes).
    const QByteArray poi = tile({ layer(QStringLiteral("poi"),
        { { 1, QVector<quint32>{ cmd(1, 1), zz(2048), zz(2048) },
            { { QStringLiteral("class"), QStringLiteral("x") } } } }) });
    for (const QString forma : { "circle", "square", "triangle", "diamond", "cross" }) {
        VectorStyle s;
        const QByteArray json = QStringLiteral(R"({ "background": "#000000", "rules": [
            { "layer": "poi", "pointRadius": 6, "pointColor": "#ff0000", "halo": "#00000000",
              "pointShape": "%1", "geometry": "point", "fill": "#00000000" } ] })").arg(forma).toUtf8();
        QVERIFY(VectorStyle::fromJson(QJsonDocument::fromJson(json).object(), &s));
        imagenes << VectorTileRenderer(s).render(poi, 15, 256);
    }
    for (const QImage &im : imagenes)
        QVERIFY(sameColor(im.pixel(128, 128), Qt::red));     // todas pintan el centro
    // En la esquina (5, 5) solo el cuadrado llega; el círculo, el rombo y la cruz no.
    QVERIFY(sameColor(imagenes[1].pixel(133, 133), Qt::red));
    QVERIFY(!sameColor(imagenes[0].pixel(133, 133), Qt::red));
    QVERIFY(!sameColor(imagenes[3].pixel(133, 133), Qt::red));
    QVERIFY(!sameColor(imagenes[4].pixel(133, 133), Qt::red));
    // La cruz llega por sus brazos (5, 0) y el rombo también; el triángulo apunta arriba.
    QVERIFY(sameColor(imagenes[4].pixel(133, 128), Qt::red));
    QVERIFY(sameColor(imagenes[2].pixel(128, 123), Qt::red));
    // Y no hay dos formas iguales.
    for (int i = 0; i < imagenes.size(); ++i)
        for (int j = i + 1; j < imagenes.size(); ++j)
            QVERIFY2(imagenes[i] != imagenes[j], qPrintable(QStringLiteral("%1 = %2").arg(i).arg(j)));
}

// Una tesela de ciudad trae miles de entidades de capas que no interesan: decode con
// lista de capas se salta las demás (y sigue dando error con un MVT roto).
void TstVectorTiles::decodeOnlyChosenLayers()
{
    const QByteArray bytes = waterAndMotorway();
    mvt::Tile todas, solo;
    QVERIFY(mvt::decode(bytes, &todas));
    QCOMPARE(todas.layers.size(), 2);
    QVERIFY(mvt::decode(bytes, &solo, nullptr, QStringList{ QStringLiteral("transportation") }));
    QCOMPARE(solo.layers.size(), 1);
    QCOMPARE(solo.layers[0].name, QStringLiteral("transportation"));
    QCOMPARE(solo.layers[0].features.size(), todas.layer(QStringLiteral("transportation"))->features.size());
    QVERIFY(mvt::decode(bytes, &solo, nullptr, QStringList{ QStringLiteral("no_existe") }));
    QVERIFY(solo.layers.isEmpty());
    QVERIFY(!mvt::decode(QByteArray("\xff\xff\xff"), &solo, nullptr, QStringList{ QStringLiteral("water") }));
}

// Buscar puntos de interés: cuatro POI puestos a mano en una tesela z14 (dos farmacias,
// una con nombre y otra sin él; un hospital; una gasolinera). Se busca por nombre sin
// tildes, por tipo en español y en inglés, por palabras de uso común ("cupet"), y se
// comprueba el orden (los que empiezan por lo escrito, luego los más cercanos) y que
// un texto de una letra no devuelva nada.
void TstVectorTiles::searchesPointsOfInterest()
{
    const int z = 14, x = 4000, y = 7000;
    const TileKey key{ z, x, y };
    auto punto = [](int px, int py) { return QVector<quint32>{ cmd(1, 1), zz(px), zz(py) }; };
    const QByteArray bytes = tile({ layer(QStringLiteral("poi"), {
        { 1, punto(1000, 1000), { { QStringLiteral("class"), QStringLiteral("pharmacy") },
                                  { QStringLiteral("name"), QStringLiteral("Farmacia Central") } } },
        { 1, punto(3000, 3000), { { QStringLiteral("class"), QStringLiteral("pharmacy") } } },
        { 1, punto(2000, 500), { { QStringLiteral("class"), QStringLiteral("hospital") },
                                 { QStringLiteral("name"), QStringLiteral("Hospital Calixto García") } } },
        { 1, punto(500, 2500), { { QStringLiteral("class"), QStringLiteral("fuel") },
                                 { QStringLiteral("name:es"), QStringLiteral("Servicentro Oro Negro") } } },
        { 1, punto(-50, 100), { { QStringLiteral("class"), QStringLiteral("bank") } } },   // del margen: no cuenta
        { 2, path({ { 0, 0 }, { 100, 100 } }, false), { { QStringLiteral("class"), QStringLiteral("pharmacy") } } },
    }) });
    mvt::Tile t;
    QVERIFY(mvt::decode(bytes, &t, nullptr, QStringList{ QStringLiteral("poi") }));
    const QVector<PointOfInterest> todos = poisearch::inTile(t, key);
    QCOMPARE(todos.size(), 4);                                   // sin la línea ni el punto del margen
    const QGeoCoordinate centro = tilePoint(z, x, y, 0.5, 0.5);
    for (const PointOfInterest &p : todos)
        QVERIFY2(qAbs(p.coordinate.latitude() - centro.latitude()) < 0.1, "posición fuera de la tesela");

    // Por nombre, sin tildes ni mayúsculas, y con varias palabras.
    auto r = poisearch::search(todos, QStringLiteral("calixto garcia"), centro, 10);
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].name, QStringLiteral("Hospital Calixto García"));
    QCOMPARE(r[0].typeLabel(), QStringLiteral("hospital"));
    QVERIFY(r[0].distanceM > 0.0);
    QCOMPARE(poisearch::search(todos, QStringLiteral("oro negro"), centro, 10).size(), 1);   // name:es

    // Por tipo: en español, en inglés y con una palabra de uso común; salen también las sin nombre.
    r = poisearch::search(todos, QStringLiteral("farmacia"), centro, 10);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].name, QStringLiteral("Farmacia Central"));     // casa por nombre, va antes
    QVERIFY(r[1].name.isEmpty());
    QCOMPARE(r[1].displayName(), QStringLiteral("Farmacia"));
    QCOMPARE(poisearch::search(todos, QStringLiteral("pharmacy"), centro, 10).size(), 2);
    QCOMPARE(poisearch::search(todos, QStringLiteral("cupet"), centro, 10).size(), 1);
    QCOMPARE(poisearch::search(todos, QStringLiteral("gasolinera"), centro, 10).first().name,
             QStringLiteral("Servicentro Oro Negro"));

    // Nombre + tipo: las dos palabras tienen que casar.
    QCOMPARE(poisearch::search(todos, QStringLiteral("hospital calixto"), centro, 10).size(), 1);
    QCOMPARE(poisearch::search(todos, QStringLiteral("farmacia calixto"), centro, 10).size(), 0);

    // Orden por cercanía dentro de un mismo grupo, y el límite.
    const QGeoCoordinate cerca = poisearch::search(todos, QStringLiteral("farmacia"), centro, 10).first().coordinate;
    r = poisearch::search(todos, QStringLiteral("farmacia"), cerca, 10);
    QCOMPARE(r.first().distanceM, 0.0);
    QCOMPARE(poisearch::search(todos, QStringLiteral("farmacia"), centro, 1).size(), 1);

    // Muy poco texto, o nada: no se devuelve todo.
    QVERIFY(poisearch::search(todos, QStringLiteral("a"), centro, 10).isEmpty());
    QVERIFY(poisearch::search(todos, QString(), centro, 10).isEmpty());
    QVERIFY(poisearch::search(todos, QStringLiteral("zzzz"), centro, 10).isEmpty());
}

// Buscar calles: tres tesela z14 puestas a mano. «Obispo» corre por la tesela A y sigue en
// la vecina B (la unión de teselas la parte en dos tramos que se tocan: debe salir UNA calle);
// «Calle 5» aparece en A y en una tesela C lejana (dos barrios: debe salir DOS veces, la más
// cercana primero); y una carretera solo con referencia. Se busca sin tildes, con la palabra
// genérica «calle», por referencia, y se comprueba el nombre en español y que un texto corto no
// devuelva nada.
void TstVectorTiles::searchesStreets()
{
    const int z = 14, x = 4000, y = 7000;
    auto linea = [](const QVector<QPoint> &pts) { return path(pts, false); };
    auto props = [](const char *nombre, const char *clase, const char *ref = nullptr) {
        QVector<QPair<QString, QString>> p;
        if (nombre && *nombre) p.append({ QStringLiteral("name"), QString::fromUtf8(nombre) });
        p.append({ QStringLiteral("class"), QString::fromLatin1(clase) });
        if (ref) p.append({ QStringLiteral("ref"), QString::fromLatin1(ref) });
        return p;
    };
    // Tesela A: Obispo de borde a borde, y una Calle 5 corta al norte; una vía de otro tipo sin nombre ni ref.
    const QByteArray tileA = tile({ layer(QStringLiteral("transportation_name"), {
        { 2, linea({ { 0, 2048 }, { 4096, 2048 } }), props("Obispo", "minor") },
        { 2, linea({ { 1000, 500 }, { 2000, 500 } }), props("Calle 5", "residential") },
        { 2, linea({ { 0, 3000 }, { 4096, 3000 } }), props("", "service") },
        { 2, linea({ { 500, 3500 }, { 900, 3500 } }), props("Línea", "tertiary") },
        { 1, QVector<quint32>{ cmd(1, 1), zz(100), zz(100) }, props("Un punto no es calle", "minor") },
    }) });
    // Tesela B (a la derecha de A): Obispo continúa desde el borde izquierdo; y una carretera con ref.
    const QByteArray tileB = tile({ layer(QStringLiteral("transportation_name"), {
        { 2, linea({ { 0, 2048 }, { 2000, 2048 } }), props("Obispo", "minor") },
        { 2, linea({ { 0, 1000 }, { 4096, 1000 } }), props("", "primary", "CC") },
    }) });
    // Tesela C (lejos): otra Calle 5.
    const QByteArray tileC = tile({ layer(QStringLiteral("transportation_name"), {
        { 2, linea({ { 1000, 500 }, { 2000, 500 } }), props("Calle 5", "residential") },
    }) });

    QVector<streetsearch::Segment> todos;
    for (const auto &c : { qMakePair(tileA, TileKey{ z, x, y }), qMakePair(tileB, TileKey{ z, x + 1, y }),
                           qMakePair(tileC, TileKey{ z, x + 30, y }) }) {
        mvt::Tile t;
        QVERIFY(mvt::decode(c.first, &t, nullptr, QStringList{ QStringLiteral("transportation_name") }));
        todos += streetsearch::inTile(t, c.second);
    }
    QCOMPARE(todos.size(), 3 + 2 + 1);                         // sin el punto ni la vía sin nombre ni referencia

    const QGeoCoordinate centro = tilePoint(z, x, y, 0.5, 0.5);

    // Obispo: dos tramos en dos teselas que se tocan en el borde → UNA calle con dos partes.
    auto r = streetsearch::search(todos, QStringLiteral("obispo"), centro, 10);
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].name, QStringLiteral("Obispo"));
    QCOMPARE(r[0].typeLabel(), QStringLiteral("calle"));
    QCOMPARE(r[0].parts.size(), 2);
    QVERIFY(r[0].distanceM < 5.0);                             // pasa por el centro de A
    // Cubre de A a la mitad de B: ~1,5 tesela de ancho (unos 3 km a esta latitud).
    QVERIFY(r[0].northWest.longitude() < r[0].southEast.longitude());
    QVERIFY(r[0].northWest.distanceTo(r[0].southEast) > 2500.0);

    // Palabra genérica opcional, mayúsculas y tildes sin importar.
    QCOMPARE(streetsearch::search(todos, QStringLiteral("CALLE Obispo"), centro, 10).size(), 1);
    QCOMPARE(streetsearch::search(todos, QStringLiteral("linea"), centro, 10).first().name, QStringLiteral("Línea"));
    QCOMPARE(streetsearch::search(todos, QStringLiteral("avenida linea"), centro, 10).size(), 1);
    // Pero solo la genérica no casa con todo: «calle» busca «calle» (Calle 5).
    QCOMPARE(streetsearch::search(todos, QStringLiteral("calle"), centro, 10).size(), 2);

    // Dos «Calle 5» lejanas son dos calles; la de A (más cercana) primero.
    r = streetsearch::search(todos, QStringLiteral("calle 5"), centro, 10);
    QCOMPARE(r.size(), 2);
    QVERIFY(r[0].distanceM < r[1].distanceM);
    QVERIFY(r[1].distanceM > 50000.0);
    QCOMPARE(r[0].parts.size(), 1);
    QCOMPARE(streetsearch::search(todos, QStringLiteral("calle 5"), centro, 1).size(), 1);       // límite

    // Por referencia (una carretera sin nombre).
    r = streetsearch::search(todos, QStringLiteral("cc"), centro, 10);
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].name, QStringLiteral("CC"));
    QCOMPARE(r[0].ref, QStringLiteral("CC"));
    QCOMPARE(r[0].typeLabel(), QStringLiteral("carretera principal"));

    // Lo que empieza por lo escrito va antes que lo que solo lo contiene.
    QVector<streetsearch::Segment> dos = todos;
    streetsearch::Segment extra;
    extra.name = QStringLiteral("Paseo del Obispo Viejo");
    extra.type = QStringLiteral("tertiary");
    extra.line = { centro, tilePoint(z, x, y, 0.55, 0.5) };
    dos.append(extra);
    r = streetsearch::search(dos, QStringLiteral("obispo"), centro, 10);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].name, QStringLiteral("Obispo"));             // empieza por lo escrito
    QCOMPARE(r[1].name, QStringLiteral("Paseo del Obispo Viejo"));

    // Poco texto o nada que casar: nada.
    QVERIFY(streetsearch::search(todos, QStringLiteral("o"), centro, 10).isEmpty());
    QVERIFY(streetsearch::search(todos, QString(), centro, 10).isEmpty());
    QVERIFY(streetsearch::search(todos, QStringLiteral("zzzz"), centro, 10).isEmpty());
    QVERIFY(streetsearch::search(todos, QStringLiteral("un punto"), centro, 10).isEmpty());      // los puntos no son calles
}

QTEST_MAIN(TstVectorTiles)
#include "tst_vectortiles.moc"
