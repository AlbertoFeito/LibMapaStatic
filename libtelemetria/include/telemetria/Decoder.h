#ifndef TELEMETRIA_DECODER_H_
#define TELEMETRIA_DECODER_H_

#include "telemetria/Ais.h"
#include "telemetria/Nmea.h"
#include "telemetria/telemetria_export.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace telemetria {

/*!
 * \brief Lo que sale de decodificar una linea: un dato, o la razon de que no lo haya.
 */
struct Event
{
    enum class Kind {
        Rmc,        //!< \c rmc
        Gga,        //!< \c gga
        Vtg,        //!< \c vtg
        Hdt,        //!< \c hdt
        Ais,        //!< \c ais (mensaje completo; \c ownShip si vino como !AIVDO)
        Bad,        //!< Frase mal formada o con la suma mal; \c detail dice por que.
        Ignored,    //!< Frase correcta de un tipo que no se decodifica; \c detail es su tipo.
    };
    Kind kind = Kind::Ignored;
    Rmc rmc;
    Gga gga;
    Vtg vtg;
    Hdt hdt;
    AisMessage ais;
    bool ownShip = false;
    QByteArray raw;
    QString detail;
};

//! Contadores de lo que ha pasado por el decodificador.
struct DecoderStats
{
    quint64 lines = 0;         //!< Lineas no vacias leidas.
    quint64 decoded = 0;       //!< Frases que dieron un dato (RMC/GGA/VTG/HDT/AIS).
    quint64 badChecksum = 0;   //!< Rechazadas por la suma de comprobacion.
    quint64 malformed = 0;     //!< Rechazadas por mal formadas (sin suma, sin campos...).
    quint64 ignored = 0;       //!< Correctas pero de un tipo que no se decodifica.
    quint64 aisMessages = 0;   //!< Mensajes AIS completos decodificados.
    quint64 aisUnsupported = 0;//!< Mensajes AIS completos de un tipo no soportado.
    quint64 aisLost = 0;       //!< Fragmentos AIS descartados (a uno le faltaba el anterior).
};

/*!
 * \brief Decodifica un flujo NMEA 0183 / AIS: lo que llega por UDP, TCP o un
 *        fichero de captura, mezclado.
 *
 * No toca la red ni el disco: se le dan bytes y devuelve eventos. Sin Qt Network,
 * sin hilos, sin senales; el que la usa decide de donde vienen los bytes.
 *
 *  - \c feed para un FLUJO (TCP, serie, fichero): guarda la linea a medias hasta
 *    que llegue el resto.
 *  - \c feedDatagram para UDP: cada datagrama trae lineas completas, aunque la
 *    ultima no termine en salto de linea.
 */
class TELEMETRIA_EXPORT Decoder
{
public:
    QVector<Event> feed(const QByteArray &bytes);
    QVector<Event> feedDatagram(const QByteArray &bytes);
    //! Una sola linea (sin salto de linea al final). Devuelve el evento, o ninguno si es una linea vacia o
    //! un fragmento AIS a la espera de los demas.
    QVector<Event> feedLine(const QByteArray &line);

    const DecoderStats &stats() const { return m_stats; }
    //! Pone los contadores a cero y olvida lo pendiente.
    void reset();

private:
    QVector<Event> split(const QByteArray &bytes, bool flushTail);

    DecoderStats m_stats;
    AisAssembler m_assembler;
    QByteArray m_tail;
};

} // namespace telemetria

#endif // TELEMETRIA_DECODER_H_
