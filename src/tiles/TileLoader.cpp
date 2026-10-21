#include "tiles/TileLoader.h"

#include "core/Logging.h"
#include "db/SqliteConnectionPool.h"
#include "vector/VectorTileRenderer.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <functional>
#include <utility>
#include <vector>

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
    m_renderers.clear();
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

// Anade un dataset en caliente. A diferencia de configure(), no vacia las
// fuentes ya abiertas: las capas que se estan usando siguen funcionando.
void TileLoader::addDataset(const TileDataset &dataset)
{
    if (!dataset.isValid()) {
        qCWarning(lcMapaTiles) << "Dataset invalido, se ignora:" << dataset.id;
        return;
    }
    m_datasets.insert(dataset.id, dataset);
    m_sources.remove(dataset.id);          // por si se reutiliza un id: se reabre
    m_renderers.remove(dataset.id);
    qCDebug(lcMapaTiles) << "TileLoader: dataset anadido" << dataset.id;
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

// Devuelve (creandolo la primera vez) el renderizador de un dataset vectorial,
// con el estilo de su vectorStyleFile o, si no tiene o no se puede leer, el de
// por defecto (OpenMapTiles para una base, el hibrido para un overlay). Un
// estilo roto se avisa una vez y no deja la capa en blanco.
VectorTileRenderer *TileLoader::rendererFor(const TileDataset &dataset)
{
    auto it = m_renderers.find(dataset.id);
    if (it != m_renderers.end())
        return it->get();

    VectorStyle style = VectorStyle::defaultFor(dataset.vectorOverlay);
    if (!dataset.vectorStyleFile.isEmpty()) {
        QString error;
        VectorStyle custom;
        if (VectorStyle::fromFile(dataset.vectorStyleFile, &custom, &error))
            style = custom;
        else
            qCWarning(lcMapaTiles) << "Estilo vectorial no valido para" << dataset.id
                                   << "- se usa el de por defecto:" << error;
    }
    auto renderer = std::make_shared<VectorTileRenderer>(style);
    renderer->setHiddenGroups(QSet<QString>(dataset.vectorHiddenGroups.begin(),
                                            dataset.vectorHiddenGroups.end()));
    // Mascara de tierra (para pintar el mar). Si no se puede leer, se avisa y la
    // capa se ve como sin ella: el mar del color de fondo.
    if (!dataset.vectorLandMask.isEmpty() && !dataset.vectorOverlay) {
        QString error;
        auto mask = LandMask::fromFile(dataset.vectorLandMask, dataset.vectorLandLayer, &error);
        if (mask)
            qCInfo(lcMapaTiles) << "Mascara de tierra para" << dataset.id << ":"
                                << mask->ringCount() << "anillos," << mask->vertexCount() << "vertices";
        else
            qCWarning(lcMapaTiles) << "Mascara de tierra no valida para" << dataset.id << "-" << error;
        renderer->setLandMask(mask);
    }
    m_renderers.insert(dataset.id, renderer);
    return renderer.get();
}

namespace {

/*!
 * Trabajo de pintado de teselas vectoriales: cada hilo va tomando la siguiente
 * tesela libre (contador atomico) hasta acabarlas o hasta que la peticion quede
 * obsoleta. Cada uno escribe solo en su casilla de \c out, asi que no hace falta
 * cerrojo. QRunnable y no una lambda: QThreadPool::start(std::function) no existe
 * en Qt 5.14.
 */
class VectorRenderJob : public QRunnable
{
public:
    // Solo guarda referencias: todo vive en renderVector mientras dura el trabajo.
    VectorRenderJob(const std::vector<std::pair<TileKey, QByteArray>> &in,
                    std::vector<QImage> &out, QAtomicInt &next,
                    const VectorTileRenderer &renderer, int size,
                    const std::function<bool()> &stale)
        : m_in(in), m_out(out), m_next(next), m_renderer(renderer), m_size(size), m_stale(stale)
    {
        setAutoDelete(true);
    }

    // Pinta teselas hasta que no quede ninguna o la peticion caduque.
    void run() override
    {
        for (;;) {
            const int i = m_next.fetchAndAddRelaxed(1);
            if (i >= int(m_in.size()) || m_stale())
                return;
            const auto &item = m_in[size_t(i)];
            m_out[size_t(i)] = m_renderer.render(item.second, item.first, m_size);
        }
    }

private:
    const std::vector<std::pair<TileKey, QByteArray>> &m_in;
    std::vector<QImage> &m_out;
    QAtomicInt &m_next;
    const VectorTileRenderer &m_renderer;
    int m_size;
    const std::function<bool()> &m_stale;
};

} // namespace

// Pinta las teselas vectoriales de una rejilla repartiendolas entre este hilo y
// los de un pool propio (no el global: la aplicacion puede estar usandolo). Una
// tesela vectorial cuesta decenas de milisegundos (miles de edificios en z14),
// mucho mas que descomprimir un PNG, y en serie una pantalla tardaria segundos.
// Devuelve false si la peticion quedo obsoleta a medias.
bool TileLoader::renderVector(const QHash<TileKey, QByteArray> &raw, const TileDataset &ds,
                              const VectorTileRenderer &renderer, quint64 requestId,
                              QVector<LoadedTile> *loaded, int *failed)
{
    std::vector<std::pair<TileKey, QByteArray>> in;
    in.reserve(size_t(raw.size()));
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
        in.emplace_back(it.key(), it.value());
    std::vector<QImage> out(in.size());

    if (!m_renderPool) {
        m_renderPool = std::make_unique<QThreadPool>();
        // Se deja un nucleo libre para la interfaz (y este hilo tambien pinta).
        m_renderPool->setMaxThreadCount(qMax(1, QThread::idealThreadCount() - 2));
    }

    QAtomicInt next(0);
    const std::function<bool()> stale = [this, requestId] { return isStale(requestId); };
    const int helpers = qMin(m_renderPool->maxThreadCount(), int(in.size()) - 1);
    for (int i = 0; i < helpers; ++i)
        m_renderPool->start(new VectorRenderJob(in, out, next, renderer, ds.tileSize, stale));
    VectorRenderJob(in, out, next, renderer, ds.tileSize, stale).run();
    m_renderPool->waitForDone();

    if (isStale(requestId))
        return false;
    for (size_t i = 0; i < in.size(); ++i) {
        if (out[i].isNull()) {
            ++*failed;
            qCWarning(lcMapaTiles) << "Tesela vectorial no decodificable" << ds.id
                                   << in[i].first.z << in[i].first.x << in[i].first.y;
            continue;
        }
        loaded->append(LoadedTile{ in[i].first, out[i] });
    }
    return true;
}

// Cambia los grupos ocultos de una capa vectorial. No se toca el renderizador en
// uso (otros hilos del pool podrian estar pintando con el): se pone uno NUEVO,
// copia del anterior con el estilo y la mascara ya cargados, y el viejo muere
// cuando nadie lo usa. Corre en el hilo trabajador, entre peticion y peticion.
void TileLoader::setVectorHiddenGroups(const QString &datasetId, const QStringList &groups)
{
    auto ds = m_datasets.find(datasetId);
    if (ds == m_datasets.end())
        return;
    ds->vectorHiddenGroups = groups;
    auto it = m_renderers.find(datasetId);
    if (it == m_renderers.end())
        return;                           // se creara con los grupos nuevos
    auto renderer = std::make_shared<VectorTileRenderer>(*it->get());
    renderer->setHiddenGroups(QSet<QString>(groups.begin(), groups.end()));
    it.value() = renderer;
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

    const TileDataset &ds = source->dataset();
    VectorTileRenderer *vectorRenderer = ds.vector ? rendererFor(ds) : nullptr;

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

        // Vectorial: se pinta con el estilo, en paralelo, en vez de decodificar
        // imagenes una a una.
        if (vectorRenderer) {
            if (!renderVector(raw, ds, *vectorRenderer, request.id, &loaded, &failed)) {
                emit requestSkipped(request.id);
                return;
            }
        }

        for (auto it = raw.constBegin(); !vectorRenderer && it != raw.constEnd(); ++it) {
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
    m_renderers.clear();
    m_renderPool.reset();                  // espera a sus hilos y los suelta aqui
    SqliteConnectionPool::closeAllForCurrentThread();
    qCDebug(lcMapaTiles) << "TileLoader libero sus recursos";
}

} // namespace libmapa
