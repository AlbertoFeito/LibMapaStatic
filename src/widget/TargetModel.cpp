#include "widget/TargetModel.h"

#include <algorithm>
#include <cmath>

namespace libmapa {

TargetModel::TargetModel(QObject *parent)
    : QObject(parent)
{
}

// Recorta la traza (rastro) de un objetivo segun el limite m_trailMax:
// < 0 la deja entera, 0 la borra, > 0 conserva solo las ultimas N posiciones
// (elimina por delante las mas viejas).
void TargetModel::podarTraza(Entry &e)
{
    if (m_trailMax < 0)
        return;                    // traza ilimitada: se guarda entera
    if (m_trailMax == 0) {
        e.trail.clear();
        return;                    // sin traza
    }
    // Se recorta por delante: la traza guarda las ultimas m_trailMax posiciones.
    if (e.trail.size() > m_trailMax)
        e.trail.remove(0, e.trail.size() - m_trailMax);
}

// Inserta o actualiza un objetivo. Si viene sin id (< 0) le asigna uno nuevo; si
// trae id, mantiene el contador por encima de el. Extiende la traza con la nueva
// posicion (si cambio) y emite changed() para que la vista repinte. Devuelve el
// id, o -1 si la coordenada no es valida.
qint64 TargetModel::upsert(MapTarget target)
{
    if (!target.position.isValid())
        return -1;

    if (target.id < 0)
        target.id = m_nextId++;
    else
        m_nextId = std::max(m_nextId, target.id + 1);

    Entry &e = m_targets[target.id];
    e.target = target;
    // La posicion inicial (o la nueva, si ya existia) abre/continua la traza.
    if (m_trailMax != 0
        && (e.trail.isEmpty() || e.trail.last() != target.position)) {
        e.trail.append(target.position);
        podarTraza(e);
    }

    emit changed();
    return target.id;
}

// Mueve un objetivo existente a una nueva posicion (y rumbo, si no es NaN),
// alargando su traza. Camino rapido para refrescar posiciones sin reconstruir el
// objetivo entero. Devuelve false si el id no existe o la posicion no es valida.
bool TargetModel::update(qint64 id, const QGeoCoordinate &position,
                         double headingDeg)
{
    if (!position.isValid())
        return false;
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return false;

    it->target.position = position;
    if (!std::isnan(headingDeg))
        it->target.headingDeg = headingDeg;

    if (m_trailMax != 0 && (it->trail.isEmpty() || it->trail.last() != position)) {
        it->trail.append(position);
        podarTraza(*it);
    }

    emit changed();
    return true;
}

// Cambia la etiqueta de texto de un objetivo. false si el id no existe.
bool TargetModel::setLabel(qint64 id, const QString &text)
{
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return false;
    it->target.label = text;
    emit changed();
    return true;
}

// Cambia el color de un objetivo. false si el id no existe.
bool TargetModel::setColor(qint64 id, const QColor &color)
{
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return false;
    it->target.color = color;
    emit changed();
    return true;
}

// Elimina un objetivo (con su traza). false si no habia ninguno con ese id.
bool TargetModel::remove(qint64 id)
{
    if (m_targets.remove(id) == 0)
        return false;
    emit changed();
    return true;
}

// Elimina TODOS los objetivos. No emite changed() si ya estaba vacio.
void TargetModel::clear()
{
    if (m_targets.isEmpty())
        return;
    m_targets.clear();
    emit changed();
}

// Vacia la traza de un objetivo dejando solo su posicion actual como semilla.
void TargetModel::clearTrail(qint64 id)
{
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return;
    it->trail.clear();
    it->trail.append(it->target.position);
    emit changed();
}

// Devuelve el objetivo de un id, o nullopt si no existe.
std::optional<MapTarget> TargetModel::target(qint64 id) const
{
    auto it = m_targets.constFind(id);
    if (it == m_targets.constEnd())
        return std::nullopt;
    return it->target;
}

// Copia todos los objetivos (sin sus trazas) en un vector para la vista.
QVector<MapTarget> TargetModel::targets() const
{
    QVector<MapTarget> out;
    out.reserve(m_targets.size());
    for (const Entry &e : m_targets)
        out.append(e.target);
    return out;
}

// Devuelve la traza (rastro de posiciones) de un objetivo, vacia si no existe.
QVector<QGeoCoordinate> TargetModel::trail(qint64 id) const
{
    auto it = m_targets.constFind(id);
    if (it == m_targets.constEnd())
        return {};
    return it->trail;
}

// Fija el limite de puntos de traza para TODOS los objetivos y reajusta las
// trazas actuales al nuevo limite (< 0 ilimitada, 0 sin traza, > 0 ultimas N).
void TargetModel::setTrailMaxPoints(int maxPoints)
{
    // < 0 = traza ilimitada (toda); 0 = sin traza; > 0 = ultimas N posiciones.
    m_trailMax = maxPoints;
    for (Entry &e : m_targets)
        podarTraza(e);
    emit changed();
}

} // namespace libmapa
