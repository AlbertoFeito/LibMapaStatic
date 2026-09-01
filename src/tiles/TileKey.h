#ifndef LIBMAPA_TILES_TILEKEY_H_
#define LIBMAPA_TILES_TILEKEY_H_

#include <QHash>
#include <QtGlobal>
#include <cstddef>

namespace libmapa {

/*!
 * \brief Identidad de una tesela en coordenadas LOGICAS (z siempre creciente
 *        hacia mas detalle, y siempre en esquema XYZ).
 *
 * La conversion a lo que realmente guarda cada BD (z invertido, y en TMS)
 * la hace TileMatrix/RMapsTileSource a partir del TileDataset. Fuera de esa
 * frontera, todo el codigo habla el mismo idioma.
 *
 * Es hashable para poder usar QHash<TileKey, ...> y sustituir el triple bucle
 * O(n^3) de CMapaPlot::Cargar_Imagenes por busquedas O(1) (falla F-16).
 */
struct TileKey
{
    int z = 0;
    int x = 0;
    int y = 0;

    bool operator==(const TileKey &o) const noexcept
    {
        return z == o.z && x == o.x && y == o.y;
    }
    bool operator!=(const TileKey &o) const noexcept { return !(*this == o); }

    //! Tesela del nivel inmediatamente inferior que contiene a esta.
    TileKey parent() const { return TileKey{z - 1, x >> 1, y >> 1}; }

    bool isValid() const noexcept
    {
        if (z < 0 || z > 30) return false;
        const int n = 1 << z;
        return x >= 0 && x < n && y >= 0 && y < n;
    }
};

// Qt 5 usa uint en qHash; Qt 6 usa size_t. Se respeta el tipo nativo de cada
// version para no truncar el hash ni disparar -Wconversion.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using TileHashResult = size_t;
#else
using TileHashResult = uint;
#endif

inline TileHashResult qHash(const TileKey &k, TileHashResult seed = 0) noexcept
{
    // z cabe en 5 bits (0..30), x e y en 30. Se empaqueta en 64 bits sin
    // colisiones y se delega en el qHash de quint64.
    const quint64 packed = (static_cast<quint64>(k.z) << 60)
                           ^ (static_cast<quint64>(static_cast<quint32>(k.x)) << 30)
                           ^ static_cast<quint64>(static_cast<quint32>(k.y));
    return ::qHash(packed, seed);
}
} // namespace libmapa

#endif // LIBMAPA_TILES_TILEKEY_H_
