#include "telemetria/Nmea.h"

#include <QDate>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QTimeZone>
#endif

#include <cmath>

namespace telemetria {

namespace {

// Un decimal de un campo NMEA, o NaN si esta vacio o no es un numero.
double fieldNumber(const QList<QByteArray> &f, int i)
{
    if (i < 0 || i >= f.size() || f.at(i).isEmpty())
        return notAvailable();
    bool ok = false;
    const double v = f.at(i).toDouble(&ok);
    return ok && std::isfinite(v) ? v : notAvailable();
}

// Un campo NMEA como texto (vacio si no esta).
QByteArray fieldText(const QList<QByteArray> &f, int i)
{
    return (i >= 0 && i < f.size()) ? f.at(i) : QByteArray();
}

// Latitud o longitud en el formato NMEA «ddmm.mmmm» / «dddmm.mmmm» mas su hemisferio, a grados con signo.
// \a degDigits son las cifras de los grados (2 en latitud, 3 en longitud). NaN si falta o no cuadra.
double parseCoordinate(const QByteArray &value, const QByteArray &hemisphere, int degDigits)
{
    if (value.size() <= degDigits || hemisphere.size() != 1)
        return notAvailable();
    bool okDeg = false, okMin = false;
    const int deg = value.left(degDigits).toInt(&okDeg);
    const double minutes = value.mid(degDigits).toDouble(&okMin);
    if (!okDeg || !okMin || minutes < 0.0 || minutes >= 60.0)
        return notAvailable();
    double r = deg + minutes / 60.0;
    switch (hemisphere.at(0)) {
    case 'N': case 'E': break;
    case 'S': case 'W': r = -r; break;
    default: return notAvailable();
    }
    const double limit = degDigits == 2 ? 90.0 : 180.0;
    return std::abs(r) <= limit ? r : notAvailable();
}

// La hora «hhmmss» o «hhmmss.ss»; invalida si no cuadra.
QTime parseTime(const QByteArray &v)
{
    if (v.size() < 6)
        return QTime();
    bool a = false, b = false, c = false;
    const int h = v.mid(0, 2).toInt(&a);
    const int m = v.mid(2, 2).toInt(&b);
    const double s = v.mid(4).toDouble(&c);
    if (!a || !b || !c || s < 0.0 || s >= 60.0)
        return QTime();
    const int sec = int(s);
    return QTime(h, m, sec, int(std::lround((s - sec) * 1000.0)) % 1000);
}

// La fecha «ddmmyy». Los años 80-99 son 19xx y el resto 20xx (el GPS no existia antes de 1980).
QDate parseDate(const QByteArray &v)
{
    if (v.size() != 6)
        return QDate();
    bool a = false, b = false, c = false;
    const int d = v.mid(0, 2).toInt(&a);
    const int m = v.mid(2, 2).toInt(&b);
    const int y = v.mid(4, 2).toInt(&c);
    if (!a || !b || !c)
        return QDate();
    return QDate(y >= 80 ? 1900 + y : 2000 + y, m, d);
}

// El inverso de parseCoordinate: «ddmm.mmmm» + hemisferio, redondeando a la diezmilesima de minuto
// con enteros para no producir «60.0000» minutos.
void formatCoordinate(double deg, bool isLat, QByteArray *value, QByteArray *hemisphere)
{
    if (!std::isfinite(deg)) {
        value->clear();
        hemisphere->clear();
        return;
    }
    const qint64 tenThousandthsOfMinute = qRound64(std::abs(deg) * 600000.0);
    const qint64 whole = tenThousandthsOfMinute / 600000;
    const double minutes = double(tenThousandthsOfMinute % 600000) / 10000.0;
    *value = QByteArray::number(whole).rightJustified(isLat ? 2 : 3, '0')
             + QByteArray::number(minutes, 'f', 4).rightJustified(7, '0');
    *hemisphere = isLat ? (deg >= 0.0 ? "N" : "S") : (deg >= 0.0 ? "E" : "W");
}

// Un decimal como campo, o vacio si es NaN.
QByteArray numberField(double v, int decimals)
{
    return std::isfinite(v) ? QByteArray::number(v, 'f', decimals) : QByteArray();
}

// La hora como campo «hhmmss.ss», o vacio si no es valida.
QByteArray timeField(const QTime &t)
{
    if (!t.isValid())
        return QByteArray();
    return t.toString(QStringLiteral("hhmmss")).toLatin1() + "."
           + QByteArray::number(t.msec() / 10).rightJustified(2, '0');
}

} // namespace

// Fecha y hora UTC sin el constructor obsoleto de Qt 6.9 (con Qt anterior a 6.5 no existe QTimeZone::UTC).
QDateTime utcDateTime(const QDate &date, const QTime &time)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QDateTime(date, time, QTimeZone::UTC);
#else
    return QDateTime(date, time, Qt::UTC);
#endif
}

// XOR de todos los bytes entre el inicio y el '*'.
quint8 nmeaChecksum(const QByteArray &body)
{
    quint8 x = 0;
    for (const char c : body)
        x = quint8(x ^ quint8(c));
    return x;
}

// Separa la linea en direccion y campos, y comprueba la suma si la trae.
Sentence parseSentence(const QByteArray &line, bool requireChecksum)
{
    Sentence s;
    s.raw = line.trimmed();
    const QByteArray &t = s.raw;
    if (t.isEmpty()) {
        s.error = QStringLiteral("linea vacia");
        return s;
    }
    if (t.at(0) != '$' && t.at(0) != '!') {
        s.error = QStringLiteral("no empieza por $ ni !");
        return s;
    }
    s.encapsulated = t.at(0) == '!';
    QByteArray body = t.mid(1);
    const int star = int(body.lastIndexOf('*'));
    if (star >= 0) {
        const QByteArray hex = body.mid(star + 1);
        body.truncate(star);
        bool ok = false;
        const uint declared = hex.size() == 2 ? hex.toUInt(&ok, 16) : 0u;
        if (!ok) {
            s.error = QStringLiteral("la suma de comprobacion no son dos cifras hexadecimales");
            return s;
        }
        s.checksumPresent = true;
        s.checksumOk = declared == nmeaChecksum(body);
        if (!s.checksumOk) {
            const QString llega = QString::number(declared, 16).rightJustified(2, QLatin1Char('0')).toUpper();
            const QString sale = QString::number(uint(nmeaChecksum(body)), 16).rightJustified(2, QLatin1Char('0')).toUpper();
            s.error = QStringLiteral("suma de comprobacion incorrecta (llega %1, sale %2)").arg(llega, sale);
            return s;
        }
    } else if (requireChecksum) {
        s.error = QStringLiteral("sin suma de comprobacion");
        return s;
    }
    const QList<QByteArray> parts = body.split(',');
    s.address = parts.first();
    if (s.address.size() < 3) {
        s.error = QStringLiteral("direccion demasiado corta");
        return s;
    }
    if (s.address.size() == 5 && s.address.at(0) != 'P') {
        s.talker = s.address.left(2);
        s.type = s.address.mid(2);
    }
    s.fields = parts.mid(1);
    s.valid = true;
    return s;
}

// Compone «$ADDRESS,campos*XX».
QByteArray formatSentence(const QByteArray &address, const QList<QByteArray> &fields, bool encapsulated)
{
    QByteArray body = address;
    for (const QByteArray &f : fields)
        body += ',' + f;
    return QByteArray(1, encapsulated ? '!' : '$') + body + '*'
           + QByteArray::number(nmeaChecksum(body), 16).toUpper().rightJustified(2, '0');
}

// RMC: hora, estado, posicion, velocidad, rumbo, fecha y variacion magnetica.
bool decodeRmc(const Sentence &s, Rmc *out)
{
    if (!s.valid || s.type != "RMC" || s.fields.size() < 9)
        return false;
    Rmc r;
    r.time = parseTime(fieldText(s.fields, 0));
    r.valid = fieldText(s.fields, 1) == "A";
    r.lat = parseCoordinate(fieldText(s.fields, 2), fieldText(s.fields, 3), 2);
    r.lon = parseCoordinate(fieldText(s.fields, 4), fieldText(s.fields, 5), 3);
    r.sogKn = fieldNumber(s.fields, 6);
    r.cogDeg = fieldNumber(s.fields, 7);
    const QDate date = parseDate(fieldText(s.fields, 8));
    if (date.isValid() && r.time.isValid())
        r.utc = utcDateTime(date, r.time);
    const double var = fieldNumber(s.fields, 9);
    if (std::isfinite(var))
        r.magVarDeg = fieldText(s.fields, 10) == "W" ? -var : var;
    *out = r;
    return true;
}

// GGA: hora, posicion, calidad, satelites, hdop y altitud.
bool decodeGga(const Sentence &s, Gga *out)
{
    if (!s.valid || s.type != "GGA" || s.fields.size() < 9)
        return false;
    Gga g;
    g.time = parseTime(fieldText(s.fields, 0));
    g.lat = parseCoordinate(fieldText(s.fields, 1), fieldText(s.fields, 2), 2);
    g.lon = parseCoordinate(fieldText(s.fields, 3), fieldText(s.fields, 4), 3);
    g.quality = fieldText(s.fields, 5).toInt();
    bool ok = false;
    const int sats = fieldText(s.fields, 6).toInt(&ok);
    g.satellites = ok ? sats : -1;
    g.hdop = fieldNumber(s.fields, 7);
    g.altitudeM = fieldNumber(s.fields, 8);
    *out = g;
    return true;
}

// VTG: rumbo verdadero y magnetico, velocidad en nudos y km/h.
bool decodeVtg(const Sentence &s, Vtg *out)
{
    if (!s.valid || s.type != "VTG" || s.fields.size() < 7)
        return false;
    Vtg v;
    v.cogTrueDeg = fieldNumber(s.fields, 0);
    v.cogMagDeg = fieldNumber(s.fields, 2);
    v.sogKn = fieldNumber(s.fields, 4);
    v.sogKmh = fieldNumber(s.fields, 6);
    *out = v;
    return true;
}

// HDT: rumbo de la proa.
bool decodeHdt(const Sentence &s, Hdt *out)
{
    if (!s.valid || s.type != "HDT" || s.fields.size() < 1)
        return false;
    Hdt h;
    h.headingDeg = fieldNumber(s.fields, 0);
    *out = h;
    return true;
}

// Compone la frase RMC de \a r.
QByteArray formatRmc(const Rmc &r, const QByteArray &talker)
{
    QByteArray lat, ns, lon, ew;
    formatCoordinate(r.lat, true, &lat, &ns);
    formatCoordinate(r.lon, false, &lon, &ew);
    const QByteArray date = r.utc.isValid() ? r.utc.toString(QStringLiteral("ddMMyy")).toLatin1() : QByteArray();
    const double var = r.magVarDeg;
    return formatSentence(talker + "RMC",
                          { timeField(r.time.isValid() ? r.time : r.utc.time()), r.valid ? "A" : "V", lat, ns, lon, ew,
                            numberField(r.sogKn, 1), numberField(r.cogDeg, 1), date, numberField(std::abs(var), 1),
                            std::isfinite(var) ? (var < 0.0 ? "W" : "E") : "" });
}

// Compone la frase GGA de \a g.
QByteArray formatGga(const Gga &g, const QByteArray &talker)
{
    QByteArray lat, ns, lon, ew;
    formatCoordinate(g.lat, true, &lat, &ns);
    formatCoordinate(g.lon, false, &lon, &ew);
    return formatSentence(talker + "GGA",
                          { timeField(g.time), lat, ns, lon, ew, QByteArray::number(g.quality),
                            g.satellites >= 0 ? QByteArray::number(g.satellites).rightJustified(2, '0') : QByteArray(),
                            numberField(g.hdop, 1), numberField(g.altitudeM, 1), "M", "", "M", "", "" });
}

// Compone la frase VTG de \a v.
QByteArray formatVtg(const Vtg &v, const QByteArray &talker)
{
    return formatSentence(talker + "VTG", { numberField(v.cogTrueDeg, 1), "T", numberField(v.cogMagDeg, 1), "M",
                                            numberField(v.sogKn, 1), "N", numberField(v.sogKmh, 1), "K" });
}

// Compone la frase HDT de \a h.
QByteArray formatHdt(const Hdt &h, const QByteArray &talker)
{
    return formatSentence(talker + "HDT", { numberField(h.headingDeg, 2), "T" });
}

} // namespace telemetria
