#ifndef LIBMAPA_VECTOR_ROUTEPLANNER_H_
#define LIBMAPA_VECTOR_ROUTEPLANNER_H_

#include "libmapa/Places.h"
#include "tiles/TileDataset.h"

#include <QGeoCoordinate>

#include <functional>

namespace libmapa {

/*!
 * \brief Calcula una ruta leyendo la red vial de un MBTiles vectorial. Solo núcleo: lo usan
 *        `MapWidget::findRoute` y `vector_tile --route`.
 */
namespace routeplanner {

//! Tope de teselas a leer (el mismo que \c MapWidget::kMaxRouteTiles).
constexpr int kMaxTiles = 3000;

/*!
 * \brief La ruta más rápida en coche de \a from a \a to por la capa `transportation` de \a dataset.
 *
 * Lee las teselas (z14 o la más alta del MBTiles) del rectángulo que une los puntos con un
 * margen de al menos 3 teselas o un cuarto de lo que miden, fila a fila, las cose en un grafo y
 * busca el camino. Si no hay camino, repite una vez con el margen triplicado (un camino que
 * rodea una bahía queda fuera del primero), siempre que quepa en \c kMaxTiles.
 *
 * \a progress (opcional) recibe (filas leídas, filas totales) y, si devuelve false, corta.
 */
RouteResult findRoute(const TileDataset &dataset, const QGeoCoordinate &from, const QGeoCoordinate &to,
                      const std::function<bool(int, int)> &progress = nullptr);

} // namespace routeplanner

} // namespace libmapa

#endif // LIBMAPA_VECTOR_ROUTEPLANNER_H_
