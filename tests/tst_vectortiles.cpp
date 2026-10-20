#include "io/DataPackage.h"
#include "io/PackageCheck.h"
#include "tiles/MBTiles.h"
#include "tiles/TileDataset.h"
#include "tiles/TileLoader.h"
#include "tiles/TileService.h"
#include "vector/Gzip.h"
#include "vector/MvtTile.h"
#include "vector/VectorStyle.h"
#include "vector/VectorTileRenderer.h"

#include <QFile>
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
                                    "vectorStyle": "estilo.json" } ] })");
    json.close();

    QString error;
    const QVector<TileDataset> ds = TileService::loadDatasets(p("datasets.json"), &error);
    QCOMPARE(ds.size(), 1);
    QVERIFY(ds[0].vector);
    QCOMPARE(ds[0].vectorStyleFile, p("estilo.json"));
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
                                     "vectorStyle": "estilo.json" } ] })");
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
}

QTEST_MAIN(TstVectorTiles)
#include "tst_vectortiles.moc"
