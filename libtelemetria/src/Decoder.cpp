#include "telemetria/Decoder.h"

namespace telemetria {

namespace {
// Lo mas que se guarda de una linea a medias: una frase NMEA no pasa de 82 caracteres, y si el flujo trae
// kilobytes sin un salto de linea no es NMEA y no hay que acumularlo sin limite.
const int kLineaMaxima = 4096;
} // namespace

// Decodifica una linea y devuelve su evento.
QVector<Event> Decoder::feedLine(const QByteArray &line)
{
    QVector<Event> out;
    if (line.trimmed().isEmpty())
        return out;
    ++m_stats.lines;
    Event e;
    e.raw = line.trimmed();
    const Sentence s = parseSentence(line);
    if (!s.valid) {
        e.kind = Event::Kind::Bad;
        e.detail = s.error;
        if (s.checksumPresent && !s.checksumOk)
            ++m_stats.badChecksum;
        else
            ++m_stats.malformed;
        out.append(e);
        return out;
    }

    if (s.type == "RMC" && decodeRmc(s, &e.rmc)) {
        e.kind = Event::Kind::Rmc;
    } else if (s.type == "GGA" && decodeGga(s, &e.gga)) {
        e.kind = Event::Kind::Gga;
    } else if (s.type == "VTG" && decodeVtg(s, &e.vtg)) {
        e.kind = Event::Kind::Vtg;
    } else if (s.type == "HDT" && decodeHdt(s, &e.hdt)) {
        e.kind = Event::Kind::Hdt;
    } else if (s.type == "VDM" || s.type == "VDO") {
        const quint64 antes = m_assembler.lostFragments();
        QByteArray payload;
        int fill = 0;
        const bool completo = m_assembler.add(s, &payload, &fill);
        m_stats.aisLost += m_assembler.lostFragments() - antes;
        if (!completo)
            return out;                                  // a la espera de los demas fragmentos (o descartado)
        if (!decodeAis(payload, fill, &e.ais)) {
            e.kind = Event::Kind::Bad;
            e.detail = QStringLiteral("carga AIS incorrecta o demasiado corta");
            ++m_stats.malformed;
            out.append(e);
            return out;
        }
        e.kind = Event::Kind::Ais;
        e.ownShip = s.type == "VDO";
        ++m_stats.aisMessages;
        if (!e.ais.supported) {
            ++m_stats.aisUnsupported;
            e.kind = Event::Kind::Ignored;
            e.detail = QStringLiteral("AIS tipo %1").arg(e.ais.type);
            ++m_stats.ignored;
            out.append(e);
            return out;
        }
    } else {
        e.kind = Event::Kind::Ignored;
        e.detail = QString::fromLatin1(s.address);
        ++m_stats.ignored;
        out.append(e);
        return out;
    }
    ++m_stats.decoded;
    out.append(e);
    return out;
}

// Parte \a bytes en lineas; lo que queda sin salto de linea se guarda (flujo) o se procesa (datagrama).
QVector<Event> Decoder::split(const QByteArray &bytes, bool flushTail)
{
    QVector<Event> out;
    m_tail += bytes;
    int start = 0;
    for (int i = 0; i < m_tail.size(); ++i) {
        if (m_tail.at(i) == '\n' || m_tail.at(i) == '\r') {
            out += feedLine(m_tail.mid(start, i - start));
            start = i + 1;
        }
    }
    m_tail.remove(0, start);
    if (flushTail || m_tail.size() > kLineaMaxima) {
        if (!m_tail.isEmpty())
            out += feedLine(m_tail);
        m_tail.clear();
    }
    return out;
}

// Flujo: la ultima linea sin terminar espera al siguiente trozo.
QVector<Event> Decoder::feed(const QByteArray &bytes)
{
    return split(bytes, false);
}

// Datagrama UDP: todo lo que traiga es de lineas completas.
QVector<Event> Decoder::feedDatagram(const QByteArray &bytes)
{
    return split(bytes, true);
}

// Contadores a cero y sin fragmentos ni linea a medias.
void Decoder::reset()
{
    m_stats = DecoderStats();
    m_assembler.reset();
    m_tail.clear();
}

} // namespace telemetria
