#include "widget/TargetModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace libmapa;

//! Tests del modelo de objetivos moviles, sin ventanas.
class TstTargetModel : public QObject
{
    Q_OBJECT

private slots:
    void assignsIdAndOpensTrail();
    void updateStreamsPositionsIntoTrail();
    void capsTrailLength();
    void updateRejectsUnknownOrInvalid();
    void labelAndRemoveAndClear();
    void scalesToManyTargets();
};

static MapTarget objetivo(double lat, double lon, const QString &etq = QString())
{
    MapTarget t;
    t.position = QGeoCoordinate(lat, lon);
    t.label = etq;
    return t;
}

void TstTargetModel::assignsIdAndOpensTrail()
{
    TargetModel m;
    QSignalSpy cambios(&m, &TargetModel::changed);

    const qint64 id = m.upsert(objetivo(23.0, -82.0, QStringLiteral("A")));
    QVERIFY(id > 0);
    QCOMPARE(m.count(), 1);
    QVERIFY(cambios.count() >= 1);

    // La posicion inicial abre la traza.
    QCOMPARE(m.trail(id).size(), 1);

    // Un id propio se respeta y no colisiona con los autoasignados.
    MapTarget conId = objetivo(23.1, -82.1);
    conId.id = 5000;
    QCOMPARE(m.upsert(conId), qint64(5000));
    QVERIFY(m.upsert(objetivo(23.2, -82.2)) > 5000);
}

void TstTargetModel::updateStreamsPositionsIntoTrail()
{
    TargetModel m;
    const qint64 id = m.upsert(objetivo(23.0, -82.0));

    QVERIFY(m.update(id, QGeoCoordinate(23.01, -82.0), 90.0));
    QVERIFY(m.update(id, QGeoCoordinate(23.02, -82.0)));   // sin rumbo: se conserva

    const auto t = m.target(id);
    QVERIFY(t.has_value());
    QCOMPARE(t->position, QGeoCoordinate(23.02, -82.0));
    QCOMPARE(t->headingDeg, 90.0);                          // el ultimo valido
    QCOMPARE(m.trail(id).size(), 3);                        // inicial + 2
}

void TstTargetModel::capsTrailLength()
{
    TargetModel m;
    m.setTrailMaxPoints(4);
    const qint64 id = m.upsert(objetivo(23.0, -82.0));
    for (int i = 1; i <= 10; ++i)
        m.update(id, QGeoCoordinate(23.0 + i * 0.001, -82.0));

    QCOMPARE(m.trail(id).size(), 4);       // acotada
    // Conserva las ULTIMAS posiciones.
    QCOMPARE(m.trail(id).last(), QGeoCoordinate(23.01, -82.0));

    // Reducir el maximo poda de inmediato.
    m.setTrailMaxPoints(2);
    QCOMPARE(m.trail(id).size(), 2);
}

void TstTargetModel::updateRejectsUnknownOrInvalid()
{
    TargetModel m;
    const qint64 id = m.upsert(objetivo(23.0, -82.0));
    QVERIFY(!m.update(9999, QGeoCoordinate(23.0, -82.0)));       // no existe
    QVERIFY(!m.update(id, QGeoCoordinate(200.0, -82.0)));        // invalida
    QCOMPARE(m.upsert(objetivo(0.0, 0.0)) > 0 ? 0 : -1, 0);      // (0,0) es valido
    MapTarget malo;                                             // sin posicion
    QCOMPARE(m.upsert(malo), qint64(-1));
}

void TstTargetModel::labelAndRemoveAndClear()
{
    TargetModel m;
    const qint64 id = m.upsert(objetivo(23.0, -82.0));
    QVERIFY(m.setLabel(id, QStringLiteral("Buque 1")));
    QCOMPARE(m.target(id)->label, QStringLiteral("Buque 1"));

    QVERIFY(m.remove(id));
    QVERIFY(!m.remove(id));
    QCOMPARE(m.count(), 0);

    m.upsert(objetivo(23.0, -82.0));
    m.upsert(objetivo(23.1, -82.1));
    m.clear();
    QCOMPARE(m.count(), 0);
}

void TstTargetModel::scalesToManyTargets()
{
    // Requisito: cientos de objetivos simultaneos. Se comprueba que el modelo
    // los mantiene y actualiza sin degenerar; el dibujo se mide aparte.
    TargetModel m;
    m.setTrailMaxPoints(200);

    const int N = 250;
    QVector<qint64> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i)
        ids.append(m.upsert(objetivo(20.0 + i * 0.01, -80.0 - i * 0.01,
                                     QStringLiteral("T%1").arg(i))));
    QCOMPARE(m.count(), N);

    // 20 actualizaciones por objetivo = 5000 posiciones.
    for (int paso = 1; paso <= 20; ++paso)
        for (int i = 0; i < N; ++i)
            QVERIFY(m.update(ids[i],
                QGeoCoordinate(20.0 + i * 0.01 + paso * 0.001, -80.0 - i * 0.01)));

    QCOMPARE(m.count(), N);
    for (qint64 id : ids)
        QVERIFY(m.trail(id).size() <= 200);     // traza acotada en todos
}

QTEST_MAIN(TstTargetModel)
#include "tst_targetmodel.moc"
