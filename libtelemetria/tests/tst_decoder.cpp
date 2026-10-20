#include "telemetria/Decoder.h"

#include <QtTest>

#include <cmath>

using namespace telemetria;

//! Tests del decodificador de flujo: lineas partidas, datagramas, contadores y mezcla de frases.
class TstDecoder : public QObject
{
    Q_OBJECT

private slots:
    void mixedStream();
    void streamKeepsPartialLine();
    void datagramWithoutNewline();
    void countsBadAndIgnored();
    void ownShipAndUnsupportedAis();
    void lostFragmentsAreCounted();
    void garbageDoesNotGrowWithoutLimit();
    void resetClearsEverything();
};

static const QByteArray kRmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
static const QByteArray kAis = "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C";

// Cuantos eventos de cada tipo hay en \a v.
static int cuantos(const QVector<Event> &v, Event::Kind k)
{
    int n = 0;
    for (const Event &e : v)
        n += e.kind == k ? 1 : 0;
    return n;
}

// Una captura con RMC, GGA, VTG, HDT, AIS de uno y de dos fragmentos, y lineas de otros tipos y vacias.
void TstDecoder::mixedStream()
{
    AisMessage m;
    m.type = 5;
    m.mmsi = 316000001;
    m.name = "PRUEBA";
    QByteArray captura = kRmc + "\r\n"
        + "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
        + "\r\n"
        + "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n"
        + formatHdt(Hdt{ 274.07 }) + "\r\n"
        + kAis + "\r\n"
        + "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\r\n";
    for (const QByteArray &f : encodeAis(m))
        captura += f + "\r\n";
    Decoder d;
    const QVector<Event> ev = d.feed(captura);
    QCOMPARE(cuantos(ev, Event::Kind::Rmc), 1);
    QCOMPARE(cuantos(ev, Event::Kind::Gga), 1);
    QCOMPARE(cuantos(ev, Event::Kind::Vtg), 1);
    QCOMPARE(cuantos(ev, Event::Kind::Hdt), 1);
    QCOMPARE(cuantos(ev, Event::Kind::Ais), 2);          // el tipo 1 y el tipo 5 (dos frases, un solo mensaje)
    QCOMPARE(cuantos(ev, Event::Kind::Ignored), 1);      // la GSV
    QCOMPARE(cuantos(ev, Event::Kind::Bad), 0);
    QCOMPARE(d.stats().lines, quint64(8));               // las dos frases del tipo 5 son 2 lineas; la vacia no cuenta
    QCOMPARE(d.stats().decoded, quint64(6));             // RMC, GGA, VTG, HDT y los dos mensajes AIS
    QCOMPARE(d.stats().aisMessages, quint64(2));
}

// Un flujo (TCP) puede cortar una frase por la mitad: la linea espera al resto.
void TstDecoder::streamKeepsPartialLine()
{
    Decoder d;
    QVERIFY(d.feed(kRmc.left(30)).isEmpty());
    QVERIFY(d.feed(kRmc.mid(30, 20)).isEmpty());
    const QVector<Event> ev = d.feed(kRmc.mid(50) + "\r\n");
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev.first().kind, Event::Kind::Rmc);
    QCOMPARE(ev.first().rmc.sogKn, 22.4);
    QCOMPARE(d.stats().decoded, quint64(1));
}

// Un datagrama UDP trae lineas completas aunque la ultima no acabe en salto de linea.
void TstDecoder::datagramWithoutNewline()
{
    Decoder d;
    const QVector<Event> ev = d.feedDatagram(kRmc + "\r\n" + kAis);
    QCOMPARE(ev.size(), 2);
    QCOMPARE(ev.at(0).kind, Event::Kind::Rmc);
    QCOMPARE(ev.at(1).kind, Event::Kind::Ais);
    QCOMPARE(ev.at(1).ais.mmsi, quint32(477553000));
    QVERIFY(!ev.at(1).ownShip);
    // Y no deja nada a medias para el siguiente datagrama.
    const QVector<Event> sig = d.feedDatagram(kRmc);
    QCOMPARE(sig.size(), 1);
}

// Suma mala y frase mal formada se cuentan aparte; un tipo desconocido se ignora, sin ser un error.
void TstDecoder::countsBadAndIgnored()
{
    Decoder d;
    QByteArray mala = kRmc;
    mala.replace("*6A", "*6B");
    QVector<Event> ev = d.feed(mala + "\r\n");
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev.first().kind, Event::Kind::Bad);
    QVERIFY(!ev.first().detail.isEmpty());
    QCOMPARE(ev.first().raw, mala);
    ev = d.feed("esto no es NMEA\r\n$GPRMC\r\n");
    QCOMPARE(cuantos(ev, Event::Kind::Bad), 2);
    ev = d.feed("$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\r\n");
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev.first().kind, Event::Kind::Ignored);
    QCOMPARE(ev.first().detail, QStringLiteral("GPGSV"));
    QCOMPARE(d.stats().badChecksum, quint64(1));
    QCOMPARE(d.stats().malformed, quint64(2));
    QCOMPARE(d.stats().ignored, quint64(1));
    QCOMPARE(d.stats().decoded, quint64(0));
}

// !AIVDO es el propio barco; un tipo AIS que no se decodifica (4) se ignora pero se cuenta.
void TstDecoder::ownShipAndUnsupportedAis()
{
    AisMessage m;
    m.type = 1;
    m.mmsi = 316000999;
    m.lat = 10.0;
    m.lon = 20.0;
    Decoder d;
    QVector<Event> ev = d.feed(encodeAis(m, 'A', 1, true).first() + "\r\n");
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev.first().kind, Event::Kind::Ais);
    QVERIFY(ev.first().ownShip);
    QCOMPARE(ev.first().ais.mmsi, quint32(316000999));

    const QByteArray tipo4 = formatSentence("AIVDM", { "1", "1", "", "A", "4" + QByteArray("0") + QByteArray(30, '0'), "0" }, true);
    ev = d.feed(tipo4 + "\r\n");
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev.first().kind, Event::Kind::Ignored);
    QCOMPARE(ev.first().detail, QStringLiteral("AIS tipo 4"));
    QCOMPARE(d.stats().aisMessages, quint64(2));
    QCOMPARE(d.stats().aisUnsupported, quint64(1));
}

// Si a un mensaje de dos frases le falta la primera, se pierde, se cuenta y el flujo sigue.
void TstDecoder::lostFragmentsAreCounted()
{
    AisMessage m;
    m.type = 5;
    m.mmsi = 1;
    const QList<QByteArray> f = encodeAis(m, 'A', 2);
    Decoder d;
    QVERIFY(d.feed(f.at(1) + "\r\n").isEmpty());
    QCOMPARE(d.stats().aisLost, quint64(1));
    const QVector<Event> ev = d.feed(kRmc + "\r\n");
    QCOMPARE(ev.size(), 1);                           // lo siguiente se decodifica con normalidad
}

// Una linea de megabytes sin saltos no se acumula: el decodificador la corta y sigue.
void TstDecoder::garbageDoesNotGrowWithoutLimit()
{
    Decoder d;
    QVector<Event> ev;
    for (int i = 0; i < 200; ++i)
        ev += d.feed(QByteArray(1000, 'x'));
    QVERIFY(cuantos(ev, Event::Kind::Bad) >= 1);      // el exceso se entrego como linea mal formada
    ev = d.feed("\r\n" + kRmc + "\r\n");
    QCOMPARE(cuantos(ev, Event::Kind::Rmc), 1);       // y despues todo vuelve a funcionar
}

// reset() deja los contadores y los fragmentos a cero.
void TstDecoder::resetClearsEverything()
{
    Decoder d;
    d.feed(kRmc + "\r\n$GPRMC,mal*00\r\n");
    QVERIFY(d.stats().lines > 0);
    d.reset();
    QCOMPARE(d.stats().lines, quint64(0));
    QCOMPARE(d.stats().decoded, quint64(0));
    QCOMPARE(d.stats().badChecksum + d.stats().malformed, quint64(0));
}

QTEST_APPLESS_MAIN(TstDecoder)
#include "tst_decoder.moc"
