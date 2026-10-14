#include "geo/ProximityMonitor.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace libmapa {

// Lleva \a t por estima hasta \a msecs (rumbo y velocidad constantes). Sirve
// para comparar dos moviles cuyas ultimas posiciones son de horas distintas.
static ProximityMonitor::Track aHora(ProximityMonitor::Track t, qint64 msecs)
{
    const double dt = double(msecs - t.msecs) / 1000.0;
    if (dt != 0.0 && t.speed > 0.0) {
        const double d = t.speed * dt;
        t.position = d >= 0.0 ? t.position.atDistanceAndAzimuth(d, t.course)
                              : t.position.atDistanceAndAzimuth(-d, t.course + 180.0);
    }
    t.msecs = msecs;
    return t;
}

// CPA de dos moviles en la hora del mas reciente: el otro se adelanta por estima.
CpaResult ProximityMonitor::evaluate(const Track &a, const Track &b)
{
    const qint64 ref = std::max(a.msecs, b.msecs);
    const Track ta = aHora(a, ref), tb = aHora(b, ref);
    return computeCpa(ta.position, ta.speed, ta.course, tb.position, tb.speed, tb.course);
}

// Criterio de alerta: ya dentro de la distancia, o el CPA cae dentro de ella
// en el horizonte (y aun no ha pasado).
bool ProximityMonitor::enAlerta(const CpaResult &c) const
{
    if (!isEnabled() || !c.valid)
        return false;
    if (c.currentDistanceMeters <= m_distance)
        return true;
    return c.distanceMeters <= m_distance && c.timeSeconds >= 0.0
        && c.timeSeconds <= m_horizon;
}

// Descarte barato antes del calculo completo: la distancia actual
// (equirectangular, sin trigonometria cara) menos lo mas que pueden acercarse
// en el horizonte -con la diferencia de horas incluida- no baja de la alerta.
bool ProximityMonitor::puedeAcercarse(const Track &a, const Track &b) const
{
    constexpr double kMetrosPorGrado = 111195.0;
    double dLon = std::abs(a.position.longitude() - b.position.longitude());
    if (dLon > 180.0)
        dLon = 360.0 - dLon;
    const double dLat = std::abs(a.position.latitude() - b.position.latitude());
    const double coseno = std::cos(qDegreesToRadians(
        std::min(std::abs(a.position.latitude()), std::abs(b.position.latitude()))));
    // Con el coseno de la latitud mas baja el este-oeste se sobreestima un
    // poco: el descarte peca de prudente, nunca de agresivo.
    const double d0 = kMetrosPorGrado * std::max(dLat, dLon * coseno);
    const double desfase = std::abs(double(a.msecs - b.msecs)) / 1000.0;
    const double alcance = (a.speed + b.speed) * (m_horizon + desfase);
    return d0 - alcance <= m_distance * 1.01;
}

// Cambia los criterios y recalcula todos los pares (O(n^2) con descarte; se
// llama rara vez). Devuelve las alertas que empiezan y las que terminan.
QVector<ProximityMonitor::Transition> ProximityMonitor::setCriteria(double distanceMeters,
                                                                    double horizonSeconds)
{
    m_distance = std::isfinite(distanceMeters) ? std::max(0.0, distanceMeters) : 0.0;
    m_horizon = std::isfinite(horizonSeconds) ? std::max(0.0, horizonSeconds) : 0.0;

    QHash<Par, CpaResult> nuevas;
    if (isEnabled()) {
        QVector<qint64> ids;
        ids.reserve(m_tracks.size());
        for (auto it = m_tracks.cbegin(); it != m_tracks.cend(); ++it)
            ids.append(it.key());
        for (int i = 0; i < ids.size(); ++i) {
            const Track &a = m_tracks[ids[i]];
            for (int j = i + 1; j < ids.size(); ++j) {
                const Track &b = m_tracks[ids[j]];
                if (!puedeAcercarse(a, b))
                    continue;
                const CpaResult c = evaluate(a, b);
                if (enAlerta(c))
                    nuevas.insert(par(ids[i], ids[j]), c);
            }
        }
    }

    QVector<Transition> cambios;
    for (auto it = nuevas.cbegin(); it != nuevas.cend(); ++it)
        if (!m_alerts.contains(it.key()))
            cambios.append({ it.key().first, it.key().second, true, it.value() });
    for (auto it = m_alerts.cbegin(); it != m_alerts.cend(); ++it)
        if (!nuevas.contains(it.key()))
            cambios.append({ it.key().first, it.key().second, false, CpaResult() });
    m_alerts = nuevas;
    return cambios;
}

// Via del tiempo real: guarda el nuevo estado y reevalua solo los pares de
// este movil. Las alertas que siguen activas se refrescan (CPA nuevo) sin
// generar transicion.
QVector<ProximityMonitor::Transition> ProximityMonitor::updateTrack(qint64 id,
                                                                    const Track &track)
{
    QVector<Transition> cambios;
    if (!track.position.isValid()) {
        m_tracks.remove(id);
        for (auto it = m_alerts.begin(); it != m_alerts.end();) {
            if (it.key().first == id || it.key().second == id) {
                cambios.append({ it.key().first, it.key().second, false, CpaResult() });
                it = m_alerts.erase(it);
            } else {
                ++it;
            }
        }
        return cambios;
    }

    m_tracks.insert(id, track);
    if (!isEnabled())
        return cambios;

    for (auto it = m_tracks.cbegin(); it != m_tracks.cend(); ++it) {
        if (it.key() == id)
            continue;
        const Par p = par(id, it.key());
        const bool estaba = m_alerts.contains(p);
        CpaResult c;
        bool esta = false;
        if (puedeAcercarse(track, it.value())) {
            // evaluate(a, b) con a = el de id menor, para que positionA/B
            // correspondan a p.first / p.second.
            c = p.first == id ? evaluate(track, it.value()) : evaluate(it.value(), track);
            esta = enAlerta(c);
        }
        if (esta) {
            m_alerts.insert(p, c);
            if (!estaba)
                cambios.append({ p.first, p.second, true, c });
        } else if (estaba) {
            m_alerts.remove(p);
            cambios.append({ p.first, p.second, false, CpaResult() });
        }
    }
    return cambios;
}

// Olvida un movil y sus alertas, sin transiciones.
void ProximityMonitor::removeTrack(qint64 id)
{
    m_tracks.remove(id);
    for (auto it = m_alerts.begin(); it != m_alerts.end();) {
        if (it.key().first == id || it.key().second == id)
            it = m_alerts.erase(it);
        else
            ++it;
    }
}

// Olvida todos los moviles y alertas, sin transiciones.
void ProximityMonitor::clear()
{
    m_tracks.clear();
    m_alerts.clear();
}

// CPA de dos moviles conocidos, en el orden pedido (positionA es de \a a).
CpaResult ProximityMonitor::cpa(qint64 a, qint64 b) const
{
    const auto ia = m_tracks.constFind(a), ib = m_tracks.constFind(b);
    if (a == b || ia == m_tracks.cend() || ib == m_tracks.cend())
        return CpaResult();
    return evaluate(*ia, *ib);
}

// Pares en alerta, ordenados por (a, b) para un resultado estable.
QVector<ProximityMonitor::Transition> ProximityMonitor::alerts() const
{
    QVector<Transition> v;
    v.reserve(m_alerts.size());
    for (auto it = m_alerts.cbegin(); it != m_alerts.cend(); ++it)
        v.append({ it.key().first, it.key().second, true, it.value() });
    std::sort(v.begin(), v.end(), [](const Transition &x, const Transition &y) {
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    });
    return v;
}

// ¿Esta el par en alerta? El orden de los ids da igual.
bool ProximityMonitor::isAlerted(qint64 a, qint64 b) const
{
    return m_alerts.contains(par(a, b));
}

} // namespace libmapa
