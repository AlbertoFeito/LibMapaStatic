#include "tiles/TileLoader.h"

#include "core/Logging.h"
#include "db/SqliteConnectionPool.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>

namespace libmapa {

TileLoader::TileLoader(QObject *parent)
    : QObject(parent)
{
}

TileLoader::~TileLoader() = default;

// Marca "la peticion mas nueva": todo lo anterior queda obsoleto. Se llama desde
// el hilo de la interfaz cuando el usuario se mueve. Atomico y monotono.
void TileLoader::invalidateBefore(quint64 requestId)
{
    // Solo avanza; nunca retrocede aunque llegue algo fuera de orden.
    quint64 current = m_newestRequest.loadAcquire();
    while (requestId > current
           && !m_newestRequest.testAndSetOrdered(current, requestId)) {
        current = m_newestRequest.loadAcquire();
    }
}

// ¿Esta peticion ya quedo atras (llego una mas nueva)? Si es asi, no vale la pena
// seguir trabajando en ella.
bool TileLoader::isStale(quint64 requestId) const
{
    return requestId < m_newestRequest.loadAcquire();
}

// Registra los datasets disponibles (solo guarda descriptores; las fuentes/BD se
// abren perezosamente en el hilo trabajador con sourceFor()).
void TileLoader::configure(const QVector<TileDataset> &datasets)
{
    m_datasets.clear();
    m_sources.clear();
    for (const TileDataset &d : datasets) {
        if (!d.isValid()) {
            qCWarning(lcMapaTiles) << "Dataset invalido, se ignora:" << d.id;
            continue;
        }
        m_datasets.insert(d.id, d);
    }
    qCDebug(lcMapaTiles) << "TileLoader configurado con"
                         << m_datasets.size() << "dataset(s)";
}

// Devuelve (creando y abriendo la primera vez) la fuente de un dataset. Se hace
// EN ESTE hilo trabajador porque la conexion SQLite pertenece a quien la abre.
RMapsTileSource *TileLoader::sourceFor(const QString &datasetId)
{
    auto it = m_sources.find(datasetId);
    if (it != m_sources.end())
        return it->get();

    auto dsIt = m_datasets.constFind(datasetId);
    if (dsIt == m_datasets.constEnd())
        return nullptr;

    // La fuente se crea y se abre en ESTE hilo, que es el trabajador: la
    // conexion SQLite pertenece a quien la abre.
    auto source = std::make_shared<RMapsTileSource>(dsIt.value());
    if (!source->open()) {
        qCCritical(lcMapaTiles) << "No se pudo abrir la fuente" << datasetId
                                << "-" << source->lastError();
        return nullptr;
    }

    m_sources.insert(datasetId, source);
    return source.get();
}

// Atiende UNA peticion (varias rejillas, de gruesa a fina): lee cada rango de la
// BD y decodifica las imagenes, emitiendo tilesLoaded por rango. Comprueba a
// menudo si la peticion quedo obsoleta (isStale) para abandonar cuanto antes si
// el usuario ya se movio. Corre en el hilo trabajador.
void TileLoader::load(const TileRequest &request)
{
    // Comprobacion previa: si el usuario ya movio el mapa, ni se abre la BD.
    if (isStale(request.id)) {
        emit requestSkipped(request.id);
        return;
    }

    RMapsTileSource *source = sourceFor(request.datasetId);
    if (!source) {
        emit loadFailed(request.id,
                        QStringLiteral("Dataset no disponible: %1")
                            .arg(request.datasetId));
        return;
    }

    // Las rejillas se sirven EN ORDEN. La primera suele ser el nivel grueso,
    // que llega enseguida y ya deja la pantalla cubierta por respaldo.
    for (const TileMatrix::TileRange &range : request.ranges) {
        if (range.isEmpty())
            continue;

        if (isStale(request.id)) {
            emit requestSkipped(request.id);
            return;
        }

        QElapsedTimer timer;
        timer.start();

        const auto raw = source->fetchRange(range.z, range.xMin, range.xMax,
                                            range.yMin, range.yMax);
        const qint64 msFetch = timer.elapsed();

        // La consulta puede haber tardado y entretanto haber llegado una
        // peticion nueva. Decodificar ahora seria tirar el tiempo.
        if (isStale(request.id)) {
            emit requestSkipped(request.id);
            return;
        }

        QVector<LoadedTile> loaded;
        loaded.reserve(raw.size());

        timer.restart();
        int failed = 0, checkCounter = 0;

        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
            // Cada 16 teselas se vuelve a mirar si la peticion sigue viva.
            // Con rejillas grandes, abandonar a mitad ahorra mucho trabajo.
            if (++checkCounter % 16 == 0 && isStale(request.id)) {
                emit requestSkipped(request.id);
                return;
            }

            QBuffer buffer;
            buffer.setData(it.value());
            if (!buffer.open(QIODevice::ReadOnly)) {
                ++failed;
                continue;
            }

            QImageReader reader(&buffer);
            QImage image = reader.read();
            if (image.isNull()) {
                ++failed;
                qCWarning(lcMapaTiles) << "Tesela no decodificable"
                                       << request.datasetId
                                       << it.key().z << it.key().x << it.key().y
                                       << "-" << reader.errorString();
                continue;
            }

            loaded.append(LoadedTile{it.key(), image});
        }

        const qint64 msDecode = timer.elapsed();

        qCDebug(lcMapaTiles) << request.datasetId << "z" << range.z
                             << "peticion" << request.id
                             << ":" << loaded.size() << "teselas"
                             << "(" << failed << "fallos)"
                             << "lectura" << msFetch << "ms,"
                             << "decodificacion" << msDecode << "ms";

        if (isStale(request.id)) {
            emit requestSkipped(request.id);
            return;
        }

        emit tilesLoaded(request.id, request.datasetId, range, loaded);
    }
}

// Suelta fuentes y conexiones del hilo trabajador (al parar el servicio). El
// orden importa: las QSqlQuery de las fuentes deben morir antes que la conexion.
void TileLoader::releaseResources()
{
    // Las fuentes primero: sus QSqlQuery deben morir antes que la conexion.
    m_sources.clear();
    SqliteConnectionPool::closeAllForCurrentThread();
    qCDebug(lcMapaTiles) << "TileLoader libero sus recursos";
}

} // namespace libmapa
