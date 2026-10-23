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
    void type21AtonVector();
    void type4And11UtcVector();
    void type9SarVector();
    void safetyTextVectors();
    void binaryVectors();
    void ackRequestInterrogationVectors();
    void dgnssAndLinkVectors();
    void channelAndGroupVectors();
    void singleSlotAndLongRangeVectors();
    void roundTripManagementTypes();
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
    m.epfsType = 1;
    m.assigned = true;
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
    QCOMPARE(d.epfsType, 1);
    QVERIFY(d.assigned);
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

// Un tipo que no se decodifica (p. ej. 28, reservado) no falla: devuelve el tipo y el MMSI.
void TstAis::unsupportedTypeKeepsMmsi()
{
    // Un tipo 28 sintetico: 6 bits de tipo (28) + 2 de repeticion + 30 de MMSI + relleno.
    QByteArray carga = "L";                     // tipo 28 = 011100 -> 'L'
    carga += "0";                               // repeticion 0 y los 4 primeros bits del MMSI
    carga += QByteArray(26, '0');
    AisMessage m;
    QVERIFY(decodeAis(carga, 0, &m));
    QCOMPARE(m.type, 28);
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
    QCOMPARE(aisAtonTypeText(20), QStringLiteral("Flotante cardinal N"));
    QCOMPARE(aisAtonTypeText(6), QStringLiteral("Luz con sector"));
    QCOMPARE(aisAtonTypeText(0), QStringLiteral("No especificado"));
    QCOMPARE(aisEpfsTypeText(1), QStringLiteral("GPS"));
    QCOMPARE(aisEpfsTypeText(3), QStringLiteral("GPS y GLONASS"));
    QCOMPARE(aisEpfsTypeText(0), QStringLiteral("No definido"));
}

// Envuelve una carga suelta (con su relleno) en una frase !AIVDM de una sola
// parte con la suma correcta, y la decodifica entera (frase, ensamblador y AIS).
static bool decodificarCarga(const char *carga, int relleno, AisMessage *m)
{
    const QByteArray linea = formatSentence("AIVDM",
        { "1", "1", "", "B", QByteArray(carga), QByteArray::number(relleno) }, true);
    return decodificar(linea, m);
}

// Los vectores de los tipos nuevos se generaron con un codificador escrito
// aparte (desde la tabla de ITU-R M.1371, sin compartir codigo con este
// decodificador ni con el de Sas-3) y se contrastaron con los dos: ver
// libtelemetria/PORT_AIS.md.

// Tipo 21: boya con nombre extendido, fuera de posicion, GPS y tamano 10 x 5 m.
void TstAis::type21AtonVector()
{
    AisMessage m;
    QVERIFY(decodificarCarga("E>mNcDb17dPh1PW0V@Hq0000000M3AHL6WIg050`@Hg@21F50", 4, &m));
    QVERIFY(m.supported);
    QCOMPARE(m.type, 21);
    QCOMPARE(m.mmsi, quint32(995601234));
    QCOMPARE(m.atonType, 20);
    QCOMPARE(m.name, QStringLiteral("BOYA CANAL 12EXT"));   // 20 caracteres + extension "EXT"
    QVERIFY(m.accuracy);
    QVERIFY(qAbs(m.lat - 23.1234) < 1.0 / 600000.0);
    QVERIFY(qAbs(m.lon - (-82.4567)) < 1.0 / 600000.0);
    QCOMPARE(m.lengthM(), 10);
    QCOMPARE(m.beamM(), 5);
    QCOMPARE(m.epfsType, 1);
    QCOMPARE(m.second, 30);
    QVERIFY(m.offPosition);
    QVERIFY(!m.virtualAton);
    QVERIFY(!m.assigned);

    // Ida y vuelta con encodeAis: el nombre largo se reparte solo (20 + extension).
    AisMessage e;
    e.type = 21;
    e.mmsi = m.mmsi;
    e.atonType = m.atonType;
    e.name = m.name;
    e.accuracy = true;
    e.lat = m.lat;
    e.lon = m.lon;
    e.toBowM = 5; e.toSternM = 5; e.toPortM = 2; e.toStarboardM = 3;
    e.epfsType = 1;
    e.second = 30;
    e.offPosition = true;
    AisMessage d;
    QVERIFY(decodificar(encodeAis(e).first(), &d));
    QCOMPARE(d.name, e.name);
    QCOMPARE(d.atonType, 20);
    QCOMPARE(d.lengthM(), 10);
    QVERIFY(d.offPosition);
    QVERIFY(qAbs(d.lat - m.lat) < 1.0 / 600000.0);
}

// Tipos 4 y 11: la misma estructura (fecha y hora UTC completas, estacion).
// El minuto 37 es impar a proposito: en binario ocupa 6 bits, no 5.
void TstAis::type4And11UtcVector()
{
    AisMessage m;
    QVERIFY(decodificarCarga("401;FQivbcBUbr6u8H=>Si102000", 0, &m));
    QVERIFY(m.supported);
    QCOMPARE(m.type, 4);
    QCOMPARE(m.mmsi, quint32(1234567));
    QCOMPARE(m.utcYear, 2026);
    QCOMPARE(m.utcMonth, 10);
    QCOMPARE(m.utcDay, 22);
    QCOMPARE(m.utcHour, 18);
    QCOMPARE(m.utcMinute, 37);
    QCOMPARE(m.utcSecond, 42);
    QVERIFY(m.accuracy);
    QVERIFY(qAbs(m.lat - 23.1167) < 1.0 / 600000.0);
    QVERIFY(qAbs(m.lon - (-82.3667)) < 1.0 / 600000.0);
    QCOMPARE(m.epfsType, 1);

    AisMessage r;
    QVERIFY(decodificarCarga(";5R2Fi1vbcBUbr6u8H=>Si102000", 0, &r));
    QCOMPARE(r.type, 11);
    QCOMPARE(r.mmsi, quint32(371234500));
    QCOMPARE(r.utcMinute, 37);
    QCOMPARE(r.utcSecond, 42);
    QVERIFY(qAbs(r.lat - m.lat) < 1e-9);

    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.type, 4);
    QCOMPARE(d.utcYear, 2026);
    QCOMPARE(d.utcMinute, 37);
    QCOMPARE(d.utcSecond, 42);
}

// Tipo 9: aeronave SAR a 1500 m y 120 nudos (enteros, no decimas), sin proa.
void TstAis::type9SarVector()
{
    AisMessage m;
    QVERIFY(decodificarCarga("91b5>1mo1pr:s30=18D:UV@04000", 0, &m));
    QVERIFY(m.supported);
    QCOMPARE(m.type, 9);
    QCOMPARE(m.mmsi, quint32(111234567));
    QCOMPARE(m.altitudeM, 1500.0);
    QCOMPARE(m.sogKn, 120.0);
    QCOMPARE(m.cogDeg, 271.0);
    QCOMPARE(m.headingDeg, -1);                        // el tipo 9 no trae proa
    QCOMPARE(m.second, 25);
    QVERIFY(m.accuracy);
    QVERIFY(qAbs(m.lat - 22.75) < 1.0 / 600000.0);
    QVERIFY(qAbs(m.lon - (-81.5)) < 1.0 / 600000.0);

    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.altitudeM, 1500.0);
    QCOMPARE(d.sogKn, 120.0);
    QCOMPARE(d.second, 25);
}

// Tipos 12 (direccionado, con destino y secuencia) y 14 (difundido).
void TstAis::safetyTextVectors()
{
    AisMessage m;
    QVERIFY(decodificarCarga("<5MwqeaGOvKL=5>C1:5P45P@BE521Pij", 0, &m));
    QVERIFY(m.supported);
    QCOMPARE(m.type, 12);
    QCOMPARE(m.mmsi, quint32(366999990));
    QCOMPARE(m.destMmsi, quint32(366999991));
    QCOMPARE(m.sequence, 2);
    QVERIFY(!m.retransmit);
    QCOMPARE(m.text, QStringLiteral("MENSAJE DE PRUEBA 12"));

    AisMessage b;
    QVERIFY(decodificarCarga(">035dF@5HU<v060p5HDL4q@E?b0<4p4j0<E984@t", 2, &b));
    QCOMPARE(b.type, 14);
    QCOMPARE(b.mmsi, quint32(3239001));
    QCOMPARE(b.text, QStringLiteral("AVISO A NAVEGANTES: CANAL CERRADO"));
    QCOMPARE(b.destMmsi, quint32(0));

    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.text, m.text);
    QCOMPARE(d.destMmsi, m.destMmsi);
    AisMessage e;
    QVERIFY(decodificar(encodeAis(b).first(), &e));
    QCOMPARE(e.text, b.text);
}

// Tipos 6 y 8: la carga binaria pasa tal cual, con su DAC/FID.
void TstAis::binaryVectors()
{
    AisMessage m;
    QVERIFY(decodificarCarga("63`l5N4rJIe2<PcNcKsg", 0, &m));
    QVERIFY(m.supported);
    QCOMPARE(m.type, 6);
    QCOMPARE(m.destMmsi, quint32(244999888));
    QCOMPARE(m.sequence, 1);
    QVERIFY(m.retransmit);
    QCOMPARE(m.dac, 200);
    QCOMPARE(m.fid, 10);
    QCOMPARE(m.binaryBits, 32);
    QCOMPARE(QString::fromLatin1(m.binaryData.toHex()), QStringLiteral("deadbeef"));

    AisMessage b;
    QVERIFY(decodificarCarga("83`l5N00Gh420h@5", 0, &b));
    QCOMPARE(b.type, 8);
    QCOMPARE(b.dac, 1);
    QCOMPARE(b.fid, 31);
    QCOMPARE(b.binaryBits, 40);
    QCOMPARE(QString::fromLatin1(b.binaryData.toHex()), QStringLiteral("0102030405"));

    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.binaryData, m.binaryData);
    QCOMPARE(d.dac, 200);
}

// Tipos 7 y 13 (acuses), 10 (peticion UTC) y 15 (interrogacion a dos estaciones).
void TstAis::ackRequestInterrogationVectors()
{
    AisMessage m;
    QVERIFY(decodificarCarga("701;FQiHPUdA>S@EpP", 4, &m));
    QCOMPARE(m.type, 7);
    QCOMPARE(m.ackMmsi.size(), 2);
    QCOMPARE(m.ackMmsi.at(0), quint32(371234500));
    QCOMPARE(m.ackSequence.at(0), 1);   // la secuencia del acuse son 2 bits (0-3)
    QCOMPARE(m.ackMmsi.at(1), quint32(244123000));
    QCOMPARE(m.ackSequence.at(1), 2);

    AisMessage s;
    QVERIFY(decodificarCarga("=01;FQiinpFS", 0, &s));
    QCOMPARE(s.type, 13);
    QCOMPARE(s.ackMmsi.size(), 1);
    QCOMPARE(s.ackMmsi.at(0), quint32(477553000));
    QCOMPARE(s.ackSequence.at(0), 3);

    AisMessage p;
    QVERIFY(decodificarCarga(":3`l5N00Bm`L", 0, &p));
    QCOMPARE(p.type, 10);
    QCOMPARE(p.destMmsi, quint32(1234567));

    AisMessage q;
    QVERIFY(decodificarCarga("?01;FQiHPUd@DBhH9H3`l5N4kQ0", 2, &q));
    QCOMPARE(q.type, 15);
    QCOMPARE(q.interrogations.size(), 2);
    QCOMPARE(q.interrogations.at(0).mmsi, quint32(371234500));
    QCOMPARE(q.interrogations.at(0).type1, 5);
    QCOMPARE(q.interrogations.at(0).offset1, 300);
    QCOMPARE(q.interrogations.at(0).type2, 24);
    QCOMPARE(q.interrogations.at(0).offset2, 600);
    QCOMPARE(q.interrogations.at(1).mmsi, quint32(244123000));
    QCOMPARE(q.interrogations.at(1).type1, 19);
    QCOMPARE(q.interrogations.at(1).offset1, 900);
}

// Tipos 17 (DGNSS: la posicion es de la estacion, no del emisor) y 20 (reservas).
void TstAis::dgnssAndLinkVectors()
{
    AisMessage m;
    QVERIFY(decodificarCarga("A01;H=k?f0K4P:bsk0", 4, &m));
    QCOMPARE(m.type, 17);
    QVERIFY(!m.hasPosition());                          // la estacion no es «mi posicion»
    QVERIFY(qAbs(m.dgnssLat - 23.1) < 1.0 / 600.0);
    QVERIFY(qAbs(m.dgnssLon - (-82.4)) < 1.0 / 600.0);
    QCOMPARE(m.binaryBits, 24);
    QCOMPARE(QString::fromLatin1(m.binaryData.toHex()), QStringLiteral("aabbcc"));

    AisMessage v;
    QVERIFY(decodificarCarga("D01;FQh7Pb14BhF0T", 2, &v));
    QCOMPARE(v.type, 20);
    QCOMPARE(v.reservations.size(), 2);
    QCOMPARE(v.reservations.at(0).offset, 120);
    QCOMPARE(v.reservations.at(0).slotCount, 2);
    QCOMPARE(v.reservations.at(0).timeout, 5);
    QCOMPARE(v.reservations.at(0).increment, 17);
    QCOMPARE(v.reservations.at(1).offset, 300);
    QCOMPARE(v.reservations.at(1).increment, 9);
}

// Tipo 22 en sus dos formas (area y direccionado) y tipo 23 (grupo).
void TstAis::channelAndGroupVectors()
{
    AisMessage m;
    QVERIFY(decodificarCarga("F01;FQj2N2PFOr0o5<uN1aN9P000", 0, &m));
    QCOMPARE(m.type, 22);
    QCOMPARE(m.channelA, 2087);
    QCOMPARE(m.channelB, 2088);
    QCOMPARE(m.txRxMode, 1);
    QVERIFY(!m.addressed);
    QVERIFY(qAbs(m.neLat - 23.5) < 1.0 / 600.0);
    QVERIFY(qAbs(m.neLon - (-82.0)) < 1.0 / 600.0);
    QVERIFY(qAbs(m.swLat - 22.5) < 1.0 / 600.0);
    QVERIFY(qAbs(m.swLon - (-83.0)) < 1.0 / 600.0);
    QCOMPARE(m.zoneSize, 3);
    QVERIFY(m.bandChannelA);
    QVERIFY(!m.bandChannelB);

    AisMessage a;
    QVERIFY(decodificarCarga("F01;FQj2N2Pbi1;HP3`l5N0E0000", 0, &a));
    QCOMPARE(a.type, 22);
    QVERIFY(a.addressed);
    QCOMPARE(a.destMmsi, quint32(371234500));
    QCOMPARE(a.destMmsi2, quint32(244123000));
    QCOMPARE(a.txRxMode, 2);
    QVERIFY(a.lowPower);
    QCOMPARE(a.zoneSize, 2);

    AisMessage g;
    QVERIFY(decodificarCarga("G01;FQk?u0KRVNg0lg0AP000V<0", 2, &g));
    QCOMPARE(g.type, 23);
    QVERIFY(qAbs(g.neLat - 23.5) < 1.0 / 600.0);
    QVERIFY(qAbs(g.swLon - (-83.0)) < 1.0 / 600.0);
    QCOMPARE(g.stationType, 0);
    QCOMPARE(g.shipType, 70);
    QCOMPARE(g.txRxMode, 2);
    QCOMPARE(g.reportInterval, 6);
    QCOMPARE(g.quietTime, 3);
}

// Tipo 25 (dos formas), 26 (sin contar el estado de comunicaciones como datos)
// y 27 (largo alcance: posicion, velocidad y rumbo enteros y bastos).
void TstAis::singleSlotAndLongRangeVectors()
{
    AisMessage m;
    QVERIFY(decodificarCarga("I3`l5N<0Bm`Lj2Q4R<h", 4, &m));
    QCOMPARE(m.type, 25);
    QVERIFY(m.addressed);
    QVERIFY(m.structured);
    QCOMPARE(m.destMmsi, quint32(1234567));
    QCOMPARE(m.dac, 200);
    QCOMPARE(m.fid, 10);
    QCOMPARE(m.binaryBits, 24);
    QCOMPARE(QString::fromLatin1(m.binaryData.toHex()), QStringLiteral("112233"));

    AisMessage b;
    QVERIFY(decodificarCarga("I3`l5N3wseo<", 0, &b));
    QCOMPARE(b.type, 25);
    QVERIFY(!b.addressed);
    QVERIFY(!b.structured);
    QCOMPARE(b.dac, -1);                                 // sin estructura no hay DAC/FID
    QCOMPARE(QString::fromLatin1(b.binaryData.toHex()), QStringLiteral("ffeeddcc"));

    AisMessage x;
    QVERIFY(decodificarCarga("J3`l5N40GhT81h<3T", 2, &x));
    QCOMPARE(x.type, 26);
    QVERIFY(x.structured);
    QCOMPARE(x.dac, 1);
    QCOMPARE(x.fid, 31);
    QCOMPARE(x.binaryBits, 24);                         // 100 - 40 - 16 - 20 del estado de comunicaciones
    QCOMPARE(QString::fromLatin1(x.binaryData.toHex()), QStringLiteral("090807"));

    AisMessage l;
    QVERIFY(decodificarCarga("K5R2Fi8<vhQdH7@v", 0, &l));
    QCOMPARE(l.type, 27);
    QVERIFY(l.supported);
    QCOMPARE(l.navStatus, 0);
    QVERIFY(l.accuracy);
    QVERIFY(l.gnss);
    QVERIFY(qAbs(l.lat - 23.12) < 1.0 / 600.0);
    QVERIFY(qAbs(l.lon - (-82.45)) < 1.0 / 600.0);
    QCOMPARE(l.sogKn, 14.0);
    QCOMPARE(l.cogDeg, 271.0);
}

// Ida y vuelta de los tipos de gestion (16, 17, 20, 22, 23, 25, 26 y 27):
// lo que encodeAis escribe, decodeAis lo lee igual.
void TstAis::roundTripManagementTypes()
{
    AisMessage m;
    m.type = 16;
    m.mmsi = 1234567;
    AisMessage::Assignment a;
    a.mmsi = 371234500; a.offset = 100; a.increment = 5;
    AisMessage::Assignment b;
    b.mmsi = 244123000; b.offset = 200; b.increment = 10;
    m.assignments = { a, b };
    AisMessage d;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.assignments.size(), 2);
    QCOMPARE(d.assignments.at(1).increment, 10);

    m = AisMessage();
    m.type = 17;
    m.mmsi = 1234999;
    m.dgnssLat = 23.1;
    m.dgnssLon = -82.4;
    m.binaryData = QByteArray::fromHex("aabbcc");
    m.binaryBits = 24;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.binaryData, m.binaryData);
    QVERIFY(qAbs(d.dgnssLat - 23.1) < 1.0 / 600.0);

    m = AisMessage();
    m.type = 20;
    m.mmsi = 1234567;
    AisMessage::Reservation v;
    v.offset = 120; v.slotCount = 2; v.timeout = 5; v.increment = 17;
    m.reservations = { v };
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.reservations.size(), 1);
    QCOMPARE(d.reservations.at(0).timeout, 5);

    m = AisMessage();
    m.type = 22;
    m.mmsi = 1234567;
    m.channelA = 2087; m.channelB = 2088; m.txRxMode = 1;
    m.neLat = 23.5; m.neLon = -82.0; m.swLat = 22.5; m.swLon = -83.0;
    m.zoneSize = 3;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.channelA, 2087);
    QVERIFY(qAbs(d.neLat - 23.5) < 1.0 / 600.0);
    QCOMPARE(d.zoneSize, 3);

    m = AisMessage();
    m.type = 23;
    m.mmsi = 1234567;
    m.neLat = 23.5; m.neLon = -82.0; m.swLat = 22.5; m.swLon = -83.0;
    m.shipType = 70; m.txRxMode = 2; m.reportInterval = 6; m.quietTime = 3;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.shipType, 70);
    QCOMPARE(d.quietTime, 3);

    m = AisMessage();
    m.type = 26;
    m.mmsi = 244123000;
    m.structured = true;
    m.dac = 1; m.fid = 31;
    m.binaryData = QByteArray::fromHex("090807");
    m.binaryBits = 24;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.binaryData, m.binaryData);
    QCOMPARE(d.dac, 1);

    m = AisMessage();
    m.type = 27;
    m.mmsi = 371234500;
    m.lat = 23.12; m.lon = -82.45;
    m.sogKn = 14.0; m.cogDeg = 271.0;
    m.navStatus = 0; m.accuracy = true; m.gnss = true;
    QVERIFY(decodificar(encodeAis(m).first(), &d));
    QCOMPARE(d.sogKn, 14.0);
    QCOMPARE(d.cogDeg, 271.0);
    QVERIFY(d.gnss);
    QVERIFY(qAbs(d.lat - 23.12) < 1.0 / 600.0);
}

QTEST_APPLESS_MAIN(TstAis)
#include "tst_ais.moc"
