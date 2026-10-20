#include "telemetria/Nmea.h"

#include <QtTest>

#include <cmath>

using namespace telemetria;

//! Tests del analisis de frases NMEA 0183 y de RMC, GGA, VTG y HDT.
class TstNmea : public QObject
{
    Q_OBJECT

private slots:
    void checksumMatchesPublishedSamples();
    void parseSplitsFields();
    void parseRejectsBadInput();
    void parseWithoutChecksum();
    void rmcSample();
    void rmcVoidAndMissingFields();
    void ggaSample();
    void vtgSample();
    void hdtSample();
    void formatRoundTrips();
    void coordinateNeverPrints60Minutes();
    void decodersRejectOtherTypes();
};

// Las frases de ejemplo de uso comun (la documentacion de NMEA 0183 y de gpsd), con la suma que traen publicada.
static const QByteArray kRmc =
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
static const QByteArray kGga =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const QByteArray kVtg =
    "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48";

// El XOR de las frases publicadas sale justo en la suma publicada (6A, 47 y 48).
void TstNmea::checksumMatchesPublishedSamples()
{
    QCOMPARE(nmeaChecksum("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"), quint8(0x6A));
    QCOMPARE(nmeaChecksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"), quint8(0x47));
    QCOMPARE(nmeaChecksum("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K"), quint8(0x48));
    QCOMPARE(nmeaChecksum(""), quint8(0));
}

// Direccion, emisor, tipo y campos (incluidos los vacios) de una frase correcta.
void TstNmea::parseSplitsFields()
{
    const Sentence s = parseSentence(kRmc + "\r\n");
    QVERIFY2(s.valid, qPrintable(s.error));
    QVERIFY(s.checksumPresent && s.checksumOk);
    QVERIFY(!s.encapsulated);
    QCOMPARE(s.address, QByteArray("GPRMC"));
    QCOMPARE(s.talker, QByteArray("GP"));
    QCOMPARE(s.type, QByteArray("RMC"));
    QCOMPARE(s.fields.size(), 11);
    QCOMPARE(s.fields.at(0), QByteArray("123519"));
    QCOMPARE(s.raw, kRmc);

    const Sentence g = parseSentence(kGga);
    QCOMPARE(g.fields.size(), 14);               // los campos vacios finales cuentan
    QVERIFY(g.fields.at(12).isEmpty());

    const Sentence a = parseSentence("!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C");
    QVERIFY(a.valid);
    QVERIFY(a.encapsulated);
    QCOMPARE(a.type, QByteArray("VDM"));

    const Sentence p = parseSentence("$PGRMZ,93,f,3*21");
    QVERIFY(p.valid);
    QVERIFY(p.type.isEmpty());                    // propietaria: sin emisor ni tipo
    QCOMPARE(p.address, QByteArray("PGRMZ"));
}

// Basura, suma mala, suma que no es hexadecimal o frase sin direccion.
void TstNmea::parseRejectsBadInput()
{
    QVERIFY(!parseSentence("").valid);
    QVERIFY(!parseSentence("GPRMC,1,2*00").valid);                          // sin $
    const Sentence mala = parseSentence("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B");
    QVERIFY(!mala.valid);
    QVERIFY(mala.checksumPresent && !mala.checksumOk);
    QVERIFY2(mala.error.contains(QStringLiteral("6A")), qPrintable(mala.error));   // dice cual deberia ser
    QVERIFY(!parseSentence("$GPRMC,1*ZZ").valid);
    QVERIFY(!parseSentence("$GPRMC,1*5").valid);
    QVERIFY(!parseSentence("$GP*00").valid);                                // direccion corta
    // Un byte cambiado en medio de la frase la invalida.
    QByteArray alterada = kRmc;
    alterada[20] = 'X';
    QVERIFY(!parseSentence(alterada).valid);
}

// Las frases antiguas sin suma se aceptan solo si se pide.
void TstNmea::parseWithoutChecksum()
{
    QVERIFY(!parseSentence("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K").valid);
    const Sentence s = parseSentence("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K", false);
    QVERIFY(s.valid);
    QVERIFY(!s.checksumPresent);
}

// RMC: la frase de ejemplo de NMEA 0183 (48°07.038'N, 11°31.000'E, 22.4 kn, 84.4°, 23-03-1994 12:35:19, 3.1° W).
void TstNmea::rmcSample()
{
    Rmc r;
    QVERIFY(decodeRmc(parseSentence(kRmc), &r));
    QVERIFY(r.valid);
    QVERIFY(qAbs(r.lat - (48.0 + 7.038 / 60.0)) < 1e-9);
    QVERIFY(qAbs(r.lon - (11.0 + 31.0 / 60.0)) < 1e-9);
    QCOMPARE(r.sogKn, 22.4);
    QCOMPARE(r.cogDeg, 84.4);
    QCOMPARE(r.utc, utcDateTime(QDate(1994, 3, 23), QTime(12, 35, 19)));
    QCOMPARE(r.magVarDeg, -3.1);                  // W = negativa
}

// Estado V (sin fijar) y campos vacios: no inventa nada.
void TstNmea::rmcVoidAndMissingFields()
{
    Rmc r;
    QVERIFY(decodeRmc(parseSentence(formatSentence("GPRMC", { "123519", "V", "", "", "", "", "", "", "230394", "", "" })), &r));
    QVERIFY(!r.valid);
    QVERIFY(std::isnan(r.lat) && std::isnan(r.lon) && std::isnan(r.sogKn) && std::isnan(r.cogDeg));
    QVERIFY(r.time.isValid());
    // Hemisferio que no existe o minutos fuera de rango: la coordenada queda en NaN.
    QVERIFY(decodeRmc(parseSentence(formatSentence("GPRMC", { "123519", "A", "4807.038", "X", "01131.000", "E", "1", "2", "230394", "", "" })), &r));
    QVERIFY(std::isnan(r.lat));
    QVERIFY(decodeRmc(parseSentence(formatSentence("GPRMC", { "123519", "A", "4867.038", "N", "01131.000", "E", "1", "2", "230394", "", "" })), &r));
    QVERIFY(std::isnan(r.lat));
    // Con menos campos de los que exige la frase, no se decodifica.
    QVERIFY(!decodeRmc(parseSentence(formatSentence("GPRMC", { "123519", "A" })), &r));
}

// GGA: calidad 1 (GPS), 8 satelites, hdop 0.9, 545.4 m.
void TstNmea::ggaSample()
{
    Gga g;
    QVERIFY(decodeGga(parseSentence(kGga), &g));
    QVERIFY(qAbs(g.lat - (48.0 + 7.038 / 60.0)) < 1e-9);
    QVERIFY(qAbs(g.lon - (11.0 + 31.0 / 60.0)) < 1e-9);
    QCOMPARE(g.quality, 1);
    QCOMPARE(g.satellites, 8);
    QCOMPARE(g.hdop, 0.9);
    QCOMPARE(g.altitudeM, 545.4);
    QCOMPARE(g.time, QTime(12, 35, 19));
}

// VTG: 54.7° verdadero, 34.4° magnetico, 5.5 kn, 10.2 km/h.
void TstNmea::vtgSample()
{
    Vtg v;
    QVERIFY(decodeVtg(parseSentence(kVtg), &v));
    QCOMPARE(v.cogTrueDeg, 54.7);
    QCOMPARE(v.cogMagDeg, 34.4);
    QCOMPARE(v.sogKn, 5.5);
    QCOMPARE(v.sogKmh, 10.2);
}

// HDT: rumbo de la proa.
void TstNmea::hdtSample()
{
    Hdt h;
    QVERIFY(decodeHdt(parseSentence(formatSentence("HEHDT", { "274.07", "T" })), &h));
    QCOMPARE(h.headingDeg, 274.07);
}

// Lo que se codifica se vuelve a decodificar igual, tambien en el hemisferio sur y al oeste.
void TstNmea::formatRoundTrips()
{
    Rmc r;
    r.valid = true;
    r.time = QTime(23, 59, 58, 120);
    r.utc = utcDateTime(QDate(2026, 10, 19), r.time);
    r.lat = -33.865143;
    r.lon = -70.123456;
    r.sogKn = 12.3;
    r.cogDeg = 359.9;
    r.magVarDeg = 4.5;
    const QByteArray linea = formatRmc(r);
    QVERIFY(linea.startsWith("$GPRMC,235958.12,A,3351."));
    Rmc d;
    QVERIFY(decodeRmc(parseSentence(linea), &d));
    QVERIFY(d.valid);
    QVERIFY(qAbs(d.lat - r.lat) < 1.0 / 600000.0);     // la resolucion de la frase: 1/10000 de minuto
    QVERIFY(qAbs(d.lon - r.lon) < 1.0 / 600000.0);
    QCOMPARE(d.sogKn, 12.3);
    QCOMPARE(d.cogDeg, 359.9);
    QCOMPARE(d.magVarDeg, 4.5);
    QCOMPARE(d.utc.date(), QDate(2026, 10, 19));
    QCOMPARE(d.time.msecsSinceStartOfDay() / 10, r.time.msecsSinceStartOfDay() / 10);

    Gga g;
    g.time = QTime(1, 2, 3);
    g.lat = 23.1136;
    g.lon = -82.3666;
    g.quality = 2;
    g.satellites = 9;
    g.hdop = 1.1;
    g.altitudeM = 25.0;
    Gga gd;
    QVERIFY(decodeGga(parseSentence(formatGga(g)), &gd));
    QVERIFY(qAbs(gd.lat - g.lat) < 1.0 / 600000.0 && qAbs(gd.lon - g.lon) < 1.0 / 600000.0);
    QCOMPARE(gd.quality, 2);
    QCOMPARE(gd.satellites, 9);

    Vtg v;
    v.cogTrueDeg = 10.5;
    v.sogKn = 3.0;
    Vtg vd;
    QVERIFY(decodeVtg(parseSentence(formatVtg(v)), &vd));
    QCOMPARE(vd.cogTrueDeg, 10.5);
    QVERIFY(std::isnan(vd.cogMagDeg));                  // lo que no se dio sigue sin darse
}

// Una latitud como 59.99999999° se redondea al minuto siguiente sin escribir «60.0000».
void TstNmea::coordinateNeverPrints60Minutes()
{
    Rmc r;
    r.valid = true;
    r.time = QTime(0, 0, 0);
    r.lat = 59.99999999;
    r.lon = 10.0 - 1e-9;
    const Sentence s = parseSentence(formatRmc(r));
    QVERIFY(s.valid);
    QVERIFY2(!s.fields.at(2).contains("60."), s.fields.at(2).constData());
    QCOMPARE(s.fields.at(2), QByteArray("6000.0000"));
    Rmc d;
    QVERIFY(decodeRmc(s, &d));
    QVERIFY(qAbs(d.lat - 60.0) < 1e-6);
    QVERIFY(qAbs(d.lon - 10.0) < 1e-6);
}

// Cada decodificador solo acepta su tipo.
void TstNmea::decodersRejectOtherTypes()
{
    Rmc r;
    Gga g;
    Vtg v;
    Hdt h;
    QVERIFY(!decodeRmc(parseSentence(kGga), &r));
    QVERIFY(!decodeGga(parseSentence(kVtg), &g));
    QVERIFY(!decodeVtg(parseSentence(kRmc), &v));
    QVERIFY(!decodeHdt(parseSentence(kRmc), &h));
    QVERIFY(!decodeRmc(Sentence(), &r));                // una frase invalida no se decodifica
}

QTEST_APPLESS_MAIN(TstNmea)
#include "tst_nmea.moc"
