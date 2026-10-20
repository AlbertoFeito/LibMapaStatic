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
 *  - 5        datos estaticos y del viaje de clase A (nombre, destino, tamano...).
 *  - 18, 19   informe de posicion de clase B (19, ampliado: con nombre y tamano).
 *  - 24       datos estaticos de clase B, en dos partes (A: nombre; B: tipo, indicativo, tamano).
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

    // Estaticos (5, 19, 24).
    QString name;
    QString callSign;
    QString destination;
    int shipType = 0;                   //!< 0 = no disponible.
    quint32 imo = 0;
    int toBowM = 0, toSternM = 0, toPortM = 0, toStarboardM = 0;
    double draughtM = notAvailable();
    int etaMonth = 0, etaDay = 0, etaHour = 24, etaMinute = 60;
    int partNo = -1;                    //!< Parte del tipo 24 (0 = A, 1 = B), -1 en los demas.

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
 * Soporta los tipos 1, 2, 3, 5, 18, 19 y 24 (parte A o B segun \c partNo). Una
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

} // namespace telemetria

#endif // TELEMETRIA_AIS_H_
