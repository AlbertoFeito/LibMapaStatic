#include "libmapa/MapWidget.h"

#include "core/Logging.h"
#include "geo/TileMatrix.h"
#include "tiles/RMapsTileSource.h"
#include "tiles/TileService.h"
#include "db/VectorRepository.h"
#include "widget/CoverageLayer.h"
#include "widget/MapView.h"

#include <QLayout>
#include <QVBoxLayout>

namespace libmapa {

/*!
 * \brief Todo lo que el cliente no debe ver.
 *
 * QCustomPlot, TileService, el hilo de carga y el estado interno viven aqui.
 * MapWidget.h no incluye ninguno de ellos.
 */
class MapWidget::Impl
{
public:
    explicit Impl(MapWidget *owner) : q(owner) {}

    /*!
     * \brief Asegura que la vista tenga el tamano del widget AHORA MISMO.
     *
     * Qt entrega los resizeEvent de forma diferida y los layouts no reparten
     * geometria mientras el widget esta oculto. Un cliente que haga
     *
     *     MapWidget m(cfg);
     *     m.resize(800, 600);
     *     m.fitBounds(no, se);          // <- sin volver al bucle de eventos
     *
     * encontraria la vista todavia con su tamano por defecto: el area de
     * dibujo medía 100x30 en vez de 800x600, y el zoom calculado salia mal.
     *
     * Sincronizar la geometria antes de cada operacion que dependa del tamano
     * cuesta una comparacion y elimina la dependencia del orden de eventos.
     */
    void syncGeometry()
    {
        if (!view)
            return;

        const QRect destino = q->rect();
        if (destino.width() <= 1 || destino.height() <= 1)
            return;

        if (view->geometry() == destino && view->viewport() == destino)
            return;                       // caso normal: nada que hacer

        if (layout)
            layout->activate();
        if (view->geometry() != destino)
            view->setGeometry(destino);
        view->ensureLayout();

        // El area de dibujo cambio: hay que rehacer el encuadre y volver a
        // pedir teselas para el viewport nuevo.
        view->setCenter(view->center());
    }

    MapWidget *q = nullptr;
    MapConfig config;
    QVBoxLayout *layout = nullptr;
    TileService service;
    MapView *view = nullptr;
    QString error;
    bool ready = false;

    // Mancha de cobertura (diagnostico): zoom objetivo y si esta encendida.
    int coverageZoom = 14;
    bool coverageVisible = false;
};

// Construye el widget completo desde la configuracion: carga los datasets del
// datasets.json, arranca el TileService (con su hilo de carga), crea la MapView y
// reemite hacia el exterior las senales utiles del view, del servicio y de los
// modelos (zoom, centro, clics, entidades, capas, cambio de base...). Si algo
// falla deja d->ready en false y emite errorOccurred; el cliente debe comprobar
// isReady(). Casi todo lo demas de esta clase son reenvios finos a view/modelos.
MapWidget::MapWidget(const MapConfig &config, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Impl>(this))
{
    d->config = config;

    d->layout = new QVBoxLayout(this);
    d->layout->setContentsMargins(0, 0, 0, 0);
    d->layout->setSpacing(0);

    QString error;
    const auto datasets = TileService::loadDatasets(config.datasetsFile, &error);

    if (datasets.isEmpty()) {
        d->error = error.isEmpty()
                       ? tr("No se pudo cargar %1").arg(config.datasetsFile)
                       : error;
        qCCritical(lcMapaRender) << d->error;
        emit errorOccurred(d->error);
        return;
    }

    connect(&d->service, &TileService::errorOccurred,
            this, &MapWidget::errorOccurred);

    if (!d->service.start(datasets, config.cacheMiB)) {
        d->error = tr("No hay ninguna base de datos de mapas utilizable.");
        return;
    }
    d->service.setDebounceMs(config.debounceMs);

    if (!config.initialLayerId.isEmpty())
        d->service.setActiveDataset(config.initialLayerId);

    d->view = new MapView(&d->service, this);
    d->layout->addWidget(d->view);

    d->view->tileLayer()->setNoDataColor(config.noDataColor);

    connect(d->view, &MapView::zoomChanged, this, &MapWidget::zoomChanged);
    connect(d->view, &MapView::centerChanged, this, &MapWidget::centerChanged);
    connect(d->view, &MapView::mouseMovedTo, this, &MapWidget::mouseMoved);
    connect(d->view, &MapView::mapClicked, this, &MapWidget::clicked);
    connect(d->view, &MapView::measurementFinished,
            this, &MapWidget::measurementFinished);
    connect(d->view, &MapView::areaSelected, this, &MapWidget::areaSelected);
    connect(d->view, &MapView::polygonSelected, this, &MapWidget::polygonSelected);
    connect(d->view, &MapView::pointPicked, this, &MapWidget::pointPicked);

    connect(&d->service, &TileService::activeDatasetChanged,
            this, [this](const QString &id) {
                if (d->view) {
                    d->view->setZoom(d->view->zoom());   // recorta al rango nuevo
                    d->view->requestVisibleTiles();
                    d->view->refreshPlan();
                }
                emit baseLayerChanged(id);
            });

    OverlayModel *modelo = d->view->overlayModel();
    connect(modelo, &OverlayModel::featureAdded, this, &MapWidget::featureAdded);
    connect(modelo, &OverlayModel::featureUpdated, this, &MapWidget::featureUpdated);
    connect(modelo, &OverlayModel::featureRemoved, this, &MapWidget::featureRemoved);
    connect(modelo, &OverlayModel::selectionChanged, this, &MapWidget::featureSelected);
    connect(modelo, &OverlayModel::layersChanged, this, &MapWidget::featureLayersChanged);
    connect(d->view, &MapView::featureCreated, this, &MapWidget::featureCreated);
    connect(d->view, &MapView::drawingCancelled, this, &MapWidget::drawingCancelled);

    d->view->setZoom(config.initialZoom);
    d->view->setCenter(config.initialCenter);
    d->ready = true;
}

MapWidget::~MapWidget() = default;

// isReady: ¿el widget se inicializo con exito? lastError: el motivo si no.
bool MapWidget::isReady() const { return d->ready; }
QString MapWidget::lastError() const { return d->error; }

// Lista las capas base (datasets de teselas) disponibles con su nombre y rango de
// zoom. Expone el zoom RECOMENDADO, no el maximo real, porque los ultimos niveles
// estan a medio poblar y llevar al usuario ahi solo muestra respaldo escalado.
QVector<BaseLayerInfo> MapWidget::availableBaseLayers() const
{
    d->syncGeometry();
    QVector<BaseLayerInfo> out;
    for (const QString &id : d->service.datasetIds()) {
        const TileDataset *ds = d->service.dataset(id);
        if (!ds)
            continue;
        BaseLayerInfo info;
        info.id = ds->id;
        info.displayName = ds->displayName.isEmpty() ? ds->id : ds->displayName;
        info.minZoom = ds->minZoom;
        // Se expone el recomendado, no maxZoom: los ultimos niveles de ambas
        // BD estan a medio poblar y llevar al usuario ahi solo produce una
        // pantalla resuelta casi entera por respaldo.
        info.maxZoom = ds->recommendedMaxZoom;
        info.available = true;
        out.append(info);
    }
    return out;
}

// Id de la capa base activa.
QString MapWidget::baseLayerId() const
{
    return d->service.activeDatasetId();
}

// Cambia la capa base activa (satelital, osm, clarity...). false si el id no
// existe.
bool MapWidget::setBaseLayerId(const QString &id)
{
    d->syncGeometry();
    return d->service.setActiveDataset(id);
}

// Fuerza a releer las teselas de la BD: vacia la cache (incluidas las marcas de
// "esta tesela no existe") y vuelve a pedir el viewport. Util tras rellenar la
// base con fill_tiles/fill_map mientras el visor esta abierto.
void MapWidget::reloadBaseLayer()
{
    d->syncGeometry();
    d->service.cache().clear();      // olvida teselas y marcas de "no existe"
    d->view->requestVisibleTiles();  // vuelve a pedirlas a la BD
    d->view->refreshPlan();
}

// Oculta el recuadro de "seleccionar area" (herramienta SelectArea).
void MapWidget::clearAreaSelection()
{
    d->view->clearAreaSelection();
}

// ---------------------------------------------------------- navegacion --
// Reenvios a la MapView. Todos llaman antes a syncGeometry() para que el calculo
// use el tamano REAL del widget aunque aun no haya vuelto al bucle de eventos
// (ver Impl::syncGeometry), y caen a los valores de la config si no hay vista.

// Centro geografico actual de la vista.
QGeoCoordinate MapWidget::center() const
{
    d->syncGeometry();
    return d->view ? d->view->center() : d->config.initialCenter;
}

// Recentra el mapa sin cambiar el zoom.
void MapWidget::setCenter(const QGeoCoordinate &center)
{
    d->syncGeometry();
    if (d->view)
        d->view->setCenter(center);
}

// Nivel de zoom actual.
int MapWidget::zoom() const
{
    d->syncGeometry();
    return d->view ? d->view->zoom() : d->config.initialZoom;
}

// Fija el nivel de zoom (se acota al rango del dataset dentro de la vista).
void MapWidget::setZoom(int zoom)
{
    d->syncGeometry();
    if (d->view)
        d->view->setZoom(zoom);
}

// Zoom minimo del dataset activo.
int MapWidget::minZoom() const
{
    const TileDataset *ds = d->service.activeDataset();
    return ds ? ds->minZoom : 0;
}

// Zoom maximo RECOMENDADO del dataset activo (no el maximo real; ver
// availableBaseLayers).
int MapWidget::maxZoom() const
{
    const TileDataset *ds = d->service.activeDataset();
    return ds ? ds->recommendedMaxZoom : 18;
}

// Atajos de acercar/alejar un nivel.
void MapWidget::zoomIn()  { setZoom(zoom() + 1); }
void MapWidget::zoomOut() { setZoom(zoom() - 1); }

// Encuadra un rectangulo geografico (elige zoom y centro para que quepa).
void MapWidget::fitBounds(const QGeoCoordinate &northWest,
                          const QGeoCoordinate &southEast)
{
    d->syncGeometry();
    if (d->view)
        d->view->fitBounds(northWest, southEast);
}

// Esquina noroeste actualmente visible.
QGeoCoordinate MapWidget::visibleNorthWest() const
{
    d->syncGeometry();
    return d->view ? d->view->visibleNorthWest() : QGeoCoordinate();
}

// Esquina sureste actualmente visible.
QGeoCoordinate MapWidget::visibleSouthEast() const
{
    d->syncGeometry();
    return d->view ? d->view->visibleSouthEast() : QGeoCoordinate();
}

// ------------------------------------------------------ capas y entidades --
// Reenvios directos al OverlayModel de la vista (cada uno documentado en
// OverlayModel). Devuelven un valor neutro si aun no hay vista creada.

// Crea una capa de entidades.
bool MapWidget::addFeatureLayer(const QString &id, const QString &displayName,
                                int zOrder)
{
    return d->view ? d->view->overlayModel()->addLayer(id, displayName, zOrder)
                   : false;
}

// Borra una capa de entidades (y su contenido).
bool MapWidget::removeFeatureLayer(const QString &id)
{
    return d->view ? d->view->overlayModel()->removeLayer(id) : false;
}

// Lista las capas de entidades (con su contador).
QVector<LayerInfo> MapWidget::featureLayers() const
{
    return d->view ? d->view->overlayModel()->layers() : QVector<LayerInfo>();
}

// Muestra u oculta una capa de entidades.
bool MapWidget::setFeatureLayerVisible(const QString &id, bool visible)
{
    return d->view ? d->view->overlayModel()->setLayerVisible(id, visible)
                   : false;
}

// Cambia el orden de pintado de una capa.
bool MapWidget::setFeatureLayerZOrder(const QString &id, int zOrder)
{
    return d->view ? d->view->overlayModel()->setLayerZOrder(id, zOrder) : false;
}

// Anade una entidad; devuelve su id (o -1).
qint64 MapWidget::addFeature(const MapFeature &feature)
{
    return d->view ? d->view->overlayModel()->addFeature(feature) : -1;
}

// Reemplaza una entidad existente.
bool MapWidget::updateFeature(const MapFeature &feature)
{
    return d->view ? d->view->overlayModel()->updateFeature(feature) : false;
}

// Borra una entidad por id.
bool MapWidget::removeFeature(qint64 id)
{
    return d->view ? d->view->overlayModel()->removeFeature(id) : false;
}

// Vacia todas las entidades de una capa (conserva la capa).
void MapWidget::clearFeatureLayer(const QString &layerId)
{
    if (d->view)
        d->view->overlayModel()->clearLayer(layerId);
}

// Vacia TODAS las entidades de todas las capas.
void MapWidget::clearFeatures()
{
    if (d->view)
        d->view->overlayModel()->clear();
}

// Una entidad por id (nullopt si no existe).
std::optional<MapFeature> MapWidget::feature(qint64 id) const
{
    return d->view ? d->view->overlayModel()->feature(id)
                   : std::optional<MapFeature>();
}

// Todas las entidades.
QVector<MapFeature> MapWidget::features() const
{
    return d->view ? d->view->overlayModel()->features() : QVector<MapFeature>();
}

// Entidades de una capa concreta.
QVector<MapFeature> MapWidget::featuresInLayer(const QString &layerId) const
{
    return d->view ? d->view->overlayModel()->featuresInLayer(layerId)
                   : QVector<MapFeature>();
}

// Entidades de un tipo de dominio concreto.
QVector<MapFeature> MapWidget::featuresOfType(const QString &type) const
{
    return d->view ? d->view->overlayModel()->featuresOfType(type)
                   : QVector<MapFeature>();
}

// Numero total de entidades.
int MapWidget::featureCount() const
{
    return d->view ? d->view->overlayModel()->count() : 0;
}

// Mueve un vertice de una entidad.
bool MapWidget::moveVertex(qint64 id, int index, const QGeoCoordinate &to)
{
    return d->view ? d->view->overlayModel()->moveVertex(id, index, to) : false;
}

// Inserta un vertice en una entidad.
bool MapWidget::insertVertex(qint64 id, int index, const QGeoCoordinate &at)
{
    return d->view ? d->view->overlayModel()->insertVertex(id, index, at) : false;
}

// Elimina un vertice de una entidad (sin degenerarla).
bool MapWidget::removeVertex(qint64 id, int index)
{
    return d->view ? d->view->overlayModel()->removeVertex(id, index) : false;
}

// Desplaza una entidad entera un delta de lat/lon.
bool MapWidget::moveFeature(qint64 id, double dLat, double dLon)
{
    return d->view ? d->view->overlayModel()->moveFeature(id, dLat, dLon) : false;
}

// ¿Hay algo que deshacer?
bool MapWidget::canUndo() const
{
    return d->view && d->view->overlayModel()->canUndo();
}

// ¿Hay algo que rehacer?
bool MapWidget::canRedo() const
{
    return d->view && d->view->overlayModel()->canRedo();
}

// Deshace el ultimo cambio de entidades.
bool MapWidget::undo()
{
    return d->view && d->view->overlayModel()->undo();
}

// Rehace el ultimo cambio deshecho.
bool MapWidget::redo()
{
    return d->view && d->view->overlayModel()->redo();
}

// Vacia el historial de deshacer/rehacer.
void MapWidget::clearUndoHistory()
{
    if (d->view)
        d->view->overlayModel()->clearUndoHistory();
}

// Guarda TODAS las entidades y capas en una BD vectorial (vuelca el estado
// completo: borra y reescribe, en vez de llevar la cuenta de altas/bajas). false
// si no se pudo abrir o escribir; los errores se reemiten por errorOccurred.
bool MapWidget::saveFeaturesTo(const QString &databasePath)
{
    if (!d->view)
        return false;

    VectorRepository repo;
    connect(&repo, &VectorRepository::errorOccurred, this,
            [this](const QString &ctx, const QString &msg) {
                emit errorOccurred(QStringLiteral("%1: %2").arg(ctx, msg));
            });

    if (!repo.open(databasePath)) {
        emit errorOccurred(repo.lastError());
        return false;
    }

    // Se vuelca el estado completo: se borra y se vuelve a escribir. Es lo
    // que corresponde a "guardar el mapa", y evita tener que llevar la cuenta
    // de que se anadio, cambio o borro desde la ultima vez.
    if (!repo.clearFeatures())
        return false;

    for (const LayerInfo &c : d->view->overlayModel()->layers())
        repo.saveLayer(c);

    return repo.saveFeatures(d->view->overlayModel()->features());
}

// Carga entidades y capas desde una BD vectorial, REEMPLAZANDO el contenido
// actual (setContents). false si no se pudo abrir; errores por errorOccurred.
bool MapWidget::loadFeaturesFrom(const QString &databasePath)
{
    if (!d->view)
        return false;

    VectorRepository repo;
    connect(&repo, &VectorRepository::errorOccurred, this,
            [this](const QString &ctx, const QString &msg) {
                emit errorOccurred(QStringLiteral("%1: %2").arg(ctx, msg));
            });

    if (!repo.open(databasePath)) {
        emit errorOccurred(repo.lastError());
        return false;
    }

    d->view->overlayModel()->setContents(repo.loadFeatures(), repo.loadLayers());
    return true;
}

// ------------------------------------------------------------ ficheros .geo --

// Carga un fichero .geo como UNA entidad (posiblemente multi-parte) en una capa
// nueva. Decide el tipo (poligono si todos los trazados cierran con suficientes
// vertices, si no polilinea), descarta partes degeneradas y, para poligonos,
// quita el vertice de cierre repetido. Devuelve el id creado o -1 (con el motivo
// en *error y por errorOccurred).
qint64 MapWidget::loadGeoAsLayer(const QString &path, const QString &layerId,
                                 const QString &displayName,
                                 const FeatureStyle &style, QString *error)
{
    if (!d->view)
        return -1;

    QString motivo;
    const QVector<GeoPath> trazados = readGeoFile(path, &motivo);
    if (trazados.isEmpty()) {
        if (error)
            *error = motivo;
        emit errorOccurred(motivo);
        return -1;
    }

    // El tipo de la entidad: poligono si TODOS los trazados cierran (y tienen
    // vertices de sobra), si no polilinea. Todas las partes comparten tipo.
    bool todosCierran = true;
    for (const GeoPath &t : trazados)
        if (!(t.closed && t.points.size() >= 4)) {
            todosCierran = false;
            break;
        }
    const GeometryKind tipo = todosCierran ? GeometryKind::Polygon
                                           : GeometryKind::Polyline;

    // Una parte por trazado. Un poligono no necesita repetir el primer vertice
    // al final; una polilinea conserva los puntos tal cual (un anillo suelto
    // dentro de una polilinea se dibuja cerrado por el vertice repetido).
    QVector<QVector<QGeoCoordinate>> partes;
    for (const GeoPath &t : trazados) {
        QVector<QGeoCoordinate> parte = t.points;
        if (tipo == GeometryKind::Polygon && parte.size() > 1)
            parte.removeLast();
        if (parte.size() < (tipo == GeometryKind::Polygon ? 3 : 2))
            continue;                       // se descartan trazados degenerados
        partes.append(parte);
    }
    if (partes.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 no tiene trazados dibujables").arg(path);
        return -1;
    }

    MapFeature f;
    f.layerId = layerId;
    f.style = style;
    f.kind = tipo;
    f.name = displayName.isEmpty() ? layerId : displayName;
    f.geometry = partes.first();
    if (partes.size() > 1)
        f.parts = partes;                   // entidad multi-parte

    addFeatureLayer(layerId, displayName, 0);
    return addFeature(f);
}

// ------------------------------------------------------ objetivos moviles --

// Reenvios al TargetModel (capa dinamica de objetivos moviles), cada uno
// documentado en TargetModel; devuelven un valor neutro sin vista.

// Inserta o actualiza un objetivo movil; devuelve su id.
qint64 MapWidget::addTarget(const MapTarget &target)
{
    return d->view ? d->view->targetModel()->upsert(target) : -1;
}

// Mueve un objetivo a una nueva posicion/rumbo (camino rapido de refresco).
bool MapWidget::updateTarget(qint64 id, const QGeoCoordinate &position,
                             double headingDeg)
{
    return d->view && d->view->targetModel()->update(id, position, headingDeg);
}

// Cambia la etiqueta de un objetivo.
bool MapWidget::setTargetLabel(qint64 id, const QString &text)
{
    return d->view && d->view->targetModel()->setLabel(id, text);
}

// Elimina un objetivo.
bool MapWidget::removeTarget(qint64 id)
{
    return d->view && d->view->targetModel()->remove(id);
}

// Elimina todos los objetivos.
void MapWidget::clearTargets()
{
    if (d->view)
        d->view->targetModel()->clear();
}

// Un objetivo por id (nullopt si no existe).
std::optional<MapTarget> MapWidget::target(qint64 id) const
{
    return d->view ? d->view->targetModel()->target(id)
                   : std::optional<MapTarget>();
}

// Todos los objetivos.
QVector<MapTarget> MapWidget::targets() const
{
    return d->view ? d->view->targetModel()->targets() : QVector<MapTarget>();
}

// Numero de objetivos.
int MapWidget::targetCount() const
{
    return d->view ? d->view->targetModel()->count() : 0;
}

// Fija la longitud maxima de las trazas de todos los objetivos.
void MapWidget::setTargetTrailLength(int maxPoints)
{
    if (d->view)
        d->view->targetModel()->setTrailMaxPoints(maxPoints);
}

// Muestra u oculta la capa entera de objetivos (repinta solo esa capa).
void MapWidget::setTargetsVisible(bool visible)
{
    if (d->view && d->view->targetLayer()) {
        d->view->targetLayer()->setVisible(visible);
        d->view->replot(QCustomPlot::rpQueuedReplot);
    }
}

// Id de la entidad seleccionada (o -1).
qint64 MapWidget::selectedFeature() const
{
    return d->view ? d->view->overlayModel()->selectedId() : -1;
}

// Selecciona una entidad por id.
void MapWidget::selectFeature(qint64 id)
{
    if (d->view)
        d->view->overlayModel()->setSelected(id);
}

// Quita la seleccion actual.
void MapWidget::clearSelection()
{
    if (d->view)
        d->view->overlayModel()->clearSelection();
}

// Id de la entidad bajo un pixel (dentro de la tolerancia), o -1.
qint64 MapWidget::featureAt(const QPoint &pixel, double tolerancePx) const
{
    d->syncGeometry();
    return d->view ? d->view->featureLayer()->featureAt(pixel, tolerancePx) : -1;
}

// Herramienta activa (arrastrar, medir, dibujar, editar...).
MapTool MapWidget::activeTool() const
{
    return d->view ? d->view->activeTool() : MapTool::None;
}

// Cambia la herramienta activa.
void MapWidget::setActiveTool(MapTool tool)
{
    d->syncGeometry();
    if (d->view)
        d->view->setActiveTool(tool);
}

// Fija la capa donde caeran las entidades nuevas, CREANDOLA si no existe (para no
// tener que declararla antes de empezar a dibujar).
void MapWidget::setActiveFeatureLayer(const QString &id)
{
    if (d->view) {
        // La capa se crea si no existe: asi no hay que declararla antes de
        // empezar a dibujar en ella.
        if (!d->view->overlayModel()->hasLayer(id))
            d->view->overlayModel()->addLayer(id);
        d->view->setActiveFeatureLayer(id);
    }
}

// Id de la capa activa para dibujo.
QString MapWidget::activeFeatureLayer() const
{
    return d->view ? d->view->activeFeatureLayer() : QString();
}

// Estilo (color, grosor, icono...) que tendran las entidades que se dibujen.
void MapWidget::setDraftStyle(const FeatureStyle &style)
{
    if (d->view)
        d->view->setDraftStyle(style);
}

// Tipo de dominio que se asignara a las entidades que se dibujen.
void MapWidget::setDraftType(const QString &type)
{
    if (d->view)
        d->view->setDraftType(type);
}

// ¿Hay un trazado en curso?
bool MapWidget::isDrawing() const
{
    return d->view && d->view->isDrawing();
}

// Cierra el trazado en curso y lo convierte en entidad (id, o -1 si se descarta).
qint64 MapWidget::finishDrawing()
{
    return d->view ? d->view->finishDrawing() : -1;
}

// Cancela el trazado en curso.
bool MapWidget::cancelDrawing()
{
    return d->view && d->view->cancelDrawing();
}

// Fraccion [0,1] del viewport cubierta por teselas EXACTAS (no por respaldo
// escalado). Es la medida de "cuan completa" esta la zona a este zoom; la usan las
// herramientas para decidir si merece la pena descargar.
double MapWidget::exactCoverage() const
{
    d->syncGeometry();
    if (!d->view || !d->view->tileLayer())
        return 0.0;
    const auto &plan = d->view->tileLayer()->plan();
    const int n = plan.slotCount();
    return n > 0 ? double(plan.exactCount) / double(n) : 0.0;
}

// Enciende/apaga la rejilla de depuracion sobre las teselas (bordes y etiqueta
// z/x/y, y si son exactas o respaldo).
void MapWidget::setDebugGridVisible(bool visible)
{
    d->syncGeometry();
    if (d->view && d->view->tileLayer()) {
        d->view->tileLayer()->setDebugGridVisible(visible);
        d->view->refreshPlan();
    }
}

// Enciende o apaga la mancha de cobertura. Al encenderla recalcula; al apagarla
// solo oculta la capa y repinta.
void MapWidget::setCoverageVisible(bool on)
{
    d->coverageVisible = on;
    if (!d->view || !d->view->coverageLayer())
        return;
    d->view->coverageLayer()->setVisible(on);
    if (on)
        refreshCoverage();
    else
        d->view->replot(QCustomPlot::rpQueuedReplot);
}

bool MapWidget::isCoverageVisible() const
{
    return d->coverageVisible;
}

// Fija el zoom cuya cobertura se muestra; recomputa solo si la mancha esta a la
// vista.
void MapWidget::setCoverageZoom(int targetZoom)
{
    d->coverageZoom = targetZoom;
    if (d->coverageVisible)
        refreshCoverage();
}

int MapWidget::coverageZoom() const
{
    return d->coverageZoom;
}

// Vuelve a consultar la BD de la capa activa y rehace la mancha. Agrega la
// cobertura del zoom objetivo a una rejilla 4 niveles mas gruesa (asi se ve a
// zoom bajo) y colorea cada celda por su fraccion de teselas presentes. La
// consulta es solo-lectura, en el hilo de la GUI (conexion propia via el pool,
// que convive con el hilo trabajador y con TileFiller gracias al busy_timeout).
void MapWidget::refreshCoverage()
{
    if (!d->view || !d->view->coverageLayer())
        return;
    CoverageLayer *capa = d->view->coverageLayer();

    const TileDataset *ds = d->service.activeDataset();
    if (!ds) {
        capa->clearData();
        d->view->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    const int zt = qBound(ds->minZoom, d->coverageZoom, ds->maxZoom);
    const int zs = qBound(ds->minZoom, zt - 3, zt);   // 3 niveles mas grueso
    const int shift = zt - zs;

    RMapsTileSource src(*ds);
    if (!src.open()) {
        capa->clearData();
        d->view->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    const auto hist = src.coverageHistogram(zt, shift);
    const double total = double(qint64(1) << (2 * shift));   // 4^shift

    QVector<CoverageLayer::Cell> celdas;
    celdas.reserve(hist.size());
    for (const RMapsTileSource::CoverageCell &h : hist) {
        CoverageLayer::Cell c;
        c.sx = h.bx;
        // by esta en coords de almacenamiento; a y logico segun el esquema.
        c.sy = TileMatrix::fromStorageY(h.by, zs, ds->scheme);
        c.frac = float(qMin(1.0, double(h.count) / total));
        celdas.append(c);
    }

    capa->setData(zt, zs, celdas);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// Expone la proyeccion lat/lon -> eje (grados de Mercator) por si el cliente
// dibuja sus propios items sobre el QCustomPlot subyacente.
QPointF MapWidget::toAxisCoords(const QGeoCoordinate &position) const
{
    return MapView::toAxis(position);
}

// Inverso de toAxisCoords: punto de eje -> lat/lon.
QGeoCoordinate MapWidget::fromAxisCoords(const QPointF &axisPoint) const
{
    return MapView::fromAxis(axisPoint);
}

// Acceso de escape al QCustomPlot interno (la MapView), para superponer items
// propios. Se ofrece como QWidget* para no filtrar QCustomPlot en la cabecera.
QWidget *MapWidget::customPlot() const
{
    d->syncGeometry();
    return d->view;
}

// Al redimensionarse el widget, sincroniza YA la geometria de la vista (sin
// esperar al resizeEvent diferido de la vista), para que un calculo posterior use
// el tamano nuevo.
void MapWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    d->syncGeometry();
}

} // namespace libmapa
