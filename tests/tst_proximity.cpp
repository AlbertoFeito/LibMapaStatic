#include "geo/ProximityMonitor.h"
#include "libmapa/Proximity.h"

#include <QElapsedTimer>
#include <QtTest>

#include <cmath>

using namespace libmapa;

using Track = ProximityMonitor::Track;
using Transition = ProximityMonitor::Transition;

//! Tests del CPA/TCPA y de las alertas de proximidad, sin ventanas.
class TstProximity : public QObject
{
    Q_OBJECT

private slots:
    void headOnMeetsHalfway();
    void crossingCourses();
    void parallelAndDiverging();
    void rejectsBadInput();
    void crossesAntimeridian();
    void monitorRaisesRefreshesAndClears();
    void monitorDeadReckonsOlderFixes();
    void criteriaChangeReevaluates();
    void removalsAreSilentInvalidLeaves();
    void scalesToManyTracks();
};

// Punto a \a metros al este (azimut 90) o donde diga \a azimut, desde \a p.
static QGeoCoordinate desde(const QGeoCoordinate &p, double metros, double azimut = 90.0)
{
    return p.atDistanceAndAzimuth(metros, azimut);
}

// Frente a frente a 10 m/s cada uno, separados ~11.1 km: se encuentran en el
// medio (CPA ~0) a los 556 s, y los dos puntos de CPA casi coinciden.
void TstProximity::headOnMeetsHalfway()
{
    const QGeoCoordinate a(0.0, 0.0), b(0.0, 0.1);
    const CpaResult c = computeCpa(a, 10.0, 90.0, b, 10.0, 270.0);
    QVERIFY(c.valid);
    QVERIFY(c.distanceMeters < 1.0);
    QVERIFY(qAbs(c.currentDistanceMeters - 11119.5) < 1.0);
    QVERIFY(qAbs(c.timeSeconds - c.currentDistanceMeters / 20.0) < 1.0);
    QVERIFY(c.positionA.distanceTo(c.positionB) < 2.0);
    QVERIFY(qAbs(c.positionA.longitude() - 0.05) < 1e-4);
}

// A al norte a 5 m/s; B a 1000 m al este, hacia el oeste a 5 m/s. Velocidad
// relativa (-5,-5): TCPA = 100 s y CPA = |(500,-500)| = 707.1 m.
void TstProximity::crossingCourses()
{
    const QGeoCoordinate a(10.0, 20.0);
    const CpaResult c = computeCpa(a, 5.0, 0.0, desde(a, 1000.0), 5.0, 270.0);
    QVERIFY(c.valid);
    QVERIFY(qAbs(c.timeSeconds - 100.0) < 0.1);
    QVERIFY(qAbs(c.distanceMeters - 707.1) < 0.5);
    // En el CPA, A esta 500 m al norte de donde salio y B 500 m al oeste del suyo.
    QVERIFY(qAbs(a.distanceTo(c.positionA) - 500.0) < 0.5);
    QVERIFY(qAbs(c.positionA.distanceTo(c.positionB) - 707.1) < 1.0);

    // En nudos: 1 kn = 0.5144 m/s.
    QVERIFY(qAbs(knotsToMetersPerSecond(10.0) - 5.1444) < 1e-3);
}

// Paralelos a la misma velocidad: la distancia no cambia (TCPA 0, CPA la
// actual). Alejandose: el CPA ya paso (TCPA negativo).
void TstProximity::parallelAndDiverging()
{
    const QGeoCoordinate a(36.0, -5.0), b = desde(QGeoCoordinate(36.0, -5.0), 2000.0, 0.0);
    CpaResult c = computeCpa(a, 7.0, 45.0, b, 7.0, 45.0);
    QVERIFY(c.valid);
    QCOMPARE(c.timeSeconds, 0.0);
    QVERIFY(qAbs(c.distanceMeters - 2000.0) < 2.0);

    // Quietos: igual.
    c = computeCpa(a, 0.0, 0.0, b, 0.0, 0.0);
    QCOMPARE(c.timeSeconds, 0.0);

    // B se aleja al norte mas rapido que A le sigue: el CPA fue en el pasado.
    c = computeCpa(a, 3.0, 0.0, b, 8.0, 0.0);
    QVERIFY(c.timeSeconds < 0.0);
}

// Datos invalidos: resultado invalido, sin NaN propagados como si nada.
void TstProximity::rejectsBadInput()
{
    const QGeoCoordinate a(0.0, 0.0);
    QVERIFY(!computeCpa(QGeoCoordinate(), 1.0, 0.0, a, 1.0, 0.0).valid);
    QVERIFY(!computeCpa(a, std::nan(""), 0.0, a, 1.0, 0.0).valid);
    QVERIFY(!computeCpa(a, 1.0, std::numeric_limits<double>::infinity(), a, 1.0, 0.0).valid);
}

// A ambos lados de +-180: la diferencia de longitud es pequeña, no 360.
void TstProximity::crossesAntimeridian()
{
    const CpaResult c = computeCpa(QGeoCoordinate(0.0, 179.99), 5.0, 90.0,
                                   QGeoCoordinate(0.0, -179.99), 5.0, 270.0);
    QVERIFY(c.valid);
    QVERIFY(c.currentDistanceMeters < 2300.0);
    QVERIFY(c.distanceMeters < 1.0);
    QVERIFY(qAbs(c.timeSeconds - c.currentDistanceMeters / 10.0) < 1.0);
}

// Alerta a 500 m con horizonte de 10 min. Dos buques en rumbo de colision a
// 5 km: alerta al acercarse; mientras sigue, se refresca sin transicion; al
// cambiar uno de rumbo, la alerta termina.
void TstProximity::monitorRaisesRefreshesAndClears()
{
    ProximityMonitor m;
    QVERIFY(m.setCriteria(500.0, 600.0).isEmpty());
    const QGeoCoordinate a(20.0, -80.0);
    const qint64 t0 = 1760000000000LL;

    QVERIFY(m.updateTrack(1, { a, 5.0, 90.0, t0 }).isEmpty());
    // B a 5 km al este, hacia el oeste a 5 m/s: TCPA 500 s, dentro del horizonte.
    QVector<Transition> c = m.updateTrack(2, { desde(a, 5000.0), 5.0, 270.0, t0 });
    QCOMPARE(c.size(), 1);
    QCOMPARE(c[0].a, qint64(1));
    QCOMPARE(c[0].b, qint64(2));
    QVERIFY(c[0].raised);
    QVERIFY(qAbs(c[0].cpa.timeSeconds - 500.0) < 1.0);
    QVERIFY(m.isAlerted(2, 1));
    QCOMPARE(m.alerts().size(), 1);

    // Cien segundos despues, A sigue igual: alerta viva, CPA refrescado.
    QVERIFY(m.updateTrack(1, { desde(a, 500.0), 5.0, 90.0, t0 + 100000 }).isEmpty());
    QVERIFY(qAbs(m.alerts().first().cpa.timeSeconds - 400.0) < 1.5);

    // A cae 90 grados al norte: el CPA sube de 500 m y la alerta termina.
    c = m.updateTrack(1, { desde(a, 500.0), 5.0, 0.0, t0 + 100000 });
    QCOMPARE(c.size(), 1);
    QVERIFY(!c[0].raised);
    QVERIFY(m.alerts().isEmpty());

    // Fuera del horizonte: mismo encuentro pero a 50 km (TCPA 5000 s): nada.
    QVERIFY(m.updateTrack(3, { desde(a, 50000.0, 270.0), 5.0, 90.0, t0 }).isEmpty());
    // Ya dentro de la distancia aunque se alejen: alerta. (A esta ahora 500 m
    // al este de a, rumbo norte; el 4 sale 300 m al norte de A, mas rapido.)
    c = m.updateTrack(4, { desde(desde(a, 500.0), 300.0, 0.0), 8.0, 0.0, t0 + 100000 });
    QCOMPARE(c.size(), 1);
    QVERIFY(c[0].cpa.timeSeconds < 0.0);
    QCOMPARE(c[0].a, qint64(1));
    QCOMPARE(c[0].b, qint64(4));
}

// B informo hace 60 s desde 6 km; por estima ya esta a 5.7 km. Sin estima el
// TCPA saldria 600 s (fuera de un horizonte de 590 s); con ella, 570 s.
void TstProximity::monitorDeadReckonsOlderFixes()
{
    ProximityMonitor m;
    m.setCriteria(200.0, 590.0);
    const QGeoCoordinate a(0.0, 0.0);
    const qint64 t0 = 1760000000000LL;
    m.updateTrack(2, { desde(a, 6000.0), 5.0, 270.0, t0 - 60000 });
    const QVector<Transition> c = m.updateTrack(1, { a, 5.0, 90.0, t0 });
    QCOMPARE(c.size(), 1);
    QVERIFY(qAbs(c[0].cpa.timeSeconds - 570.0) < 1.5);
    QVERIFY(qAbs(c[0].cpa.currentDistanceMeters - 5700.0) < 3.0);
    // cpa() da lo mismo y en el orden pedido.
    const CpaResult directo = m.cpa(2, 1);
    QVERIFY(directo.valid);
    QVERIFY(qAbs(directo.timeSeconds - 570.0) < 1.5);
    QVERIFY(!m.cpa(1, 99).valid);
}

// Cambiar los criterios reevalua todos los pares; distancia 0 apaga todo.
void TstProximity::criteriaChangeReevaluates()
{
    ProximityMonitor m;
    const QGeoCoordinate a(0.0, 0.0);
    // Sin criterios no se calcula ni se avisa.
    QVERIFY(m.updateTrack(1, { a, 0.0, 0.0, 0 }).isEmpty());
    QVERIFY(m.updateTrack(2, { desde(a, 800.0), 0.0, 0.0, 0 }).isEmpty());
    QVERIFY(m.updateTrack(3, { desde(a, 3000.0), 0.0, 0.0, 0 }).isEmpty());

    QCOMPARE(m.setCriteria(1000.0, 60.0).size(), 1);          // 1-2
    QVector<Transition> c = m.setCriteria(2500.0, 60.0);       // + 2-3 (2200 m)
    QCOMPARE(c.size(), 1);
    QCOMPARE(c[0].a, qint64(2));
    QCOMPARE(c[0].b, qint64(3));
    QCOMPARE(m.alerts().size(), 2);

    c = m.setCriteria(0.0, 60.0);
    QCOMPARE(c.size(), 2);
    QVERIFY(!c[0].raised && !c[1].raised);
    QVERIFY(!m.isEnabled());
}

// Borrar no avisa; perder la posicion (invalida) si saca de las alertas.
void TstProximity::removalsAreSilentInvalidLeaves()
{
    ProximityMonitor m;
    m.setCriteria(1000.0, 60.0);
    const QGeoCoordinate a(0.0, 0.0);
    m.updateTrack(1, { a, 0.0, 0.0, 0 });
    m.updateTrack(2, { desde(a, 100.0), 0.0, 0.0, 0 });
    m.updateTrack(3, { desde(a, 200.0), 0.0, 0.0, 0 });
    QCOMPARE(m.alerts().size(), 3);

    m.removeTrack(3);
    QCOMPARE(m.alerts().size(), 1);
    QVERIFY(!m.hasTrack(3));

    const QVector<Transition> c = m.updateTrack(2, { QGeoCoordinate(), 0.0, 0.0, 0 });
    QCOMPARE(c.size(), 1);
    QVERIFY(!c[0].raised);
    QVERIFY(!m.hasTrack(2));

    m.clear();
    QVERIFY(m.alerts().isEmpty());
}

// 2000 moviles repartidos en un cuadrado de ~2 grados: una pasada completa de
// actualizaciones (cada una contra todos) con el descarte barato. Comprueba que
// el resultado cuadra con el calculo por fuerza bruta y que no se dispara.
void TstProximity::scalesToManyTracks()
{
    ProximityMonitor m;
    m.setCriteria(1852.0, 600.0);                 // 1 NM, 10 min
    QVector<Track> tracks;
    quint32 semilla = 12345;
    const auto azar = [&semilla] {
        semilla = semilla * 1664525u + 1013904223u;
        return double(semilla >> 8) / double(1u << 24);
    };
    for (int i = 0; i < 2000; ++i)
        tracks.append({ QGeoCoordinate(20.0 + 2.0 * azar(), -80.0 + 2.0 * azar()),
                        2.0 + 8.0 * azar(), 360.0 * azar(), 0 });

    QElapsedTimer reloj;
    reloj.start();
    for (int i = 0; i < tracks.size(); ++i)
        m.updateTrack(i, tracks[i]);
    const qint64 ms = reloj.elapsed();

    int fuerzaBruta = 0;
    for (int i = 0; i < tracks.size(); ++i)
        for (int j = i + 1; j < tracks.size(); ++j) {
            const CpaResult c = computeCpa(tracks[i].position, tracks[i].speed, tracks[i].course,
                                           tracks[j].position, tracks[j].speed, tracks[j].course);
            if (c.currentDistanceMeters <= 1852.0
                || (c.distanceMeters <= 1852.0 && c.timeSeconds >= 0 && c.timeSeconds <= 600.0))
                ++fuerzaBruta;
        }
    QCOMPARE(int(m.alerts().size()), fuerzaBruta);
    QVERIFY(fuerzaBruta > 0);
    qInfo("2000 moviles: %lld ms, %d pares en alerta", ms, fuerzaBruta);
    QVERIFY(ms < 5000);
}

QTEST_APPLESS_MAIN(TstProximity)
#include "tst_proximity.moc"
