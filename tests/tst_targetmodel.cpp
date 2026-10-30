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
    void trailOptionsWholeOrNone();
    void updateRejectsUnknownOrInvalid();
    void labelAndRemoveAndClear();
    void carriesKindAndAttributes();
    void scalesToManyTargets();
    //! El historial guarda todo desde el ORIGEN aunque la traza dibujada sea corta.
    void historyKeepsOriginBeyondTrail();
    //! Al pasar del tope se aclara, pero origen y ultimo punto no se pierden.
    void historyThinningKeepsOriginAndLast();
    //! Distancia minima, horas dadas por la app y borrado.
    void historyTimesMinDistanceAndRemoval();
    void estimatesVelocityFromTrack();
    //! Offset de rotulo por defecto frente al propio: marca, quitar, restaurar.
    void labelOffsetDefaultOwnAndRestore();
    //! Cambiar el rotulo por segmentos de un objetivo ya creado (el estado que cambia de
    //! color) conserva su traza y avisa; un id que no existe da false.
    void labelContentCanChangeAfterCreation();
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

void TstTargetModel::trailOptionsWholeOrNone()
{
    TargetModel m;
    const qint64 id = m.upsert(objetivo(23.0, -82.0));

    // Toda la traza (ilimitada): se guardan todas las posiciones.
    m.setTrailMaxPoints(-1);
    for (int i = 1; i <= 50; ++i)
        m.update(id, QGeoCoordinate(23.0 + i * 0.001, -82.0));
    QCOMPARE(m.trail(id).size(), 51);          // inicial + 50

    // Sin traza: se vacia y no crece.
    m.setTrailMaxPoints(0);
    QCOMPARE(m.trail(id).size(), 0);
    m.update(id, QGeoCoordinate(23.1, -82.0));
    QCOMPARE(m.trail(id).size(), 0);

    // Un tope concreto vuelve a acotar.
    m.setTrailMaxPoints(10);
    for (int i = 1; i <= 30; ++i)
        m.update(id, QGeoCoordinate(23.1 + i * 0.001, -82.0));
    QCOMPARE(m.trail(id).size(), 10);
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

void TstTargetModel::carriesKindAndAttributes()
{
    // El modelo es agnostico del dominio: lleva 'kind' y un saco de atributos
    // libres (AIS/ADS-B/telemetria) sin interpretarlos, para que el mismo
    // MapTarget sirva a un buque, una aeronave o un UAV.
    TargetModel m;

    MapTarget t = objetivo(23.0, -82.0, QStringLiteral("Buque 1"));
    t.kind = QStringLiteral("buque");
    t.attributes.insert(QStringLiteral("mmsi"), QStringLiteral("224123000"));
    t.attributes.insert(QStringLiteral("eslora_m"), 180);
    const qint64 id = m.upsert(t);
    QVERIFY(id > 0);

    // upsert guarda kind y atributos completos.
    auto g = m.target(id);
    QVERIFY(g.has_value());
    QCOMPARE(g->kind, QStringLiteral("buque"));
    QCOMPARE(g->attributes.value(QStringLiteral("mmsi")).toString(),
             QStringLiteral("224123000"));
    QCOMPARE(g->attributes.value(QStringLiteral("eslora_m")).toInt(), 180);

    // La via rapida de tiempo real (update de posicion) NO pierde los atributos.
    QVERIFY(m.update(id, QGeoCoordinate(23.01, -82.0), 45.0));
    g = m.target(id);
    QCOMPARE(g->attributes.value(QStringLiteral("mmsi")).toString(),
             QStringLiteral("224123000"));

    // set/get de un atributo en caliente.
    QVERIFY(m.setAttribute(id, QStringLiteral("rumbo_ais"), 270));
    QCOMPARE(m.attribute(id, QStringLiteral("rumbo_ais")).toInt(), 270);

    // Atributo o id inexistentes -> QVariant invalido; set sobre id ausente -> false.
    QVERIFY(!m.attribute(id, QStringLiteral("no_existe")).isValid());
    QVERIFY(!m.attribute(9999, QStringLiteral("mmsi")).isValid());
    QVERIFY(!m.setAttribute(9999, QStringLiteral("x"), 1));
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

// Traza dibujada de 10 puntos, 500 actualizaciones: el historial las tiene todas
// y su primer punto es donde se dio de alta el objetivo.
void TstTargetModel::historyKeepsOriginBeyondTrail()
{
    TargetModel m;
    m.setTrailMaxPoints(10);
    const qint64 id = m.upsert(objetivo(20.0, -80.0));
    for (int i = 1; i <= 500; ++i)
        QVERIFY(m.update(id, QGeoCoordinate(20.0 + i * 0.001, -80.0)));

    QCOMPARE(m.trail(id).size(), 10);                 // lo que se dibuja: corto
    const QVector<TargetTrackPoint> h = m.history(id);
    QCOMPARE(h.size(), 501);                          // el historial: todo
    QCOMPARE(h.first().position, QGeoCoordinate(20.0, -80.0));
    QCOMPARE(m.origin(id).position, QGeoCoordinate(20.0, -80.0));
    QVERIFY(qAbs(h.last().position.latitude() - 20.5) < 1e-9);
}

// Tope de 100 puntos con 1000 actualizaciones: nunca pasa del tope, conserva el
// origen y la ultima posicion, y sigue cubriendo todo el recorrido (los
// intermedios van ordenados de sur a norte).
void TstTargetModel::historyThinningKeepsOriginAndLast()
{
    TargetModel m;
    m.setHistoryMaxPoints(100);
    const qint64 id = m.upsert(objetivo(10.0, -70.0));
    for (int i = 1; i <= 1000; ++i)
        m.update(id, QGeoCoordinate(10.0 + i * 0.001, -70.0));

    const QVector<TargetTrackPoint> h = m.history(id);
    QVERIFY(h.size() <= 100);
    QVERIFY(h.size() >= 40);
    QCOMPARE(h.first().position, QGeoCoordinate(10.0, -70.0));
    QVERIFY(qAbs(h.last().position.latitude() - 11.0) < 1e-9);
    for (int i = 1; i < int(h.size()); ++i)
        QVERIFY(h[i].position.latitude() > h[i - 1].position.latitude());

    // Bajar el tope aclara en el acto, sin perder los extremos.
    m.setHistoryMaxPoints(2);
    QCOMPARE(m.history(id).size(), 2);
    QCOMPARE(m.history(id).first().position, QGeoCoordinate(10.0, -70.0));
}

// Un objetivo parado (o casi) no llena el historial; la hora de la app se
// respeta (la del ORIGEN incluida); al borrar el objetivo su historial se va.
void TstTargetModel::historyTimesMinDistanceAndRemoval()
{
    TargetModel m;
    m.setHistoryMinDistance(10.0);
    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1700000000000LL).toUTC();
    const qint64 id = m.upsert(objetivo(23.0, -82.0), t0);
    for (int i = 0; i < 50; ++i)                      // ~1 m: por debajo de 10 m
        m.update(id, QGeoCoordinate(23.0 + 0.00001 * (i % 2), -82.0),
                 std::numeric_limits<double>::quiet_NaN(), t0.addSecs(i));
    QCOMPARE(m.history(id).size(), 1);

    m.update(id, QGeoCoordinate(23.01, -82.0), 90.0, t0.addSecs(60));   // ~1.1 km
    const QVector<TargetTrackPoint> h = m.history(id);
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.first().time, t0);
    QCOMPARE(m.origin(id).time, t0);
    QCOMPARE(h.last().time, t0.addSecs(60));

    // Sin hora de la app: la actual.
    const qint64 otro = m.upsert(objetivo(1.0, 1.0));
    QVERIFY(qAbs(m.origin(otro).time.secsTo(QDateTime::currentDateTimeUtc())) < 5);

    QVERIFY(m.remove(id));
    QVERIFY(m.history(id).isEmpty());
    QVERIFY(!m.origin(id).isValid());
}

// Velocidad por la traza: 10 m/s al este durante 60 s, con la ventana de 30 s
// usa el tramo reciente; al pararse, la estima baja a 0; recien creado no hay.
void TstTargetModel::estimatesVelocityFromTrack()
{
    TargetModel m;
    MapTarget t;
    t.position = QGeoCoordinate(20.0, -80.0);
    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760000000000LL).toUTC();
    const qint64 id = m.upsert(t, t0);
    double v = -1.0, rumbo = -1.0;
    QVERIFY(!m.estimateVelocity(id, 30000, &v, &rumbo));     // un solo punto
    QCOMPARE(m.lastUpdateMsecs(id), t0.toMSecsSinceEpoch());

    for (int s = 1; s <= 60; ++s)
        m.update(id, t.position.atDistanceAndAzimuth(10.0 * s, 90.0),
                 std::numeric_limits<double>::quiet_NaN(), t0.addSecs(s));
    QVERIFY(m.estimateVelocity(id, 30000, &v, &rumbo));
    QVERIFY(qAbs(v - 10.0) < 0.05);
    QVERIFY(qAbs(rumbo - 90.0) < 0.5);

    // Gira al norte: con 30 s de ventana, a los 30 s la estima ya es el norte.
    const QGeoCoordinate giro = m.target(id)->position;
    for (int s = 1; s <= 30; ++s)
        m.update(id, giro.atDistanceAndAzimuth(10.0 * s, 0.0),
                 std::numeric_limits<double>::quiet_NaN(), t0.addSecs(60 + s));
    QVERIFY(m.estimateVelocity(id, 30000, &v, &rumbo));
    QVERIFY(qAbs(v - 10.0) < 0.2);
    QVERIFY(qAbs(rumbo) < 1.0 || qAbs(rumbo - 360.0) < 1.0);

    // Se para: la misma posicion 40 s mas tarde (el historial no crece).
    const QGeoCoordinate quieto = m.target(id)->position;
    m.update(id, quieto, std::numeric_limits<double>::quiet_NaN(), t0.addSecs(130));
    QVERIFY(m.estimateVelocity(id, 30000, &v, &rumbo));
    QCOMPARE(v, 0.0);

    QVERIFY(!m.estimateVelocity(999, 30000, &v, &rumbo));
}

void TstTargetModel::labelOffsetDefaultOwnAndRestore()
{
    TargetModel m;
    const qint64 id = m.upsert(objetivo(23.0, -82.0, QStringLiteral("A")));
    QSignalSpy cambios(&m, &TargetModel::changed);

    // Al principio no hay offset por defecto y el objetivo no tiene el suyo.
    QCOMPARE(m.defaultLabelOffsetPx(), QPointF(0.0, 0.0));
    QVERIFY(!m.target(id)->labelOffsetSet);
    QCOMPARE(m.effectiveLabelOffset(*m.target(id)), QPointF(0.0, 0.0));

    // Un offset por defecto lo ven los que no fijaron el suyo; avisa al repintar,
    // pero repetir el mismo valor no repinta.
    m.setDefaultLabelOffsetPx(QPointF(-31.0, -31.0));
    QCOMPARE(cambios.size(), 1);
    m.setDefaultLabelOffsetPx(QPointF(-31.0, -31.0));
    QCOMPARE(cambios.size(), 1);
    QCOMPARE(m.effectiveLabelOffset(*m.target(id)), QPointF(-31.0, -31.0));

    // Fijar el propio lo marca y manda sobre el de por defecto, incluso un (0,0)
    // fijado a proposito (pegar el rotulo al simbolo).
    QVERIFY(m.setLabelOffset(id, QPointF(10.0, 20.0)));
    QVERIFY(m.target(id)->labelOffsetSet);
    QCOMPARE(m.effectiveLabelOffset(*m.target(id)), QPointF(10.0, 20.0));
    QVERIFY(m.setLabelOffset(id, QPointF(0.0, 0.0)));
    QCOMPARE(m.effectiveLabelOffset(*m.target(id)), QPointF(0.0, 0.0));

    // Quitarlo vuelve al de por defecto.
    QVERIFY(m.clearLabelOffset(id));
    QVERIFY(!m.target(id)->labelOffsetSet);
    QCOMPARE(m.effectiveLabelOffset(*m.target(id)), QPointF(-31.0, -31.0));

    // Un offset suelto sin marca (como lo dejan las apps antiguas) cuenta como propio.
    MapTarget viejo = objetivo(23.1, -82.1, QStringLiteral("B"));
    viejo.labelOffsetPx = QPointF(5.0, 6.0);
    const qint64 idViejo = m.upsert(viejo);
    QVERIFY(!m.target(idViejo)->labelOffsetSet);
    QCOMPARE(m.effectiveLabelOffset(*m.target(idViejo)), QPointF(5.0, 6.0));

    // Restaurar deja valor y marca exactos, y los ids desconocidos fallan.
    QVERIFY(m.setLabelOffset(idViejo, QPointF(50.0, 60.0)));
    QVERIFY(m.restoreLabelOffset(idViejo, QPointF(5.0, 6.0), false));
    QCOMPARE(m.target(idViejo)->labelOffsetPx, QPointF(5.0, 6.0));
    QVERIFY(!m.target(idViejo)->labelOffsetSet);
    QVERIFY(!m.setLabelOffset(999, QPointF()));
    QVERIFY(!m.clearLabelOffset(999));
    QVERIFY(!m.restoreLabelOffset(999, QPointF(), false));
}

void TstTargetModel::labelContentCanChangeAfterCreation()
{
    TargetModel m;
    const qint64 id = m.upsert(objetivo(23.0, -82.0, QStringLiteral("A")));
    m.upsert([&] { MapTarget t = *m.target(id); t.position = QGeoCoordinate(23.1, -82.0); return t; }());
    const int puntos = int(m.trail(id).size());
    QSignalSpy cambios(&m, &TargetModel::changed);

    TargetLabelLine l;
    l.segments.append({ QStringLiteral("E046"), QColor(0, 70, 200) });
    l.segments.append({ QStringLiteral("EMER"), QColor(200, 0, 0) });
    QVERIFY(m.setLabelContent(id, { l }));
    QCOMPARE(cambios.size(), 1);
    QCOMPARE(m.target(id)->labelContent.size(), 1);
    QCOMPARE(m.target(id)->labelContent.first().segments.at(1).color, QColor(200, 0, 0));
    QCOMPARE(int(m.trail(id).size()), puntos);       // la traza sigue ahi

    QVERIFY(m.setLabelContent(id, {}));                      // vuelve al texto plano
    QVERIFY(m.target(id)->labelContent.isEmpty());
    QVERIFY(!m.setLabelContent(9999, { l }));
}

QTEST_MAIN(TstTargetModel)
#include "tst_targetmodel.moc"
