#ifndef LIBMAPA_PROXIMITY_H_
#define LIBMAPA_PROXIMITY_H_

#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>

#include <limits>

namespace libmapa {

/*!
 * \brief Punto de maximo acercamiento (CPA) entre dos moviles.
 *
 * Suponiendo que los dos mantienen rumbo y velocidad, el CPA es la menor
 * distancia a la que pasaran el uno del otro, y el TCPA el tiempo que falta
 * para ese momento. Es la cuenta clasica de la navegacion para prevenir
 * abordajes, y vale igual para aeronaves o vehiculos (solo es geometria).
 *
 * Se calcula en un plano tangente local, asi que es exacto para las
 * distancias de trabajo de una alerta (decenas de km); a cientos de km
 * empieza a notarse la curvatura.
 */
struct CpaResult
{
    bool valid = false;
    //! Distancia en el CPA, en metros.
    double distanceMeters = std::numeric_limits<double>::quiet_NaN();
    //! TCPA en segundos desde el instante de referencia. Negativo: el momento
    //! de maximo acercamiento ya paso y se estan alejando.
    double timeSeconds = std::numeric_limits<double>::quiet_NaN();
    //! Distancia ahora (en el instante de referencia), en metros.
    double currentDistanceMeters = std::numeric_limits<double>::quiet_NaN();
    QGeoCoordinate positionA;   //!< Donde estara A en el CPA.
    QGeoCoordinate positionB;   //!< Donde estara B en el CPA.
};

/*!
 * \brief CPA/TCPA de dos moviles con posicion, velocidad (m/s) y rumbo
 *        (grados, 0 = norte, sentido horario).
 *
 * Si se mueven igual (velocidad relativa nula), la distancia no cambia: el
 * CPA es la distancia actual con TCPA 0. Invalido si alguna posicion no es
 * valida o algun dato no es finito.
 */
LIBMAPA_EXPORT CpaResult computeCpa(const QGeoCoordinate &positionA, double speedA,
                                    double courseA, const QGeoCoordinate &positionB,
                                    double speedB, double courseB);

//! Un par de objetivos en alerta de proximidad (MapWidget::proximityAlerts).
struct ProximityAlert
{
    qint64 targetA = -1;     //!< El de id menor.
    qint64 targetB = -1;
    CpaResult cpa;           //!< positionA es la de targetA.
};

//! Nudos a metros por segundo (1 kn = 1852 m/h).
constexpr double knotsToMetersPerSecond(double knots) { return knots * 1852.0 / 3600.0; }

} // namespace libmapa

#endif // LIBMAPA_PROXIMITY_H_
