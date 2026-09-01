#include "tiles/TileService.h"

#include "core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSet>
#include <algorithm>

namespace libmapa {

namespace {
//! A partir de esta profundidad, el ancestro se amplia 2^d veces y deja de
//! parecer un mapa: 3 niveles son 8x, 5 niveles 32x. Por encima conviene
//! traer un nivel intermedio aunque el hueco este formalmente cubierto.
constexpr int kMaxAcceptableDepth = 3;
} // namespace

TileService::TileService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<LoadedTile>("libmapa::LoadedTile");
    qRegisterMetaType<QVector<LoadedTile>>("QVector<libmapa::LoadedTile>");
    qRegisterMetaType<TileDataset>("libmapa::TileDataset");
    qRegisterMetaType<TileRequest>("libmapa::TileRequest");
    qRegisterMetaType<TileMatrix::TileRange>("libmapa::TileMatrix::TileRange");
    qRegisterMetaType<QVector<TileDataset>>("QVector<libmapa::TileDataset>");

    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, &TileService::flushPendingRequest);
}

TileService::~TileService()
{
    if (m_thread.isRunning()) {
        // Las conexiones SQLite se cierran DENTRO del hilo que las abrio.
        QMetaObject::invokeMethod(m_loader, "releaseResources",
                                  Qt::BlockingQueuedConnection);
        m_thread.quit();
        if (!m_thread.wait(5000)) {
            qCWarning(lcMapaTiles) << "El hilo de teselas no termino a tiempo";
            m_thread.terminate();
            m_thread.wait(1000);
        }
    }
}

bool TileService::start(const QVector<TileDataset> &datasets, int cacheMiB)
{
    if (m_started) {
        qCWarning(lcMapaTiles) << "TileService ya estaba arrancado";
        return true;
    }

    QVector<TileDataset> valid;
    for (const TileDataset &d : datasets) {
        if (!d.isValid()) {
            qCWarning(lcMapaTiles) << "Dataset invalido, se ignora:" << d.id;
            continue;
        }
        if (!QFileInfo::exists(d.filePath)) {
            qCWarning(lcMapaTiles) << "No existe el fichero de" << d.id
                                   << ":" << d.filePath;
            continue;
        }
        m_datasets.insert(d.id, d);
        valid.append(d);
    }

    if (valid.isEmpty()) {
        emit errorOccurred(tr("No hay ninguna base de datos de mapas utilizable."));
        return false;
    }

    // El presupuesto se reparte entre los datasets: cada uno tiene su propia
    // cache y no queremos multiplicar el consumo por el numero de capas.
    const qint64 bytes = static_cast<qint64>(qMax(1, cacheMiB)) * 1024 * 1024;
    m_cacheBytesPerDataset =
        qMax<qint64>(16LL * 1024 * 1024, bytes / qMax(1, valid.size()));

    m_loader = new TileLoader;          // sin padre: se mueve de hilo
    m_loader->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_loader, &QObject::deleteLater);
    connect(m_loader, &TileLoader::tilesLoaded, this, &TileService::onTilesLoaded);
    connect(m_loader, &TileLoader::loadFailed, this, &TileService::onLoadFailed);

    m_thread.setObjectName(QStringLiteral("libmapa-tiles"));
    m_thread.start();

    QMetaObject::invokeMethod(m_loader, "configure", Qt::QueuedConnection,
                              Q_ARG(QVector<libmapa::TileDataset>, valid));

    m_activeId = valid.first().id;
    m_planner.setTileSize(valid.first().tileSize);
    m_matrix = TileMatrix(valid.first().tileSize);
    m_started = true;

    qCInfo(lcMapaTiles) << "TileService arrancado con" << valid.size()
                        << "dataset(s); capa activa:" << m_activeId;
    return true;
}

QVector<TileDataset> TileService::loadDatasets(const QString &jsonPath,
                                               QString *error)
{
    QVector<TileDataset> out;

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("No se pudo abrir %1").arg(jsonPath);
        return out;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        if (error) *error = QStringLiteral("JSON invalido en %1: %2")
                                .arg(jsonPath, parseError.errorString());
        return out;
    }

    const QDir baseDir = QFileInfo(jsonPath).absoluteDir();
    const QJsonArray array = doc.object().value(QStringLiteral("datasets")).toArray();

    for (const QJsonValue &v : array) {
        TileDataset d = TileDataset::fromJson(v.toObject());
        // Las rutas relativas se resuelven respecto al propio JSON, para que
        // el fichero se pueda mover junto a los datos.
        if (!d.filePath.isEmpty() && QFileInfo(d.filePath).isRelative())
            d.filePath = baseDir.absoluteFilePath(d.filePath);
        if (d.isValid())
            out.append(d);
    }

    if (out.isEmpty() && error)
        *error = QStringLiteral("%1 no contiene datasets validos").arg(jsonPath);

    return out;
}

/*!
 * Cache de un dataset concreto, creandola la primera vez.
 *
 * Hay una por capa, y no es un lujo: TileKey solo lleva z/x/y, y las dos BD
 * usan los MISMOS indices logicos para la misma geografia. Con una cache
 * compartida, al cambiar de capa se veian teselas de la otra mezcladas con
 * las nuevas. El README llego a afirmar que compartirla era seguro; no lo era.
 */
TileCache &TileService::cacheFor(const QString &datasetId)
{
    auto it = m_caches.find(datasetId);
    if (it == m_caches.end()) {
        // Una cuarta parte se reserva para los niveles gruesos de respaldo,
        // que son pocos y no deben ser expulsados por la avalancha de detalle.
        auto c = std::make_unique<TileCache>(
            m_cacheBytesPerDataset,
            qMax<qint64>(4LL * 1024 * 1024, m_cacheBytesPerDataset / 4));
        it = m_caches.emplace(datasetId, std::move(c)).first;
    }
    return *it->second;
}

TileCache &TileService::cache()
{
    return cacheFor(m_activeId);
}

const TileCache &TileService::cache() const
{
    return const_cast<TileService *>(this)->cacheFor(m_activeId);
}

QStringList TileService::datasetIds() const
{
    return m_datasets.keys();
}

const TileDataset *TileService::dataset(const QString &id) const
{
    auto it = m_datasets.constFind(id);
    return it == m_datasets.constEnd() ? nullptr : &it.value();
}

const TileDataset *TileService::activeDataset() const
{
    return dataset(m_activeId);
}

bool TileService::setActiveDataset(const QString &id)
{
    if (id == m_activeId)
        return true;

    const TileDataset *ds = dataset(id);
    if (!ds) {
        emit errorOccurred(tr("Capa base no disponible: %1").arg(id));
        return false;
    }

    m_activeId = id;
    m_planner.setTileSize(ds->tileSize);
    m_matrix = TileMatrix(ds->tileSize);

    // No hay nada que vaciar: cada capa tiene su propia cache, asi que volver
    // a la anterior es instantaneo y no hay forma de que se mezclen. Lo que si
    // se hace es descartar la peticion en vuelo, que era de la capa anterior.
    m_hasPending = false;
    m_debounce.stop();

    qCInfo(lcMapaTiles) << "Capa base activa:" << id;
    emit activeDatasetChanged(id);
    return true;
}

int TileService::effectiveFallbackLevels() const
{
    if (m_fallbackLevels >= 0)
        return m_fallbackLevels;          // fijado a mano

    const TileDataset *ds = activeDataset();
    if (!ds)
        return 0;

    // Cuanto mas hueco tenga la BD, mas escalera hace falta. Los umbrales
    // salen de las dos BD reales: OSM al 100 % no necesita ninguna, la
    // satelital al 32 % necesita bajar hasta encontrar un nivel denso.
    if (ds->typicalFill >= 0.95) return 1;   // solo para cubrir la latencia
    if (ds->typicalFill >= 0.60) return 2;
    return 3;
}

quint64 TileService::requestViewport(const QGeoCoordinate &northWest,
                                     const QGeoCoordinate &southEast,
                                     int zoom,
                                     int marginTiles)
{
    const TileDataset *ds = activeDataset();
    if (!ds || !m_started)
        return 0;

    // El zoom se recorta al rango del dataset. Esto sustituye al parche
    // "if (ZoomValido == 17) ZoomValido = 16;" del codigo original.
    const int z = qBound(ds->minZoom, zoom, ds->maxZoom);

    const auto target = TileMatrix::rangeFor(northWest, southEast, z, marginTiles);
    if (target.isEmpty())
        return 0;

    const quint64 id = m_nextRequestId++;

    TileRequest req;
    req.datasetId = m_activeId;
    req.id = id;

    // Carga progresiva por escalera: se piden primero varios niveles gruesos,
    // del mas grueso al mas fino, y por ultimo el de detalle.
    //
    // Un solo nivel de respaldo no basta cuando ese nivel tambien tiene
    // huecos, que es justo el caso de la capa satelital. Con la escalera,
    // el planificador siempre encuentra algun ancestro con imagen.
    const int levels = effectiveFallbackLevels();

    QVector<int> baseZooms;

    // 1. Nivel de FONDO GARANTIZADO. Estas BD solo guardan teselas donde hay
    //    tierra, salvo en los niveles muy gruesos: sobre mar abierto es el
    //    unico que existe. Cuesta una o dos teselas y se queda anclado.
    const int fondoZ = ds->effectiveBaseZoom();
    if (fondoZ < z)
        baseZooms.append(fondoZ);

    // 2. Peldano intermedio mas grueso. Da cobertura inmediata con detalle
    //    razonable mientras llega el nivel pedido: en la satelital real, z10
    //    tardo 75 ms frente a los 485 ms del detalle. Los peldanos restantes
    //    se piden despues, y solo si al llegar el detalle quedan huecos.
    if (levels > 0) {
        const int coarsest = qMax(ds->minZoom, z - levels * m_fallbackStep);
        if (coarsest < z && !baseZooms.contains(coarsest))
            baseZooms.append(coarsest);
    }

    std::sort(baseZooms.begin(), baseZooms.end());   // del mas grueso al mas fino

    m_targetZoom = z;
    m_lastNorthWest = northWest;
    m_lastSouthEast = southEast;
    m_lastMargin = marginTiles;

    // Una rejilla que ya esta entera en cache no se vuelve a pedir. Sin
    // esto, cada movimiento del mapa re-leia y re-decodificaba la escalera,
    // que por definicion cambia poco.
    auto alreadyCached = [this](const TileMatrix::TileRange &r) {
        return !r.isEmpty()
               && cache().countCached(r.z, r.xMin, r.xMax, r.yMin, r.yMax)
                      == r.count();
    };

    for (int bz : baseZooms) {
        const auto base = TileMatrix::rangeFor(northWest, southEast, bz,
                                               marginTiles);
        if (!base.isEmpty() && !alreadyCached(base))
            req.ranges.append(base);
    }
    if (!alreadyCached(target))
        req.ranges.append(target);

    if (req.ranges.isEmpty()) {
        // Todo estaba ya en memoria: no hay nada que leer.
        qCDebug(lcMapaTiles) << "Viewport ya en cache; no se pide nada";
        return 0;
    }

    m_pending = req;
    m_hasPending = true;

    // Todo lo anterior queda obsoleto en cuanto se pide algo nuevo. Se avisa
    // ya, sin esperar al retardo, para que el hilo trabajador pueda abandonar
    // cuanto antes lo que este haciendo.
    m_loader->invalidateBefore(id);

    if (m_debounceMs > 0)
        m_debounce.start(m_debounceMs);
    else
        flushPendingRequest();

    return id;
}

void TileService::flushPendingRequest()
{
    if (!m_hasPending || !m_loader)
        return;

    const TileRequest req = m_pending;
    m_hasPending = false;

    QMetaObject::invokeMethod(m_loader, "load", Qt::QueuedConnection,
                              Q_ARG(libmapa::TileRequest, req));
}

void TileService::onTilesLoaded(quint64 requestId, const QString &datasetId,
                                const TileMatrix::TileRange &range,
                                const QVector<LoadedTile> &tiles)
{
    // Las teselas van a la cache de SU dataset, no a la de la capa activa: si
    // el usuario cambio de capa mientras se leian, guardarlas en la cache
    // equivocada las mostraria sobre el mapa que no toca.
    TileCache &destino = cacheFor(datasetId);

    // Los niveles por debajo del pedido son la escalera de respaldo: van al
    // area protegida para que el detalle no los expulse.
    const bool pinned = (range.z < m_targetZoom);

    QSet<TileKey> arrived;
    arrived.reserve(tiles.size());

    int added = 0;
    for (const LoadedTile &t : tiles) {
        destino.insertImage(t.key, t.image, pinned);
        arrived.insert(t.key);
        ++added;
    }

    // Lo que se pidio y no vino, no existe en la BD. Anotarlo evita volver a
    // consultarlo en cada movimiento del mapa: en la capa satelital son dos
    // tercios de cada pantalla.
    int missing = 0;
    for (int x = range.xMin; x <= range.xMax; ++x) {
        for (int y = range.yMin; y <= range.yMax; ++y) {
            const TileKey k{range.z, x, y};
            if (!arrived.contains(k)) {
                destino.markMissing(k);
                ++missing;
            }
        }
    }

    if (missing > 0) {
        qCDebug(lcMapaTiles) << "z" << range.z << ":" << added << "teselas,"
                             << missing << "inexistentes anotadas";
    }

    emit tilesReady(requestId, range.z, added);

    // Con el detalle ya en memoria se puede saber si de verdad quedan huecos,
    // en vez de suponerlo por el relleno medio de la BD.
    if (range.z == m_targetZoom && requestId != m_repairedFor) {
        m_repairedFor = requestId;
        repairGapsIfNeeded();
    }
}

void TileService::repairGapsIfNeeded()
{
    const TileDataset *ds = activeDataset();
    if (!ds || !m_lastNorthWest.isValid() || !m_lastSouthEast.isValid())
        return;

    const auto plan = planFor(m_lastNorthWest, m_lastSouthEast,
                              m_targetZoom, m_lastMargin);

    // Criterio de reparacion: no basta con mirar si quedan huecos VACIOS.
    // Un hueco tapado con un ancestro 6 niveles arriba se amplia 64 veces y
    // se ve como un color plano; formalmente esta cubierto, pero no es un
    // mapa. Se cuenta tambien ese respaldo de mala calidad.
    int borrosos = 0;
    for (const TileDrawItem &i : plan.items)
        if (!i.isExact() && i.ancestorDepth >= kMaxAcceptableDepth)
            ++borrosos;

    if (plan.emptyCount == 0 && borrosos == 0)
        return;   // el respaldo disponible ya es de calidad suficiente

    // Si ni siquiera el nivel de fondo tiene teselas ahi, es mar abierto
    // fuera de lo que cubre la BD. Pedir peldanos intermedios seria pedir
    // vacio sobre vacio: todos van a estar igual de ausentes.
    const auto fondo = TileMatrix::rangeFor(m_lastNorthWest, m_lastSouthEast,
                                            ds->effectiveBaseZoom(), 0);
    if (!fondo.isEmpty()
        && cache().countCached(fondo.z, fondo.xMin, fondo.xMax,
                               fondo.yMin, fondo.yMax) == fondo.count()) {
        bool hayAlgunFondo = false;
        for (int x = fondo.xMin; x <= fondo.xMax && !hayAlgunFondo; ++x)
            for (int y = fondo.yMin; y <= fondo.yMax && !hayAlgunFondo; ++y)
                if (cache().contains(TileKey{fondo.z, x, y}))
                    hayAlgunFondo = true;

        if (!hayAlgunFondo) {
            qCDebug(lcMapaTiles) << "Zona sin cobertura en la BD (mar abierto"
                                 << "fuera del area); no se piden mas niveles";
            return;
        }
    }

    // Quedan huecos de verdad: ahora si merece la pena traer los peldanos
    // intermedios, de mas grueso a mas fino.
    const int levels = effectiveFallbackLevels();
    TileRequest req;
    req.datasetId = m_activeId;
    req.id = m_nextRequestId++;

    for (int i = levels - 1; i >= 1; --i) {
        const int bz = qMax(ds->minZoom, m_targetZoom - i * m_fallbackStep);
        if (bz >= m_targetZoom)
            continue;
        const auto r = TileMatrix::rangeFor(m_lastNorthWest, m_lastSouthEast,
                                            bz, m_lastMargin);
        if (r.isEmpty())
            continue;
        if (cache().countCached(r.z, r.xMin, r.xMax, r.yMin, r.yMax) == r.count())
            continue;
        req.ranges.append(r);
    }

    if (req.ranges.isEmpty())
        return;

    qCDebug(lcMapaTiles) << "Quedan" << plan.emptyCount << "huecos y"
                         << borrosos << "respaldos demasiado ampliados;"
                         << "se piden" << req.ranges.size()
                         << "peldanos intermedios";

    m_loader->invalidateBefore(req.id);
    QMetaObject::invokeMethod(m_loader, "load", Qt::QueuedConnection,
                              Q_ARG(libmapa::TileRequest, req));
}

void TileService::onLoadFailed(quint64 requestId, const QString &message)
{
    qCWarning(lcMapaTiles) << "Peticion" << requestId << "fallo:" << message;
    emit errorOccurred(message);
}

TilePlanner::Plan TileService::planFor(const QGeoCoordinate &northWest,
                                       const QGeoCoordinate &southEast,
                                       int zoom,
                                       int marginTiles) const
{
    const TileDataset *ds = activeDataset();
    if (!ds)
        return {};

    const int z = qBound(ds->minZoom, zoom, ds->maxZoom);
    const auto range = TileMatrix::rangeFor(northWest, southEast, z, marginTiles);

    // La profundidad maxima de busqueda tiene que llegar al peldano mas
    // grueso de la escalera; si no, el nivel que se acaba de precargar
    // quedaria fuera de alcance y el respaldo no serviria de nada.
    // OJO: hay que usar los peldanos EFECTIVOS, no m_fallbackLevels, que
    // vale -1 en modo automatico. Con -1 el calculo daba qMax(5, -1) = 5 y
    // el peldano mas grueso de la escalera quedaba fuera de alcance: la
    // cobertura de la capa satelital caia del 100 % al 93.3 %.
    // La busqueda de ancestros tiene que poder llegar hasta el nivel de fondo
    // garantizado. Con el tope anterior de 7 niveles, desde z15 solo se
    // alcanzaba el z8; y en la satelital los unicos niveles con teselas de mar
    // son del 1 al 5, asi que sobre mar abierto la pantalla quedaba vacia.
    const int maxDepth = qMax(z - ds->effectiveBaseZoom(), 1);

    return m_planner.plan(range, cache(), ds->effectiveBaseZoom(), maxDepth);
}

int TileService::bestZoomFor(double spanLongitudeDeg, int viewportWidthPx) const
{
    const TileDataset *ds = activeDataset();
    if (!ds)
        return 0;

    // Se usa recommendedMaxZoom, no maxZoom: los niveles finales de ambas BD
    // estan a medio poblar y llevar al usuario hasta ahi solo produce una
    // pantalla resuelta casi entera por respaldo de tesela padre.
    return m_matrix.bestZoomFor(spanLongitudeDeg, viewportWidthPx,
                                ds->minZoom, ds->recommendedMaxZoom);
}

} // namespace libmapa
