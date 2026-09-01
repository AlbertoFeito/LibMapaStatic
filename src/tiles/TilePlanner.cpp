#include "tiles/TilePlanner.h"

#include <algorithm>

namespace libmapa {

TileKey TilePlanner::ancestorOf(const TileKey &key, int depth)
{
    if (depth <= 0)
        return key;
    return TileKey{key.z - depth, key.x >> depth, key.y >> depth};
}

QRectF TilePlanner::sourceRectFor(const TileKey &slot, int ancestorDepth) const
{
    const double size = static_cast<double>(m_tileSize);
    if (ancestorDepth <= 0)
        return QRectF(0.0, 0.0, size, size);

    const int scale = 1 << ancestorDepth;          // huecos por lado del ancestro
    const double side = size / static_cast<double>(scale);

    // Posicion del hueco dentro de la rejilla que cubre el ancestro.
    const int offsetX = slot.x - ((slot.x >> ancestorDepth) << ancestorDepth);
    const int offsetY = slot.y - ((slot.y >> ancestorDepth) << ancestorDepth);

    return QRectF(offsetX * side, offsetY * side, side, side);
}

TilePlanner::Plan TilePlanner::plan(const TileMatrix::TileRange &range,
                                    const TileCache &cache,
                                    int minZoom,
                                    int maxAncestorDepth) const
{
    Plan result;
    if (range.isEmpty())
        return result;

    result.items.reserve(range.count());

    for (int x = range.xMin; x <= range.xMax; ++x) {
        for (int y = range.yMin; y <= range.yMax; ++y) {
            const TileKey slot{range.z, x, y};

            if (cache.contains(slot)) {
                TileDrawItem item;
                item.slot = slot;
                item.source = slot;
                item.sourceRect = sourceRectFor(slot, 0);
                item.ancestorDepth = 0;
                result.items.append(item);
                ++result.exactCount;
                continue;
            }

            // La tesela exacta no esta: hay que pedirla...
            result.missing.append(slot);

            // ...y mientras tanto, buscar el ancestro mas cercano que si este.
            // Cuanto menor sea la profundidad, menos borroso se vera.
            bool resolved = false;
            const int maxDepth = std::min(maxAncestorDepth, slot.z - minZoom);

            for (int d = 1; d <= maxDepth; ++d) {
                const TileKey ancestor = ancestorOf(slot, d);
                if (!ancestor.isValid())
                    break;
                if (!cache.contains(ancestor))
                    continue;

                TileDrawItem item;
                item.slot = slot;
                item.source = ancestor;
                item.sourceRect = sourceRectFor(slot, d);
                item.ancestorDepth = d;
                result.items.append(item);
                ++result.fallbackCount;
                resolved = true;
                break;
            }

            if (!resolved)
                ++result.emptyCount;
        }
    }

    // Orden de pintado: primero los origenes de menor zoom (los mas
    // escalados y borrosos), al final las teselas exactas. Asi lo nitido
    // siempre queda encima y no hay parpadeo cuando llega una tesela nueva.
    std::stable_sort(result.items.begin(), result.items.end(),
                     [](const TileDrawItem &a, const TileDrawItem &b) {
                         return a.source.z < b.source.z;
                     });

    return result;
}

} // namespace libmapa
