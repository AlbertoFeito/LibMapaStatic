#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"

#include <QtTest>

using namespace libmapa;

class TstTileMatrix : public QObject
{
    Q_OBJECT

private slots:
    // --- WebMercator -------------------------------------------------------

    void mercatorRoundTrip_data();
    void mercatorRoundTrip();

    //! F-19: inverse(x, y) debe interpretar x como x e y como y.
    void mercatorArgumentOrderIsHonest();

    void mercatorOriginIsZero();
    void mercatorClampsPoles();

    // --- TileMatrix --------------------------------------------------------

    void tileRoundTrip_data();
    void tileRoundTrip();

    void tileZeroCoversWholeWorld();
    void tileBoundsAreContiguous();

    //! El y de TMS es el complemento del de XYZ y la operacion es involutiva.
    void tmsFlipIsInvolutive();

    void rangeForClampsToWorld();
    void rangeForCoversCuba();
    void rangeMarginExpands();

    void bestZoomIsMonotonic();

    /*! Reproduce la falla F-7.
     *
     * ValidaZoomActivo calculaba los indices con "ZoomValido - 1" mientras
     * Cargar_BD_IMG consultaba "WHERE z = Zoom_Level", sin el -1. Este test
     * demuestra que los indices de dos niveles contiguos NO son
     * intercambiables: usar unos con el otro nivel apunta a otro sitio.
     */
    void offByOneZoomPointsElsewhere();
};

void TstTileMatrix::mercatorRoundTrip_data()
{
    QTest::addColumn<double>("lat");
    QTest::addColumn<double>("lon");

    QTest::newRow("ecuador/greenwich") << 0.0 << 0.0;
    QTest::newRow("la habana")         << 23.1136 << -82.3666;
    QTest::newRow("santiago de cuba")  << 20.0247 << -75.8219;
    QTest::newRow("punta maisi")       << 20.2456 << -74.1372;
    QTest::newRow("cabo san antonio")  << 21.8667 << -84.9500;
    QTest::newRow("sur extremo")       << -60.0 << 120.0;
    QTest::newRow("antimeridiano")     << 10.0 << 179.9;
}

void TstTileMatrix::mercatorRoundTrip()
{
    QFETCH(double, lat);
    QFETCH(double, lon);

    const QPointF m = WebMercator::forward(lat, lon);
    const QGeoCoordinate back = WebMercator::inverse(m);

    QVERIFY2(qAbs(back.latitude() - lat) < 1e-9,
             qPrintable(QStringLiteral("lat %1 -> %2").arg(lat).arg(back.latitude())));
    QVERIFY2(qAbs(back.longitude() - lon) < 1e-9,
             qPrintable(QStringLiteral("lon %1 -> %2").arg(lon).arg(back.longitude())));
}

void TstTileMatrix::mercatorArgumentOrderIsHonest()
{
    // Un punto claramente asimetrico: x grande, y pequeno.
    const QPointF m = WebMercator::forward(10.0, 80.0);
    QVERIFY(m.x() > 0);
    QVERIFY(m.y() > 0);
    QVERIFY(m.x() > m.y());   // 80 grados de longitud > 10 de latitud

    // inverse(x, y) y inverse(QPointF(x, y)) deben coincidir.
    const QGeoCoordinate a = WebMercator::inverse(m.x(), m.y());
    const QGeoCoordinate b = WebMercator::inverse(m);
    QCOMPARE(a.latitude(),  b.latitude());
    QCOMPARE(a.longitude(), b.longitude());

    // Y devolver el punto original: si estuvieran cruzados, latitud y
    // longitud saldrian intercambiadas.
    QVERIFY(qAbs(a.latitude()  - 10.0) < 1e-9);
    QVERIFY(qAbs(a.longitude() - 80.0) < 1e-9);
}

void TstTileMatrix::mercatorOriginIsZero()
{
    const QPointF m = WebMercator::forward(0.0, 0.0);
    QVERIFY(qAbs(m.x()) < 1e-9);
    QVERIFY(qAbs(m.y()) < 1e-9);
}

void TstTileMatrix::mercatorClampsPoles()
{
    // Sin recorte, tan(90 grados) diverge.
    const QPointF n = WebMercator::forward(90.0, 0.0);
    const QPointF s = WebMercator::forward(-90.0, 0.0);
    QVERIFY(std::isfinite(n.y()));
    QVERIFY(std::isfinite(s.y()));
    QVERIFY(qAbs(n.y() + s.y()) < 1e-6);   // simetricos
}

void TstTileMatrix::tileRoundTrip_data()
{
    QTest::addColumn<int>("z");
    QTest::newRow("z0")  << 0;
    QTest::newRow("z5")  << 5;
    QTest::newRow("z10") << 10;
    QTest::newRow("z16") << 16;
    QTest::newRow("z17") << 17;
}

void TstTileMatrix::tileRoundTrip()
{
    QFETCH(int, z);

    const QGeoCoordinate habana(23.1136, -82.3666);
    const TileKey key = TileMatrix::tileAt(habana, z);

    QVERIFY(key.isValid());
    QCOMPARE(key.z, z);

    // El punto debe caer dentro de los limites de su propia tesela.
    const QGeoCoordinate nw = TileMatrix::tileNorthWest(key);
    const QGeoCoordinate se = TileMatrix::tileSouthEast(key);

    QVERIFY(habana.longitude() >= nw.longitude());
    QVERIFY(habana.longitude() <= se.longitude());
    QVERIFY(habana.latitude()  <= nw.latitude());
    QVERIFY(habana.latitude()  >= se.latitude());
}

void TstTileMatrix::tileZeroCoversWholeWorld()
{
    const TileKey root{0, 0, 0};
    const QGeoCoordinate nw = TileMatrix::tileNorthWest(root);
    const QGeoCoordinate se = TileMatrix::tileSouthEast(root);

    QVERIFY(qAbs(nw.longitude() + 180.0) < 1e-9);
    QVERIFY(qAbs(se.longitude() - 180.0) < 1e-9);
    QVERIFY(qAbs(nw.latitude() - WebMercator::kMaxLatitude) < 1e-6);
    QVERIFY(qAbs(se.latitude() + WebMercator::kMaxLatitude) < 1e-6);
}

void TstTileMatrix::tileBoundsAreContiguous()
{
    // La esquina SE de una tesela es exactamente la NO de su vecina.
    const TileKey a{8, 70, 110};
    const TileKey b{8, 71, 111};

    const QGeoCoordinate seA = TileMatrix::tileSouthEast(a);
    const QGeoCoordinate nwB = TileMatrix::tileNorthWest(b);

    QVERIFY(qAbs(seA.longitude() - nwB.longitude()) < 1e-9);
    QVERIFY(qAbs(seA.latitude()  - nwB.latitude())  < 1e-9);
}

void TstTileMatrix::tmsFlipIsInvolutive()
{
    for (int z = 0; z <= 17; ++z) {
        const int n = TileMatrix::tilesPerSide(z);
        for (int y : {0, n / 3, n / 2, n - 1}) {
            const int stored = TileMatrix::toStorageY(y, z, TileScheme::TMS);
            QCOMPARE(stored, n - 1 - y);
            QCOMPARE(TileMatrix::fromStorageY(stored, z, TileScheme::TMS), y);

            // XYZ no toca nada.
            QCOMPARE(TileMatrix::toStorageY(y, z, TileScheme::XYZ), y);
        }
    }
}

void TstTileMatrix::rangeForClampsToWorld()
{
    // Un rectangulo que se sale por los polos y por los meridianos extremos.
    const auto r = TileMatrix::rangeFor(QGeoCoordinate(89.0, -180.0),
                                        QGeoCoordinate(-89.0, 180.0),
                                        4, /*margin=*/10);
    const int n = TileMatrix::tilesPerSide(4);

    QVERIFY(r.xMin >= 0);
    QVERIFY(r.yMin >= 0);
    QVERIFY(r.xMax < n);
    QVERIFY(r.yMax < n);
}

void TstTileMatrix::rangeForCoversCuba()
{
    const QGeoCoordinate nw(23.3, -85.0);
    const QGeoCoordinate se(19.7, -74.0);

    const auto r = TileMatrix::rangeFor(nw, se, 8);

    QVERIFY(!r.isEmpty());
    QCOMPARE(r.z, 8);

    // Las esquinas del rectangulo deben caer dentro del rango calculado.
    QVERIFY(r.contains(TileMatrix::tileAt(nw, 8)));
    QVERIFY(r.contains(TileMatrix::tileAt(se, 8)));

    // Y un punto interior tambien.
    QVERIFY(r.contains(TileMatrix::tileAt(QGeoCoordinate(22.0, -79.5), 8)));
}

void TstTileMatrix::rangeMarginExpands()
{
    const QGeoCoordinate nw(23.0, -83.0);
    const QGeoCoordinate se(22.0, -82.0);

    const auto r0 = TileMatrix::rangeFor(nw, se, 10, 0);
    const auto r2 = TileMatrix::rangeFor(nw, se, 10, 2);

    QCOMPARE(r2.xMin, r0.xMin - 2);
    QCOMPARE(r2.xMax, r0.xMax + 2);
    QVERIFY(r2.count() > r0.count());
}

void TstTileMatrix::bestZoomIsMonotonic()
{
    const TileMatrix m(256);

    // A menor extension visible (mas ampliado), mayor nivel de zoom.
    const int zWide   = m.bestZoomFor(180.0, 1024, 0, 17);
    const int zMedium = m.bestZoomFor(10.0,  1024, 0, 17);
    const int zTight  = m.bestZoomFor(0.05,  1024, 0, 17);

    QVERIFY(zWide < zMedium);
    QVERIFY(zMedium < zTight);

    // Y siempre dentro de los limites del dataset.
    QVERIFY(m.bestZoomFor(0.0001, 1024, 0, 16) <= 16);
    QVERIFY(m.bestZoomFor(360.0,  1024, 3, 17) >= 3);
}

void TstTileMatrix::offByOneZoomPointsElsewhere()
{
    const QGeoCoordinate p(23.1136, -82.3666);

    const TileKey k10 = TileMatrix::tileAt(p, 10);
    const TileKey k11 = TileMatrix::tileAt(p, 11);

    // Los indices de un nivel NO sirven para el otro: al duplicarse la
    // rejilla, x e y cambian. Pedirle a la BD el nivel 11 con los indices
    // calculados para el 10 devuelve teselas de otra zona del mapa (o nada).
    QVERIFY(k10.x != k11.x || k10.y != k11.y);
    QCOMPARE(k11.parent().x, k10.x);
    QCOMPARE(k11.parent().y, k10.y);

    // Comprobacion geografica del error: la tesela (k10.x, k10.y) en z=11
    // esta en un sitio distinto del punto original.
    const TileKey wrong{11, k10.x, k10.y};
    const QGeoCoordinate wrongNW = TileMatrix::tileNorthWest(wrong);
    QVERIFY(qAbs(wrongNW.longitude() - p.longitude()) > 1.0);
}

QTEST_MAIN(TstTileMatrix)
#include "tst_tilematrix.moc"
