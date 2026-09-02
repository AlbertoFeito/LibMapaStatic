#include "libmapa/MapWidget.h"

#include "core/Logging.h"
#include "tiles/TileService.h"
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
};

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

bool MapWidget::isReady() const { return d->ready; }
QString MapWidget::lastError() const { return d->error; }

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

QString MapWidget::baseLayerId() const
{
    return d->service.activeDatasetId();
}

bool MapWidget::setBaseLayerId(const QString &id)
{
    d->syncGeometry();
    return d->service.setActiveDataset(id);
}

QGeoCoordinate MapWidget::center() const
{
    d->syncGeometry();
    return d->view ? d->view->center() : d->config.initialCenter;
}

void MapWidget::setCenter(const QGeoCoordinate &center)
{
    d->syncGeometry();
    if (d->view)
        d->view->setCenter(center);
}

int MapWidget::zoom() const
{
    d->syncGeometry();
    return d->view ? d->view->zoom() : d->config.initialZoom;
}

void MapWidget::setZoom(int zoom)
{
    d->syncGeometry();
    if (d->view)
        d->view->setZoom(zoom);
}

int MapWidget::minZoom() const
{
    const TileDataset *ds = d->service.activeDataset();
    return ds ? ds->minZoom : 0;
}

int MapWidget::maxZoom() const
{
    const TileDataset *ds = d->service.activeDataset();
    return ds ? ds->recommendedMaxZoom : 18;
}

void MapWidget::zoomIn()  { setZoom(zoom() + 1); }
void MapWidget::zoomOut() { setZoom(zoom() - 1); }

void MapWidget::fitBounds(const QGeoCoordinate &northWest,
                          const QGeoCoordinate &southEast)
{
    d->syncGeometry();
    if (d->view)
        d->view->fitBounds(northWest, southEast);
}

QGeoCoordinate MapWidget::visibleNorthWest() const
{
    d->syncGeometry();
    return d->view ? d->view->visibleNorthWest() : QGeoCoordinate();
}

QGeoCoordinate MapWidget::visibleSouthEast() const
{
    d->syncGeometry();
    return d->view ? d->view->visibleSouthEast() : QGeoCoordinate();
}

// ------------------------------------------------------ capas y entidades --

bool MapWidget::addFeatureLayer(const QString &id, const QString &displayName,
                                int zOrder)
{
    return d->view ? d->view->overlayModel()->addLayer(id, displayName, zOrder)
                   : false;
}

bool MapWidget::removeFeatureLayer(const QString &id)
{
    return d->view ? d->view->overlayModel()->removeLayer(id) : false;
}

QVector<LayerInfo> MapWidget::featureLayers() const
{
    return d->view ? d->view->overlayModel()->layers() : QVector<LayerInfo>();
}

bool MapWidget::setFeatureLayerVisible(const QString &id, bool visible)
{
    return d->view ? d->view->overlayModel()->setLayerVisible(id, visible)
                   : false;
}

bool MapWidget::setFeatureLayerZOrder(const QString &id, int zOrder)
{
    return d->view ? d->view->overlayModel()->setLayerZOrder(id, zOrder) : false;
}

qint64 MapWidget::addFeature(const MapFeature &feature)
{
    return d->view ? d->view->overlayModel()->addFeature(feature) : -1;
}

bool MapWidget::updateFeature(const MapFeature &feature)
{
    return d->view ? d->view->overlayModel()->updateFeature(feature) : false;
}

bool MapWidget::removeFeature(qint64 id)
{
    return d->view ? d->view->overlayModel()->removeFeature(id) : false;
}

void MapWidget::clearFeatureLayer(const QString &layerId)
{
    if (d->view)
        d->view->overlayModel()->clearLayer(layerId);
}

void MapWidget::clearFeatures()
{
    if (d->view)
        d->view->overlayModel()->clear();
}

std::optional<MapFeature> MapWidget::feature(qint64 id) const
{
    return d->view ? d->view->overlayModel()->feature(id)
                   : std::optional<MapFeature>();
}

QVector<MapFeature> MapWidget::features() const
{
    return d->view ? d->view->overlayModel()->features() : QVector<MapFeature>();
}

QVector<MapFeature> MapWidget::featuresInLayer(const QString &layerId) const
{
    return d->view ? d->view->overlayModel()->featuresInLayer(layerId)
                   : QVector<MapFeature>();
}

QVector<MapFeature> MapWidget::featuresOfType(const QString &type) const
{
    return d->view ? d->view->overlayModel()->featuresOfType(type)
                   : QVector<MapFeature>();
}

int MapWidget::featureCount() const
{
    return d->view ? d->view->overlayModel()->count() : 0;
}

bool MapWidget::moveVertex(qint64 id, int index, const QGeoCoordinate &to)
{
    return d->view ? d->view->overlayModel()->moveVertex(id, index, to) : false;
}

bool MapWidget::insertVertex(qint64 id, int index, const QGeoCoordinate &at)
{
    return d->view ? d->view->overlayModel()->insertVertex(id, index, at) : false;
}

bool MapWidget::removeVertex(qint64 id, int index)
{
    return d->view ? d->view->overlayModel()->removeVertex(id, index) : false;
}

bool MapWidget::moveFeature(qint64 id, double dLat, double dLon)
{
    return d->view ? d->view->overlayModel()->moveFeature(id, dLat, dLon) : false;
}

qint64 MapWidget::selectedFeature() const
{
    return d->view ? d->view->overlayModel()->selectedId() : -1;
}

void MapWidget::selectFeature(qint64 id)
{
    if (d->view)
        d->view->overlayModel()->setSelected(id);
}

void MapWidget::clearSelection()
{
    if (d->view)
        d->view->overlayModel()->clearSelection();
}

qint64 MapWidget::featureAt(const QPoint &pixel, double tolerancePx) const
{
    d->syncGeometry();
    return d->view ? d->view->featureLayer()->featureAt(pixel, tolerancePx) : -1;
}

MapTool MapWidget::activeTool() const
{
    return d->view ? d->view->activeTool() : MapTool::None;
}

void MapWidget::setActiveTool(MapTool tool)
{
    d->syncGeometry();
    if (d->view)
        d->view->setActiveTool(tool);
}

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

QString MapWidget::activeFeatureLayer() const
{
    return d->view ? d->view->activeFeatureLayer() : QString();
}

void MapWidget::setDraftStyle(const FeatureStyle &style)
{
    if (d->view)
        d->view->setDraftStyle(style);
}

void MapWidget::setDraftType(const QString &type)
{
    if (d->view)
        d->view->setDraftType(type);
}

bool MapWidget::isDrawing() const
{
    return d->view && d->view->isDrawing();
}

qint64 MapWidget::finishDrawing()
{
    return d->view ? d->view->finishDrawing() : -1;
}

bool MapWidget::cancelDrawing()
{
    return d->view && d->view->cancelDrawing();
}

double MapWidget::exactCoverage() const
{
    d->syncGeometry();
    if (!d->view || !d->view->tileLayer())
        return 0.0;
    const auto &plan = d->view->tileLayer()->plan();
    const int n = plan.slotCount();
    return n > 0 ? double(plan.exactCount) / double(n) : 0.0;
}

void MapWidget::setDebugGridVisible(bool visible)
{
    d->syncGeometry();
    if (d->view && d->view->tileLayer()) {
        d->view->tileLayer()->setDebugGridVisible(visible);
        d->view->refreshPlan();
    }
}

QPointF MapWidget::toAxisCoords(const QGeoCoordinate &position) const
{
    return MapView::toAxis(position);
}

QGeoCoordinate MapWidget::fromAxisCoords(const QPointF &axisPoint) const
{
    return MapView::fromAxis(axisPoint);
}

QWidget *MapWidget::customPlot() const
{
    d->syncGeometry();
    return d->view;
}

void MapWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    d->syncGeometry();
}

} // namespace libmapa
