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
