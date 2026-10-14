#ifndef LIBMAPA_GEO_PROXIMITYMONITOR_H_
#define LIBMAPA_GEO_PROXIMITYMONITOR_H_

#include "libmapa/Proximity.h"

#include <QGeoCoordinate>
#include <QHash>
#include <QPair>
#include <QVector>

namespace libmapa {

/*!
 * \brief Que pares de moviles van a pasar demasiado cerca (alertas CPA/TCPA).
 *
 * Es la logica de las alertas de proximidad de MapWidget, separada del widget
 * (como ZoneMonitor) para poder probarla sin ventanas. Solo sabe de ids,
 * posiciones, velocidades y horas.
 *
 * Un par esta EN ALERTA si, con los criterios fijados (distancia y horizonte):
 * - ya estan a menos de esa distancia, o
 * - el CPA queda por debajo de esa distancia dentro del horizonte
 *   (0 <= TCPA <= horizonte).
 *
 * Cada movil lleva la hora de su ultima posicion: para comparar dos, se lleva
 * el mas atrasado a la hora del otro por estima (rumbo y velocidad). Asi dos
 * fuentes que informan a ritmos distintos no dan CPA falsos.
 *
 * Coste: al mover uno se reevaluan sus pares, O(moviles), con un descarte
 * barato de los que no pueden acercarse lo bastante en el horizonte. Sin
 * criterios (distancia 0) no se calcula nada.
 */
class ProximityMonitor
{
public:
    //! Estado de un movil: posicion, velocidad (m/s), rumbo (grados) y hora
    //! de la posicion (ms desde epoch, UTC).
    struct Track {
        QGeoCoordinate position;
        double speed = 0.0;
        double course = 0.0;
        qint64 msecs = 0;
    };

    //! Un par entro en alerta (\a raised) o salio de ella. a < b siempre.
    struct Transition {
        qint64 a = -1;
        qint64 b = -1;
        bool raised = false;
        CpaResult cpa;
    };

    /*!
     * \brief Fija la distancia (m) y el horizonte (s) de la alerta y reevalua
     *        todos los pares. Distancia <= 0 apaga las alertas (y las que
     *        hubiera salen como transiciones de fin).
     */
    QVector<Transition> setCriteria(double distanceMeters, double horizonSeconds);
    double distance() const { return m_distance; }
    double horizon() const { return m_horizon; }
    bool isEnabled() const { return m_distance > 0.0; }

    //! Nueva posicion (o velocidad) de un movil; devuelve las transiciones de
    //! sus pares. Una posicion invalida lo quita (con las salidas que toque).
    QVector<Transition> updateTrack(qint64 id, const Track &track);
    //! Olvida un movil SIN producir salidas (se ha borrado, no se ha alejado).
    void removeTrack(qint64 id);
    //! Olvida todos, sin producir salidas.
    void clear();
    bool hasTrack(qint64 id) const { return m_tracks.contains(id); }

    //! CPA de dos moviles conocidos (invalido si falta alguno), calculado al
    //! momento aunque no esten en alerta.
    CpaResult cpa(qint64 a, qint64 b) const;
    //! Pares en alerta ahora, ordenados (a < b), con su CPA.
    QVector<Transition> alerts() const;
    bool isAlerted(qint64 a, qint64 b) const;
    bool hasAlerts() const { return !m_alerts.isEmpty(); }

    //! CPA de dos estados cualesquiera: el de hora mas antigua se lleva por
    //! estima a la del mas reciente, y el TCPA cuenta desde esa hora.
    static CpaResult evaluate(const Track &a, const Track &b);

private:
    using Par = QPair<qint64, qint64>;
    //! Clave de un par sin orden: siempre (menor, mayor).
    static Par par(qint64 a, qint64 b) { return a < b ? Par(a, b) : Par(b, a); }
    bool enAlerta(const CpaResult &c) const;
    bool puedeAcercarse(const Track &a, const Track &b) const;

    double m_distance = 0.0;
    double m_horizon = 0.0;
    QHash<qint64, Track> m_tracks;
    QHash<Par, CpaResult> m_alerts;
};

} // namespace libmapa

#endif // LIBMAPA_GEO_PROXIMITYMONITOR_H_
