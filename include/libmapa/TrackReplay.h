#ifndef LIBMAPA_TRACKREPLAY_H_
#define LIBMAPA_TRACKREPLAY_H_

#include "libmapa/VectorFormats.h"
#include "libmapa/libmapa_export.h"

#include <QDateTime>
#include <QGeoCoordinate>

#include <limits>

namespace libmapa {

/*!
 * \brief Donde estaba una pista grabada en un instante dado.
 *
 * Es la base de la reproduccion de historiales (TrackPlayer), sin widget: se
 * prueba sola.
 */
struct TrackSample
{
    //! true si en ese instante la pista existia: entre su primer y su ultimo
    //! punto con hora (ambos incluidos).
    bool active = false;
    QGeoCoordinate position;
    //! Rumbo (grados) del tramo en el que esta; NaN si no se puede saber.
    double headingDeg = std::numeric_limits<double>::quiet_NaN();
    //! Indice del ultimo punto grabado que ya se ha alcanzado (-1 si ninguno).
    int pointIndex = -1;
};

/*!
 * \brief Muestrea una pista en \a time.
 *
 * Entre dos puntos grabados interpola por la ortodromica en proporcion al
 * tiempo, y el rumbo es el de ese tramo. Los puntos sin hora o con posicion
 * invalida se ignoran; los demas deben venir en orden de tiempo (como los
 * escribe tracksToGpx). Fuera de [primero, ultimo] la pista no esta activa.
 */
LIBMAPA_EXPORT TrackSample sampleTrack(const GpxTrack &track, const QDateTime &time);

/*!
 * \brief Primer y ultimo instante con hora de un conjunto de pistas.
 *
 * false (y horas invalidas) si ninguna pista tiene puntos con hora.
 */
LIBMAPA_EXPORT bool trackTimeRange(const QVector<GpxTrack> &tracks, QDateTime *start,
                                   QDateTime *end);

} // namespace libmapa

#endif // LIBMAPA_TRACKREPLAY_H_
