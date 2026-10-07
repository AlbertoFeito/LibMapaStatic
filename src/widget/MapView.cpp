#include "widget/MapView.h"

#include "core/Logging.h"
#include "geo/GeoMath.h"
#include "geo/TileMatrix.h"
#include "geo/WebMercator.h"
#include "geo/WebMercator.h"

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
    m_targetLayer = new TargetLayer(this, m_targetModel);
    m_targetLayer->setLayer(QStringLiteral("targets"));
    m_targetLayer->setAxisMapper(
        [](const QGeoCoordinate &c) { return MapView::toAxis(c); });

    // Capa de COBERTURA, encima de todo: una mancha de diagnostico (que teselas
    // de un zoom hay en la BD). Oculta por defecto; la enciende fill_map.
    addLayer(QStringLiteral("coverage"), layer(QStringLiteral("targets")),
             QCustomPlot::limAbove);
    m_coverageLayer = new CoverageLayer(this);
    m_coverageLayer->setLayer(QStringLiteral("coverage"));
    m_coverageLayer->setVisible(false);

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
}

// Oculta el recuadro de "seleccionar area" si estaba visible (sin borrarlo, para
// reutilizar el item). Lo usa MapWidget al confirmar o descartar la seleccion.
void MapView::clearAreaSelection()
{
    if (m_areaRect && m_areaRect->visible()) {
        m_areaRect->setVisible(false);
        replot(QCustomPlot::rpQueuedReplot);
    }
}

// Recalcula el plan de dibujo de teselas para el viewport actual (que pintar y
// con que respaldo) y encola un repintado. Se llama tras cada cambio de vista.
void MapView::refreshPlan()
{
    if (!m_service || !m_tileLayer)
        return;
    m_tileLayer->setPlan(m_service->planFor(visibleNorthWest(),
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

// Rueda del raton = zoom por pasos discretos, anclado al punto bajo el cursor
// (para eso pasa la posicion a setZoom). Ignora deltas que no llegan a un paso.
void MapView::wheelEvent(QWheelEvent *event)
{
    const int pasos = event->angleDelta().y() / 120;
    if (pasos == 0) {
        event->ignore();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPointF pos = event->position();
#else
    const QPointF pos = event->posF();
#endif

    setZoom(m_zoom + pasos, &pos);
    event->accept();
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

// Cancela el trazado en curso (si lo hay): descarta el borrador y avisa. Devuelve
// true si de verdad habia algo que cancelar. Lo llama Escape y el cambio de
// herramienta.
bool MapView::cancelDrawing()
{
    if (!m_drafting)
        return false;
    m_drafting = false;
    m_draftIsSelection = false;
    m_draft = MapFeature();
    m_featureLayer->setDraft(nullptr);
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
            m_draft = MapFeature();
            m_featureLayer->setDraft(nullptr);
            emit drawingCancelled();
            return -1;
        }
        m_featureLayer->setDraftCursor(QPoint(), false);  // apaga la goma
        m_featureLayer->setDraft(&m_draft);               // deja el contorno
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

// Atajos de teclado sensibles al contexto: Escape cancela el trazado o la
// seleccion; Enter cierra el trazado; Supr/Retroceso deshace el ultimo vertice
// mientras se dibuja o borra la entidad seleccionada al editar. Lo no manejado se
// delega en QCustomPlot.
void MapView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (cancelDrawing()) {
            event->accept();
            return;
        }
        if (m_model->selectedId() >= 0) {
            m_model->clearSelection();
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
            m_model->removeFeature(m_model->selectedId());
            event->accept();
            return;
        }
        break;

    default:
        break;
    }
    QCustomPlot::keyPressEvent(event);
}

// Doble clic: si se esta trazando, lo cierra; si se esta editando y cae sobre un
// lado de una entidad, inserta un vertice ahi. En cualquier otro caso, delega en
// QCustomPlot.
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
                    m_model->insertVertex(id, tras + 1, donde);
                    m_model->setSelected(id);
                    event->accept();
                    return;
                }
            }
        }
    }

    QCustomPlot::mouseDoubleClickEvent(event);
}

// Boton izquierdo segun la herramienta activa: arrastrar el mapa (None), anclar
// el rectangulo de area (AreaZoom/SelectArea), medir o elegir punto (al PULSAR,
// no al soltar, para que la marca caiga donde se apunta), colocar/continuar el
// trazado de una entidad, o en modo edicion agarrar un vertice o mover/seleccionar
// una entidad (abriendo un grupo de deshacer para todo el arrastre).
void MapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        switch (m_tool) {
        case MapTool::None: {
            // Sin herramienta, un clic sobre un objetivo lo SELECCIONA (no inicia
            // arrastre); sobre el mapa vacio, arrastra para desplazar.
            const qint64 tid = targetAt(event->pos());
            if (tid >= 0) {
                setSelectedTarget(tid);
                emit targetClicked(tid, coordinateAt(event->pos()));
                break;
            }
            m_dragging = true;
            m_dragStartPx = event->pos();
            m_dragStartGeo = m_center;
            setCursor(Qt::ClosedHandCursor);
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
                m_drafting = true;
                m_draftIsSelection = false;
                m_draft = MapFeature();
                m_draft.kind = (m_tool == MapTool::DrawPolygon)
                                   ? GeometryKind::Polygon
                                   : GeometryKind::Polyline;
                m_draft.layerId = m_activeLayer;
                m_draft.type = m_draftType;
                m_draft.style = m_draftStyle;
            }
            m_draft.geometry.append(coordinateAt(event->pos()));
            m_featureLayer->setDraft(&m_draft);
            break;

        case MapTool::SelectPolygon:
            // Igual que dibujar un poligono, pero es una SELECCION transitoria:
            // no crea entidad. Reusa el borrador de FeatureLayer para la linea
            // de goma; doble clic / Enter / clic derecho lo cierra (finishDrawing
            // detecta m_draftIsSelection y emite polygonSelected).
            if (!m_drafting) {
                m_drafting = true;
                m_draftIsSelection = true;
                m_draft = MapFeature();
                m_draft.kind = GeometryKind::Polygon;
                m_draft.style.lineColor = QColor(0, 0, 0);
                m_draft.style.lineWidth = 2;
            }
            m_draft.geometry.append(coordinateAt(event->pos()));
            m_featureLayer->setDraft(&m_draft);
            break;

        case MapTool::EditFeature: {
            const qint64 seleccionada = m_model->selectedId();

            // Primero se mira si hay un tirador de vertice bajo el cursor:
            // tiene prioridad sobre la propia entidad, porque los tiradores
            // caen encima de ella.
            if (seleccionada >= 0) {
                const int v = m_featureLayer->vertexAt(seleccionada, event->pos());
                if (v >= 0) {
                    m_editVertex = v;
                    m_lastEditPos = coordinateAt(event->pos());
                    // Un arrastre genera decenas de moveVertex; deben
                    // deshacerse de una sola vez.
                    m_model->beginUndoGroup();
                    break;
                }
            }

            const qint64 id = m_featureLayer->featureAt(event->pos());
            m_model->setSelected(id);
            if (id >= 0) {
                emit featureClicked(id, coordinateAt(event->pos()));
                m_movingFeature = true;
                m_lastEditPos = coordinateAt(event->pos());
                m_model->beginUndoGroup();
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
// Mercator para que no haya desfase con el zoom ni la latitud.
void MapView::mouseMoveEvent(QMouseEvent *event)
{
    const QGeoCoordinate bajo = coordinateAt(event->pos());
    emit mouseMovedTo(bajo);

    // Edicion en curso: tiene prioridad sobre el desplazamiento del mapa.
    if (m_tool == MapTool::EditFeature
        && (m_editVertex >= 0 || m_movingFeature)) {
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
    }

    if (m_dragging) {
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

    event->accept();
}

// Soltar el boton cierra el gesto en curso: termina la edicion (cerrando el grupo
// de deshacer), suelta el arrastre del mapa, o confirma el area seleccionada
// (emite areaSelected y, si es AreaZoom, encuadra; si es SelectArea, deja el
// recuadro como marca). El clic derecho cierra el trazado. Medir/elegir ya se
// atendieron al pulsar.
void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    const QGeoCoordinate donde = coordinateAt(event->pos());

    if (m_editVertex >= 0 || m_movingFeature) {
        m_model->endUndoGroup();
        m_editVertex = -1;
        m_movingFeature = false;
        m_lastEditPos = QGeoCoordinate();
        event->accept();
        return;
    }

    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
        emit mapClicked(donde, event->button());
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
    } else {
        emit mapClicked(donde, event->button());
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
        m_draft = MapFeature();
        if (m_featureLayer) m_featureLayer->setDraft(nullptr);
    }

    m_tool = tool;
    m_toolFirstPointSet = false;
    if (m_measureLine) m_measureLine->setVisible(false);
    if (m_areaRect)    m_areaRect->setVisible(false);

    setCursor(tool == MapTool::None ? Qt::ArrowCursor
              : tool == MapTool::EditFeature ? Qt::PointingHandCursor
                                             : Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);   // hace falta para recibir Escape y Supr
    replot(QCustomPlot::rpQueuedReplot);
}

} // namespace libmapa
