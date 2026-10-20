#include "telemetria/Ais.h"

#include <QtTest>

#include <cmath>

using namespace telemetria;

//! Tests de AIS: un mensaje publicado de referencia, ida y vuelta de todos los tipos y reensamblado.
class TstAis : public QObject
{
    Q_OBJECT

private slots:
    void publishedType1Sample();
    void positionNotAvailable();
    void roundTripClassA();
    void roundTripType5SplitInTwoSentences();
    void roundTripClassB();
    void roundTripType24BothParts();
    void negativeCoordinatesAndLimits();
    void assemblerOrderAndLoss();
    void assemblerBoundsMemory();
    void badPayloads();
    void unsupportedTypeKeepsMmsi();
    void textLabels();
};

// Pasa una frase !AIVDM suelta por parseSentence y el ensamblador, y la decodifica.
static bool decodificar(const QByteArray &linea, AisMessage *m)
{
    const Sentence s = parseSentence(linea);
    AisAssembler a;
    QByteArray carga;
    int relleno = 0;
    return s.valid && a.add(s, &carga, &relleno) && decodeAis(carga, relleno, m);
}

// El ejemplo de la documentacion de gpsd («AIVDM/AIVDO protocol decoding»): tipo 1, MMSI 477553000, amarrado,
// en 47.582833° N, 122.345833° O, rumbo 51.0°, proa 181°, segundo 15. La suma 5C la comprueba parseSentence.
void TstAis::publishedType1Sample()
{
    const QByteArray linea = "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C";
    QVERIFY(parseSentence(linea).valid);
    AisMessage m;
    QVERIFY(decodificar(linea, &m));
    QVERIFY(m.supported);
    QCOMPARE(m.type, 1);
    QCOMPARE(m.mmsi, quint32(477553000));
    QCOMPARE(m.navStatus, 5);                        // amarrado
    QVERIFY(qAbs(m.lat - 47.582833) < 1e-5);
    QVERIFY(qAbs(m.lon - (-122.345833)) < 1e-5);
    QCOMPARE(m.sogKn, 0.0);
    QCOMPARE(m.cogDeg, 51.0);
    QCOMPARE(m.headingDeg, 181);
    QCOMPARE(m.second, 15);
    QVERIFY(!m.classB);
}

// 181° / 91° / 1023 / 3600 / 511 son «no disponible»: no se toman por datos.
void TstAis::positionNotAvailable()
{
    AisMessage m;
    m.type = 1;
    m.mmsi = 123456789;
    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QVERIFY(!d.hasPosition());
    QVERIFY(std::isnan(d.sogKn) && std::isnan(d.cogDeg));
    QCOMPARE(d.headingDeg, -1);
    QCOMPARE(d.second, 60);
}

// Un informe de clase A: lo codificado se decodifica igual.
void TstAis::roundTripClassA()
{
    AisMessage m;
    m.type = 3;
    m.repeat = 1;
    m.mmsi = 316001234;
    m.navStatus = 0;
    m.lat = 23.123456;
    m.lon = -82.345678;
    m.sogKn = 12.3;
    m.cogDeg = 271.4;
    m.headingDeg = 270;
    m.second = 42;
    m.accuracy = true;
    const QList<QByteArray> frases = encodeAis(m, 'B');
    QCOMPARE(frases.size(), 1);
    QVERIFY(frases.first().startsWith("!AIVDM,1,1,,B,"));
    AisMessage d;
    QVERIFY(decodificar(frases.first(), &d));
    QCOMPARE(d.type, 3);
    QCOMPARE(d.repeat, 1);
    QCOMPARE(d.mmsi, quint32(316001234));
    QCOMPARE(d.navStatus, 0);
    QVERIFY(qAbs(d.lat - m.lat) < 1.0 / 600000.0);
    QVERIFY(qAbs(d.lon - m.lon) < 1.0 / 600000.0);
    QCOMPARE(d.sogKn, 12.3);
    QCOMPARE(d.cogDeg, 271.4);
    QCOMPARE(d.headingDeg, 270);
    QCOMPARE(d.second, 42);
    QVERIFY(d.accuracy);

    // Como AIVDO (el propio barco) lleva otra direccion.
    QVERIFY(encodeAis(m, 'A', 1, true).first().startsWith("!AIVDO"));
}

// El tipo 5 no cabe en una frase: sale en dos, con el mismo identificador de secuencia, y se reensambla.
void TstAis::roundTripType5SplitInTwoSentences()
{
    AisMessage m;
    m.type = 5;
    m.mmsi = 255805000;
    m.imo = 9876543;
    m.callSign = "CQ1234";
    m.name = "NAVIO DE PRUEBA";
    m.shipType = 70;
    m.toBowM = 150;
    m.toSternM = 30;
    m.toPortM = 12;
    m.toStarboardM = 13;
    m.draughtM = 8.4;
    m.destination = "LA HABANA";
    m.etaMonth = 11;
    m.etaDay = 3;
    m.etaHour = 14;
    m.etaMinute = 30;
    const QList<QByteArray> frases = encodeAis(m, 'A', 7);
    QCOMPARE(frases.size(), 2);
    QVERIFY(frases.at(0).startsWith("!AIVDM,2,1,7,A,"));
    QVERIFY(frases.at(1).startsWith("!AIVDM,2,2,7,A,"));
    for (const QByteArray &f : frases)
        QVERIFY2(f.size() <= 82, f.constData());     // el limite de una linea NMEA

    AisAssembler a;
    QByteArray carga;
    int relleno = 0;
    QVERIFY(!a.add(parseSentence(frases.at(0)), &carga, &relleno));
    QCOMPARE(a.pending(), 1);
    QVERIFY(a.add(parseSentence(frases.at(1)), &carga, &relleno));
    QCOMPARE(a.pending(), 0);
    AisMessage d;
    QVERIFY(decodeAis(carga, relleno, &d));
    QCOMPARE(d.type, 5);
    QCOMPARE(d.mmsi, quint32(255805000));
    QCOMPARE(d.imo, quint32(9876543));
    QCOMPARE(d.callSign, QStringLiteral("CQ1234"));
    QCOMPARE(d.name, QStringLiteral("NAVIO DE PRUEBA"));
    QCOMPARE(d.shipType, 70);
    QCOMPARE(d.lengthM(), 180);
    QCOMPARE(d.beamM(), 25);
    QCOMPARE(d.draughtM, 8.4);
    QCOMPARE(d.destination, QStringLiteral("LA HABANA"));
    QCOMPARE(d.etaMonth, 11);
    QCOMPARE(d.etaDay, 3);
    QCOMPARE(d.etaHour, 14);
    QCOMPARE(d.etaMinute, 30);
}

// Clase B: posicion (18) y posicion con nombre y tamano (19).
void TstAis::roundTripClassB()
{
    AisMessage m;
    m.type = 18;
    m.mmsi = 338123456;
    m.lat = 25.7617;
    m.lon = -80.1918;
    m.sogKn = 5.5;
    m.cogDeg = 90.0;
    m.headingDeg = 89;
    m.second = 7;
    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QVERIFY(d.classB);
    QCOMPARE(d.type, 18);
    QVERIFY(qAbs(d.lat - 25.7617) < 1.0 / 600000.0 && qAbs(d.lon - (-80.1918)) < 1.0 / 600000.0);
    QCOMPARE(d.sogKn, 5.5);
    QCOMPARE(d.headingDeg, 89);
    QCOMPARE(d.second, 7);

    m.type = 19;
    m.name = "VELERO";
    m.shipType = 36;
    m.toBowM = 8;
    m.toSternM = 4;
    m.toPortM = 2;
    m.toStarboardM = 2;
    const QList<QByteArray> frases = encodeAis(m);
    AisAssembler a;
    QByteArray carga;
    int relleno = 0;
    bool completo = false;
    for (const QByteArray &f : frases)
        completo = a.add(parseSentence(f), &carga, &relleno);
    QVERIFY(completo);
    QVERIFY(decodeAis(carga, relleno, &d));
    QCOMPARE(d.type, 19);
    QCOMPARE(d.name, QStringLiteral("VELERO"));
    QCOMPARE(d.shipType, 36);
    QCOMPARE(d.lengthM(), 12);
    QCOMPARE(d.beamM(), 4);
    QVERIFY(qAbs(d.lat - 25.7617) < 1.0 / 600000.0);
}

// El tipo 24 son dos mensajes: la parte A trae el nombre y la B el tipo, el indicativo y el tamano.
void TstAis::roundTripType24BothParts()
{
    AisMessage a;
    a.type = 24;
    a.partNo = 0;
    a.mmsi = 338999999;
    a.name = "PESCADOR 1";
    AisMessage b = a;
    b.partNo = 1;
    b.shipType = 30;
    b.callSign = "W1ABC";
    b.toBowM = 9;
    b.toSternM = 3;
    b.toPortM = 2;
    b.toStarboardM = 2;
    AisMessage da, db;
    QVERIFY(decodificar(encodeAis(a).first(), &da));
    QVERIFY(decodificar(encodeAis(b).first(), &db));
    QCOMPARE(da.partNo, 0);
    QCOMPARE(da.name, QStringLiteral("PESCADOR 1"));
    QCOMPARE(db.partNo, 1);
    QCOMPARE(db.shipType, 30);
    QCOMPARE(db.callSign, QStringLiteral("W1ABC"));
    QCOMPARE(db.lengthM(), 12);
    QCOMPARE(da.mmsi, db.mmsi);
    a.partNo = 2;
    QVERIFY(encodeAis(a).isEmpty());                  // una parte que no existe no se codifica
}

// Longitudes y latitudes negativas y extremas, para el complemento a dos de 28 y 27 bits.
void TstAis::negativeCoordinatesAndLimits()
{
    const double puntos[][2] = { { -89.999, -179.999 }, { 89.999, 179.999 }, { 0.0, 0.0 }, { -0.0001, -0.0001 },
                                 { -33.8688, 151.2093 }, { 64.1466, -21.9426 } };
    for (const auto &p : puntos) {
        AisMessage m;
        m.type = 1;
        m.mmsi = 1;
        m.lat = p[0];
        m.lon = p[1];
        AisMessage d;
        QVERIFY(decodificar(encodeAis(m).first(), &d));
        QVERIFY2(qAbs(d.lat - p[0]) < 1.0 / 600000.0 && qAbs(d.lon - p[1]) < 1.0 / 600000.0,
                 qPrintable(QStringLiteral("%1,%2 -> %3,%4").arg(p[0]).arg(p[1]).arg(d.lat).arg(d.lon)));
    }
}

// Los fragmentos fuera de orden o a los que falta el anterior descartan el mensaje y se cuentan.
void TstAis::assemblerOrderAndLoss()
{
    AisMessage m;
    m.type = 5;
    m.mmsi = 1;
    m.name = "X";
    const QList<QByteArray> frases = encodeAis(m, 'A', 3);
    QCOMPARE(frases.size(), 2);

    AisAssembler a;
    QByteArray carga;
    int relleno = 0;
    QVERIFY(!a.add(parseSentence(frases.at(1)), &carga, &relleno));   // llega la 2 sin la 1
    QCOMPARE(a.lostFragments(), quint64(1));
    QCOMPARE(a.pending(), 0);

    QVERIFY(!a.add(parseSentence(frases.at(0)), &carga, &relleno));
    QVERIFY(!a.add(parseSentence(frases.at(0)), &carga, &relleno));   // la 1 otra vez: la anterior quedo a medias
    QCOMPARE(a.lostFragments(), quint64(2));
    QVERIFY(a.add(parseSentence(frases.at(1)), &carga, &relleno));    // y con la 2 se completa

    // Dos mensajes distintos entrelazados (otro canal) no se mezclan.
    const QList<QByteArray> b = encodeAis(m, 'B', 3);
    AisAssembler c;
    QVERIFY(!c.add(parseSentence(frases.at(0)), &carga, &relleno));
    QVERIFY(!c.add(parseSentence(b.at(0)), &carga, &relleno));
    QCOMPARE(c.pending(), 2);
    QVERIFY(c.add(parseSentence(b.at(1)), &carga, &relleno));
    QVERIFY(c.add(parseSentence(frases.at(1)), &carga, &relleno));
    QCOMPARE(c.pending(), 0);

    // Una frase que no es VDM/VDO o con campos que no son numeros, ni se mira.
    QVERIFY(!c.add(parseSentence("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48"), &carga, &relleno));
    QVERIFY(!c.add(parseSentence(formatSentence("AIVDM", { "x", "1", "", "A", "0", "0" }, true)), &carga, &relleno));
}

// Una fuente que empieza mensajes y no los termina no hace crecer la memoria sin limite.
void TstAis::assemblerBoundsMemory()
{
    AisAssembler a;
    QByteArray carga;
    int relleno = 0;
    for (int i = 0; i < 1000; ++i)
        a.add(parseSentence(formatSentence("AIVDM", { "2", "1", QByteArray::number(i), "A", "0000", "0" }, true)), &carga, &relleno);
    QVERIFY2(a.pending() <= 32, qPrintable(QString::number(a.pending())));
}

// Caracteres fuera del alfabeto de 6 bits, relleno imposible y cargas demasiado cortas.
void TstAis::badPayloads()
{
    AisMessage m;
    QVERIFY(!decodeAis("177KQJ5000G?tO`K>RA1wUbN0TKH ", 0, &m));       // espacio: no es del alfabeto
    QVERIFY(!decodeAis("177KQJ5000G?tO`K>RA1wUbN0TKH", 9, &m));        // relleno imposible
    QVERIFY(!decodeAis("177KQJ5000", 0, &m));                          // tipo 1 pero a medias
    QVERIFY(!decodeAis("", 0, &m));
    QVERIFY(!decodeAis("1", 0, &m));
}

// Un tipo que no se decodifica (p. ej. 4, base fija) no falla: devuelve el tipo y el MMSI.
void TstAis::unsupportedTypeKeepsMmsi()
{
    // Un tipo 4 sintetico: 6 bits de tipo (4) + 2 de repeticion + 30 de MMSI + 94 de relleno.
    QByteArray carga = "4";                     // tipo 4 = 000100 -> '4'
    carga += "0";                               // repeticion 0 y los 4 primeros bits del MMSI
    carga += QByteArray(26, '0');
    AisMessage m;
    QVERIFY(decodeAis(carga, 0, &m));
    QCOMPARE(m.type, 4);
    QVERIFY(!m.supported);
    QVERIFY(!m.hasPosition());
}

// Las etiquetas en espanol de los codigos mas comunes.
void TstAis::textLabels()
{
    QCOMPARE(aisNavStatusText(5), QStringLiteral("Amarrado"));
    QCOMPARE(aisNavStatusText(0), QStringLiteral("Navegando a motor"));
    QCOMPARE(aisShipTypeText(70), QStringLiteral("Carguero"));
    QCOMPARE(aisShipTypeText(79), QStringLiteral("Carguero"));
    QCOMPARE(aisShipTypeText(80), QStringLiteral("Petrolero"));
    QCOMPARE(aisShipTypeText(30), QStringLiteral("Pesquero"));
    QCOMPARE(aisShipTypeText(0), QStringLiteral("No disponible"));
}

QTEST_APPLESS_MAIN(TstAis)
#include "tst_ais.moc"
