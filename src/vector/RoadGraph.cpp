#include "vector/RoadGraph.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace libmapa {
namespace roadgraph {

const char *const kLayer = "transportation";

namespace {

const double kPi = 3.14159265358979323846;
//! Radio (m) de soldadura de un vértice interior, y de un extremo de vía (donde se juntan las calles).
const double kWeldM = 1.5;
const double kWeldEndM = 4.0;
//! Tamaño (m) de celda de la cuadrícula de búsqueda de vértices cercanos.
const double kCellM = 4.0;
//! A pie desde el punto elegido hasta la vía: se cuenta como distancia y a esta velocidad (km/h).
const double kSnapKmh = 15.0;
//! Velocidad máxima (km/h) para la heurística de A*: nunca sobrestima el tiempo.
const double kMaxKmh = 90.0;

// Fila de la tabla de velocidades.
struct Vel
{
    const char *cls;
    double kmh;
};

// Velocidad media por tipo de vía, pensada para estimar tiempos (no para cumplir límites).
const Vel kVel[] = {
    { "motorway", 90 }, { "trunk", 80 }, { "primary", 60 }, { "secondary", 50 },
    { "tertiary", 40 }, { "minor", 30 }, { "service", 15 }, { "track", 15 },
};

// Entero con el sentido único de OpenMapTiles ("1", "-1", "0", y "true"/"false" por si acaso).
int parseOneway(const QVariant &v)
{
    const QString s = v.toString();
    if (s == QLatin1String("1") || s == QLatin1String("true"))
        return 1;
    if (s == QLatin1String("-1"))
        return -1;
    return 0;
}

} // namespace

// Velocidad por tipo de vía; 0 si el coche no puede.
double carSpeedKmh(const QString &cls)
{
    for (const Vel &v : kVel)
        if (cls == QLatin1String(v.cls))
            return v.kmh;
    return 0.0;
}

// Los tramos de la capa transportation, con coordenadas geográficas.
QVector<Segment> inTile(const mvt::Tile &tile, const TileKey &key)
{
    QVector<Segment> out;
    const mvt::Layer *layer = tile.layer(QLatin1String(kLayer));
    if (!layer)
        return out;
    const int kClass = layer->keyIndex(QStringLiteral("class"));
    const int kOneway = layer->keyIndex(QStringLiteral("oneway"));
    const int kAccess = layer->keyIndex(QStringLiteral("access"));
    const int kBrunnel = layer->keyIndex(QStringLiteral("brunnel"));
    const double n = double(1 << key.z);
    const double ext = double(layer->extent);

    for (const mvt::Feature &f : layer->features) {
        if (f.type != mvt::GeomType::LineString)
            continue;
        Segment base;
        base.cls = layer->value(f, kClass).toString();
        base.oneway = parseOneway(layer->value(f, kOneway));
        const QString acc = layer->value(f, kAccess).toString();
        base.blocked = acc == QLatin1String("no") || acc == QLatin1String("private");
        const QString br = layer->value(f, kBrunnel).toString();
        base.level = br == QLatin1String("bridge") ? 1 : (br == QLatin1String("tunnel") ? 2 : 0);
        for (const QVector<QPoint> &part : f.parts) {
            if (part.size() < 2)
                continue;
            Segment s = base;
            s.line.reserve(part.size());
            for (const QPoint &pt : part) {
                const double lon = (key.x + pt.x() / ext) / n * 360.0 - 180.0;
                const double lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * (key.y + pt.y() / ext) / n))) * 180.0 / kPi;
                s.line.append(QGeoCoordinate(lat, lon));
            }
            out.append(s);
        }
    }
    return out;
}

// El coseno de la latitud de referencia convierte grados de longitud en metros.
Graph::Graph(double refLatDeg)
    : m_cosRef(std::cos(refLatDeg * kPi / 180.0))
{
}

// Distancia en metros (equirectangular: sobrada a las distancias de una ruta urbana o regional).
double Graph::meters(double lat1, double lon1, double lat2, double lon2) const
{
    const double dx = (lon2 - lon1) * 111320.0 * m_cosRef;
    const double dy = (lat2 - lat1) * 111320.0;
    return std::hypot(dx, dy);
}

// Celda de la cuadrícula que contiene el punto.
qint64 Graph::cellKey(double lat, double lon) const
{
    const qint64 cx = qint64(std::floor(lon * 111320.0 * m_cosRef / kCellM));
    const qint64 cy = qint64(std::floor(lat * 111320.0 / kCellM));
    return (cx << 32) ^ (cy & 0xffffffffLL);
}

// Índice de una clase en la lista (la crea si es nueva): las aristas guardan un byte, no un texto.
int Graph::classIndex(const QString &cls)
{
    int i = int(m_classes.indexOf(cls));
    if (i < 0) {
        m_classes.append(cls);
        i = int(m_classes.size()) - 1;
    }
    return i;
}

// El nodo en esa posición: uno ya existente a menos del radio de soldadura (y del mismo
// nivel, salvo extremos de puente/túnel, que se unen con el suelo) o uno nuevo.
int Graph::nodeFor(const QGeoCoordinate &c, int level, bool endpoint)
{
    const double lat = c.latitude(), lon = c.longitude();
    const double radius = endpoint ? kWeldEndM : kWeldM;
    const quint8 bit = quint8(1 << level);
    const qint64 cx = qint64(std::floor(lon * 111320.0 * m_cosRef / kCellM));
    const qint64 cy = qint64(std::floor(lat * 111320.0 / kCellM));
    int best = -1;
    double bestD = radius;
    for (qint64 dx = -1; dx <= 1; ++dx)
        for (qint64 dy = -1; dy <= 1; ++dy) {
            const auto it = m_cells.constFind(((cx + dx) << 32) ^ ((cy + dy) & 0xffffffffLL));
            if (it == m_cells.constEnd())
                continue;
            for (int idx : it.value()) {
                const Node &nd = m_nodes[idx];
                // Un puente no se suelda con lo que pasa por debajo, salvo en sus extremos.
                if (!(nd.levels & bit) && !endpoint && !nd.bridgeEnd)
                    continue;
                const double d = meters(lat, lon, nd.lat, nd.lon);
                if (d < bestD) {
                    bestD = d;
                    best = idx;
                }
            }
        }
    if (best >= 0) {
        m_nodes[best].levels |= bit;
        if (endpoint && level != 0)
            m_nodes[best].bridgeEnd = true;
        return best;
    }
    Node nd;
    nd.lat = lat;
    nd.lon = lon;
    nd.levels = bit;
    nd.bridgeEnd = endpoint && level != 0;
    m_nodes.append(nd);
    m_adj.append(QVector<Edge>());
    const int idx = int(m_nodes.size()) - 1;
    m_cells[cellKey(lat, lon)].append(idx);
    return idx;
}

// Añade los tramos: un nodo por vértice (soldando los cercanos) y una arista por cada par consecutivo.
void Graph::add(const QVector<Segment> &segments)
{
    for (const Segment &s : segments) {
        const double kmh = carSpeedKmh(s.cls);
        if (kmh <= 0.0 || s.blocked || s.line.size() < 2)
            continue;
        const int cls = classIndex(s.cls);
        const double mps = kmh / 3.6;
        int prev = -1;
        for (int i = 0; i < s.line.size(); ++i) {
            const bool endpoint = i == 0 || i == s.line.size() - 1;
            const int cur = nodeFor(s.line[i], s.level, endpoint);
            if (prev >= 0 && cur != prev) {
                const Node &a = m_nodes[prev], &b = m_nodes[cur];
                const float len = float(meters(a.lat, a.lon, b.lat, b.lon));
                m_nodes[prev].onRoad = true;        // también el que solo recibe (sentido único)
                m_nodes[cur].onRoad = true;
                Edge fwd;
                fwd.to = cur; fwd.lengthM = len; fwd.timeS = float(double(len) / mps); fwd.cls = quint8(cls);
                Edge back = fwd;
                back.to = prev;
                if (s.oneway >= 0)
                    m_adj[prev].append(fwd);        // en el sentido del trazado
                if (s.oneway <= 0)
                    m_adj[cur].append(back);        // en el contrario
            }
            prev = cur;
        }
    }
}

namespace {

// Proyección de un punto sobre el segmento a-b (en metros locales): fracción t de a a b y distancia.
void projectOnSegment(double px, double py, double ax, double ay, double bx, double by, double *t, double *dist)
{
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double f = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    f = std::max(0.0, std::min(1.0, f));
    *t = f;
    *dist = std::hypot(ax + f * dx - px, ay + f * dy - py);
}

} // namespace

// A* por tiempo. El origen y el destino se enganchan al PUNTO más cercano de una vía (no al
// vértice: una carretera recta puede tener un vértice cada kilómetro), partiendo ese tramo en dos con
// nodos virtuales. La heurística es la distancia en línea recta a la velocidad máxima: no
// sobrestima nunca, así que la ruta que da es la más rápida del grafo.
RouteResult Graph::route(const QGeoCoordinate &from, const QGeoCoordinate &to, double maxSnapM) const
{
    RouteResult r;
    r.nodes = nodeCount();
    const int n = int(m_nodes.size());
    // Coordenadas locales en metros (origen en el primer nodo) para proyectar sin dar vueltas.
    const double lat0 = n ? m_nodes[0].lat : 0.0, lon0 = n ? m_nodes[0].lon : 0.0;
    const auto xOf = [&](double lon) { return (lon - lon0) * 111320.0 * m_cosRef; };
    const auto yOf = [&](double lat) { return (lat - lat0) * 111320.0; };

    // El punto de vía más cercano a \a p: el tramo (a -> b, el sentido de la arista que lo guarda), la
    // fracción y la distancia. Cualquier arista cuenta, también las que solo se pueden recorrer en un sentido.
    struct Snap
    {
        int a = -1, b = -1;
        int k = -1;                 // índice de la arista en m_adj[a]
        double t = 0, dist = std::numeric_limits<double>::infinity();
    };
    const auto snapTo = [&](const QGeoCoordinate &p) {
        Snap best;
        const double px = xOf(p.longitude()), py = yOf(p.latitude());
        for (int a = 0; a < n; ++a)
            for (int k = 0; k < m_adj[a].size(); ++k) {
                const int b = m_adj[a][k].to;
                double t = 0, d = 0;
                projectOnSegment(px, py, xOf(m_nodes[a].lon), yOf(m_nodes[a].lat),
                                 xOf(m_nodes[b].lon), yOf(m_nodes[b].lat), &t, &d);
                if (d < best.dist) {
                    best.a = a; best.b = b; best.k = k; best.t = t; best.dist = d;
                }
            }
        return best;
    };
    if (n == 0) {
        r.error = QStringLiteral("No hay vías en la zona");
        return r;
    }
    const Snap ss = snapTo(from), st = snapTo(to);
    r.snapFromM = ss.dist;
    r.snapToM = st.dist;
    if (ss.a < 0 || ss.dist > maxSnapM) {
        r.error = QStringLiteral("El origen queda a más de %1 m de cualquier vía").arg(int(maxSnapM));
        return r;
    }
    if (st.a < 0 || st.dist > maxSnapM) {
        r.error = QStringLiteral("El destino queda a más de %1 m de cualquier vía").arg(int(maxSnapM));
        return r;
    }

    // Arista que va de x a y (la más rápida si hay varias paralelas), o null.
    const auto edgeBetween = [&](int x, int y) -> const Edge * {
        const Edge *best = nullptr;
        for (const Edge &e : m_adj[x])
            if (e.to == y && (!best || e.timeS < best->timeS))
                best = &e;
        return best;
    };
    const auto pointOn = [&](const Snap &sn) {
        const Node &a = m_nodes[sn.a], &b = m_nodes[sn.b];
        return QGeoCoordinate(a.lat + sn.t * (b.lat - a.lat), a.lon + sn.t * (b.lon - a.lon));
    };
    const QGeoCoordinate pS = pointOn(ss), pT = pointOn(st);

    // Nodos virtuales: n = punto de salida, n + 1 = punto de llegada. Aristas propias de cada uno.
    const int S = n, T = n + 1;
    QVector<Edge> salidas;                       // de S a los extremos de su tramo
    QHash<int, QVector<Edge>> llegadas;          // de los extremos del tramo de T a T
    // Aristas parciales: fracción del tramo a->b (la arista e de a a b) que va de f0 a f1.
    const auto parcial = [&](const Edge &e, int destino, double fraccion) {
        Edge p = e;
        p.to = destino;
        p.lengthM = float(double(e.lengthM) * fraccion);
        p.timeS = float(double(e.timeS) * fraccion);
        return p;
    };
    {
        const Edge &ea = m_adj[ss.a][ss.k];
        salidas.append(parcial(ea, ss.b, 1.0 - ss.t));                  // de S hacia b
        if (const Edge *back = edgeBetween(ss.b, ss.a))
            salidas.append(parcial(*back, ss.a, ss.t));                 // y hacia a, si el tramo es de doble sentido
        const Edge &et = m_adj[st.a][st.k];
        llegadas[st.a].append(parcial(et, T, st.t));                    // de a a T
        if (const Edge *back = edgeBetween(st.b, st.a))
            llegadas[st.b].append(parcial(*back, T, 1.0 - st.t));       // de b a T
    }
    // Los dos puntos en el mismo tramo: se va directo si el sentido lo permite (es lo más corto).
    double directoT = -1.0, directoLen = 0.0;
    QString directoCls;
    if ((ss.a == st.a && ss.b == st.b) || (ss.a == st.b && ss.b == st.a)) {
        const double ts = ss.t, tt = (ss.a == st.a) ? st.t : 1.0 - st.t;    // las dos fracciones, de a=ss.a a b=ss.b
        const bool adelante = tt >= ts;
        const Edge *e = adelante ? &m_adj[ss.a][ss.k] : edgeBetween(ss.b, ss.a);
        if (e) {
            const double frac = std::fabs(tt - ts);
            directoLen = double(e->lengthM) * frac;
            directoT = double(e->timeS) * frac;
            directoCls = m_classes.value(e->cls);
        }
    }

    const auto coord = [&](int i) {
        return i == S ? pS : (i == T ? pT : QGeoCoordinate(m_nodes[i].lat, m_nodes[i].lon));
    };
    const double inf = std::numeric_limits<double>::infinity();
    QVector<double> g(n + 2, inf);
    QVector<int> prev(n + 2, -1);
    QVector<Edge> via(n + 2);
    QVector<char> closed(n + 2, 0);
    const auto h = [&](int i) {
        const QGeoCoordinate c = coord(i);
        return meters(c.latitude(), c.longitude(), pT.latitude(), pT.longitude()) / (kMaxKmh / 3.6);
    };
    using QItem = std::pair<double, int>;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> open;
    g[S] = 0.0;
    open.push({ h(S), S });
    // Relaja la arista e desde u.
    const auto relaja = [&](int u, const Edge &e) {
        const int v = e.to;
        const double ng = g[u] + double(e.timeS);
        if (ng < g[v]) {
            g[v] = ng;
            prev[v] = u;
            via[v] = e;
            open.push({ ng + h(v), v });
        }
    };
    while (!open.empty()) {
        const int u = open.top().second;
        open.pop();
        if (closed[u])
            continue;
        closed[u] = 1;
        if (u == T)
            break;
        if (u == S) {
            for (const Edge &e : salidas)
                relaja(u, e);
            continue;
        }
        for (const Edge &e : m_adj[u])
            relaja(u, e);
        for (const Edge &e : llegadas.value(u))
            relaja(u, e);
    }

    // Reconstruye el camino (por el A* o, si los dos puntos están en el mismo tramo y se puede ir
    // directo, por ese tramo), sumando longitud y tiempo por clase.
    QHash<QString, double> porClase;
    QVector<QGeoCoordinate> trazado;
    if (directoT >= 0.0 && (!(g[T] < inf) || directoT <= g[T])) {
        r.distanceM = directoLen;
        r.durationS = directoT;
        porClase[directoCls] += directoLen;
        trazado = { pS, pT };
    } else {
        if (!(g[T] < inf)) {
            r.error = QStringLiteral("No hay ruta entre esos puntos: las vías no llegan a unirse (o es otra isla)");
            return r;
        }
        QVector<int> nodos;
        for (int v = T; v >= 0; v = prev[v])
            nodos.prepend(v);
        for (int i = 1; i < nodos.size(); ++i) {
            const Edge &e = via[nodos[i]];
            r.distanceM += double(e.lengthM);
            r.durationS += double(e.timeS);
            porClase[m_classes.value(e.cls)] += double(e.lengthM);
        }
        for (int v : nodos)
            trazado.append(coord(v));
    }
    // Del punto pedido a la vía y de la vía al destino: se anda, y cuenta como distancia y, despacio, tiempo.
    if (r.snapFromM > 0.5)
        trazado.prepend(from);
    if (r.snapToM > 0.5)
        trazado.append(to);
    r.path = trazado;
    r.distanceM += r.snapFromM + r.snapToM;
    r.durationS += (r.snapFromM + r.snapToM) / (kSnapKmh / 3.6);
    for (auto it = porClase.constBegin(); it != porClase.constEnd(); ++it)
        r.byClass.append({ it.key(), it.value() });
    std::sort(r.byClass.begin(), r.byClass.end(),
              [](const QPair<QString, double> &a, const QPair<QString, double> &b) { return a.second > b.second; });
    if (r.path.size() < 2)
        r.path = { from, to };
    return r;
}

} // namespace roadgraph
} // namespace libmapa
