#include "libmapa/MapFeature.h"

#include <QtTest>

#include <limits>

using namespace libmapa;

//! Tests de los circulos y arcos geodesicos (calculos puros, sin ventanas): el
//! barrido, la validez, el contorno sobre el elipsoide y el aplanado para exportar.
class TstRoundGeometry : public QObject
{
    Q_OBJECT

private slots:
    void enumValuesAreFrozen();
    void arcSweepTable_data();
    void arcSweepTable();
    void validity_data();
    void validity();
    void circleOutlineKeepsRadiusAndIsClosedArea();
    void arcOutlineFollowsClockwiseSweep();
    void arcWrapsAroundNorth();
    void largeRadiusAtHighLatitudeStaysGeodesic();
    void storedPartsKeepOnlyTheCenter();
    void plainCopyForExport();
    void labelPointOfAnArcIsItsMiddle();
};

// Una entidad redonda de ejemplo en La Habana; el llamador ajusta el tipo y los datos.
static MapFeature redonda(GeometryKind tipo, double radio, double ini = 0.0, double fin = 0.0,
                          const QGeoCoordinate &centro = QGeoCoordinate(23.1136, -82.3666))
{
    MapFeature f;
    f.kind = tipo;
    f.geometry = { centro };
    f.radiusMeters = radio;
    f.arcStartDeg = ini;
    f.arcEndDeg = fin;
    return f;
}

// El tipo se guarda como entero en SQLite: los valores de siempre no pueden moverse y
// los nuevos van al final.
void TstRoundGeometry::enumValuesAreFrozen()
{
    QCOMPARE(int(GeometryKind::Point), 0);
    QCOMPARE(int(GeometryKind::Polyline), 1);
    QCOMPARE(int(GeometryKind::Polygon), 2);
    QCOMPARE(int(GeometryKind::Circle), 3);
    QCOMPARE(int(GeometryKind::Arc), 4);
}

// Barridos de los anexos (incluidos los dos casos que daban error con QPainter::drawArc).
void TstRoundGeometry::arcSweepTable_data()
{
    QTest::addColumn<double>("inicio");
    QTest::addColumn<double>("fin");
    QTest::addColumn<double>("barrido");

    QTest::newRow("0 a 90")        << 0.0   << 90.0  << 90.0;
    QTest::newRow("45 a 135")      << 45.0  << 135.0 << 90.0;
    QTest::newRow("315 a 45")      << 315.0 << 45.0  << 90.0;    // da la vuelta: 90, no 270
    QTest::newRow("180 a 270")     << 180.0 << 270.0 << 90.0;
    QTest::newRow("0 a 180")       << 0.0   << 180.0 << 180.0;
    QTest::newRow("90 a 270")      << 90.0  << 270.0 << 180.0;
    QTest::newRow("0 a 359")       << 0.0   << 359.0 << 359.0;
    QTest::newRow("45 a 315")      << 45.0  << 315.0 << 270.0;
    QTest::newRow("270 a 90")      << 270.0 << 90.0  << 180.0;
    QTest::newRow("iguales")       << 10.0  << 10.0  << 0.0;
    QTest::newRow("0 a 360")       << 0.0   << 360.0 << 0.0;     // vuelta entera: es un Circle
    QTest::newRow("inicio negativo") << -45.0 << 45.0 << 90.0;
    QTest::newRow("mas de 360")    << 350.0 << 730.0 << 20.0;
}

// El barrido siempre va de inicio a fin en sentido horario.
void TstRoundGeometry::arcSweepTable()
{
    QFETCH(double, inicio);
    QFETCH(double, fin);
    QFETCH(double, barrido);
    QVERIFY(qAbs(MapFeature::arcSweepDeg(inicio, fin) - barrido) < 1e-9);
}

// Validez de circulos y arcos: un vertice (el centro), radio finito y positivo, sin
// partes y, en un arco, un barrido distinto de cero. La tabla lleva valores simples
// (un MapFeature lleva un QPixmap y no se puede guardar en los datos sin una
// aplicacion grafica) y la entidad se monta en el test.
void TstRoundGeometry::validity_data()
{
    QTest::addColumn<int>("tipo");
    QTest::addColumn<double>("radio");
    QTest::addColumn<double>("inicio");
    QTest::addColumn<double>("fin");
    QTest::addColumn<int>("vertices");
    QTest::addColumn<bool>("conPartes");
    QTest::addColumn<bool>("valida");

    const int C = int(GeometryKind::Circle), A = int(GeometryKind::Arc);
    const double nan = std::nan(""), inf = std::numeric_limits<double>::infinity();
    QTest::newRow("circulo")                << C << 1000.0 << 0.0 << 0.0 << 1 << false << true;
    QTest::newRow("circulo radio 0")        << C << 0.0 << 0.0 << 0.0 << 1 << false << false;
    QTest::newRow("circulo radio negativo") << C << -5.0 << 0.0 << 0.0 << 1 << false << false;
    QTest::newRow("circulo radio NaN")      << C << nan << 0.0 << 0.0 << 1 << false << false;
    QTest::newRow("circulo radio infinito") << C << inf << 0.0 << 0.0 << 1 << false << false;
    QTest::newRow("arco")                   << A << 1000.0 << 315.0 << 45.0 << 1 << false << true;
    QTest::newRow("arco iguales")           << A << 1000.0 << 90.0 << 90.0 << 1 << false << false;
    QTest::newRow("arco 0 a 360")           << A << 1000.0 << 0.0 << 360.0 << 1 << false << false;
    QTest::newRow("arco azimut NaN")        << A << 1000.0 << nan << 90.0 << 1 << false << false;
    QTest::newRow("circulo con dos vertices") << C << 1000.0 << 0.0 << 0.0 << 2 << false << false;
    QTest::newRow("circulo con partes")     << C << 1000.0 << 0.0 << 0.0 << 1 << true << false;
    QTest::newRow("arco sin centro")        << A << 1000.0 << 0.0 << 90.0 << 0 << false << false;
}

void TstRoundGeometry::validity()
{
    QFETCH(int, tipo);
    QFETCH(double, radio);
    QFETCH(double, inicio);
    QFETCH(double, fin);
    QFETCH(int, vertices);
    QFETCH(bool, conPartes);
    QFETCH(bool, valida);

    MapFeature f = redonda(GeometryKind(tipo), radio, inicio, fin);
    f.geometry.clear();
    for (int i = 0; i < vertices; ++i)
        f.geometry.append(QGeoCoordinate(23.1136 + 0.1 * i, -82.3666));
    if (conPartes)
        f.parts = { f.geometry };
    QCOMPARE(f.isValid(), valida);
}

// Un circulo es un area cerrada de puntos a la misma distancia del centro, sin repetir
// el primero, y su tamano no depende del zoom.
void TstRoundGeometry::circleOutlineKeepsRadiusAndIsClosedArea()
{
    const MapFeature c = redonda(GeometryKind::Circle, 25000.0);
    QVERIFY(c.isRound() && c.isArea() && !c.isLinear());
    const QVector<QGeoCoordinate> contorno = c.roundOutline();
    QCOMPARE(contorno.size(), 180);                    // un punto cada 2 grados
    QVERIFY(contorno.first() != contorno.last());       // no repite el primero
    for (const QGeoCoordinate &p : contorno)
        QVERIFY2(qAbs(c.geometry.first().distanceTo(p) - 25000.0) < 0.5,
                 "un punto no esta a la distancia del radio");
    // Empieza al norte y va en sentido horario.
    const double az0 = c.geometry.first().azimuthTo(contorno.at(0));
    QVERIFY(qAbs(az0) < 0.01 || qAbs(az0 - 360.0) < 0.01);
    QVERIFY(qAbs(c.geometry.first().azimuthTo(contorno.at(45)) - 90.0) < 0.01);
    QCOMPARE(c.outlines().size(), 1);
    QCOMPARE(c.outlines().first(), contorno);
}

// Un arco empieza en su azimut de inicio, acaba en el de fin y pasa por todos los
// intermedios, siempre en sentido horario.
void TstRoundGeometry::arcOutlineFollowsClockwiseSweep()
{
    const MapFeature a = redonda(GeometryKind::Arc, 10000.0, 45.0, 135.0);
    QVERIFY(a.isRound() && a.isLinear() && !a.isArea());
    const QVector<QGeoCoordinate> contorno = a.roundOutline();
    QCOMPARE(contorno.size(), 46);                     // 90 grados / 2 = 45 tramos
    const QGeoCoordinate centro = a.geometry.first();
    QVERIFY(qAbs(centro.azimuthTo(contorno.first()) - 45.0) < 0.01);
    QVERIFY(qAbs(centro.azimuthTo(contorno.last()) - 135.0) < 0.01);
    double anterior = -1.0;
    for (const QGeoCoordinate &p : contorno) {
        QVERIFY(qAbs(centro.distanceTo(p) - 10000.0) < 0.5);
        const double az = centro.azimuthTo(p);
        QVERIFY2(az > anterior, "los azimuts deben crecer en sentido horario");
        anterior = az;
    }
}

// De 315 a 45 el arco pasa por el norte (90 grados), no por el sur (270): es el caso
// que salia mal al convertir a QPainter::drawArc.
void TstRoundGeometry::arcWrapsAroundNorth()
{
    const MapFeature a = redonda(GeometryKind::Arc, 10000.0, 315.0, 45.0);
    const QVector<QGeoCoordinate> contorno = a.roundOutline();
    QCOMPARE(contorno.size(), 46);
    const QGeoCoordinate centro = a.geometry.first();
    // El punto de en medio esta al norte del centro (a 1 grado como mucho: 45 tramos
    // no tienen un punto justo en el medio).
    const QGeoCoordinate medio = contorno.at(contorno.size() / 2);
    QVERIFY(medio.latitude() > centro.latitude());
    QVERIFY(qAbs(centro.azimuthTo(medio)) < 1.01 || qAbs(centro.azimuthTo(medio) - 360.0) < 1.01);
    // Y ninguno cae al sur del centro.
    for (const QGeoCoordinate &p : contorno)
        QVERIFY(p.latitude() > centro.latitude());
}

// Con 300 km a 60 grados de latitud la figura es geodesica: todos los puntos siguen a
// 300 km del centro, aunque en Mercator no se vea como un circulo.
void TstRoundGeometry::largeRadiusAtHighLatitudeStaysGeodesic()
{
    const MapFeature c = redonda(GeometryKind::Circle, 300000.0, 0.0, 0.0,
                                 QGeoCoordinate(60.0, 10.0));
    QVERIFY(c.isValid());
    for (const QGeoCoordinate &p : c.roundOutline())
        QVERIFY(qAbs(c.geometry.first().distanceTo(p) - 300000.0) < 1.0);
}

// Lo que se guarda es el centro; el contorno se recalcula al leer.
void TstRoundGeometry::storedPartsKeepOnlyTheCenter()
{
    const MapFeature c = redonda(GeometryKind::Circle, 5000.0);
    QCOMPARE(c.storedParts().size(), 1);
    QCOMPARE(c.storedParts().first().size(), 1);
    QCOMPARE(c.storedParts().first().first(), c.geometry.first());
    QCOMPARE(c.position(), c.geometry.first());
    // Una entidad normal guarda sus partes tal cual.
    MapFeature linea;
    linea.kind = GeometryKind::Polyline;
    linea.geometry = { QGeoCoordinate(23.0, -82.0), QGeoCoordinate(23.1, -82.1) };
    QCOMPARE(linea.storedParts(), linea.outlines());
}

// Para exportar a GeoJSON, KML o GPX: un circulo pasa a poligono y un arco a linea, con
// el centro, el radio y los azimuts como atributos para no perderlos.
void TstRoundGeometry::plainCopyForExport()
{
    MapFeature c = redonda(GeometryKind::Circle, 5000.0);
    c.name = QStringLiteral("TMA");
    const MapFeature pc = c.toPlain();
    QCOMPARE(pc.kind, GeometryKind::Polygon);
    QCOMPARE(pc.geometry.size(), 180);
    QVERIFY(pc.isValid());
    QCOMPARE(pc.attributes.value(QStringLiteral("radius_m")).toDouble(), 5000.0);
    QCOMPARE(pc.name, QStringLiteral("TMA"));

    const MapFeature pa = redonda(GeometryKind::Arc, 8000.0, 315.0, 45.0).toPlain();
    QCOMPARE(pa.kind, GeometryKind::Polyline);
    QCOMPARE(pa.geometry.size(), 46);
    QCOMPARE(pa.attributes.value(QStringLiteral("arc_start_deg")).toDouble(), 315.0);
    QCOMPARE(pa.attributes.value(QStringLiteral("arc_end_deg")).toDouble(), 45.0);

    // Una entidad normal no cambia.
    MapFeature p;
    p.kind = GeometryKind::Point;
    p.geometry = { QGeoCoordinate(23.0, -82.0) };
    QCOMPARE(p.toPlain().kind, GeometryKind::Point);
    QVERIFY(p.toPlain().attributes.isEmpty());
}

// El rotulo de un arco cuelga de la mitad de su recorrido; el de un circulo, de su punto norte.
void TstRoundGeometry::labelPointOfAnArcIsItsMiddle()
{
    const MapFeature a = redonda(GeometryKind::Arc, 10000.0, 315.0, 45.0);
    const QGeoCoordinate m = a.roundLabelPoint();
    QVERIFY(m.isValid());
    QVERIFY(qAbs(a.geometry.first().distanceTo(m) - 10000.0) < 0.5);
    QVERIFY(m.latitude() > a.geometry.first().latitude());     // al norte

    const MapFeature c = redonda(GeometryKind::Circle, 10000.0);
    QVERIFY(qAbs(c.geometry.first().distanceTo(c.roundLabelPoint()) - 10000.0) < 0.5);
    QVERIFY(c.roundLabelPoint().latitude() > c.geometry.first().latitude());

    MapFeature p;
    p.kind = GeometryKind::Point;
    p.geometry = { QGeoCoordinate(23.0, -82.0) };
    QVERIFY(!p.roundLabelPoint().isValid());
}

QTEST_MAIN(TstRoundGeometry)
#include "tst_roundgeometry.moc"
