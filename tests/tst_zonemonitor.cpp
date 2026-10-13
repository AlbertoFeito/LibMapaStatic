#include "geo/ZoneMonitor.h"

#include <QtTest>

#include <algorithm>

using namespace libmapa;

using Transition = ZoneMonitor::Transition;

//! Tests de la logica de alertas de zona (quien esta dentro de que), sin ventanas.
class TstZoneMonitor : public QObject
{
    Q_OBJECT

private slots:
    void entersAndLeaves();
    void watchingReportsTargetsAlreadyInside();
    void multiPartCountsAnyRing();
    void reshapingZoneReevaluates();
    void removalsAreSilent();
    void invalidPositionLeavesEveryZone();
    void edgesAreStraightInMercator();
    void manyTargetsAndZones();
};

// Anillo cuadrado de lado 2*r grados centrado en (lat, lon).
static QVector<QGeoCoordinate> cuadrado(double lat, double lon, double r)
{
    return { QGeoCoordinate(lat - r, lon - r), QGeoCoordinate(lat - r, lon + r),
             QGeoCoordinate(lat + r, lon + r), QGeoCoordinate(lat + r, lon - r) };
}

// Ordena transiciones por objetivo y luego por zona, para comparar sin depender
// del orden de un QHash.
static QVector<Transition> ordenadas(QVector<Transition> t)
{
    std::sort(t.begin(), t.end(), [](const Transition &a, const Transition &b) {
        return a.targetId != b.targetId ? a.targetId < b.targetId : a.zoneId < b.zoneId;
    });
    return t;
}

// Un objetivo que entra y sale de una zona produce exactamente una entrada y una
// salida; moverse dentro (o fuera) no produce nada.
void TstZoneMonitor::entersAndLeaves()
{
    ZoneMonitor m;
    QVERIFY(m.setZone(10, { cuadrado(23.0, -82.0, 0.5) }).isEmpty());

    QVERIFY(m.updateTarget(1, QGeoCoordinate(25.0, -82.0)).isEmpty());   // fuera
    QCOMPARE(m.updateTarget(1, QGeoCoordinate(23.1, -82.1)),
             QVector<Transition>({ {1, 10, true} }));
    QVERIFY(m.updateTarget(1, QGeoCoordinate(23.2, -81.9)).isEmpty());   // sigue dentro
    QVERIFY(m.isInside(1, 10));
    QCOMPARE(m.targetsIn(10), QVector<qint64>({1}));
    QCOMPARE(m.zonesOf(1), QVector<qint64>({10}));

    QCOMPARE(m.updateTarget(1, QGeoCoordinate(24.0, -82.0)),
             QVector<Transition>({ {1, 10, false} }));
    QVERIFY(m.updateTarget(1, QGeoCoordinate(24.5, -82.0)).isEmpty());   // sigue fuera
    QVERIFY(m.targetsIn(10).isEmpty());
}

// Vigilar una zona nueva avisa de los objetivos que ya estaban dentro.
void TstZoneMonitor::watchingReportsTargetsAlreadyInside()
{
    ZoneMonitor m;
    m.updateTarget(1, QGeoCoordinate(23.0, -82.0));
    m.updateTarget(2, QGeoCoordinate(30.0, -70.0));
    m.updateTarget(3, QGeoCoordinate(23.3, -81.8));

    QCOMPARE(ordenadas(m.setZone(7, { cuadrado(23.0, -82.0, 0.5) })),
             QVector<Transition>({ {1, 7, true}, {3, 7, true} }));
    QCOMPARE(m.targetsIn(7), QVector<qint64>({1, 3}));
}

// En una zona multi-parte basta con estar dentro de una de las partes.
void TstZoneMonitor::multiPartCountsAnyRing()
{
    ZoneMonitor m;
    m.setZone(1, { cuadrado(20.0, -80.0, 0.5), cuadrado(22.0, -78.0, 0.5) });

    QCOMPARE(m.updateTarget(5, QGeoCoordinate(22.1, -78.1)),
             QVector<Transition>({ {5, 1, true} }));
    // Pasa a la otra parte: sigue dentro de la misma zona, sin transicion.
    QVERIFY(m.updateTarget(5, QGeoCoordinate(20.1, -80.1)).isEmpty());
    // Entre las dos partes: fuera.
    QCOMPARE(m.updateTarget(5, QGeoCoordinate(21.0, -79.0)),
             QVector<Transition>({ {5, 1, false} }));
}

// Cambiar el contorno reevalua a todos; un contorno sin anillos validos quita
// la zona sacando a quien estuviera dentro.
void TstZoneMonitor::reshapingZoneReevaluates()
{
    ZoneMonitor m;
    m.updateTarget(1, QGeoCoordinate(23.0, -82.0));
    m.updateTarget(2, QGeoCoordinate(26.0, -82.0));
    m.setZone(3, { cuadrado(23.0, -82.0, 0.5) });
    QCOMPARE(m.targetsIn(3), QVector<qint64>({1}));

    // La zona se desplaza al norte: 1 sale, 2 entra.
    QCOMPARE(ordenadas(m.setZone(3, { cuadrado(26.0, -82.0, 0.5) })),
             QVector<Transition>({ {1, 3, false}, {2, 3, true} }));
    QCOMPARE(m.zoneRings(3), QVector<QVector<QGeoCoordinate>>({ cuadrado(26.0, -82.0, 0.5) }));

    // Dos vertices no son un poligono: la zona desaparece y 2 sale.
    const QVector<QGeoCoordinate> linea = { QGeoCoordinate(26.0, -82.0),
                                            QGeoCoordinate(27.0, -82.0) };
    QCOMPARE(m.setZone(3, { linea }), QVector<Transition>({ {2, 3, false} }));
    QVERIFY(!m.hasZone(3));
}

// Borrar objetivos o zonas no genera salidas: no se han ido, han dejado de existir.
void TstZoneMonitor::removalsAreSilent()
{
    ZoneMonitor m;
    m.setZone(1, { cuadrado(23.0, -82.0, 0.5) });
    m.setZone(2, { cuadrado(23.0, -82.0, 1.0) });
    m.updateTarget(10, QGeoCoordinate(23.0, -82.0));
    m.updateTarget(11, QGeoCoordinate(23.1, -82.0));
    QCOMPARE(m.zonesOf(10), QVector<qint64>({1, 2}));

    QCOMPARE(m.trackedTargets(), QVector<qint64>({10, 11}));
    m.removeTarget(10);
    QCOMPARE(m.trackedTargets(), QVector<qint64>({11}));
    QCOMPARE(m.targetsIn(1), QVector<qint64>({11}));
    QVERIFY(m.zonesOf(10).isEmpty());

    QVERIFY(m.removeZone(1));
    QVERIFY(!m.removeZone(1));
    QCOMPARE(m.zones(), QVector<qint64>({2}));
    QVERIFY(m.targetsIn(1).isEmpty());

    m.clearTargets();
    QVERIFY(m.targetsIn(2).isEmpty());
    // Tras olvidarlos, una zona nueva no los ve.
    QVERIFY(m.setZone(3, { cuadrado(23.0, -82.0, 2.0) }).isEmpty());
}

// Una posicion invalida (p.ej. el objetivo perdio el fijo) lo saca de todas.
void TstZoneMonitor::invalidPositionLeavesEveryZone()
{
    ZoneMonitor m;
    m.setZone(1, { cuadrado(23.0, -82.0, 0.5) });
    m.setZone(2, { cuadrado(23.0, -82.0, 1.0) });
    m.updateTarget(4, QGeoCoordinate(23.0, -82.0));

    QCOMPARE(ordenadas(m.updateTarget(4, QGeoCoordinate())),
             QVector<Transition>({ {4, 1, false}, {4, 2, false} }));
    QVERIFY(m.zonesOf(4).isEmpty());
}

// El lado de la zona es la recta que se DIBUJA (Web Mercator), no la recta en
// grados. Triangulo (0,0)-(0,60)-(60,60) [lat,lon]: a lon 30 la hipotenusa pasa
// por lat 30 en grados pero por lat ~35.3 en Mercator. Un punto a lat 33 se ve
// DENTRO en el mapa, y asi debe contar.
void TstZoneMonitor::edgesAreStraightInMercator()
{
    ZoneMonitor m;
    m.setZone(1, { { QGeoCoordinate(0.0, 0.0), QGeoCoordinate(0.0, 60.0),
                     QGeoCoordinate(60.0, 60.0) } });

    QCOMPARE(m.updateTarget(1, QGeoCoordinate(33.0, 30.0)),
             QVector<Transition>({ {1, 1, true} }));
    QVERIFY(m.updateTarget(2, QGeoCoordinate(37.0, 30.0)).isEmpty());   // por encima
}

// Muchos objetivos y zonas: los recuentos cuadran tras una pasada completa.
void TstZoneMonitor::manyTargetsAndZones()
{
    ZoneMonitor m;
    // 20 zonas en fila, de 1 grado, separadas 2 grados en longitud.
    for (int z = 0; z < 20; ++z)
        m.setZone(z, { cuadrado(10.0, -100.0 + 2.0 * z, 0.5) });

    // 2000 objetivos: los pares dentro de su zona, los impares entre zonas.
    int entradas = 0;
    for (int t = 0; t < 2000; ++t) {
        const int z = t % 20;
        const double lon = -100.0 + 2.0 * z + ((t % 2 == 0) ? 0.1 : 1.0);
        entradas += int(m.updateTarget(t, QGeoCoordinate(10.0, lon)).size());
    }
    QCOMPARE(entradas, 1000);
    int total = 0;
    for (qint64 z : m.zones())
        total += int(m.targetsIn(z).size());
    QCOMPARE(total, 1000);
}

QTEST_APPLESS_MAIN(TstZoneMonitor)
#include "tst_zonemonitor.moc"
