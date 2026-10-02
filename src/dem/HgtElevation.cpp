#include "dem/HgtElevation.h"

#include <QFile>
#include <QtEndian>
#include <QtGlobal>

#include <cmath>

namespace libmapa {

// Valor de relleno de SRTM: donde no hubo dato (nube, agua, sombra de radar) el
// fichero trae el minimo de int16. No es terreno: hay que tratarlo como "sin dato".
static constexpr int kHgtVoid = -32768;

// Cambia la carpeta de los `.hgt` y vacia la cache (los tiles cacheados son de la
// carpeta anterior). Si se vuelve a poner la misma ruta igualmente se refresca.
void HgtElevation::setDirectory(const QString &dir)
{
    m_dir = dir;
    m_cache.clear();
    m_lru.clear();
}

// Fija el tope de tiles en memoria. Un minimo de 1 para que siempre quepa el que
// se esta consultando; cada tile de 30 m ocupa ~25 MB, asi que el valor es un
// compromiso entre no releer el disco y no comerse la RAM.
void HgtElevation::setCacheSize(int tiles)
{
    m_cacheSize = qMax(1, tiles);
    while (m_lru.size() > m_cacheSize) {
        m_cache.remove(m_lru.first());
        m_lru.removeFirst();
    }
}

// Arma el nombre SRTM del tile cuya esquina SUROESTE es (latFloor, lonFloor):
// hemisferio por el signo y relleno con ceros (lat a 2 digitos, lon a 3), p.ej.
// (19, -77) -> "N19W077.hgt". Es el convenio de NASA/USGS y el de AWS Skadi.
QString HgtElevation::fileNameFor(int latFloor, int lonFloor)
{
    const QChar ns = latFloor >= 0 ? QLatin1Char('N') : QLatin1Char('S');
    const QChar ew = lonFloor >= 0 ? QLatin1Char('E') : QLatin1Char('W');
    return QStringLiteral("%1%2%3%4.hgt")
        .arg(ns)
        .arg(qAbs(latFloor), 2, 10, QLatin1Char('0'))
        .arg(ew)
        .arg(qAbs(lonFloor), 3, 10, QLatin1Char('0'));
}

// Raiz cuadrada entera exacta: devuelve n si n*n == v, si no 0. Con ella se deduce
// el lado del tile (1201, 3601, ...) a partir del numero de muestras del fichero,
// que es lo unico que distingue 90 m de 30 m (el `.hgt` no tiene cabecera).
int HgtElevation::isqrtExact(qint64 v)
{
    if (v < 0)
        return 0;
    qint64 n = qint64(std::llround(std::sqrt(double(v))));
    // Ajuste fino por si el double redondeo cae a un lado (lo habitual es que no).
    while (n * n > v)
        --n;
    while ((n + 1) * (n + 1) <= v)
        ++n;
    return (n * n == v) ? int(n) : 0;
}

// Devuelve el tile de esa esquina, cargandolo del disco la primera vez y dejandolo
// en la cache LRU. Nunca falla hacia fuera: si el fichero no existe o esta corrupto
// devuelve un Tile con ok=false, que tambien se cachea para no golpear el disco en
// cada movimiento del raton sobre una zona sin datos (p.ej. el mar).
const HgtElevation::Tile &HgtElevation::tileFor(int latFloor, int lonFloor) const
{
    const QString name = fileNameFor(latFloor, lonFloor);

    // Acierto de cache: lo marcamos como el mas usado y lo devolvemos.
    auto it = m_cache.find(name);
    if (it != m_cache.end()) {
        m_lru.removeAll(name);
        m_lru.append(name);
        return it.value();
    }

    // Fallo: cargar del disco. Un fichero ausente o de tamano no-cuadrado queda
    // como Tile invalido (ok=false), igualmente cacheado.
    Tile t;
    QFile f(m_dir + QLatin1Char('/') + name);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = f.readAll();
        const int side = isqrtExact(qint64(bytes.size()) / 2);
        if (side >= 2 && qint64(side) * side * 2 == bytes.size()) {
            t.data = bytes;
            t.side = side;
            t.ok = true;
        }
    }

    // Hacemos sitio ANTES de insertar (y sin tocar nunca el recien insertado, que
    // va al final de la lista), para que la referencia devuelta siga siendo valida.
    while (m_cache.size() >= m_cacheSize && !m_lru.isEmpty()) {
        m_cache.remove(m_lru.first());
        m_lru.removeFirst();
    }
    m_lru.append(name);
    return *m_cache.insert(name, t);
}

// Lee la muestra (row, col) de un tile ya cargado: int16 big-endian en metros, o
// el valor de hueco. row=0 es la fila NORTE y col=0 la columna OESTE.
int HgtElevation::sampleAt(const Tile &t, int row, int col)
{
    const qint64 offset = (qint64(row) * t.side + col) * 2;
    const uchar *p = reinterpret_cast<const uchar *>(t.data.constData()) + offset;
    return int(qFromBigEndian<qint16>(p));
}

// Cota del terreno en metros interpolada bilinealmente en \a c, o NaN si no hay
// dato. Localiza el tile (floor de lat/lon), situa el punto en la rejilla (fila 0
// = borde norte, columna 0 = borde oeste), toma los 4 nodos que lo rodean y
// mezcla. Si cualquiera de los 4 es hueco SRTM devuelve NaN: no inventamos cota.
double HgtElevation::elevationAt(const QGeoCoordinate &c) const
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

    const int intervals = t.side - 1;   // numero de celdas por grado

    // Posicion fraccionaria en la rejilla. En X (columnas) crece hacia el ESTE; en
    // Y (filas) crece hacia el SUR, por eso la fila se mide desde el borde norte
    // (latFloor + 1). Se recorta a [0, intervals] por si el punto cae en el borde.
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
    if (v00 == kHgtVoid || v01 == kHgtVoid || v10 == kHgtVoid || v11 == kHgtVoid)
        return qQNaN();

    // Bilineal: primero las dos filas (oeste->este) y luego entre filas (norte->sur).
    const double top = v00 * (1.0 - tx) + v01 * tx;
    const double bottom = v10 * (1.0 - tx) + v11 * tx;
    return top * (1.0 - ty) + bottom * ty;
}

} // namespace libmapa
