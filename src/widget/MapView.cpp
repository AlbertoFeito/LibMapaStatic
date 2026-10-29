#include "widget/MapView.h"

#include "core/Logging.h"
#include "geo/GeoMath.h"
#include "geo/MapDecor.h"
#include "libmapa/Coordinates.h"
#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"
#include "geo/WebMercator.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <cmath>

#include "geo/WebMercator.h"

namespace libmapa {

// Construye la vista de mapa sobre QCustomPlot: oculta ejes y rejilla (pero usa
// su sistema de coordenadas), desactiva la interaccion nativa (el arrastre/zoom
// se manejan aqui por niveles discretos) y crea las tres capas apiladas —
// teselas, entidades estaticas y objetivos moviles (esta en modo BUFFERED para
// repintarse sola sin rehacer el resto)—, cada una con su modelo y su mapeador de
// proyeccion.
MapView::MapView(TileService *service, QWidget *parent)
    : QCustomPlot(parent)
    , m_service(service)
{
    // Un mapa no tiene ejes ni rejilla visibles, pero si usa el sistema de
    // coordenadas de los ejes: x = longitud, y = latitud, ambos en grados.
    // Asi cualquier overlay dibujado con coordenadas de eje encaja solo.
    axisRect()->setMargins(QMargins(0, 0, 0, 0));
    axisRect()->setAutoMargins(QCP::msNone);
    // Los ejes se usan solo como sistema de coordenadas: nada de marcas,
    // etiquetas ni rejilla. Ademas de ahorrar trabajo en cada repintado, esto
    // evita que QCPAxisTicker llegue a ejecutarse: setupTickVectors solo
    // retorna pronto si ticks, etiquetas y rejilla estan los tres apagados, y
    // con un rango degenerado el generador puede no terminar.
    for (QCPAxis *eje : {xAxis, yAxis, xAxis2, yAxis2}) {
        eje->setVisible(false);
        eje->setTicks(false);
        eje->setTickLabels(false);
        eje->setSubTicks(false);
        eje->grid()->setVisible(false);
        eje->grid()->setSubGridVisible(false);
    }
    setBackground(Qt::NoBrush);

    // El arrastre y la rueda se manejan aqui: el de QCustomPlot mueve los ejes
    // de forma continua, y un mapa de teselas necesita niveles discretos.
    setInteractions(QCP::Interactions());

    // Capa propia para las teselas, por debajo de todo lo demas.
    addLayer(QStringLiteral("tiles"), layer(QStringLiteral("background")),
             QCustomPlot::limAbove);
    m_tileLayer = new TileLayer(this, service);
    m_tileLayer->setLayer(QStringLiteral("tiles"));

    // Capa de entidades ESTATICAS, encima de las teselas. Los objetivos en
    // movimiento tendran la suya propia: si compartieran capa, cada
    // actualizacion de posicion obligaria a redibujar todas las zonas.
    addLayer(QStringLiteral("features"), layer(QStringLiteral("tiles")),
             QCustomPlot::limAbove);
    m_model = new OverlayModel(this);
    m_featureLayer = new FeatureLayer(this, m_model);
    m_featureLayer->setLayer(QStringLiteral("features"));
    m_featureLayer->setAxisMapper(
        [](const QGeoCoordinate &c) { return MapView::toAxis(c); });

    // Capa DINAMICA de objetivos moviles, encima de las entidades. En modo
    // BUFFERED: actualizar cientos de objetivos repinta solo esta capa y
    // recompone, sin rehacer teselas ni entidades estaticas.
    addLayer(QStringLiteral("targets"), layer(QStringLiteral("features")),
             QCustomPlot::limAbove);
    layer(QStringLiteral("targets"))->setMode(QCPLayer::lmBuffered);
    m_targetModel = new TargetModel(this);
    // Anillos de distancia y marcaciones: en la MISMA capa que los objetivos
    // (se repintan con ellos cuando cuelgan de uno) y creados antes, para que
    // los objetivos queden dibujados encima.
    m_rangeLayer = new RangeLayer(this, m_targetModel);
    m_rangeLayer->setLayer(QStringLiteral("targets"));
    m_rangeLayer->setAxisMapper(
        [](const QGeoCoordinate &c) { return MapView::toAxis(c); });
    // Anillos en una entidad punto: su posicion sale del modelo de entidades,
    // y la siguen al moverla (arrastre, edicion, deshacer).
    m_rangeLayer->setFeatureResolver([this](qint64 id) {
        const auto f = m_model ? m_model->feature(id) : std::nullopt;
        return f && f->kind == GeometryKind::Point ? f->position() : QGeoCoordinate();
    });
    if (m_model)
        connect(m_model, &OverlayModel::featureUpdated, m_rangeLayer, &RangeLayer::featureChanged);
    m_targetLayer = new TargetLayer(this, m_targetModel);
    m_targetLayer->setLayer(QStringLiteral("targets"));
    m_targetLayer->setAxisMapper(
        [](const QGeoCoordinate &c) { return MapView::toAxis(c); });

    // ROTULOS de entidades, anillos y marcaciones: en la misma capa buffered, pero
    // creados los ULTIMOS para quedar por encima de todo (un anillo o una
    // marcacion no deben tapar el nombre de una zona). Se repintan con los objetivos.
    m_labelLayer = new LabelLayer(this, m_featureLayer, m_rangeLayer);
    m_labelLayer->setLayer(QStringLiteral("targets"));

    // Capa de COBERTURA, encima de todo: una mancha de diagnostico (que teselas
    // de un zoom hay en la BD). Oculta por defecto; la enciende fill_map.
    addLayer(QStringLiteral("coverage"), layer(QStringLiteral("targets")),
             QCustomPlot::limAbove);
    m_coverageLayer = new CoverageLayer(this);
    m_coverageLayer->setLayer(QStringLiteral("coverage"));
    m_coverageLayer->setVisible(false);

    // CUADRICULA de coordenadas: encima de las entidades y por debajo de los
    // objetivos (orienta sin tapar lo que se mueve). Oculta por defecto.
    addLayer(QStringLiteral("graticule"), layer(QStringLiteral("features")),
             QCustomPlot::limAbove);
    m_graticuleLayer = new GraticuleLayer(this);
    m_graticuleLayer->setLayer(QStringLiteral("graticule"));
    m_graticuleLayer->setVisible(false);

    // ESCALA GRAFICA: encima de todo, es un elemento de la pantalla. Oculta por
    // defecto.
    addLayer(QStringLiteral("scalebar"), layer(QStringLiteral("coverage")),
             QCustomPlot::limAbove);
    m_scaleBarLayer = new ScaleBarLayer(this);
    m_scaleBarLayer->setLayer(QStringLiteral("scalebar"));
    m_scaleBarLayer->setVisible(false);

    // Capa de RELIEVE SOMBREADO (hillshade): justo ENCIMA de las teselas y por
    // DEBAJO de las entidades, para que el sombreado adorne la base sin tapar los
    // overlays. Oculta por defecto; la calcula MapWidget desde el DEM local.
    addLayer(QStringLiteral("hillshade"), layer(QStringLiteral("tiles")),
             QCustomPlot::limAbove);
    m_hillshadeLayer = new HillshadeLayer(this);
    m_hillshadeLayer->setLayer(QStringLiteral("hillshade"));
    m_hillshadeLayer->setVisible(false);

    if (service) {
        connect(service, &TileService::tilesReady,
                this, &MapView::onTilesReady);
    }
}

MapView::~MapView() = default;

// Area de dibujo en pixeles. Usa rect() (tamano real del widget) y cae a
// viewport() o a un tamano por defecto para no devolver NUNCA un rectangulo
// degenerado, que colgaria el calculo de rangos de eje.
QRect MapView::plotArea() const
{
    // rect() es siempre el tamano actual del widget; viewport() puede ir por
    // detras si QCustomPlot aun no ha procesado su resizeEvent.
    const QRect r = rect();
    if (r.width() > 1 && r.height() > 1)
        return r;
    const QRect vp = viewport();
    if (vp.width() > 1 && vp.height() > 1)
        return vp;
    return QRect(0, 0, 640, 480);   // ultimo recurso, nunca degenerado
}

// Relacion de aspecto (ancho/alto) del area de dibujo. 1.0 si la altura es nula.
double MapView::aspectRatio() const
{
    const QRect r = plotArea();
    if (r.height() <= 0)
        return 1.0;
    return double(r.width()) / double(r.height());
}

// Centro geografico actual de la vista.
QGeoCoordinate MapView::center() const
{
    return m_center;
}

// Devuelve una coordenada SIEMPRE valida: envuelve la longitud a [-180,180],
// recorta la latitud al limite de Mercator y sustituye NaN por 0. Es la barrera
// que impide que una coordenada invalida (que da NaN) llegue a los rangos de eje
// y cuelgue a QCustomPlot generando marcas.
QGeoCoordinate MapView::normalized(const QGeoCoordinate &c)
{
    // QGeoCoordinate con longitud fuera de [-180,180] o latitud fuera de
    // [-90,90] es INVALIDO: latitude() y longitude() devuelven NaN. Si eso
    // llega a los rangos de los ejes, QCustomPlot se cuelga generando marcas.
    //
    // A zoom 3 el viewport abarca 225 grados de longitud, asi que basta
    // arrastrar un poco para salirse. De ahi el bloqueo.
    double lon = c.isValid() ? c.longitude() : 0.0;
    double lat = c.isValid() ? c.latitude() : 0.0;

    if (!std::isfinite(lon)) lon = 0.0;
    if (!std::isfinite(lat)) lat = 0.0;

    // La longitud da la vuelta al mundo; la latitud se recorta al limite
    // representable en Mercator.
    while (lon >  180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;
    lat = WebMercator::clampLatitude(lat);

    return QGeoCoordinate(lat, lon);
}

// Traduce (centro, zoom) a los rangos de los ejes X/Y. Calcula el ancho del
// mundo en pixeles al zoom actual, deriva cuantos grados abarca la pantalla en
// cada eje y centra alli, ACOTANDO el centro para que la vista no se salga del
// mundo (evita las bandas vacias arriba/lados). Trabaja en el eje proyectado
// (grados de Mercator en Y), donde el mundo mide 360 en ambos ejes y las teselas
// salen cuadradas. Guarda el centro efectivo en m_center.
void MapView::applyZoomToAxes(const QGeoCoordinate &center)
{
    ensureLayout();

    const QGeoCoordinate c = normalized(center);

    const QRect r = plotArea();
    const int widthPx = qMax(1, r.width());
    const int heightPx = qMax(1, r.height());

    const TileDataset *ds = m_service ? m_service->activeDataset() : nullptr;
    const int tileSize = ds ? ds->tileSize : 256;

    // Anchura del mundo proyectado, en pixeles, al zoom actual.
    const double worldPx = double(tileSize) * double(1 << m_zoom);

    // En unidades de eje (grados de longitud en X, grados de Mercator en Y)
    // el mundo mide 360 en ambos, asi que la misma formula vale para los dos
    // y las teselas salen cuadradas sin corregir la relacion de aspecto.
    const double spanX = 360.0 * widthPx / worldPx;
    const double spanY = 360.0 * heightPx / worldPx;

    double cx = c.longitude();
    double cy = TileMatrix::latitudeToAxisY(c.latitude());

    // El centro se limita para que la vista no se salga del mundo. Sin esto
    // aparecen las bandas vacias que se ven en las capturas: arriba, al
    // asomarse por encima del paralelo 85, y a los lados al pasar de +-180.
    // Si el mundo cabe entero en pantalla, se centra y no se mueve.
    if (spanX >= 360.0)
        cx = 0.0;
    else
        cx = qBound(-180.0 + spanX / 2.0, cx, 180.0 - spanX / 2.0);

    if (spanY >= 360.0)
        cy = 0.0;
    else
        cy = qBound(-180.0 + spanY / 2.0, cy, 180.0 - spanY / 2.0);

    xAxis->setRange(cx - spanX / 2.0, cx + spanX / 2.0);
    yAxis->setRange(cy - spanY / 2.0, cy + spanY / 2.0);

    m_center = normalized(QGeoCoordinate(
        WebMercator::mercatorDegreesToLatitude(cy), cx));
}

// Recentra el mapa en una coordenada (sin cambiar el zoom) y pide las teselas
// visibles y el plan de dibujo. Ignora coordenadas invalidas.
void MapView::setCenter(const QGeoCoordinate &center)
{
    if (!center.isValid())
        return;
    applyZoomToAxes(center);
    emit centerChanged(m_center);
    requestVisibleTiles();
    refreshPlan();
}

// Fija el nivel de zoom (acotado al rango del dataset). Si se pasa un pixel de
// ancla (la rueda del raton), recoloca el centro para que el punto bajo el cursor
// se quede QUIETO tras ampliar/reducir; si no, mantiene el centro. Pide teselas y
// plan tras el cambio.
void MapView::setZoom(int zoom, const QPointF *anchorPx)
{
    const TileDataset *ds = m_service ? m_service->activeDataset() : nullptr;
    const int lo = ds ? ds->minZoom : 0;
    int hi = ds ? ds->recommendedMaxZoom : 18;
    if (m_maxZoomOverride >= 0)      // sobre-zoom permitido por la herramienta
        hi = qMax(hi, m_maxZoomOverride);

    const int nuevo = qBound(lo, zoom, hi);
    if (nuevo == m_zoom)
        return;

    // Al ampliar con la rueda, el punto bajo el cursor debe quedarse quieto.
    QGeoCoordinate ancla;
    if (anchorPx)
        ancla = coordinateAt(anchorPx->toPoint());

    const QGeoCoordinate anterior = m_center;
    m_zoom = nuevo;

    if (ancla.isValid()) {
        // Se recoloca el centro para que 'ancla' siga en el mismo pixel. El
        // ajuste va en el eje proyectado, por la misma razon que el arrastre.
        applyZoomToAxes(anterior);
        const QPointF anclaEje = toAxis(ancla);
        const QPointF ahoraEje(xAxis->pixelToCoord(anchorPx->x()),
                               yAxis->pixelToCoord(anchorPx->y()));
        const QPointF centroEje = toAxis(anterior);
        applyZoomToAxes(fromAxis(centroEje + (anclaEje - ahoraEje)));
    } else {
        applyZoomToAxes(anterior);
    }

    emit zoomChanged(m_zoom);
    emit centerChanged(m_center);
    requestVisibleTiles();
    refreshPlan();
}

// Encuadra un rectangulo geografico: elige el mayor zoom con el que la anchura
// del rectangulo cabe en el ancho del widget y centra en su punto medio. Lo usa
// la herramienta "ampliar area".
void MapView::fitBounds(const QGeoCoordinate &northWest,
                        const QGeoCoordinate &southEast)
{
    if (!northWest.isValid() || !southEast.isValid())
        return;

    const double spanLon = std::abs(southEast.longitude() - northWest.longitude());
    if (spanLon <= 0.0)
        return;

    const TileDataset *ds = m_service ? m_service->activeDataset() : nullptr;
    const int lo = ds ? ds->minZoom : 0;
    int hi = ds ? ds->recommendedMaxZoom : 18;
    if (m_maxZoomOverride >= 0)
        hi = qMax(hi, m_maxZoomOverride);

    ensureLayout();
    const int widthPx = qMax(1, plotArea().width());
    const int tileSize = ds ? ds->tileSize : 256;

    // z tal que spanLon ocupe justo el ancho del widget.
    const double worldPx = widthPx * 360.0 / spanLon;
    const int z = qBound(lo, int(std::floor(std::log2(worldPx / tileSize))), hi);

    m_zoom = z;
    applyZoomToAxes(QGeoCoordinate(
        (northWest.latitude() + southEast.latitude()) / 2.0,
        (northWest.longitude() + southEast.longitude()) / 2.0));

    emit zoomChanged(m_zoom);
    emit centerChanged(m_center);
    requestVisibleTiles();
    refreshPlan();
}

QGeoCoordinate MapView::visibleNorthWest() const
{
    // Por normalized() tambien aqui: los rangos de los ejes pueden asomarse
    // fuera del mundo, y una QGeoCoordinate construida con esos valores se
    // marca invalida y devuelve NaN. Ese NaN llegaba a rangeFor y de ahi a
    // toda la cadena de peticion de teselas.
    return normalized(QGeoCoordinate(
        WebMercator::mercatorDegreesToLatitude(yAxis->range().upper),
        xAxis->range().lower));
}

// Esquina SURESTE visible (misma proteccion normalized() que visibleNorthWest).
QGeoCoordinate MapView::visibleSouthEast() const
{
    return normalized(QGeoCoordinate(
        WebMercator::mercatorDegreesToLatitude(yAxis->range().lower),
        xAxis->range().upper));
}

// Pixel de pantalla -> coordenada geografica (a traves del eje proyectado).
QGeoCoordinate MapView::coordinateAt(const QPoint &pixel) const
{
    return fromAxis(QPointF(xAxis->pixelToCoord(pixel.x()),
                                  yAxis->pixelToCoord(pixel.y())));
}

// Objetivo movil mas cercano al pixel dado, dentro de tolPx. Proyecta cada
// objetivo a pantalla con la misma transformacion que usa TargetLayer (toAxis +
// coordToPixel) y se queda con el mas proximo. O(n) sobre los objetivos, que es
// de sobra para un clic ocasional.
qint64 MapView::targetAt(const QPoint &pixel, double tolPx) const
{
    if (!m_targetModel)
        return -1;
    qint64 mejor = -1;
    double mejorD2 = tolPx * tolPx;
    for (auto it = m_targetModel->entries().constBegin();
         it != m_targetModel->entries().constEnd(); ++it) {
        const MapTarget &t = it->target;
        if (!t.position.isValid())
            continue;
        if (m_targetLayer && (m_targetLayer->isFilteredOut(t.id)
                              || m_targetLayer->isClustered(t.id)))
            continue;                  // oculto por el filtro o dentro de un grupo
        const QPointF a = toAxis(t.position);
        const double dx = xAxis->coordToPixel(a.x()) - pixel.x();
        const double dy = yAxis->coordToPixel(a.y()) - pixel.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 <= mejorD2) {
            mejorD2 = d2;
            mejor = t.id;
        }
    }
    return mejor;
}

// Fija el objetivo resaltado y lo propaga a la capa (que dibuja el halo y le
// fuerza la etiqueta). No emite senal: la seleccion es decision de quien llama.
void MapView::setSelectedTarget(qint64 id)
{
    m_selectedTarget = id;
    if (m_targetLayer)
        m_targetLayer->setSelected(id);
}

// Coordenada geografica -> punto de EJE (x = longitud, y = grados de Mercator).
// Es la proyeccion que comparten todas las capas para pintar sobre el mapa.
QPointF MapView::toAxis(const QGeoCoordinate &c)
{
    return QPointF(c.longitude(),
                   WebMercator::latitudeToMercatorDegrees(c.latitude()));
}

// Inverso de toAxis: punto de eje -> coordenada geografica.
QGeoCoordinate MapView::fromAxis(const QPointF &p)
{
    return QGeoCoordinate(WebMercator::mercatorDegreesToLatitude(p.y()), p.x());
}

// Fuerza a QCustomPlot a poner al dia su viewport/disposicion antes de leer o
// fijar rangos de eje, porque el resizeEvent del widget puede ir por detras. Sin
// esto, el area del axisRect llega a cero y los calculos de zoom salen mal.
void MapView::ensureLayout()
{
    if (rect().width() <= 1 || rect().height() <= 1)
        return;
    if (viewport() != rect()) {
        setViewport(rect());          // esto ya reajusta el outerRect del layout
        plotLayout()->update(QCPLayoutElement::upLayout);
    } else if (axisRect()->rect().width() <= 1) {
        plotLayout()->update(QCPLayoutElement::upLayout);
    }
}

// Pide al servicio que cargue (en segundo plano) las teselas del viewport actual,
// con un margen de una tesela para que al arrastrar ya esten precargadas.
void MapView::requestVisibleTiles()
{
    if (!m_service)
        return;
    m_service->requestViewport(visibleNorthWest(), visibleSouthEast(),
                               m_zoom, /*marginTiles=*/1);
    // El overlay (si se ve) pide lo mismo a SU servicio; su zoom se recorta al
    // rango de su MBTiles y, mas alla, se amplia la ultima tesela.
    if (m_overlayService && m_overlayLayer && m_overlayLayer->visible())
        m_overlayService->requestViewport(visibleNorthWest(), visibleSouthEast(),
                                          m_zoom, /*marginTiles=*/1);
}

// Pone (o quita, con null) el overlay de teselas. La capa se crea la primera vez
// en su propio nivel de QCustomPlot, encima del relieve y debajo de las
// entidades, y sin color de "sin datos": donde no hay overlay se ve la base.
void MapView::setOverlayService(TileService *service)
{
    if (m_overlayReady)
        disconnect(m_overlayReady);
    m_overlayService = service;
    if (!m_overlayLayer) {
        addLayer(QStringLiteral("tileoverlay"), layer(QStringLiteral("hillshade")),
                 QCustomPlot::limAbove);
        m_overlayLayer = new TileLayer(this, nullptr);
        m_overlayLayer->setLayer(QStringLiteral("tileoverlay"));
        m_overlayLayer->setNoDataColor(Qt::transparent);
    }
    m_overlayLayer->setService(service);
    m_overlayLayer->setPlan(TilePlanner::Plan());
    m_overlayLayer->setVisible(service != nullptr);
    if (service)
        m_overlayReady = connect(service, &TileService::tilesReady, this,
                                 [this](quint64, int, int nuevas) {
                                     if (nuevas > 0)
                                         refreshPlan();
                                 });
    requestVisibleTiles();
    refreshPlan();
}

// Oculta la marca de seleccion: el recuadro de SelectArea (sin borrarlo, para
// reutilizar el item) y el contorno de un poligono de SelectPolygon ya cerrado.
// Lo usa MapWidget al confirmar o descartar la seleccion.
void MapView::clearAreaSelection()
{
    bool cambio = false;
    if (m_areaRect && m_areaRect->visible()) {
        m_areaRect->setVisible(false);
        cambio = true;
    }
    if (m_draftIsSelection && !m_drafting) {
        m_draftIsSelection = false;
        m_selectionIsMeasure = false;
        m_draft = MapFeature();
        if (m_featureLayer) {
            m_featureLayer->setDraft(nullptr);
            m_featureLayer->setDraftLabel(QString());
        }
        cambio = true;
    }
    if (cambio)
        replot(QCustomPlot::rpQueuedReplot);
}

// Recalcula el plan de dibujo de teselas para el viewport actual (que pintar y
// con que respaldo) y encola un repintado. Se llama tras cada cambio de vista.
void MapView::refreshPlan()
{
    if (!m_service || !m_tileLayer)
        return;
    m_tileLayer->setPlan(m_service->planFor(visibleNorthWest(),
                                            visibleSouthEast(), m_zoom));
    if (m_overlayService && m_overlayLayer && m_overlayLayer->visible())
        m_overlayLayer->setPlan(m_overlayService->planFor(visibleNorthWest(),
                                                          visibleSouthEast(), m_zoom));
    replot(QCustomPlot::rpQueuedReplot);
}

// Respuesta del servicio: llegaron teselas nuevas a la cache. Solo rehace el plan
// (y repinta) si de verdad hay teselas nuevas que mostrar.
void MapView::onTilesReady(quint64 requestId, int zoom, int newTiles)
{
    Q_UNUSED(requestId)
    Q_UNUSED(zoom)
    if (newTiles > 0)
        refreshPlan();
}

void MapView::resizeEvent(QResizeEvent *event)
{
    // Firma correcta, a diferencia del "virtual void resizeEvent();" del
    // original, que no sobreescribia nada y nunca se llamaba (falla F-18).
    // Y aqui solo se recalcula el viewport: no se recrean capas ni se
    // vuelven a leer ficheros .geo.
    QCustomPlot::resizeEvent(event);
    applyZoomToAxes(m_center);
    requestVisibleTiles();
    refreshPlan();
}

// Posicion global del evento (Qt5 y Qt6), para que la app abra menus y tarjetas
// justo donde se hizo clic.
static QPoint globalDe(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition().toPoint();
#else
    return e->globalPos();
#endif
}

// Rueda: acerca o aleja un nivel por cada 120 unidades de giro, centrado en el
// cursor. Los giros se ACUMULAN: una rueda de alta resolucion o un touchpad
// mandan muchos eventos de 5 a 40 unidades y antes se descartaban todos (solo
// contaba un evento de 120 o mas). Lo acumulado se olvida tras medio segundo
// parado o al cambiar de sentido.
void MapView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    if (!m_wheelClock.isValid() || m_wheelClock.elapsed() > 500
        || (m_wheelAccum != 0 && (m_wheelAccum > 0) != (delta > 0)))
        m_wheelAccum = 0;
    m_wheelClock.restart();
    m_wheelAccum += delta;
    const int pasos = m_wheelAccum / 120;
    m_wheelAccum -= pasos * 120;
    event->accept();
    if (pasos == 0)
        return;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPointF pos = event->position();
#else
    const QPointF pos = event->posF();
#endif
    setZoom(m_zoom + pasos, &pos);
}

// Desplaza la vista \a dx, \a dy pixeles (positivo = hacia la derecha / abajo).
// Como el arrastre, se calcula en el eje PROYECTADO: el mismo numero de pixeles
// no son los mismos grados de latitud arriba que abajo del mapa.
void MapView::panByPixels(int dx, int dy)
{
    const QPointF a(xAxis->pixelToCoord(0), yAxis->pixelToCoord(0));
    const QPointF b(xAxis->pixelToCoord(dx), yAxis->pixelToCoord(dy));
    const double centroMercY = WebMercator::latitudeToMercatorDegrees(m_center.latitude());
    applyZoomToAxes(fromAxis(QPointF(m_center.longitude() + (b.x() - a.x()),
                                     centroMercY + (b.y() - a.y()))));
    emit centerChanged(m_center);
    requestVisibleTiles();
    refreshPlan();
}

// ¿Se puede editar la capa? Una capa que no existe aun se crea al anadir, asi
// que cuenta como editable. La bandera de la capa se hacia cumplir solo por
// \c selectable en las capas del paquete; aqui se respeta en la edicion.
bool MapView::capaEditable(const QString &layerId) const
{
    const auto capa = m_model->layer(layerId);
    return !capa || capa->editable;
}

// Cancela lo que la herramienta activa tenga a medias, SIN cambiar de
// herramienta: el primer punto de Medir, el recuadro de Zoom/Seleccionar area
// (y su marca ya cerrada), o la captura de un punto (que vuelve a Navegar y
// avisa con pickCancelled). Devuelve false si no habia nada que cancelar. Lo
// llaman Esc y el clic derecho.
bool MapView::cancelOperation()
{
    bool hecho = false;
    if (m_tool == MapTool::Measure
        && (m_toolFirstPointSet || (m_measureLine && m_measureLine->visible()))) {
        m_toolFirstPointSet = false;
        if (m_measureLine)
            m_measureLine->setVisible(false);
        if (m_measureStart)
            m_measureStart->setVisible(false);
        hecho = true;
    } else if ((m_tool == MapTool::AreaZoom || m_tool == MapTool::SelectArea)
               && (m_toolFirstPointSet || (m_areaRect && m_areaRect->visible()))) {
        m_toolFirstPointSet = false;
        if (m_areaRect)
            m_areaRect->setVisible(false);
        hecho = true;
    } else if (m_tool == MapTool::PickPoint) {
        setActiveTool(MapTool::None);
        emit pickCancelled();
        return true;
    }
    if (hecho)
        replot(QCustomPlot::rpQueuedReplot);
    return hecho;
}

// Cursor segun lo que haya bajo el puntero (sin botones pulsados), para saber
// que se puede agarrar: manita sobre objetivos, grupos, entidades y marcas;
// «mover» sobre rotulos y sobre los tiradores de vertice. Limitado a un calculo
// cada 30 ms: son varias pruebas de acierto.
void MapView::actualizarCursorHover(const QPoint &pos)
{
    if (m_hoverClock.isValid() && m_hoverClock.elapsed() < 30)
        return;
    m_hoverClock.restart();

    Qt::CursorShape c = Qt::ArrowCursor;
    if (m_tool == MapTool::None) {
        if (m_featureLayer->labelAt(pos) >= 0 || (m_targetLayer && m_targetLayer->labelAt(pos) >= 0)
            || (m_rangeLayer && m_rangeLayer->bearingLabelAt(QPointF(pos)) >= 0)) {
            c = Qt::SizeAllCursor;
        } else if (m_targetLayer && (!m_targetLayer->clusterAt(pos).isEmpty() || targetAt(pos) >= 0)) {
            c = Qt::PointingHandCursor;
        } else if (m_featureLayer->featureAt(pos) >= 0) {
            c = Qt::PointingHandCursor;
        } else if (m_rangeLayer && m_rangeLayer->markAt(QPointF(pos)) >= 0) {
            c = Qt::PointingHandCursor;
        }
    } else if (m_tool == MapTool::EditFeature) {
        const qint64 sel = m_model->selectedId();
        m_hoverVertex = sel >= 0 ? m_featureLayer->vertexAt(sel, pos) : -1;
        c = m_hoverVertex >= 0 ? Qt::SizeAllCursor : Qt::PointingHandCursor;
    } else {
        return;                 // el resto de herramientas tienen su cruz
    }
    if (!m_hoverSet || c != m_hoverShape) {
        setCursor(c);
        m_hoverShape = c;
        m_hoverSet = true;
    }
}

// Gestiona la herramienta de medir en dos clics. Primer clic: ancla el origen
// (dibuja una marca y una linea que seguira al raton). Segundo clic: calcula
// distancia y azimut entre los dos puntos y emite measurementFinished. La marca
// del origen se dimensiona en pixeles para no deformarse con el zoom.
void MapView::handleMeasureClick(const QGeoCoordinate &donde)
{
    if (!m_toolFirstPointSet) {
        m_toolFirstPoint = donde;
        m_toolFirstPointSet = true;

        if (!m_measureLine) {
            m_measureLine = new QCPItemLine(this);
            m_measureLine->setPen(QPen(Qt::red, 2));
            m_measureLine->setHead(QCPLineEnding::esSpikeArrow);
        }
        // Marca visible en el punto de anclaje, para que se vea de inmediato
        // si cae donde se pincho.
        if (!m_measureStart) {
            m_measureStart = new QCPItemEllipse(this);
            m_measureStart->setPen(QPen(Qt::red, 2));
            m_measureStart->setBrush(QBrush(QColor(255, 0, 0, 120)));
        }

        const QPointF eje = toAxis(donde);
        m_measureLine->start->setCoords(eje.x(), eje.y());
        m_measureLine->end->setCoords(eje.x(), eje.y());
        m_measureLine->setVisible(true);

        // El radio se fija en PIXELES, no en grados: asi la marca mide igual
        // a cualquier zoom y no se deforma con la latitud.
        m_measureStart->topLeft->setType(QCPItemPosition::ptAbsolute);
        m_measureStart->bottomRight->setType(QCPItemPosition::ptAbsolute);
        const double px = xAxis->coordToPixel(eje.x());
        const double py = yAxis->coordToPixel(eje.y());
        m_measureStart->topLeft->setCoords(px - 4, py - 4);
        m_measureStart->bottomRight->setCoords(px + 4, py + 4);
        m_measureStart->setVisible(true);
    } else {
        Measurement m;
        m.from = m_toolFirstPoint;
        m.to = donde;
        m.distanceMeters = GeoMath::distanceMeters(m.from, m.to);
        m.azimuthDegrees = GeoMath::azimuthDegrees(m.from, m.to);
        m_toolFirstPointSet = false;
        if (m_measureStart)
            m_measureStart->setVisible(false);
        emit measurementFinished(m);
    }
    replot(QCustomPlot::rpQueuedReplot);
}

// Medida en vivo de MeasureArea: superficie y perimetro de los vertices puestos
// mas el punto del cursor (si es valido), cerrando el poligono. Con menos de 3
// puntos solo hay longitud. Unidades de la escala grafica.
void MapView::updateAreaLabel(const QGeoCoordinate &cursor)
{
    if (!m_featureLayer)
        return;
    QVector<QGeoCoordinate> pts = m_draft.geometry;
    if (cursor.isValid() && (pts.isEmpty() || pts.last() != cursor))
        pts.append(cursor);
    const ScaleUnits u = m_scaleBarLayer ? m_scaleBarLayer->units() : ScaleUnits::Metric;
    QString txt;
    if (pts.size() >= 3) {
        txt = tr("Superficie %1\nPerímetro %2")
                  .arg(MapDecor::formatArea(polygonArea(pts), u),
                       MapDecor::formatDistance(pathLength(pts, true), u));
    } else if (pts.size() == 2) {
        txt = tr("Lado %1").arg(MapDecor::formatDistance(pathLength(pts), u));
    }
    m_featureLayer->setDraftLabel(txt);
}

// Cancela el trazado en curso (si lo hay): descarta el borrador y avisa. Devuelve
// true si de verdad habia algo que cancelar. Lo llama Escape y el cambio de
// herramienta.
bool MapView::cancelDrawing()
{
    if (!m_drafting)
        return false;
    m_drafting = false;
    m_draftIsSelection = false;
    m_selectionIsMeasure = false;
    m_draft = MapFeature();
    m_featureLayer->setDraft(nullptr);
    m_featureLayer->setDraftLabel(QString());
    emit drawingCancelled();
    return true;
}

// Cierra el trazado en curso y lo convierte en entidad definitiva en el modelo.
// Si la geometria no es valida (pocos vertices) la descarta en vez de crear una
// entidad degenerada. Devuelve el id creado o -1. Lo llaman Enter, doble clic y
// clic derecho.
qint64 MapView::finishDrawing()
{
    if (!m_drafting)
        return -1;

    // Caso SELECCION de poligono: no crea entidad. Emite el poligono y deja el
    // contorno dibujado (sin linea de goma) como marca de la zona elegida.
    if (m_draftIsSelection) {
        m_drafting = false;
        const QVector<QGeoCoordinate> poly = m_draft.geometry;
        if (poly.size() < 3) {               // poligono degenerado: se descarta
            m_draftIsSelection = false;
            m_selectionIsMeasure = false;
            m_draft = MapFeature();
            m_featureLayer->setDraft(nullptr);
            m_featureLayer->setDraftLabel(QString());
            emit drawingCancelled();
            return -1;
        }
        m_featureLayer->setDraftCursor(QPoint(), false);  // apaga la goma
        m_featureLayer->setDraft(&m_draft);               // deja el contorno
        if (m_selectionIsMeasure) {
            // Medida final (sin el lado al cursor), rotulada en el centro.
            updateAreaLabel(QGeoCoordinate());
            AreaMeasurement m;
            m.polygon = poly;
            m.areaSquareMeters = polygonArea(poly);
            m.perimeterMeters = pathLength(poly, true);
            emit areaMeasured(m);
            return -1;
        }
        emit polygonSelected(poly);
        return -1;                                        // no hay id de entidad
    }

    MapFeature f = m_draft;
    m_drafting = false;
    m_draft = MapFeature();
    m_featureLayer->setDraft(nullptr);

    // Si el usuario cierra con menos vertices de los necesarios, se descarta
    // en vez de crear una geometria degenerada.
    if (!f.isValid()) {
        emit drawingCancelled();
        return -1;
    }

    const qint64 id = m_model->addFeature(f);
    if (id > 0)
        emit featureCreated(id);
    return id;
}

// Teclado sobre el mapa. Esc: cancela el trazado, o lo que la herramienta tenga
// a medias, o deselecciona (entidad Y objetivo; antes un objetivo seleccionado no
// se podia deseleccionar). Enter cierra el trazado. Supr/Retroceso quitan el
// ultimo vertice trazado o, en Editar, el vertice bajo el puntero (y, si no hay,
// la entidad). Flechas: mueven la vista (Mayus, mas); + y - (o RePag/AvPag): zoom.
void MapView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (cancelDrawing() || cancelOperation()) {
            event->accept();
            return;
        }
        if (m_model->selectedId() >= 0 || m_selectedTarget >= 0) {
            m_model->clearSelection();
            setSelectedTarget(-1);
            event->accept();
            return;
        }
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_drafting) {
            finishDrawing();
            event->accept();
            return;
        }
        break;

    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_drafting && !m_draft.geometry.isEmpty()) {
            // Deshace el ultimo vertice puesto.
            m_draft.geometry.removeLast();
            if (m_draft.geometry.isEmpty())
                cancelDrawing();
            else
                m_featureLayer->setDraft(&m_draft);
            event->accept();
            return;
        }
        if (m_tool == MapTool::EditFeature && m_model->selectedId() >= 0) {
            const qint64 sel = m_model->selectedId();
            const auto f = m_model->feature(sel);
            if (f && !capaEditable(f->layerId)) {
                emit interactionBlocked(tr("La capa «%1» está bloqueada: no se puede borrar").arg(f->layerId));
            } else if (m_hoverVertex >= 0) {
                if (!m_model->removeVertex(sel, m_hoverVertex))
                    emit interactionBlocked(tr("No se puede quitar ese vértice: la entidad "
                                               "se quedaría con menos de los necesarios"));
                m_hoverVertex = -1;
            } else {
                m_model->removeFeature(sel);
            }
            event->accept();
            return;
        }
        break;

    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int paso = qMax(40, plotArea().width() / 5)
                         * ((event->modifiers() & Qt::ShiftModifier) ? 3 : 1);
        panByPixels(event->key() == Qt::Key_Left ? -paso : event->key() == Qt::Key_Right ? paso : 0,
                    event->key() == Qt::Key_Up ? -paso : event->key() == Qt::Key_Down ? paso : 0);
        event->accept();
        return;
    }

    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_PageUp:
        setZoom(m_zoom + 1);
        event->accept();
        return;

    case Qt::Key_Minus:
    case Qt::Key_PageDown:
        setZoom(m_zoom - 1);
        event->accept();
        return;

    default:
        break;
    }
    QCustomPlot::keyPressEvent(event);
}

// Menu contextual pedido con el TECLADO (tecla de menu, Mayus+F10): se abre en el
// puntero, o en el centro de la vista si esta fuera. Con el raton no se usa: el
// clic derecho lo emite mouseReleaseEvent.
void MapView::contextMenuEvent(QContextMenuEvent *event)
{
    if (event->reason() != QContextMenuEvent::Keyboard) {
        event->ignore();
        return;
    }
    QPoint p = mapFromGlobal(QCursor::pos());
    if (!rect().contains(p))
        p = rect().center();
    emit contextMenuRequested(p, mapToGlobal(p));
    event->accept();
}

// Doble clic: si se esta trazando, lo cierra; si se esta editando y cae sobre un
// lado de una entidad, inserta un vertice ahi. En cualquier otro caso Qt manda
// el doble clic EN LUGAR del segundo pulsado: se trata como un pulsado, para no
// perder el gesto (dos clics seguidos, o un clic y un arrastre rapidos) y, sobre
// el mapa vacio con Navegar, acercar un nivel al soltar.
void MapView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QGeoCoordinate donde = coordinateAt(event->pos());

    if (m_drafting) {
        // El doble clic cierra el trazado. El primer clic del doble ya anadio
        // su vertice, asi que no se anade otro encima.
        finishDrawing();
        event->accept();
        return;
    }

    if (m_tool == MapTool::EditFeature) {
        // Doble clic sobre un lado: inserta un vertice ahi.
        const qint64 id = m_featureLayer->featureAt(event->pos());
        if (id >= 0) {
            const auto f = m_model->feature(id);
            if (f && f->kind != GeometryKind::Point) {
                const int tras = m_featureLayer->segmentAt(*f, event->pos());
                if (tras >= 0) {
                    if (!capaEditable(f->layerId)) {
                        emit interactionBlocked(tr("La capa «%1» está bloqueada: no se puede editar").arg(f->layerId));
                    } else {
                        m_model->insertVertex(id, tras + 1, donde);
                        m_model->setSelected(id);
                    }
                    event->accept();
                    return;
                }
            }
        }
    }

    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_dblPress = true;
        mousePressEvent(event);
        return;
    }
    QCustomPlot::mouseDoubleClickEvent(event);
}

// Boton izquierdo segun la herramienta activa. Con Navegar el gesto se decide
// al SOLTAR: pulsar y arrastrar mueve el mapa aunque se empiece sobre un objetivo,
// un grupo, una entidad o una marca; pulsar y soltar sin mover (menos de unos
// pixeles) es un CLIC (selecciona, abre el grupo, avisa con elementClicked; en
// vacio, deselecciona, o con doble clic acerca). Solo los rotulos y las
// entidades draggable se agarran al pulsar (y las segundas, tras el umbral).
// Medir y elegir punto actuan AL PULSAR (la marca cae donde se apunta). Con
// Editar se agarra un vertice o la entidad, y el cambio empieza tras el umbral
// para que un clic con temblor no mueva nada. El boton central mueve el mapa con
// cualquier herramienta.
void MapView::mousePressEvent(QMouseEvent *event)
{
    const bool dbl = m_dblPress;
    m_dblPress = false;

    if (event->button() == Qt::MiddleButton) {
        m_dragging = true;
        m_panButton = Qt::MiddleButton;
        m_dragStartPx = event->pos();
        m_dragStartGeo = m_center;
        m_dragMoved = false;
        m_pend = Pend::Nada;
        m_pendDbl = false;
        setCursor(Qt::ClosedHandCursor);
        m_hoverSet = false;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        switch (m_tool) {
        case MapTool::None: {
            // Prioridad 1: un clic sobre la CAJA DE ETIQUETA empieza a arrastrarla
            // (de entidad o de objetivo). El rotulo se mueve aparte de su punto;
            // como el offset es relativo, al mover el punto la etiqueta lo sigue.
            // El rotulo de una marcacion se pinta por encima de los demas, asi que
            // tambien se prueba el primero.
            const qint64 blab = m_rangeLayer ? m_rangeLayer->bearingLabelAt(QPointF(event->pos()))
                                             : -1;
            if (blab >= 0) {
                BearingLine linea;
                if (m_rangeLayer->bearing(blab, &linea)) {
                    m_dragLabelBearing = blab;
                    m_dragLabelStartPx = event->pos();
                    m_dragLabelStartOffset = linea.labelOffsetPx;
                    setCursor(Qt::SizeAllCursor);
                    break;
                }
            }
            const qint64 flab = m_featureLayer->labelAt(event->pos());
            if (flab >= 0) {
                m_dragLabelFeature = flab;
                m_dragLabelStartPx = event->pos();
                const auto f = m_model->feature(flab);
                m_dragLabelStartOffset = f ? f->style.labelOffsetPx : QPointF();
                m_model->setSelected(flab);
                m_model->beginUndoGroup();     // todo el arrastre, un solo deshacer
                setCursor(Qt::SizeAllCursor);
                break;
            }
            const qint64 tlab = m_targetLayer ? m_targetLayer->labelAt(event->pos())
                                              : -1;
            if (tlab >= 0) {
                m_dragLabelTarget = tlab;
                m_dragLabelStartPx = event->pos();
                const auto t = m_targetModel->target(tlab);
                // Se arrastra a partir del offset que se VE (el propio o el de por
                // defecto); el estado propio se guarda para poder cancelar.
                m_dragLabelStartOffset = t ? m_targetModel->effectiveLabelOffset(*t) : QPointF();
                m_dragLabelStartRaw = t ? t->labelOffsetPx : QPointF();
                m_dragLabelStartSet = t ? t->labelOffsetSet : false;
                setSelectedTarget(tlab);
                setCursor(Qt::SizeAllCursor);
                break;
            }

            // Prioridad 2: una entidad PUNTO marcada 'draggable' (un pin A/B, un
            // radar...) se arrastra en vez de desplazar el mapa. Se selecciona al
            // pulsar, pero NO se mueve hasta superar el umbral: un clic solo la
            // selecciona. La semantica (que hacer con la nueva posicion) la decide
            // la app por las senales featureDrag*; la libreria ademas la mueve.
            const qint64 fid = m_featureLayer->featureAt(event->pos());
            if (fid >= 0) {
                const auto f = m_model->feature(fid);
                if (f && f->draggable && f->kind == GeometryKind::Point) {
                    m_dragFeature = fid;
                    m_dragFeatureStarted = false;
                    m_dragFeaturePx = event->pos();
                    m_model->setSelected(fid);
                    break;
                }
            }

            // El resto (grupo, objetivo, entidad, marca o fondo) se resuelve al
            // SOLTAR; entretanto, arrastrar mueve el mapa.
            m_pend = Pend::Nada;
            m_pendId = -1;
            m_pendGrupo.clear();
            m_pendDbl = dbl;
            if (m_targetLayer) {
                m_pendGrupo = m_targetLayer->clusterAt(event->pos());
                if (!m_pendGrupo.isEmpty())
                    m_pend = Pend::Grupo;
            }
            if (m_pend == Pend::Nada) {
                const qint64 tid = targetAt(event->pos());
                if (tid >= 0) {
                    m_pend = Pend::Objetivo;
                    m_pendId = tid;
                } else if (fid >= 0) {
                    m_pend = Pend::Entidad;
                    m_pendId = fid;
                } else if (m_rangeLayer) {
                    const qint64 marca = m_rangeLayer->markAt(QPointF(event->pos()));
                    if (marca >= 0) {
                        m_pend = Pend::Marca;
                        m_pendId = marca;
                    }
                }
            }
            m_dragging = true;
            m_panButton = Qt::LeftButton;
            m_dragStartPx = event->pos();
            m_dragStartGeo = m_center;
            m_dragMoved = false;
            break;
        }

        case MapTool::AreaZoom:
        case MapTool::SelectArea:
            m_toolFirstPoint = coordinateAt(event->pos());
            m_toolFirstPointSet = true;
            if (!m_areaRect) {
                m_areaRect = new QCPItemRect(this);
                m_areaRect->setPen(QPen(Qt::black, 1, Qt::DashLine));
                m_areaRect->setBrush(QBrush(QColor(0, 0, 0, 40)));
            }
            m_areaRect->setVisible(true);
            break;

        case MapTool::Measure:
            // El punto se ancla AL PULSAR, no al soltar.
            //
            // Capturarlo en mouseReleaseEvent hacia que la marca cayera unos
            // pixeles mas abajo del clic: entre pulsar y soltar, el raton se
            // mueve un poco, y casi siempre hacia abajo al levantar el dedo.
            // El calculo pixel->coordenada era exacto; lo que fallaba era el
            // momento de capturarlo.
            handleMeasureClick(coordinateAt(event->pos()));
            break;

        case MapTool::PickPoint:
            // Por lo mismo: se emite al pulsar, que es donde el usuario
            // apunta, no donde acaba soltando.
            emit pointPicked(coordinateAt(event->pos()));
            break;

        case MapTool::DrawPoint: {
            if (!capaEditable(m_activeLayer)) {
                emit interactionBlocked(tr("La capa «%1» está bloqueada: elige otra para dibujar").arg(m_activeLayer));
                break;
            }
            MapFeature f;
            f.kind = GeometryKind::Point;
            f.layerId = m_activeLayer;
            f.type = m_draftType;
            f.style = m_draftStyle;
            f.geometry = {coordinateAt(event->pos())};
            const qint64 id = m_model->addFeature(f);
            if (id > 0)
                emit featureCreated(id);
            break;
        }

        case MapTool::DrawPolyline:
        case MapTool::DrawPolygon:
            if (!m_drafting) {
                if (!capaEditable(m_activeLayer)) {
                    emit interactionBlocked(tr("La capa «%1» está bloqueada: elige otra para dibujar").arg(m_activeLayer));
                    break;
                }
                m_drafting = true;
                m_draftIsSelection = false;
                m_draft = MapFeature();
                m_draft.kind = (m_tool == MapTool::DrawPolygon)
                                   ? GeometryKind::Polygon
                                   : GeometryKind::Polyline;
                m_draft.layerId = m_activeLayer;
                m_draft.type = m_draftType;
                m_draft.style = m_draftStyle;
                // El tope se guarda en la entidad para que la edicion posterior
                // (insertVertex) tambien lo respete.
                if (m_draftMaxVertices > 0)
                    m_draft.attributes.insert(QStringLiteral("max_vertices"),
                                              m_draftMaxVertices);
            }
            // Tope de vertices: al llegar al limite se ignoran los clics nuevos.
            if (m_draftMaxVertices < 0
                || m_draft.geometry.size() < m_draftMaxVertices) {
                m_draft.geometry.append(coordinateAt(event->pos()));
                m_featureLayer->setDraft(&m_draft);
            }
            break;

        case MapTool::SelectPolygon:
        case MapTool::MeasureArea:
            // Igual que dibujar un poligono, pero es una SELECCION transitoria:
            // no crea entidad. Reusa el borrador de FeatureLayer para la linea
            // de goma; doble clic / Enter / clic derecho lo cierra (finishDrawing
            // detecta m_draftIsSelection y emite polygonSelected, o areaMeasured
            // si es una medida). Un clic tras una medida ya cerrada empieza otra.
            if (!m_drafting) {
                m_drafting = true;
                m_draftIsSelection = true;
                m_selectionIsMeasure = (m_tool == MapTool::MeasureArea);
                m_draft = MapFeature();
                m_draft.kind = GeometryKind::Polygon;
                m_draft.style.lineColor = m_selectionIsMeasure ? QColor(0x6a, 0x1b, 0x9a)
                                                               : QColor(0, 0, 0);
                m_draft.style.lineWidth = 2;
            }
            m_draft.geometry.append(coordinateAt(event->pos()));
            m_featureLayer->setDraft(&m_draft);
            if (m_selectionIsMeasure)
                updateAreaLabel(coordinateAt(event->pos()));
            break;

        case MapTool::EditFeature: {
            const qint64 seleccionada = m_model->selectedId();
            m_editStarted = false;
            m_editPressPx = event->pos();

            // Primero se mira si hay un tirador de vertice bajo el cursor:
            // tiene prioridad sobre la propia entidad, porque los tiradores
            // caen encima de ella.
            if (seleccionada >= 0) {
                const int v = m_featureLayer->vertexAt(seleccionada, event->pos());
                if (v >= 0) {
                    const auto f = m_model->feature(seleccionada);
                    if (f && !capaEditable(f->layerId)) {
                        emit interactionBlocked(tr("La capa «%1» está bloqueada: no se puede editar").arg(f->layerId));
                        break;
                    }
                    // Ctrl + clic en un tirador: quita ese vertice.
                    if (event->modifiers() & Qt::ControlModifier) {
                        if (!m_model->removeVertex(seleccionada, v))
                            emit interactionBlocked(tr("No se puede quitar ese vértice: la entidad "
                                                       "se quedaría con menos de los necesarios"));
                        break;
                    }
                    m_editVertex = v;
                    m_lastEditPos = coordinateAt(event->pos());
                    break;
                }
            }

            const qint64 id = m_featureLayer->featureAt(event->pos());
            m_model->setSelected(id);
            if (id >= 0) {
                emit featureClicked(id, coordinateAt(event->pos()));
                const auto f = m_model->feature(id);
                if (f && !capaEditable(f->layerId)) {
                    emit interactionBlocked(tr("La capa «%1» está bloqueada: no se puede editar").arg(f->layerId));
                    break;
                }
                m_movingFeature = true;
                m_lastEditPos = coordinateAt(event->pos());
            }
            break;
        }

        default:
            break;
        }
    }
    event->accept();
}

// Movimiento del raton: emite la coordenada bajo el cursor y, segun el estado,
// arrastra un vertice/entidad (prioridad sobre el resto), estira el "lado de goma"
// del trazado, desplaza el mapa (calculado en el eje proyectado, no en latitud) o
// redimensiona el recuadro de area / la linea de medir. Todo en coordenadas de
// Mercator para que no haya desfase con el zoom ni la latitud. Un arrastre (del
// mapa, de una entidad o de un vertice) no empieza hasta superar unos pixeles
// desde el pulsado: antes es un clic. Sin botones pulsados, pone el cursor segun
// lo que haya debajo.
void MapView::mouseMoveEvent(QMouseEvent *event)
{
    const QGeoCoordinate bajo = coordinateAt(event->pos());
    emit mouseMovedTo(bajo);

    // Arrastre de una etiqueta: nuevo offset = inicial + (cursor - inicio). Tiene
    // prioridad y no toca la geometria ni el mapa.
    if (m_dragLabelFeature >= 0) {
        m_model->setLabelOffset(m_dragLabelFeature,
            m_dragLabelStartOffset + QPointF(event->pos() - m_dragLabelStartPx));
        event->accept();
        return;
    }
    if (m_dragLabelTarget >= 0) {
        m_targetModel->setLabelOffset(m_dragLabelTarget,
            m_dragLabelStartOffset + QPointF(event->pos() - m_dragLabelStartPx));
        event->accept();
        return;
    }
    if (m_dragLabelBearing >= 0) {
        m_rangeLayer->setBearingLabelOffset(m_dragLabelBearing,
            m_dragLabelStartOffset + QPointF(event->pos() - m_dragLabelStartPx));
        event->accept();
        return;
    }

    // Arrastre de una entidad draggable: la mueve en el modelo (no-op si la app
    // la recreo) y avisa con la coordenada para que la app actualice su estado.
    // Empieza (y abre su grupo de deshacer) al superar el umbral.
    if (m_dragFeature >= 0) {
        if (!m_dragFeatureStarted) {
            if ((event->pos() - m_dragFeaturePx).manhattanLength() <= kClickSlopPx) {
                event->accept();
                return;
            }
            m_dragFeatureStarted = true;
            m_model->beginUndoGroup();
            setCursor(Qt::ClosedHandCursor);
            emit featureDragStarted(m_dragFeature);
        }
        if (bajo.isValid()) {
            m_model->moveVertex(m_dragFeature, 0, bajo);
            emit featureDragMovedTo(m_dragFeature, bajo);
        }
        event->accept();
        return;
    }

    // Edicion en curso: tiene prioridad sobre el desplazamiento del mapa.
    if (m_tool == MapTool::EditFeature
        && (m_editVertex >= 0 || m_movingFeature)) {
        if (!m_editStarted) {
            if ((event->pos() - m_editPressPx).manhattanLength() <= kClickSlopPx) {
                event->accept();
                return;
            }
            m_editStarted = true;
            m_model->beginUndoGroup();     // todo el arrastre, un solo deshacer
        }
        const qint64 id = m_model->selectedId();
        if (id >= 0 && m_lastEditPos.isValid() && bajo.isValid()) {
            if (m_editVertex >= 0) {
                m_model->moveVertex(id, m_editVertex, bajo);
            } else {
                m_model->moveFeature(id,
                                     bajo.latitude() - m_lastEditPos.latitude(),
                                     bajo.longitude() - m_lastEditPos.longitude());
            }
            m_lastEditPos = bajo;
        }
        event->accept();
        return;
    }

    // Mientras se traza, el lado siguiente sigue al raton.
    if (m_drafting) {
        m_featureLayer->setDraftCursor(event->pos(), true);
        if (m_selectionIsMeasure)
            updateAreaLabel(bajo);
    }

    if (m_dragging) {
        // Hasta superar el umbral es un clic, no un arrastre.
        if (!m_dragMoved) {
            if ((event->pos() - m_dragStartPx).manhattanLength() <= kClickSlopPx) {
                event->accept();
                return;
            }
            m_dragMoved = true;
            m_pend = Pend::Nada;           // ya no es un clic sobre nada
            setCursor(Qt::ClosedHandCursor);
        }
        // El desplazamiento se calcula en el eje PROYECTADO, no en latitud:
        // arrastrar 100 pixeles no equivale al mismo numero de grados de
        // latitud arriba que abajo del mapa.
        const QPointF inicio(xAxis->pixelToCoord(m_dragStartPx.x()),
                             yAxis->pixelToCoord(m_dragStartPx.y()));
        const QPointF ahora(xAxis->pixelToCoord(event->pos().x()),
                            yAxis->pixelToCoord(event->pos().y()));

        const double centroMercY =
            WebMercator::latitudeToMercatorDegrees(m_center.latitude());

        applyZoomToAxes(fromAxis(
            QPointF(m_center.longitude() + (inicio.x() - ahora.x()),
                    centroMercY + (inicio.y() - ahora.y()))));
        m_dragStartPx = event->pos();

        emit centerChanged(m_center);
        requestVisibleTiles();
        refreshPlan();
    } else if ((m_tool == MapTool::AreaZoom || m_tool == MapTool::SelectArea)
               && m_toolFirstPointSet && m_areaRect) {
        // toAxis() y no la latitud a secas: el eje Y va en grados de
        // MERCATOR, no de latitud. Pasarle la latitud coloca el item por
        // debajo del cursor. Sobre Cuba a zoom 3 el desfase es de unos 4
        // pixeles, pero crece con el zoom y con la latitud: a zoom 11 son
        // casi mil pixeles, o sea fuera de la pantalla.
        const QPointF ejeIni = toAxis(m_toolFirstPoint);
        const QPointF ejeAct = toAxis(bajo);
        m_areaRect->topLeft->setCoords(ejeIni.x(), ejeIni.y());
        m_areaRect->bottomRight->setCoords(ejeAct.x(), ejeAct.y());
        replot(QCustomPlot::rpQueuedReplot);
    } else if (m_tool == MapTool::Measure && m_toolFirstPointSet && m_measureLine) {
        const QPointF eje = toAxis(bajo);
        m_measureLine->end->setCoords(eje.x(), eje.y());
        replot(QCustomPlot::rpQueuedReplot);
    }

    if (event->buttons() == Qt::NoButton)
        actualizarCursorHover(event->pos());

    event->accept();
}

// Soltar el boton cierra el gesto en curso: termina la edicion (cerrando el grupo
// de deshacer), suelta el arrastre del mapa o decide el CLIC pendiente (ver
// mousePressEvent), o confirma el area seleccionada (emite areaSelected y, si es
// AreaZoom, encuadra; si es SelectArea, deja el recuadro como marca). El clic
// derecho cierra el trazado, o cancela lo que la herramienta tenga a medias, o
// pide el menu contextual. Medir/elegir ya se atendieron al pulsar.
void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    const QGeoCoordinate donde = coordinateAt(event->pos());
    const bool izq = event->button() == Qt::LeftButton;

    // Fin del arrastre de una etiqueta.
    if (izq && m_dragLabelFeature >= 0) {
        m_model->endUndoGroup();
        m_dragLabelFeature = -1;
        unsetCursor();
        m_hoverSet = false;
        event->accept();
        return;
    }
    if (izq && m_dragLabelTarget >= 0) {
        const qint64 id = m_dragLabelTarget;
        m_dragLabelTarget = -1;
        if (!rect().contains(event->pos())) {
            // Soltado fuera del mapa: se cancela y el rotulo vuelve a como estaba.
            m_targetModel->restoreLabelOffset(id, m_dragLabelStartRaw, m_dragLabelStartSet);
        } else if (const auto t = m_targetModel->target(id);
                   t && m_targetModel->effectiveLabelOffset(*t) != m_dragLabelStartOffset) {
            emit targetLabelMoved(id, t->labelOffsetPx);
        }
        unsetCursor();
        m_hoverSet = false;
        event->accept();
        return;
    }
    if (izq && m_dragLabelBearing >= 0) {
        m_dragLabelBearing = -1;
        unsetCursor();
        m_hoverSet = false;
        event->accept();
        return;
    }

    // Fin del arrastre de una entidad draggable. Si no llego a moverse fue un
    // clic: la entidad ya quedo seleccionada al pulsar.
    if (izq && m_dragFeature >= 0) {
        const qint64 id = m_dragFeature;
        const bool movio = m_dragFeatureStarted;
        m_dragFeature = -1;
        m_dragFeatureStarted = false;
        unsetCursor();
        m_hoverSet = false;
        if (movio) {
            m_model->endUndoGroup();
            emit featureDragFinished(id);
        } else {
            emit featureClicked(id, donde);
            emit elementClicked(event->pos(), globalDe(event));
            emit mapClicked(donde, Qt::LeftButton);
        }
        event->accept();
        return;
    }

    if (izq && (m_editVertex >= 0 || m_movingFeature)) {
        if (m_editStarted)
            m_model->endUndoGroup();
        m_editStarted = false;
        m_editVertex = -1;
        m_movingFeature = false;
        m_lastEditPos = QGeoCoordinate();
        event->accept();
        return;
    }

    // Fin del arrastre del mapa (boton izquierdo o central). Sin moverse es un
    // clic: aqui se decide lo que estaba pendiente al pulsar.
    if (m_dragging && event->button() == m_panButton) {
        m_dragging = false;
        unsetCursor();
        m_hoverSet = false;
        const bool movio = m_dragMoved;
        const Pend pend = m_pend;
        const bool dbl = m_pendDbl;
        const qint64 id = m_pendId;
        const QVector<qint64> grupo = m_pendGrupo;
        const Qt::MouseButton boton = m_panButton;
        m_dragMoved = false;
        m_pend = Pend::Nada;
        m_pendDbl = false;
        m_pendId = -1;
        m_pendGrupo.clear();
        m_panButton = Qt::NoButton;
        if (!movio) {
            if (boton == Qt::LeftButton) {
                // Un doble clic sobre un elemento no repite lo que hizo el primero.
                switch (pend) {
                case Pend::Objetivo:
                    if (!dbl) {
                        setSelectedTarget(id);
                        emit targetClicked(id, donde);
                        emit elementClicked(event->pos(), globalDe(event));
                    }
                    break;
                case Pend::Grupo:
                    if (!dbl)
                        emit targetClusterClicked(grupo, donde);
                    break;
                case Pend::Entidad:
                    if (!dbl) {
                        m_model->setSelected(id);
                        emit featureClicked(id, donde);
                        emit elementClicked(event->pos(), globalDe(event));
                    }
                    break;
                case Pend::Marca:
                    if (!dbl)
                        emit elementClicked(event->pos(), globalDe(event));
                    break;
                case Pend::Nada:
                    if (dbl) {
                        const QPointF pos(event->pos());
                        setZoom(m_zoom + 1, &pos);      // doble clic en vacio: acerca
                    } else {
                        m_model->clearSelection();      // clic en vacio: deselecciona
                        setSelectedTarget(-1);
                    }
                    break;
                }
            }
            emit mapClicked(donde, boton);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        switch (m_tool) {
        case MapTool::PickPoint:
            break;      // ya se emitio al pulsar

        case MapTool::Measure:
            break;      // ya se atendio al pulsar

        case MapTool::AreaZoom:
        case MapTool::SelectArea:
            if (m_toolFirstPointSet) {
                m_toolFirstPointSet = false;

                const QGeoCoordinate no(qMax(m_toolFirstPoint.latitude(),
                                             donde.latitude()),
                                        qMin(m_toolFirstPoint.longitude(),
                                             donde.longitude()));
                const QGeoCoordinate se(qMin(m_toolFirstPoint.latitude(),
                                             donde.latitude()),
                                        qMax(m_toolFirstPoint.longitude(),
                                             donde.longitude()));
                emit areaSelected(no, se);

                if (m_tool == MapTool::AreaZoom) {
                    // Ampliar: el recuadro se oculta y saltamos a la zona.
                    if (m_areaRect)
                        m_areaRect->setVisible(false);
                    fitBounds(no, se);
                } else {
                    // SelectArea: dejamos el recuadro visible como marca de la
                    // zona elegida; NO hacemos zoom.
                    replot(QCustomPlot::rpQueuedReplot);
                }
            }
            break;

        default:
            emit mapClicked(donde, event->button());
            break;
        }
    } else if (event->button() == Qt::RightButton && m_drafting) {
        finishDrawing();
    } else if (event->button() == Qt::RightButton && cancelOperation()) {
        // El clic derecho cancela lo que la herramienta tenia a medias (la
        // medida, el recuadro, la captura de un punto) en vez de abrir el menu.
    } else {
        emit mapClicked(donde, event->button());
        if (event->button() == Qt::RightButton)
            emit contextMenuRequested(event->pos(), globalDe(event));
    }

    event->accept();
}

// Cambia la herramienta activa (arrastrar, medir, dibujar, editar, seleccionar
// area...). Abandona cualquier trazado o edicion a medias, oculta las marcas
// temporales y ajusta el cursor. Fija StrongFocus para poder recibir Escape/Supr.
void MapView::setActiveTool(MapTool tool)
{
    // Cambiar de herramienta abandona lo que hubiera a medias: dejarlo vivo
    // haria que un trazado reapareciera al volver a la herramienta.
    cancelDrawing();
    m_editVertex = -1;
    m_movingFeature = false;
    m_lastEditPos = QGeoCoordinate();

    // Un poligono de seleccion YA cerrado queda como borrador visible (no estaba
    // "en curso", asi que cancelDrawing no lo toca): se limpia aqui al cambiar de
    // herramienta, igual que el recuadro de SelectArea.
    if (m_draftIsSelection) {
        m_draftIsSelection = false;
        m_selectionIsMeasure = false;
        m_draft = MapFeature();
        if (m_featureLayer) {
            m_featureLayer->setDraft(nullptr);
            m_featureLayer->setDraftLabel(QString());
        }
    }

    m_tool = tool;
    m_toolFirstPointSet = false;
    m_pend = Pend::Nada;
    m_pendDbl = false;
    m_dblPress = false;
    m_hoverVertex = -1;
    m_hoverSet = false;               // el cursor de la herramienta manda hasta el primer hover
    if (m_measureLine) m_measureLine->setVisible(false);
    if (m_areaRect)    m_areaRect->setVisible(false);

    setCursor(tool == MapTool::None ? Qt::ArrowCursor
              : tool == MapTool::EditFeature ? Qt::PointingHandCursor
                                             : Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);   // hace falta para recibir Escape y Supr
    replot(QCustomPlot::rpQueuedReplot);
}

} // namespace libmapa
