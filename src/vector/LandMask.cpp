#include "vector/LandMask.h"

#include "db/VectorRepository.h"
#include "geo/WebMercator.h"
#include "libmapa/MapFeature.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace libmapa {

namespace {

// Coordenada -> Web Mercator normalizado: x de 0 (180° O) a 1 (180° E) e y de 0
// (85° N) a 1 (85° S), la misma rejilla que las teselas XYZ a cualquier zoom.
QPointF normalized(const QGeoCoordinate &c)
{
    const double pi = 3.14159265358979323846;
    const double lat = WebMercator::clampLatitude(c.latitude()) * pi / 180.0;
    return QPointF((c.longitude() + 180.0) / 360.0,
                   (1.0 - std::asinh(std::tan(lat)) / pi) / 2.0);
}

// Lados del recuadro de la tesela (ampliado con un margen) por fuera de los que
// queda un punto: bit 1 izquierda, 2 derecha, 4 arriba, 8 abajo. 0 = dentro.
int outCode(const QPointF &p, double lo, double hi)
{
    return (p.x() < lo ? 1 : 0) | (p.x() > hi ? 2 : 0) | (p.y() < lo ? 4 : 0) | (p.y() > hi ? 8 : 0);
}

} // namespace

// Abre la BD de entidades y se queda con las partes de sus polígonos (los de la
// capa pedida, o todos). Es lo mismo que lee dem_to_hillshade --water-mask.
std::shared_ptr<LandMask> LandMask::fromFile(const QString &path, const QString &layer, QString *error)
{
    VectorRepository repo;
    if (!repo.open(path)) {
        if (error)
            *error = QStringLiteral("No se pudo abrir la máscara de tierra %1: %2").arg(path, repo.lastError());
        return nullptr;
    }
    QVector<QVector<QGeoCoordinate>> rings;
    const QVector<MapFeature> features = layer.isEmpty() ? repo.loadFeatures()
                                                         : repo.loadFeaturesInLayer(layer);
    for (const MapFeature &f : features)
        if (f.kind == GeometryKind::Polygon)
            for (const QVector<QGeoCoordinate> &part : f.outlines())
                rings.append(part);
    repo.close();

    auto mask = fromRings(rings);
    if (!mask) {
        if (error)
            *error = QStringLiteral("La máscara de tierra %1%2 no tiene polígonos (se espera una BD de "
                                    "entidades con la tierra como polígono, la de osm_costa_a_sqlitedb.py)")
                         .arg(path, layer.isEmpty() ? QString() : QStringLiteral(" (capa %1)").arg(layer));
    }
    return mask;
}

// Construye la máscara con los anillos de 3 o más puntos; null si no queda ninguno.
std::shared_ptr<LandMask> LandMask::fromRings(const QVector<QVector<QGeoCoordinate>> &rings)
{
    auto mask = std::make_shared<LandMask>();
    for (const QVector<QGeoCoordinate> &r : rings)
        if (r.size() >= 3)
            mask->addRing(r);
    if (mask->m_rings.empty())
        return nullptr;
    return mask;
}

// Pasa el anillo a Mercator normalizado y le calcula el recuadro.
void LandMask::addRing(const QVector<QGeoCoordinate> &ring)
{
    Ring r;
    r.pts.reserve(ring.size());
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const QGeoCoordinate &c : ring) {
        const QPointF p = normalized(c);
        r.pts.append(p);
        x0 = qMin(x0, p.x()); x1 = qMax(x1, p.x());
        y0 = qMin(y0, p.y()); y1 = qMax(y1, p.y());
    }
    r.box = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    m_vertices += r.pts.size();
    m_rings.push_back(std::move(r));
}

/*
 * Rellena la tierra de una tesela. El anillo de la isla grande tiene ~100 000
 * vértices y toca casi todas las teselas, así que no se le pasan todos a QPainter:
 *  - se descartan los anillos cuyo recuadro no toca la tesela, y los que miden
 *    menos de medio píxel (cayos invisibles a ese zoom);
 *  - una racha de puntos que quedan todos al mismo lado FUERA de la tesela se
 *    reduce a sus extremos: el tramo sustituto cae en ese mismo semiplano, así
 *    que no cambia qué queda dentro o fuera en la tesela;
 *  - un punto a menos de 0,3 px del anterior no se añade.
 * Así una tesela cuesta lo que su trozo de costa, no lo que la isla entera.
 */
bool LandMask::paint(QPainter &p, int z, int x, int y, int sizePx, const QColor &land) const
{
    const double n = std::ldexp(1.0, z);
    const double size = double(sizePx);
    const double margin = 2.0;                               // px fuera del borde
    const double pxPerUnit = n * size;
    const QRectF tileBox((double(x) - margin / size) / n, (double(y) - margin / size) / n,
                         (1.0 + 2.0 * margin / size) / n, (1.0 + 2.0 * margin / size) / n);
    const double lo = -margin, hi = size + margin;
    const double minStep2 = 0.3 * 0.3;

    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    QVector<QPointF> out;
    QVector<int> codes;

    for (const Ring &r : m_rings) {
        if (!r.box.intersects(tileBox))
            continue;
        if (r.box.width() * pxPerUnit < 0.5 && r.box.height() * pxPerUnit < 0.5)
            continue;
        out.clear();
        codes.clear();
        int runCode = 0;
        for (const QPointF &q : r.pts) {
            const QPointF pt((q.x() * n - double(x)) * size, (q.y() * n - double(y)) * size);
            const int c = outCode(pt, lo, hi);
            const int k = int(out.size());
            if (k >= 2 && (runCode & c) != 0) {               // sigue la racha de fuera
                out[k - 1] = pt;
                codes[k - 1] = c;
                runCode &= c;
                continue;
            }
            if (k >= 1) {
                const QPointF d = pt - out[k - 1];
                if (d.x() * d.x() + d.y() * d.y() < minStep2)
                    continue;
            }
            out.append(pt);
            codes.append(c);
            runCode = k >= 1 ? (codes[k - 1] & c) : 0;
        }
        if (out.size() >= 3)
            path.addPolygon(QPolygonF(out));
    }
    if (path.isEmpty())
        return false;
    p.fillPath(path, land);
    return true;
}

} // namespace libmapa
