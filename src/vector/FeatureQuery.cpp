#include "vector/FeatureQuery.h"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace libmapa {
namespace featurequery {

namespace {

const double kPi = 3.14159265358979323846;
//! Circunferencia terrestre en el ecuador (m): el mundo mide eso a z0 en Web Mercator.
const double kEquatorM = 40075016.685578488;

// Distancia (en unidades de tesela) del punto \a p al segmento a-b.
double distToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = a.x() + t * dx - p.x(), cy = a.y() + t * dy - p.y();
    return std::sqrt(cx * cx + cy * cy);
}

// ¿Cae \a p dentro del anillo \a ring? (par-impar; el anillo no repite el primer punto).
bool inRing(const QPointF &p, const QVector<QPoint> &ring)
{
    bool inside = false;
    const int n = int(ring.size());
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPoint &a = ring[i], &b = ring[j];
        if ((a.y() > p.y()) != (b.y() > p.y())
            && p.x() < double(b.x() - a.x()) * (p.y() - a.y()) / double(b.y() - a.y()) + a.x())
            inside = !inside;
    }
    return inside;
}

// Distancia mínima del punto a una polilínea (cerrada si \a closed), en unidades de tesela.
double distToPath(const QPointF &p, const QVector<QPoint> &pts, bool closed)
{
    double best = std::numeric_limits<double>::infinity();
    if (pts.size() == 1)
        return std::hypot(p.x() - pts[0].x(), p.y() - pts[0].y());
    const int n = int(pts.size());
    for (int i = 0; i + 1 < n; ++i)
        best = std::min(best, distToSegment(p, pts[i], pts[i + 1]));
    if (closed && n > 2)
        best = std::min(best, distToSegment(p, pts[n - 1], pts[0]));
    return best;
}

// Área con signo del anillo (positiva = exterior, según la convención de MvtTile).
double signedArea(const QVector<QPoint> &r)
{
    double a = 0.0;
    for (int i = 0, n = int(r.size()); i < n; ++i) {
        const QPoint &p = r[i], &q = r[(i + 1) % n];
        a += double(p.x()) * q.y() - double(q.x()) * p.y();
    }
    return a / 2.0;
}

// Distancia del punto a la entidad: 0 si cae dentro de un polígono (exterior sin
// estar en un hueco); si no, al borde / línea / punto más cercano.
double distanceTo(const QPointF &p, const mvt::Feature &f)
{
    double best = std::numeric_limits<double>::infinity();
    if (f.type == mvt::GeomType::Polygon) {
        // El MVT agrupa cada exterior con los huecos que le siguen: se evalúa por
        // grupos para que un hueco de un edificio no se «coma» al vecino.
        bool inside = false;
        for (const auto &ring : f.parts) {
            const bool outer = signedArea(ring) >= 0.0;
            if (outer)
                inside = inRing(p, ring);
            else if (inside && inRing(p, ring))
                inside = false;
            best = std::min(best, distToPath(p, ring, true));
        }
        return inside ? 0.0 : best;
    }
    for (const auto &part : f.parts) {
        if (f.type == mvt::GeomType::Point) {
            for (const QPoint &pt : part)
                best = std::min(best, std::hypot(p.x() - pt.x(), p.y() - pt.y()));
        } else {
            best = std::min(best, distToPath(p, part, false));
        }
    }
    return best;
}

// Orden de especificidad: puntos, líneas, polígonos.
int rank(mvt::GeomType t)
{
    switch (t) {
    case mvt::GeomType::Point: return 0;
    case mvt::GeomType::LineString: return 1;
    default: return 2;
    }
}

} // namespace

// Convierte el punto a coordenadas de tesela, mide contra cada entidad y ordena.
QVector<Hit> query(const mvt::Tile &tile, const TileKey &key, double lat, double lon,
                   double radiusM)
{
    QVector<Hit> out;
    const double n = double(1 << key.z);
    const double latR = std::max(-85.0511287798, std::min(85.0511287798, lat)) * kPi / 180.0;
    // Posición del punto dentro del mundo, en teselas (0..n).
    const double wx = (lon + 180.0) / 360.0 * n;
    const double wy = (1.0 - std::asinh(std::tan(latR)) / kPi) / 2.0 * n;
    // Metros reales por lado de tesela a esta latitud (Mercator estira con 1/cos).
    const double tileM = kEquatorM * std::cos(latR) / n;

    for (const mvt::Layer &layer : tile.layers) {
        const double ext = double(layer.extent);
        const QPointF p((wx - key.x) * ext, (wy - key.y) * ext);
        const double mPerUnit = tileM / ext;
        const double radiusU = radiusM / mPerUnit;

        for (const mvt::Feature &f : layer.features) {
            const double d = distanceTo(p, f);
            if (d > radiusU)
                continue;
            Hit h;
            h.layer = layer.name;
            h.type = f.type;
            h.distanceM = d * mPerUnit;
            h.id = f.id;
            for (int i = 0; i + 1 < f.tags.size(); i += 2) {
                const quint32 k = f.tags[i], v = f.tags[i + 1];
                if (k < quint32(layer.keys.size()) && v < quint32(layer.values.size()))
                    h.attributes.insert(layer.keys[int(k)], layer.values[int(v)]);
            }
            out.append(h);
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const Hit &a, const Hit &b) {
        const bool ia = a.type == mvt::GeomType::Polygon && a.distanceM == 0.0;
        const bool ib = b.type == mvt::GeomType::Polygon && b.distanceM == 0.0;
        if (ia != ib)
            return ia;
        if (std::fabs(a.distanceM - b.distanceM) > 0.5)
            return a.distanceM < b.distanceM;
        return rank(a.type) < rank(b.type);
    });
    return out;
}

} // namespace featurequery
} // namespace libmapa
