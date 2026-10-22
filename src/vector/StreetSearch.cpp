#include "vector/StreetSearch.h"

#include "io/PlaceIndex.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace libmapa {
namespace streetsearch {

const char *const kLayer = "transportation_name";

namespace {

const double kPi = 3.14159265358979323846;
//! Dos tramos del mismo nombre a menos de esto (m) son la misma calle: cubre el solape de
//! las teselas vecinas (el MVT trae un margen) y los cortes por una intersección.
const double kJoinM = 15.0;

// Tipos de vía en español. Lo que no esté se muestra con el nombre en inglés.
struct Tipo
{
    const char *clave;
    const char *texto;
};

const Tipo kTipos[] = {
    { "motorway", QT_TRANSLATE_NOOP("libmapa::Street", "autopista") },
    { "trunk", QT_TRANSLATE_NOOP("libmapa::Street", "vía troncal") },
    { "primary", QT_TRANSLATE_NOOP("libmapa::Street", "carretera principal") },
    { "secondary", QT_TRANSLATE_NOOP("libmapa::Street", "carretera secundaria") },
    { "tertiary", QT_TRANSLATE_NOOP("libmapa::Street", "carretera terciaria") },
    { "minor", QT_TRANSLATE_NOOP("libmapa::Street", "calle") },
    { "residential", QT_TRANSLATE_NOOP("libmapa::Street", "calle") },
    { "service", QT_TRANSLATE_NOOP("libmapa::Street", "vía de servicio") },
    { "track", QT_TRANSLATE_NOOP("libmapa::Street", "camino") },
    { "path", QT_TRANSLATE_NOOP("libmapa::Street", "sendero") },
    { "pedestrian", QT_TRANSLATE_NOOP("libmapa::Street", "calle peatonal") },
    { "raceway", QT_TRANSLATE_NOOP("libmapa::Street", "circuito") },
    { "busway", QT_TRANSLATE_NOOP("libmapa::Street", "vía de guaguas") },
    { "rail", QT_TRANSLATE_NOOP("libmapa::Street", "ferrocarril") },
};

// Palabras genéricas que alguien escribe delante del nombre y que el nombre del MBTiles
// puede no llevar («calle obispo» → «Obispo»). Son opcionales en la búsqueda.
bool esGenerica(const QString &w)
{
    static const QSet<QString> g = { "calle", "c", "avenida", "ave", "av", "avda", "calzada", "carretera",
                                     "camino", "paseo", "via", "autopista", "callejon", "alameda" };
    return g.contains(w);
}

// Distancia en metros entre dos coordenadas (equirectangular: sobrada a decenas de metros).
double dist(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    const double k = 111320.0;
    const double dx = (a.longitude() - b.longitude()) * k * std::cos((a.latitude() + b.latitude()) * 0.5 * kPi / 180.0);
    const double dy = (a.latitude() - b.latitude()) * k;
    return std::hypot(dx, dy);
}

// Distancia mínima del punto \a p a la polilínea \a l (m), y el punto más cercano en \a *at.
double distAPolilinea(const QGeoCoordinate &p, const QVector<QGeoCoordinate> &l, QGeoCoordinate *at)
{
    double best = std::numeric_limits<double>::infinity();
    const double k = 111320.0;
    const double cosL = std::cos(p.latitude() * kPi / 180.0);
    for (int i = 0; i + 1 < l.size(); ++i) {
        // Plano local en metros con origen en p.
        const double ax = (l[i].longitude() - p.longitude()) * k * cosL, ay = (l[i].latitude() - p.latitude()) * k;
        const double bx = (l[i + 1].longitude() - p.longitude()) * k * cosL, by = (l[i + 1].latitude() - p.latitude()) * k;
        const double dx = bx - ax, dy = by - ay;
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0.0 ? (-ax * dx - ay * dy) / len2 : 0.0;
        t = std::max(0.0, std::min(1.0, t));
        const double cx = ax + t * dx, cy = ay + t * dy;
        const double d = std::hypot(cx, cy);
        if (d < best) {
            best = d;
            if (at)
                *at = QGeoCoordinate(l[i].latitude() + t * (l[i + 1].latitude() - l[i].latitude()),
                                     l[i].longitude() + t * (l[i + 1].longitude() - l[i].longitude()));
        }
    }
    if (l.size() == 1) {
        best = dist(p, l[0]);
        if (at)
            *at = l[0];
    }
    return best;
}

// ¿Se tocan (a menos de kJoinM) dos tramos? Vértices de uno contra los segmentos del otro, con
// descarte rápido por cajas.
bool seTocan(const Segment &a, const Segment &b)
{
    const auto caja = [](const Segment &s, double *s0, double *n0, double *w0, double *e0) {
        *s0 = 90; *n0 = -90; *w0 = 180; *e0 = -180;
        for (const QGeoCoordinate &c : s.line) {
            *s0 = std::min(*s0, c.latitude());  *n0 = std::max(*n0, c.latitude());
            *w0 = std::min(*w0, c.longitude()); *e0 = std::max(*e0, c.longitude());
        }
    };
    double as, an, aw, ae, bs, bn, bw, be;
    caja(a, &as, &an, &aw, &ae);
    caja(b, &bs, &bn, &bw, &be);
    const double m = kJoinM / 111320.0 * 1.5;       // margen en grados (algo holgado)
    if (ae + m < bw || be + m < aw || an + m < bs || bn + m < as)
        return false;
    for (const QGeoCoordinate &c : a.line)
        if (distAPolilinea(c, b.line, nullptr) <= kJoinM)
            return true;
    for (const QGeoCoordinate &c : b.line)
        if (distAPolilinea(c, a.line, nullptr) <= kJoinM)
            return true;
    return false;
}

// Union-find mínimo para agrupar tramos.
int raiz(QVector<int> &p, int i)
{
    while (p[i] != i) {
        p[i] = p[p[i]];
        i = p[i];
    }
    return i;
}

} // namespace

// Los tramos de línea de la capa, con su geometría geográfica.
QVector<Segment> inTile(const mvt::Tile &tile, const TileKey &key)
{
    QVector<Segment> out;
    const mvt::Layer *layer = tile.layer(QLatin1String(kLayer));
    if (!layer)
        return out;
    const int kName = layer->keyIndex(QStringLiteral("name"));
    const int kNameEs = layer->keyIndex(QStringLiteral("name:es"));
    const int kNameLatin = layer->keyIndex(QStringLiteral("name:latin"));
    const int kRef = layer->keyIndex(QStringLiteral("ref"));
    const int kClass = layer->keyIndex(QStringLiteral("class"));
    const double n = double(1 << key.z);
    const double ext = double(layer->extent);

    for (const mvt::Feature &f : layer->features) {
        if (f.type != mvt::GeomType::LineString)
            continue;
        QString name = layer->value(f, kNameEs).toString();
        if (name.isEmpty())
            name = layer->value(f, kName).toString();
        if (name.isEmpty())
            name = layer->value(f, kNameLatin).toString();
        const QString ref = layer->value(f, kRef).toString();
        if (name.isEmpty() && ref.isEmpty())
            continue;
        for (const QVector<QPoint> &part : f.parts) {
            if (part.size() < 2)
                continue;
            Segment s;
            s.name = name;
            s.ref = ref;
            s.type = layer->value(f, kClass).toString();
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

// Agrupa por nombre, une los tramos que se tocan, filtra por texto y ordena.
QVector<Street> search(const QVector<Segment> &segments, const QString &text,
                       const QGeoCoordinate &from, int limit)
{
    const QString norm = PlaceIndex::normalize(text);
    if (norm.size() < 2 || limit <= 0)
        return {};
    QStringList palabras = norm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Las genéricas ("calle") son opcionales, salvo que sea lo único que se escribió.
    QStringList utiles;
    for (const QString &w : palabras)
        if (!esGenerica(w))
            utiles << w;
    const QStringList buscadas = utiles.isEmpty() ? palabras : utiles;

    // 1) Los tramos que casan, agrupados por nombre normalizado (o la referencia).
    struct Grupo
    {
        QVector<int> idx;           // índices en segments
        int rango = 2;              // 0 empieza por lo escrito, 1 lo contiene
    };
    QHash<QString, Grupo> grupos;
    for (int i = 0; i < segments.size(); ++i) {
        const Segment &s = segments[i];
        const QString nombre = PlaceIndex::normalize(s.name);
        const QString ref = PlaceIndex::normalize(s.ref);
        bool todas = true;
        bool todasEnNombre = !nombre.isEmpty();
        for (const QString &w : buscadas) {
            const bool enNombre = !nombre.isEmpty() && nombre.contains(w);
            todasEnNombre = todasEnNombre && enNombre;
            if (!enNombre && !(!ref.isEmpty() && ref.contains(w))) {
                todas = false;
                break;
            }
        }
        if (!todas)
            continue;
        Grupo &g = grupos[nombre.isEmpty() ? QStringLiteral("ref:") + ref : nombre];
        g.idx.append(i);
        g.rango = std::min(g.rango, todasEnNombre ? (nombre.startsWith(buscadas.first()) ? 0 : 1) : 2);
    }

    // 2) Dentro de cada nombre, los tramos que se tocan son una calle.
    struct Candidata
    {
        Street street;
        int rango;
    };
    QVector<Candidata> cands;
    for (auto it = grupos.constBegin(); it != grupos.constEnd(); ++it) {
        const QVector<int> &idx = it.value().idx;
        QVector<int> padre(idx.size());
        for (int i = 0; i < idx.size(); ++i)
            padre[i] = i;
        for (int i = 0; i < idx.size(); ++i)
            for (int j = i + 1; j < idx.size(); ++j)
                if (raiz(padre, i) != raiz(padre, j) && seTocan(segments[idx[i]], segments[idx[j]]))
                    padre[raiz(padre, i)] = raiz(padre, j);
        QHash<int, QVector<int>> componentes;
        for (int i = 0; i < idx.size(); ++i)
            componentes[raiz(padre, i)].append(idx[i]);

        for (const QVector<int> &comp : componentes) {
            Street st;
            double s0 = 90, n0 = -90, w0 = 180, e0 = -180;
            double mejor = std::numeric_limits<double>::infinity();
            QHash<QString, int> porTipo;                    // el tipo más repetido
            for (int k : comp) {
                const Segment &seg = segments[k];
                if (st.name.isEmpty())
                    st.name = seg.name;
                if (st.ref.isEmpty())
                    st.ref = seg.ref;
                porTipo[seg.type] += int(seg.line.size());
                st.parts.append(seg.line);
                for (const QGeoCoordinate &c : seg.line) {
                    s0 = std::min(s0, c.latitude());  n0 = std::max(n0, c.latitude());
                    w0 = std::min(w0, c.longitude()); e0 = std::max(e0, c.longitude());
                }
                QGeoCoordinate en;
                const double d = from.isValid() ? distAPolilinea(from, seg.line, &en) : 0.0;
                if (d < mejor || !st.nearest.isValid()) {
                    mejor = d;
                    st.nearest = from.isValid() ? en : seg.line.first();
                }
            }
            int masVotos = -1;
            for (auto t = porTipo.constBegin(); t != porTipo.constEnd(); ++t)
                if (t.value() > masVotos) {
                    masVotos = t.value();
                    st.type = t.key();
                }
            if (st.name.isEmpty())
                st.name = st.ref;
            st.northWest = QGeoCoordinate(n0, w0);
            st.southEast = QGeoCoordinate(s0, e0);
            st.distanceM = from.isValid() ? mejor : 0.0;
            cands.append({ st, it.value().rango });
        }
    }

    std::stable_sort(cands.begin(), cands.end(), [](const Candidata &a, const Candidata &b) {
        if (a.rango != b.rango)
            return a.rango < b.rango;
        return a.street.distanceM < b.street.distanceM;
    });
    QVector<Street> out;
    for (int i = 0; i < cands.size() && i < limit; ++i)
        out.append(cands[i].street);
    return out;
}

} // namespace streetsearch

// Tipo en español de la vía; el tipo tal cual si no lo conoce.
QString Street::typeLabel() const
{
    for (const streetsearch::Tipo &t : streetsearch::kTipos)
        if (type == QLatin1String(t.clave))
            return QCoreApplication::translate("libmapa::Street", t.texto);
    return type;
}

} // namespace libmapa
