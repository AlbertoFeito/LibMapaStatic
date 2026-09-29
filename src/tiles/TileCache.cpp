#include "tiles/TileCache.h"

#include "core/Logging.h"

#include <QBuffer>
#include <QImageReader>
#include <QMutexLocker>
#include <climits>

namespace libmapa {

namespace {

//! QCache usa int para el coste, asi que se contabiliza en KiB y no en bytes:
//! con bytes, 2 GiB desbordarian el int.
int costInKiB(const QImage &image)
{
    const qint64 bytes = static_cast<qint64>(image.sizeInBytes());
    return static_cast<int>(qBound<qint64>(1, bytes / 1024, static_cast<qint64>(INT_MAX)));
}

int limitInKiB(qint64 bytes)
{
    return static_cast<int>(qBound<qint64>(1, bytes / 1024, static_cast<qint64>(INT_MAX)));
}

} // namespace

TileCache::TileCache(qint64 maxBytes, qint64 pinnedBytes)
    : m_cache(limitInKiB(maxBytes))
    , m_pinned(limitInKiB(pinnedBytes))
    , m_missing(50000)      // ~2 MiB; de sobra para muchas pantallas de huecos
{
}

QImage TileCache::take(const TileKey &key) const
{
    QMutexLocker lock(&m_mutex);
    if (QImage *img = m_cache.object(key)) {
        ++m_stats.hits;
        return *img;               // copia implicitamente compartida
    }
    if (QImage *img = m_pinned.object(key)) {
        ++m_stats.hits;
        return *img;
    }
    ++m_stats.misses;
    return {};
}

bool TileCache::contains(const TileKey &key) const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.contains(key) || m_pinned.contains(key);
}

void TileCache::markMissing(const TileKey &key)
{
    QMutexLocker lock(&m_mutex);
    if (!m_cache.contains(key) && !m_pinned.contains(key))
        m_missing.insert(key, new bool(true));
}

bool TileCache::isKnownMissing(const TileKey &key) const
{
    QMutexLocker lock(&m_mutex);
    return m_missing.contains(key);
}

int TileCache::missingCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_missing.size());
}

int TileCache::countCached(int z, int xMin, int xMax,
                           int yMin, int yMax) const
{
    QMutexLocker lock(&m_mutex);
    int n = 0;
    for (int x = xMin; x <= xMax; ++x) {
        for (int y = yMin; y <= yMax; ++y) {
            const TileKey k{z, x, y};
            // Resuelta = la tenemos, o sabemos que no existe.
            if (m_cache.contains(k) || m_pinned.contains(k)
                || m_missing.contains(k))
                ++n;
        }
    }
    return n;
}

bool TileCache::insert(const TileKey &key, const QByteArray &encoded, bool pinned)
{
    if (encoded.isEmpty())
        return false;

    QBuffer buffer;
    buffer.setData(encoded);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;

    QImageReader reader(&buffer);
    const QImage image = reader.read();

    if (image.isNull()) {
        QMutexLocker lock(&m_mutex);
        ++m_stats.decodeFailures;
        qCWarning(lcMapaTiles) << "Tesela no decodificable z" << key.z
                               << "x" << key.x << "y" << key.y
                               << "-" << reader.errorString();
        return false;
    }

    insertImage(key, image, pinned);
    return true;
}

void TileCache::insertImage(const TileKey &key, const QImage &image, bool pinned)
{
    if (image.isNull())
        return;
    QMutexLocker lock(&m_mutex);
    // El coste es el de la imagen DECODIFICADA, que es lo que de verdad
    // ocupa en RAM, no el del PNG del que salio.
    m_missing.remove(key);          // ya no falta: acaba de llegar
    if (pinned) {
        m_cache.remove(key);        // que no quede duplicada en las dos areas
        m_pinned.insert(key, new QImage(image), costInKiB(image));
    } else if (!m_pinned.contains(key)) {
        m_cache.insert(key, new QImage(image), costInKiB(image));
    }
    ++m_stats.inserts;
}

void TileCache::remove(const TileKey &key)
{
    QMutexLocker lock(&m_mutex);
    m_cache.remove(key);
    m_pinned.remove(key);
    m_missing.remove(key);
}

void TileCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_pinned.clear();
    m_missing.clear();
}

int TileCache::count() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_cache.size()) + static_cast<int>(m_pinned.size());
}

int TileCache::pinnedCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_pinned.size());
}

qint64 TileCache::usedBytes() const
{
    QMutexLocker lock(&m_mutex);
    return (static_cast<qint64>(m_cache.totalCost())
            + static_cast<qint64>(m_pinned.totalCost())) * 1024;
}

qint64 TileCache::pinnedBytes() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<qint64>(m_pinned.maxCost()) * 1024;
}

void TileCache::setPinnedBytes(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    m_pinned.setMaxCost(limitInKiB(bytes));
}

qint64 TileCache::maxBytes() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<qint64>(m_cache.maxCost()) * 1024;
}

void TileCache::setMaxBytes(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    m_cache.setMaxCost(limitInKiB(bytes));
}

TileCache::Stats TileCache::stats() const
{
    QMutexLocker lock(&m_mutex);
    return m_stats;
}

void TileCache::resetStats()
{
    QMutexLocker lock(&m_mutex);
    m_stats = Stats{};
}

} // namespace libmapa
