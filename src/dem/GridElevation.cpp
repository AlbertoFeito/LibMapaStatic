#include "dem/GridElevation.h"

#include <QtEndian>
#include <QtGlobal>

#include <cmath>

namespace libmapa {

// Valor de relleno de SRTM: donde no hubo dato (nube, agua, sombra de radar) el
// tile trae el minimo de int16. No es terreno: se trata como "sin dato".
static constexpr int kVoid = -32768;

// Cambia el tope de tiles en memoria (minimo 1: siempre debe caber el que se
// consulta). Si al bajarlo sobran, se desalojan los menos usados.
void GridElevation::setCacheSize(int tiles)
{
    m_cacheSize = qMax(1, tiles);
    while (m_lru.size() > m_cacheSize) {
        m_cache.remove(m_lru.first());
        m_lru.removeFirst();
    }
}

// Vacia la cache por completo (p.ej. al cambiar la carpeta o la BD de origen).
void GridElevation::clearCache()
{
    m_cache.clear();
    m_lru.clear();
}

// Raiz cuadrada entera exacta: n si n*n == v, si no 0. Con ella se deduce el lado
// (1201, 3601, ...) a partir del numero de muestras, que es lo unico que
// distingue 90 m de 30 m en un `.hgt` (no tiene cabecera).
int GridElevation::isqrtExact(qint64 v)
{
    if (v < 0)
        return 0;
    qint64 n = qint64(std::llround(std::sqrt(double(v))));
    while (n * n > v)
        --n;
    while ((n + 1) * (n + 1) <= v)
        ++n;
    return (n * n == v) ? int(n) : 0;
}

// Lee la muestra (row, col) de un tile cargado: int16 big-endian en metros, o el
// valor de hueco. row=0 es la fila NORTE y col=0 la columna OESTE.
int GridElevation::sampleAt(const Tile &t, int row, int col)
{
    const qint64 offset = (qint64(row) * t.side + col) * 2;
    const uchar *p = reinterpret_cast<const uchar *>(t.data.constData()) + offset;
    return int(qFromBigEndian<qint16>(p));
}

// Devuelve el tile de esa esquina, cargandolo via loadTile() la primera vez y
// dejandolo en la cache LRU. Nunca falla hacia fuera: si no existe (o el tamano
// no cuadra), devuelve un Tile con ok=false, que tambien se cachea para no
// repetir el acceso al disco/BD sobre una zona sin datos (p.ej. el mar).
const GridElevation::Tile &GridElevation::tileFor(int latFloor, int lonFloor) const
{
    const qint64 key = keyFor(latFloor, lonFloor);

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_lru.removeAll(key);
        m_lru.append(key);
        return it.value();
    }

    Tile t;
    QByteArray data;
    int side = 0;
    if (loadTile(latFloor, lonFloor, data, side)
        && side >= 2 && qint64(side) * side * 2 == data.size()) {
        t.data = data;
        t.side = side;
        t.ok = true;
    }

    // Hacer sitio ANTES de insertar (y sin tocar el recien insertado, al final de
    // la lista) para que la referencia devuelta siga siendo valida.
    while (m_cache.size() >= m_cacheSize && !m_lru.isEmpty()) {
        m_cache.remove(m_lru.first());
        m_lru.removeFirst();
    }
    m_lru.append(key);
    return *m_cache.insert(key, t);
}

// Cota del terreno (m) interpolada bilinealmente en \a c, o NaN si no hay dato.
// Localiza el tile (floor de lat/lon), situa el punto en la rejilla (fila 0 =
// borde norte, columna 0 = borde oeste), toma los 4 nodos que lo rodean y mezcla.
// Si alguno es hueco SRTM devuelve NaN: no se inventa cota.
double GridElevation::elevationAt(const QGeoCoordinate &c) const
{
    if (!c.isValid())
        return qQNaN();

    const double lat = c.latitude();
    const double lon = c.longitude();
    const int latFloor = int(std::floor(lat));
    const int lonFloor = int(std::floor(lon));

    const Tile &t = tileFor(latFloor, lonFloor);
    if (!t.ok)
        return qQNaN();

    const int intervals = t.side - 1;   // celdas por grado

    // Posicion fraccionaria. En X (columnas) crece hacia el ESTE; en Y (filas)
    // crece hacia el SUR, por eso la fila se mide desde el borde norte
    // (latFloor + 1). Se recorta a [0, intervals] por si cae en el borde.
    double fx = (lon - lonFloor) * intervals;
    double fy = (double(latFloor + 1) - lat) * intervals;
    fx = qBound(0.0, fx, double(intervals));
    fy = qBound(0.0, fy, double(intervals));

    const int col0 = int(std::floor(fx));
    const int row0 = int(std::floor(fy));
    const int col1 = qMin(col0 + 1, t.side - 1);
    const int row1 = qMin(row0 + 1, t.side - 1);
    const double tx = fx - col0;
    const double ty = fy - row0;

    const int v00 = sampleAt(t, row0, col0);
    const int v01 = sampleAt(t, row0, col1);
    const int v10 = sampleAt(t, row1, col0);
    const int v11 = sampleAt(t, row1, col1);
    if (v00 == kVoid || v01 == kVoid || v10 == kVoid || v11 == kVoid)
        return qQNaN();

    // Bilineal: primero las dos filas (oeste->este) y luego entre filas (norte->sur).
    const double top = v00 * (1.0 - tx) + v01 * tx;
    const double bottom = v10 * (1.0 - tx) + v11 * tx;
    return top * (1.0 - ty) + bottom * ty;
}

} // namespace libmapa
