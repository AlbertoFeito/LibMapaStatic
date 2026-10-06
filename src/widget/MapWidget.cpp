#include "libmapa/MapWidget.h"

#include "core/Logging.h"
#include "dem/ElevationAnalysis.h"
#include "dem/HgtElevation.h"
#include "dem/SqliteElevation.h"
#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"
#include "tiles/RMapsTileSource.h"
#include "tiles/TileService.h"
#include "db/VectorRepository.h"
#include "io/DataPackage.h"
#include "io/PackageCheck.h"
#include "widget/CoverageLayer.h"
#include "widget/HillshadeLayer.h"
#include "widget/MapView.h"

#include <QFile>
#include <QImage>
#include <QtMath>
#include <cmath>
#include <vector>
#include <QLayout>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

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

    // Elevacion del terreno (opcional): carpeta `.hgt` o base de datos. Puntero a
    // la interfaz para poder cambiar de origen en caliente. Nulo = sin elevacion.
    std::unique_ptr<IElevationSource> elevation;

    // Persistencia automatica de entidades (opcional). Vacio = apagada. El timer
    // agrupa las rafagas de cambios; suppressAutosave evita guardar durante la
    // carga inicial (que tambien emite senales de modelo).
    QString featuresDbFile;
    QTimer *autosave = nullptr;
    bool suppressAutosave = false;

    // Mancha de cobertura (diagnostico): zoom objetivo y si esta encendida.
    int coverageZoom = 14;
    bool coverageVisible = false;

    // Relieve sombreado (hillshade) en vivo: parametros del sol y del render, y un
    // temporizador antirebote para recalcular al terminar de mover la vista.
    bool hillshadeVisible = false;
    double hsSunAz = 315.0;      // NO
    double hsSunAlt = 45.0;
    double hsOpacity = 0.6;
    double hsZFactor = 2.0;      // exageracion suave por defecto
    bool hsColored = false;
    QTimer *hsTimer = nullptr;

    // Paquete de datos (opcional). Sus capas fijas (overlays) se cargan al abrir
    // pero NO son del usuario: fixedLayers marca sus ids para que el guardado de
    // entidades las salte (si no, se duplicarian en cada arranque).
    DataPackageInfo package;
    QVector<DataPackage::Overlay> overlays;
    QSet<QString> fixedLayers;
    QStringList dataWarnings;      // comprobacion rapida del paquete al abrir

    /*!
     * \brief Carga las capas fijas del paquete que aun no esten en el modelo.
     *
     * Idempotente: se llama al abrir y otra vez tras cada loadFeaturesFrom,
     * porque setContents reemplaza TODO el modelo y se las llevaria por delante.
     * Quedan bloqueadas (capa no editable, entidad no seleccionable).
     */
    void loadOverlays()
    {
        if (!view)
            return;
        OverlayModel *modelo = view->overlayModel();
        for (const DataPackage::Overlay &ov : overlays) {
            if (modelo->hasLayer(ov.id))
                continue;
            QString motivo;
            const qint64 fid = q->loadGeoAsLayer(ov.file, ov.id, ov.name, ov.style, &motivo);
            if (fid < 0) {
                qCWarning(lcMapaRender) << "Capa fija" << ov.id << "no cargada:" << motivo;
                continue;
            }
            if (auto f = modelo->feature(fid)) {
                f->selectable = false;
                modelo->updateFeature(*f);
            }
            modelo->setLayerEditable(ov.id, false);
            modelo->setLayerZOrder(ov.id, ov.zOrder);
        }
    }
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
    d->layout = new QVBoxLayout(this);
    d->layout->setContentsMargins(0, 0, 0, 0);
    d->layout->setSpacing(0);

    // La configuracion EFECTIVA: lo que puso la aplicacion, completado con el
    // paquete de datos (si hay) y, al final, con los valores de siempre. Lo
    // rellenado a mano gana siempre sobre el paquete.
    MapConfig cfg = config;
    QString error;
    QVector<TileDataset> datasets;

    if (!cfg.dataDir.isEmpty()) {
        const auto paquete = DataPackage::load(cfg.dataDir, &error);
        if (!paquete) {
            d->error = error;
            qCCritical(lcMapaRender) << d->error;
            emit errorOccurred(d->error);
            return;
        }
        // Comprobacion RAPIDA (sin contar cobertura): lo que dejaria el mapa en
        // blanco sin explicacion -ficheros, bases que no abren, imagenes que no
        // se decodifican- se queda en dataWarnings() y en el log. No impide
        // arrancar: se dibuja lo que si funcione.
        PackageCheck::Options rapido;
        rapido.coverage = false;
        d->dataWarnings = PackageCheck::run(*paquete, rapido).problems();
        for (const QString &aviso : d->dataWarnings)
            qCWarning(lcMapaRender) << aviso;

        d->package = paquete->info;
        d->overlays = paquete->overlays;
        for (const DataPackage::Overlay &ov : paquete->overlays)
            d->fixedLayers.insert(ov.id);

        if (cfg.datasetsFile.isEmpty())
            datasets = paquete->datasets;
        if (cfg.elevationDbFile.isEmpty() && cfg.elevationDir.isEmpty()) {
            cfg.elevationDbFile = paquete->elevationFile;
            cfg.elevationDir = paquete->elevationDir;
        }
        if (cfg.featuresDbFile.isEmpty()) {
            QString motivo;
            cfg.featuresDbFile = paquete->prepareFeaturesFile(&motivo);
            if (!motivo.isEmpty())
                qCWarning(lcMapaRender) << motivo;
        }
        if (cfg.initialLayerId.isEmpty())
            cfg.initialLayerId = paquete->startLayer;
        if (!cfg.initialCenter.isValid())
            cfg.initialCenter = paquete->startCenter;
        if (cfg.initialZoom < 0)
            cfg.initialZoom = paquete->startZoom;
    }

    if (!cfg.initialCenter.isValid())
        cfg.initialCenter = QGeoCoordinate(23.1136, -82.3666);   // La Habana
    if (cfg.initialZoom < 0)
        cfg.initialZoom = 10;
    d->config = cfg;

    if (datasets.isEmpty()) {
        if (cfg.datasetsFile.isEmpty() && cfg.dataDir.isEmpty())
            error = tr("Falta la configuracion: indica MapConfig::dataDir "
                       "(o MapConfig::datasetsFile)");
        else if (!cfg.datasetsFile.isEmpty())
            datasets = TileService::loadDatasets(cfg.datasetsFile, &error);
    }

    if (datasets.isEmpty()) {
        d->error = error.isEmpty()
                       ? tr("No se pudo cargar %1").arg(cfg.datasetsFile)
                       : error;
        qCCritical(lcMapaRender) << d->error;
        emit errorOccurred(d->error);
        return;
    }

    connect(&d->service, &TileService::errorOccurred,
            this, &MapWidget::errorOccurred);

    if (!d->service.start(datasets, cfg.cacheMiB)) {
        d->error = tr("No hay ninguna base de datos de mapas utilizable.");
        return;
    }
    d->service.setDebounceMs(cfg.debounceMs);

    // Origen de elevacion: la base de datos tiene prioridad sobre la carpeta.
    if (!cfg.elevationDbFile.isEmpty())
        d->elevation = std::make_unique<SqliteElevation>(cfg.elevationDbFile);
    else if (!cfg.elevationDir.isEmpty()) {
        auto hgt = std::make_unique<HgtElevation>();
        hgt->setDirectory(cfg.elevationDir);
        d->elevation = std::move(hgt);
    }

    if (!cfg.initialLayerId.isEmpty())
        d->service.setActiveDataset(cfg.initialLayerId);

    d->view = new MapView(&d->service, this);
    d->layout->addWidget(d->view);

    d->view->tileLayer()->setNoDataColor(cfg.noDataColor);

    connect(d->view, &MapView::zoomChanged, this, &MapWidget::zoomChanged);
    connect(d->view, &MapView::centerChanged, this, &MapWidget::centerChanged);

    // Hillshade en vivo: tras mover la vista, recalcular con un pequeno retardo
    // (antirebote) para no recalcular en cada pixel del arrastre.
    d->hsTimer = new QTimer(this);
    d->hsTimer->setSingleShot(true);
    d->hsTimer->setInterval(180);
    connect(d->hsTimer, &QTimer::timeout, this, &MapWidget::refreshHillshade);
    auto programarHs = [this] { if (d->hillshadeVisible) d->hsTimer->start(); };
    connect(d->view, &MapView::zoomChanged, this, [programarHs](int) { programarHs(); });
    connect(d->view, &MapView::centerChanged, this, [programarHs](const QGeoCoordinate &) { programarHs(); });

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
    connect(d->view, &MapView::targetClicked, this, &MapWidget::targetClicked);
    connect(d->view, &MapView::featureCreated, this, &MapWidget::featureCreated);
    connect(d->view, &MapView::drawingCancelled, this, &MapWidget::drawingCancelled);

    // Persistencia automatica: el timer de antirebote agrupa las rafagas de
    // cambios (arrastrar un vertice emite muchos featureUpdated) en un solo
    // guardado. Solo actua si hay featuresDbFile y no estamos cargando.
    d->autosave = new QTimer(this);
    d->autosave->setSingleShot(true);
    d->autosave->setInterval(500);
    connect(d->autosave, &QTimer::timeout, this, &MapWidget::saveFeaturesNow);
    auto programarGuardado = [this] {
        if (!d->featuresDbFile.isEmpty() && !d->suppressAutosave)
            d->autosave->start();
    };
    connect(modelo, &OverlayModel::featureAdded, this,
            [programarGuardado](qint64) { programarGuardado(); });
    connect(modelo, &OverlayModel::featureUpdated, this,
            [programarGuardado](qint64) { programarGuardado(); });
    connect(modelo, &OverlayModel::featureRemoved, this,
            [programarGuardado](qint64) { programarGuardado(); });
    connect(modelo, &OverlayModel::layersChanged, this,
            [programarGuardado] { programarGuardado(); });
    if (!cfg.featuresDbFile.isEmpty())
        setFeaturesDbFile(cfg.featuresDbFile);

    // Capas fijas del paquete, DESPUES de las entidades del usuario (cargarlas
    // reemplaza el modelo). Sin guardado ni historial: no son algo que el
    // usuario haya hecho y no debe poder "deshacerlas".
    d->suppressAutosave = true;
    d->loadOverlays();
    d->suppressAutosave = false;
    modelo->clearUndoHistory();

    d->view->setZoom(cfg.initialZoom);
    d->view->setCenter(cfg.initialCenter);
    d->ready = true;
}

MapWidget::~MapWidget() = default;

// isReady: ¿el widget se inicializo con exito? lastError: el motivo si no.
bool MapWidget::isReady() const { return d->ready; }

// Informacion del paquete de datos abierto (invalida si no se uso dataDir).
DataPackageInfo MapWidget::packageInfo() const { return d->package; }

// Problemas de la comprobacion rapida del paquete al abrir (vacio = todo bien).
QStringList MapWidget::dataWarnings() const { return d->dataWarnings; }
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

// Permite sobre-zoom por encima del maximo recomendado (-1 lo restaura).
void MapWidget::setMaxZoomOverride(int zoom)
{
    if (d->view)
        d->view->setMaxZoomOverride(zoom);
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

// Guarda las entidades y capas DEL USUARIO en una BD vectorial (vuelca el estado
// completo: borra y reescribe, en vez de llevar la cuenta de altas/bajas). Las
// capas fijas del paquete se saltan: vienen con los datos, no son del usuario.
// false si no se pudo abrir o escribir; los errores se reemiten por errorOccurred.
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
        if (!d->fixedLayers.contains(c.id))
            repo.saveLayer(c);

    QVector<MapFeature> propias = d->view->overlayModel()->features();
    if (!d->fixedLayers.isEmpty()) {
        propias.erase(std::remove_if(propias.begin(), propias.end(),
                                     [this](const MapFeature &f) {
                                         return d->fixedLayers.contains(f.layerId);
                                     }),
                      propias.end());
    }
    return repo.saveFeatures(propias);
}

// Carga entidades y capas desde una BD vectorial, REEMPLAZANDO el contenido
// actual (setContents), y vuelve a poner las capas fijas del paquete, que
// setContents se habria llevado. Todo en UN paso de deshacer: deshacer la carga
// devuelve el mapa anterior entero, capas fijas incluidas. false si no se pudo
// abrir; errores por errorOccurred.
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

    OverlayModel *modelo = d->view->overlayModel();
    modelo->beginUndoGroup();
    modelo->setContents(repo.loadFeatures(), repo.loadLayers());
    d->loadOverlays();
    modelo->endUndoGroup();
    return true;
}

// Enciende (o apaga, con cadena vacia) el guardado automatico. Si el fichero ya
// existe se carga ahora; se marca suppressAutosave durante la carga para que las
// senales de setContents (layersChanged) no programen un guardado redundante.
void MapWidget::setFeaturesDbFile(const QString &databasePath)
{
    d->featuresDbFile = databasePath;
    if (databasePath.isEmpty())
        return;
    if (QFile::exists(databasePath)) {
        d->suppressAutosave = true;
        loadFeaturesFrom(databasePath);
        d->suppressAutosave = false;
    }
}

// Fuerza un volcado inmediato al fichero de persistencia, si hay uno. Lo llama el
// timer de antirebote y puede llamarlo la app al cerrar.
void MapWidget::saveFeaturesNow()
{
    if (!d->featuresDbFile.isEmpty())
        saveFeaturesTo(d->featuresDbFile);
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

// Cuelga un dato de dominio (AIS, ADS-B, telemetria...) en un objetivo. La
// libreria no lo interpreta; solo lo guarda y lo devuelve tal cual.
bool MapWidget::setTargetAttribute(qint64 id, const QString &key, const QVariant &value)
{
    return d->view && d->view->targetModel()->setAttribute(id, key, value);
}

// Devuelve el valor de un atributo de un objetivo (invalido si no existe).
QVariant MapWidget::targetAttribute(qint64 id, const QString &key) const
{
    return d->view ? d->view->targetModel()->attribute(id, key) : QVariant();
}

// Registra el proveedor de simbolos de la app (icono por tipo/estado + rotacion
// por rumbo). Lo gestiona la capa de objetivos; sin el, se usa el galon.
void MapWidget::setTargetSymbolProvider(TargetSymbolProvider provider)
{
    if (d->view && d->view->targetLayer())
        d->view->targetLayer()->setSymbolProvider(std::move(provider));
}

// Fija el nivel de detalle de la capa de objetivos (topes de etiquetas/trazas).
void MapWidget::setTargetDetailBudget(int maxLabels, int maxTrails)
{
    if (d->view && d->view->targetLayer())
        d->view->targetLayer()->setDetailBudget(maxLabels, maxTrails);
}

// Objetivo movil mas cercano a un punto de la pantalla, o -1.
qint64 MapWidget::targetAt(const QPoint &pixelPos, double tolerancePx) const
{
    return d->view ? d->view->targetAt(pixelPos, tolerancePx) : -1;
}

// Resalta un objetivo (-1 = ninguno).
void MapWidget::setSelectedTarget(qint64 id)
{
    if (d->view)
        d->view->setSelectedTarget(id);
}

// Objetivo resaltado, o -1.
qint64 MapWidget::selectedTarget() const
{
    return d->view ? d->view->selectedTarget() : -1;
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

// Cota del terreno (m) en una coordenada. Reenvio fino al origen de elevacion
// configurado; NaN si no hay origen o no hay dato (el llamador lo comprueba).
double MapWidget::elevationAt(const QGeoCoordinate &position) const
{
    return d->elevation ? d->elevation->elevationAt(position)
                        : std::numeric_limits<double>::quiet_NaN();
}

// Perfil de elevacion a lo largo de una ruta. Reenvio al calculo del nucleo
// usando el origen de elevacion configurado; perfil vacio si no hay origen.
ElevationProfile MapWidget::elevationProfile(
    const QVector<QGeoCoordinate> &path,
    const ElevationProfileParams &params) const
{
    if (!d->elevation)
        return ElevationProfile();
    return libmapa::elevationProfile(*d->elevation, path, params);
}

// Visibilidad punto a punto. Reenvio al calculo del nucleo con el origen de
// elevacion configurado; resultado invalido si no hay origen.
LineOfSightResult MapWidget::lineOfSight(
    const QGeoCoordinate &a, const QGeoCoordinate &b,
    double antennaA, double antennaB,
    const LineOfSightParams &params) const
{
    if (!d->elevation)
        return LineOfSightResult();
    return libmapa::lineOfSight(*d->elevation, a, b, antennaA, antennaB, params);
}

// Viewshed 360 grados. Reenvio al calculo del nucleo con el origen de elevacion
// configurado; resultado invalido si no hay origen.
Viewshed MapWidget::viewshed(const QGeoCoordinate &origin,
                             const ViewshedParams &params,
                             const ViewshedProgress &progress) const
{
    if (!d->elevation)
        return Viewshed();
    return libmapa::computeViewshed(*d->elevation, origin, params, progress);
}

// Cambia en caliente el origen a una CARPETA de `.hgt` (vacia = quita elevacion).
void MapWidget::setElevationDir(const QString &dir)
{
    if (dir.isEmpty()) {
        d->elevation.reset();
        return;
    }
    auto hgt = std::make_unique<HgtElevation>();
    hgt->setDirectory(dir);
    d->elevation = std::move(hgt);
}

// Cambia en caliente el origen a una BASE DE DATOS `.sqlitedb` (vacia = quita).
void MapWidget::setElevationDb(const QString &dbFile)
{
    if (dbFile.isEmpty()) {
        d->elevation.reset();
        return;
    }
    d->elevation = std::make_unique<SqliteElevation>(dbFile);
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

// --- Relieve sombreado (hillshade) en vivo -----------------------------------

namespace {
// Color hipsometrico (tintado por altura) para el modo coloreado. Rampa simple
// mar/costa -> verde -> marron -> roca -> nieve. Interpola entre paradas.
QColor hypsometric(double e)
{
    struct P { double z; int r, g, b; };
    static const P ramp[] = {
        {   0.0, 0xa9, 0xd1, 0x8c}, { 200.0, 0x8c, 0xbf, 0x6a},
        { 500.0, 0xd9, 0xcf, 0x8f}, {1000.0, 0xc2, 0xa0, 0x6a},
        {1500.0, 0x9c, 0x6b, 0x4a}, {2000.0, 0xcf, 0xcf, 0xcf},
        {3000.0, 0xff, 0xff, 0xff}
    };
    const int n = int(sizeof(ramp) / sizeof(ramp[0]));
    if (e <= ramp[0].z) return QColor(ramp[0].r, ramp[0].g, ramp[0].b);
    if (e >= ramp[n - 1].z) return QColor(ramp[n - 1].r, ramp[n - 1].g, ramp[n - 1].b);
    for (int i = 1; i < n; ++i) {
        if (e <= ramp[i].z) {
            const double t = (e - ramp[i - 1].z) / (ramp[i].z - ramp[i - 1].z);
            return QColor(int(ramp[i - 1].r + (ramp[i].r - ramp[i - 1].r) * t),
                          int(ramp[i - 1].g + (ramp[i].g - ramp[i - 1].g) * t),
                          int(ramp[i - 1].b + (ramp[i].b - ramp[i - 1].b) * t));
        }
    }
    return QColor(ramp[n - 1].r, ramp[n - 1].g, ramp[n - 1].b);
}
} // namespace

void MapWidget::setHillshadeVisible(bool on)
{
    d->hillshadeVisible = on;
    HillshadeLayer *capa = d->view ? d->view->hillshadeLayer() : nullptr;
    if (!capa) return;
    capa->setVisible(on);
    if (on) {
        refreshHillshade();
    } else {
        capa->clear();
        d->view->replot(QCustomPlot::rpQueuedReplot);
    }
}

bool MapWidget::isHillshadeVisible() const { return d->hillshadeVisible; }

void MapWidget::setHillshadeSun(double azimuthDeg, double altitudeDeg)
{
    d->hsSunAz = azimuthDeg;
    d->hsSunAlt = qBound(1.0, altitudeDeg, 89.0);
    if (d->hillshadeVisible) refreshHillshade();
}

void MapWidget::setHillshadeOpacity(double opacity)
{
    d->hsOpacity = qBound(0.0, opacity, 1.0);
    if (d->hillshadeVisible) refreshHillshade();
}

void MapWidget::setHillshadeExaggeration(double zFactor)
{
    d->hsZFactor = qMax(0.1, zFactor);
    if (d->hillshadeVisible) refreshHillshade();
}

void MapWidget::setHillshadeColored(bool on)
{
    d->hsColored = on;
    if (d->hillshadeVisible) refreshHillshade();
}

// Calcula el relieve sombreado para la vista actual a partir del DEM LOCAL (sin
// conexion) y lo deja en la capa. Submuestrea el area para ser rapido; la imagen
// se calcula en (longitud lineal, grados de Mercator) para alinear con la base.
void MapWidget::refreshHillshade()
{
    HillshadeLayer *capa = d->view ? d->view->hillshadeLayer() : nullptr;
    if (!capa) return;
    d->syncGeometry();
    if (!d->hillshadeVisible || !d->elevation) {
        capa->clear();
        if (d->view) d->view->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    const QGeoCoordinate nw = d->view->visibleNorthWest();
    const QGeoCoordinate se = d->view->visibleSouthEast();
    const double lonW = nw.longitude(), lonE = se.longitude();
    const double latN = nw.latitude(),  latS = se.latitude();
    if (!(lonE > lonW) || !(latN > latS)) { capa->clear(); return; }

    // FRENO por area: el coste del hillshade lo domina cargar teselas del DEM, que
    // crece con el area de la vista. A escala de pais (p. ej. Cuba entera) serian
    // demasiadas teselas y la GUI se congelaria. Por encima de un ancho/alto se deja
    // la capa en blanco: el relieve es util al acercar, no a vista general.
    const double kMaxSpanDeg = 2.0;    // ~220 km; a partir de aqui no se calcula
    if ((lonE - lonW) > kMaxSpanDeg || (latN - latS) > kMaxSpanDeg) {
        capa->clear();
        d->view->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    // Resolucion de salida: se limita el lado mayor para acotar el coste.
    const int anchoPx = qMax(16, d->view->width() > 1 ? d->view->width() : 640);
    const int altoPx  = qMax(16, d->view->height() > 1 ? d->view->height() : 480);
    const int maxDim = 360;
    int W = qMin(anchoPx, 4000);
    int H = qMin(altoPx, 4000);
    const double esc = double(maxDim) / double(qMax(W, H));
    if (esc < 1.0) { W = qMax(16, int(double(W) * esc)); H = qMax(16, int(double(H) * esc)); }
    const std::size_t WH = std::size_t(W) * std::size_t(H);

    // Latitud de cada fila por el inverso de Mercator (filas lineales en el eje Y).
    const double ayTop = WebMercator::latitudeToMercatorDegrees(latN);
    const double ayBot = WebMercator::latitudeToMercatorDegrees(latS);
    std::vector<double> lats;
    lats.resize(std::size_t(H));
    for (int j = 0; j < H; ++j) {
        const double ay = ayTop + (ayBot - ayTop) * (H == 1 ? 0.0 : double(j) / double(H - 1));
        lats[std::size_t(j)] = WebMercator::mercatorDegreesToLatitude(ay);
    }
    // Rejilla de cotas.
    std::vector<double> z(WH);
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const double lon = lonW + (lonE - lonW) * (W == 1 ? 0.0 : double(i) / double(W - 1));
            z[std::size_t(j) * std::size_t(W) + std::size_t(i)] =
                d->elevation->elevationAt(QGeoCoordinate(lats[std::size_t(j)], lon));
        }

    // Metros por pixel (aprox, con la latitud central).
    const double latC = 0.5 * (latN + latS);
    const double mLon = 111320.0 * std::cos(qDegreesToRadians(latC));
    const double dxm = qMax(1.0, (lonE - lonW) / qMax(1, W - 1) * mLon);
    const double dym = qMax(1.0, (latN - latS) / qMax(1, H - 1) * 110540.0);

    const double ze = d->hsZFactor;
    const double zenith = qDegreesToRadians(90.0 - d->hsSunAlt);
    const double azm = qDegreesToRadians(360.0 - d->hsSunAz + 90.0);
    const double cz = std::cos(zenith), sz = std::sin(zenith);
    const double fuerza = d->hsOpacity;

    auto at = [&](int i, int j) -> double {
        return z[std::size_t(qBound(0, j, H - 1)) * std::size_t(W) + std::size_t(qBound(0, i, W - 1))];
    };

    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int j = 0; j < H; ++j) {
        QRgb *fila = reinterpret_cast<QRgb *>(img.scanLine(j));
        for (int i = 0; i < W; ++i) {
            const double zc = at(i, j);
            if (std::isnan(zc)) { fila[i] = qRgba(0, 0, 0, 0); continue; }  // sin dato
            auto nz = [&](int ii, int jj) { const double v = at(ii, jj); return std::isnan(v) ? zc : v; };
            const double dzdx = (nz(i + 1, j) - nz(i - 1, j)) / (2.0 * dxm) * ze;
            const double dzdy = (nz(i, j - 1) - nz(i, j + 1)) / (2.0 * dym) * ze;  // j-1 = norte
            const double slope = std::atan(std::sqrt(dzdx * dzdx + dzdy * dzdy));
            const double aspect = std::atan2(dzdy, -dzdx);
            double hs = cz * std::cos(slope) + sz * std::sin(slope) * std::cos(azm - aspect);
            hs = qBound(0.0, hs, 1.0);
            if (d->hsColored) {
                const QColor c = hypsometric(zc);
                fila[i] = qRgba(int(c.red() * hs), int(c.green() * hs), int(c.blue() * hs), 255);
            } else {
                // Gris para Multiply; la fuerza mezcla hacia blanco (255 = sin efecto).
                const int v = 255 - int(fuerza * (255.0 - hs * 255.0));
                fila[i] = qRgb(v, v, v);
            }
        }
    }

    // Gris -> Multiply a opacidad 1 (la fuerza ya va en el pixel). Color ->
    // SourceOver con la opacidad elegida.
    if (d->hsColored)
        capa->setImage(img, lonW, lonE, latN, latS, QPainter::CompositionMode_SourceOver, d->hsOpacity);
    else
        capa->setImage(img, lonW, lonE, latN, latS, QPainter::CompositionMode_Multiply, 1.0);
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
