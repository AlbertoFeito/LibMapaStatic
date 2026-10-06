#ifndef LIBMAPA_MAPWIDGET_H_
#define LIBMAPA_MAPWIDGET_H_

#include "libmapa/DataPackage.h"
#include "libmapa/GeoFile.h"
#include "libmapa/Elevation.h"
#include "libmapa/MapFeature.h"
#include "libmapa/MapTarget.h"
#include "libmapa/TargetSymbol.h"
#include "libmapa/MapTypes.h"
#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <limits>
#include <memory>
#include <optional>

namespace libmapa {

//! Ajustes de arranque del widget.
struct MapConfig
{
    /*!
     * \brief Carpeta del PAQUETE DE DATOS (la que contiene `mapa.json`), o la
     *        ruta del propio manifiesto. Es la forma recomendada:
     *
     *     cfg.dataDir = QCoreApplication::applicationDirPath() + "/datos";
     *
     * Con eso salen las capas base, la elevacion, las capas fijas, la BD de
     * entidades del usuario y el punto de arranque. Cualquier otro campo de
     * esta estructura que se rellene a mano TIENE PRIORIDAD sobre el paquete.
     * Vacio = sin paquete (configuracion clasica con datasetsFile).
     */
    QString dataDir;

    /*!
     * \brief Ruta al datasets.json generado por probe_db.
     *
     * Ahi estan las convenciones de cada BD: el mapeo de zoom, el esquema del
     * eje Y, el nivel de fondo garantizado y el relleno tipico. Nada de eso
     * se cablea en el codigo.
     */
    QString datasetsFile;

    //! Carpeta con ficheros de elevacion SRTM `.hgt` (nombres tipo `N19W077.hgt`).
    //! Vacia = sin elevacion por carpeta. Opcional.
    QString elevationDir;

    //! Base de datos de elevacion (`.sqlitedb` generada por `dem_to_db`). Si viene,
    //! TIENE PRIORIDAD sobre elevationDir. Vacia = no se usa. Opcional.
    QString elevationDbFile;

    //! Base de datos de ENTIDADES (puntos/lineas/poligonos dibujados). Si viene,
    //! el widget la carga al abrir y guarda SOLO lo que se dibuje/edite/borre
    //! (con antirebote). Vacia = sin persistencia automatica. Opcional.
    QString featuresDbFile;

    QString initialLayerId;                    //!< Vacio = el del paquete o el primero del JSON.
    //! Invalido = el del paquete ("start.center"), o La Habana si no hay paquete.
    QGeoCoordinate initialCenter;
    int initialZoom = -1;                      //!< < 0 = el del paquete, o 10.

    int cacheMiB = 128;         //!< Memoria para teselas ya decodificadas.
    int debounceMs = 80;        //!< Agrupacion de peticiones al arrastrar.

    //! Color de las zonas sin ninguna tesela. Azul mar por defecto: las BD no
    //! guardan teselas de oceano abierto y ese color ES el mapa correcto.
    QColor noDataColor = QColor(0x9d, 0xd3, 0xdf);
};

/*!
 * \brief Widget de mapa embebible.
 *
 * Se usa asi, y esto es todo lo que hace falta:
 *
 *     libmapa::MapConfig cfg;
 *     cfg.dataDir = QCoreApplication::applicationDirPath() + "/datos";
 *
 *     auto *mapa = new libmapa::MapWidget(cfg, this);
 *     ui->contenedor->layout()->addWidget(mapa);
 *
 *     connect(ui->btnSat, &QPushButton::clicked, mapa, [mapa]{
 *         mapa->setBaseLayerId("satelital");
 *     });
 *
 * El motor de dibujo es QCustomPlot, pero NO aparece en esta cabecera. En el
 * codigo original, cmapaplot.h hacia #include "qcustomplot.h" y CMapaPlot
 * heredaba publicamente de QCustomPlot: cualquier proyecto que usara la
 * libreria se tragaba 300 KB de cabecera y veia doscientos metodos que no
 * deberia tocar. Aqui QCustomPlot queda dentro del PIMPL.
 *
 * Quien quiera acceso directo al QCustomPlot interno para dibujar sus propias
 * capas puede pedirlo con customPlot(), pero es una salida de emergencia
 * explicita, no la via normal.
 */
class LIBMAPA_EXPORT MapWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString baseLayerId READ baseLayerId WRITE setBaseLayerId
                   NOTIFY baseLayerChanged)
    Q_PROPERTY(int zoom READ zoom WRITE setZoom NOTIFY zoomChanged)

public:
    explicit MapWidget(const MapConfig &config, QWidget *parent = nullptr);
    ~MapWidget() override;

    //! false si no se pudo abrir ninguna base de datos de mapas.
    bool isReady() const;
    QString lastError() const;

    //! Paquete de datos abierto (nombre, version, atribucion...). Invalido si el
    //! widget se configuro sin MapConfig::dataDir.
    DataPackageInfo packageInfo() const;

    /*!
     * \brief Problemas encontrados en los datos al abrir el paquete, uno por
     *        linea ("satelital: ..."). Vacio = todo bien.
     *
     * Con MapConfig::dataDir el widget hace una comprobacion RAPIDA (milisegundos)
     * de lo que dejaria el mapa en blanco sin decir nada: que falte un fichero,
     * que una base no abra o que sus imagenes no se puedan decodificar (falta el
     * plugin de imagen de Qt al desplegar). El mapa arranca igual con lo que si
     * funciona; la aplicacion decide si avisar. Para el informe completo,
     * con cobertura por zoom, esta la herramienta check_data.
     */
    QStringList dataWarnings() const;

    // --- Capa base -------------------------------------------------------
    QVector<BaseLayerInfo> availableBaseLayers() const;
    QString baseLayerId() const;
    bool setBaseLayerId(const QString &id);

    // --- Navegacion ------------------------------------------------------
    QGeoCoordinate center() const;
    void setCenter(const QGeoCoordinate &center);

    int zoom() const;
    void setZoom(int zoom);
    int minZoom() const;
    int maxZoom() const;

    //! Permite SOBRE-ZOOM por encima del maximo recomendado del dataset (hasta
    //! \a zoom). -1 restaura el limite normal. Pensado para herramientas de
    //! descarga (fill_map): navegar/enmarcar niveles altos (z15/16) que se van a
    //! bajar, aunque la base recomiende un maximo menor.
    void setMaxZoomOverride(int zoom);

    /*!
     * \brief Olvida las teselas en cache y vuelve a pedir las del viewport.
     *
     * Util despues de MODIFICAR la base de teselas por fuera (p. ej. tras
     * rellenar huecos con la herramienta de descarga): sin esto, la cache
     * seguiria mostrando lo de antes -incluidas las marcas de "no existe"-.
     */
    void reloadBaseLayer();

    //! Quita el recuadro dibujado con la herramienta SelectArea.
    void clearAreaSelection();

    void zoomIn();
    void zoomOut();
    void fitBounds(const QGeoCoordinate &northWest,
                   const QGeoCoordinate &southEast);

    //! Extension visible ahora mismo.
    QGeoCoordinate visibleNorthWest() const;
    QGeoCoordinate visibleSouthEast() const;

    // --- Capas de entidades ----------------------------------------------
    /*!
     * \brief Crea una capa. Si ya existe, no hace nada y devuelve false.
     *
     * "Pueden existir varias de cada uno" se resuelve creando las capas que
     * hagan falta: una para zonas prohibidas, otra para puntos de interes,
     * otra para areas de vigilancia. La libreria no impone ninguna.
     */
    bool addFeatureLayer(const QString &id, const QString &displayName = QString(),
                         int zOrder = 0);
    bool removeFeatureLayer(const QString &id);
    QVector<LayerInfo> featureLayers() const;
    bool setFeatureLayerVisible(const QString &id, bool visible);
    bool setFeatureLayerZOrder(const QString &id, int zOrder);

    // --- Entidades -------------------------------------------------------
    //! Devuelve el identificador asignado, o -1 si la geometria no es valida.
    qint64 addFeature(const MapFeature &feature);
    bool updateFeature(const MapFeature &feature);
    bool removeFeature(qint64 id);
    void clearFeatureLayer(const QString &layerId);
    void clearFeatures();

    std::optional<MapFeature> feature(qint64 id) const;
    QVector<MapFeature> features() const;
    QVector<MapFeature> featuresInLayer(const QString &layerId) const;
    //! Filtra por la etiqueta de dominio que puso la aplicacion.
    QVector<MapFeature> featuresOfType(const QString &type) const;
    int featureCount() const;

    // --- Edicion de geometria --------------------------------------------
    bool moveVertex(qint64 id, int index, const QGeoCoordinate &to);
    bool insertVertex(qint64 id, int index, const QGeoCoordinate &at);
    bool removeVertex(qint64 id, int index);
    bool moveFeature(qint64 id, double deltaLat, double deltaLon);

    // --- Deshacer y rehacer ----------------------------------------------
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();
    void clearUndoHistory();

    // --- Guardar y cargar ------------------------------------------------
    /*!
     * \brief Vuelca todas las entidades y capas a un fichero SQLite (manual).
     *
     * Volcado COMPLETO (borra y reescribe). Es la via manual: la app decide
     * cuando. Para guardado AUTOMATICO ver \ref setFeaturesDbFile.
     */
    bool saveFeaturesTo(const QString &databasePath);
    bool loadFeaturesFrom(const QString &databasePath);

    // --- Persistencia automatica de entidades ----------------------------
    /*!
     * \brief Enciende el guardado AUTOMATICO de entidades en \a databasePath.
     *
     * Si el fichero existe, lo carga ahora (reemplaza el contenido). A partir de
     * aqui, cada alta/edicion/borrado de entidad o capa se guarda SOLO, con un
     * pequeno antirebote que agrupa las rafagas (p.ej. arrastrar un vertice).
     * Cadena vacia = apaga la persistencia automatica (no borra el fichero).
     * Se puede fijar tambien al arrancar con \c MapConfig::featuresDbFile.
     *
     * NOTA: los OBJETIVOS en movimiento (TargetModel) no se persisten; esto es
     * solo para entidades dibujadas, que cambian a mano y en poco volumen.
     */
    void setFeaturesDbFile(const QString &databasePath);
    //! Fuerza un guardado inmediato al fichero de persistencia (si hay). Util al
    //! cerrar la app para no perder lo que estuviera en el antirebote.
    void saveFeaturesNow();

    // --- Ficheros .geo ---------------------------------------------------
    /*!
     * \brief Carga un fichero .geo como UNA sola entidad multi-parte.
     *
     * Un .geo puede llevar varios trazados separados por "0.0,0.0" (las aguas
     * jurisdiccionales son un anillo; "corredores" son parejas de lineas; las
     * divisiones administrativas, decenas de polilineas). Todo el fichero entra
     * como una unica entidad: si todos los trazados cierran es un poligono
     * multi-parte, si no una polilinea multi-parte. La capa se crea si no
     * existe.
     *
     * \return el identificador de la entidad creada, o -1 si el fichero no se
     *         pudo leer o no tenia trazados validos (el motivo queda en
     *         \a error).
     */
    qint64 loadGeoAsLayer(const QString &path, const QString &layerId,
                          const QString &displayName = QString(),
                          const FeatureStyle &style = FeatureStyle(),
                          QString *error = nullptr);

    // --- Objetivos moviles (capa dinamica) -------------------------------
    /*!
     * \brief Da de alta o reemplaza un objetivo movil.
     *
     * Pensado para cientos de objetivos actualizandose en tiempo real. Van en
     * una capa propia que se repinta sola sin rehacer teselas ni entidades.
     * Si \a target.id es < 0 se asigna uno. Devuelve el identificador o -1.
     */
    qint64 addTarget(const MapTarget &target);

    //! Via rapida del tiempo real: nueva posicion (y rumbo) de un objetivo ya
    //! existente, anadiendola a su traza. NaN en el rumbo lo deja como estaba.
    bool updateTarget(qint64 id, const QGeoCoordinate &position,
                      double headingDeg = std::numeric_limits<double>::quiet_NaN());

    bool setTargetLabel(qint64 id, const QString &text);

    //! Cuelga (o reemplaza) un dato de dominio en un objetivo: AIS (mmsi, imo),
    //! ADS-B (callsign, squawk), telemetria de un UAV... La libreria los guarda
    //! y los devuelve tal cual, sin interpretarlos. false si el id no existe.
    bool setTargetAttribute(qint64 id, const QString &key, const QVariant &value);
    //! Valor de un atributo de un objetivo, o QVariant() invalido si no existe.
    QVariant targetAttribute(qint64 id, const QString &key) const;

    bool removeTarget(qint64 id);
    void clearTargets();

    std::optional<MapTarget> target(qint64 id) const;
    QVector<MapTarget> targets() const;
    int targetCount() const;

    //! Registra como dibuja la APP el simbolo de cada objetivo: recibe el
    //! MapTarget (con kind/attributes) y devuelve un TargetSymbol (icono +
    //! rotacion por rumbo + escala). Sin proveedor, la libreria usa un galon por
    //! defecto. El juego de iconos lo trae la app, asi la libreria sigue siendo
    //! agnostica del dominio (buques, aeronaves, UAVs).
    void setTargetSymbolProvider(TargetSymbolProvider provider);

    //! Nivel de detalle para escalar a MILES de objetivos: si en un repintado
    //! hay mas visibles que \a maxLabels no se dibuja ninguna etiqueta (se
    //! solaparian), e igual con las trazas y \a maxTrails. El simbolo se dibuja
    //! siempre. Por defecto 150 y 400. Sube los topes si tu equipo va sobrado.
    void setTargetDetailBudget(int maxLabels, int maxTrails);

    //! Longitud de la traza de cada objetivo: < 0 = toda (ilimitada),
    //! 0 = sin traza, > 0 = las ultimas N posiciones (p. ej. 10, 100, 500).
    void setTargetTrailLength(int maxPoints);
    //! Muestra u oculta la capa de objetivos entera.
    void setTargetsVisible(bool visible);

    //! Objetivo movil mas cercano a un punto de la pantalla (coords del widget)
    //! dentro de \a tolerancePx, o -1. Para "clic en un objetivo -> sus datos".
    qint64 targetAt(const QPoint &pixelPos, double tolerancePx = 14.0) const;
    //! Resalta un objetivo (-1 = ninguno): un halo y su etiqueta forzada. Sin
    //! herramienta activa, un clic sobre un objetivo ya lo selecciona y emite
    //! \ref targetClicked; esto permite hacerlo tambien desde codigo.
    void setSelectedTarget(qint64 id);
    qint64 selectedTarget() const;

    // --- Seleccion -------------------------------------------------------
    qint64 selectedFeature() const;
    void selectFeature(qint64 id);
    void clearSelection();

    //! Entidad bajo un punto de la pantalla, o -1.
    qint64 featureAt(const QPoint &pixel, double tolerancePx = 8.0) const;

    // --- Elevacion -------------------------------------------------------
    //! Cota del terreno (metros) en \a position leida del origen de elevacion
    //! configurado (carpeta `.hgt` o base de datos), o NaN si no hay dato (sin
    //! origen, tile ausente o hueco). Comprueba el resultado con std::isnan.
    double elevationAt(const QGeoCoordinate &position) const;
    //! Usa en caliente una CARPETA de ficheros `.hgt` como origen (vacia = quita).
    void setElevationDir(const QString &dir);
    //! Usa en caliente una BASE DE DATOS `.sqlitedb` como origen (vacia = quita).
    void setElevationDb(const QString &dbFile);

    //! Perfil de elevacion del terreno a lo largo de una ruta (polilinea): la
    //! cota en cada lugar, muestreada cada \c params.stepMeters (30 m por
    //! defecto), con distancia total y desniveles. Perfil vacio si no hay origen
    //! de elevacion o la ruta tiene menos de dos puntos validos.
    ElevationProfile elevationProfile(
        const QVector<QGeoCoordinate> &path,
        const ElevationProfileParams &params = ElevationProfileParams()) const;

    //! Visibilidad directa entre \a a y \a b con altura de antena en cada extremo
    //! (metros sobre el terreno). Corrige la curvatura+refraccion de la Tierra
    //! (radio efectivo 4/3 por defecto; ver \c LineOfSightParams). Indica si hay
    //! vision, la holgura minima y el punto critico. Resultado invalido
    //! (isValid()==false) si no hay origen de elevacion o falta la cota de un
    //! extremo.
    LineOfSightResult lineOfSight(
        const QGeoCoordinate &a, const QGeoCoordinate &b,
        double antennaA = 0.0, double antennaB = 0.0,
        const LineOfSightParams &params = LineOfSightParams()) const;

    //! Viewshed 360 grados desde \a origin: por azimut, el angulo de cierre del
    //! terreno (horizonte y picos) y hasta donde se ve un objetivo a
    //! \c ViewshedParams::targetHeight (zona de visibilidad). Corrige la
    //! curvatura 4/3 (configurable). Resultado invalido (isValid()==false) si no
    //! hay origen de elevacion o falta la cota del origen.
    //! \a progress (opcional) se invoca tras cada azimut con (rayos hechos,
    //! total); si devuelve false se cancela (resultado invalido). Para barras de
    //! progreso cancelables en alcances largos.
    Viewshed viewshed(
        const QGeoCoordinate &origin,
        const ViewshedParams &params = ViewshedParams(),
        const ViewshedProgress &progress = ViewshedProgress()) const;

    // --- Herramientas ----------------------------------------------------
    MapTool activeTool() const;
    void setActiveTool(MapTool tool);

    //! Capa donde van las entidades que se creen con el raton.
    void setActiveFeatureLayer(const QString &id);
    QString activeFeatureLayer() const;

    //! Estilo y tipo de dominio de las entidades que se creen a partir de ahora.
    void setDraftStyle(const FeatureStyle &style);
    void setDraftType(const QString &type);

    //! true mientras hay una geometria a medio trazar.
    bool isDrawing() const;
    //! Cierra el trazado en curso. Devuelve el identificador, o -1.
    qint64 finishDrawing();
    //! Descarta el trazado en curso.
    bool cancelDrawing();

    // --- Diagnostico -----------------------------------------------------
    //! Porcentaje de la pantalla resuelto con teselas propias (no ancestros).
    double exactCoverage() const;
    void setDebugGridVisible(bool visible);

    /*!
     * \brief Mancha de COBERTURA: que zonas de un zoom OBJETIVO hay ya en la BD.
     *
     * Dibuja una capa traslucida FIJA (visible aunque se mire a un zoom menor)
     * que agrega la cobertura del zoom objetivo a una rejilla gruesa y la colorea
     * por completitud (ambar = a medias, verde = llena). A diferencia de la
     * rejilla de depuracion, no depende del zoom actual de la vista.
     */
    void setCoverageVisible(bool on);
    bool isCoverageVisible() const;
    //! Zoom cuya cobertura se muestra. Recomputa si la mancha esta visible.
    void setCoverageZoom(int targetZoom);
    int coverageZoom() const;
    //! Vuelve a consultar la BD y redibuja la mancha (p.ej. tras una descarga).
    void refreshCoverage();

    /*!
     * \brief Relieve sombreado (hillshade) calculado EN VIVO del DEM local.
     *
     * Capa que sombrea el terreno segun un sol virtual (sin conexion: solo usa el
     * origen de elevacion). Se recalcula sola al desplazar/hacer zoom (con un
     * pequeno retardo). \c setHillshadeColored activa un tintado por altura
     * (hipsometrico) en vez del gris translucido. Sin origen de elevacion la capa
     * queda vacia.
     */
    void setHillshadeVisible(bool on);
    bool isHillshadeVisible() const;
    //! Sol: azimut (0=N, 90=E) y altura sobre el horizonte, en grados.
    void setHillshadeSun(double azimuthDeg, double altitudeDeg);
    //! Intensidad del sombreado en [0,1] (gris) u opacidad del tintado (color).
    void setHillshadeOpacity(double opacity);
    //! Exageracion vertical del relieve (1 = real; 2–3 resalta terreno suave).
    void setHillshadeExaggeration(double zFactor);
    //! true = tintado por altura (hipsometrico); false = gris sobre la base.
    void setHillshadeColored(bool on);
    //! Recalcula el relieve para la vista actual (lo hace solo al mover la vista).
    void refreshHillshade();

    /*!
     * \brief Geografico -> coordenadas de los ejes del QCustomPlot interno.
     *
     * IMPRESCINDIBLE para cualquier overlay dibujado con coordenadas de eje
     * (QCPCurve, QCPItemEllipse, QCPItemLine...). El eje X va en grados de
     * longitud, pero el Y NO va en grados de latitud: va en "grados de
     * Mercator", porque el eje de QCustomPlot es lineal y la proyeccion no lo
     * es. Pasarle la latitud directamente deforma el mapa: a 23 grados el
     * desfase es de 0.64 y a 60 de 15.5.
     *
     *     QCPItemEllipse *punto = new QCPItemEllipse(plot);
     *     const QPointF c = mapa->toAxisCoords(coordenada);
     *     punto->topLeft->setCoords(c.x() - r, c.y() + r);
     *     punto->bottomRight->setCoords(c.x() + r, c.y() - r);
     */
    QPointF toAxisCoords(const QGeoCoordinate &position) const;
    QGeoCoordinate fromAxisCoords(const QPointF &axisPoint) const;

    /*!
     * \brief QCustomPlot interno, para dibujar capas propias.
     *
     * Devuelve QWidget* a proposito: quien lo necesite hara el cast y
     * asumira la dependencia en SU codigo, sin imponersela a los demas.
     */
    QWidget *customPlot() const;

signals:
    void baseLayerChanged(const QString &id);
    void featureAdded(qint64 id);
    void featureUpdated(qint64 id);
    void featureRemoved(qint64 id);
    void featureSelected(qint64 id);      //!< -1 al deseleccionar
    //! Clic sobre un objetivo movil sin herramienta activa (ya queda resaltado).
    void targetClicked(qint64 id, const QGeoCoordinate &position);
    void featureLayersChanged();
    //! Emitida al crear una entidad con el raton.
    void featureCreated(qint64 id);
    void drawingCancelled();
    void zoomChanged(int zoom);
    void centerChanged(const QGeoCoordinate &center);
    void mouseMoved(const QGeoCoordinate &position);
    void clicked(const QGeoCoordinate &position, Qt::MouseButton button);
    void measurementFinished(const libmapa::Measurement &measurement);
    void areaSelected(const QGeoCoordinate &northWest,
                      const QGeoCoordinate &southEast);
    //! Poligono cerrado con la herramienta SelectPolygon.
    void polygonSelected(const QVector<QGeoCoordinate> &polygon);
    void pointPicked(const QGeoCoordinate &position);
    void errorOccurred(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;   //!< Firma correcta (F-18)

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace libmapa

#endif // LIBMAPA_MAPWIDGET_H_
