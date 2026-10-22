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

    for (int intento = 0; intento < 2; ++intento) {
        const int m = intento == 0 ? margen1 : margen1 * 3;
        const int x0 = qMax(0, qMin(ax, bx) - m), x1 = qMin(int(n) - 1, qMax(ax, bx) + m);
        const int y0 = qMax(0, qMin(ay, by) - m), y1 = qMin(int(n) - 1, qMax(ay, by) + m);
        const int teselas = (x1 - x0 + 1) * (y1 - y0 + 1);
        if (teselas > kMaxTiles) {
            if (intento == 0) {
                r.error = QStringLiteral("Ruta demasiado larga: haría falta leer unas %1 teselas y el máximo es %2")
                              .arg(teselas).arg(kMaxTiles);
                r.tiles = teselas;
                return r;
            }
            break;                          // el segundo intento no cabe: se queda con el primer error
        }
        const RouteResult nueva = routeInRect(ds, z, x0, x1, y0, y1, from, to, progress);
        if (nueva.ok() || !nueva.error.startsWith(QLatin1String("No hay ruta")))
            return nueva;
        r = nueva;                           // sin camino: se reintenta con más margen
    }
    return r;
}

} // namespace routeplanner
} // namespace libmapa
