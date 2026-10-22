#include "libmapa/MapWidget.h"

#include "core/Logging.h"
#include "dem/ElevationAnalysis.h"
#include "dem/ElevationSources.h"
#include "dem/GeoTiffElevation.h"
#include "dem/HgtElevation.h"
#include "dem/SqliteElevation.h"
#include "geo/GeoMath.h"
#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"
#include "geo/MapDecor.h"
#include "geo/ProximityMonitor.h"
#include "geo/ZoneMonitor.h"
#include "tiles/MBTiles.h"
#include "tiles/RMapsTileSource.h"
#include "tiles/TileDatasetProbe.h"
#include "tiles/TileService.h"
#include "vector/FeatureQuery.h"
#include "vector/MvtTile.h"
#include "vector/PoiSearch.h"
#include "vector/RoutePlanner.h"
#include "vector/VectorStyle.h"
#include "db/VectorRepository.h"
#include "io/DataPackage.h"
#include "io/PackageCheck.h"
#include "io/PlaceIndex.h"
#include "widget/CoverageLayer.h"
#include "widget/HillshadeLayer.h"
#include "widget/MapView.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QFontMetricsF>
#include <QImageWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
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
    QString elevationDesc;                  //!< resumen para el usuario (ver elevationDescription)
    QStringList elevationWarn;              //!< problemas de la ultima carga
    PlaceIndex places;                      //!< busqueda de lugares (vacio = sin indice)
    //! POI ya leidos de las teselas, por "fichero|z/x/y" (vacio = tesela sin POI):
    //! buscar mientras se teclea no vuelve a abrir la base de datos.
    mutable QHash<QString, QVector<PointOfInterest>> poiCache;

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

    // Mapa hibrido: overlay de teselas vectoriales con su propio servicio (hilo
    // y cache aparte de la base). Nulo = sin overlay.
    std::unique_ptr<TileService> vectorOverlay;
    bool vectorOverlayVisible = true;
    std::optional<DataPackage::VectorOverlay> pendingOverlay;   // del paquete, al crear la vista

    //! Instala \a ds como overlay vectorial (ver MapWidget::setVectorOverlay).
    bool installVectorOverlay(TileDataset ds, bool visible, bool hiddenGroupsSet, QString *motivo);
    QStringList dataWarnings;      // comprobacion rapida del paquete al abrir

    // Alertas de zona: que objetivos estan dentro de que entidades vigiladas.
    // La logica vive en ZoneMonitor (probada sin ventanas); aqui solo se le
    // pasan los cambios de objetivos y entidades y se emiten sus transiciones.
    ZoneMonitor zones;
    // Las mismas zonas, vigilando las ENTIDADES PUNTO (marcadores fijos que el
    // operador coloca o arrastra). Monitor aparte porque los ids de entidad y de
    // objetivo son espacios distintos y podrian coincidir.
    ZoneMonitor pointZones;
    // Resalte en el mapa de las zonas ocupadas y de los puntos dentro de ellas.
    bool zoneHighlight = true;

    // Alertas de proximidad (CPA/TCPA). La logica vive en ProximityMonitor; solo
    // se le pasan objetivos mientras las alertas esten activas (coste cero si no).
    ProximityMonitor proximity;
    // Velocidad (m/s) y rumbo dados por la app; mandan sobre la estima por traza.
    QHash<qint64, QPair<double, double>> reportedVelocity;
    bool proximityLines = true;
    // Ventana de la estima de velocidad por la traza: lo bastante larga para
    // promediar el ruido de posicion, y corta para seguir un cambio de rumbo.
    static constexpr qint64 kVentanaVelocidadMs = 30000;

    // Estado de un objetivo para el monitor: posicion, hora de la ultima
    // posicion y velocidad (la de la app o la estimada; parado si no hay).
    ProximityMonitor::Track trackOf(qint64 id) const
    {
        ProximityMonitor::Track t;
        if (!view)
            return t;
        const TargetModel *m = view->targetModel();
        const auto it = m->entries().constFind(id);
        if (it == m->entries().constEnd())
            return t;
        t.position = it->target.position;
        t.msecs = it->lastMsecs;
        const auto v = reportedVelocity.constFind(id);
        if (v != reportedVelocity.constEnd()) {
            t.speed = v->first;
            t.course = v->second;
        } else {
            double vel = 0.0, rumbo = 0.0;
            if (m->estimateVelocity(id, kVentanaVelocidadMs, &vel, &rumbo)) {
                t.speed = vel;
                t.course = rumbo;
            }
        }
        return t;
    }

    // Texto corto de una alerta para el mapa: "CPA 0.32 NM en 4 min", o la
    // distancia actual si ya estan dentro y se alejan. Unidades de la escala.
    QString textoCpa(const CpaResult &c) const
    {
        const ScaleUnits u = view && view->scaleBarLayer() ? view->scaleBarLayer()->units()
                                                           : ScaleUnits::Metric;
        const auto distancia = [u](double m) { return MapDecor::formatDistance(m, u); };
        if (c.timeSeconds < 0.0)
            return QStringLiteral("%1 (se alejan)").arg(distancia(c.currentDistanceMeters));
        const double s = c.timeSeconds;
        const QString cuando = s < 60.0 ? QStringLiteral("%1 s").arg(qRound(s))
            : s < 3600.0 ? QStringLiteral("%1 min").arg(qRound(s / 60.0))
                         : QStringLiteral("%1 h %2").arg(int(s / 3600.0))
                               .arg(int(std::fmod(s, 3600.0) / 60.0), 2, 10, QLatin1Char('0'));
        return QStringLiteral("CPA %1 en %2").arg(distancia(c.distanceMeters), cuando);
    }

    // Añade un lote de entidades a una capa (creandola si no existe) como UNA
    // sola operacion de deshacer. Sin el grupo, cada entidad apilaba una copia
    // del estado: importar n entidades costaba O(n^2) y dejaba n pasos que
    // deshacer uno a uno. El grupo se abre ANTES de crear la capa, asi que
    // deshacer la importacion quita tambien la capa nueva. \a nuevosIds: las
    // entidades vienen de un fichero y deben recibir id nuevo.
    qint64 addFeaturesAsOneUndo(const QVector<MapFeature> &feats, const QString &layerId,
                                const QString &displayName, bool nuevosIds)
    {
        OverlayModel *modelo = view->overlayModel();
        modelo->beginUndoGroup();
        q->addFeatureLayer(layerId, displayName, 0);
        qint64 primero = -1;
        for (MapFeature f : feats) {
            if (nuevosIds)
                f.id = -1;
            f.layerId = layerId;
            const qint64 id = q->addFeature(f);   // conserva estilo/etiqueta/atributos
            if (primero < 0)
                primero = id;
        }
        modelo->endUndoGroup();
        return primero;
    }

    // Filtro de objetivos: el que puso la app y los que hoy NO lo pasan.
    TargetFilter filtro;
    bool hayFiltro = false;
    QSet<qint64> fueraDeFiltro;

    // ¿Pasa el objetivo el filtro? Clase, velocidad (la de la app o estimada,
    // como en las alertas CPA), zonas vigiladas y el criterio libre de la app.
    bool pasaFiltro(qint64 id) const
    {
        if (!hayFiltro || !view)
            return true;
        const auto it = view->targetModel()->entries().constFind(id);
        if (it == view->targetModel()->entries().constEnd())
            return false;
        const MapTarget &t = it->target;
        if (!filtro.kinds.isEmpty() && !filtro.kinds.contains(t.kind))
            return false;
        if (!std::isnan(filtro.minSpeed) || !std::isnan(filtro.maxSpeed)) {
            const double v = trackOf(id).speed;
            if (!std::isnan(filtro.minSpeed) && v < filtro.minSpeed)
                return false;
            if (!std::isnan(filtro.maxSpeed) && v > filtro.maxSpeed)
                return false;
        }
        if (filtro.onlyInWatchedZones && zones.zonesOf(id).isEmpty())
            return false;
        return !filtro.predicate || filtro.predicate(t);
    }

    // Reevalua UN objetivo (al moverse o cambiar su velocidad) y avisa a la
    // capa solo si cambio. O(1): no recorre los demas.
    void syncFiltro(qint64 id)
    {
        if (!hayFiltro || !view)
            return;
        const bool fuera = view->targetModel()->contains(id) && !pasaFiltro(id);
        if (fuera == fueraDeFiltro.contains(id))
            return;
        if (fuera)
            fueraDeFiltro.insert(id);
        else
            fueraDeFiltro.remove(id);
        view->targetLayer()->setFilteredOut(fueraDeFiltro, filtro.dim);
    }

    // Reevalua todos (al cambiar el filtro) y se lo pasa a la capa.
    void recalcularFiltro()
    {
        fueraDeFiltro.clear();
        if (hayFiltro && view)
            for (auto it = view->targetModel()->entries().cbegin();
                 it != view->targetModel()->entries().cend(); ++it)
                if (!pasaFiltro(it.key()))
                    fueraDeFiltro.insert(it.key());
        if (view && view->targetLayer())
            view->targetLayer()->setFilteredOut(fueraDeFiltro, hayFiltro && filtro.dim);
    }

    // Pasa a la capa de objetivos las lineas de los pares en alerta.
    void refreshProximityLines()
    {
        if (!view || !view->targetLayer())
            return;
        QVector<TargetLayer::ProximityLine> lineas;
        if (proximityLines) {
            for (const ProximityMonitor::Transition &a : proximity.alerts())
                lineas.append({ a.a, a.b, a.cpa.positionA, a.cpa.positionB, textoCpa(a.cpa) });
        }
        view->targetLayer()->setProximityLines(lineas);
    }

    // Emite las transiciones del monitor como señales publicas (con el estado
    // ya al dia) y refresca las lineas del mapa.
    void emitProximityTransitions(const QVector<ProximityMonitor::Transition> &cambios)
    {
        refreshProximityLines();
        for (const ProximityMonitor::Transition &t : cambios) {
            if (t.raised)
                emit q->proximityAlertRaised(t.a, t.b, t.cpa.distanceMeters, t.cpa.timeSeconds);
            else
                emit q->proximityAlertCleared(t.a, t.b);
        }
    }

    // Lleva un objetivo al monitor de proximidad (solo con alertas activas).
    void syncProximity(qint64 id)
    {
        if (!proximity.isEnabled())
            return;
        const bool habiaLineas = proximity.hasAlerts();
        const auto cambios = proximity.updateTrack(id, trackOf(id));
        if (!cambios.isEmpty() || habiaLineas)
            emitProximityTransitions(cambios);
    }

    // Recalcula que entidades estan "en alerta" (zonas vigiladas con algo
    // dentro, y entidades punto dentro de alguna) y se lo pasa a la capa de
    // entidades, que solo redibuja si el conjunto cambio. Se llama tras
    // cualquier cambio de pertenencia, tambien los silenciosos (borrados).
    void refreshAlerts()
    {
        if (!view || !view->featureLayer())
            return;
        QSet<qint64> alerta;
        if (zoneHighlight) {
            for (qint64 z : zones.zones()) {
                const QVector<qint64> puntos = pointZones.targetsIn(z);
                if (!puntos.isEmpty() || !zones.targetsIn(z).isEmpty())
                    alerta.insert(z);
                for (qint64 p : puntos)
                    alerta.insert(p);
            }
        }
        view->featureLayer()->setAlerted(alerta);
        view->replot(QCustomPlot::rpQueuedReplot);
    }

    // Convierte las transiciones de ZoneMonitor en senales publicas. El estado
    // ya esta actualizado al emitir, asi que un slot puede consultar
    // targetsInZone o mover objetivos sin ver datos a medias.
    void emitZoneTransitions(const QVector<ZoneMonitor::Transition> &cambios)
    {
        if (!cambios.isEmpty())
            refreshAlerts();
        if (hayFiltro && filtro.onlyInWatchedZones)
            for (const ZoneMonitor::Transition &t : cambios)
                syncFiltro(t.targetId);
        for (const ZoneMonitor::Transition &t : cambios) {
            if (t.entered)
                emit q->targetEnteredZone(t.targetId, t.zoneId);
            else
                emit q->targetLeftZone(t.targetId, t.zoneId);
        }
    }

    // Igual para las entidades punto: featureEnteredZone / featureLeftZone.
    void emitPointZoneTransitions(const QVector<ZoneMonitor::Transition> &cambios)
    {
        if (!cambios.isEmpty())
            refreshAlerts();
        for (const ZoneMonitor::Transition &t : cambios) {
            if (t.entered)
                emit q->featureEnteredZone(t.targetId, t.zoneId);
            else
                emit q->featureLeftZone(t.targetId, t.zoneId);
        }
    }

    // Lleva la posicion de una entidad al monitor de puntos: si es un punto, su
    // (primera) posicion; si ya no existe o no es un punto, se olvida sin
    // emitir salida. Sin zonas vigiladas no se hace nada (coste cero).
    void syncPoint(qint64 featureId)
    {
        if (!view || zones.zones().isEmpty())
            return;
        const std::optional<MapFeature> f = view->overlayModel()->feature(featureId);
        if (!f || f->kind != GeometryKind::Point) {
            pointZones.removeTarget(featureId);
            return;
        }
        emitPointZoneTransitions(pointZones.updateTarget(featureId, f->position()));
    }

    // Resincroniza TODAS las entidades punto con el modelo: tras cargas en bloque
    // o deshacer, que solo avisan con layersChanged. Olvida las que ya no estan.
    // O(puntos x zonas) con descarte por caja; solo si hay zonas vigiladas.
    void syncAllPoints()
    {
        if (!view || zones.zones().isEmpty())
            return;
        QSet<qint64> vivos;
        for (const MapFeature &f : view->overlayModel()->features()) {
            if (f.kind != GeometryKind::Point)
                continue;
            vivos.insert(f.id);
            emitPointZoneTransitions(pointZones.updateTarget(f.id, f.position()));
        }
        for (qint64 id : pointZones.trackedTargets())
            if (!vivos.contains(id))
                pointZones.removeTarget(id);
    }

    // Deja de vigilar una zona en los dos monitores, sin emitir salidas.
    bool dropZone(qint64 featureId)
    {
        pointZones.removeZone(featureId);
        const bool habia = zones.removeZone(featureId);
        if (habia)
            refreshAlerts();
        return habia;
    }

    // Vuelve a leer una zona vigilada de su entidad: si la entidad ya no existe
    // o ha dejado de ser un poligono, se deja de vigilar en silencio; si cambio
    // de forma, se reevalua y se emiten las entradas/salidas. Comparar con el
    // contorno guardado evita reevaluar cuando la edicion no toco la geometria.
    void syncZone(qint64 featureId)
    {
        if (!zones.hasZone(featureId) || !view)
            return;
        const std::optional<MapFeature> f = view->overlayModel()->feature(featureId);
        if (!f || f->kind != GeometryKind::Polygon) {
            dropZone(featureId);
            return;
        }
        const QVector<QVector<QGeoCoordinate>> contorno = f->outlines();
        if (contorno != zones.zoneRings(featureId)) {
            emitZoneTransitions(zones.setZone(featureId, contorno));
            emitPointZoneTransitions(pointZones.setZone(featureId, contorno));
        }
    }

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
            // Una capa fija puede venir de un .geo (un trazado, un estilo) o de
            // una BD vectorial .sqlitedb (muchas entidades con estilo propio, p.
            // ej. curvas de nivel). Se distingue por la extension del fichero.
            const bool esDb = ov.file.endsWith(QLatin1String(".sqlitedb"), Qt::CaseInsensitive)
                           || ov.file.endsWith(QLatin1String(".db"), Qt::CaseInsensitive);
            const qint64 fid = esDb
                ? q->loadFeaturesAsLayer(ov.file, ov.id, ov.name, &motivo)
                : q->loadGeoAsLayer(ov.file, ov.id, ov.name, ov.style, &motivo);
            if (fid < 0) {
                qCWarning(lcMapaRender) << "Capa fija" << ov.id << "no cargada:" << motivo;
                continue;
            }
            modelo->setLayerEditable(ov.id, false);
            // Decoracion de fondo: ocultas y sin poder elegirse con el raton salvo
            // que el mapa.json lo pida ("visible" / "selectable").
            modelo->setLayerVisible(ov.id, ov.visible);
            modelo->setLayerSelectable(ov.id, ov.selectable);
            modelo->setLayerZOrder(ov.id, ov.zOrder);
            modelo->setLayerZoomRange(ov.id, ov.minZoom, ov.maxZoom);
        }
    }
};

// Instala \a path como origen de elevacion (vacia = quita) y deja en \a d una
// descripcion para el usuario y los problemas encontrados. Una carpeta sin `.hgt`
// ni GeoTIFF, o una BD que no existe, no es un origen utilizable: se avisa en vez
// de quedarse en silencio con una elevacion que siempre da «sin dato».
static void instalarElevacion(std::unique_ptr<IElevationSource> &elevation, QString &desc,
                              QStringList &warn, const QString &path)
{
    elevation.reset();
    desc.clear();
    warn.clear();
    if (path.isEmpty())
        return;

    QString avisos;
    elevation = openElevationSource(path, &avisos);
    if (!avisos.isEmpty())
        warn = avisos.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    const QFileInfo info(path);
    if (const auto *tif = dynamic_cast<const GeoTiffElevation *>(elevation.get())) {
        double la0, la1, lo0, lo1;
        if (tif->extent(&la0, &la1, &lo0, &lo1))
            desc = QObject::tr("%1 GeoTIFF, lat %2..%3, lon %4..%5")
                                   .arg(tif->imageCount())
                                   .arg(la0, 0, 'f', 1).arg(la1, 0, 'f', 1)
                                   .arg(lo0, 0, 'f', 1).arg(lo1, 0, 'f', 1);
    } else if (info.isDir()) {
        const int hgt = int(QDir(path).entryList({ QStringLiteral("*.hgt"), QStringLiteral("*.HGT") },
                                                 QDir::Files).size());
        if (hgt > 0)
            desc = QObject::tr("%1 ficheros .hgt").arg(hgt);
        else
            warn << QObject::tr("La carpeta %1 no tiene ficheros .hgt ni GeoTIFF (.tif)")
                                    .arg(QDir::toNativeSeparators(path));
    } else if (info.isFile()) {
        desc = QObject::tr("base de datos %1").arg(info.fileName());
    } else {
        warn << QObject::tr("No existe %1").arg(QDir::toNativeSeparators(path));
    }
    if (desc.isEmpty())
        elevation.reset();           // sin datos utilizables: que no parezca que hay DEM
}

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
        d->pendingOverlay = paquete->vectorOverlay;
        for (const DataPackage::Overlay &ov : paquete->overlays)
            d->fixedLayers.insert(ov.id);

        if (cfg.datasetsFile.isEmpty())
            datasets = paquete->datasets;
        if (cfg.elevationDbFile.isEmpty() && cfg.elevationDir.isEmpty()) {
            cfg.elevationDbFile = paquete->elevationFile;
            cfg.elevationDir = paquete->elevationDir;
        }
        // Solo si la app quiere autoguardado (por defecto): si no fijo un
        // featuresDbFile propio, se usa el del paquete. Con autosaveFeatures=false
        // (guardado explicito) NO se toca el fichero del paquete.
        if (cfg.placesDbFile.isEmpty())
            cfg.placesDbFile = paquete->placesFile;
        if (cfg.autosaveFeatures && cfg.featuresDbFile.isEmpty()) {
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

    // Origen de elevacion: la base de datos tiene prioridad sobre la carpeta. El
    // formato lo da la ruta (SQLite, carpeta de .hgt o GeoTIFF; ver
    // openElevationSource), asi que ambos campos admiten cualquiera de ellos.
    instalarElevacion(d->elevation, d->elevationDesc, d->elevationWarn,
                      !cfg.elevationDbFile.isEmpty() ? cfg.elevationDbFile : cfg.elevationDir);
    for (const QString &aviso : d->elevationWarn)
        qCWarning(lcMapaRender) << aviso;
    d->places.setDatabase(cfg.placesDbFile);

    if (!cfg.initialLayerId.isEmpty())
        d->service.setActiveDataset(cfg.initialLayerId);

    d->view = new MapView(&d->service, this);
    d->layout->addWidget(d->view);

    d->view->tileLayer()->setNoDataColor(cfg.noDataColor);

    // Overlay vectorial del paquete (mapa hibrido), si lo declara.
    if (d->pendingOverlay) {
        QString motivo;
        if (!d->installVectorOverlay(d->pendingOverlay->dataset, d->pendingOverlay->visible,
                                  d->pendingOverlay->hiddenGroupsSet, &motivo))
            qCWarning(lcMapaRender) << "Overlay vectorial:" << motivo;
        d->pendingOverlay.reset();
    }

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
    // Clic derecho: se resuelve aqui que hay debajo, para que la app solo
    // tenga que montar el menu.
    connect(d->view, &MapView::contextMenuRequested, this,
            [this](const QPoint &pixel, const QPoint &global) {
                emit contextMenuRequested(hitTest(pixel), global);
            });
    connect(d->view, &MapView::elementClicked, this,
            [this](const QPoint &pixel, const QPoint &global) {
                emit elementClicked(hitTest(pixel), global);
            });
    connect(d->view, &MapView::pickCancelled, this, &MapWidget::pickCancelled);
    connect(d->view, &MapView::interactionBlocked, this, &MapWidget::interactionBlocked);
    connect(d->view, &MapView::measurementFinished,
            this, &MapWidget::measurementFinished);
    connect(d->view, &MapView::areaSelected, this, &MapWidget::areaSelected);
    connect(d->view, &MapView::polygonSelected, this, &MapWidget::polygonSelected);
    connect(d->view, &MapView::areaMeasured, this, &MapWidget::areaMeasured);
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
    // Zonas vigiladas: seguir la geometria de su entidad. Las cargas en bloque y
    // el deshacer solo emiten layersChanged, por eso tambien se resincroniza ahi.
    // Las entidades PUNTO tambien se vigilan: alta, movimiento (edicion o
    // arrastre) y borrado; en bloque, se resincronizan todas (primero los
    // puntos, luego las zonas).
    connect(modelo, &OverlayModel::featureAdded, this,
            [this](qint64 id) { d->syncPoint(id); });
    connect(modelo, &OverlayModel::featureUpdated, this, [this](qint64 id) {
        d->syncPoint(id);
        d->syncZone(id);
    });
    connect(modelo, &OverlayModel::featureRemoved, this, [this](qint64 id) {
        d->dropZone(id);
        d->pointZones.removeTarget(id);
        d->refreshAlerts();
    });
    connect(modelo, &OverlayModel::layersChanged, this, [this] {
        d->syncAllPoints();
        for (qint64 id : d->zones.zones())
            d->syncZone(id);
    });
    connect(d->view, &MapView::targetClicked, this, &MapWidget::targetClicked);
    connect(d->view, &MapView::targetClusterClicked, this, &MapWidget::targetClusterClicked);
    connect(d->view, &MapView::featureCreated, this, &MapWidget::featureCreated);
    connect(d->view, &MapView::featureDragStarted, this, &MapWidget::featureDragStarted);
    connect(d->view, &MapView::featureDragMovedTo, this, &MapWidget::featureDragMovedTo);
    connect(d->view, &MapView::featureDragFinished, this, &MapWidget::featureDragFinished);
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
    if (cfg.autosaveFeatures && !cfg.featuresDbFile.isEmpty())
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

// Grupos del estilo de la capa con su visibilidad actual (los de hiddenGroups,
// apagados).
QVector<BaseLayerGroup> MapWidget::baseLayerGroups(const QString &layerId) const
{
    const QString id = layerId.isEmpty() ? d->service.activeDatasetId() : layerId;
    const TileDataset *ds = d->service.dataset(id);
    QVector<BaseLayerGroup> out;
    if (!ds)
        return out;
    for (const auto &g : d->service.vectorGroups(id))
        out.append(BaseLayerGroup{ g.first, g.second, !ds->vectorHiddenGroups.contains(g.first) });
    return out;
}

// Cambia un grupo y, si la capa es la que se ve, vuelve a pedir el viewport (la
// cache de esa capa ya la vacio el servicio).
bool MapWidget::setBaseLayerGroupVisible(const QString &group, bool visible,
                                         const QString &layerId)
{
    const QString id = layerId.isEmpty() ? d->service.activeDatasetId() : layerId;
    const TileDataset *ds = d->service.dataset(id);
    if (!ds)
        return false;
    bool known = false;
    for (const auto &g : d->service.vectorGroups(id))
        known = known || g.first == group;
    if (!known)
        return false;
    QStringList hidden = ds->vectorHiddenGroups;
    if (visible == !hidden.contains(group))
        return true;                                  // ya estaba asi
    if (visible)
        hidden.removeAll(group);
    else
        hidden.append(group);
    if (!d->service.setVectorHiddenGroups(id, hidden))
        return false;
    if (id == d->service.activeDatasetId() && d->view) {
        d->syncGeometry();
        d->view->requestVisibleTiles();
        d->view->refreshPlan();
    }
    return true;
}

// ---------------------------------------------------- «que hay aqui» ------

namespace {

// Entidades de un dataset vectorial bajo el punto. Abre la fuente en el hilo que
// llama (la conexion SQLite es de quien la abre) con un id propio, para no
// compartir conexion con el hilo trabajador ni con otro fichero. Si el zoom pedido
// no tiene teselas (se mira por encima del maximo del MBTiles), usa la del nivel
// mas alto que si la tenga: la geometria es la misma, solo menos detallada.
QVector<VectorFeatureInfo> featuresInDataset(const TileDataset &dataset, bool overlay,
                                             const QGeoCoordinate &pos, int zoom, double radiusM)
{
    QVector<VectorFeatureInfo> out;
    if (!dataset.vector)
        return out;
    TileDataset ds = dataset;
    ds.id = QStringLiteral("quehayaqui:") + dataset.filePath;
    RMapsTileSource src(ds);
    if (!src.open())
        return out;

    for (int z = qMin(zoom, ds.maxZoom); z >= ds.minZoom; --z) {
        const double n = double(1 << z);
        const double latR = qDegreesToRadians(WebMercator::clampLatitude(pos.latitude()));
        const TileKey key{ z,
                           int(std::floor((pos.longitude() + 180.0) / 360.0 * n)),
                           int(std::floor((1.0 - std::asinh(std::tan(latR)) / M_PI) / 2.0 * n)) };
        const QByteArray bytes = src.fetch(key);
        if (bytes.isEmpty())
            continue;
        mvt::Tile tile;
        if (!mvt::decode(bytes, &tile))
            return out;
        for (const featurequery::Hit &h : featurequery::query(tile, key, pos.latitude(),
                                                             pos.longitude(), radiusM)) {
            VectorFeatureInfo info;
            info.layer = h.layer;
            info.geometry = h.type == mvt::GeomType::Point ? QStringLiteral("point")
                          : h.type == mvt::GeomType::LineString ? QStringLiteral("line")
                                                                : QStringLiteral("polygon");
            info.distanceM = h.distanceM;
            info.attributes = h.attributes;
            info.fromOverlay = overlay;
            out.append(info);
        }
        break;
    }
    return out;
}

} // namespace

// Entidades vectoriales bajo \a position: primero las del overlay, luego las de la base.
QVector<VectorFeatureInfo> MapWidget::vectorFeaturesAt(const QGeoCoordinate &position,
                                                       double radiusMeters) const
{
    QVector<VectorFeatureInfo> out;
    if (!position.isValid())
        return out;
    const int z = zoom();
    double radius = radiusMeters;
    if (radius < 0.0) {
        const double mPerPx = 156543.03392804097
            * std::cos(qDegreesToRadians(position.latitude())) / double(1 << qBound(0, z, 30));
        radius = 8.0 * mPerPx;
    }
    if (d->vectorOverlay && d->vectorOverlayVisible) {
        if (const TileDataset *ds = d->vectorOverlay->activeDataset())
            out += featuresInDataset(*ds, true, position, z, radius);
    }
    if (const TileDataset *ds = d->service.activeDataset())
        out += featuresInDataset(*ds, false, position, z, radius);
    return out;
}

// ------------------------------------------------- mapa hibrido (overlay) --

// Lee el MBTiles, comprueba que sea vectorial y lo instala como overlay.
bool MapWidget::setVectorOverlay(const QString &file, QString *error, const QString &styleFile)
{
    QString motivo;
    TileDataset ds = MBTiles::fromFile(file, QStringLiteral("hibrido"), &motivo);
    if (motivo.isEmpty() && ds.isValid() && !ds.vector)
        motivo = tr("%1 no es un MBTiles vectorial (pbf)").arg(file);
    if (!motivo.isEmpty() || !ds.isValid()) {
        if (error)
            *error = motivo.isEmpty() ? tr("No se puede usar %1").arg(file) : motivo;
        return false;
    }
    ds.vectorStyleFile = styleFile;
    return d->installVectorOverlay(ds, true, false, error);
}

/*
 * Arranca el servicio del overlay y se lo da a la vista. Un overlay no lleva
 * mascara de tierra (no hay mar que pintar: debajo esta la base) y su nivel de
 * fondo es el ultimo: mientras llega una tesela no se rellena con una de mucho
 * menos zoom ampliada, que con texto se ve fatal; solo con la del nivel de
 * encima. Los grupos apagados salen del estilo ("visible": false) salvo que el
 * paquete diga los suyos.
 */
bool MapWidget::Impl::installVectorOverlay(TileDataset ds, bool visible, bool hiddenGroupsSet,
                                           QString *motivo)
{
    Impl *d = this;   // mismo cuerpo que el resto de MapWidget
    ds.vectorOverlay = true;
    ds.vectorLandMask.clear();
    ds.baseZoom = ds.maxZoom;
    // Sin "hiddenGroups" propio, TileService apaga los que el estilo marca.
    ds.vectorHiddenGroupsSet = hiddenGroupsSet;

    if (d->view)
        d->view->setOverlayService(nullptr);
    d->vectorOverlay.reset();

    auto servicio = std::make_unique<TileService>();
    if (!servicio->start({ ds }, qMax(32, d->config.cacheMiB / 2))) {
        if (motivo)
            *motivo = MapWidget::tr("No se pudo abrir el overlay %1").arg(ds.filePath);
        return false;
    }
    servicio->setDebounceMs(d->config.debounceMs);
    d->vectorOverlay = std::move(servicio);
    d->vectorOverlayVisible = visible;
    if (d->view) {
        d->syncGeometry();
        d->view->setOverlayService(d->vectorOverlay.get());
        d->view->overlayTileLayer()->setVisible(visible);
        d->view->requestVisibleTiles();
        d->view->refreshPlan();
    }
    emit q->vectorOverlayChanged();
    return true;
}

// Quita el overlay: la vista suelta el servicio antes de destruirlo.
void MapWidget::clearVectorOverlay()
{
    if (!d->vectorOverlay)
        return;
    if (d->view)
        d->view->setOverlayService(nullptr);
    d->vectorOverlay.reset();
    if (d->view)
        d->view->refreshPlan();
    emit vectorOverlayChanged();
}

// ¿Hay overlay vectorial puesto?
bool MapWidget::hasVectorOverlay() const
{
    return d->vectorOverlay != nullptr;
}

// Fichero del overlay, o vacio.
QString MapWidget::vectorOverlayFile() const
{
    const TileDataset *ds = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    return ds ? ds->filePath : QString();
}

// Enciende o apaga la capa del overlay; al encenderla pide las teselas que falten.
void MapWidget::setVectorOverlayVisible(bool visible)
{
    if (!d->vectorOverlay || d->vectorOverlayVisible == visible)
        return;
    d->vectorOverlayVisible = visible;
    if (d->view && d->view->overlayTileLayer()) {
        d->view->overlayTileLayer()->setVisible(visible);
        d->view->requestVisibleTiles();
        d->view->refreshPlan();
    }
    emit vectorOverlayChanged();
}

// ¿Se ve el overlay?
bool MapWidget::isVectorOverlayVisible() const
{
    return d->vectorOverlay && d->vectorOverlayVisible;
}

// Grupos del overlay con su visibilidad actual.
QVector<BaseLayerGroup> MapWidget::vectorOverlayGroups() const
{
    QVector<BaseLayerGroup> out;
    const TileDataset *ds = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    if (!ds)
        return out;
    for (const auto &g : d->vectorOverlay->vectorGroups(ds->id))
        out.append(BaseLayerGroup{ g.first, g.second, !ds->vectorHiddenGroups.contains(g.first) });
    return out;
}

// Cambia un grupo del overlay: el servicio vacia su cache y aqui se vuelve a pedir.
bool MapWidget::setVectorOverlayGroupVisible(const QString &group, bool visible)
{
    const TileDataset *ds = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    if (!ds)
        return false;
    bool known = false;
    for (const auto &g : d->vectorOverlay->vectorGroups(ds->id))
        known = known || g.first == group;
    if (!known)
        return false;
    QStringList hidden = ds->vectorHiddenGroups;
    if (visible == !hidden.contains(group))
        return true;
    if (visible)
        hidden.removeAll(group);
    else
        hidden.append(group);
    if (!d->vectorOverlay->setVectorHiddenGroups(ds->id, hidden))
        return false;
    if (d->view) {
        d->syncGeometry();
        d->view->requestVisibleTiles();
        d->view->refreshPlan();
    }
    emit vectorOverlayChanged();
    return true;
}

// Quita la marca de "seleccionar area" o "seleccionar poligono" que queda
// visible tras emitir la seleccion.
void MapWidget::clearAreaSelection()
{
    if (d->view)
        d->view->clearAreaSelection();
}

// ------------------------------------------------------ exportar la vista --

namespace {

// Texto de una coordenada para el pie: decimal con 5 cifras (~1 m), corto y
// sin ambiguedad para cualquiera que lo lea.
QString textoPie(const QGeoCoordinate &c)
{
    return c.isValid() ? QStringLiteral("%1, %2").arg(c.latitude(), 0, 'f', 5)
                                                 .arg(c.longitude(), 0, 'f', 5)
                       : QStringLiteral("?");
}

// Redondea el denominador de escala a dos o tres cifras significativas
// (1:23 456 -> 1:23 500): la escala de una hoja Mercator ya es aproximada.
qint64 escalaRedonda(double denominador)
{
    if (!(denominador > 0.0) || !std::isfinite(denominador))
        return 0;
    const double p = std::pow(10.0, std::floor(std::log10(denominador)) - 2.0);
    return qint64(std::llround(std::round(denominador / p) * p));
}

// Entero con los miles separados por espacios (1 570 000), como en las cartas;
// no depende del idioma del sistema.
QString conMiles(qint64 n)
{
    QString s = QString::number(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(i, QLatin1Char(' '));
    return s;
}

// Lineas del pie: extension de la vista y zoom (y la escala, si se conoce), la
// fecha UTC de la exportacion, y las notas y la atribucion si las hay.
QStringList lineasPie(const QGeoCoordinate &nw, const QGeoCoordinate &se, int zoom,
                      qint64 escala, const MapExportOptions &o)
{
    QString l1 = QStringLiteral("NO %1  \u2014  SE %2   \u00b7   zoom %3")
                     .arg(textoPie(nw), textoPie(se)).arg(zoom);
    if (escala > 0)
        l1 += QStringLiteral("   \u00b7   escala aprox. 1:%1 (en el centro)")
                  .arg(conMiles(escala));
    QStringList l2;
    l2 << QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
    if (!o.notes.isEmpty())
        l2 << o.notes;
    if (!o.attribution.isEmpty())
        l2 << o.attribution;
    return { l1, l2.join(QStringLiteral("   \u00b7   ")) };
}

} // namespace

// Dibuja la vista con QCustomPlot::toPixmap al tamaño del widget por \a scale.
// Antes se fuerza un replot inmediato: si el widget no se ha pintado aun, la
// disposicion de los ejes (y con ella la posicion de todo) estaria sin calcular.
QImage MapWidget::renderImage(double scale) const
{
    if (!d->view)
        return QImage();
    d->syncGeometry();
    const double s = (scale > 0.0 && std::isfinite(scale)) ? scale : 1.0;
    d->view->replot(QCustomPlot::rpImmediateRefresh);
    return d->view->toPixmap(d->view->width(), d->view->height(), s).toImage();
}

// Guarda la vista: PDF (vectorial, con escala de impresion en el pie) o imagen
// (titulo y pie como bandas encima y debajo). Siempre a traves de QSaveFile.
bool MapWidget::exportView(const QString &path, const MapExportOptions &o, QString *error)
{
    const auto fallo = [&](const QString &motivo) {
        if (error)
            *error = motivo;
        emit errorOccurred(motivo);
        return false;
    };
    if (!d->view)
        return fallo(QStringLiteral("No hay mapa que exportar"));
    d->syncGeometry();
    const int vw = d->view->width(), vh = d->view->height();
    if (vw < 2 || vh < 2)
        return fallo(QStringLiteral("La vista no tiene tamaño"));

    const QString ext = QFileInfo(path).suffix().toLower();
    const bool pdf = ext == QLatin1String("pdf");
    if (!pdf && !QImageWriter::supportedImageFormats().contains(ext.toLatin1()))
        return fallo(QStringLiteral("%1: formato no reconocido (usa .pdf, .png, .jpg...)").arg(path));

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return fallo(QStringLiteral("No se pudo escribir %1: %2").arg(path, f.errorString()));

    const QGeoCoordinate nw = visibleNorthWest(), se = visibleSouthEast();
    const QColor tinta(0x20, 0x20, 0x20);

    if (pdf) {
        // Resolucion = la logica de la pantalla: asi los tamaños de letra y de
        // linea de las capas guardan la misma proporcion que en pantalla. No
        // resta calidad: vectores y texto son vectores, y las teselas van a su
        // resolucion nativa.
        const int dpi = qMax(72, d->view->logicalDpiX());
        QPdfWriter pdfw(&f);
        pdfw.setResolution(dpi);
        pdfw.setCreator(QStringLiteral("libmapa"));
        pdfw.setTitle(o.title);
        const double ancho = qMax(50.0, o.pageWidthMm), alto = qMax(50.0, o.pageHeightMm);
        const bool apaisada = vw > vh;
        pdfw.setPageLayout(QPageLayout(QPageSize(QSizeF(qMin(ancho, alto), qMax(ancho, alto)),
                                                 QPageSize::Millimeter),
                                       apaisada ? QPageLayout::Landscape : QPageLayout::Portrait,
                                       QMarginsF(o.marginMm, o.marginMm, o.marginMm, o.marginMm),
                                       QPageLayout::Millimeter));

        QCPPainter p;
        if (!p.begin(&pdfw))
            return fallo(QStringLiteral("No se pudo crear el PDF %1").arg(path));
        p.setMode(QCPPainter::pmVectorized);
        p.setMode(QCPPainter::pmNoCaching);
        p.setPen(tinta);
        const QRectF hoja(0, 0, pdfw.width(), pdfw.height());
        double y = 0.0;
        QRectF titulo;      // se escribe al final, alineado con el mapa ya centrado
        if (!o.title.isEmpty()) {
            QFont ft = font();
            ft.setPointSizeF(15.0);
            ft.setBold(true);
            const double h = QFontMetricsF(ft, &pdfw).height() * 1.4;
            titulo = QRectF(0, 0, hoja.width(), h);
            y = h;
        }
        QFont fp = font();
        fp.setPointSizeF(7.5);
        const double hLinea = QFontMetricsF(fp, &pdfw).height();
        const double hPie = o.footer ? hLinea * 2.6 : 0.0;

        // El mapa, encajado sin deformar en lo que queda de hoja.
        const QRectF hueco(0, y, hoja.width(), hoja.height() - y - hPie);
        const double k = qMin(hueco.width() / vw, hueco.height() / vh);
        const QRectF mapa(hueco.left() + (hueco.width() - vw * k) / 2.0, hueco.top(),
                          vw * k, vh * k);
        d->view->replot(QCustomPlot::rpImmediateRefresh);
        p.save();
        p.translate(mapa.topLeft());
        p.scale(k, k);
        p.setClipRect(QRectF(0, 0, vw, vh));
        d->view->toPainter(&p, vw, vh);
        p.restore();
        p.setPen(QPen(tinta, 0.8));
        p.setBrush(Qt::NoBrush);
        p.drawRect(mapa);
        if (!titulo.isNull()) {
            QFont ft = font();
            ft.setPointSizeF(15.0);
            ft.setBold(true);
            p.setFont(ft);
            p.drawText(QRectF(mapa.left(), titulo.top(), mapa.width(), titulo.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, o.title);
        }

        if (o.footer) {
            // Escala de impresion: metros de terreno que cubre el ancho del
            // mapa entre lo que mide ese ancho en la hoja.
            const double mmHoja = mapa.width() / dpi * 25.4;
            const double terreno = metersPerPixel() * vw;
            const QStringList lineas = lineasPie(nw, se, zoom(),
                                                 escalaRedonda(terreno / (mmHoja / 1000.0)), o);
            p.setFont(fp);
            p.setPen(tinta);
            double yl = mapa.bottom() + hLinea * 0.4;
            for (const QString &l : lineas) {
                p.drawText(QRectF(mapa.left(), yl, hoja.width() - mapa.left(), hLinea),
                           Qt::AlignLeft | Qt::AlignVCenter, l);
                yl += hLinea * 1.1;
            }
        }
        p.end();
    } else {
        const double s = (o.imageScale > 0.0 && std::isfinite(o.imageScale)) ? o.imageScale : 1.0;
        QImage img = renderImage(s);
        if (img.isNull())
            return fallo(QStringLiteral("No se pudo dibujar la vista"));
        if (!o.title.isEmpty() || o.footer) {
            // Bandas blancas: titulo encima y pie debajo, en pixeles de la
            // imagen (escalados con ella).
            QFont ft = font();
            ft.setPixelSize(qRound(17 * s));
            ft.setBold(true);
            QFont fp = font();
            fp.setPixelSize(qRound(11 * s));
            const int hTit = o.title.isEmpty() ? 0 : qRound(QFontMetricsF(ft).height() * 1.6);
            const int hLinea = qRound(QFontMetricsF(fp).height() * 1.15);
            const QStringList lineas = o.footer ? lineasPie(nw, se, zoom(), 0, o) : QStringList();
            const int hPie = lineas.isEmpty() ? 0 : hLinea * int(lineas.size()) + qRound(8 * s);
            QImage total(img.width(), img.height() + hTit + hPie, QImage::Format_RGB32);
            total.fill(Qt::white);
            QPainter p(&total);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.drawImage(0, hTit, img);
            p.setPen(tinta);
            const int margen = qRound(8 * s);
            if (hTit > 0) {
                p.setFont(ft);
                p.drawText(QRect(margen, 0, total.width() - 2 * margen, hTit),
                           Qt::AlignLeft | Qt::AlignVCenter, o.title);
            }
            p.setFont(fp);
            int yl = hTit + img.height() + qRound(4 * s);
            for (const QString &l : lineas) {
                p.drawText(QRect(margen, yl, total.width() - 2 * margen, hLinea),
                           Qt::AlignLeft | Qt::AlignVCenter, l);
                yl += hLinea;
            }
            p.end();
            img = total;
        }
        // JPEG no tiene canal alfa: se aplana sobre blanco para que lo
        // transparente no salga negro.
        if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) {
            QImage plano(img.size(), QImage::Format_RGB32);
            plano.fill(Qt::white);
            QPainter pp(&plano);
            pp.drawImage(0, 0, img);
            pp.end();
            img = plano;
        }
        if (!img.save(&f, ext.toLatin1().constData()))
            return fallo(QStringLiteral("No se pudo codificar la imagen %1").arg(path));
    }

    if (!f.commit())
        return fallo(QStringLiteral("No se pudo escribir %1: %2").arg(path, f.errorString()));
    return true;
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

// Permite o impide seleccionar con el raton las entidades de una capa.
bool MapWidget::setFeatureLayerSelectable(const QString &id, bool selectable)
{
    return d->view ? d->view->overlayModel()->setLayerSelectable(id, selectable)
                   : false;
}

// Cambia el orden de pintado de una capa.
bool MapWidget::setFeatureLayerZOrder(const QString &id, int zOrder)
{
    return d->view ? d->view->overlayModel()->setLayerZOrder(id, zOrder) : false;
}

// Fija el rango de zoom en que se dibuja (y selecciona) una capa (−1 = sin
// límite). Útil para capas de detalle como las curvas de nivel, que a vista
// general estorban: con minZoom se ocultan hasta acercar.
bool MapWidget::setFeatureLayerZoomRange(const QString &id, int minZoom, int maxZoom)
{
    return d->view ? d->view->overlayModel()->setLayerZoomRange(id, minZoom, maxZoom)
                   : false;
}

// Marca (o desmarca) una capa como TEMPORAL: sus entidades y la propia capa se
// EXCLUYEN del guardado (saveFeaturesTo), igual que las capas fijas del paquete.
// Útil para resultados de análisis o pines auxiliares que no deben persistir.
void MapWidget::setFeatureLayerTransient(const QString &id, bool transient)
{
    if (transient)
        d->fixedLayers.insert(id);
    else
        d->fixedLayers.remove(id);
    // Tampoco entra en deshacer: lo temporal lo pone y quita la app, y un
    // "deshacer" del usuario no debe hacerlo desaparecer ni reaparecer.
    if (d->view)
        d->view->overlayModel()->setLayerUndoable(id, !transient);
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

// Carga una BD vectorial (esquema de VectorRepository) como una capa fija mas,
// AÑADIENDO sus entidades sin reemplazar las existentes (al reves que
// loadFeaturesFrom, que hace setContents). Cada entidad conserva su estilo,
// etiqueta y atributos; todas quedan bajo layerId. Devuelve el id de la primera.
qint64 MapWidget::loadFeaturesAsLayer(const QString &databasePath,
                                      const QString &layerId,
                                      const QString &displayName, QString *error)
{
    if (!d->view)
        return -1;

    VectorRepository repo;
    if (!repo.open(databasePath)) {
        if (error)
            *error = repo.lastError();
        emit errorOccurred(repo.lastError());
        return -1;
    }
    const QVector<MapFeature> feats = repo.loadFeatures();
    if (feats.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 no tiene entidades").arg(databasePath);
        return -1;
    }

    return d->addFeaturesAsOneUndo(feats, layerId,
                                   displayName.isEmpty() ? layerId : displayName, false);
}

// Importa GeoJSON/KML/GPX: lee con VectorFormats y AÑADE cada entidad a la
// capa (creandola si no existe), como loadFeaturesAsLayer. El fichero aporta
// nombre, atributos y estilo; la capa la pone quien importa.
qint64 MapWidget::importVectorFile(const QString &path, const QString &layerId,
                                   const QString &displayName, QString *error)
{
    if (!d->view)
        return -1;

    QString motivo;
    const QVector<MapFeature> feats = readVectorFile(path, &motivo);
    if (feats.isEmpty()) {
        if (error)
            *error = motivo;
        emit errorOccurred(motivo);
        return -1;
    }

    return d->addFeaturesAsOneUndo(feats, layerId,
                                   displayName.isEmpty() ? layerId : displayName, true);
}

// Exporta las entidades (todas, o las de una capa) al formato que diga la
// extension. Sin entidades no se escribe nada: un fichero vacio solo confunde.
bool MapWidget::exportVectorFile(const QString &path, const QString &layerId,
                                 QString *error)
{
    const QVector<MapFeature> feats = layerId.isEmpty() ? features()
                                                        : featuresInLayer(layerId);
    if (feats.isEmpty()) {
        if (error)
            *error = layerId.isEmpty()
                ? QStringLiteral("No hay entidades que exportar")
                : QStringLiteral("La capa '%1' no tiene entidades").arg(layerId);
        return false;
    }
    QString motivo;
    if (!writeVectorFile(path, feats, &motivo)) {
        if (error)
            *error = motivo;
        emit errorOccurred(motivo);
        return false;
    }
    return true;
}

// ------------------------------------------------------ objetivos moviles --

// Reenvios al TargetModel (capa dinamica de objetivos moviles), cada uno
// documentado en TargetModel; devuelven un valor neutro sin vista.

// Inserta o actualiza un objetivo movil; devuelve su id.
qint64 MapWidget::addTarget(const MapTarget &target, const QDateTime &time)
{
    if (!d->view)
        return -1;
    const qint64 id = d->view->targetModel()->upsert(target, time);
    if (id >= 0) {
        d->emitZoneTransitions(d->zones.updateTarget(id, target.position));
        d->syncProximity(id);
        d->syncFiltro(id);
    }
    return id;
}

// Mueve un objetivo a una nueva posicion/rumbo (camino rapido de refresco).
bool MapWidget::updateTarget(qint64 id, const QGeoCoordinate &position,
                             double headingDeg, const QDateTime &time)
{
    if (!d->view || !d->view->targetModel()->update(id, position, headingDeg, time))
        return false;
    d->emitZoneTransitions(d->zones.updateTarget(id, position));
    d->syncProximity(id);
    d->syncFiltro(id);
    return true;
}

// Cambia la etiqueta de un objetivo.
bool MapWidget::setTargetLabel(qint64 id, const QString &text)
{
    return d->view && d->view->targetModel()->setLabel(id, text);
}

// Desplazamiento de la etiqueta del objetivo (px). Reenvio al modelo, que avisa
// para repintar. La etiqueta queda unida al simbolo por una linea guia.
bool MapWidget::setTargetLabelOffset(qint64 id, const QPointF &offsetPx)
{
    return d->view && d->view->targetModel()->setLabelOffset(id, offsetPx);
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

// Elimina un objetivo; tambien lo olvidan las zonas vigiladas, sin emitir salida.
bool MapWidget::removeTarget(qint64 id)
{
    d->zones.removeTarget(id);
    d->refreshAlerts();
    d->proximity.removeTrack(id);
    d->reportedVelocity.remove(id);
    d->refreshProximityLines();
    if (d->fueraDeFiltro.remove(id) && d->view)
        d->view->targetLayer()->setFilteredOut(d->fueraDeFiltro, d->filtro.dim);
    return d->view && d->view->targetModel()->remove(id);
}

// Elimina todos los objetivos (las zonas vigiladas quedan vacias, sin salidas).
void MapWidget::clearTargets()
{
    d->zones.clearTargets();
    d->refreshAlerts();
    d->proximity.clear();
    d->reportedVelocity.clear();
    d->refreshProximityLines();
    d->fueraDeFiltro.clear();
    if (d->view)
        d->view->targetLayer()->setFilteredOut(d->fueraDeFiltro, d->filtro.dim);
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

// Historial completo (desde el origen) de un objetivo; vacio si no existe.
QVector<TargetTrackPoint> MapWidget::targetHistory(qint64 id) const
{
    return d->view ? d->view->targetModel()->history(id) : QVector<TargetTrackPoint>();
}

// Un track GPX por objetivo con historial (todos si \a ids viene vacio): la
// etiqueta (primera linea) como nombre, el kind como tipo y el id como
// extension, para poder volver a asociarlo al releerlo.
bool MapWidget::exportTargetHistory(const QString &path, const QVector<qint64> &ids,
                                    QString *error)
{
    QVector<qint64> lista = ids;
    if (lista.isEmpty())
        for (const MapTarget &t : targets())
            lista.append(t.id);
    std::sort(lista.begin(), lista.end());

    QVector<GpxTrack> tracks;
    for (qint64 id : lista) {
        const auto t = target(id);
        if (!t)
            continue;
        GpxTrack g;
        g.targetId = id;
        g.name = t->label.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (g.name.isEmpty())
            g.name = QStringLiteral("Objetivo %1").arg(id);
        g.type = t->kind;
        g.points = targetHistory(id);
        if (!g.points.isEmpty())
            tracks.append(g);
    }
    if (tracks.isEmpty()) {
        if (error)
            *error = QStringLiteral("No hay historial de objetivos que exportar");
        return false;
    }
    QString motivo;
    if (!writeGpxTracks(path, tracks, &motivo)) {
        if (error)
            *error = motivo;
        emit errorOccurred(motivo);
        return false;
    }
    return true;
}

// Origen (primer punto del historial) de un objetivo.
TargetTrackPoint MapWidget::targetOrigin(qint64 id) const
{
    return d->view ? d->view->targetModel()->origin(id) : TargetTrackPoint();
}

// Tope de puntos del historial por objetivo (se aclara al superarlo).
void MapWidget::setTargetHistoryLimit(int maxPoints)
{
    if (d->view)
        d->view->targetModel()->setHistoryMaxPoints(maxPoints);
}

// Distancia minima entre puntos del historial, en metros.
void MapWidget::setTargetHistoryMinDistance(double meters)
{
    if (d->view)
        d->view->targetModel()->setHistoryMinDistance(meters);
}

// Enciende/apaga el dibujo del historial del objetivo seleccionado.
void MapWidget::setSelectedTargetHistoryVisible(bool on)
{
    if (d->view && d->view->targetLayer())
        d->view->targetLayer()->setSelectedHistoryVisible(on);
}

// Muestra u oculta la capa entera de objetivos (repinta solo esa capa).
void MapWidget::setTargetsVisible(bool visible)
{
    if (d->view && d->view->targetLayer()) {
        d->view->targetLayer()->setVisible(visible);
        d->view->replot(QCustomPlot::rpQueuedReplot);
    }
}

// ------------------------------------------------------ alertas de zona ----

// Empieza a vigilar un poligono: se registra su contorno en ZoneMonitor con las
// posiciones de los objetivos actuales y se emiten las entradas de quienes ya
// estan dentro. Si ya se vigilaba, solo se resincroniza su forma.
bool MapWidget::watchZone(qint64 featureId)
{
    if (!d->view)
        return false;
    const std::optional<MapFeature> f = d->view->overlayModel()->feature(featureId);
    if (!f || f->kind != GeometryKind::Polygon || !f->isValid())
        return false;
    if (d->zones.hasZone(featureId)) {
        d->syncZone(featureId);
        return true;
    }
    d->emitZoneTransitions(d->zones.setZone(featureId, f->outlines()));
    // Puntos: se cargan sus posiciones (si es la primera zona, aun no se
    // seguian) y despues se da de alta la zona, que avisa de los que ya estan.
    d->syncAllPoints();
    d->emitPointZoneTransitions(d->pointZones.setZone(featureId, f->outlines()));
    return d->zones.hasZone(featureId);
}

// Deja de vigilar una zona sin emitir salidas: la app lo ha pedido, ya lo sabe.
bool MapWidget::unwatchZone(qint64 featureId)
{
    return d->dropZone(featureId);
}

// ¿Se vigila esta entidad como zona?
bool MapWidget::isZoneWatched(qint64 featureId) const
{
    return d->zones.hasZone(featureId);
}

// Entidades vigiladas como zona, ordenadas por id.
QVector<qint64> MapWidget::watchedZones() const
{
    return d->zones.zones();
}

// Objetivos dentro de una zona vigilada (vacio si no se vigila).
QVector<qint64> MapWidget::targetsInZone(qint64 featureId) const
{
    return d->zones.targetsIn(featureId);
}

// Zonas vigiladas que contienen a un objetivo.
QVector<qint64> MapWidget::zonesOfTarget(qint64 targetId) const
{
    return d->zones.zonesOf(targetId);
}

// Enciende/apaga el resalte de las zonas ocupadas y sus puntos, y repinta.
void MapWidget::setZoneAlertHighlight(bool on)
{
    d->zoneHighlight = on;
    d->refreshAlerts();
}

// ¿Se resaltan las zonas ocupadas?
bool MapWidget::isZoneAlertHighlight() const
{
    return d->zoneHighlight;
}

// Color del resalte de alerta (rojo por defecto).
void MapWidget::setZoneAlertColor(const QColor &color)
{
    if (!d->view || !d->view->featureLayer())
        return;
    d->view->featureLayer()->setAlertColor(color);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// ------------------------------------------------- alertas de proximidad --

// Activa (o cambia, o apaga con distancia <= 0) las alertas CPA/TCPA. Al
// activarlas se cargan todos los objetivos en el monitor; al apagarlas se emite
// el fin de las activas y se vacia el monitor para no gastar nada.
void MapWidget::setProximityAlert(double distanceMeters, double horizonSeconds)
{
    const bool estaba = d->proximity.isEnabled();
    const bool activar = std::isfinite(distanceMeters) && distanceMeters > 0.0;
    if (activar && !estaba && d->view) {
        for (const MapTarget &t : targets())
            d->proximity.updateTrack(t.id, d->trackOf(t.id));   // sin criterios: no avisa
    }
    d->emitProximityTransitions(d->proximity.setCriteria(activar ? distanceMeters : 0.0,
                                                         horizonSeconds));
    if (!activar)
        d->proximity.clear();
}

// Distancia de alerta en metros (0 = apagadas).
double MapWidget::proximityAlertDistance() const
{
    return d->proximity.distance();
}

// Horizonte de la alerta en segundos.
double MapWidget::proximityAlertHorizon() const
{
    return d->proximity.horizon();
}

// Pares en alerta, ordenados, con su CPA.
QVector<ProximityAlert> MapWidget::proximityAlerts() const
{
    QVector<ProximityAlert> v;
    for (const ProximityMonitor::Transition &t : d->proximity.alerts())
        v.append({ t.a, t.b, t.cpa });
    return v;
}

// CPA de dos objetivos al momento, con el mismo modelo de velocidad que las
// alertas (aunque esten apagadas).
CpaResult MapWidget::targetCpa(qint64 targetA, qint64 targetB) const
{
    if (!d->view || targetA == targetB || !d->view->targetModel()->contains(targetA)
        || !d->view->targetModel()->contains(targetB))
        return CpaResult();
    return ProximityMonitor::evaluate(d->trackOf(targetA), d->trackOf(targetB));
}

// Velocidad/rumbo dados por la app (NaN = volver a estimar por la traza). Se
// reevaluan en el acto los pares del objetivo.
bool MapWidget::setTargetVelocity(qint64 id, double speedMetersPerSecond, double courseDeg)
{
    if (!d->view || !d->view->targetModel()->contains(id))
        return false;
    if (std::isnan(speedMetersPerSecond) || !std::isfinite(courseDeg))
        d->reportedVelocity.remove(id);
    else
        d->reportedVelocity.insert(id, qMakePair(std::max(0.0, speedMetersPerSecond), courseDeg));
    d->syncProximity(id);
    d->syncFiltro(id);
    return true;
}

// ---------------------------------------------- agrupacion de objetivos --

// Activa, cambia o apaga la agrupacion (la dibuja TargetLayer).
void MapWidget::setTargetClustering(const TargetClustering &clustering)
{
    if (d->view && d->view->targetLayer())
        d->view->targetLayer()->setClustering(clustering);
}

// Agrupacion actual (desactivada si no hay vista).
TargetClustering MapWidget::targetClustering() const
{
    return d->view && d->view->targetLayer() ? d->view->targetLayer()->clustering()
                                             : TargetClustering();
}

// Grupo bajo un pixel del widget, segun el ultimo repintado.
QVector<qint64> MapWidget::targetClusterAt(const QPoint &pixelPos) const
{
    if (!d->view || !d->view->targetLayer())
        return {};
    return d->view->targetLayer()->clusterAt(pixelPos);   // la vista ocupa todo el widget, como en targetAt
}

// --------------------------------------------------- filtro de objetivos --

// Fija el filtro (uno vacio lo quita) y reevalua todos los objetivos.
void MapWidget::setTargetFilter(const TargetFilter &filter)
{
    d->filtro = filter;
    d->hayFiltro = !filter.isEmpty();
    d->recalcularFiltro();
}

// Quita el filtro: se vuelven a ver todos.
void MapWidget::clearTargetFilter()
{
    setTargetFilter(TargetFilter());
}

// ¿Hay un filtro activo?
bool MapWidget::hasTargetFilter() const
{
    return d->hayFiltro;
}

// ¿Pasa el objetivo el filtro? (true sin filtro; false si no existe).
bool MapWidget::targetPassesFilter(qint64 id) const
{
    if (!d->view || !d->view->targetModel()->contains(id))
        return false;
    return !d->fueraDeFiltro.contains(id);
}

// Los que no pasan, ordenados.
QVector<qint64> MapWidget::filteredOutTargets() const
{
    QVector<qint64> v;
    v.reserve(d->fueraDeFiltro.size());
    for (qint64 id : d->fueraDeFiltro)
        v.append(id);
    std::sort(v.begin(), v.end());
    return v;
}

// Muestra u oculta las lineas de maximo acercamiento.
void MapWidget::setProximityLinesVisible(bool on)
{
    d->proximityLines = on;
    d->refreshProximityLines();
}

// ¿Se dibujan las lineas CPA?
bool MapWidget::isProximityLinesVisible() const
{
    return d->proximityLines;
}

// Color de las lineas y rotulos CPA.
void MapWidget::setProximityColor(const QColor &color)
{
    if (d->view && d->view->targetLayer() && color.isValid()) {
        d->view->targetLayer()->setProximityColor(color);
        d->refreshProximityLines();
    }
}

// Entidades punto dentro de una zona vigilada (vacio si no se vigila).
QVector<qint64> MapWidget::featuresInZone(qint64 zoneFeatureId) const
{
    return d->pointZones.targetsIn(zoneFeatureId);
}

// Zonas vigiladas que contienen a una entidad punto.
QVector<qint64> MapWidget::zonesOfFeature(qint64 featureId) const
{
    return d->pointZones.zonesOf(featureId);
}

// Objetivos dentro de un poligono cualquiera: un ZoneMonitor de usar y tirar
// con una sola zona, para que el criterio de "dentro" sea el de las alertas.
QVector<qint64> MapWidget::targetsInArea(const QVector<QGeoCoordinate> &polygon) const
{
    ZoneMonitor m;
    m.setZone(0, { polygon });
    if (!m.hasZone(0))
        return {};
    for (const MapTarget &t : targets())
        m.updateTarget(t.id, t.position);
    return m.targetsIn(0);
}

// Entidades con algun vertice dentro del poligono (mismo criterio que arriba).
// Cada vertice se prueba como un "objetivo" del monitor hasta que uno cae dentro.
QVector<qint64> MapWidget::featuresInArea(const QVector<QGeoCoordinate> &polygon) const
{
    ZoneMonitor m;
    m.setZone(0, { polygon });
    if (!m.hasZone(0))
        return {};
    QVector<qint64> ids;
    for (const MapFeature &f : features()) {
        for (const QGeoCoordinate &c : f.geometry) {
            m.updateTarget(f.id, c);
            if (m.isInside(f.id, 0)) {
                ids.append(f.id);
                break;
            }
        }
        m.removeTarget(f.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
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
    // Consulta «que hay debajo»: mira tambien las capas fijas no seleccionables
    // (para un tooltip o una lectura de cota); elegir con el raton no las alcanza.
    return d->view ? d->view->featureLayer()->featureAt(pixel, tolerancePx, true) : -1;
}

// Todo lo que hay bajo un pixel, cada cosa por separado (ver MapHit).
MapHit MapWidget::hitTest(const QPoint &pixel) const
{
    MapHit h;
    h.pixel = pixel;
    if (!d->view)
        return h;
    d->syncGeometry();
    h.coordinate = d->view->coordinateAt(pixel);
    h.targetId = targetAt(pixel);
    h.clusterTargets = targetClusterAt(pixel);
    // Lo que se puede ELEGIR bajo el cursor (el menu contextual no ofrece acciones
    // sobre una provincia de fondo): sin las capas fijas no seleccionables.
    h.featureId = d->view ? d->view->featureLayer()->featureAt(pixel) : -1;
    if (d->view->rangeLayer())
        h.rangeMarkId = d->view->rangeLayer()->markAt(pixel);
    const qint64 sel = d->view->overlayModel()->selectedId();
    if (sel >= 0 && d->view->featureLayer()) {
        const int v = d->view->featureLayer()->vertexAt(sel, pixel);
        if (v >= 0) {
            h.vertexFeature = sel;
            h.vertexIndex = v;
        }
    }
    return h;
}

// Entidad cuyo rotulo cae bajo el pixel (hit-test de la caja de etiqueta).
qint64 MapWidget::featureLabelAt(const QPoint &pixel) const
{
    d->syncGeometry();
    return d->view ? d->view->featureLayer()->labelAt(pixel) : -1;
}

// Objetivo cuyo rotulo dibujado cae bajo el pixel.
qint64 MapWidget::targetLabelAt(const QPoint &pixel) const
{
    return (d->view && d->view->targetLayer())
               ? d->view->targetLayer()->labelAt(pixel) : -1;
}

// Marcacion cuyo rotulo dibujado cae bajo el pixel.
qint64 MapWidget::bearingLabelAt(const QPoint &pixel) const
{
    return (d->view && d->view->rangeLayer())
               ? d->view->rangeLayer()->bearingLabelAt(QPointF(pixel)) : -1;
}

// Desplaza el rotulo de una marcacion (el offset vive en la propia marcacion).
bool MapWidget::setBearingLabelOffset(qint64 id, const QPointF &offsetPx)
{
    return d->view && d->view->rangeLayer()
           && d->view->rangeLayer()->setBearingLabelOffset(id, offsetPx);
}

// Fija el desplazamiento de la etiqueta de una entidad y lo PERSISTE: lee la
// entidad, le cambia el offset del estilo y la actualiza por el camino normal
// (que repinta y guarda con antirebote).
bool MapWidget::setFeatureLabelOffset(qint64 id, const QPointF &offsetPx)
{
    std::optional<MapFeature> f = feature(id);
    if (!f)
        return false;
    f->style.labelOffsetPx = offsetPx;
    return updateFeature(*f);
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

// Cambia en caliente el origen a una CARPETA de `.hgt` o de GeoTIFF (vacia =
// quita elevacion).
void MapWidget::setElevationDir(const QString &dir)
{
    instalarElevacion(d->elevation, d->elevationDesc, d->elevationWarn, dir);
    for (const QString &aviso : d->elevationWarn)
        qCWarning(lcMapaRender) << aviso;
}

// Cambia en caliente el origen a una BASE DE DATOS `.sqlitedb` o a un fichero
// GeoTIFF (vacia = quita).
void MapWidget::setElevationDb(const QString &dbFile)
{
    instalarElevacion(d->elevation, d->elevationDesc, d->elevationWarn, dbFile);
    for (const QString &aviso : d->elevationWarn)
        qCWarning(lcMapaRender) << aviso;
}

// Resumen para el usuario de la elevacion cargada (vacio si no hay ninguna util).
QString MapWidget::elevationDescription() const
{
    return d->elevationDesc;
}

// Problemas de la ultima carga de elevacion.
QStringList MapWidget::elevationWarnings() const
{
    return d->elevationWarn;
}

// Anade una capa base en caliente. El id sale del nombre del fichero (solo
// minusculas, cifras y guion bajo) y se hace unico con un sufijo si ya existe. Un
// MBTiles se describe con su propia metadata; cualquier otra base de teselas se
// sondea como hace probe_db, usando la zona del paquete (si la hay) para decidir
// entre XYZ y TMS.
QString MapWidget::addBaseLayer(const QString &file, QString *error, const QString &id)
{
    auto fallo = [error](const QString &motivo) {
        if (error)
            *error = motivo;
        return QString();
    };
    if (!QFileInfo(file).isFile())
        return fallo(tr("No existe el fichero %1").arg(QDir::toNativeSeparators(file)));

    QString base = id.trimmed();
    if (base.isEmpty()) {
        base = QFileInfo(file).completeBaseName().toLower();
        for (QChar &c : base)
            if (!c.isLetterOrNumber() || c.unicode() > 127)
                c = QLatin1Char('_');
        if (base.isEmpty())
            base = QStringLiteral("capa");
    }
    QString unico = base;
    for (int n = 2; d->service.dataset(unico); ++n)
        unico = QStringLiteral("%1_%2").arg(base).arg(n);

    TileDataset ds;
    QString motivo;
    if (MBTiles::isMBTilesPath(file)) {
        ds = MBTiles::fromFile(file, unico, &motivo);
        if (!ds.isValid())
            return fallo(motivo.isEmpty() ? tr("No es un MBTiles de teselas ráster.") : motivo);
    } else {
        const std::optional<ProbeResult> r = TileDatasetProbe::probe(file, unico, d->package.bounds);
        if (!r)
            return fallo(tr("No se reconoce %1 como una base de datos de teselas.")
                             .arg(QDir::toNativeSeparators(file)));
        ds = r->dataset;
        for (const QString &aviso : r->warnings)
            qCWarning(lcMapaRender) << aviso;
    }
    ds.id = unico;
    ds.filePath = QFileInfo(file).absoluteFilePath();
    if (ds.displayName.isEmpty())
        ds.displayName = QFileInfo(file).completeBaseName();

    if (!d->service.addDataset(ds, &motivo))
        return fallo(motivo);
    return unico;
}

// Cambia en caliente la base de datos de lugares (vacia = sin busqueda).
void MapWidget::setPlacesDb(const QString &dbFile)
{
    d->places.setDatabase(dbFile);
}

// ¿Hay una base de lugares que se pueda consultar?
bool MapWidget::hasPlaces() const
{
    return d->places.isUsable();
}

// Busca lugares por nombre; la consulta es indexada y rapida.
QVector<Place> MapWidget::searchPlaces(const QString &text, int limit) const
{
    return d->places.search(text, limit);
}

// Centra el mapa en el lugar, con el zoom que le corresponde sin pasar del
// maximo de la capa activa (mas alla solo habria teselas escaladas).
void MapWidget::goToPlace(const Place &place)
{
    if (!place.coordinate.isValid())
        return;
    setZoom(qMin(place.suggestedZoom(), maxZoom()));
    setCenter(place.coordinate);
}

// ------------------------------------------------- puntos de interes --

namespace {

// POI de las teselas que rodean \a centro en un dataset vectorial, a menos de
// \a radiusM. Lee de la cache lo que ya vio y pide a la base de datos, de una vez, el
// rectangulo que cubre lo que falta; las teselas que no existen (o sin POI) se
// guardan vacias para no volver a pedirlas.
QVector<PointOfInterest> poisAround(const TileDataset &dataset, const QGeoCoordinate &centro,
                                    double radiusM, QHash<QString, QVector<PointOfInterest>> *cache)
{
    QVector<PointOfInterest> out;
    if (!dataset.vector)
        return out;
    const int z = qBound(dataset.minZoom, 14, dataset.maxZoom);
    const double n = double(1 << z);
    const double latR = qDegreesToRadians(WebMercator::clampLatitude(centro.latitude()));
    const double lado = 40075016.685578488 * std::cos(latR) / n;       // metros por lado de tesela
    const int cx = int(std::floor((centro.longitude() + 180.0) / 360.0 * n));
    const int cy = int(std::floor((1.0 - std::asinh(std::tan(latR)) / M_PI) / 2.0 * n));
    const int r = qBound(0, int(std::ceil(radiusM / lado)), 4);
    const int x0 = qMax(0, cx - r), x1 = qMin(int(n) - 1, cx + r);
    const int y0 = qMax(0, cy - r), y1 = qMin(int(n) - 1, cy + r);

    const auto clave = [&](int x, int y) {
        return QStringLiteral("%1|%2/%3/%4").arg(dataset.filePath).arg(z).arg(x).arg(y);
    };
    // Rectangulo que cubre las teselas que faltan en la cache.
    int mx0 = x1 + 1, mx1 = -1, my0 = y1 + 1, my1 = -1;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            if (!cache->contains(clave(x, y))) {
                mx0 = qMin(mx0, x); mx1 = qMax(mx1, x);
                my0 = qMin(my0, y); my1 = qMax(my1, y);
            }
    if (mx1 >= 0) {
        TileDataset ds = dataset;
        ds.id = QStringLiteral("poi:") + dataset.filePath;      // conexion propia de este hilo
        RMapsTileSource src(ds);
        if (!src.open())
            return out;
        if (cache->size() > 800)
            cache->clear();                                      // cota simple: no crece sin limite
        const QHash<TileKey, QByteArray> crudas = src.fetchRange(z, mx0, mx1, my0, my1);
        for (int x = mx0; x <= mx1; ++x)
            for (int y = my0; y <= my1; ++y) {
                if (cache->contains(clave(x, y)))
                    continue;
                QVector<PointOfInterest> pois;
                const TileKey key{ z, x, y };
                const QByteArray bytes = crudas.value(key);
                mvt::Tile tile;
                // Solo la capa poi: saltarse edificios y vias es lo que lo hace rapido.
                if (!bytes.isEmpty()
                    && mvt::decode(bytes, &tile, nullptr, QStringList{ QLatin1String(poisearch::kLayer) }))
                    pois = poisearch::inTile(tile, key);
                cache->insert(clave(x, y), pois);
            }
    }
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (const PointOfInterest &p : cache->value(clave(x, y)))
                if (centro.distanceTo(p.coordinate) <= radiusM)
                    out.append(p);
    return out;
}

} // namespace

// ¿Hay donde buscar POI? Una capa vectorial: el overlay o la base activa.
bool MapWidget::canSearchPointsOfInterest() const
{
    const TileDataset *base = d->service.activeDataset();
    const TileDataset *over = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    return (base && base->vector) || (over && over->vector);
}

// Busca en el overlay (si esta) y en la base, y ordena todo junto por relevancia.
QVector<PointOfInterest> MapWidget::searchPointsOfInterest(const QString &text, int limit,
                                                           double radiusMeters) const
{
    if (text.trimmed().size() < 2 || limit <= 0)
        return {};
    const QGeoCoordinate centro = center();
    if (!centro.isValid())
        return {};
    const double radio = qBound(1000.0, radiusMeters, 15000.0);
    QVector<PointOfInterest> todos;
    const TileDataset *over = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    const TileDataset *base = d->service.activeDataset();
    if (over)
        todos += poisAround(*over, centro, radio, &d->poiCache);
    // Overlay y base pueden ser el mismo MBTiles: no se cuenta dos veces.
    if (base && (!over || base->filePath != over->filePath))
        todos += poisAround(*base, centro, radio, &d->poiCache);
    return poisearch::search(todos, text, centro, limit);
}

// Centra en el POI con el zoom mas cercano que tenga teselas.
void MapWidget::goToPointOfInterest(const PointOfInterest &poi)
{
    if (!poi.coordinate.isValid())
        return;
    setZoom(qMin(17, maxZoom()));
    setCenter(poi.coordinate);
}

namespace {

// Lee los POI de TODAS las teselas del rectangulo de \a area en un dataset vectorial,
// fila a fila (una consulta SQLite por fila: la memoria no crece con el tamaño de la
// zona) y devuelve los que estan dentro del poligono y casan con \a text. Sin cache:
// una importacion grande la desbordaria y no se repite. \a progress se llama una vez
// por fila con (filas hechas, filas totales); si devuelve false se corta y \a *cancelled
// queda a true.
QVector<PointOfInterest> poisInArea(const TileDataset &dataset, const QVector<QGeoCoordinate> &area,
                                    const QString &text, const std::function<bool(int, int)> &progress,
                                    int rowsOffset, int rowsTotal, bool *cancelled)
{
    QVector<PointOfInterest> out;
    const int z = qBound(dataset.minZoom, 14, dataset.maxZoom);
    const double n = double(1 << z);
    double s = 90, nn = -90, w = 180, e = -180;
    for (const QGeoCoordinate &c : area) {
        s = qMin(s, c.latitude());  nn = qMax(nn, c.latitude());
        w = qMin(w, c.longitude()); e = qMax(e, c.longitude());
    }
    const auto tx = [&](double lon) { return int(std::floor((lon + 180.0) / 360.0 * n)); };
    const auto ty = [&](double lat) {
        const double r = qDegreesToRadians(WebMercator::clampLatitude(lat));
        return int(std::floor((1.0 - std::asinh(std::tan(r)) / M_PI) / 2.0 * n));
    };
    const int x0 = qBound(0, tx(w), int(n) - 1), x1 = qBound(0, tx(e), int(n) - 1);
    const int y0 = qBound(0, ty(nn), int(n) - 1), y1 = qBound(0, ty(s), int(n) - 1);

    TileDataset ds = dataset;
    ds.id = QStringLiteral("poiimport:") + dataset.filePath;
    RMapsTileSource src(ds);
    if (!src.open())
        return out;
    const QStringList soloPoi{ QLatin1String(poisearch::kLayer) };
    for (int y = y0; y <= y1; ++y) {
        if (progress && !progress(rowsOffset + (y - y0), rowsTotal)) {
            *cancelled = true;
            return out;
        }
        const QHash<TileKey, QByteArray> fila = src.fetchRange(z, x0, x1, y, y);
        for (auto it = fila.constBegin(); it != fila.constEnd(); ++it) {
            mvt::Tile tile;
            if (it.value().isEmpty() || !mvt::decode(it.value(), &tile, nullptr, soloPoi))
                continue;
            QVector<PointOfInterest> candidatos;
            for (const PointOfInterest &p : poisearch::inTile(tile, it.key()))
                if (GeoMath::pointInPolygon(p.coordinate.longitude(), p.coordinate.latitude(), area))
                    candidatos.append(p);
            out += poisearch::filter(candidatos, text);
        }
    }
    return out;
}

// Teselas (rectangulo, z14) que abarca la zona en un dataset: lo que hay que leer.
int tilesOfArea(const TileDataset &dataset, const QVector<QGeoCoordinate> &area, int *rows = nullptr)
{
    const int z = qBound(dataset.minZoom, 14, dataset.maxZoom);
    const double n = double(1 << z);
    double s = 90, nn = -90, w = 180, e = -180;
    for (const QGeoCoordinate &c : area) {
        s = qMin(s, c.latitude());  nn = qMax(nn, c.latitude());
        w = qMin(w, c.longitude()); e = qMax(e, c.longitude());
    }
    const auto ty = [&](double lat) {
        const double r = qDegreesToRadians(WebMercator::clampLatitude(lat));
        return int(std::floor((1.0 - std::asinh(std::tan(r)) / M_PI) / 2.0 * n));
    };
    const int cols = int(std::floor((e + 180.0) / 360.0 * n)) - int(std::floor((w + 180.0) / 360.0 * n)) + 1;
    const int filas = qBound(0, ty(s), int(n) - 1) - qBound(0, ty(nn), int(n) - 1) + 1;
    if (rows)
        *rows = filas;
    return qMax(0, cols) * qMax(0, filas);
}

} // namespace

// Importa como entidades los POI de la zona que casan con el texto.
PoiImportResult MapWidget::importPointsOfInterest(const QVector<QGeoCoordinate> &area, const QString &text,
                                                  const QString &layerId,
                                                  const std::function<bool(int, int)> &progress,
                                                  int maxFeatures)
{
    PoiImportResult r;
    if (area.size() < 3) {
        r.error = tr("La zona necesita al menos tres vértices");
        return r;
    }
    if (layerId.isEmpty()) {
        r.error = tr("Falta la capa de destino");
        return r;
    }
    // Datasets vectoriales donde buscar: overlay (si esta) y base, sin repetir fichero.
    QVector<const TileDataset *> fuentes;
    const TileDataset *over = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    const TileDataset *base = d->service.activeDataset();
    if (over && over->vector)
        fuentes << over;
    if (base && base->vector && (!over || base->filePath != over->filePath))
        fuentes << base;
    if (fuentes.isEmpty()) {
        r.error = tr("No hay un mapa vectorial (MBTiles pbf) donde buscar sitios");
        return r;
    }
    int filasTotal = 0;
    for (const TileDataset *ds : fuentes) {
        int filas = 0;
        r.tiles += tilesOfArea(*ds, area, &filas);
        filasTotal += filas;
    }
    if (r.tiles > kMaxPoiImportTiles) {
        r.tooLarge = true;
        return r;
    }

    QVector<PointOfInterest> encontrados;
    int hechas = 0;
    for (const TileDataset *ds : fuentes) {
        bool cortado = false;
        int filas = 0;
        tilesOfArea(*ds, area, &filas);
        encontrados += poisInArea(*ds, area, text, progress, hechas, filasTotal, &cortado);
        hechas += filas;
        if (cortado) {
            r.cancelled = true;
            return r;                      // a medias: no se anade nada
        }
    }
    if (progress)
        progress(filasTotal, filasTotal);
    r.found = int(encontrados.size());

    addFeatureLayer(layerId, layerId);      // si ya existe, no hace nada
    // Los que ya estan (tipo, nombre y sitio a ~1 m): no se duplican al repetir o ampliar.
    const auto clave = [](const QString &tipo, const QString &nombre, const QGeoCoordinate &c) {
        return QStringLiteral("%1|%2|%3|%4").arg(tipo, nombre).arg(c.latitude(), 0, 'f', 5)
                                            .arg(c.longitude(), 0, 'f', 5);
    };
    QSet<QString> yaEstan;
    for (const MapFeature &f : featuresInLayer(layerId))
        yaEstan.insert(clave(f.type, f.name, f.position()));

    for (const PointOfInterest &p : encontrados) {
        const QString k = clave(p.type, p.displayName(), p.coordinate);
        if (yaEstan.contains(k)) {
            ++r.skipped;
            continue;
        }
        if (r.added >= maxFeatures) {
            r.truncated = true;
            continue;
        }
        MapFeature f;
        f.kind = GeometryKind::Point;
        f.layerId = layerId;
        f.type = p.type;
        f.name = p.displayName();
        f.geometry = { p.coordinate };
        f.style.lineColor = poisearch::themeColor(p.type);
        f.style.fillColor = f.style.lineColor;
        f.style.pointRadiusPx = 6.0;
        f.attributes.insert(QStringLiteral("clase"), p.type);
        f.attributes.insert(QStringLiteral("subclase"), p.subtype);
        f.attributes.insert(QStringLiteral("tipo"), p.typeLabel());
        f.attributes.insert(QStringLiteral("origen"), QStringLiteral("mbtiles"));
        if (addFeature(f) >= 0) {
            ++r.added;
            yaEstan.insert(k);
        }
    }
    return r;
}

// -------------------------------------------------------- rutas ---------

// El tope de teselas de la API publica y el del planificador son el mismo numero.
static_assert(MapWidget::kMaxRouteTiles == routeplanner::kMaxTiles,
              "kMaxRouteTiles debe coincidir con routeplanner::kMaxTiles");

// Ruta en coche por la red vial de las teselas vectoriales (la logica vive en RoutePlanner, que
// tambien usa vector_tile --route): aqui solo se elige el dataset.
RouteResult MapWidget::findRoute(const QGeoCoordinate &from, const QGeoCoordinate &to,
                                 const std::function<bool(int, int)> &progress) const
{
    // La red sale de la base vectorial o, si no, del overlay.
    const TileDataset *base = d->service.activeDataset();
    const TileDataset *over = d->vectorOverlay ? d->vectorOverlay->activeDataset() : nullptr;
    const TileDataset *ds = (base && base->vector) ? base : ((over && over->vector) ? over : nullptr);
    if (!ds) {
        RouteResult r;
        r.error = tr("No hay un mapa vectorial (MBTiles pbf) con la red vial");
        return r;
    }
    return routeplanner::findRoute(*ds, from, to, progress);
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

// Tope de vertices del trazado punto a punto (p. ej. 10 para un area objetivo).
void MapWidget::setDraftMaxVertices(int n)
{
    if (d->view)
        d->view->setDraftMaxVertices(n);
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

// ------------------------------------------- escala grafica y cuadricula ----

// Muestra u oculta la escala grafica. Solo repinta: la capa calcula la barra en
// cada dibujado, asi sigue al zoom y al desplazamiento sin mas avisos.
void MapWidget::setScaleBarVisible(bool on)
{
    if (!d->view || !d->view->scaleBarLayer())
        return;
    d->view->scaleBarLayer()->setVisible(on);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// ¿Se ve la escala grafica?
bool MapWidget::isScaleBarVisible() const
{
    return d->view && d->view->scaleBarLayer() && d->view->scaleBarLayer()->visible();
}

// Cambia las unidades de la escala (metrico / millas nauticas) y repinta.
void MapWidget::setScaleBarUnits(ScaleUnits units)
{
    if (!d->view || !d->view->scaleBarLayer())
        return;
    d->view->scaleBarLayer()->setUnits(units);
    // Las mismas unidades rotulan anillos, marcaciones y alertas CPA.
    if (d->view->rangeLayer())
        d->view->rangeLayer()->setUnits(units);
    d->refreshProximityLines();
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// ---------------------------------------- anillos de distancia y marcaciones --

// Alta de anillos de distancia; -1 sin vista. Reenvio a RangeLayer.
qint64 MapWidget::addRangeRings(const RangeRings &rings)
{
    return d->view && d->view->rangeLayer() ? d->view->rangeLayer()->addRings(rings) : -1;
}

// Cambia unos anillos (p. ej. otra separacion). false si el id no es de anillos.
bool MapWidget::updateRangeRings(qint64 id, const RangeRings &rings)
{
    return d->view && d->view->rangeLayer() && d->view->rangeLayer()->updateRings(id, rings);
}

// Alta de una marcacion; -1 sin vista.
qint64 MapWidget::addBearingLine(const BearingLine &line)
{
    return d->view && d->view->rangeLayer() ? d->view->rangeLayer()->addBearing(line) : -1;
}

// Cambia una marcacion. false si el id no es de marcacion.
bool MapWidget::updateBearingLine(qint64 id, const BearingLine &line)
{
    return d->view && d->view->rangeLayer() && d->view->rangeLayer()->updateBearing(id, line);
}

// Quita unos anillos o una marcacion.
bool MapWidget::removeRangeMark(qint64 id)
{
    return d->view && d->view->rangeLayer() && d->view->rangeLayer()->remove(id);
}

// Quita todos los anillos y marcaciones.
void MapWidget::clearRangeMarks()
{
    if (d->view && d->view->rangeLayer())
        d->view->rangeLayer()->clear();
}

// Ids de anillos y marcaciones, ordenados.
QVector<qint64> MapWidget::rangeMarks() const
{
    return d->view && d->view->rangeLayer() ? d->view->rangeLayer()->ids() : QVector<qint64>();
}

// Copia de unos anillos por id.
std::optional<RangeRings> MapWidget::rangeRings(qint64 id) const
{
    RangeRings r;
    if (d->view && d->view->rangeLayer() && d->view->rangeLayer()->rings(id, &r))
        return r;
    return std::nullopt;
}

// Copia de una marcacion por id.
std::optional<BearingLine> MapWidget::bearingLine(qint64 id) const
{
    BearingLine l;
    if (d->view && d->view->rangeLayer() && d->view->rangeLayer()->bearing(id, &l))
        return l;
    return std::nullopt;
}

// Separacion actual de unos anillos (la automatica depende del zoom).
double MapWidget::rangeRingsSpacing(qint64 id) const
{
    return d->view && d->view->rangeLayer() ? d->view->rangeLayer()->spacing(id) : 0.0;
}

// Rumbo verdadero y distancia actuales de una marcacion (con sus objetivos
// donde esten ahora). false si no existe o no se puede resolver.
bool MapWidget::bearingLineMeasure(qint64 id, double *bearingDeg, double *distanceMeters) const
{
    QGeoCoordinate a, b;
    if (!d->view || !d->view->rangeLayer() || !d->view->rangeLayer()->bearingEnds(id, &a, &b))
        return false;
    if (bearingDeg)
        *bearingDeg = a.azimuthTo(b);
    if (distanceMeters)
        *distanceMeters = a.distanceTo(b);
    return true;
}

// Que lineas marcan su inicio y su fin. Reenvio a la capa de entidades.
void MapWidget::setRouteEndpoints(RouteEndpoints mode)
{
    if (!d->view || !d->view->featureLayer())
        return;
    d->view->featureLayer()->setRouteEndpoints(mode);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// Modo actual de las marcas de inicio/fin.
RouteEndpoints MapWidget::routeEndpoints() const
{
    return d->view && d->view->featureLayer() ? d->view->featureLayer()->routeEndpoints()
                                              : RouteEndpoints::Routes;
}

// Unidades actuales de la escala (metrico si no hay vista).
ScaleUnits MapWidget::scaleBarUnits() const
{
    return d->view && d->view->scaleBarLayer() ? d->view->scaleBarLayer()->units()
                                               : ScaleUnits::Metric;
}

// Metros por pixel en el centro, medidos como los mide la escala. Sincroniza
// antes la geometria para no medir sobre un tamano de vista viejo.
double MapWidget::metersPerPixel() const
{
    d->syncGeometry();
    return d->view && d->view->scaleBarLayer()
        ? d->view->scaleBarLayer()->metersPerPixelAtCenter() : 0.0;
}

// Muestra u oculta la cuadricula de coordenadas; se recalcula en cada dibujado.
void MapWidget::setGraticuleVisible(bool on)
{
    if (!d->view || !d->view->graticuleLayer())
        return;
    d->view->graticuleLayer()->setVisible(on);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// ¿Se ve la cuadricula?
bool MapWidget::isGraticuleVisible() const
{
    return d->view && d->view->graticuleLayer() && d->view->graticuleLayer()->visible();
}

// Color de lineas y rotulos de la cuadricula, y repinta.
void MapWidget::setGraticuleColor(const QColor &color)
{
    if (!d->view || !d->view->graticuleLayer())
        return;
    d->view->graticuleLayer()->setColor(color);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// Cambia entre cuadricula geografica y UTM, y repinta.
void MapWidget::setGraticuleMode(GraticuleMode mode)
{
    if (!d->view || !d->view->graticuleLayer())
        return;
    d->view->graticuleLayer()->setMode(mode);
    d->view->replot(QCustomPlot::rpQueuedReplot);
}

// Modo actual de la cuadricula (geografica si no hay vista).
GraticuleMode MapWidget::graticuleMode() const
{
    return d->view && d->view->graticuleLayer() ? d->view->graticuleLayer()->mode()
                                                : GraticuleMode::Geographic;
}

// Color actual de la cuadricula (invalido si no hay vista).
QColor MapWidget::graticuleColor() const
{
    return d->view && d->view->graticuleLayer() ? d->view->graticuleLayer()->color()
                                                : QColor();
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
