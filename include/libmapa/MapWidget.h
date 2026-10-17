#ifndef LIBMAPA_MAPWIDGET_H_
#define LIBMAPA_MAPWIDGET_H_

#include "libmapa/Coordinates.h"
#include "libmapa/DataPackage.h"
#include "libmapa/GeoFile.h"
#include "libmapa/VectorFormats.h"
#include "libmapa/Elevation.h"
#include "libmapa/MapFeature.h"
#include "libmapa/MapTarget.h"
#include "libmapa/TargetSymbol.h"
#include "libmapa/MapTypes.h"
#include "libmapa/Proximity.h"
#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QImage>
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

    //! Carpeta con ficheros de elevacion SRTM `.hgt` (nombres tipo `N19W077.hgt`) o,
    //! si no hay `.hgt`, con GeoTIFF (`.tif`/`.tiff`). Vacia = sin elevacion por
    //! carpeta. Opcional.
    QString elevationDir;

    //! Base de datos de elevacion (`.sqlitedb` generada por `dem_to_db`) o un
    //! fichero GeoTIFF (`.tif`/`.tiff`, en grados o Web Mercator). Si viene, TIENE
    //! PRIORIDAD sobre elevationDir. Vacia = no se usa. Opcional.
    QString elevationDbFile;

    //! Base de datos de ENTIDADES (puntos/lineas/poligonos dibujados). Si viene,
    //! el widget la carga al abrir y guarda SOLO lo que se dibuje/edite/borre
    //! (con antirebote). Vacia = sin persistencia automatica. Opcional.
    QString featuresDbFile;

    //! Guardado AUTOMATICO de entidades. Por defecto true (compatibilidad): si
    //! \a featuresDbFile viene vacio y se abre un paquete, se usa el fichero de
    //! entidades del paquete y se autoguarda ahi. Ponlo en \c false para que la
    //! app lleve un GUARDADO EXPLICITO (cargar/guardar a mano con
    //! loadFeaturesFrom/saveFeaturesTo): entonces NO se usa el fichero del
    //! paquete ni se autoguarda nada, aunque se abra un paquete.
    bool autosaveFeatures = true;

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

    //! Quita la marca que dejan SelectArea (el recuadro) o SelectPolygon (el
    //! contorno) tras emitir areaSelected / polygonSelected. Tambien se quitan
    //! solas al cambiar de herramienta.
    void clearAreaSelection();

    // --- Exportar la vista -----------------------------------------------
    /*!
     * \brief La vista actual como imagen, tal cual se ve.
     *
     * \a scale > 1 da mas pixeles por lado (lineas, textos y simbolos se
     * redibujan nitidos; las teselas se escalan). Imagen nula sin vista.
     */
    QImage renderImage(double scale = 1.0) const;

    /*!
     * \brief Guarda la vista actual en un fichero de imagen o PDF.
     *
     * El formato sale de la extension: .pdf, o cualquier formato de imagen de
     * Qt con escritura (.png, .jpg, .bmp, .tif...). En PDF las entidades, los
     * objetivos, la escala y la cuadricula van como VECTORES (se imprimen
     * nitidos a cualquier tamaño) y el pie indica la escala de impresion
     * (1:N) de la hoja. Titulo, pie, notas y atribucion: \ref MapExportOptions.
     * Escritura atomica: o queda el fichero completo, o no se toca.
     *
     * \return false con el motivo en \a error si no se pudo.
     */
    bool exportView(const QString &path,
                    const MapExportOptions &options = MapExportOptions(),
                    QString *error = nullptr);

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

    //! Fija el rango de zoom [minZoom, maxZoom] en que la capa se dibuja y se
    //! puede seleccionar (−1 = sin límite por ese lado). Fuera del rango la capa
    //! se oculta: pensado para capas de detalle (curvas de nivel) que estorban a
    //! vista general. En el paquete se declara con "minZoom"/"maxZoom" del overlay.
    bool setFeatureLayerZoomRange(const QString &id, int minZoom, int maxZoom);

    //! Marca una capa como TEMPORAL: se excluye del guardado (\a saveFeaturesTo),
    //! como las capas fijas del paquete. Para resultados de análisis o marcadores
    //! auxiliares que no deben persistir. \a transient=false la vuelve guardable.
    //! Una capa temporal tampoco entra en deshacer/rehacer: sus cambios no se
    //! apilan y un deshacer del usuario no la hace desaparecer ni reaparecer.
    void setFeatureLayerTransient(const QString &id, bool transient = true);

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

    /*!
     * \brief Carga las entidades de una BD vectorial (.sqlitedb con el esquema
     *        de VectorRepository) como una capa fija MÁS, sin reemplazar las
     *        entidades existentes (al revés que \a loadFeaturesFrom).
     *
     * Cada entidad conserva su estilo, etiqueta y atributos propios (p. ej. una
     * capa de curvas de nivel de \c dem_to_contours: índice gruesas+etiqueta, la
     * cota en \c atributos). Todas quedan bajo \a layerId. Pensada para capas de
     * solo lectura del paquete (overlays con fichero .sqlitedb); las entidades
     * siguen siendo seleccionables (para leer su cota), pero la capa no editable.
     *
     * \return el id de la primera entidad añadida, o -1 si la BD no abre o está
     *         vacía (el motivo queda en \a error).
     */
    qint64 loadFeaturesAsLayer(const QString &databasePath, const QString &layerId,
                               const QString &displayName = QString(),
                               QString *error = nullptr);

    /*!
     * \brief Importa un fichero GeoJSON, KML o GPX (segun su extension) como
     *        capa: cada entidad del fichero es una entidad del mapa.
     *
     * Conserva lo que traiga el fichero (nombre, descripcion, atributos y, en
     * GeoJSON/KML, el estilo; ver VectorFormats.h). La capa se crea si no existe
     * y las entidades se AÑADEN (no reemplazan lo que ya hubiera en ella).
     *
     * \return el id de la primera entidad añadida, o -1 si el fichero no se pudo
     *         leer o no tenia geometrias validas (el motivo queda en \a error).
     */
    qint64 importVectorFile(const QString &path, const QString &layerId,
                            const QString &displayName = QString(),
                            QString *error = nullptr);

    /*!
     * \brief Exporta entidades a GeoJSON, KML o GPX (segun la extension).
     *
     * Con \a layerId vacio exporta TODAS las entidades; si no, solo las de esa
     * capa. Para llevarlas a QGIS, Google Earth o un GPS. GPX no guarda
     * atributos ni estilo (sí nombre, descripcion y tipo).
     *
     * \return false si no hay nada que exportar o no se pudo escribir (el motivo
     *         queda en \a error).
     */
    bool exportVectorFile(const QString &path, const QString &layerId = QString(),
                          QString *error = nullptr);

    // --- Objetivos moviles (capa dinamica) -------------------------------
    /*!
     * \brief Da de alta o reemplaza un objetivo movil.
     *
     * Pensado para cientos de objetivos actualizandose en tiempo real. Van en
     * una capa propia que se repinta sola sin rehacer teselas ni entidades.
     * Si \a target.id es < 0 se asigna uno. Devuelve el identificador o -1.
     */
    //! \a time: hora del dato (p. ej. la del mensaje AIS); invalida = ahora.
    //! Es la hora del ORIGEN del historial si el objetivo es nuevo.
    qint64 addTarget(const MapTarget &target, const QDateTime &time = QDateTime());

    //! Via rapida del tiempo real: nueva posicion (y rumbo) de un objetivo ya
    //! existente, anadiendola a su traza y a su historial. NaN en el rumbo lo
    //! deja como estaba; \a time es la hora del dato (invalida = ahora).
    bool updateTarget(qint64 id, const QGeoCoordinate &position,
                      double headingDeg = std::numeric_limits<double>::quiet_NaN(),
                      const QDateTime &time = QDateTime());

    bool setTargetLabel(qint64 id, const QString &text);

    //! Desplazamiento de la ETIQUETA del objetivo respecto a su simbolo, en
    //! pixeles. Relativo: al moverse el objetivo la etiqueta lo sigue. Cuando no
    //! es nulo se traza una linea guia. false si el id no existe.
    bool setTargetLabelOffset(qint64 id, const QPointF &offsetPx);

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
    //! Es solo lo que se DIBUJA para todos; el historial completo va aparte.
    void setTargetTrailLength(int maxPoints);

    /*!
     * \brief Historial completo de un objetivo: todas sus posiciones con hora,
     *        desde que se vio por primera vez en el sistema hasta ahora.
     *
     * Se guarda mientras el objetivo exista (se pierde con \ref removeTarget o
     * \ref clearTargets: leelo antes si la app quiere archivarlo) y es
     * independiente de la traza dibujada (\ref setTargetTrailLength). El primer
     * punto es el ORIGEN. Al seleccionar un objetivo se dibuja entero, con el
     * origen marcado (\ref setSelectedTargetHistoryVisible).
     *
     * Memoria acotada: no se anotan puntos a menos de
     * \ref setTargetHistoryMinDistance metros del anterior (1 m por defecto) y,
     * pasado \ref setTargetHistoryLimit puntos (5000 por defecto), se aclaran
     * los intermedios — el origen y la ultima posicion nunca se pierden.
     */
    QVector<TargetTrackPoint> targetHistory(qint64 id) const;
    //! Origen de un objetivo: donde y cuando se vio por primera vez (invalido si
    //! no existe).
    TargetTrackPoint targetOrigin(qint64 id) const;
    void setTargetHistoryLimit(int maxPoints);
    void setTargetHistoryMinDistance(double meters);
    //! El objetivo seleccionado dibuja todo su historial desde el origen
    //! (activo por defecto).
    void setSelectedTargetHistoryVisible(bool on);
    /*!
     * \brief Exporta el historial de objetivos a GPX, con la hora de cada punto.
     *
     * Un track por objetivo (nombre = primera linea de su etiqueta, tipo =
     * su \c kind, y su id como extension libmapa:targetId), con el historial
     * completo desde el origen. Con \a ids vacio exporta todos. Se puede
     * reproducir en el tiempo en QGIS o GPXSee, o releer con parseGpxTracks.
     *
     * \return false si no hay ningun historial o no se pudo escribir (el motivo
     *         queda en \a error).
     */
    bool exportTargetHistory(const QString &path,
                             const QVector<qint64> &ids = QVector<qint64>(),
                             QString *error = nullptr);
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

    // --- Alertas de zona (objetivos que entran o salen de un area) --------
    /*!
     * \brief Vigila una entidad POLIGONAL como zona de alerta.
     *
     * Desde ese momento, cada vez que un objetivo movil entra o sale de ella se
     * emite \ref targetEnteredZone o \ref targetLeftZone. Que la zona sea
     * "prohibida", "de interes" o "de fondeo" lo decide la app (con el \c type
     * o los atributos de la entidad); la libreria solo hace la geometria.
     *
     * - Los objetivos que YA estan dentro al empezar a vigilar producen una
     *   entrada: asi la app se entera de lo que hay en la zona.
     * - "Dentro" es lo que se ve dentro en el mapa (lados rectos en la
     *   proyeccion del mapa). En una entidad multi-parte basta con una parte.
     * - Si la entidad cambia de forma (edicion, arrastre, deshacer), se
     *   reevalua y se emiten las entradas/salidas que provoque. Si se borra,
     *   se deja de vigilar sin emitir salidas.
     * - Borrar un objetivo (o \ref clearTargets) tampoco emite salidas: no se
     *   ha ido, ha dejado de existir. Una posicion invalida si lo saca.
     * - Tambien se vigilan las ENTIDADES PUNTO (marcadores que el operador pone
     *   o arrastra), con sus propias señales \ref featureEnteredZone /
     *   \ref featureLeftZone: los ids de entidad y de objetivo son espacios
     *   distintos. Cuenta su (primera) posicion; borrarla no es «salir».
     * - La vigilancia no se guarda con las entidades: la app la vuelve a pedir
     *   al cargar (p. ej. para todas las entidades de cierto \c type).
     *
     * Coste: cada actualizacion de un objetivo se compara con todas las zonas
     * vigiladas, con un descarte rapido por caja envolvente.
     *
     * \return false si no existe la entidad o no es un poligono.
     */
    bool watchZone(qint64 featureId);
    //! Deja de vigilar una zona (sin emitir salidas). false si no se vigilaba.
    bool unwatchZone(qint64 featureId);
    bool isZoneWatched(qint64 featureId) const;
    //! Ids de las entidades vigiladas como zona, ordenados.
    QVector<qint64> watchedZones() const;
    //! Objetivos que estan ahora dentro de una zona vigilada, ordenados.
    QVector<qint64> targetsInZone(qint64 featureId) const;
    //! Zonas vigiladas en las que esta ahora un objetivo, ordenadas.
    QVector<qint64> zonesOfTarget(qint64 targetId) const;
    //! Entidades PUNTO que estan ahora dentro de una zona vigilada, ordenadas.
    QVector<qint64> featuresInZone(qint64 zoneFeatureId) const;
    //! Zonas vigiladas en las que esta ahora una entidad punto, ordenadas.
    QVector<qint64> zonesOfFeature(qint64 featureId) const;
    /*!
     * \brief Consulta puntual: objetivos dentro de un poligono cualquiera.
     *
     * Para la zona que marca el usuario con SelectArea / SelectPolygon (o
     * cualquier otra), sin vigilarla ni crear entidad. Mismo criterio de
     * "dentro" que las alertas. Ids ordenados.
     */
    QVector<qint64> targetsInArea(const QVector<QGeoCoordinate> &polygon) const;
    //! Entidades con algun vertice dentro del poligono, ordenadas por id.
    QVector<qint64> featuresInArea(const QVector<QGeoCoordinate> &polygon) const;
    //! Resalte en el mapa (activo por defecto): una zona vigilada con algo
    //! dentro se pinta con borde ancho y velo del color de alerta, y cada
    //! entidad punto dentro de una zona lleva un aro. Los objetivos moviles los
    //! marca la app con su proveedor de simbolos (\ref zonesOfTarget).
    void setZoneAlertHighlight(bool on);
    bool isZoneAlertHighlight() const;
    void setZoneAlertColor(const QColor &color);

    // --- Alertas de proximidad (CPA/TCPA) ----------------------------------
    /*!
     * \brief Avisa de los pares de objetivos que van a pasar demasiado cerca.
     *
     * Un par entra en alerta si ya estan a menos de \a distanceMeters, o si
     * su punto de maximo acercamiento (CPA) cae por debajo de esa distancia en
     * los proximos \a horizonSeconds (TCPA). Se emite
     * \ref proximityAlertRaised al entrar y \ref proximityAlertCleared al
     * salir, y el mapa dibuja por donde se cruzan (\ref setProximityLinesVisible).
     *
     * - Se reevalua cada vez que se mueve cualquiera de los dos.
     * - Velocidad y rumbo: los que dé la app con \ref setTargetVelocity (p. ej.
     *   SOG/COG del AIS) o, si no, los estimados por la traza de los ultimos
     *   30 s. La libreria no interpreta MapTarget::speed (sus unidades son
     *   cosa de la app).
     * - Si las ultimas posiciones son de horas distintas, se compara llevando
     *   la mas antigua a la hora de la otra por estima.
     * - Borrar un objetivo no emite fin de alerta (ha dejado de existir).
     *
     * \a distanceMeters <= 0 apaga las alertas (las activas terminan con su
     * señal) y deja de calcular: sin alertas, coste cero. Cada movimiento cuesta
     * O(objetivos), con un descarte rapido de los que no pueden acercarse.
     */
    void setProximityAlert(double distanceMeters, double horizonSeconds = 600.0);
    double proximityAlertDistance() const;
    double proximityAlertHorizon() const;
    //! Pares en alerta ahora, ordenados.
    QVector<ProximityAlert> proximityAlerts() const;
    //! CPA/TCPA de dos objetivos cualesquiera, ahora (este o no activa la
    //! alerta). Invalido si alguno no existe.
    CpaResult targetCpa(qint64 targetA, qint64 targetB) const;
    /*!
     * \brief Velocidad (m/s) y rumbo (grados) de un objetivo, dados por la app.
     *
     * Para las alertas de proximidad: mandan sobre la estima por la traza.
     * Con \a speedMetersPerSecond NaN se vuelve a estimar. Se olvida al borrar
     * el objetivo. false si no existe. (Nudos: knotsToMetersPerSecond.)
     */
    bool setTargetVelocity(qint64 id, double speedMetersPerSecond, double courseDeg);
    // --- Agrupacion de objetivos ------------------------------------------
    /*!
     * \brief Agrupa los objetivos cercanos en pantalla en un circulo con su
     *        numero (ver \ref TargetClustering). Desactivada por defecto.
     *
     * Solo cambia como se dibujan: los objetivos siguen existiendo y contando
     * para alertas y filtros. Un objetivo dentro de un grupo no se puede
     * pinchar suelto (\ref targetAt); el grupo emite \ref targetClusterClicked.
     */
    void setTargetClustering(const TargetClustering &clustering);
    TargetClustering targetClustering() const;
    //! Ids del grupo dibujado bajo \a pixelPos (coordenadas del widget), o vacio.
    QVector<qint64> targetClusterAt(const QPoint &pixelPos) const;

    // --- Filtro de objetivos ----------------------------------------------
    /*!
     * \brief Muestra solo los objetivos que cumplen \a filter (por clase,
     *        velocidad, zona o un criterio de la app); el resto se oculta o
     *        se atenua. Ver \ref TargetFilter.
     *
     * Se reevalua cada objetivo al moverse o al cambiar su velocidad, y todos
     * al cambiar el filtro. Un objetivo oculto no se puede pinchar
     * (\ref targetAt), pero sigue existiendo: cuenta para las alertas de zona
     * y de proximidad, y para \ref targets. Un filtro vacio quita el filtro.
     */
    void setTargetFilter(const TargetFilter &filter);
    void clearTargetFilter();
    bool hasTargetFilter() const;
    //! ¿Pasa el objetivo el filtro actual? true sin filtro; false si no existe.
    bool targetPassesFilter(qint64 id) const;
    //! Objetivos que NO pasan el filtro (ocultos o atenuados), ordenados.
    QVector<qint64> filteredOutTargets() const;

    //! Lineas de maximo acercamiento de los pares en alerta (activo por defecto).
    void setProximityLinesVisible(bool on);
    bool isProximityLinesVisible() const;
    void setProximityColor(const QColor &color);

    // --- Seleccion -------------------------------------------------------
    qint64 selectedFeature() const;
    void selectFeature(qint64 id);
    void clearSelection();

    //! Entidad bajo un punto de la pantalla, o -1.
    qint64 featureAt(const QPoint &pixel, double tolerancePx = 8.0) const;
    /*!
     * \brief Todo lo que hay bajo un pixel: objetivo suelto, grupo de
     *        objetivos, entidad, anillos o marcacion, y la coordenada.
     *
     * Lo mismo que \ref targetAt, \ref targetClusterAt, \ref featureAt y el
     * hit-test de anillos/marcaciones en una sola llamada. Ver \ref MapHit.
     */
    MapHit hitTest(const QPoint &pixel) const;

    //! Entidad cuyo ROTULO cae bajo el pixel, o -1. Permite seleccionar/arrastrar
    //! la etiqueta por separado de la geometria.
    qint64 featureLabelAt(const QPoint &pixel) const;
    //! Objetivo cuyo ROTULO dibujado cae bajo el pixel, o -1.
    qint64 targetLabelAt(const QPoint &pixel) const;

    //! Fija el desplazamiento de la etiqueta de una entidad (px) y lo persiste.
    //! false si el id no existe.
    bool setFeatureLabelOffset(qint64 id, const QPointF &offsetPx);

    // --- Elevacion -------------------------------------------------------
    //! Cota del terreno (metros) en \a position leida del origen de elevacion
    //! configurado (carpeta `.hgt` o base de datos), o NaN si no hay dato (sin
    //! origen, tile ausente o hueco). Comprueba el resultado con std::isnan.
    double elevationAt(const QGeoCoordinate &position) const;
    //! Usa en caliente una CARPETA de ficheros `.hgt` o GeoTIFF como origen
    //! (vacia = quita). El formato lo reconoce la libreria por lo que haya dentro.
    void setElevationDir(const QString &dir);
    //! Usa en caliente una BASE DE DATOS `.sqlitedb` o un fichero GeoTIFF como
    //! origen (vacia = quita).
    void setElevationDb(const QString &dbFile);
    //! Que hay cargado como origen de elevacion, en una linea para mostrarla al
    //! usuario (p. ej. «42 GeoTIFF, lat 19.0..24.0, lon -85.0..-74.0»). Vacia si no
    //! hay elevacion utilizable: sin origen, o una carpeta sin `.hgt` ni GeoTIFF.
    QString elevationDescription() const;
    //! Problemas de la ultima carga de elevacion (p. ej. GeoTIFF ilegibles, o una
    //! carpeta sin datos), uno por elemento. Vacia si todo fue bien.
    QStringList elevationWarnings() const;

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

    //! Tope de vertices del trazado punto a punto (−1 = sin límite). Al llegar al
    //! tope se ignoran los clics; el límite queda en la entidad (atributo
    //! \c max_vertices) para que la edicion posterior también lo respete. Para un
    //! «area objetivo» de ≤10 puntos: setDraftMaxVertices(10).
    void setDraftMaxVertices(int n);

    //! true mientras hay una geometria a medio trazar.
    bool isDrawing() const;
    //! Cierra el trazado en curso. Devuelve el identificador, o -1.
    qint64 finishDrawing();
    //! Descarta el trazado en curso.
    bool cancelDrawing();

    // --- Escala grafica y cuadricula de coordenadas -----------------------
    /*!
     * \brief Escala grafica en la esquina inferior izquierda (oculta por defecto).
     *
     * Barra de longitud redonda (1-2-5 x 10^n) medida en el CENTRO de la vista:
     * en Mercator la escala cambia con la latitud, y el centro es donde se mira.
     */
    void setScaleBarVisible(bool on);
    bool isScaleBarVisible() const;
    //! Metros/kilometros (por defecto) o millas nauticas.
    void setScaleBarUnits(ScaleUnits units);
    ScaleUnits scaleBarUnits() const;

    // --- Anillos de distancia y marcaciones ------------------------------
    /*!
     * \brief Pone anillos de distancia (VRM) alrededor de un punto o de un
     *        objetivo (al que siguen al moverse). Ver \ref RangeRings.
     *
     * Rotulados en las unidades de la escala (\ref setScaleBarUnits). Si el
     * objetivo deja de existir, los anillos no se dibujan (siguen guardados).
     * \return su id (comun con las marcaciones), o -1 sin vista.
     */
    qint64 addRangeRings(const RangeRings &rings);
    bool updateRangeRings(qint64 id, const RangeRings &rings);
    /*!
     * \brief Pone una linea de marcacion (EBL) entre puntos u objetivos, o desde
     *        uno con un rumbo y una longitud. Ver \ref BearingLine.
     *
     * Muestra el rumbo verdadero y la distancia ("045° · 5.2 NM"), que se
     * actualizan solos si los extremos son objetivos que se mueven.
     * \return su id, o -1 si no tiene longitud (el mismo objetivo o el mismo
     * punto en los dos extremos) o no hay vista. Si mas tarde dos objetivos
     * coinciden, la linea simplemente no se dibuja mientras tanto.
     */
    qint64 addBearingLine(const BearingLine &line);
    bool updateBearingLine(qint64 id, const BearingLine &line);
    //! Quita unos anillos o una marcacion. false si el id no existe.
    bool removeRangeMark(qint64 id);
    void clearRangeMarks();
    //! Ids de anillos y marcaciones, ordenados.
    QVector<qint64> rangeMarks() const;
    //! Los anillos con ese id (vacio si no existen o es una marcacion).
    std::optional<RangeRings> rangeRings(qint64 id) const;
    //! La marcacion con ese id (vacio si no existe o son anillos).
    std::optional<BearingLine> bearingLine(qint64 id) const;
    /*!
     * \brief Separacion (m) con la que se dibujan ahora unos anillos.
     *
     * La fija o, si son automaticos (\c spacingMeters = 0), la que toca al zoom
     * actual: cambia al acercar o alejar. 0 si no existen o no se pueden
     * resolver.
     */
    double rangeRingsSpacing(qint64 id) const;
    //! Rumbo verdadero (grados) y distancia (m) actuales de una marcacion.
    //! false si no existe o algun extremo no se puede resolver ahora.
    bool bearingLineMeasure(qint64 id, double *bearingDeg, double *distanceMeters) const;

    /*!
     * \brief Que lineas marcan su primer y su ultimo punto.
     *
     * Inicio con un circulo verde y fin con un cuadrado rojo. La linea
     * SELECCIONADA lleva ademas los rotulos "inicio" y "fin" con su hora, si
     * la tiene (atributos \c time_start / \c time_end, que pone la lectura de
     * GPX). Por defecto \ref RouteEndpoints::Routes: siempre las rutas y
     * tracks leidos de GPX, y cualquier linea al seleccionarla.
     */
    void setRouteEndpoints(RouteEndpoints mode);
    RouteEndpoints routeEndpoints() const;
    //! Resolucion del mapa en el centro de la vista, en metros por pixel
    //! (0 si aun no hay vista). Es el dato con el que se dibuja la escala.
    double metersPerPixel() const;

    /*!
     * \brief Cuadricula de meridianos y paralelos con su rotulo (oculta por defecto).
     *
     * La separacion se elige sola segun el zoom, en pasos de carta (grados,
     * 30', 10', 1', 30"...), y cada linea se rotula en el borde con esa
     * precision (23°30'N). Va encima de las entidades y debajo de los objetivos.
     */
    void setGraticuleVisible(bool on);
    bool isGraticuleVisible() const;
    //! Color de lineas y rotulos (p. ej. claro sobre la capa satelital).
    void setGraticuleColor(const QColor &color);
    QColor graticuleColor() const;
    //! Cuadricula geografica (por defecto) o UTM: lineas de este/norte
    //! constantes de cada zona visible, cada 100 km .. 100 m segun el zoom,
    //! con los limites de zona marcados. Las conversiones, en Coordinates.h.
    void setGraticuleMode(GraticuleMode mode);
    GraticuleMode graticuleMode() const;

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
    //! Clic (sin herramienta) sobre un GRUPO de objetivos (\ref setTargetClustering):
    //! los ids que reune. Lo habitual es ampliar para separarlos.
    void targetClusterClicked(const QVector<qint64> &ids, const QGeoCoordinate &position);
    //! Un objetivo movil ha entrado en una zona vigilada (ver \ref watchZone).
    void targetEnteredZone(qint64 targetId, qint64 zoneFeatureId);
    //! Un objetivo movil ha salido de una zona vigilada (ver \ref watchZone).
    void targetLeftZone(qint64 targetId, qint64 zoneFeatureId);
    //! Una entidad PUNTO ha entrado en una zona vigilada (al crearla, moverla,
    //! arrastrarla o deshacer).
    void featureEnteredZone(qint64 featureId, qint64 zoneFeatureId);
    //! Una entidad PUNTO ha salido de una zona vigilada.
    void featureLeftZone(qint64 featureId, qint64 zoneFeatureId);
    //! Dos objetivos van a pasar (o ya estan) a menos de la distancia de alerta
    //! (\ref setProximityAlert). \a targetA < \a targetB. \a tcpaSeconds
    //! negativo: ya estan dentro y se alejan.
    void proximityAlertRaised(qint64 targetA, qint64 targetB, double cpaMeters,
                              double tcpaSeconds);
    //! El par deja de estar en alerta de proximidad.
    void proximityAlertCleared(qint64 targetA, qint64 targetB);
    void featureLayersChanged();
    //! Emitida al crear una entidad con el raton.
    void featureCreated(qint64 id);
    //! Arrastre (herramienta Navegar) de una entidad marcada \c draggable: al
    //! empezar, en cada movimiento (coordenada bajo el cursor) y al soltar. Para
    //! marcadores que el operador reposiciona (p. ej. los puntos A/B).
    void featureDragStarted(qint64 id);
    void featureDragMovedTo(qint64 id, const QGeoCoordinate &position);
    void featureDragFinished(qint64 id);
    void drawingCancelled();
    void zoomChanged(int zoom);
    void centerChanged(const QGeoCoordinate &center);
    void mouseMoved(const QGeoCoordinate &position);
    //! Clic (pulsar y soltar SIN arrastrar) en un punto del mapa. Tras un arrastre
    //! del mapa no se emite.
    void clicked(const QGeoCoordinate &position, Qt::MouseButton button);
    /*!
     * \brief Clic derecho sobre el mapa (no mientras se dibuja, que lo cierra).
     *
     * \a hit dice que hay debajo (\ref hitTest) y \a globalPos es donde abrir
     * el menu (QMenu::exec). La libreria no muestra menu propio: la app pone
     * las acciones que tengan sentido para un objetivo, una entidad, etc.
     */
    void contextMenuRequested(const libmapa::MapHit &hit, const QPoint &globalPos);
    /*!
     * \brief Clic izquierdo SIN arrastrar sobre un objetivo, una entidad o una
     *        marca (anillos, marcacion) con la herramienta Navegar.
     *
     * Antes de emitirse el elemento ya esta seleccionado. La app suele mostrar
     * una tarjeta con sus datos y acciones rapidas (frente al menu completo del
     * clic derecho). \a hit dice que hay debajo y \a globalPos donde abrirla.
     * Pulsar y arrastrar mueve el mapa y no emite esto.
     */
    void elementClicked(const libmapa::MapHit &hit, const QPoint &globalPos);
    //! La captura de un punto (\ref MapTool::PickPoint) se cancelo con Esc o con el
    //! clic derecho; la herramienta ya ha vuelto a Navegar.
    void pickCancelled();
    //! Una accion del raton o del teclado se rechazo (capa bloqueada, vertices
    //! minimos...); \a reason lo explica para mostrarlo al usuario.
    void interactionBlocked(const QString &reason);
    void measurementFinished(const libmapa::Measurement &measurement);
    void areaSelected(const QGeoCoordinate &northWest,
                      const QGeoCoordinate &southEast);
    //! Poligono cerrado con la herramienta SelectPolygon.
    void polygonSelected(const QVector<QGeoCoordinate> &polygon);
    /*!
     * \brief Poligono cerrado con la herramienta MeasureArea, con su superficie
     *        y su perimetro.
     *
     * Mientras se marca, el mapa ya rotula la medida en vivo junto al cursor; al
     * cerrar, el contorno se queda con su medida en el centro hasta cambiar de
     * herramienta o \ref clearAreaSelection. Un clic mas empieza otra medida.
     */
    void areaMeasured(const libmapa::AreaMeasurement &measurement);
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
