#include "geo/MapDecor.h"

#include <QtTest>

#include <limits>

using namespace libmapa;

//! Tests de la escala grafica y la cuadricula (calculos puros, sin ventanas).
class TstMapDecor : public QObject
{
    Q_OBJECT

private slots:
    void scaleBarPicksRoundValues();
    void scaleBarNauticalMiles();
    void scaleBarRejectsBadInput();
    void graticuleStepUsesChartSteps();
    void formatsLatitudeByStep();
    void formatsLongitudeAndNormalizes();
    void formatsDistanceAndBearing();
    void geodesicCircleKeepsRadius();
};

// La barra elige 1-2-5 x 10^n sin pasarse del ancho maximo.
void TstMapDecor::scaleBarPicksRoundValues()
{
    // 10 m/px y 120 px como maximo -> caben 1200 m -> 1 km en 100 px.
    MapDecor::ScaleBar b = MapDecor::scaleBar(10.0, 120.0);
    QCOMPARE(b.meters, 1000.0);
    QCOMPARE(b.pixels, 100.0);
    QCOMPARE(b.label, QStringLiteral("1 km"));

    // 3 m/px -> caben 360 m -> 200 m.
    b = MapDecor::scaleBar(3.0, 120.0);
    QCOMPARE(b.meters, 200.0);
    QCOMPARE(b.label, QStringLiteral("200 m"));
    QVERIFY(b.pixels <= 120.0);

    // 50 m/px -> caben 6000 m -> 5 km.
    b = MapDecor::scaleBar(50.0, 120.0);
    QCOMPARE(b.label, QStringLiteral("5 km"));

    // Valores muy pequenos (zoom maximo): 0.05 m/px -> 6 m -> 5 m.
    b = MapDecor::scaleBar(0.05, 120.0);
    QCOMPARE(b.label, QStringLiteral("5 m"));
    QVERIFY(qAbs(b.pixels - 100.0) < 1e-6);
}

// En millas nauticas el redondeo se hace en millas (0.5, 1, 2...).
void TstMapDecor::scaleBarNauticalMiles()
{
    // 20 m/px * 120 px = 2400 m = 1.296 NM -> 1 NM = 1852 m = 92.6 px.
    MapDecor::ScaleBar b = MapDecor::scaleBar(20.0, 120.0, ScaleUnits::Nautical);
    QCOMPARE(b.label, QStringLiteral("1 NM"));
    QCOMPARE(b.meters, 1852.0);
    QVERIFY(qAbs(b.pixels - 92.6) < 1e-6);

    // 5 m/px * 120 = 600 m = 0.324 NM -> 0.2 NM.
    b = MapDecor::scaleBar(5.0, 120.0, ScaleUnits::Nautical);
    QCOMPARE(b.label, QStringLiteral("0.2 NM"));
}

// Resolucion invalida: barra vacia, sin dividir por cero.
void TstMapDecor::scaleBarRejectsBadInput()
{
    QCOMPARE(MapDecor::scaleBar(0.0, 120.0).pixels, 0.0);
    QCOMPARE(MapDecor::scaleBar(-1.0, 120.0).pixels, 0.0);
    QCOMPARE(MapDecor::scaleBar(std::numeric_limits<double>::quiet_NaN(), 120.0).pixels, 0.0);
    QCOMPARE(MapDecor::scaleBar(10.0, 0.0).pixels, 0.0);
    QVERIFY(MapDecor::scaleBar(0.0, 120.0).label.isEmpty());
}

// El paso es el mas fino de la serie de cartas que no supera las lineas pedidas.
void TstMapDecor::graticuleStepUsesChartSteps()
{
    QCOMPARE(MapDecor::graticuleStep(360.0, 8), 45.0);     // mundo entero
    QCOMPARE(MapDecor::graticuleStep(10.0, 6), 2.0);       // un pais
    QCOMPARE(MapDecor::graticuleStep(10.0, 10), 1.0);
    QCOMPARE(MapDecor::graticuleStep(2.0, 6), 20.0 / 60.0); // 20 minutos
    QCOMPARE(MapDecor::graticuleStep(0.05, 6), 30.0 / 3600.0);
    // Tramo enorme con una sola linea: el paso mas grueso.
    QCOMPARE(MapDecor::graticuleStep(360.0, 1), 90.0);
    // maxLines < 1 se trata como 1; el tramo puede venir negativo.
    QCOMPARE(MapDecor::graticuleStep(-10.0, 0), 10.0);
}

// La precision de la etiqueta sigue al paso: grados, minutos o segundos.
void TstMapDecor::formatsLatitudeByStep()
{
    const QString g = QString(QChar(0x00B0));
    QCOMPARE(MapDecor::formatLatitude(23.0, 1.0), QStringLiteral("23") + g + QStringLiteral("N"));
    QCOMPARE(MapDecor::formatLatitude(23.5, 0.5), QStringLiteral("23") + g + QStringLiteral("30'N"));
    QCOMPARE(MapDecor::formatLatitude(-23.0 - 15.0 / 3600.0, 15.0 / 3600.0),
             QStringLiteral("23") + g + QStringLiteral("00'15\"S"));
    // Un valor arrastrado del calculo (23.4999999...) sale redondo.
    QCOMPARE(MapDecor::formatLatitude(23.0 + 29.999999 / 60.0, 1.0 / 60.0),
             QStringLiteral("23") + g + QStringLiteral("30'N"));
    QCOMPARE(MapDecor::formatLatitude(0.0, 1.0), QStringLiteral("0") + g);
}

// Longitudes con E/W, normalizadas; 0 y 180 sin letra.
void TstMapDecor::formatsLongitudeAndNormalizes()
{
    const QString g = QString(QChar(0x00B0));
    QCOMPARE(MapDecor::formatLongitude(-82.25, 0.25), QStringLiteral("82") + g + QStringLiteral("15'W"));
    QCOMPARE(MapDecor::formatLongitude(10.0, 5.0), QStringLiteral("10") + g + QStringLiteral("E"));
    QCOMPARE(MapDecor::formatLongitude(190.0, 10.0), QStringLiteral("170") + g + QStringLiteral("W"));
    QCOMPARE(MapDecor::formatLongitude(-180.0, 10.0), QStringLiteral("180") + g);
    QCOMPARE(MapDecor::formatLongitude(0.0, 10.0), QStringLiteral("0") + g);
}

// Distancias con los decimales justos y sin ceros de relleno; rumbos con tres
// cifras normalizados a [0, 360).
void TstMapDecor::formatsDistanceAndBearing()
{
    QCOMPARE(MapDecor::formatDistance(750.4, ScaleUnits::Metric), QStringLiteral("750 m"));
    QCOMPARE(MapDecor::formatDistance(2500.0, ScaleUnits::Metric), QStringLiteral("2.5 km"));
    QCOMPARE(MapDecor::formatDistance(12345.0, ScaleUnits::Metric), QStringLiteral("12.3 km"));
    QCOMPARE(MapDecor::formatDistance(250000.0, ScaleUnits::Metric), QStringLiteral("250 km"));
    QCOMPARE(MapDecor::formatDistance(1852.0, ScaleUnits::Nautical), QStringLiteral("1 NM"));
    QCOMPARE(MapDecor::formatDistance(2315.0, ScaleUnits::Nautical), QStringLiteral("1.25 NM"));
    QCOMPARE(MapDecor::formatDistance(500.0, ScaleUnits::Nautical), QStringLiteral("0.27 NM"));
    QVERIFY(MapDecor::formatDistance(std::nan(""), ScaleUnits::Metric).isEmpty());

    const QString dos = QString(QChar(0x00B2));
    QCOMPARE(MapDecor::formatArea(850.4, ScaleUnits::Metric), QStringLiteral("850 m") + dos);
    QCOMPARE(MapDecor::formatArea(125000.0, ScaleUnits::Metric), QStringLiteral("12.5 ha"));
    QCOMPARE(MapDecor::formatArea(3.42e6, ScaleUnits::Metric), QStringLiteral("3.42 km") + dos);
    QCOMPARE(MapDecor::formatArea(1852.0 * 1852.0, ScaleUnits::Nautical),
             QStringLiteral("1 NM") + dos);
    QVERIFY(MapDecor::formatArea(std::nan(""), ScaleUnits::Metric).isEmpty());

    const QString g = QString(QChar(0x00B0));
    QCOMPARE(MapDecor::formatBearing(45.0), QStringLiteral("045") + g);
    QCOMPARE(MapDecor::formatBearing(-10.0), QStringLiteral("350") + g);
    QCOMPARE(MapDecor::formatBearing(359.7), QStringLiteral("000") + g);
    QCOMPARE(MapDecor::formatBearing(720.0 + 5.2), QStringLiteral("005") + g);
    QVERIFY(MapDecor::formatBearing(std::numeric_limits<double>::infinity()).isEmpty());
}

// Todos los puntos del anillo estan a la distancia pedida del centro, en
// cualquier latitud; el anillo viene cerrado. Entradas malas: vacio.
void TstMapDecor::geodesicCircleKeepsRadius()
{
    for (double lat : { 0.0, 45.0, 75.0 }) {
        const QGeoCoordinate c(lat, -30.0);
        const QVector<QGeoCoordinate> anillo = MapDecor::geodesicCircle(c, 5000.0, 60);
        QCOMPARE(anillo.size(), 61);
        QCOMPARE(anillo.first(), anillo.last());
        for (const QGeoCoordinate &p : anillo)
            QVERIFY(qAbs(c.distanceTo(p) - 5000.0) < 0.5);
    }
    QVERIFY(MapDecor::geodesicCircle(QGeoCoordinate(), 100.0).isEmpty());
    QVERIFY(MapDecor::geodesicCircle(QGeoCoordinate(0, 0), 0.0).isEmpty());
}

QTEST_APPLESS_MAIN(TstMapDecor)
#include "tst_mapdecor.moc"
