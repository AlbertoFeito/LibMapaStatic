#include "geo/ZoneMonitor.h"

#include "geo/WebMercator.h"

#include <algorithm>

namespace libmapa {

// Pasa una coordenada al plano en que se dibuja el mapa: X = longitud, Y =
// latitud Mercator en grados. Asi un lado del poligono es la misma recta que se
// ve en pantalla, y "dentro" coincide con lo que ve el operador.
QPointF ZoneMonitor::project(const QGeoCoordinate &c)
{
    return {c.longitude(), WebMercator::latitudeToMercatorDegrees(c.latitude())};
}

// ¿Cae el punto (ya proyectado) dentro de la zona? Descarta primero por la caja
// envolvente (lo normal con muchos objetivos lejos de la zona) y luego aplica
// ray-casting par-impar a cada anillo: basta con que este en uno.
bool ZoneMonitor::contains(const Zone &z, const QPointF &p)
{
    if (p.x() < z.bounds.left() || p.x() > z.bounds.right()
        || p.y() < z.bounds.top() || p.y() > z.bounds.bottom())
        return false;

    for (const QVector<QPointF> &anillo : z.projected) {
        const int n = int(anillo.size());
        bool dentro = false;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const QPointF &a = anillo[i];
            const QPointF &b = anillo[j];
            const bool cruza = ((a.y() > p.y()) != (b.y() > p.y()))
                && (p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x());
            if (cruza)
                dentro = !dentro;
        }
        if (dentro)
            return true;
    }
    return false;
}

// Alta o cambio de contorno de una zona. Guarda los anillos validos (>= 3
// vertices validos), los proyecta y calcula su caja; despues reevalua TODOS los
// objetivos conocidos contra el nuevo contorno y devuelve las entradas/salidas.
// Sin anillos validos la zona se quita, emitiendo la salida de quien estuviera
// dentro (es un cambio de geometria, no un borrado).
QVector<ZoneMonitor::Transition> ZoneMonitor::setZone(
    qint64 zoneId, const QVector<QVector<QGeoCoordinate>> &rings)
{
    Zone nueva;
    for (const QVector<QGeoCoordinate> &anillo : rings) {
        QVector<QPointF> proy;
        proy.reserve(anillo.size());
        for (const QGeoCoordinate &c : anillo)
            if (c.isValid())
                proy.append(project(c));
        if (proy.size() < 3)
            continue;
        nueva.rings.append(anillo);
        nueva.projected.append(proy);
    }

    QVector<Transition> cambios;
    auto it = m_zones.find(zoneId);
    const QSet<qint64> antes = (it != m_zones.end()) ? it->inside : QSet<qint64>();

    if (nueva.projected.isEmpty()) {
        for (qint64 t : antes)
            cambios.append({t, zoneId, false});
        m_zones.remove(zoneId);
        return cambios;
    }

    double x0 = nueva.projected.first().first().x(), x1 = x0;
    double y0 = nueva.projected.first().first().y(), y1 = y0;
    for (const QVector<QPointF> &anillo : nueva.projected)
        for (const QPointF &p : anillo) {
            x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y());
        }
    nueva.bounds = QRectF(QPointF(x0, y0), QPointF(x1, y1));

    for (auto p = m_positions.cbegin(); p != m_positions.cend(); ++p)
        if (contains(nueva, project(p.value())))
            nueva.inside.insert(p.key());

    for (qint64 t : nueva.inside)
        if (!antes.contains(t))
            cambios.append({t, zoneId, true});
    for (qint64 t : antes)
        if (!nueva.inside.contains(t))
            cambios.append({t, zoneId, false});

    m_zones.insert(zoneId, nueva);
    return cambios;
}

// Quita una zona sin generar salidas: si la borra la aplicacion (o la entidad
// desaparece) no tiene sentido avisar de que todos "se fueron".
bool ZoneMonitor::removeZone(qint64 zoneId)
{
    return m_zones.remove(zoneId) > 0;
}

// Identificadores de las zonas vigiladas, ordenados para dar un resultado
// estable (QHash no garantiza orden).
QVector<qint64> ZoneMonitor::zones() const
{
    QVector<qint64> ids;
    ids.reserve(m_zones.size());
    for (auto it = m_zones.cbegin(); it != m_zones.cend(); ++it)
        ids.append(it.key());
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Contorno con el que se dio de alta la zona (solo los anillos validos).
QVector<QVector<QGeoCoordinate>> ZoneMonitor::zoneRings(qint64 zoneId) const
{
    const auto it = m_zones.constFind(zoneId);
    return it != m_zones.cend() ? it->rings : QVector<QVector<QGeoCoordinate>>();
}

// Mueve (o da de alta) un objetivo y reevalua solo ese objetivo contra cada zona:
// es la via del tiempo real, O(zonas). Una posicion invalida lo saca de todas
// (genera salidas) y deja de contar para zonas futuras.
QVector<ZoneMonitor::Transition> ZoneMonitor::updateTarget(qint64 targetId,
                                                           const QGeoCoordinate &position)
{
    const bool valida = position.isValid();
    if (valida)
        m_positions.insert(targetId, position);
    else
        m_positions.remove(targetId);

    QVector<Transition> cambios;
    const QPointF p = valida ? project(position) : QPointF();
    for (auto it = m_zones.begin(); it != m_zones.end(); ++it) {
        const bool estaba = it->inside.contains(targetId);
        const bool esta = valida && contains(*it, p);
        if (esta == estaba)
            continue;
        if (esta)
            it->inside.insert(targetId);
        else
            it->inside.remove(targetId);
        cambios.append({targetId, it.key(), esta});
    }
    return cambios;
}

// Olvida un objetivo borrado: lo quita de su posicion y de todas las zonas, sin
// generar salidas.
void ZoneMonitor::removeTarget(qint64 targetId)
{
    m_positions.remove(targetId);
    for (auto it = m_zones.begin(); it != m_zones.end(); ++it)
        it->inside.remove(targetId);
}

// Olvida todos los objetivos (p.ej. clearTargets del mapa), sin generar salidas.
void ZoneMonitor::clearTargets()
{
    m_positions.clear();
    for (auto it = m_zones.begin(); it != m_zones.end(); ++it)
        it->inside.clear();
}

// Objetivos dentro de una zona, ordenados por id.
QVector<qint64> ZoneMonitor::targetsIn(qint64 zoneId) const
{
    QVector<qint64> ids;
    const auto it = m_zones.constFind(zoneId);
    if (it == m_zones.cend())
        return ids;
    ids.reserve(it->inside.size());
    for (qint64 t : it->inside)
        ids.append(t);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Zonas que contienen al objetivo, ordenadas por id.
QVector<qint64> ZoneMonitor::zonesOf(qint64 targetId) const
{
    QVector<qint64> ids;
    for (auto it = m_zones.cbegin(); it != m_zones.cend(); ++it)
        if (it->inside.contains(targetId))
            ids.append(it.key());
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Objetivos cuya posicion se conoce (los que cuentan al dar de alta una zona),
// ordenados por id. Sirve para resincronizar contra otra fuente de verdad.
QVector<qint64> ZoneMonitor::trackedTargets() const
{
    QVector<qint64> ids;
    ids.reserve(m_positions.size());
    for (auto it = m_positions.cbegin(); it != m_positions.cend(); ++it)
        ids.append(it.key());
    std::sort(ids.begin(), ids.end());
    return ids;
}

// ¿Esta ahora el objetivo dentro de la zona? false si alguno no existe.
bool ZoneMonitor::isInside(qint64 targetId, qint64 zoneId) const
{
    const auto it = m_zones.constFind(zoneId);
    return it != m_zones.cend() && it->inside.contains(targetId);
}

} // namespace libmapa
