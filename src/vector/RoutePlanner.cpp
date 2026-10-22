#include "vector/RoutePlanner.h"

#include "geo/WebMercator.h"
#include "tiles/RMapsTileSource.h"
#include "vector/MvtTile.h"
#include "vector/RoadGraph.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace libmapa {
namespace routeplanner {

namespace {

// Arma la red vial del rectángulo de teselas [x0..x1] x [y0..y1] (zoom z) y busca la ruta. Las filas
// se leen de una en una (una consulta SQLite por fila) y solo se decodifica la capa de vías.
RouteResult routeInRect(const TileDataset &ds, int z, int x0, int x1, int y0, int y1,
                        const QGeoCoordinate &from, const QGeoCoordinate &to,
                        const std::function<bool(int, int)> &progress)
{
    RouteResult r;
    TileDataset conn = ds;
    conn.id = QStringLiteral("ruta:") + ds.filePath;        // conexión propia de este hilo
    RMapsTileSource src(conn);
    if (!src.open()) {
        r.error = QStringLiteral("No se pudo abrir el mapa vectorial");
        return r;
    }
    roadgraph::Graph grafo((from.latitude() + to.latitude()) / 2.0);
    const QStringList soloVias{ QLatin1String(roadgraph::kLayer) };
    const int filas = y1 - y0 + 1;
    for (int y = y0; y <= y1; ++y) {
        if (progress && !progress(y - y0, filas)) {
            r.error = QStringLiteral("Cálculo cancelado");
            return r;
        }
        const QHash<TileKey, QByteArray> fila = src.fetchRange(z, x0, x1, y, y);
        for (auto it = fila.constBegin(); it != fila.constEnd(); ++it) {
            mvt::Tile tile;
            if (!it.value().isEmpty() && mvt::decode(it.value(), &tile, nullptr, soloVias))
                grafo.add(roadgraph::inTile(tile, it.key()));
        }
    }
    if (progress)
        progress(filas, filas);
    r = grafo.route(from, to);
    r.tiles = (x1 - x0 + 1) * filas;
    return r;
}

} // namespace

namespace {

// Rectángulo de teselas (zoom z) que se lee para ir de \a from a \a to en el intento \a intento (0: margen
// normal, 1: margen triplicado). Lo comparten findRoute y routeReach, para que lo que se dibuja como
// alcance sea exactamente lo que findRoute acepta.
struct Rect
{
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    int teselas() const { return (x1 - x0 + 1) * (y1 - y0 + 1); }
};

// Calcula el rectángulo de teselas del intento \a intento (ver Rect).
Rect rectFor(int z, const QGeoCoordinate &from, const QGeoCoordinate &to, int intento)
{
    const double n = double(1 << z);
    const auto tx = [&](double lon) { return int(std::floor((lon + 180.0) / 360.0 * n)); };
    const auto ty = [&](double lat) {
        const double rad = qDegreesToRadians(WebMercator::clampLatitude(lat));
        return int(std::floor((1.0 - std::asinh(std::tan(rad)) / M_PI) / 2.0 * n));
    };
    const int ax = tx(from.longitude()), bx = tx(to.longitude());
    const int ay = ty(from.latitude()), by = ty(to.latitude());
    const int lado = qMax(qAbs(ax - bx), qAbs(ay - by));
    const int margen1 = qMax(3, lado / 4);
    const int m = intento == 0 ? margen1 : margen1 * 3;
    Rect r;
    r.x0 = qMax(0, qMin(ax, bx) - m);
    r.x1 = qMin(int(n) - 1, qMax(ax, bx) + m);
    r.y0 = qMax(0, qMin(ay, by) - m);
    r.y1 = qMin(int(n) - 1, qMax(ay, by) + m);
    return r;
}

} // namespace

// Rectángulo de teselas entre los dos puntos con margen, y un segundo intento más ancho si no hay camino.
RouteResult findRoute(const TileDataset &ds, const QGeoCoordinate &from, const QGeoCoordinate &to,
                      const std::function<bool(int, int)> &progress)
{
    RouteResult r;
    if (!from.isValid() || !to.isValid()) {
        r.error = QStringLiteral("Faltan el origen o el destino");
        return r;
    }
    if (!ds.vector) {
        r.error = QStringLiteral("No hay un mapa vectorial (MBTiles pbf) con la red vial");
        return r;
    }
    const int z = qBound(ds.minZoom, 14, ds.maxZoom);

    for (int intento = 0; intento < 2; ++intento) {
        const Rect rc = rectFor(z, from, to, intento);
        const int teselas = rc.teselas();
        if (teselas > kMaxTiles) {
            if (intento == 0) {
                r.error = QStringLiteral("Ruta demasiado larga: haría falta leer unas %1 teselas y el máximo es %2")
                              .arg(teselas).arg(kMaxTiles);
                r.tiles = teselas;
                return r;
            }
            break;                          // el segundo intento no cabe: se queda con el primer error
        }
        const RouteResult nueva = routeInRect(ds, z, rc.x0, rc.x1, rc.y0, rc.y1, from, to, progress);
        if (nueva.ok() || !nueva.error.startsWith(QLatin1String("No hay ruta")))
            return nueva;
        r = nueva;                           // sin camino: se reintenta con más margen
    }
    return r;
}

// Por cada rumbo, la distancia máxima a la que cabe el destino: se busca por bisección, porque las
// teselas que hacen falta crecen siempre con la distancia.
QVector<QGeoCoordinate> routeReach(const TileDataset &ds, const QGeoCoordinate &from, int bearings)
{
    QVector<QGeoCoordinate> out;
    if (!from.isValid() || !ds.vector || bearings < 3)
        return out;
    const int z = qBound(ds.minZoom, 14, ds.maxZoom);
    for (int i = 0; i < bearings; ++i) {
        const double rumbo = 360.0 * i / bearings;
        double lo = 0.0, hi = 1000000.0;                      // metros; 1000 km sobra de largo
        for (int k = 0; k < 24; ++k) {
            const double mid = (lo + hi) / 2.0;
            const QGeoCoordinate b = from.atDistanceAndAzimuth(mid, rumbo);
            const bool cabe = b.isValid() && rectFor(z, from, b, 0).teselas() <= kMaxTiles;
            (cabe ? lo : hi) = mid;
        }
        out.append(from.atDistanceAndAzimuth(lo, rumbo));
    }
    return out;
}

} // namespace routeplanner
} // namespace libmapa
