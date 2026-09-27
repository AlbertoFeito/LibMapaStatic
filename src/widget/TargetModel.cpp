#include "widget/TargetModel.h"

#include <algorithm>
#include <cmath>

namespace libmapa {

TargetModel::TargetModel(QObject *parent)
    : QObject(parent)
{
}

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

bool TargetModel::setLabel(qint64 id, const QString &text)
{
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return false;
    it->target.label = text;
    emit changed();
    return true;
}

bool TargetModel::setColor(qint64 id, const QColor &color)
{
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return false;
    it->target.color = color;
    emit changed();
    return true;
}

bool TargetModel::remove(qint64 id)
{
    if (m_targets.remove(id) == 0)
        return false;
    emit changed();
    return true;
}

void TargetModel::clear()
{
    if (m_targets.isEmpty())
        return;
    m_targets.clear();
    emit changed();
}

void TargetModel::clearTrail(qint64 id)
{
    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return;
    it->trail.clear();
    it->trail.append(it->target.position);
    emit changed();
}

std::optional<MapTarget> TargetModel::target(qint64 id) const
{
    auto it = m_targets.constFind(id);
    if (it == m_targets.constEnd())
        return std::nullopt;
    return it->target;
}

QVector<MapTarget> TargetModel::targets() const
{
    QVector<MapTarget> out;
    out.reserve(m_targets.size());
    for (const Entry &e : m_targets)
        out.append(e.target);
    return out;
}

QVector<QGeoCoordinate> TargetModel::trail(qint64 id) const
{
    auto it = m_targets.constFind(id);
    if (it == m_targets.constEnd())
        return {};
    return it->trail;
}

void TargetModel::setTrailMaxPoints(int maxPoints)
{
    // < 0 = traza ilimitada (toda); 0 = sin traza; > 0 = ultimas N posiciones.
    m_trailMax = maxPoints;
    for (Entry &e : m_targets)
        podarTraza(e);
    emit changed();
}

} // namespace libmapa
