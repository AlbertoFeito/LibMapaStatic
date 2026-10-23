#include "telemetria/Ais.h"

#include <algorithm>
#include <cmath>

namespace telemetria {

namespace {

// «No disponible» de la posicion en AIS: 181° de longitud y 91° de latitud, en 1/10000 de minuto.
const qint32 kLonNoDisponible = 108600000;
const qint32 kLatNoDisponible = 54600000;
// Cuantos caracteres de carga caben en cada frase (con la cabecera y la suma, la linea queda bajo los 82 de NMEA).
const int kCargaPorFrase = 60;

// Los bits de una carga AIS: cada caracter aporta 6, el primero el mas significativo.
class BitReader
{
public:
    // false si algun caracter no es del alfabeto de 6 bits (ASCII 48-87 y 96-119).
    bool load(const QByteArray &payload, int fillBits)
    {
        m_bits.clear();
        m_bits.reserve(int(payload.size()) * 6);
        for (const char ch : payload) {
            const int c = int(quint8(ch));
            if (!((c >= 48 && c <= 87) || (c >= 96 && c <= 119)))
                return false;
            int v = c - 48;
            if (v > 40)
                v -= 8;
            for (int b = 5; b >= 0; --b)
                m_bits.append(quint8((v >> b) & 1));
        }
        if (fillBits < 0 || fillBits > 5 || fillBits > m_bits.size())
            return false;
        m_bits.resize(m_bits.size() - fillBits);
        return true;
    }

    int size() const { return int(m_bits.size()); }

    // \a len bits sin signo desde \a pos (hasta 32).
    quint32 u(int pos, int len) const
    {
        quint32 v = 0;
        for (int i = 0; i < len; ++i)
            v = (v << 1) | quint32(m_bits.at(pos + i));
        return v;
    }

    // \a len bits en complemento a dos.
    qint32 s(int pos, int len) const
    {
        const quint32 v = u(pos, len);
        if (len < 32 && (v & (1u << (len - 1))) != 0)
            return qint32(qint64(v) - (qint64(1) << len));
        return qint32(v);
    }

    // \a chars caracteres de 6 bits desde \a pos, sin el relleno final ('@' y espacios).
    QString text(int pos, int chars) const
    {
        QString r;
        for (int i = 0; i < chars; ++i) {
            const quint32 v = u(pos + 6 * i, 6);
            r.append(QChar(ushort(v < 32 ? 64 + v : v)));
        }
        const int at = int(r.indexOf(QLatin1Char('@')));
        if (at >= 0)
            r.truncate(at);
        return r.trimmed();
    }

    // Los \a len bits desde \a pos como bytes, el primero el mas significativo
    // (el ultimo byte se completa con ceros si \a len no es multiplo de 8).
    QByteArray bytes(int pos, int len) const
    {
        QByteArray out;
        for (int i = 0; i < len; i += 8) {
            const int n = std::min(8, len - i);
            int v = 0;
            for (int b = 0; b < n; ++b)
                v = (v << 1) | int(m_bits.at(pos + i + b));
            out.append(char(v << (8 - n)));
        }
        return out;
    }

private:
    QVector<quint8> m_bits;
};

// Escribe los campos de un mensaje AIS bit a bit y los empaqueta en caracteres de 6 bits.
class BitWriter
{
public:
    void u(quint32 value, int len)
    {
        for (int b = len - 1; b >= 0; --b)
            m_bits.append(quint8((value >> b) & 1u));
    }
    void s(qint32 value, int len) { u(quint32(value) & (len < 32 ? ((1u << len) - 1u) : 0xffffffffu), len); }

    // \a text en mayusculas, ajustado a \a chars caracteres y relleno con '@'.
    void text(const QString &text, int chars)
    {
        const QString t = text.toUpper();
        for (int i = 0; i < chars; ++i) {
            quint32 v = 0;                               // '@' = relleno
            if (i < t.size()) {
                const ushort c = t.at(i).unicode();
                if (c >= 64 && c <= 95)
                    v = quint32(c - 64);
                else if (c >= 32 && c <= 63)
                    v = c;
                else
                    v = 32;                              // lo que no cabe en 6 bits, espacio
            }
            u(v, 6);
        }
    }

    // Los \a bits primeros bits de \a data, el primero el mas significativo.
    void raw(const QByteArray &data, int bits)
    {
        for (int i = 0; i < bits; ++i) {
            const int byte = i / 8;
            const quint8 v = byte < data.size() ? quint8(data.at(byte)) : quint8(0);
            m_bits.append(quint8((v >> (7 - i % 8)) & 1));
        }
    }

    // La carga: los bits rellenados con ceros hasta multiplo de 6, y el relleno que hubo.
    QByteArray payload(int *fillBits) const
    {
        QByteArray out;
        const int fill = (6 - int(m_bits.size()) % 6) % 6;
        QVector<quint8> bits = m_bits;
        bits.resize(bits.size() + fill);
        for (int i = 0; i < bits.size(); i += 6) {
            int v = 0;
            for (int b = 0; b < 6; ++b)
                v = (v << 1) | bits.at(i + b);
            out.append(char(v < 40 ? v + 48 : v + 56));
        }
        *fillBits = fill;
        return out;
    }

private:
    QVector<quint8> m_bits;
};

// Grados a 1/10000 de minuto (lo que AIS guarda), o «no disponible» si \a deg no es un numero.
qint32 toAisCoord(double deg, qint32 noDisponible)
{
    return std::isfinite(deg) ? qint32(std::lround(deg * 600000.0)) : noDisponible;
}

// De 1/10000 de minuto a grados, o NaN si es el valor de «no disponible» (o esta fuera de rango).
double fromAisCoord(qint32 v, qint32 noDisponible, double limit)
{
    if (v == noDisponible)
        return notAvailable();
    const double deg = double(v) / 600000.0;
    return std::abs(deg) <= limit ? deg : notAvailable();
}

// Los tipos 17, 22, 23 y 27 guardan las coordenadas en 1/10 de minuto (mas basto).
const qint32 kLonNoDisponibleC = 108600;
const qint32 kLatNoDisponibleC = 54600;

double fromAisCoordC(qint32 v, qint32 noDisponible, double limit)
{
    if (v == noDisponible)
        return notAvailable();
    const double deg = double(v) / 600.0;
    return std::abs(deg) <= limit ? deg : notAvailable();
}

qint32 toAisCoordC(double deg, qint32 noDisponible)
{
    return std::isfinite(deg) ? qint32(std::lround(deg * 600.0)) : noDisponible;
}

// Campos comunes de posicion de clase B (tipos 18 y 19): del bit 46 en adelante.
void readClassBPosition(const BitReader &r, AisMessage *m)
{
    const quint32 sog = r.u(46, 10);
    m->sogKn = sog == 1023 ? notAvailable() : double(sog) / 10.0;
    m->accuracy = r.u(56, 1) != 0;
    m->lon = fromAisCoord(r.s(57, 28), kLonNoDisponible, 180.0);
    m->lat = fromAisCoord(r.s(85, 27), kLatNoDisponible, 90.0);
    const quint32 cog = r.u(112, 12);
    m->cogDeg = cog >= 3600 ? notAvailable() : double(cog) / 10.0;
    const quint32 hdg = r.u(124, 9);
    m->headingDeg = hdg == 511 ? -1 : int(hdg);
    m->second = int(r.u(133, 6));
}

} // namespace

// Decodifica un mensaje AIS completo segun su tipo.
bool decodeAis(const QByteArray &payload, int fillBits, AisMessage *out)
{
    BitReader r;
    if (!r.load(payload, fillBits) || r.size() < 38)
        return false;
    AisMessage m;
    m.type = int(r.u(0, 6));
    m.repeat = int(r.u(6, 2));
    m.mmsi = r.u(8, 30);

    switch (m.type) {
    case 1: case 2: case 3: {
        if (r.size() < 168)
            return false;
        m.supported = true;
        m.navStatus = int(r.u(38, 4));
        const quint32 sog = r.u(50, 10);
        m.sogKn = sog == 1023 ? notAvailable() : double(sog) / 10.0;
        m.accuracy = r.u(60, 1) != 0;
        m.lon = fromAisCoord(r.s(61, 28), kLonNoDisponible, 180.0);
        m.lat = fromAisCoord(r.s(89, 27), kLatNoDisponible, 90.0);
        const quint32 cog = r.u(116, 12);
        m.cogDeg = cog >= 3600 ? notAvailable() : double(cog) / 10.0;
        const quint32 hdg = r.u(128, 9);
        m.headingDeg = hdg == 511 ? -1 : int(hdg);
        m.second = int(r.u(137, 6));
        break;
    }
    case 4: case 11: {
        // Estacion base (4) y respuesta a la peticion de fecha y hora (11):
        // misma estructura, con la hora UTC completa y la posicion de la estacion.
        if (r.size() < 168)
            return false;
        m.supported = true;
        m.utcYear = int(r.u(38, 14));
        m.utcMonth = int(r.u(52, 4));
        m.utcDay = int(r.u(56, 5));
        m.utcHour = int(r.u(61, 5));
        m.utcMinute = int(r.u(66, 6));
        m.utcSecond = int(r.u(72, 6));
        m.accuracy = r.u(78, 1) != 0;
        m.lon = fromAisCoord(r.s(79, 28), kLonNoDisponible, 180.0);
        m.lat = fromAisCoord(r.s(107, 27), kLatNoDisponible, 90.0);
        m.epfsType = int(r.u(134, 4));
        break;
    }
    case 5: {
        if (r.size() < 420)                              // el ultimo bit de relleno puede faltar
            return false;
        m.supported = true;
        m.imo = r.u(40, 30);
        m.callSign = r.text(70, 7);
        m.name = r.text(112, 20);
        m.shipType = int(r.u(232, 8));
        m.toBowM = int(r.u(240, 9));
        m.toSternM = int(r.u(249, 9));
        m.toPortM = int(r.u(258, 6));
        m.toStarboardM = int(r.u(264, 6));
        m.etaMonth = int(r.u(274, 4));
        m.etaDay = int(r.u(278, 5));
        m.etaHour = int(r.u(283, 5));
        m.etaMinute = int(r.u(288, 6));
        const quint32 draught = r.u(294, 8);
        m.draughtM = double(draught) / 10.0;
        m.destination = r.text(302, 20);
        break;
    }
    case 6: {
        // Binario direccionado: destino, DAC/FID y la carga de aplicacion sin tocar.
        if (r.size() < 88)
            return false;
        m.supported = true;
        m.sequence = int(r.u(38, 2));
        m.destMmsi = r.u(40, 30);
        m.retransmit = r.u(70, 1) != 0;
        m.dac = int(r.u(72, 10));
        m.fid = int(r.u(82, 6));
        m.binaryBits = r.size() - 88;
        m.binaryData = r.bytes(88, m.binaryBits);
        break;
    }
    case 7: case 13: {
        // Acuses: hasta cuatro parejas (MMSI, secuencia) tras la cabecera.
        if (r.size() < 72)
            return false;
        m.supported = true;
        for (int i = 0; i < 4; ++i) {
            const int base = 40 + 32 * i;
            if (r.size() < base + 32)
                break;
            m.ackMmsi.append(r.u(base, 30));
            m.ackSequence.append(int(r.u(base + 30, 2)));
        }
        break;
    }
    case 8: {
        // Binario difundido: DAC/FID y la carga de aplicacion sin tocar.
        if (r.size() < 56)
            return false;
        m.supported = true;
        m.dac = int(r.u(40, 10));
        m.fid = int(r.u(50, 6));
        m.binaryBits = r.size() - 56;
        m.binaryData = r.bytes(56, m.binaryBits);
        break;
    }
    case 9: {
        // Aeronave SAR: como el informe de clase A, pero con altitud y con la
        // velocidad en nudos enteros (1023 = no disponible), y sin proa.
        if (r.size() < 168)
            return false;
        m.supported = true;
        const quint32 alt = r.u(38, 12);
        m.altitudeM = alt == 4095 ? notAvailable() : double(alt);
        const quint32 sog = r.u(50, 10);
        m.sogKn = sog == 1023 ? notAvailable() : double(sog);
        m.accuracy = r.u(60, 1) != 0;
        m.lon = fromAisCoord(r.s(61, 28), kLonNoDisponible, 180.0);
        m.lat = fromAisCoord(r.s(89, 27), kLatNoDisponible, 90.0);
        const quint32 cog = r.u(116, 12);
        m.cogDeg = cog >= 3600 ? notAvailable() : double(cog) / 10.0;
        m.second = int(r.u(128, 6));
        m.assigned = r.u(146, 1) != 0;
        break;
    }
    case 10: {
        if (r.size() < 72)
            return false;
        m.supported = true;
        m.destMmsi = r.u(40, 30);
        break;
    }
    case 12: {
        // Texto de seguridad direccionado: el texto ocupa lo que quede de mensaje.
        if (r.size() < 72)
            return false;
        m.supported = true;
        m.sequence = int(r.u(38, 2));
        m.destMmsi = r.u(40, 30);
        m.retransmit = r.u(70, 1) != 0;
        m.text = r.text(72, (r.size() - 72) / 6);
        break;
    }
    case 14: {
        // Texto de seguridad difundido.
        if (r.size() < 40)
            return false;
        m.supported = true;
        m.text = r.text(40, (r.size() - 40) / 6);
        break;
    }
    case 15: {
        // Interrogacion: una estacion obligatoria y una segunda si el mensaje
        // llega a 160 bits; a la primera se le pueden pedir dos tipos.
        if (r.size() < 88)
            return false;
        m.supported = true;
        AisMessage::Interrogation q;
        q.mmsi = r.u(40, 30);
        q.type1 = int(r.u(70, 6));
        q.offset1 = int(r.u(76, 12));
        if (r.size() >= 110) {
            q.type2 = int(r.u(90, 6));
            q.offset2 = int(r.u(96, 12));
        }
        m.interrogations.append(q);
        if (r.size() >= 160) {
            AisMessage::Interrogation q2;
            q2.mmsi = r.u(110, 30);
            q2.type1 = int(r.u(140, 6));
            q2.offset1 = int(r.u(146, 12));
            m.interrogations.append(q2);
        }
        break;
    }
    case 16: {
        // Asignacion de modo: una estacion, o dos si el mensaje llega a 144 bits.
        if (r.size() < 96)
            return false;
        m.supported = true;
        AisMessage::Assignment a;
        a.mmsi = r.u(40, 30);
        a.offset = int(r.u(70, 12));
        a.increment = int(r.u(82, 10));
        m.assignments.append(a);
        if (r.size() >= 144) {
            AisMessage::Assignment b;
            b.mmsi = r.u(92, 30);
            b.offset = int(r.u(122, 12));
            b.increment = int(r.u(134, 10));
            m.assignments.append(b);
        }
        break;
    }
    case 17: {
        // Correcciones DGNSS difundidas: posicion de la estacion (en 1/10 de
        // minuto) y la carga de correcciones sin tocar.
        if (r.size() < 80)
            return false;
        m.supported = true;
        m.dgnssLon = fromAisCoordC(r.s(40, 18), kLonNoDisponibleC, 180.0);
        m.dgnssLat = fromAisCoordC(r.s(58, 17), kLatNoDisponibleC, 90.0);
        m.binaryBits = r.size() - 80;
        m.binaryData = r.bytes(80, m.binaryBits);
        break;
    }
    case 18: {
        if (r.size() < 168)
            return false;
        m.supported = true;
        m.classB = true;
        readClassBPosition(r, &m);
        break;
    }
    case 19: {
        if (r.size() < 312)
            return false;
        m.supported = true;
        m.classB = true;
        readClassBPosition(r, &m);
        m.name = r.text(143, 20);
        m.shipType = int(r.u(263, 8));
        m.toBowM = int(r.u(271, 9));
        m.toSternM = int(r.u(280, 9));
        m.toPortM = int(r.u(289, 6));
        m.toStarboardM = int(r.u(295, 6));
        m.epfsType = int(r.u(301, 4));
        m.assigned = r.u(307, 1) != 0;
        break;
    }
    case 20: {
        // Gestion del enlace de datos: de una a cuatro reservas de 30 bits.
        if (r.size() < 72)
            return false;
        m.supported = true;
        for (int i = 0; i < 4; ++i) {
            const int base = 40 + 30 * i;
            if (r.size() < base + 30)
                break;
            AisMessage::Reservation v;
            v.offset = int(r.u(base, 12));
            v.slotCount = int(r.u(base + 12, 4));
            v.timeout = int(r.u(base + 16, 3));
            v.increment = int(r.u(base + 19, 11));
            m.reservations.append(v);
        }
        break;
    }
    case 21: {
        // Ayuda a la navegacion: tipo, nombre (20 caracteres mas la extension
        // que quepa tras el bit 272), posicion y dimensiones de la ayuda.
        if (r.size() < 272)
            return false;
        m.supported = true;
        m.atonType = int(r.u(38, 5));
        m.name = r.text(43, 20);
        m.accuracy = r.u(163, 1) != 0;
        m.lon = fromAisCoord(r.s(164, 28), kLonNoDisponible, 180.0);
        m.lat = fromAisCoord(r.s(192, 27), kLatNoDisponible, 90.0);
        m.toBowM = int(r.u(219, 9));
        m.toSternM = int(r.u(228, 9));
        m.toPortM = int(r.u(237, 6));
        m.toStarboardM = int(r.u(243, 6));
        m.epfsType = int(r.u(249, 4));
        m.second = int(r.u(253, 6));
        m.offPosition = r.u(259, 1) != 0;
        m.virtualAton = r.u(269, 1) != 0;
        m.assigned = r.u(270, 1) != 0;
        if (r.size() > 272)
            m.name += r.text(272, (r.size() - 272) / 6);
        break;
    }
    case 22: {
        // Gestion de canales: dos canales y o dos MMSI de destino o un area
        // (esquinas en 1/10 de minuto), segun el indicador de direccionado.
        if (r.size() < 168)
            return false;
        m.supported = true;
        m.channelA = int(r.u(40, 12));
        m.channelB = int(r.u(52, 12));
        m.txRxMode = int(r.u(64, 4));
        m.lowPower = r.u(68, 1) != 0;
        m.addressed = r.u(139, 1) != 0;
        if (m.addressed) {
            m.destMmsi = r.u(69, 30);
            m.destMmsi2 = r.u(104, 30);
        } else {
            m.neLon = fromAisCoordC(r.s(69, 18), kLonNoDisponibleC, 180.0);
            m.neLat = fromAisCoordC(r.s(87, 17), kLatNoDisponibleC, 90.0);
            m.swLon = fromAisCoordC(r.s(104, 18), kLonNoDisponibleC, 180.0);
            m.swLat = fromAisCoordC(r.s(122, 17), kLatNoDisponibleC, 90.0);
        }
        m.bandChannelA = r.u(140, 1) != 0;
        m.bandChannelB = r.u(141, 1) != 0;
        m.zoneSize = int(r.u(142, 3));
        break;
    }
    case 23: {
        // Asignacion a un grupo: area (en 1/10 de minuto), tipo de estacion y
        // de barco a los que se aplica, modo Tx/Rx, intervalo y silencio.
        if (r.size() < 160)
            return false;
        m.supported = true;
        m.neLon = fromAisCoordC(r.s(40, 18), kLonNoDisponibleC, 180.0);
        m.neLat = fromAisCoordC(r.s(58, 17), kLatNoDisponibleC, 90.0);
        m.swLon = fromAisCoordC(r.s(75, 18), kLonNoDisponibleC, 180.0);
        m.swLat = fromAisCoordC(r.s(93, 17), kLatNoDisponibleC, 90.0);
        m.stationType = int(r.u(110, 4));
        m.shipType = int(r.u(114, 8));
        m.txRxMode = int(r.u(144, 2));
        m.reportInterval = int(r.u(146, 4));
        m.quietTime = int(r.u(150, 4));
        break;
    }
    case 24: {
        if (r.size() < 160)
            return false;
        m.supported = true;
        m.classB = true;
        m.partNo = int(r.u(38, 2));
        if (m.partNo == 0) {
            m.name = r.text(40, 20);
        } else if (m.partNo == 1) {
            m.shipType = int(r.u(40, 8));
            m.callSign = r.text(90, 7);
            m.toBowM = int(r.u(132, 9));
            m.toSternM = int(r.u(141, 9));
            m.toPortM = int(r.u(150, 6));
            m.toStarboardM = int(r.u(156, 6));
        } else {
            m.supported = false;
        }
        break;
    }
    case 25: case 26: {
        // Binario de un intervalo (25) o de varios (26): direccionado y/o con
        // DAC/FID opcionales; el 26 reserva los ultimos 20 bits para el estado
        // de comunicaciones, que no forma parte de la carga.
        if (r.size() < (m.type == 25 ? 40 : 60))
            return false;
        m.supported = true;
        m.addressed = r.u(38, 1) != 0;
        m.structured = r.u(39, 1) != 0;
        int pos = 40;
        if (m.addressed) {
            if (r.size() < 70)
                return false;
            m.destMmsi = r.u(40, 30);
            pos = 70;
        }
        if (m.structured) {
            if (r.size() < pos + 16)
                return false;
            m.dac = int(r.u(pos, 10));
            m.fid = int(r.u(pos + 10, 6));
            pos += 16;
        }
        const int fin = m.type == 26 ? r.size() - 20 : r.size();
        m.binaryBits = std::max(0, fin - pos);
        m.binaryData = r.bytes(pos, m.binaryBits);
        break;
    }
    case 27: {
        // Largo alcance: posicion basto (1/10 de minuto), velocidad en nudos
        // enteros (63 = no disponible) y rumbo en grados enteros (511 = no).
        if (r.size() < 96)
            return false;
        m.supported = true;
        m.accuracy = r.u(38, 1) != 0;
        m.navStatus = int(r.u(40, 4));
        m.lon = fromAisCoordC(r.s(44, 18), kLonNoDisponibleC, 180.0);
        m.lat = fromAisCoordC(r.s(62, 17), kLatNoDisponibleC, 90.0);
        const quint32 sog = r.u(79, 6);
        m.sogKn = sog >= 63 ? notAvailable() : double(sog);
        const quint32 cog = r.u(85, 9);
        m.cogDeg = cog >= 511 ? notAvailable() : double(cog);
        m.gnss = r.u(94, 1) != 0;
        break;
    }
    default:
        break;
    }
    *out = m;
    return true;
}

// Escribe el mensaje a bits y lo reparte en frases de a lo sumo 60 caracteres de carga.
QList<QByteArray> encodeAis(const AisMessage &m, char channel, int sequenceId, bool ownShip)
{
    BitWriter w;
    const int type = m.type;
    w.u(quint32(type), 6);
    w.u(quint32(m.repeat), 2);
    w.u(m.mmsi, 30);
    const auto sog = [&] { return std::isfinite(m.sogKn) ? quint32(std::lround(m.sogKn * 10.0)) : 1023u; };
    const auto cog = [&] { return std::isfinite(m.cogDeg) ? quint32(std::lround(m.cogDeg * 10.0)) : 3600u; };
    const auto hdg = [&] { return m.headingDeg >= 0 ? quint32(m.headingDeg) : 511u; };
    switch (type) {
    case 1: case 2: case 3:
        w.u(quint32(m.navStatus >= 0 ? m.navStatus : 15), 4);
        w.s(-128, 8);                                    // velocidad de giro: no disponible
        w.u(std::min(sog(), 1023u), 10);
        w.u(m.accuracy ? 1u : 0u, 1);
        w.s(toAisCoord(m.lon, kLonNoDisponible), 28);
        w.s(toAisCoord(m.lat, kLatNoDisponible), 27);
        w.u(std::min(cog(), 3600u), 12);
        w.u(hdg(), 9);
        w.u(quint32(m.second), 6);
        w.u(0, 2 + 3 + 1 + 19);                          // maniobra, reservado, RAIM y radio
        break;
    case 5:
        w.u(0, 2);                                       // version AIS
        w.u(m.imo, 30);
        w.text(m.callSign, 7);
        w.text(m.name, 20);
        w.u(quint32(m.shipType), 8);
        w.u(quint32(m.toBowM), 9);
        w.u(quint32(m.toSternM), 9);
        w.u(quint32(m.toPortM), 6);
        w.u(quint32(m.toStarboardM), 6);
        w.u(0, 4);                                       // tipo de receptor de posicion
        w.u(quint32(m.etaMonth), 4);
        w.u(quint32(m.etaDay), 5);
        w.u(quint32(m.etaHour), 5);
        w.u(quint32(m.etaMinute), 6);
        w.u(std::isfinite(m.draughtM) ? quint32(std::lround(m.draughtM * 10.0)) : 0u, 8);
        w.text(m.destination, 20);
        w.u(0, 1 + 1);                                   // DTE y reservado
        break;
    case 18:
    case 19:
        w.u(0, 8);                                       // reservado
        w.u(std::min(sog(), 1023u), 10);
        w.u(m.accuracy ? 1u : 0u, 1);
        w.s(toAisCoord(m.lon, kLonNoDisponible), 28);
        w.s(toAisCoord(m.lat, kLatNoDisponible), 27);
        w.u(std::min(cog(), 3600u), 12);
        w.u(hdg(), 9);
        w.u(quint32(m.second), 6);
        if (type == 18) {
            w.u(0, 2 + 7 + 20);                          // reservado, indicadores y radio
        } else {
            w.u(0, 4);                                   // regional
            w.text(m.name, 20);
            w.u(quint32(m.shipType), 8);
            w.u(quint32(m.toBowM), 9);
            w.u(quint32(m.toSternM), 9);
            w.u(quint32(m.toPortM), 6);
            w.u(quint32(m.toStarboardM), 6);
            w.u(0, 4 + 1 + 1 + 1 + 4);                   // receptor, RAIM, DTE, asignado y reservado
        }
        break;
    case 24:
        if (m.partNo == 0) {
            w.u(0, 2);
            w.text(m.name, 20);
            w.u(0, 8);                                   // reservado
        } else if (m.partNo == 1) {
            w.u(1, 2);
            w.u(quint32(m.shipType), 8);
            w.u(0, 42);                                  // fabricante, modelo y serie
            w.text(m.callSign, 7);
            w.u(quint32(m.toBowM), 9);
            w.u(quint32(m.toSternM), 9);
            w.u(quint32(m.toPortM), 6);
            w.u(quint32(m.toStarboardM), 6);
            w.u(0, 6);                                   // reservado
        } else {
            return {};
        }
        break;
    case 4: case 11:
        w.u(quint32(m.utcYear), 14);
        w.u(quint32(m.utcMonth), 4);
        w.u(quint32(m.utcDay), 5);
        w.u(quint32(m.utcHour), 5);
        w.u(quint32(m.utcMinute), 6);
        w.u(quint32(m.utcSecond), 6);
        w.u(m.accuracy ? 1u : 0u, 1);
        w.s(toAisCoord(m.lon, kLonNoDisponible), 28);
        w.s(toAisCoord(m.lat, kLatNoDisponible), 27);
        w.u(quint32(m.epfsType), 4);
        w.u(0, 10 + 1 + 19);                             // repuesto, RAIM y radio
        break;
    case 6:
        w.u(quint32(m.sequence), 2);
        w.u(m.destMmsi, 30);
        w.u(m.retransmit ? 1u : 0u, 1);
        w.u(0, 1);                                       // repuesto
        w.u(quint32(m.dac < 0 ? 0 : m.dac), 10);
        w.u(quint32(m.fid < 0 ? 0 : m.fid), 6);
        w.raw(m.binaryData, m.binaryBits);
        break;
    case 7: case 13:
        w.u(0, 2);                                       // repuesto
        for (int i = 0; i < m.ackMmsi.size() && i < 4; ++i) {
            w.u(m.ackMmsi.at(i), 30);
            w.u(quint32(i < m.ackSequence.size() ? m.ackSequence.at(i) : 0), 2);
        }
        break;
    case 8:
        w.u(0, 2);                                       // repuesto
        w.u(quint32(m.dac < 0 ? 0 : m.dac), 10);
        w.u(quint32(m.fid < 0 ? 0 : m.fid), 6);
        w.raw(m.binaryData, m.binaryBits);
        break;
    case 9:
        w.u(std::isfinite(m.altitudeM) ? quint32(std::lround(m.altitudeM)) : 4095u, 12);
        w.u(std::isfinite(m.sogKn) ? quint32(std::lround(m.sogKn)) : 1023u, 10);   // nudos enteros
        w.u(m.accuracy ? 1u : 0u, 1);
        w.s(toAisCoord(m.lon, kLonNoDisponible), 28);
        w.s(toAisCoord(m.lat, kLatNoDisponible), 27);
        w.u(std::min(cog(), 3600u), 12);
        w.u(quint32(m.second), 6);
        w.u(0, 8 + 1 + 3);                               // regional, DTE y repuesto
        w.u(m.assigned ? 1u : 0u, 1);
        w.u(0, 1 + 20);                                  // RAIM y radio
        break;
    case 10:
        w.u(0, 2);                                       // repuesto
        w.u(m.destMmsi, 30);
        w.u(0, 2);                                       // repuesto
        break;
    case 12:
        w.u(quint32(m.sequence), 2);
        w.u(m.destMmsi, 30);
        w.u(m.retransmit ? 1u : 0u, 1);
        w.u(0, 1);                                       // repuesto
        w.text(m.text, int(m.text.size()));
        break;
    case 14:
        w.u(0, 2);                                       // repuesto
        w.text(m.text, int(m.text.size()));
        break;
    case 15: {
        w.u(0, 2);                                       // repuesto
        const bool dos = m.interrogations.size() > 1;
        for (int i = 0; i < m.interrogations.size() && i < 2; ++i) {
            const AisMessage::Interrogation &q = m.interrogations.at(i);
            w.u(q.mmsi, 30);
            w.u(quint32(q.type1), 6);
            w.u(quint32(q.offset1), 12);
            w.u(0, 2);                                   // repuesto
            if (i == 0 && (q.type2 >= 0 || dos)) {
                w.u(quint32(q.type2 >= 0 ? q.type2 : 0), 6);
                w.u(quint32(q.type2 >= 0 ? q.offset2 : 0), 12);
                w.u(0, 2);                               // repuesto
            }
        }
        break;
    }
    case 16:
        w.u(0, 2);                                       // repuesto
        for (int i = 0; i < m.assignments.size() && i < 2; ++i) {
            const AisMessage::Assignment &a = m.assignments.at(i);
            w.u(a.mmsi, 30);
            w.u(quint32(a.offset), 12);
            w.u(quint32(a.increment), 10);
        }
        w.u(0, 4);                                       // repuesto final
        break;
    case 17:
        w.u(0, 2);                                       // repuesto
        w.s(toAisCoordC(m.dgnssLon, kLonNoDisponibleC), 18);
        w.s(toAisCoordC(m.dgnssLat, kLatNoDisponibleC), 17);
        w.u(0, 5);                                       // repuesto
        w.raw(m.binaryData, m.binaryBits);
        break;
    case 20:
        w.u(0, 2);                                       // repuesto
        for (int i = 0; i < m.reservations.size() && i < 4; ++i) {
            const AisMessage::Reservation &v = m.reservations.at(i);
            w.u(quint32(v.offset), 12);
            w.u(quint32(v.slotCount), 4);
            w.u(quint32(v.timeout), 3);
            w.u(quint32(v.increment), 11);
        }
        w.u(0, 2);                                       // repuesto: el mensaje minimo son 72 bits
        break;
    case 21:
        w.u(quint32(m.atonType), 5);
        w.text(m.name.left(20), 20);
        w.u(m.accuracy ? 1u : 0u, 1);
        w.s(toAisCoord(m.lon, kLonNoDisponible), 28);
        w.s(toAisCoord(m.lat, kLatNoDisponible), 27);
        w.u(quint32(m.toBowM), 9);
        w.u(quint32(m.toSternM), 9);
        w.u(quint32(m.toPortM), 6);
        w.u(quint32(m.toStarboardM), 6);
        w.u(quint32(m.epfsType), 4);
        w.u(quint32(m.second), 6);
        w.u(m.offPosition ? 1u : 0u, 1);
        w.u(0, 8);                                       // regional
        w.u(0, 1);                                       // RAIM
        w.u(m.virtualAton ? 1u : 0u, 1);
        w.u(m.assigned ? 1u : 0u, 1);
        w.u(0, 1);                                       // repuesto
        if (m.name.size() > 20)
            w.text(m.name.mid(20), int(m.name.size()) - 20);
        break;
    case 22:
        w.u(0, 2);                                       // repuesto
        w.u(quint32(m.channelA), 12);
        w.u(quint32(m.channelB), 12);
        w.u(quint32(m.txRxMode), 4);
        w.u(m.lowPower ? 1u : 0u, 1);
        if (m.addressed) {
            w.u(m.destMmsi, 30);
            w.u(0, 5);                                   // repuesto
            w.u(m.destMmsi2, 30);
            w.u(0, 5);                                   // repuesto
        } else {
            w.s(toAisCoordC(m.neLon, kLonNoDisponibleC), 18);
            w.s(toAisCoordC(m.neLat, kLatNoDisponibleC), 17);
            w.s(toAisCoordC(m.swLon, kLonNoDisponibleC), 18);
            w.s(toAisCoordC(m.swLat, kLatNoDisponibleC), 17);
        }
        w.u(m.addressed ? 1u : 0u, 1);
        w.u(m.bandChannelA ? 1u : 0u, 1);
        w.u(m.bandChannelB ? 1u : 0u, 1);
        w.u(quint32(m.zoneSize), 3);
        w.u(0, 23);                                      // repuesto
        break;
    case 23:
        w.u(0, 2);                                       // repuesto
        w.s(toAisCoordC(m.neLon, kLonNoDisponibleC), 18);
        w.s(toAisCoordC(m.neLat, kLatNoDisponibleC), 17);
        w.s(toAisCoordC(m.swLon, kLonNoDisponibleC), 18);
        w.s(toAisCoordC(m.swLat, kLatNoDisponibleC), 17);
        w.u(quint32(m.stationType), 4);
        w.u(quint32(m.shipType), 8);
        w.u(0, 22);                                      // repuesto
        w.u(quint32(m.txRxMode), 2);
        w.u(quint32(m.reportInterval), 4);
        w.u(quint32(m.quietTime), 4);
        w.u(0, 6);                                       // repuesto
        break;
    case 25: case 26:
        w.u(m.addressed ? 1u : 0u, 1);
        w.u(m.structured ? 1u : 0u, 1);
        if (m.addressed)
            w.u(m.destMmsi, 30);
        if (m.structured) {
            w.u(quint32(m.dac < 0 ? 0 : m.dac), 10);
            w.u(quint32(m.fid < 0 ? 0 : m.fid), 6);
        }
        w.raw(m.binaryData, m.binaryBits);
        if (type == 26)
            w.u(0, 20);                                  // estado de comunicaciones (no se guarda)
        break;
    case 27:
        w.u(m.accuracy ? 1u : 0u, 1);
        w.u(0, 1);                                       // RAIM
        w.u(quint32(m.navStatus >= 0 ? m.navStatus : 15), 4);
        w.s(toAisCoordC(m.lon, kLonNoDisponibleC), 18);
        w.s(toAisCoordC(m.lat, kLatNoDisponibleC), 17);
        w.u(std::isfinite(m.sogKn) ? quint32(std::lround(m.sogKn)) : 63u, 6);      // nudos enteros
        w.u(std::isfinite(m.cogDeg) ? quint32(std::lround(m.cogDeg)) : 511u, 9);   // grados enteros
        w.u(m.gnss ? 1u : 0u, 1);
        w.u(0, 1);                                       // repuesto
        break;
    default:
        return {};
    }
    int fill = 0;
    const QByteArray payload = w.payload(&fill);
    const QByteArray address = ownShip ? "AIVDO" : "AIVDM";
    const int fragments = (int(payload.size()) + kCargaPorFrase - 1) / kCargaPorFrase;
    QList<QByteArray> out;
    for (int i = 0; i < fragments; ++i) {
        const bool last = i == fragments - 1;
        out.append(formatSentence(address,
                                  { QByteArray::number(fragments), QByteArray::number(i + 1),
                                    fragments > 1 ? QByteArray::number(sequenceId) : QByteArray(),
                                    QByteArray(1, channel), payload.mid(i * kCargaPorFrase, kCargaPorFrase),
                                    QByteArray::number(last ? fill : 0) },
                                  true));
    }
    return out;
}

// Junta los fragmentos de un mismo mensaje; devuelve la carga completa con la ultima frase.
bool AisAssembler::add(const Sentence &s, QByteArray *payload, int *fillBits)
{
    if (!s.valid || (s.type != "VDM" && s.type != "VDO") || s.fields.size() < 6)
        return false;
    bool okTotal = false, okNum = false, okFill = false;
    const int total = s.fields.at(0).toInt(&okTotal);
    const int num = s.fields.at(1).toInt(&okNum);
    const int fill = s.fields.at(5).toInt(&okFill);
    if (!okTotal || !okNum || !okFill || total < 1 || total > 9 || num < 1 || num > total)
        return false;
    const QByteArray &data = s.fields.at(4);
    if (total == 1) {
        *payload = data;
        *fillBits = fill;
        return true;
    }
    const QByteArray key = s.fields.at(2) + '/' + s.fields.at(3);
    if (num == 1) {
        if (m_pending.size() >= 32)
            m_pending.clear();                           // memoria acotada ante una fuente que no termina mensajes
        if (m_pending.contains(key))
            ++m_lost;                                    // el anterior con esta clave quedo a medias
        m_pending.insert(key, Partial{ total, 2, data });
        return false;
    }
    auto it = m_pending.find(key);
    if (it == m_pending.end() || it->total != total || it->next != num) {
        if (it != m_pending.end())
            m_pending.erase(it);
        ++m_lost;
        return false;
    }
    it->payload += data;
    if (num < total) {
        ++it->next;
        return false;
    }
    *payload = it->payload;
    *fillBits = fill;
    m_pending.erase(it);
    return true;
}

// Olvida lo que estaba a medias.
void AisAssembler::reset()
{
    m_pending.clear();
    m_lost = 0;
}

// Tipo de barco segun la tabla de AIS (decena = familia, unidad = carga).
QString aisShipTypeText(int t)
{
    if (t <= 0 || t > 99)
        return QStringLiteral("No disponible");
    if (t >= 20 && t <= 28)
        return QStringLiteral("Ala en efecto suelo");
    switch (t) {
    case 30: return QStringLiteral("Pesquero");
    case 31: case 32: return QStringLiteral("Remolcador");
    case 33: return QStringLiteral("Dragado o submarino");
    case 34: return QStringLiteral("Buceo");
    case 35: return QStringLiteral("Militar");
    case 36: return QStringLiteral("Vela");
    case 37: return QStringLiteral("Recreo");
    case 50: return QStringLiteral("Practico");
    case 51: return QStringLiteral("Busqueda y rescate");
    case 52: return QStringLiteral("Remolcador");
    case 53: return QStringLiteral("Embarcacion de puerto");
    case 54: return QStringLiteral("Antipolucion");
    case 55: return QStringLiteral("Autoridad");
    case 58: return QStringLiteral("Sanitario");
    case 59: return QStringLiteral("Otro servicio");
    default: break;
    }
    switch (t / 10) {
    case 4: return QStringLiteral("Alta velocidad");
    case 6: return QStringLiteral("Pasaje");
    case 7: return QStringLiteral("Carguero");
    case 8: return QStringLiteral("Petrolero");
    case 9: return QStringLiteral("Otro");
    default: return QStringLiteral("Otro (%1)").arg(t);
    }
}

// Tipo de ayuda a la navegacion del mensaje 21 (tabla de ITU-R M.1371).
QString aisAtonTypeText(int t)
{
    switch (t) {
    case 0: return QStringLiteral("No especificado");
    case 1: return QStringLiteral("Punto de referencia");
    case 2: return QStringLiteral("RACON");
    case 3: return QStringLiteral("Estructura fija costa afuera");
    case 4: return QStringLiteral("Boya de naufragio de emergencia");
    case 5: return QStringLiteral("Luz sin sector");
    case 6: return QStringLiteral("Luz con sector");
    case 7: return QStringLiteral("Luz de enfilacion anterior");
    case 8: return QStringLiteral("Luz de enfilacion posterior");
    case 9: return QStringLiteral("Baliza cardinal N");
    case 10: return QStringLiteral("Baliza cardinal E");
    case 11: return QStringLiteral("Baliza cardinal S");
    case 12: return QStringLiteral("Baliza cardinal O");
    case 13: return QStringLiteral("Baliza de babor");
    case 14: return QStringLiteral("Baliza de estribor");
    case 15: return QStringLiteral("Baliza de canal preferido a babor");
    case 16: return QStringLiteral("Baliza de canal preferido a estribor");
    case 17: return QStringLiteral("Baliza de peligro aislado");
    case 18: return QStringLiteral("Baliza de aguas seguras");
    case 19: return QStringLiteral("Baliza de marca especial");
    case 20: return QStringLiteral("Flotante cardinal N");
    case 21: return QStringLiteral("Flotante cardinal E");
    case 22: return QStringLiteral("Flotante cardinal S");
    case 23: return QStringLiteral("Flotante cardinal O");
    case 24: return QStringLiteral("Flotante de babor");
    case 25: return QStringLiteral("Flotante de estribor");
    case 26: return QStringLiteral("Flotante de canal preferido a babor");
    case 27: return QStringLiteral("Flotante de canal preferido a estribor");
    case 28: return QStringLiteral("Flotante de peligro aislado");
    case 29: return QStringLiteral("Flotante de aguas seguras");
    case 30: return QStringLiteral("Flotante de marca especial");
    case 31: return QStringLiteral("Buque faro");
    default: return QStringLiteral("Desconocido");
    }
}

// Fijador de posicion electronico (EPFS) de los tipos 4, 11, 19 y 21.
QString aisEpfsTypeText(int t)
{
    switch (t) {
    case 0: return QStringLiteral("No definido");
    case 1: return QStringLiteral("GPS");
    case 2: return QStringLiteral("GLONASS");
    case 3: return QStringLiteral("GPS y GLONASS");
    case 4: return QStringLiteral("Loran-C");
    case 5: return QStringLiteral("Chayka");
    case 6: return QStringLiteral("Navegacion integrada");
    case 7: return QStringLiteral("Topografiado");
    case 8: return QStringLiteral("Galileo");
    case 15: return QStringLiteral("No usado");
    default: return QStringLiteral("Interno");
    }
}

// Estado de navegacion de AIS (0-15).
QString aisNavStatusText(int s)
{
    switch (s) {
    case 0: return QStringLiteral("Navegando a motor");
    case 1: return QStringLiteral("Fondeado");
    case 2: return QStringLiteral("Sin gobierno");
    case 3: return QStringLiteral("Maniobra restringida");
    case 4: return QStringLiteral("Condicionado por su calado");
    case 5: return QStringLiteral("Amarrado");
    case 6: return QStringLiteral("Encallado");
    case 7: return QStringLiteral("Pescando");
    case 8: return QStringLiteral("Navegando a vela");
    case 15: return QStringLiteral("No definido");
    default: return QStringLiteral("Reservado");
    }
}

} // namespace telemetria
