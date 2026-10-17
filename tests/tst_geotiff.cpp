#include "GeoTiffWriter.h"

#include "dem/ElevationSources.h"
#include "dem/GeoTiff.h"
#include "dem/GeoTiffElevation.h"
#include "dem/HgtElevation.h"
#include "dem/SqliteElevation.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <memory>

using namespace libmapa;
using namespace libmapa::test;

// Pruebas del lector de GeoTIFF y de la fuente de elevación que lo usa. Se
// fabrican ficheros sintéticos con todas las variantes que debe entender
// (franjas/teselas, TIFF/BigTIFF, orden de bytes, compresiones, predictores,
// tipos de muestra, planos) y se comprueba que cada muestra sale exacta.
class TstGeoTiff : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void everyVariantReadsExactly_data();
    void everyVariantReadsExactly();
    void filesFromAnIndependentLibrary_data();
    void filesFromAnIndependentLibrary();
    void georeference();
    void pixelIsPointAndTransformation();
    void interpolationAndBorders();
    void noData();
    void webMercator();
    void planarBands();
    void rejectsWhatItCannotRead();
    void largeTiledImage();
    void sharedCacheStaysBounded();
    void folderOfTiles();
    void factoryPicksTheRightSource();

private:
    // Valor sintético exacto en cada formato (cabe y se representa sin pérdida).
    static double valueFor(int bps, int format, int col, int row, int band);

    QTemporaryDir m_dir;
    int m_n = 0;
    QString tmp(const char *name) { return m_dir.path() + QStringLiteral("/%1_%2.tif").arg(name).arg(++m_n); }
};

// Prepara el directorio temporal.
void TstGeoTiff::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

// Valor de la muestra (col,row,band) según el formato: enteros y flotantes que
// cada tipo guarda sin redondear.
double TstGeoTiff::valueFor(int bps, int format, int col, int row, int band)
{
    if (format == 3)
        return row * 0.5 + col * 0.25 + band * 1000.0;
    if (bps == 8)
        return (col * 7 + row * 3 + band * 11) % 251;
    if (bps == 16 && format == 1)
        return col * 300 + row + band;
    if (bps == 16)
        return row * 100 + col - 500 + band;
    return row * 100000 + col - 40000 + band;
}

// Filas: formato, compresión, predictor, teselas, BigTIFF y orden de bytes.
void TstGeoTiff::everyVariantReadsExactly_data()
{
    QTest::addColumn<int>("bps");
    QTest::addColumn<int>("format");
    QTest::addColumn<int>("compression");
    QTest::addColumn<int>("predictor");
    QTest::addColumn<bool>("tiled");
    QTest::addColumn<bool>("bigTiff");
    QTest::addColumn<bool>("bigEndian");

    struct Row { const char *name; int bps, fmt, comp, pred; bool tiled, big, be; };
    const Row rows[] = {
        {"int16 sin comprimir franjas LE",  16, 2, 1, 1, false, false, false},
        {"int16 sin comprimir franjas BE",  16, 2, 1, 1, false, false, true},
        {"int16 teselas LE",                16, 2, 1, 1, true,  false, false},
        {"int16 LZW franjas",               16, 2, 5, 1, false, false, false},
        {"int16 LZW predictor2 BE",         16, 2, 5, 2, false, false, true},
        {"int16 Deflate teselas",           16, 2, 8, 1, true,  false, false},
        {"int16 Deflate predictor2",        16, 2, 8, 2, false, false, false},
        {"int16 Deflate predictor2 BE",     16, 2, 8, 2, true,  false, true},
        {"int16 PackBits",                  16, 2, 32773, 1, false, false, false},
        {"uint16 Deflate",                  16, 1, 8, 1, false, false, false},
        {"uint8 LZW",                        8, 1, 5, 1, true,  false, false},
        {"uint8 PackBits predictor2",        8, 1, 32773, 2, false, false, false},
        {"int32 Deflate predictor2",        32, 2, 8, 2, false, false, false},
        {"int32 LZW BE",                    32, 2, 5, 1, true,  false, true},
        {"float32 sin comprimir",           32, 3, 1, 1, false, false, false},
        {"float32 Deflate predictor3",      32, 3, 8, 3, false, false, false},
        {"float32 LZW predictor3 BE",       32, 3, 5, 3, true,  false, true},
        {"float64 Deflate predictor3",      64, 3, 8, 3, true,  false, false},
        {"float64 sin comprimir BE",        64, 3, 1, 1, false, false, true},
        {"BigTIFF int16 Deflate",           16, 2, 8, 1, false, true,  false},
        {"BigTIFF float32 teselas BE",      32, 3, 8, 3, true,  true,  true},
    };
    for (const Row &r : rows)
        QTest::newRow(r.name) << r.bps << r.fmt << r.comp << r.pred << r.tiled << r.big << r.be;
}

// Cada muestra del fichero sale exactamente como se escribió, con cualquier
// combinación de las anteriores. Usa una imagen que NO es múltiplo del bloque
// para probar los bordes.
void TstGeoTiff::everyVariantReadsExactly()
{
    QFETCH(int, bps);
    QFETCH(int, format);
    QFETCH(int, compression);
    QFETCH(int, predictor);
    QFETCH(bool, tiled);
    QFETCH(bool, bigTiff);
    QFETCH(bool, bigEndian);

    TiffSpec s;
    s.width = 37;
    s.height = 29;
    s.bps = bps;
    s.sampleFormat = format;
    s.compression = compression;
    s.predictor = predictor;
    s.tiled = tiled;
    s.tile = 16;
    s.rowsPerStrip = 5;
    s.bigTiff = bigTiff;
    s.bigEndian = bigEndian;
    const QString path = tmp("variante");
    QVERIFY(writeGeoTiff(path, s, [&](int c, int r, int b) { return valueFor(bps, format, c, r, b); }));

    GeoTiffImage img;
    QString error;
    QVERIFY2(img.open(path, &error), qPrintable(error));
    QCOMPARE(img.width(), 37);
    QCOMPARE(img.height(), 29);
    for (int r = 0; r < s.height; ++r)
        for (int c = 0; c < s.width; ++c)
            QCOMPARE(img.sample(c, r), valueFor(bps, format, c, r, 0));
}

// Ficheros reales de tests/data/geotiff, escritos con tifffile+imagecodecs (una
// implementación independiente de la nuestra): así el lector no solo coincide
// con el escritor de estos tests, sino con lo que produce otra herramienta.
// Cada uno es de 40x30 y su contenido sale de una fórmula.
void TstGeoTiff::filesFromAnIndependentLibrary_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("kind");        // 0 int16, 1 float32, 2 uint8
    for (const char *n : { "lzw_pred2_int16_tiled", "deflate_pred2_int16_strips",
                           "bigtiff_deflate_int16" })
        QTest::newRow(n) << QString::fromLatin1(n) << 0;
    for (const char *n : { "deflate_pred3_float32", "lzw_pred3_float32_be" })
        QTest::newRow(n) << QString::fromLatin1(n) << 1;
    QTest::newRow("packbits_uint8") << QStringLiteral("packbits_uint8") << 2;
}

// Lee uno de esos ficheros y compara las 1200 muestras con la fórmula.
void TstGeoTiff::filesFromAnIndependentLibrary()
{
    QFETCH(QString, file);
    QFETCH(int, kind);
    GeoTiffImage img;
    QString error;
    const QString path = QStringLiteral(LIBMAPA_TESTDATA) + QStringLiteral("/geotiff/") + file
                         + QStringLiteral(".tif");
    QVERIFY2(img.open(path, &error), qPrintable(error));
    QCOMPARE(img.width(), 40);
    QCOMPARE(img.height(), 30);
    QCOMPARE(img.lonMin(), -77.0);
    QCOMPARE(img.latMax(), 20.0);
    for (int r = 0; r < 30; ++r)
        for (int c = 0; c < 40; ++c) {
            const double expected = kind == 0 ? double((c * 7 + r * 13) % 3000 - 1000)
                                  : kind == 1 ? r * 0.5 + c * 0.25
                                              : double((c * 7 + r * 3) % 251);
            QCOMPARE(img.sample(c, r), expected);
        }
}

// Extensión y posición: el píxel (0,0) cubre desde el punto de enlace.
void TstGeoTiff::georeference()
{
    TiffSpec s;
    s.width = 20; s.height = 10;
    s.originX = -80.0; s.originY = 25.0; s.dx = 0.5; s.dy = 0.25;
    const QString path = tmp("georef");
    QVERIFY(writeGeoTiff(path, s, [](int c, int r, int) { return r * 100 + c; }));

    GeoTiffImage img;
    QVERIFY(img.open(path));
    QCOMPARE(img.crs(), GeoTiffImage::Crs::Geographic);
    QCOMPARE(img.lonMin(), -80.0);
    QCOMPARE(img.lonMax(), -70.0);
    QCOMPARE(img.latMax(), 25.0);
    QCOMPARE(img.latMin(), 22.5);

    double col = 0, row = 0;
    QVERIFY(img.locate(-79.75, 24.875, &col, &row));   // centro del píxel (0,0)
    QVERIFY(std::fabs(col) < 1e-9 && std::fabs(row) < 1e-9);
    QVERIFY(!img.locate(-81.0, 24.0, &col, &row));     // fuera
    QVERIFY(!img.locate(-75.0, 26.0, &col, &row));
    QVERIFY(std::isnan(img.interpolate(-60.0, 24.0)));
}

// PixelIsPoint desplaza media muestra, y ModelTransformation da lo mismo que
// escala + enlace.
void TstGeoTiff::pixelIsPointAndTransformation()
{
    TiffSpec area;
    area.width = 10; area.height = 10;
    area.originX = 10.0; area.originY = 50.0; area.dx = 1.0; area.dy = 1.0;
    const QString a = tmp("area");
    QVERIFY(writeGeoTiff(a, area, [](int c, int r, int) { return r * 10 + c; }));

    TiffSpec point = area;
    point.pixelIsPoint = true;                    // el enlace es el CENTRO del píxel (0,0)
    point.originX = 10.5; point.originY = 49.5;
    const QString p = tmp("point");
    QVERIFY(writeGeoTiff(p, point, [](int c, int r, int) { return r * 10 + c; }));

    TiffSpec tr = area;
    tr.useTransformation = true;
    const QString t = tmp("transf");
    QVERIFY(writeGeoTiff(t, tr, [](int c, int r, int) { return r * 10 + c; }));

    GeoTiffImage ia, ip, it;
    QVERIFY(ia.open(a) && ip.open(p) && it.open(t));
    for (const QPointF &q : { QPointF(12.5, 47.5), QPointF(10.3, 49.9), QPointF(19.7, 40.2) }) {
        const double va = ia.interpolate(q.x(), q.y());
        QVERIFY(!std::isnan(va));
        QCOMPARE(ip.interpolate(q.x(), q.y()), va);
        QCOMPARE(it.interpolate(q.x(), q.y()), va);
    }
    QCOMPARE(ip.lonMin(), 10.0);
    QCOMPARE(ip.latMax(), 50.0);
}

// Bilineal exacta en un plano inclinado, sobre una muestra y en el borde.
void TstGeoTiff::interpolationAndBorders()
{
    TiffSpec s;
    s.width = 8; s.height = 8; s.bps = 32; s.sampleFormat = 3;
    s.originX = 0; s.originY = 8; s.dx = 1; s.dy = 1;
    const QString path = tmp("plano");
    // z = 2*col + 3*row: un plano, así la bilineal es exacta en cualquier punto.
    QVERIFY(writeGeoTiff(path, s, [](int c, int r, int) { return 2.0 * c + 3.0 * r; }));
    GeoTiffImage img;
    QVERIFY(img.open(path));
    // El centro del píxel (c,r) es (c+0.5, 8-(r+0.5)).
    QCOMPARE(img.interpolate(2.5, 8.0 - 3.5), 2.0 * 2 + 3.0 * 3);
    QVERIFY(std::fabs(img.interpolate(3.0, 8.0 - 4.0) - (2.0 * 2.5 + 3.0 * 3.5)) < 1e-4);
    // Media muestra hacia fuera del borde: toma la muestra más cercana, no NaN.
    QVERIFY(!std::isnan(img.interpolate(0.1, 7.9)));
    QVERIFY(std::isnan(img.interpolate(-0.2, 7.9)));
}

// El valor sin dato del fichero (GDAL_NODATA) da NaN, y contamina a sus vecinos.
void TstGeoTiff::noData()
{
    TiffSpec s;
    s.width = 6; s.height = 6; s.bps = 16; s.sampleFormat = 2;
    s.originX = 0; s.originY = 6; s.dx = 1; s.dy = 1;
    s.nodata = QStringLiteral("-9999");
    const QString path = tmp("nodata");
    QVERIFY(writeGeoTiff(path, s, [](int c, int r, int) { return (c == 2 && r == 2) ? -9999.0 : 100.0 + c; }));
    GeoTiffImage img;
    QVERIFY(img.open(path));
    QVERIFY(img.hasNoData());
    QCOMPARE(img.noData(), -9999.0);
    QVERIFY(std::isnan(img.sample(2, 2)));
    QCOMPARE(img.sample(3, 3), 103.0);
    QVERIFY(std::isnan(img.interpolate(2.5, 6.0 - 2.5)));       // sobre el hueco
    QVERIFY(std::isnan(img.interpolate(3.0, 6.0 - 3.0)));       // uno de sus 4 vecinos
    QVERIFY(!std::isnan(img.interpolate(4.5, 6.0 - 4.5)));      // lejos
}

// EPSG:3857: la extensión se convierte a grados y las consultas también.
void TstGeoTiff::webMercator()
{
    const double R = 6378137.0;
    const double pi = 3.14159265358979323846;
    // Un cuadrado de 100 km x 100 km con la esquina NO en (-80°, y de 20°).
    const double x0 = R * (-80.0) * pi / 180.0;
    const double y0 = R * std::log(std::tan(pi / 4.0 + 20.0 * pi / 360.0));
    TiffSpec s;
    s.width = 10; s.height = 10; s.bps = 16;
    s.modelType = 1; s.projectedEpsg = 3857;
    s.originX = x0; s.originY = y0; s.dx = 10000.0; s.dy = 10000.0;
    const QString path = tmp("mercator");
    QVERIFY(writeGeoTiff(path, s, [](int c, int r, int) { return r * 10 + c; }));
    GeoTiffImage img;
    QString error;
    QVERIFY2(img.open(path, &error), qPrintable(error));
    QCOMPARE(img.crs(), GeoTiffImage::Crs::WebMercator);
    QVERIFY(std::fabs(img.lonMin() - (-80.0)) < 1e-9);
    QVERIFY(std::fabs(img.latMax() - 20.0) < 1e-9);
    QVERIFY(img.lonMax() > img.lonMin() && img.latMin() < img.latMax());

    // El punto medio del píxel (3,2): se pasa a grados y se vuelve a pedir.
    const double xc = x0 + 3.5 * 10000.0, yc = y0 - 2.5 * 10000.0;
    const double lon = xc / R * 180.0 / pi;
    const double lat = std::atan(std::sinh(yc / R)) * 180.0 / pi;
    QCOMPARE(img.interpolate(lon, lat), 2.0 * 10 + 3);
}

// Tres bandas, con las muestras entrelazadas y con planos separados.
void TstGeoTiff::planarBands()
{
    for (const bool planar : { false, true }) {
        TiffSpec s;
        s.width = 11; s.height = 9; s.spp = 3; s.bps = 8; s.sampleFormat = 1;
        s.planar = planar; s.compression = 8; s.predictor = 2; s.rowsPerStrip = 4;
        const QString path = tmp(planar ? "planos" : "chunky");
        QVERIFY(writeGeoTiff(path, s, [](int c, int r, int b) { return valueFor(8, 1, c, r, b); }));
        GeoTiffImage img;
        QString error;
        QVERIFY2(img.open(path, &error), qPrintable(error));
        QCOMPARE(img.bands(), 3);
        for (int b = 0; b < 3; ++b)
            for (int r = 0; r < s.height; ++r)
                for (int c = 0; c < s.width; ++c)
                    QCOMPARE(img.sample(c, r, b), valueFor(8, 1, c, r, b));
        QVERIFY(std::isnan(img.sample(0, 0, 3)));
    }
}

// Lo que no entiende se rechaza con un motivo claro, sin fallar en silencio.
void TstGeoTiff::rejectsWhatItCannotRead()
{
    GeoTiffImage img;
    QString error;

    // Proyectado que no es Web Mercator: dice cuál es.
    TiffSpec utm;
    utm.modelType = 1; utm.projectedEpsg = 32617;
    const QString pUtm = tmp("utm");
    QVERIFY(writeGeoTiff(pUtm, utm, [](int, int, int) { return 1.0; }));
    QVERIFY(!img.open(pUtm, &error));
    QVERIFY2(error.contains(QStringLiteral("32617")), qPrintable(error));

    // Sin georreferencia.
    TiffSpec plain;
    plain.georeference = false;
    const QString pPlain = tmp("sin_geo");
    QVERIFY(writeGeoTiff(pPlain, plain, [](int, int, int) { return 1.0; }));
    QVERIFY(!img.open(pPlain, &error));
    QVERIFY2(error.contains(QStringLiteral("georreferencia")), qPrintable(error));

    // Compresión JPEG.
    TiffSpec jpeg;
    jpeg.compression = 7;
    const QString pJpeg = tmp("jpeg");
    QVERIFY(writeGeoTiff(pJpeg, jpeg, [](int, int, int) { return 1.0; }));
    QVERIFY(!img.open(pJpeg, &error));
    QVERIFY2(error.contains(QStringLiteral("compresi")), qPrintable(error));

    // No es un TIFF, no existe, está vacío.
    const QString pTxt = m_dir.path() + QStringLiteral("/nada.tif");
    QFile f(pTxt);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("esto no es un tiff, ni de lejos");
    f.close();
    QVERIFY(!img.open(pTxt, &error));
    QVERIFY(!img.open(m_dir.path() + QStringLiteral("/no_existe.tif"), &error));
    QVERIFY(!img.isOpen());
    QVERIFY(std::isnan(img.sample(0, 0)));
}

// Una imagen grande por teselas no se carga entera: se leen solo los bloques
// consultados y los valores salen bien en puntos repartidos.
void TstGeoTiff::largeTiledImage()
{
    TiffSpec s;
    s.width = 1500; s.height = 1100; s.bps = 16; s.sampleFormat = 2;
    s.tiled = true; s.tile = 64; s.compression = 8; s.predictor = 2;
    const QString path = tmp("grande");
    QVERIFY(writeGeoTiff(path, s, [](int c, int r, int) { return (c * 7 + r * 13) % 3000 - 1000; }));
    GeoTiffImage img;
    QVERIFY(img.open(path));
    for (int k = 0; k < 400; ++k) {
        const int c = (k * 389) % s.width;
        const int r = (k * 211) % s.height;
        QCOMPARE(img.sample(c, r), double((c * 7 + r * 13) % 3000 - 1000));
    }
    QVERIFY(std::isnan(img.sample(s.width, 0)));
    QVERIFY(std::isnan(img.sample(0, s.height)));
    QVERIFY(std::isnan(img.sample(-1, 0)));
}

// La caché de bloques es una para todo el proceso y respeta su límite: con un
// presupuesto minúsculo (unos pocos bloques) las lecturas siguen saliendo exactas
// aunque se expulsen y se descodifiquen una y otra vez, y varias imágenes a la vez
// no suman presupuestos.
void TstGeoTiff::sharedCacheStaysBounded()
{
    const qint64 previo = GeoTiffImage::cacheLimit();
    QCOMPARE(previo, qint64(256) * 1024 * 1024);

    TiffSpec s;
    s.width = 600; s.height = 600; s.bps = 16; s.sampleFormat = 2;
    s.tiled = true; s.tile = 64; s.compression = 8;
    const QString a = tmp("cache_a"), b = tmp("cache_b");
    QVERIFY(writeGeoTiff(a, s, [](int c, int r, int) { return c + r * 3 - 900; }));
    QVERIFY(writeGeoTiff(b, s, [](int c, int r, int) { return c * 2 - r; }));
    GeoTiffImage ia, ib;
    QVERIFY(ia.open(a) && ib.open(b));

    GeoTiffImage::setCacheLimit(40 * 1024);          // ~4 bloques de 64x64 (16 KiB c/u)
    for (int k = 0; k < 600; ++k) {
        const int c = (k * 97) % 600, r = (k * 211) % 600;
        QCOMPARE(ia.sample(c, r), double(c + r * 3 - 900));
        QCOMPARE(ib.sample(c, r), double(c * 2 - r));
    }
    GeoTiffImage::setCacheLimit(0);                              // sin caché: todo se vuelve a leer
    QCOMPARE(ia.sample(321, 123), double(321 + 123 * 3 - 900));
    GeoTiffImage::setCacheLimit(previo);
    QCOMPARE(GeoTiffImage::cacheLimit(), previo);
}

// Una carpeta con varias zonas contiguas: cada coordenada se resuelve en su
// fichero, el borde compartido responde y lo de fuera da NaN. Los ficheros
// rotos se saltan y se anotan.
void TstGeoTiff::folderOfTiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 3; ++i) {              // tres zonas de 1° en fila hacia el este
        TiffSpec s;
        s.width = 11; s.height = 11; s.bps = 16;
        s.originX = 10.0 + i; s.originY = 21.0; s.dx = 0.1; s.dy = 0.1;
        // Cota = 1000*i + 10*fila (plano en cada zona).
        QVERIFY(writeGeoTiff(dir.path() + QStringLiteral("/z%1.tif").arg(i), s,
                             [i](int, int r, int) { return 1000.0 * i + 10.0 * r; }));
    }
    QFile bad(dir.path() + QStringLiteral("/roto.tif"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("no");
    bad.close();

    GeoTiffElevation dem;
    QVERIFY(dem.setPath(dir.path()));
    QCOMPARE(dem.imageCount(), 3);
    QCOMPARE(dem.warnings().size(), 1);
    // Fila 5 de cada zona: latitud 21 - 5.5*0.1 = 20.45.
    QVERIFY(std::fabs(dem.elevationAt(QGeoCoordinate(20.45, 10.5)) - 50.0) < 1e-6);
    QVERIFY(std::fabs(dem.elevationAt(QGeoCoordinate(20.45, 11.5)) - 1050.0) < 1e-6);
    QVERIFY(std::fabs(dem.elevationAt(QGeoCoordinate(20.45, 12.5)) - 2050.0) < 1e-6);
    QVERIFY(!std::isnan(dem.elevationAt(QGeoCoordinate(20.45, 11.0))));    // junta entre dos zonas
    QVERIFY(std::isnan(dem.elevationAt(QGeoCoordinate(20.45, 14.5))));     // fuera
    QVERIFY(std::isnan(dem.elevationAt(QGeoCoordinate(23.0, 10.5))));
    QVERIFY(std::isnan(dem.elevationAt(QGeoCoordinate())));                // coordenada inválida

    GeoTiffElevation vacia;
    QVERIFY(!vacia.setPath(m_dir.path() + QStringLiteral("/no_hay_nada")));
    QVERIFY(std::isnan(vacia.elevationAt(QGeoCoordinate(20.0, 10.0))));
}

// La fábrica elige la fuente por la ruta, y lo que ya existía no cambia: una
// carpeta con .hgt sigue siendo HgtElevation y otro fichero, SqliteElevation.
void TstGeoTiff::factoryPicksTheRightSource()
{
    TiffSpec s;
    s.width = 5; s.height = 5; s.originX = 0; s.originY = 5; s.dx = 1; s.dy = 1;
    const QString tif = tmp("fabrica");
    QVERIFY(writeGeoTiff(tif, s, [](int, int, int) { return 42.0; }));
    QVERIFY(isGeoTiffElevationPath(tif));
    auto a = openElevationSource(tif);
    QVERIFY(dynamic_cast<GeoTiffElevation *>(a.get()) != nullptr);
    QCOMPARE(a->elevationAt(QGeoCoordinate(2.5, 2.5)), 42.0);

    QTemporaryDir soloTif;
    QVERIFY(QFile::copy(tif, soloTif.path() + QStringLiteral("/a.TIF")));
    QVERIFY(isGeoTiffElevationPath(soloTif.path()));
    QVERIFY(dynamic_cast<GeoTiffElevation *>(openElevationSource(soloTif.path()).get()) != nullptr);

    QTemporaryDir mixta;                         // .hgt + .tif: manda .hgt, como antes
    QVERIFY(QFile::copy(tif, mixta.path() + QStringLiteral("/a.tif")));
    QFile hgt(mixta.path() + QStringLiteral("/N00E000.hgt"));
    QVERIFY(hgt.open(QIODevice::WriteOnly));
    hgt.write(QByteArray(2 * 4 * 4, '\0'));
    hgt.close();
    QVERIFY(!isGeoTiffElevationPath(mixta.path()));
    QVERIFY(dynamic_cast<HgtElevation *>(openElevationSource(mixta.path()).get()) != nullptr);

    QTemporaryDir vacia;                         // carpeta vacía: Hgt, como antes
    QVERIFY(dynamic_cast<HgtElevation *>(openElevationSource(vacia.path()).get()) != nullptr);

    const QString db = m_dir.path() + QStringLiteral("/dem.sqlitedb");
    QVERIFY(dynamic_cast<SqliteElevation *>(openElevationSource(db).get()) != nullptr);
    QVERIFY(openElevationSource(QString()) == nullptr);
}

QTEST_APPLESS_MAIN(TstGeoTiff)
#include "tst_geotiff.moc"
