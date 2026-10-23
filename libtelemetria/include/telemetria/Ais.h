#ifndef TELEMETRIA_AIS_H_
#define TELEMETRIA_AIS_H_

#include "telemetria/Nmea.h"
#include "telemetria/telemetria_export.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <cmath>

namespace telemetria {

/*!
 * \brief Un mensaje AIS decodificado (ITU-R M.1371).
 *
 * Una sola estructura para todos los tipos que se entienden; cada tipo rellena
 * lo suyo y deja el resto en su valor de "no disponible":
 *  - 1, 2, 3  informe de posicion de clase A (barcos grandes).
 *  - 4, 11    estacion base / respuesta de fecha y hora UTC (misma estructura).
 *  - 5        datos estaticos y del viaje de clase A (nombre, destino, tamano...).
 *  - 6, 8     binario direccionado / difundido (la carga de aplicacion, sin decodificar).
 *  - 7, 13    acuse de recibo binario / de mensaje de seguridad.
 *  - 9        informe de posicion de aeronave de busqueda y rescate (SAR).
 *  - 10       peticion de fecha y hora UTC.
 *  - 12, 14   texto de seguridad direccionado / difundido.
 *  - 15       interrogacion.  16  asignacion de modo.  17  correcciones DGNSS.
 *  - 18, 19   informe de posicion de clase B (19, ampliado: con nombre y tamano).
 *  - 20       gestion del enlace de datos (reservas de intervalos).
 *  - 21       ayuda a la navegacion (AtoN): tipo, nombre, posicion y dimensiones.
 *  - 22       gestion de canales.  23  asignacion a un grupo.
 *  - 24       datos estaticos de clase B, en dos partes (A: nombre; B: tipo, indicativo, tamano).
 *  - 25, 26   binario de un intervalo / de varios (con estado de comunicaciones).
 *  - 27       informe de posicion de largo alcance (resolucion de 1/10 de minuto).
 * Cualquier otro tipo se devuelve con \c supported = false y solo el tipo y el MMSI.
 */
struct AisMessage
{
    int type = 0;
    int repeat = 0;
    quint32 mmsi = 0;
    bool supported = false;
    bool classB = false;

    // Dinamicos (1, 2, 3, 18, 19).
    double lat = notAvailable();        //!< Grados, + norte; NaN si el barco no la da.
    double lon = notAvailable();
    double sogKn = notAvailable();      //!< Velocidad sobre el fondo, nudos.
    double cogDeg = notAvailable();     //!< Rumbo sobre el fondo, grados.
    int headingDeg = -1;                //!< Rumbo de la proa, grados; -1 si no lo da.
    int navStatus = -1;                 //!< Estado de navegacion (clase A), -1 si no hay.
    int second = 60;                    //!< Segundo UTC del informe; 60 = no disponible.
    bool accuracy = false;              //!< Posicion de alta precision (< 10 m).

    // Estaticos (5, 19, 24) y nombre de la ayuda (21: los 20 caracteres mas la
    // extension, si el mensaje la trae, unidos en un solo nombre).
    QString name;
    QString callSign;
    QString destination;
    int shipType = 0;                   //!< 0 = no disponible.
    quint32 imo = 0;
    int toBowM = 0, toSternM = 0, toPortM = 0, toStarboardM = 0;
    double draughtM = notAvailable();
    int etaMonth = 0, etaDay = 0, etaHour = 24, etaMinute = 60;
    int partNo = -1;                    //!< Parte del tipo 24 (0 = A, 1 = B), -1 en los demas.

    // Fecha y hora UTC completas (4 y 11, que comparten estructura). La posicion
    // va en lat/lon y la precision en accuracy, como en los informes de posicion.
    int utcYear = 0, utcMonth = 0, utcDay = 0;
    int utcHour = 24, utcMinute = 60, utcSecond = 60;   //!< 24/60 = no disponible, como la ETA.
    int epfsType = 0;                   //!< Fijador de posicion (0 = no definido); tipos 4, 11, 19 y 21.

    // Aeronave SAR (9): posicion en lat/lon, velocidad en sogKn (en nudos enteros,
    // no en decimas como en los demas informes) y rumbo en cogDeg; sin proa.
    double altitudeM = notAvailable();  //!< Metros; NaN si la aeronave no la da.

    // Ayuda a la navegacion (21). La posicion va en lat/lon, las dimensiones en
    // toBowM/toSternM/toPortM/toStarboardM y el segundo UTC en second, como en 5/19.
    int atonType = 0;                   //!< Tipo de ayuda (0 = no especificado).
    bool offPosition = false;           //!< La ayuda esta fuera de su posicion anunciada.
    bool virtualAton = false;           //!< Ayuda virtual (no existe fisicamente).
    bool assigned = false;              //!< Modo asignado (9, 19 y 21).

    // Texto de seguridad (12 direccionado y 14 difundido).
    QString text;

    // Direccionados (6, 10, 12 y, con addressed, 22, 25 y 26).
    quint32 destMmsi = 0;               //!< MMSI de destino; 0 si no es direccionado.
    quint32 destMmsi2 = 0;              //!< Segundo destino (solo el 22 direccionado).
    int sequence = 0;                   //!< Numero de secuencia (6 y 12).
    bool retransmit = false;            //!< Indicador de retransmision (6 y 12).
    bool addressed = false;             //!< El mensaje va dirigido a MMSI concretos (22, 25, 26).
    bool structured = false;            //!< Lleva DAC/FID delante de los datos (25 y 26).

    // Binarios (6, 8, 17, 25 y 26): la carga de aplicacion TAL CUAL, sin
    // decodificar: que significa la decide la aplicacion que digan dac/fid.
    int dac = -1, fid = -1;             //!< Identificador de aplicacion; -1 si el tipo no lo trae.
    QByteArray binaryData;              //!< Bytes de la carga (el ultimo se rellena con ceros si binaryBits no es multiplo de 8).
    int binaryBits = 0;                 //!< Bits utiles de binaryData.

    // Acuses (7 y 13): hasta cuatro parejas (MMSI acusado, numero de secuencia).
    QVector<quint32> ackMmsi;
    QVector<int> ackSequence;

    //! Una estacion interrogada (15): se le pide un tipo de mensaje (y otro, si type2 no es -1).
    struct Interrogation {
        quint32 mmsi = 0;
        int type1 = 0, offset1 = 0;
        int type2 = -1, offset2 = 0;
    };
    QVector<Interrogation> interrogations;

    //! Una asignacion de modo (16): intervalo y desplazamiento para un MMSI.
    struct Assignment {
        quint32 mmsi = 0;
        int offset = 0, increment = 0;
    };
    QVector<Assignment> assignments;

    //! Una reserva del enlace de datos (20).
    struct Reservation {
        int offset = 0, slotCount = 0, timeout = 0, increment = 0;
    };
    QVector<Reservation> reservations;

    // DGNSS (17): posicion de la ESTACION de referencia (no es la del barco que
    // emite: por eso no va en lat/lon y hasPosition() no la cuenta).
    double dgnssLat = notAvailable(), dgnssLon = notAvailable();

    // Area rectangular (22 y 23): esquinas noreste y suroeste, en grados con la
    // resolucion de 1/10 de minuto propia de estos tipos.
    double neLat = notAvailable(), neLon = notAvailable();
    double swLat = notAvailable(), swLon = notAvailable();

    // Gestion de canales (22) y asignacion a un grupo (23).
    int channelA = 0, channelB = 0;
    int txRxMode = 0;                   //!< Modo Tx/Rx (22 y 23).
    bool lowPower = false;              //!< Potencia baja (22).
    bool bandChannelA = false, bandChannelB = false;   //!< Banda completa o 12,5 kHz (22).
    int zoneSize = 0;                   //!< Tamano de la zona de transicion (22).
    int stationType = 0;                //!< Tipo de estacion a la que se aplica (23).
    int reportInterval = 0;             //!< Intervalo de informe impuesto (23).
    int quietTime = 0;                  //!< Minutos de silencio impuestos (23).

    bool gnss = false;                  //!< La posicion del 27 viene de un GNSS (si no, de Loran-C).

    //! ¿Trae una posicion utilizable?
    bool hasPosition() const { return std::isfinite(lat) && std::isfinite(lon); }
    //! Eslora y manga, en metros (0 si no se declaran).
    int lengthM() const { return toBowM + toSternM; }
    int beamM() const { return toPortM + toStarboardM; }
};

/*!
 * \brief Decodifica la carga util de un mensaje AIS (el sexto campo de !AIVDM,
 *        ya reensamblada) con sus \a fillBits de relleno.
 *
 * \c false si la carga tiene caracteres que no son del alfabeto de 6 bits o es
 * mas corta de lo que exige su tipo. Un tipo no soportado devuelve \c true con
 * \c supported = false.
 */
TELEMETRIA_EXPORT bool decodeAis(const QByteArray &payload, int fillBits, AisMessage *out);

/*!
 * \brief Codifica \a m como una o varias frases `!AIVDM` (`!AIVDO` si \a ownShip).
 *
 * Soporta los tipos 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
 * 18, 19, 20, 21, 22, 23, 24 (parte A o B segun \c partNo), 25, 26 y 27. Una
 * carga de mas de 60 caracteres (el tipo 5) se parte en dos frases con el mismo
 * \a sequenceId. Lista vacia si el tipo no se sabe codificar.
 */
TELEMETRIA_EXPORT QList<QByteArray> encodeAis(const AisMessage &m, char channel = 'A', int sequenceId = 1,
                                              bool ownShip = false);

/*!
 * \brief Reensambla los mensajes AIS partidos en varias frases.
 *
 * Se le dan las frases `VDM`/`VDO` en el orden en que llegan. Si la frase es la
 * unica o la ultima de su mensaje, \c add devuelve \c true con la carga completa.
 * Una frase fuera de orden, o a la que le falta la anterior, descarta ese mensaje
 * y se cuenta en \c lostFragments.
 */
class TELEMETRIA_EXPORT AisAssembler
{
public:
    bool add(const Sentence &s, QByteArray *payload, int *fillBits);
    void reset();
    int pending() const { return int(m_pending.size()); }
    quint64 lostFragments() const { return m_lost; }

private:
    struct Partial {
        int total = 0;
        int next = 0;
        QByteArray payload;
    };
    QHash<QByteArray, Partial> m_pending;
    quint64 m_lost = 0;
};

//! Nombre del tipo de barco (campo `shipType`) en espanol, p. ej. 70 -> "Carguero".
TELEMETRIA_EXPORT QString aisShipTypeText(int shipType);
//! Nombre del estado de navegacion (clase A) en espanol, p. ej. 5 -> "Amarrado".
TELEMETRIA_EXPORT QString aisNavStatusText(int navStatus);
//! Nombre del tipo de ayuda a la navegacion (campo `atonType` del tipo 21) en espanol.
TELEMETRIA_EXPORT QString aisAtonTypeText(int atonType);
//! Nombre del fijador de posicion (campo `epfsType`) en espanol, p. ej. 1 -> "GPS".
TELEMETRIA_EXPORT QString aisEpfsTypeText(int epfsType);

} // namespace telemetria

#endif // TELEMETRIA_AIS_H_
