#ifndef TELEMETRIA_NMEA_H_
#define TELEMETRIA_NMEA_H_

#include "telemetria/telemetria_export.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTime>

#include <limits>

namespace telemetria {

//! Valor de "dato no disponible" de los campos numericos (NaN).
inline double notAvailable() { return std::numeric_limits<double>::quiet_NaN(); }

/*!
 * \brief Suma de comprobacion de NMEA 0183: el XOR de todos los bytes del
 *        \a body, que es lo que hay entre el '$' (o '!') y el '*'.
 */
TELEMETRIA_EXPORT quint8 nmeaChecksum(const QByteArray &body);

/*!
 * \brief Una frase NMEA 0183 separada en sus campos, sin interpretar.
 *
 * `$GPRMC,123519,A,...*6A` y `!AIVDM,1,1,,B,...,0*5C` tienen la misma forma:
 * un inicio (`$` o `!`), una direccion (`GPRMC`: emisor `GP` + tipo `RMC`),
 * campos separados por comas y, opcionalmente, `*` y dos digitos hexadecimales.
 */
struct Sentence
{
    bool valid = false;            //!< Bien formada y, si trae suma, correcta.
    bool checksumPresent = false;
    bool checksumOk = false;
    bool encapsulated = false;     //!< Empieza por '!' (AIS) y no por '$'.
    QByteArray address;            //!< "GPRMC", "AIVDM", "PGRMZ"...
    QByteArray talker;             //!< "GP", "AI"... (vacio si es propietaria, 'P...').
    QByteArray type;               //!< "RMC", "VDM"... (vacio si es propietaria).
    QList<QByteArray> fields;      //!< Los campos tras la direccion, sin la suma.
    QByteArray raw;                //!< La linea tal cual llego, sin saltos de linea.
    QString error;                 //!< Por que no es valida (vacio si lo es).
};

/*!
 * \brief Separa una linea en sus campos y comprueba la suma.
 *
 * Quita los saltos de linea y los espacios de los extremos. Con
 * \a requireChecksum (por defecto) una frase SIN suma se rechaza; las muy
 * antiguas la omiten, y asi se pueden aceptar con \c false.
 */
TELEMETRIA_EXPORT Sentence parseSentence(const QByteArray &line, bool requireChecksum = true);

/*!
 * \brief Compone una frase con su suma: `$` (o `!`) + \a address + campos + `*XX`.
 *        Sin saltos de linea al final (el que la envia pone `\r\n`).
 */
TELEMETRIA_EXPORT QByteArray formatSentence(const QByteArray &address, const QList<QByteArray> &fields,
                                            bool encapsulated = false);

// -- Frases de posicion y rumbo ------------------------------------------------

/*!
 * \brief RMC: lo minimo recomendado de un GPS (hora, posicion, velocidad y rumbo).
 *
 * \c valid es el estado `A` del receptor (`V` = aviso, sin fijar). Los
 * campos que la frase deja vacios quedan en NaN / invalidos.
 */
struct Rmc
{
    bool valid = false;
    QTime time;                    //!< Hora UTC.
    QDateTime utc;                 //!< Fecha y hora UTC (invalida si falta la fecha).
    double lat = notAvailable();   //!< Grados, + norte.
    double lon = notAvailable();   //!< Grados, + este.
    double sogKn = notAvailable(); //!< Velocidad sobre el fondo, nudos.
    double cogDeg = notAvailable();//!< Rumbo sobre el fondo, grados verdaderos.
    double magVarDeg = notAvailable(); //!< Variacion magnetica, + este.
};

//! GGA: datos de fijado (calidad, satelites, hdop, altitud).
struct Gga
{
    QTime time;
    double lat = notAvailable();
    double lon = notAvailable();
    int quality = 0;               //!< 0 sin fijar, 1 GPS, 2 DGPS...
    int satellites = -1;
    double hdop = notAvailable();
    double altitudeM = notAvailable();
};

//! VTG: rumbo y velocidad sobre el fondo.
struct Vtg
{
    double cogTrueDeg = notAvailable();
    double cogMagDeg = notAvailable();
    double sogKn = notAvailable();
    double sogKmh = notAvailable();
};

//! HDT: rumbo del barco (proa), grados verdaderos.
struct Hdt
{
    double headingDeg = notAvailable();
};

//! Decodificadores: \c false si la frase no es del tipo o le faltan campos.
TELEMETRIA_EXPORT bool decodeRmc(const Sentence &s, Rmc *out);
TELEMETRIA_EXPORT bool decodeGga(const Sentence &s, Gga *out);
TELEMETRIA_EXPORT bool decodeVtg(const Sentence &s, Vtg *out);
TELEMETRIA_EXPORT bool decodeHdt(const Sentence &s, Hdt *out);

//! Codificadores (para simular un receptor y para probar los decodificadores).
TELEMETRIA_EXPORT QByteArray formatRmc(const Rmc &r, const QByteArray &talker = "GP");
TELEMETRIA_EXPORT QByteArray formatGga(const Gga &g, const QByteArray &talker = "GP");
TELEMETRIA_EXPORT QByteArray formatVtg(const Vtg &v, const QByteArray &talker = "GP");
TELEMETRIA_EXPORT QByteArray formatHdt(const Hdt &h, const QByteArray &talker = "HE");

} // namespace telemetria

#endif // TELEMETRIA_NMEA_H_
