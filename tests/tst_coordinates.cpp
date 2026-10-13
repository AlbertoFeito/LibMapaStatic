#include "libmapa/Coordinates.h"

#include <QtTest>

using namespace libmapa;

//! Tests de UTM/MGRS y formatos de coordenadas contra referencias externas.
class TstCoordinates : public QObject
{
    Q_OBJECT

private slots:
    void utmMatchesProj();
    void utmRoundTrip();
    void zoneExceptionsNorwaySvalbard();
    void outsideUtmIsInvalid();
    void mgrsMatchesReference();
    void mgrsParsesBackToCellCorner();
    void mgrsRejectsGarbage();
    void formatsEveryKind();
    void parsesHumanInput();
};

namespace {
// Referencias calculadas con PROJ 9.8 (pyproj, EPSG:326zz/327zz) y la libreria
// "mgrs" (GeoTrans) de Python. Incluyen La Habana, Nueva York, Sidney,
// Islandia, la excepcion noruega (32V), Svalbard (31X/33X), el ecuador, un
// punto apenas al sur del ecuador y los extremos de UTM (83.5°N, 79.9°S).
struct Ref {
    double lat, lon;
    int zone;
    bool north;
    double easting, northing;
    const char *mgrs;
};
const Ref kRefs[] = {
    { 23.1136, -82.3666, 17, true, 360056.9750, 2556750.5762, "17QLF6005656750" },
    { 40.689247, -74.044502, 18, true, 580735.6450, 4504700.3807, "18TWL8073504700" },
    { -33.8568, 151.2153, 56, false, 334900.5697, 6252288.7529, "56HLH3490052288" },
    { 64.1466, -21.9426, 27, true, 454138.3765, 7113689.8690, "27WVM5413813689" },
    { 60.0, 5.0, 32, true, 276979.9264, 6658157.2024, "32VKM7697958157" },
    { 78.2232, 15.6267, 33, true, 514278.7151, 8683355.4695, "33XWG1427883355" },
    { 0.0, 3.0, 31, true, 500000.0000, 0.0000, "31NEA0000000000" },
    { -0.0001, -0.0001, 30, false, 833967.4140, 9999988.9317, "30MZE3396799988" },
    { 83.5, -30.0, 26, true, 462101.3367, 9273261.9089, "26XMT6210173261" },
    { -79.9, 170.0, 59, false, 480423.3890, 1129407.4826, "59CMM8042329407" },
    { 56.5, 3.5, 32, true, 161622.3457, 6275290.4057, "32VJH6162275290" },
    { 72.5, 8.9, 31, true, 697737.0973, 8054424.6129, "31XFA9773754424" },
    { 72.5, 9.1, 33, true, 302262.9027, 8054424.6129, "33XUA0226254424" },
};
} // namespace

// Este/norte coinciden con PROJ al milimetro, con la zona correcta.
void TstCoordinates::utmMatchesProj()
{
    for (const Ref &r : kRefs) {
        const UtmCoordinate u = toUtm(QGeoCoordinate(r.lat, r.lon));
        const QString ctx = QStringLiteral("%1,%2").arg(r.lat).arg(r.lon);
        QVERIFY2(u.isValid(), qPrintable(ctx));
        QCOMPARE(u.zone, r.zone);
        QCOMPARE(u.north, r.north);
        QVERIFY2(qAbs(u.easting - r.easting) < 0.001, qPrintable(ctx + QStringLiteral(" E %1").arg(u.easting, 0, 'f', 4)));
        QVERIFY2(qAbs(u.northing - r.northing) < 0.001, qPrintable(ctx + QStringLiteral(" N %1").arg(u.northing, 0, 'f', 4)));
    }
}

// Ida y vuelta UTM -> geografica en < 1e-9 grados (~0.1 mm), incluso forzando
// la zona vecina (3° fuera de su meridiano central).
void TstCoordinates::utmRoundTrip()
{
    for (const Ref &r : kRefs) {
        const QGeoCoordinate c(r.lat, r.lon);
        const QGeoCoordinate v = fromUtm(toUtm(c));
        QVERIFY(qAbs(v.latitude() - r.lat) < 1e-9);
        QVERIFY(qAbs(v.longitude() - r.lon) < 1e-9);
    }
    const QGeoCoordinate borde(23.0, -84.0);                 // limite 16/17
    const QGeoCoordinate v = fromUtm(toUtm(borde, 16));
    QVERIFY(qAbs(v.latitude() - 23.0) < 1e-9);
    QVERIFY(qAbs(v.longitude() + 84.0) < 1e-9);
    QCOMPARE(toUtm(borde, 16).zone, 16);
}

// Excepciones historicas de la cuadricula.
void TstCoordinates::zoneExceptionsNorwaySvalbard()
{
    QCOMPARE(utmZoneFor(QGeoCoordinate(60.0, 4.0)), 32);    // 32V ensanchada
    QCOMPARE(utmZoneFor(QGeoCoordinate(55.9, 4.0)), 31);    // fuera de la banda V
    QCOMPARE(utmZoneFor(QGeoCoordinate(78.0, 8.0)), 31);    // Svalbard
    QCOMPARE(utmZoneFor(QGeoCoordinate(78.0, 10.0)), 33);
    QCOMPARE(utmZoneFor(QGeoCoordinate(78.0, 25.0)), 35);
    QCOMPARE(utmZoneFor(QGeoCoordinate(78.0, 40.0)), 37);
    // Antimeridiano: 180 exacto es zona 1 (igual que GeoTrans), justo antes, 60.
    QCOMPARE(utmZoneFor(QGeoCoordinate(10.0, 180.0)), 1);
    QCOMPARE(utmZoneFor(QGeoCoordinate(10.0, -180.0)), 1);
    QCOMPARE(utmZoneFor(QGeoCoordinate(10.0, 179.9999)), 60);
}

// Fuera de 80°S..84°N no hay UTM ni MGRS.
void TstCoordinates::outsideUtmIsInvalid()
{
    QVERIFY(!toUtm(QGeoCoordinate(85.0, 0.0)).isValid());
    QVERIFY(!toUtm(QGeoCoordinate(-81.0, 0.0)).isValid());
    QVERIFY(toMgrs(QGeoCoordinate(-81.0, 0.0)).isEmpty());
    QVERIFY(formatCoordinate(QGeoCoordinate(89.0, 0.0), CoordinateFormat::UTM).isEmpty());
    QVERIFY(!toUtm(QGeoCoordinate()).isValid());
}

// MGRS a 1 m identico a GeoTrans; tambien con espacios y a menor precision.
void TstCoordinates::mgrsMatchesReference()
{
    for (const Ref &r : kRefs)
        QCOMPARE(toMgrs(QGeoCoordinate(r.lat, r.lon), 5, false), QString::fromLatin1(r.mgrs));
    QCOMPARE(toMgrs(QGeoCoordinate(23.1136, -82.3666)), QStringLiteral("17Q LF 60056 56750"));
    QCOMPARE(toMgrs(QGeoCoordinate(23.1136, -82.3666), 3), QStringLiteral("17Q LF 600 567"));
    QCOMPARE(toMgrs(QGeoCoordinate(23.1136, -82.3666), 1, false), QStringLiteral("17QLF65"));
}

// Leer un MGRS devuelve la esquina suroeste de su celda: a 1 m, a menos de
// 1 m del punto original (en ambos hemisferios y en zonas de excepcion).
void TstCoordinates::mgrsParsesBackToCellCorner()
{
    for (const Ref &r : kRefs) {
        bool ok = false;
        const QGeoCoordinate c = fromMgrs(QString::fromLatin1(r.mgrs), &ok);
        QVERIFY2(ok, r.mgrs);
        const double d = c.distanceTo(QGeoCoordinate(r.lat, r.lon));
        QVERIFY2(d < 1.5, qPrintable(QStringLiteral("%1 a %2 m").arg(QLatin1String(r.mgrs)).arg(d)));
    }
    bool ok = false;
    const QGeoCoordinate c = fromMgrs(QStringLiteral("17q lf 600 567"), &ok);   // 100 m
    QVERIFY(ok);
    QVERIFY(c.distanceTo(QGeoCoordinate(23.1136, -82.3666)) < 150.0);
    QVERIFY(fromMgrs(QStringLiteral("17QLF"), &ok).isValid());                   // 100 km
    QVERIFY(ok);
}

// Entradas mal formadas: no se aceptan.
void TstCoordinates::mgrsRejectsGarbage()
{
    bool ok = true;
    QVERIFY(!fromMgrs(QStringLiteral("17QLF600"), &ok).isValid() || !ok);   // digitos impares
    QVERIFY(!ok);
    fromMgrs(QStringLiteral("17QIF6005656750"), &ok);                         // I no existe
    QVERIFY(!ok);
    fromMgrs(QStringLiteral("17QAF6005656750"), &ok);                         // A no es del juego de la 17
    QVERIFY(!ok);
    fromMgrs(QStringLiteral("61QLF00"), &ok);
    QVERIFY(!ok);
    fromMgrs(QStringLiteral("hola"), &ok);
    QVERIFY(!ok);
}

// Cada formato de salida.
void TstCoordinates::formatsEveryKind()
{
    const QGeoCoordinate c(23.1136, -82.3666);
    const QString g = QString(QChar(0x00B0));
    QCOMPARE(formatCoordinate(c, CoordinateFormat::Decimal), QStringLiteral("23.113600, -82.366600"));
    QCOMPARE(formatCoordinate(c, CoordinateFormat::DMS),
             QStringLiteral("23") + g + QStringLiteral("06'48.96\"N 82") + g + QStringLiteral("21'59.76\"W"));
    QCOMPARE(formatCoordinate(c, CoordinateFormat::DDM),
             QStringLiteral("23") + g + QStringLiteral("06.8160'N 82") + g + QStringLiteral("21.9960'W"));
    QCOMPARE(formatCoordinate(c, CoordinateFormat::UTM), QStringLiteral("17Q 360057 2556751"));
    QCOMPARE(formatCoordinate(c, CoordinateFormat::MGRS), QStringLiteral("17Q LF 60056 56750"));
    QCOMPARE(formatCoordinate(QGeoCoordinate(-33.8568, 151.2153), CoordinateFormat::UTM),
             QStringLiteral("56H 334901 6252289"));
}

// Lo que escribiria una persona, en cualquier formato.
void TstCoordinates::parsesHumanInput()
{
    const QGeoCoordinate ref(23.1136, -82.3666);
    const QString g = QString(QChar(0x00B0));
    const QStringList entradas = {
        QStringLiteral("23.1136, -82.3666"),
        QStringLiteral("23.1136 -82.3666"),
        QStringLiteral("17QLF6005656750"),
        QStringLiteral("17Q LF 60056 56750"),
        QStringLiteral("17Q 360057 2556751"),
        QStringLiteral("23") + g + QStringLiteral("06'48.96\"N 82") + g + QStringLiteral("21'59.76\"W"),
        QStringLiteral("23") + g + QStringLiteral(" 06.816' N, 82") + g + QStringLiteral(" 21.996' O"),
        QStringLiteral("82") + g + QStringLiteral("21'59.76\"W 23") + g + QStringLiteral("06'48.96\"N"),
    };
    for (const QString &e : entradas) {
        bool ok = false;
        const QGeoCoordinate c = parseCoordinate(e, &ok);
        QVERIFY2(ok, qPrintable(e));
        QVERIFY2(c.distanceTo(ref) < 2.0, qPrintable(e + QStringLiteral(" -> %1 m").arg(c.distanceTo(ref))));
    }
    bool ok = true;
    parseCoordinate(QStringLiteral("no es una coordenada"), &ok);
    QVERIFY(!ok);
    parseCoordinate(QStringLiteral("95.0, 10.0"), &ok);          // latitud imposible
    QVERIFY(!ok);
}

QTEST_APPLESS_MAIN(TstCoordinates)
#include "tst_coordinates.moc"
