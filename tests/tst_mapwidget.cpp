#include "GeoTiffWriter.h"
#include "SyntheticTileDb.h"

#include "libmapa/MapWidget.h"
#include "libmapa/TrackPlayer.h"
#include "libmapa/MapFeature.h"
#include "db/SqliteConnectionPool.h"
#include "db/VectorRepository.h"
#include "widget/MapView.h"
#include "widget/OverlayModel.h"
#include "geo/TileMatrix.h"

#include <climits>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWheelEvent>

//! El constructor de QMouseEvent con QPointF quedo obsoleto en Qt 6.
static QMouseEvent mouseEvent(QEvent::Type tipo, const QPoint &pos,
                              Qt::MouseButton boton, Qt::MouseButtons botones)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QMouseEvent(tipo, QPointF(pos), QPointF(pos), boton, botones,
                       Qt::NoModifier);
#else
    return QMouseEvent(tipo, pos, boton, botones, Qt::NoModifier);
#endif
}
#include <cmath>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QSet>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVariant>
#include <QtTest>

using namespace libmapa;
using namespace libmapa::test;

class TstMapWidget : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void buildsFromDatasetsJson();
    void hybridOverlayOverAnyBase();
    void searchesPointsOfInterest();
    void findsARoute();
    void searchesStreets();
    void importsPointsOfInterestAsFeatures();
    void reportsErrorOnMissingJson();

    void listsAvailableLayers();

    //! El requisito central: alternar OSM <-> Satelital en caliente.
    void switchesBaseLayerInPlace();

    //! Formatos de entrada estandar: un MBTiles se dibuja como una capa mas, y la
    //! elevacion puede venir de un GeoTIFF (fichero o carpeta).
    void mbtilesLayerRendersInTheWidget();
    void geotiffElevationThroughTheWidget();
    void placeSearchThroughTheWidget();
    void addsBaseLayersAtRuntime();

    void navigationKeepsCenter();
    void zoomIsClampedToRecommendedRange();
    void fitBoundsFramesTheArea();
    //! La API de hillshade conmuta y acepta parametros sin colgar (sin DEM: capa vacia).
    void hillshadeApiTogglesSafely();

    void resizeDoesNotLoseTheView();

    /*! Los tres fallos de geometria que los tests anteriores NO detectaban,
     *  porque medían coherencia interna y no el resultado del dibujo. */
    void viewHasRealSizeWithoutBeingShown();
    void drawsTilesInTheRightPlace();

    /*! Regresion del cuelgue: al zoom 3 la pantalla abarca ~190 grados, asi
     *  que un arrastre corto cruza +-180. QGeoCoordinate se marca invalida
     *  ahi y devuelve NaN, que colgaba al generador de marcas de QCP. */
    void draggingAtLowZoomNeverProducesNaN();
    void viewNeverLeavesTheWorld();

    /*! Cada capa tiene su propia cache: la 10/285/447 de OSM y la de la
     *  satelital son teselas distintas. */
    void eachLayerHasItsOwnCache();

    /*! Un clic debe marcar EXACTAMENTE donde se hizo clic. */
    void pixelRoundTripIsExact();

    /*! El punto se ancla al PULSAR, no al soltar: entre una cosa y otra el
     *  raton se mueve unos pixeles y la marca caia mas abajo del clic. */
    void toolsAnchorOnPressNotOnRelease();

    // --- Herramientas interactivas de entidades --------------------------
    void drawsAPointWithOneClick();
    void drawsAPolygonClickByClick();
    void escapeCancelsTheDrawing();
    void backspaceUndoesTheLastVertex();
    void selectsAndDragsAVertex();
    void movesAWholeFeature();
    void deleteRemovesTheSelectedFeature();
    void switchingToolDiscardsTheDraft();

    // --- Deshacer y persistencia -----------------------------------------
    void undoAndRedoRestoreTheModel();
    void draggingIsOneUndoStep();
    void savesAndLoadsFeatures();
    void loadsFeaturesDbAsFixedLayer();
    void viewportCullKeepsCrossingFeatures();
    void persistSkipsTransientLayers();
    void iconAnchorHitTestCoversPin();
    //! La etiqueta desplazada se puede localizar por su caja (featureLabelAt) y
    //! el offset se fija y persiste a traves de la fachada.
    void movableLabelHitTestAndOffset();
    //! Los rotulos de entidades se pintan POR ENCIMA de las marcaciones: una linea
    //! de marcacion ya no tapa el nombre de una zona.
    void featureLabelsDrawAboveRangeMarks();
    //! El rotulo de una marcacion se puede localizar, desplazar (con linea guia) y
    //! arrastrar con el raton.
    void bearingLabelMovesWithMouse();
    //! Un «area objetivo» (polígono con labelAnchorVertex=0) cuelga la etiqueta
    //! del PRIMER vértice, no del centro.
    void areaLabelHangsFromFirstVertex();

    // --- Fase 7: .geo y objetivos moviles --------------------------------
    void loadsGeoFileAsPolygonLayer();
    void loadsGeoWithSeveralPolylines();
    void savesAndLoadsMultiPartFeature();
    void drawsManyMovingTargets();
    //! El proveedor de simbolos de la app se invoca al dibujar los objetivos.
    void usesTargetSymbolProvider();
    //! Miles de objetivos: el nivel de detalle apaga etiquetas/trazas a densidad
    //! alta y el declutter evita el amontonamiento; mide el tiempo de render.
    void scalesToThousandsOfTargets();
    //! targetAt encuentra el objetivo bajo el pixel; un clic lo selecciona y
    //! emite targetClicked.
    void selectsTargetByClick();
    //! El historial guarda el recorrido completo y el seleccionado lo dibuja
    //! desde el origen, aunque la traza dibujada para todos sea corta.
    void selectedTargetShowsFullHistory();
    //! Alertas de zona: entradas/salidas de objetivos en un poligono vigilado,
    //! siguiendo tambien las ediciones y el deshacer de la zona.
    void zoneAlertsFollowTargetsAndZoneEdits();
    //! Alertas de zona para ENTIDADES PUNTO (el «Objetivo» del demo dentro de
    //! un «Área objetivo»): alta, arrastre/edicion, deshacer y borrado.
    void zoneAlertsCoverPointFeatures();
    //! Escala grafica y cuadricula: ocultas por defecto, se dibujan al
    //! encenderlas y la resolucion sigue al zoom.
    void scaleBarAndGraticuleDraw();
    //! Importar GeoJSON como capa y exportarla a KML/GPX/GeoJSON ida y vuelta.
    void importsAndExportsVectorFiles();
    //! Historial de objetivos a GPX con horas, releible con parseGpxTracks.
    void exportsTargetHistoryToGpx();
    //! Exportar la vista: imagen (con y sin titulo/pie, a escala) y PDF.
    void exportsViewToImageAndPdf();
    //! Que hay dentro de una zona marcada, y quitar la marca de la seleccion.
    void areaQueriesFindTargetsAndFeatures();
    //! Alertas CPA/TCPA: dos objetivos en rumbo de colision, con velocidad dada
    //! por la app o estimada por la traza; señales, lineas y apagado.
    void proximityAlertsWarnOfCloseApproach();
    //! Anillos de distancia que siguen a un objetivo y marcaciones entre
    //! objetivos que se recalculan al moverse; quitar y limpiar.
    void rangeRingsAndBearingLinesFollowTargets();
    //! Un historial GPX importado marca su inicio y fin; la linea seleccionada
    //! los rotula. Los modos de setRouteEndpoints cambian lo que se ve.
    void routesShowStartAndEnd();
    //! Reproduccion de un historial: los objetivos aparecen, se mueven y
    //! desaparecen con las horas del fichero; ir atras rehace su historial.
    void trackPlayerReplaysHistory();
    //! Herramienta MeasureArea: rotulo en vivo, areaMeasured al cerrar con la
    //! superficie y el perimetro del poligono, y limpieza al cambiar de herramienta.
    void measureAreaToolMeasuresPolygon();
    //! Importar un fichero es UNA operacion de deshacer (capa incluida), y no
    //! cuesta O(n^2) aunque traiga cientos de entidades.
    void importIsOneUndoStep();
    //! Filtro de objetivos: clase, velocidad, zona y criterio de la app;
    //! ocultar (no se pueden pinchar) o atenuar; se reevalua al moverse.
    void targetFilterHidesOrDims();
    //! Agrupacion: un monton de objetivos es un grupo con su numero, que se
    //! pincha como grupo; el seleccionado queda suelto; maxZoom la limita.
    void targetClusteringGroupsCrowds();

    // --- Interaccion con el raton y el teclado ---------------------------
    //! Rueda: los giros finos (touchpad) se acumulan; 120 en total = un nivel.
    void wheelAccumulatesFineDeltas();
    //! Navegar: agarrar un objetivo y arrastrar mueve el mapa; pulsar y soltar
    //! lo selecciona al soltar; un clic en vacio y Esc deseleccionan.
    void navigateGesturesAreDecidedOnRelease();
    //! Doble clic: acerca sobre el vacio y no pierde el segundo pulsado (un clic
    //! seguido de un clic-arrastre rapido mueve el mapa).
    void doubleClickZoomsAndNeverLosesTheDrag();
    //! `clicked` no se emite al terminar un arrastre.
    void clickedIsNotEmittedAfterDragging();
    //! Editar: un clic con temblor no mueve nada, un arrastre real es un solo
    //! deshacer; Ctrl+clic quita un vertice; las capas bloqueadas se respetan.
    void editNeedsARealDragAndRespectsLockedLayers();
    //! Clic derecho y Esc cancelan lo que la herramienta tenga a medias; el clic
    //! derecho sin nada pendiente abre el menu.
    void rightClickAndEscCancelPendingOperations();
    //! Boton central, flechas y +/- mueven la vista con cualquier herramienta.
    void middleButtonAndKeysMoveTheView();
    //! Cursor de mano sobre lo que se puede agarrar; menu contextual con el teclado.
    void hoverCursorAndKeyboardContextMenu();
    //! elementClicked: con un clic sobre un objetivo o una entidad, no al arrastrar.
    void elementClickedOnlyWithoutDragging();
    //! El indice por capa del modelo sigue el orden y los cambios de capa.
    void layerIndexStaysConsistent();
    //! hitTest dice que hay bajo un pixel (objetivo, entidad, anillos,
    //! marcacion, mapa), y el clic derecho lo emite en contextMenuRequested.
    void hitTestAndContextMenu();

    /*! El item debe quedar EXACTAMENTE bajo el cursor. */
    void toolsLandExactlyUnderTheCursor();

    /*! El eje Y va en grados de Mercator: colocar items con la latitud a
     *  secas los situa por debajo del cursor. */
    void toolItemsLandExactlyUnderTheCursor();
    void measureToolPlacesLineAtClickedPixels();

    /*! Los tres fallos que aparecieron probando demo.exe. */
    void doesNotHangDraggingAtLowZoom();
    void tilesAreSquareAtLowZoom();
    void layersDoNotShareCachedTiles();

    //! Una sola capa de teselas, no un item por tesela. (F-15)
    void usesASingleTileLayer();

    void toolChangesCursorAndState();

    /*! Los items de las herramientas deben quedar EXACTAMENTE bajo el cursor.
     *  El eje Y va en grados de Mercator, no de latitud: pasarle la latitud
     *  los colocaba por debajo del clic. */
    void toolItemsLandUnderTheCursor();

    //! La cabecera publica no debe arrastrar QCustomPlot.
    void publicHeaderHasNoQCustomPlot();

    // --- Persistencia automatica de entidades ----------------------------
    //! Con featuresDbFile, lo que se anade se guarda (saveFeaturesNow).
    void autoPersistsFeatures();
    //! Al abrir con featuresDbFile existente, se recargan las entidades.
    void reloadsFeaturesOnOpen();
    //! El antirebote dispara el guardado solo tras un cambio (sin saveFeaturesNow).
    void debouncedAutosaveTriggers();

    // --- Paquete de datos (mapa.json) ------------------------------------
    //! Solo con dataDir: capas base, arranque, capa fija bloqueada y entidades;
    //! la capa fija no se guarda ni se duplica al reabrir.
    void opensDataPackageWithOneSetting();
    //! "visible" y "selectable" del overlay en mapa.json los activan; por defecto no.
    void packageOverlaysCanBeMadeVisibleAndSelectable();
    //! Una capa no seleccionable no roba el clic, pero featureAt la sigue viendo.
    void unselectableLayerDoesNotStealTheClick();
    void autosaveFeaturesFalseSkipsPackagePersistence();
    //! Lo rellenado a mano en MapConfig gana sobre el paquete.
    void explicitConfigWinsOverPackage();
    //! Paquete inexistente: no arranca y el error dice que falta mapa.json.
    void reportsMissingDataPackage();
    //! Paquete incompleto: arranca con lo que hay y dataWarnings() dice que falta.
    void reportsDataWarningsButStillOpens();

private:
    //! Monta un paquete en 'carpeta' sobre las BD sinteticas del test, con una
    //! capa fija .geo y la BD de entidades en 'entidades' (ruta absoluta).
    QString makePackage(const QString &carpeta, const QString &entidades);

    QString m_jsonPath;
    QTemporaryDir m_dir;
};

void TstMapWidget::initTestCase()
{
    QVERIFY(m_dir.isValid());

    SyntheticSpec mundo;
    mundo.path = m_dir.filePath(QStringLiteral("osm.sqlitedb"));
    mundo.minLogicalZ = 3;
    mundo.maxLogicalZ = 4;
    mundo.worldCoverage = true;
    QVERIFY(buildSyntheticDb(mundo) > 0);

    SyntheticSpec osm;
    osm.path = mundo.path;
    osm.minLogicalZ = 8;
    osm.maxLogicalZ = 12;
    osm.createTable = false;
    QVERIFY(buildSyntheticDb(osm) > 0);

    SyntheticSpec mundoSat;
    mundoSat.path = m_dir.filePath(QStringLiteral("sat.sqlitedb"));
    mundoSat.minLogicalZ = 3;
    mundoSat.maxLogicalZ = 4;
    mundoSat.worldCoverage = true;
    mundoSat.colorSeed = 7;
    mundoSat.invertedZ = true;
    mundoSat.zOffsetForInverted = 17;
    QVERIFY(buildSyntheticDb(mundoSat) > 0);

    SyntheticSpec sat;
    sat.path = mundoSat.path;
    sat.minLogicalZ = 8;
    sat.maxLogicalZ = 12;
    sat.colorSeed = 7;          // teselas distinguibles de las de OSM
    sat.invertedZ = true;
    sat.zOffsetForInverted = 17;
    sat.createTable = false;
    QVERIFY(buildSyntheticDb(sat) > 0);

    TileDataset dOsm;
    dOsm.id = QStringLiteral("osm");
    dOsm.displayName = QStringLiteral("Open Street Map");
    dOsm.filePath = mundo.path;
    dOsm.minZoom = 3; dOsm.maxZoom = 12; dOsm.recommendedMaxZoom = 12;
    dOsm.baseZoom = 3; dOsm.typicalFill = 1.0;

    TileDataset dSat;
    dSat.id = QStringLiteral("satelital");
    dSat.displayName = QStringLiteral("Satelital");
    dSat.filePath = mundoSat.path;
    dSat.zFactor = -1; dSat.zOffset = 17;
    dSat.minZoom = 3; dSat.maxZoom = 12; dSat.recommendedMaxZoom = 11;
    dSat.baseZoom = 3; dSat.typicalFill = 0.32;

    QJsonArray arr;
    arr.append(dOsm.toJson());
    arr.append(dSat.toJson());
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("datasets")] = arr;

    m_jsonPath = m_dir.filePath(QStringLiteral("datasets.json"));
    QFile f(m_jsonPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(root).toJson());
}

void TstMapWidget::cleanupTestCase()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

static MapConfig baseConfig(const QString &json)
{
    MapConfig cfg;
    cfg.datasetsFile = json;
    cfg.initialCenter = QGeoCoordinate(22.0, -79.5);
    cfg.initialZoom = 10;
    cfg.cacheMiB = 32;
    cfg.debounceMs = 0;
    return cfg;
}

void TstMapWidget::buildsFromDatasetsJson()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    QVERIFY(w.lastError().isEmpty());
    QCOMPARE(w.zoom(), 10);
    QVERIFY(w.center().isValid());
}

void TstMapWidget::reportsErrorOnMissingJson()
{
    MapConfig cfg = baseConfig(m_dir.filePath(QStringLiteral("nada.json")));
    MapWidget w(cfg);
    QVERIFY(!w.isReady());
    QVERIFY(!w.lastError().isEmpty());
    // Y no revienta al usarlo.
    QVERIFY(w.availableBaseLayers().isEmpty());
    w.setZoom(12);
    w.zoomIn();
}

void TstMapWidget::listsAvailableLayers()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    const auto capas = w.availableBaseLayers();
    QCOMPARE(capas.size(), 2);

    bool hayOsm = false, haySat = false;
    for (const BaseLayerInfo &c : capas) {
        QVERIFY(c.available);
        QVERIFY(!c.displayName.isEmpty());
        if (c.id == QStringLiteral("osm")) {
            hayOsm = true;
            QCOMPARE(c.displayName, QStringLiteral("Open Street Map"));
        }
        if (c.id == QStringLiteral("satelital")) {
            haySat = true;
            // Se expone el zoom RECOMENDADO, no el ultimo con teselas sueltas.
            QCOMPARE(c.maxZoom, 11);
        }
    }
    QVERIFY(hayOsm);
    QVERIFY(haySat);
}

void TstMapWidget::switchesBaseLayerInPlace()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    QSignalSpy cambios(&w, &MapWidget::baseLayerChanged);

    QCOMPARE(w.baseLayerId(), QStringLiteral("osm"));
    const QGeoCoordinate antes = w.center();
    const int zoomAntes = w.zoom();

    QVERIFY(w.setBaseLayerId(QStringLiteral("satelital")));
    QCOMPARE(w.baseLayerId(), QStringLiteral("satelital"));
    QCOMPARE(cambios.count(), 1);

    // Conserva el centro; el zoom puede recortarse al rango de la capa nueva.
    QVERIFY(qAbs(w.center().latitude() - antes.latitude()) < 0.5);
    QVERIFY(qAbs(w.center().longitude() - antes.longitude()) < 0.5);
    QVERIFY(w.zoom() <= zoomAntes);

    // Ida y vuelta varias veces, que es donde el codigo original se rompia
    // por el connectionName compartido.
    for (int i = 0; i < 5; ++i) {
        QVERIFY(w.setBaseLayerId(QStringLiteral("osm")));
        QVERIFY(w.setBaseLayerId(QStringLiteral("satelital")));
    }
    QCOMPARE(w.baseLayerId(), QStringLiteral("satelital"));

    // Una capa inexistente se rechaza sin romper el estado.
    QVERIFY(!w.setBaseLayerId(QStringLiteral("relieve")));
    QCOMPARE(w.baseLayerId(), QStringLiteral("satelital"));
}

// Un MBTiles listado en datasets.json con solo "id" y "filePath" se dibuja como
// cualquier otra capa base: el color del centro del mapa es el de la tesela que
// geograficamente lo contiene (x,y en XYZ), lo que comprueba el volteo TMS de
// extremo a extremo. Y las capas de siempre siguen funcionando a su lado.
void TstMapWidget::mbtilesLayerRendersInTheWidget()
{
    const QString mb = m_dir.filePath(QStringLiteral("e2e.mbtiles"));
    const QString conn = QStringLiteral("mk_e2e");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(mb);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                      "tile_row INTEGER, tile_data BLOB)")));
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO metadata VALUES ('name','Prueba MBTiles'), "
                                      "('format','png'), ('minzoom','3'), ('maxzoom','4')")));
        db.transaction();
        for (int z = 3; z <= 4; ++z)
            for (int x = 0; x < (1 << z); ++x)
                for (int y = 0; y < (1 << z); ++y) {
                    QImage img(256, 256, QImage::Format_RGB32);
                    img.fill(QColor(x * 16, y * 16, 100));       // el color dice la tesela (XYZ)
                    QByteArray png;
                    QBuffer buf(&png);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
                    q.bindValue(QStringLiteral(":z"), z);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), (1 << z) - 1 - y);          // TMS
                    q.bindValue(QStringLiteral(":d"), png);
                    QVERIFY(q.exec());
                }
        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);

    // datasets.json: las dos bases de siempre y el MBTiles con lo minimo.
    QJsonObject root = QJsonDocument::fromJson([&] {
        QFile f(m_jsonPath);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }()).object();
    QJsonArray arr = root.value(QStringLiteral("datasets")).toArray();
    arr.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("mb") },
                            { QStringLiteral("filePath"), mb } });
    root[QStringLiteral("datasets")] = arr;
    const QString json = m_dir.filePath(QStringLiteral("con_mbtiles.json"));
    QFile out(json);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(QJsonDocument(root).toJson());
    out.close();

    MapConfig cfg = baseConfig(json);
    cfg.initialLayerId = QStringLiteral("mb");
    cfg.initialZoom = 4;
    cfg.initialCenter = QGeoCoordinate(40.0, -100.0);
    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    w.resize(600, 400);
    QCOMPARE(w.baseLayerId(), QStringLiteral("mb"));
    QCOMPARE(w.availableBaseLayers().size(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.99, 20000);

    // Tesela XYZ que contiene el centro, calculada en el test con Mercator.
    const double lat = 40.0, lon = -100.0;
    const double pi = 3.14159265358979323846;
    const int n = 1 << 4;
    const int ex = int(std::floor((lon + 180.0) / 360.0 * n));
    const int ey = int(std::floor((1.0 - std::log(std::tan(lat * pi / 180.0)
                                  + 1.0 / std::cos(lat * pi / 180.0)) / pi) / 2.0 * n));
    QCoreApplication::processEvents();
    QTest::qWait(50);
    const QImage img = w.grab().toImage();
    const QColor centro = QColor(img.pixel(img.width() / 2, img.height() / 2));
    QCOMPARE(centro.red(), ex * 16);
    QCOMPARE(centro.green(), ey * 16);
    QCOMPARE(centro.blue(), 100);

    // Las capas de siempre siguen ahi y se alterna con ellas sin problema.
    QVERIFY(w.setBaseLayerId(QStringLiteral("osm")));
    QVERIFY(w.setBaseLayerId(QStringLiteral("mb")));
}

// La elevacion puede venir de un GeoTIFF suelto o de una carpeta de ellos, tanto
// por la configuracion como en caliente; el resto de la API (cota, visibilidad)
// funciona igual que con las bases de siempre.
// Añadir una capa base con el mapa abierto: un MBTiles y una base de teselas
// normal entran como capas nuevas (con id unico), se pueden activar y dibujar, y
// los ficheros que no sirven devuelven un motivo en vez de romper nada.
void TstMapWidget::addsBaseLayersAtRuntime()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    const auto antes = w.availableBaseLayers().size();

    // MBTiles propio de este test (no depende de que otro lo haya creado antes):
    // el mundo a zoom 3 y 4, con el color de cada tesela en el PNG.
    const QString mb = m_dir.filePath(QStringLiteral("capa_extra.mbtiles"));
    const QString conn = QStringLiteral("mk_extra");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(mb);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                      "tile_row INTEGER, tile_data BLOB)")));
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO metadata VALUES ('name','Capa extra'), "
                                      "('format','png'), ('minzoom','3'), ('maxzoom','4')")));
        db.transaction();
        for (int z = 3; z <= 4; ++z)
            for (int x = 0; x < (1 << z); ++x)
                for (int y = 0; y < (1 << z); ++y) {
                    QImage img(256, 256, QImage::Format_RGB32);
                    img.fill(QColor(x * 16, y * 16, 200));
                    QByteArray png;
                    QBuffer buf(&png);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
                    q.bindValue(QStringLiteral(":z"), z);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), (1 << z) - 1 - y);          // TMS
                    q.bindValue(QStringLiteral(":d"), png);
                    QVERIFY(q.exec());
                }
        db.commit();
    }
    QSqlDatabase::removeDatabase(conn);

    QString error;
    const QString id = w.addBaseLayer(mb, &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(id, QStringLiteral("capa_extra"));
    QCOMPARE(w.availableBaseLayers().size(), antes + 1);

    // El mismo fichero otra vez: id distinto, no pisa a la anterior.
    const QString id2 = w.addBaseLayer(mb, &error);
    QCOMPARE(id2, QStringLiteral("capa_extra_2"));

    // Una base de teselas normal (la satelital sintetica) con id a elegir.
    const QString sat = w.addBaseLayer(m_dir.filePath(QStringLiteral("sat.sqlitedb")), &error,
                                       QStringLiteral("mi_sat"));
    QVERIFY2(sat == QStringLiteral("mi_sat"), qPrintable(error));

    // Se activa y dibuja como cualquier otra.
    QVERIFY(w.setBaseLayerId(id));
    QCOMPARE(w.baseLayerId(), id);
    w.setCenter(QGeoCoordinate(22.0, -79.5));
    w.setZoom(4);
    const QImage img = w.renderImage();
    QVERIFY(!img.isNull());
    QVERIFY(w.setBaseLayerId(QStringLiteral("osm")));          // y se vuelve a las de siempre

    // Lo que no sirve: motivo claro y nada cambia.
    const auto total = w.availableBaseLayers().size();
    QVERIFY(w.addBaseLayer(m_dir.filePath(QStringLiteral("no_existe.mbtiles")), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("no_existe")));
    const QString basura = m_dir.filePath(QStringLiteral("basura.sqlitedb"));
    QFile f(basura);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(3000, 'x'));
    f.close();
    error.clear();
    QVERIFY(w.addBaseLayer(basura, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(w.availableBaseLayers().size(), total);
}

// Busqueda de lugares a traves del widget: sin indice no hay nada; con el de
// prueba (de MapConfig o en caliente) se busca sin tildes y goToPlace centra el
// mapa con el zoom del tipo de lugar, sin pasar del maximo de la capa.
void TstMapWidget::placeSearchThroughTheWidget()
{
    const QString lugares = QStringLiteral(LIBMAPA_TESTDATA) + QStringLiteral("/places/lugares_prueba.sqlitedb");

    MapWidget sin(baseConfig(m_jsonPath));
    QVERIFY(sin.isReady());
    QVERIFY(!sin.hasPlaces());
    QVERIFY(sin.searchPlaces(QStringLiteral("santiago")).isEmpty());

    MapConfig cfg = baseConfig(m_jsonPath);
    cfg.placesDbFile = lugares;
    MapWidget w(cfg);
    QVERIFY(w.isReady());
    QVERIFY(w.hasPlaces());
    const QVector<Place> r = w.searchPlaces(QStringLiteral("SANTIAGO de"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.first().name, QStringLiteral("Santiago de Cuba"));

    w.goToPlace(r.first());
    QVERIFY(std::fabs(w.center().latitude() - 20.0247) < 1e-3);
    QVERIFY(std::fabs(w.center().longitude() + 75.8295) < 1e-3);
    QVERIFY(w.zoom() <= w.maxZoom());
    QVERIFY(w.zoom() <= r.first().suggestedZoom());

    Place invalido;                                  // sin coordenada: no mueve nada
    const QGeoCoordinate antes = w.center();
    w.goToPlace(invalido);
    QCOMPARE(w.center(), antes);

    w.setPlacesDb(QString());                        // en caliente: se quita
    QVERIFY(!w.hasPlaces());
    sin.setPlacesDb(lugares);                        // y se pone
    QVERIFY(sin.hasPlaces());
    QCOMPARE(sin.searchPlaces(QStringLiteral("baracoa")).size(), 1);
}

void TstMapWidget::geotiffElevationThroughTheWidget()
{
    // Un cerro cónico de 1° x 1° en (20..21 N, -78..-77 E): cota = 1000 - distancia al
    // centro en grados * 4000, con 0.01° de píxel.
    TiffSpec s;
    s.width = 100; s.height = 100; s.bps = 16; s.sampleFormat = 2;
    s.originX = -78.0; s.originY = 21.0; s.dx = 0.01; s.dy = 0.01;
    s.tiled = true; s.tile = 32; s.compression = 8; s.predictor = 2;
    const QString tif = m_dir.filePath(QStringLiteral("cerro.tif"));
    QVERIFY(writeGeoTiff(tif, s, [](int c, int r, int) {
        const double lon = -78.0 + (c + 0.5) * 0.01, lat = 21.0 - (r + 0.5) * 0.01;
        const double d = std::sqrt((lon + 77.5) * (lon + 77.5) + (lat - 20.5) * (lat - 20.5));
        return std::round(1000.0 - d * 4000.0);
    }));

    MapConfig cfg = baseConfig(m_jsonPath);
    cfg.elevationDbFile = tif;                       // un .tif donde iría la BD
    MapWidget w(cfg);
    QVERIFY(w.isReady());
    // El pico cae en la esquina de cuatro píxeles; se pide en el centro de uno de
    // ellos, a 0.00707° del vértice: 1000 - 0.00707*4000 = 971.7.
    const double cima = w.elevationAt(QGeoCoordinate(20.495, -77.495));
    QVERIFY2(std::fabs(cima - 972.0) < 1.5, qPrintable(QString::number(cima)));
    const double falda = w.elevationAt(QGeoCoordinate(20.5, -77.3));      // a 0.2° del centro
    QVERIFY2(std::fabs(falda - 200.0) < 3.0, qPrintable(QString::number(falda)));
    QVERIFY(std::isnan(w.elevationAt(QGeoCoordinate(25.0, -70.0))));      // fuera

    // En caliente, con una carpeta de GeoTIFF.
    QDir().mkpath(m_dir.filePath(QStringLiteral("dems")));
    QVERIFY(QFile::copy(tif, m_dir.filePath(QStringLiteral("dems/z1.tif"))));
    w.setElevationDb(QString());
    QVERIFY(std::isnan(w.elevationAt(QGeoCoordinate(20.495, -77.495))));
    w.setElevationDir(m_dir.filePath(QStringLiteral("dems")));
    QVERIFY(std::fabs(w.elevationAt(QGeoCoordinate(20.495, -77.495)) - 972.0) < 1.5);

    // La libreria dice QUE cargo, y por que no si no cargo nada: la demo lo muestra.
    QVERIFY2(w.elevationDescription().contains(QStringLiteral("1 GeoTIFF")),
             qPrintable(w.elevationDescription()));
    QVERIFY(w.elevationWarnings().isEmpty());
    QFile rota(m_dir.filePath(QStringLiteral("dems/roto.tif")));
    QVERIFY(rota.open(QIODevice::WriteOnly));
    rota.write("no soy un tiff");
    rota.close();
    w.setElevationDir(m_dir.filePath(QStringLiteral("dems")));      // uno bueno y uno roto
    QVERIFY(w.elevationDescription().contains(QStringLiteral("1 GeoTIFF")));
    QCOMPARE(w.elevationWarnings().size(), 1);
    QVERIFY(w.elevationWarnings().first().contains(QStringLiteral("roto.tif")));
    QFile::remove(m_dir.filePath(QStringLiteral("dems/roto.tif")));
    QDir().mkpath(m_dir.filePath(QStringLiteral("vacia")));
    w.setElevationDir(m_dir.filePath(QStringLiteral("vacia")));      // sin datos: no hay DEM
    QVERIFY(w.elevationDescription().isEmpty());
    QCOMPARE(w.elevationWarnings().size(), 1);
    QVERIFY(std::isnan(w.elevationAt(QGeoCoordinate(20.495, -77.495))));
    w.setElevationDir(m_dir.filePath(QStringLiteral("dems")));       // y vuelve a funcionar
    QVERIFY(std::fabs(w.elevationAt(QGeoCoordinate(20.495, -77.495)) - 972.0) < 1.5);

    // Y el análisis de siempre, sobre ese DEM: perfil recto por encima del cerro.
    const ElevationProfile perfil = w.elevationProfile({ QGeoCoordinate(20.5, -77.8),
                                                         QGeoCoordinate(20.5, -77.2) });
    QVERIFY(perfil.isValid());
    QVERIFY(std::fabs(perfil.maxElevation - 1000.0) < 40.0);
}

void TstMapWidget::navigationKeepsCenter()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    const QGeoCoordinate destino(23.1136, -82.3666);
    w.setCenter(destino);

    QVERIFY(qAbs(w.center().latitude() - destino.latitude()) < 1e-6);
    QVERIFY(qAbs(w.center().longitude() - destino.longitude()) < 1e-6);

    // La extension visible debe contener el centro.
    const QGeoCoordinate no = w.visibleNorthWest();
    const QGeoCoordinate se = w.visibleSouthEast();
    QVERIFY(no.latitude() > destino.latitude());
    QVERIFY(se.latitude() < destino.latitude());
    QVERIFY(no.longitude() < destino.longitude());
    QVERIFY(se.longitude() > destino.longitude());
}

void TstMapWidget::zoomIsClampedToRecommendedRange()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    w.setZoom(99);
    QCOMPARE(w.zoom(), w.maxZoom());

    w.setZoom(-5);
    QCOMPARE(w.zoom(), w.minZoom());

    // zoomIn/zoomOut respetan los limites.
    for (int i = 0; i < 30; ++i) w.zoomIn();
    QCOMPARE(w.zoom(), w.maxZoom());
    for (int i = 0; i < 30; ++i) w.zoomOut();
    QCOMPARE(w.zoom(), w.minZoom());
}

void TstMapWidget::fitBoundsFramesTheArea()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    const QGeoCoordinate no(23.3, -85.0);
    const QGeoCoordinate se(19.7, -74.0);
    w.fitBounds(no, se);

    // El area de dibujo debe tener el tamano del widget aunque este no se
    // haya mostrado nunca ni se haya vuelto al bucle de eventos.
    auto *plot = qobject_cast<QCustomPlot *>(w.customPlot());
    QCOMPARE(plot->viewport().size(), QSize(800, 600));
    QCOMPARE(plot->axisRect()->rect().size(), QSize(800, 600));

    // El centro debe caer dentro del area pedida.
    QVERIFY(w.center().latitude() < no.latitude());
    QVERIFY(w.center().latitude() > se.latitude());
    QVERIFY(w.center().longitude() > no.longitude());
    QVERIFY(w.center().longitude() < se.longitude());

    // Y el area pedida debe caber en lo visible.
    QVERIFY(w.visibleNorthWest().longitude() <= no.longitude() + 1e-6);
    QVERIFY(w.visibleSouthEast().longitude() >= se.longitude() - 1e-6);
}

void TstMapWidget::resizeDoesNotLoseTheView()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    const QGeoCoordinate destino(22.5, -80.0);
    w.setCenter(destino);
    const int z = w.zoom();

    // Un resizeEvent con la firma correcta solo recalcula el viewport: no
    // recrea capas ni recarga ficheros, que era lo que habria hecho el
    // resizeEvent() sin argumentos del codigo original (falla F-18).
    w.resize(1280, 800);
    QCoreApplication::processEvents();

    QCOMPARE(w.zoom(), z);
    QVERIFY(qAbs(w.center().latitude() - destino.latitude()) < 1e-6);
    QVERIFY(qAbs(w.center().longitude() - destino.longitude()) < 1e-6);

    w.resize(400, 300);
    QCoreApplication::processEvents();
    QCOMPARE(w.zoom(), z);
    QVERIFY(w.isReady());
}

void TstMapWidget::usesASingleTileLayer()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    auto *plot = qobject_cast<QCustomPlot *>(w.customPlot());
    QVERIFY(plot != nullptr);

    const int itemsIniciales = plot->itemCount();

    // Se mueve el mapa varias veces. En el codigo original, cada movimiento
    // destruia y recreaba un QCPItemPixmap por tesela visible.
    for (int i = 0; i < 10; ++i) {
        w.setCenter(QGeoCoordinate(22.0 + i * 0.05, -79.5 + i * 0.05));
        QCoreApplication::processEvents();
    }

    QCOMPARE(plot->itemCount(), itemsIniciales);
    QVERIFY2(plot->itemCount() < 5,
             qPrintable(QStringLiteral("hay %1 items en el plot; las teselas "
                                       "deben ir en UNA capa, no en items")
                            .arg(plot->itemCount())));
}

void TstMapWidget::toolChangesCursorAndState()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    QCOMPARE(w.activeTool(), MapTool::None);

    w.setActiveTool(MapTool::Measure);
    QCOMPARE(w.activeTool(), MapTool::Measure);

    w.setActiveTool(MapTool::AreaZoom);
    QCOMPARE(w.activeTool(), MapTool::AreaZoom);

    w.setActiveTool(MapTool::None);
    QCOMPARE(w.activeTool(), MapTool::None);
}

void TstMapWidget::publicHeaderHasNoQCustomPlot()
{
    // La comprobacion de verdad la hace tst_headers_selfcontained al compilar
    // MapWidget.h aislado, sin la ruta de include de QCustomPlot. Aqui se
    // documenta la intencion y se verifica el contrato: customPlot() devuelve
    // QWidget*, no QCustomPlot*, para que el cliente no herede la dependencia.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    QWidget *generico = w.customPlot();
    QVERIFY(generico != nullptr);
    QVERIFY(qobject_cast<QCustomPlot *>(generico) != nullptr);
}

void TstMapWidget::viewHasRealSizeWithoutBeingShown()
{
    // Sin mostrar la ventana y sin volver al bucle de eventos: los layouts de
    // Qt no reparten geometria con el widget oculto, y el resizeEvent tampoco
    // se entrega. El area de dibujo se quedaba en 100x30 y todo el calculo de
    // zoom salia mal.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(640, 480);

    auto *plot = qobject_cast<QCustomPlot *>(w.customPlot());
    QVERIFY(plot != nullptr);
    QCOMPARE(plot->size(), QSize(640, 480));
    QCOMPARE(plot->viewport().size(), QSize(640, 480));

    // axisRect solo tiene geometria tras el primer recalculo de disposicion
    // de QCustomPlot, que es diferido. ensureLayout() lo fuerza.
    QCOMPARE(plot->axisRect()->rect().size(), QSize(640, 480));

    // Y varios tamanos seguidos, siempre sin mostrar.
    //
    // La sincronizacion es PEREZOSA: ocurre al entrar por la API publica, no
    // en el instante del resize. Con el widget oculto Qt no entrega el
    // resizeEvent, asi que no hay ningun momento en el que engancharse. Da
    // igual en la practica -- para hacer algo con el mapa hay que llamarle a
    // algo -- pero conviene que el test lo refleje en vez de fingir que es
    // instantaneo.
    for (const QSize &s : {QSize(1280, 800), QSize(320, 240), QSize(1024, 768)}) {
        w.resize(s);
        auto *tras = qobject_cast<QCustomPlot *>(w.customPlot());   // sincroniza
        QCOMPARE(tras->size(), s);
        QCOMPARE(tras->viewport().size(), s);
        QCOMPARE(tras->axisRect()->rect().size(), s);
    }
}

void TstMapWidget::drawsTilesInTheRightPlace()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(640, 480);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.0, -79.5));

    // Se deja llegar a las teselas: la carga es asincrona.
    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.99, 20000);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);
    const auto &plan = vista->tileLayer()->plan();

    // A zoom 10 con 640x480 el viewport abarca unas 2.5 x 1.9 teselas, asi
    // que con el margen de una tesela salen del orden de una docena de
    // celdas. Lo importante no es el numero exacto sino que NO sea 2, que es
    // lo que salia cuando el area de dibujo era 100x30.
    QVERIFY2(plan.slotCount() >= 6,
             qPrintable(QStringLiteral("solo %1 celdas; el area de dibujo "
                                       "probablemente sigue mal")
                            .arg(plan.slotCount())));
    QCOMPARE(plan.emptyCount, 0);

    // Todas las celdas son del nivel pedido y sus indices son contiguos.
    int xMin = INT_MAX, xMax = INT_MIN, yMin = INT_MAX, yMax = INT_MIN;
    for (const TileDrawItem &i : plan.items) {
        QCOMPARE(i.slot.z, 10);
        xMin = qMin(xMin, i.slot.x); xMax = qMax(xMax, i.slot.x);
        yMin = qMin(yMin, i.slot.y); yMax = qMax(yMax, i.slot.y);
    }
    QCOMPARE(plan.slotCount(), (xMax - xMin + 1) * (yMax - yMin + 1));

    // Y el centro pedido cae dentro de la tesela que le corresponde.
    const TileKey esperada = TileMatrix::tileAt(QGeoCoordinate(22.0, -79.5), 10);
    bool encontrada = false;
    for (const TileDrawItem &i : plan.items)
        if (i.slot == esperada)
            encontrada = true;
    QVERIFY2(encontrada, "La tesela del centro no esta en el plan de dibujo");

    // El dibujo llega de verdad al lienzo: se renderiza y se comprueba que la
    // imagen no es de un solo color, que es lo que salia cuando las teselas
    // se pintaban a escala equivocada.
    QImage lienzo = vista->toPixmap(640, 480).toImage();
    QVERIFY(!lienzo.isNull());
    QSet<QRgb> colores;
    for (int y = 10; y < lienzo.height(); y += 37)
        for (int x = 10; x < lienzo.width(); x += 37)
            colores.insert(lienzo.pixel(x, y));
    QVERIFY2(colores.size() > 1,
             "El lienzo es de un solo color: las teselas no se dibujaron");

    qInfo() << "Dibujadas" << vista->tileLayer()->lastDrawnCount()
            << "teselas en" << plan.slotCount() << "celdas;"
            << colores.size() << "colores distintos en el lienzo";
}

void TstMapWidget::doesNotHangDraggingAtLowZoom()
{
    // A zoom 3 el viewport abarca 225 grados de longitud, asi que basta
    // arrastrar un poco para que el centro se salga de [-180,180].
    // QGeoCoordinate invalido devuelve NaN en latitude()/longitude(), los
    // rangos de los ejes se vuelven NaN y QCustomPlot se cuelga generando
    // marcas. Es el bloqueo que se veia en demo.exe.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1280, 800);
    w.setZoom(3);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    // Se simula un arrastre largo hacia el este, muy por encima de una vuelta
    // al mundo, y otro hacia el norte hasta pasarse del polo.
    for (int i = 0; i < 60; ++i) {
        // El constructor con posicion global esta disponible y no deprecado
        // en Qt 5 y Qt 6 por igual.
        QMouseEvent pulsar(QEvent::MouseButtonPress, QPointF(1000, 400),
                           QPointF(1000, 400), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
        QMouseEvent mover(QEvent::MouseMove, QPointF(100, 100),
                          QPointF(100, 100), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent soltar(QEvent::MouseButtonRelease, QPointF(100, 100),
                           QPointF(100, 100), Qt::LeftButton,
                           Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vista, &pulsar);
        QApplication::sendEvent(vista, &mover);
        QApplication::sendEvent(vista, &soltar);

        // Ni un solo NaN en ningun momento, y el centro siempre valido.
        QVERIFY2(std::isfinite(vista->xAxis->range().lower)
                 && std::isfinite(vista->xAxis->range().upper)
                 && std::isfinite(vista->yAxis->range().lower)
                 && std::isfinite(vista->yAxis->range().upper),
                 qPrintable(QStringLiteral("rangos NaN en la iteracion %1").arg(i)));
        QVERIFY(w.center().isValid());
        QVERIFY(w.center().longitude() >= -180.0 && w.center().longitude() <= 180.0);
        QVERIFY(qAbs(w.center().latitude()) <= 85.06);
    }

    // Y sigue respondiendo. Ojo: a zoom 3 con 1280 px se ven 225 grados de
    // longitud, asi que el centro se RECORTA a +-67.5 para que la vista no
    // se salga del mundo. Pedir -79.5 devuelve -67.5, y esta bien: es lo que
    // evita las bandas vacias a los lados que se veian en las capturas.
    w.setCenter(QGeoCoordinate(22.0, -79.5));
    QVERIFY(w.center().isValid());
    QVERIFY(w.visibleNorthWest().longitude() >= -180.0001);
    QVERIFY(w.visibleSouthEast().longitude() <= 180.0001);

    // A un zoom donde el mundo es mas ancho que la pantalla, el centro si se
    // respeta exactamente.
    w.setZoom(8);
    w.setCenter(QGeoCoordinate(22.0, -79.5));
    QVERIFY(qAbs(w.center().longitude() + 79.5) < 1e-6);
    QVERIFY(qAbs(w.center().latitude() - 22.0) < 1e-6);
}

void TstMapWidget::tilesAreSquareAtLowZoom()
{
    // El eje Y va en unidades de Mercator, no en grados de latitud. Con
    // latitud, el mapa se estiraba verticalmente: a 23 grados el desfase es
    // de 0.64 grados y a 60 de 15.5. A zoom alto no se notaba.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(640, 512);
    w.setZoom(4);
    w.setCenter(QGeoCoordinate(45.0, 0.0));    // latitud alta: es donde duele

    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.0, 20000);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    const auto &plan = vista->tileLayer()->plan();
    QVERIFY(!plan.items.isEmpty());

    // Cada tesela debe salir CUADRADA en pantalla, y del tamano que toca.
    const double ladoEsperado = 256.0;
    for (const TileDrawItem &i : plan.items) {
        const QGeoCoordinate nw = TileMatrix::tileNorthWest(i.slot);
        const QGeoCoordinate se = TileMatrix::tileSouthEast(i.slot);

        const double ancho = vista->xAxis->coordToPixel(se.longitude())
                             - vista->xAxis->coordToPixel(nw.longitude());
        const double alto  = vista->yAxis->coordToPixel(
                                 TileMatrix::latitudeToAxisY(se.latitude()))
                           - vista->yAxis->coordToPixel(
                                 TileMatrix::latitudeToAxisY(nw.latitude()));

        QVERIFY2(qAbs(ancho - ladoEsperado) < 1.0,
                 qPrintable(QStringLiteral("ancho %1 px").arg(ancho)));
        QVERIFY2(qAbs(alto - ladoEsperado) < 1.0,
                 qPrintable(QStringLiteral("alto %1 px (deformacion vertical)")
                                .arg(alto)));
    }

    qInfo() << "A zoom 4 y latitud 45, las" << plan.items.size()
            << "teselas salen cuadradas de 256 px";
}

void TstMapWidget::layersDoNotShareCachedTiles()
{
    // TileKey solo lleva z/x/y, y las dos BD usan los mismos indices logicos
    // para la misma geografia. Con una cache compartida, al cambiar de capa
    // se veian teselas de la otra. Ahora hay una cache por capa.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(640, 480);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.0, -79.5));

    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.99, 20000);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    const QImage lienzoOsm = vista->toPixmap(640, 480).toImage();

    QVERIFY(w.setBaseLayerId(QStringLiteral("satelital")));
    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.99, 20000);
    const QImage lienzoSat = vista->toPixmap(640, 480).toImage();

    QVERIFY2(lienzoOsm != lienzoSat,
             "Las dos capas dibujan lo mismo: la cache las esta mezclando");

    // Y al volver a OSM se recupera exactamente lo de antes, sin releer nada.
    QVERIFY(w.setBaseLayerId(QStringLiteral("osm")));
    QCoreApplication::processEvents();
    const QImage vuelta = vista->toPixmap(640, 480).toImage();
    QCOMPARE(vuelta, lienzoOsm);

    qInfo() << "Las dos capas dibujan imagenes distintas y volver a la"
            << "anterior la recupera intacta";
}

void TstMapWidget::draggingAtLowZoomNeverProducesNaN()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1100, 750);
    w.setZoom(3);
    w.setCenter(QGeoCoordinate(44.9, -89.1));

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    // Arrastre largo hacia el oeste, muy por encima de lo que hace falta para
    // cruzar el antimeridiano al zoom 3.
    QPoint donde(900, 400);
    QMouseEvent presiona = mouseEvent(QEvent::MouseButtonPress, donde,
                                      Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(vista, &presiona);

    for (int i = 0; i < 60; ++i) {
        donde -= QPoint(40, 0);        // hacia el oeste
        if (donde.x() < 20) donde.setX(1080);
        QMouseEvent mueve = mouseEvent(QEvent::MouseMove, donde,
                                       Qt::NoButton, Qt::LeftButton);
        QCoreApplication::sendEvent(vista, &mueve);

        // Lo esencial: en ningun momento aparece un NaN.
        QVERIFY2(w.center().isValid(), "El centro dejo de ser valido");
        QVERIFY(!qIsNaN(w.center().latitude()));
        QVERIFY(!qIsNaN(w.center().longitude()));
        QVERIFY(!qIsNaN(vista->xAxis->range().lower));
        QVERIFY(!qIsNaN(vista->xAxis->range().upper));
        QVERIFY(!qIsNaN(vista->yAxis->range().lower));
        QVERIFY(!qIsNaN(vista->yAxis->range().upper));
    }

    QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease, donde,
                                    Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &suelta);

    // Y lo mismo arrastrando en vertical hasta los polos.
    QCoreApplication::sendEvent(vista, &presiona);
    for (int i = 0; i < 40; ++i) {
        donde += QPoint(0, 30);
        if (donde.y() > 730) donde.setY(20);
        QMouseEvent mueve = mouseEvent(QEvent::MouseMove, donde,
                                       Qt::NoButton, Qt::LeftButton);
        QCoreApplication::sendEvent(vista, &mueve);
        QVERIFY(w.center().isValid());
        QVERIFY(qAbs(w.center().latitude()) <= 90.0);
    }
    QCoreApplication::sendEvent(vista, &suelta);
}

void TstMapWidget::viewNeverLeavesTheWorld()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1100, 750);

    // En cada nivel y en cada esquina, la extension visible debe quedar
    // dentro del mundo: nada de bandas vacias por salirse del mapa.
    for (int z = w.minZoom(); z <= qMin(w.minZoom() + 4, w.maxZoom()); ++z) {
        w.setZoom(z);
        for (const QGeoCoordinate &c : {QGeoCoordinate(84.0, 179.0),
                                        QGeoCoordinate(-84.0, -179.0),
                                        QGeoCoordinate(0.0, 0.0)}) {
            w.setCenter(c);
            const QGeoCoordinate no = w.visibleNorthWest();
            const QGeoCoordinate se = w.visibleSouthEast();

            QVERIFY(no.isValid());
            QVERIFY(se.isValid());
            QVERIFY2(no.longitude() >= -180.0001,
                     qPrintable(QStringLiteral("z%1 lon oeste %2")
                                    .arg(z).arg(no.longitude())));
            QVERIFY2(se.longitude() <= 180.0001,
                     qPrintable(QStringLiteral("z%1 lon este %2")
                                    .arg(z).arg(se.longitude())));
            QVERIFY(no.latitude() <= 85.06);
            QVERIFY(se.latitude() >= -85.06);
        }
    }
}

void TstMapWidget::eachLayerHasItsOwnCache()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.0, -79.5));

    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.99, 20000);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    const auto &planOsm = vista->tileLayer()->plan();
    QVERIFY(!planOsm.items.isEmpty());

    // Se guarda la imagen de una tesela concreta con la capa OSM activa.
    const TileKey clave = planOsm.items.first().source;
    const QImage imgOsm = vista->tileLayer()->imageFor(clave);
    QVERIFY(!imgOsm.isNull());

    // Se cambia a satelital y se espera a que cargue LA MISMA clave z/x/y.
    QVERIFY(w.setBaseLayerId(QStringLiteral("satelital")));
    QTRY_VERIFY_WITH_TIMEOUT(
        !vista->tileLayer()->imageFor(clave).isNull(), 20000);

    const QImage imgSat = vista->tileLayer()->imageFor(clave);
    QVERIFY(!imgSat.isNull());

    // Misma clave z/x/y, imagenes DISTINTAS: cada capa tiene su cache.
    // Con la cache compartida se devolvia la de OSM sobre el mapa satelital,
    // que es lo que se veia en pantalla: teselas de calles mezcladas con
    // fotografia aerea.
    QVERIFY2(imgOsm != imgSat,
             "Las dos capas devuelven la MISMA imagen para z/x/y: la cache "
             "esta compartida");

    // Y al volver a OSM se recupera la suya, no la satelital.
    QVERIFY(w.setBaseLayerId(QStringLiteral("osm")));
    QCOMPARE(vista->tileLayer()->imageFor(clave), imgOsm);
}

void TstMapWidget::toolItemsLandUnderTheCursor()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    // Se comprueba a varios zooms y latitudes: el desfase entre latitud y
    // Mercator crece con ambos. Sobre Cuba a zoom 3 son unos 4 pixeles, casi
    // imperceptible; a zoom 11 son cientos.
    struct Caso { int zoom; double lat; double lon; };
    const QVector<Caso> casos = {
        {3, 23.0, -82.0}, {8, 23.0, -82.0}, {11, 23.1, -82.4},
        {8, 45.0, -70.0}, {6, 60.0, -60.0},
    };

    for (const Caso &c : casos) {
        w.setZoom(c.zoom);
        w.setCenter(QGeoCoordinate(c.lat, c.lon));
        w.setActiveTool(MapTool::Measure);

        const QPoint clic(370, 250);

        QMouseEvent pulsar(QEvent::MouseButtonPress, QPointF(clic), QPointF(clic),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent soltar(QEvent::MouseButtonRelease, QPointF(clic), QPointF(clic),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vista, &pulsar);
        QApplication::sendEvent(vista, &soltar);

        // El primer clic fija el origen de la medicion. Se busca el item y se
        // comprueba en que pixel ha quedado.
        QCPItemLine *linea = nullptr;
        for (int i = 0; i < vista->itemCount(); ++i)
            if (auto *l = qobject_cast<QCPItemLine *>(vista->item(i)))
                linea = l;
        QVERIFY2(linea != nullptr, "No se creo la linea de medicion");

        const QPointF pixel = linea->start->pixelPosition();

        QVERIFY2(qAbs(pixel.x() - clic.x()) < 1.5,
                 qPrintable(QStringLiteral("z%1 lat%2: desfase en X de %3 px")
                                .arg(c.zoom).arg(c.lat)
                                .arg(pixel.x() - clic.x())));
        QVERIFY2(qAbs(pixel.y() - clic.y()) < 1.5,
                 qPrintable(QStringLiteral("z%1 lat%2: el marcador quedo %3 px "
                                           "por debajo del clic")
                                .arg(c.zoom).arg(c.lat)
                                .arg(pixel.y() - clic.y())));

        w.setActiveTool(MapTool::None);
    }

    // Lo mismo con el rectangulo de zoom a area.
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(23.0, -82.0));
    w.setActiveTool(MapTool::AreaZoom);

    const QPoint inicio(200, 150);
    const QPoint fin(500, 400);
    QMouseEvent pulsar(QEvent::MouseButtonPress, QPointF(inicio), QPointF(inicio),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent mover(QEvent::MouseMove, QPointF(fin), QPointF(fin),
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(vista, &pulsar);
    QApplication::sendEvent(vista, &mover);

    QCPItemRect *rect = nullptr;
    for (int i = 0; i < vista->itemCount(); ++i)
        if (auto *r = qobject_cast<QCPItemRect *>(vista->item(i)))
            rect = r;
    QVERIFY2(rect != nullptr, "No se creo el rectangulo de zoom a area");

    const QPointF p1 = rect->topLeft->pixelPosition();
    const QPointF p2 = rect->bottomRight->pixelPosition();
    QVERIFY(qAbs(p1.x() - inicio.x()) < 1.5);
    QVERIFY2(qAbs(p1.y() - inicio.y()) < 1.5,
             qPrintable(QStringLiteral("esquina inicial desfasada %1 px")
                            .arg(p1.y() - inicio.y())));
    QVERIFY(qAbs(p2.x() - fin.x()) < 1.5);
    QVERIFY2(qAbs(p2.y() - fin.y()) < 1.5,
             qPrintable(QStringLiteral("esquina final desfasada %1 px")
                            .arg(p2.y() - fin.y())));
}

void TstMapWidget::toolItemsLandExactlyUnderTheCursor()
{
    // El eje Y va en grados de MERCATOR, no de latitud. Colocar un item con
    // la latitud a secas lo situa por DEBAJO del clic: unos 4 px al zoom 3 y
    // cerca de mil al zoom 11, o sea fuera de la pantalla. Es lo que pasaba
    // con la regla de medir y con el zoom a area.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1000, 800);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    for (int z : {3, 5, 8, 11}) {
        if (z > w.maxZoom())
            continue;
        w.setZoom(z);
        w.setCenter(QGeoCoordinate(23.1136, -82.3666));

        for (const QPoint &px : {QPoint(500, 400), QPoint(120, 90),
                                 QPoint(880, 710), QPoint(500, 120)}) {
            // pixel -> geografico -> coordenadas de eje -> pixel
            const QGeoCoordinate geo = vista->coordinateAt(px);
            QVERIFY(geo.isValid());

            const QPointF eje = w.toAxisCoords(geo);
            const QPointF vuelta(vista->xAxis->coordToPixel(eje.x()),
                                 vista->yAxis->coordToPixel(eje.y()));

            const double dx = qAbs(vuelta.x() - px.x());
            const double dy = qAbs(vuelta.y() - px.y());

            QVERIFY2(dx < 1.0 && dy < 1.0,
                     qPrintable(QStringLiteral(
                         "z%1 pixel (%2,%3): el item cae en (%4,%5), "
                         "desfase (%6,%7) px")
                         .arg(z).arg(px.x()).arg(px.y())
                         .arg(vuelta.x(), 0, 'f', 1).arg(vuelta.y(), 0, 'f', 1)
                         .arg(dx, 0, 'f', 1).arg(dy, 0, 'f', 1)));

            // Y la vuelta completa devuelve la misma coordenada.
            const QGeoCoordinate otraVez = w.fromAxisCoords(eje);
            QVERIFY(qAbs(otraVez.latitude() - geo.latitude()) < 1e-9);
            QVERIFY(qAbs(otraVez.longitude() - geo.longitude()) < 1e-9);
        }
    }
}

void TstMapWidget::measureToolPlacesLineAtClickedPixels()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1000, 800);
    w.setZoom(8);
    w.setCenter(QGeoCoordinate(23.1136, -82.3666));
    w.setActiveTool(MapTool::Measure);

    auto *vista = qobject_cast<MapView *>(w.customPlot());

    const QPoint p1(300, 250);
    const QPoint p2(700, 600);

    QMouseEvent c1p = mouseEvent(QEvent::MouseButtonPress, p1,
                                 Qt::LeftButton, Qt::LeftButton);
    QMouseEvent c1r = mouseEvent(QEvent::MouseButtonRelease, p1,
                                 Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &c1p);
    QCoreApplication::sendEvent(vista, &c1r);

    QMouseEvent mv = mouseEvent(QEvent::MouseMove, p2,
                                Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &mv);

    // Se busca la linea de medicion entre los items del plot y se comprueba
    // que sus extremos caen donde se hizo clic, no unos pixeles mas abajo.
    QCPItemLine *linea = nullptr;
    for (int i = 0; i < vista->itemCount(); ++i)
        if (auto *l = qobject_cast<QCPItemLine *>(vista->item(i)))
            if (l->visible())
                linea = l;
    QVERIFY2(linea != nullptr, "No se creo la linea de medicion");

    const QPointF ini = linea->start->pixelPosition();
    const QPointF fin = linea->end->pixelPosition();

    QVERIFY2(qAbs(ini.x() - p1.x()) < 1.5 && qAbs(ini.y() - p1.y()) < 1.5,
             qPrintable(QStringLiteral("inicio en (%1,%2), se pulso en (%3,%4)")
                 .arg(ini.x(), 0, 'f', 1).arg(ini.y(), 0, 'f', 1)
                 .arg(p1.x()).arg(p1.y())));
    QVERIFY2(qAbs(fin.x() - p2.x()) < 1.5 && qAbs(fin.y() - p2.y()) < 1.5,
             qPrintable(QStringLiteral("fin en (%1,%2), el raton esta en (%3,%4)")
                 .arg(fin.x(), 0, 'f', 1).arg(fin.y(), 0, 'f', 1)
                 .arg(p2.x()).arg(p2.y())));

    qInfo() << "Regla de medir: extremos a menos de 1.5 px del clic";
}

void TstMapWidget::toolsLandExactlyUnderTheCursor()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1000, 700);
    w.setZoom(11);
    w.setCenter(QGeoCoordinate(23.1136, -82.3666));

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    w.setActiveTool(MapTool::Measure);

    // Se pulsa en varios puntos repartidos por la pantalla: si hay un
    // desfase constante se vera igual en todos, y si es proporcional a la
    // distancia al origen crecera hacia abajo.
    for (const QPoint &donde : {QPoint(120, 90), QPoint(500, 350),
                                QPoint(880, 610)}) {
        // El punto se ancla al PULSAR. Se manda tambien el release, con el
        // raton ya desplazado, para comprobar que no lo mueve.
        QMouseEvent pulsa = mouseEvent(QEvent::MouseButtonPress, donde,
                                       Qt::LeftButton, Qt::LeftButton);
        QCoreApplication::sendEvent(vista, &pulsa);

        QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease,
                                        donde + QPoint(0, 9),
                                        Qt::LeftButton, Qt::NoButton);
        QCoreApplication::sendEvent(vista, &suelta);

        // Se localiza la linea de medicion y se mira donde quedo su origen.
        QCPItemLine *linea = nullptr;
        for (int i = 0; i < vista->itemCount(); ++i)
            if (auto *l = qobject_cast<QCPItemLine *>(vista->item(i)))
                linea = l;
        QVERIFY(linea != nullptr);

        const QPointF puesto = linea->start->pixelPosition();
        const double dx = puesto.x() - donde.x();
        const double dy = puesto.y() - donde.y();

        qInfo() << "click en" << donde << "-> item en" << puesto
                << " desfase" << dx << dy;

        QVERIFY2(qAbs(dx) < 1.0 && qAbs(dy) < 1.0,
                 qPrintable(QStringLiteral("desfase de %1, %2 pixeles")
                                .arg(dx).arg(dy)));

        // Se reinicia la herramienta para el siguiente punto.
        w.setActiveTool(MapTool::None);
        w.setActiveTool(MapTool::Measure);
    }
}

void TstMapWidget::pixelRoundTripIsExact()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1000, 700);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    double peorX = 0.0, peorY = 0.0;
    QPoint peorPunto;
    int peorZoom = 0;

    for (int z : {3, 8, 11, 14}) {
        w.setZoom(z);
        w.setCenter(QGeoCoordinate(23.1136, -82.3666));

        for (const QPoint &p : {QPoint(10, 10), QPoint(500, 350),
                                QPoint(990, 690), QPoint(123, 456),
                                QPoint(500, 20), QPoint(500, 680)}) {
            // pixel -> geografico -> unidades de eje -> pixel
            const QGeoCoordinate g = vista->coordinateAt(p);
            QVERIFY(g.isValid());

            const QPointF eje = vista->toAxis(g);
            const double px = vista->xAxis->coordToPixel(eje.x());
            const double py = vista->yAxis->coordToPixel(eje.y());

            const double dx = qAbs(px - p.x());
            const double dy = qAbs(py - p.y());
            if (dx > peorX) { peorX = dx; peorPunto = p; peorZoom = z; }
            if (dy > peorY) { peorY = dy; peorPunto = p; peorZoom = z; }
        }
    }

    qInfo() << "Desfase maximo del viaje de ida y vuelta:"
            << peorX << "px en X," << peorY << "px en Y"
            << "(peor caso: zoom" << peorZoom << "punto" << peorPunto << ")";

    // Un clic debe marcar donde se hizo clic. Medio pixel es el redondeo del
    // QPoint entero del evento; mas que eso es un error real.
    QVERIFY2(peorX < 0.5 && peorY < 0.5,
             qPrintable(QStringLiteral("desfase de %1 x %2 pixeles")
                            .arg(peorX).arg(peorY)));
}

void TstMapWidget::toolsAnchorOnPressNotOnRelease()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(1000, 700);
    w.setZoom(11);
    w.setCenter(QGeoCoordinate(23.1136, -82.3666));

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    const QPoint pulsa(400, 300);
    const QPoint suelta(400, 312);      // el raton baja 12 px al soltar

    const QGeoCoordinate esperado = vista->coordinateAt(pulsa);
    const QGeoCoordinate seRia    = vista->coordinateAt(suelta);
    QVERIFY(qAbs(esperado.latitude() - seRia.latitude()) > 1e-9);

    // --- Medir ---------------------------------------------------------
    w.setActiveTool(MapTool::Measure);
    QSignalSpy medido(&w, &MapWidget::measurementFinished);

    QMouseEvent p1 = mouseEvent(QEvent::MouseButtonPress, pulsa,
                                Qt::LeftButton, Qt::LeftButton);
    QMouseEvent m1 = mouseEvent(QEvent::MouseMove, suelta,
                                Qt::NoButton, Qt::LeftButton);
    QMouseEvent r1 = mouseEvent(QEvent::MouseButtonRelease, suelta,
                                Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &p1);
    QCoreApplication::sendEvent(vista, &m1);
    QCoreApplication::sendEvent(vista, &r1);

    // Segundo clic para cerrar la medicion, en un punto conocido.
    const QPoint segundo(600, 300);
    QMouseEvent p2 = mouseEvent(QEvent::MouseButtonPress, segundo,
                                Qt::LeftButton, Qt::LeftButton);
    QMouseEvent r2 = mouseEvent(QEvent::MouseButtonRelease, segundo,
                                Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &p2);
    QCoreApplication::sendEvent(vista, &r2);

    QCOMPARE(medido.count(), 1);
    const auto m = medido.first().at(0).value<libmapa::Measurement>();

    // El origen debe ser donde se PULSO, no donde se solto.
    QVERIFY2(qAbs(m.from.latitude() - esperado.latitude()) < 1e-9,
             "La medicion se anclo donde se solto el boton, no donde se pulso");
    QVERIFY(qAbs(m.from.longitude() - esperado.longitude()) < 1e-9);
    QVERIFY(m.distanceMeters > 0.0);

    // --- Zoom a area ---------------------------------------------------
    w.setActiveTool(MapTool::AreaZoom);
    QSignalSpy area(&w, &MapWidget::areaSelected);

    QMouseEvent p3 = mouseEvent(QEvent::MouseButtonPress, pulsa,
                                Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(vista, &p3);
    QMouseEvent m3 = mouseEvent(QEvent::MouseMove, QPoint(700, 550),
                                Qt::NoButton, Qt::LeftButton);
    QCoreApplication::sendEvent(vista, &m3);
    QMouseEvent r3 = mouseEvent(QEvent::MouseButtonRelease, QPoint(700, 550),
                                Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &r3);

    QCOMPARE(area.count(), 1);
    const QGeoCoordinate no = area.first().at(0).value<QGeoCoordinate>();
    QVERIFY2(qAbs(no.latitude() - esperado.latitude()) < 1e-9,
             "La esquina del area no coincide con el punto pulsado");
}

//! Simula pulsar y soltar en un punto.
static void clic(MapView *v, const QPoint &p,
                 Qt::MouseButton boton = Qt::LeftButton)
{
    QMouseEvent pulsa = mouseEvent(QEvent::MouseButtonPress, p, boton, boton);
    QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease, p, boton,
                                    Qt::NoButton);
    QCoreApplication::sendEvent(v, &pulsa);
    QCoreApplication::sendEvent(v, &suelta);
}

void TstMapWidget::drawsAPointWithOneClick()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    w.setCenter(QGeoCoordinate(23.1136, -82.3666));

    auto *v = qobject_cast<MapView *>(w.customPlot());
    w.setActiveFeatureLayer(QStringLiteral("puntos"));
    w.setDraftType(QStringLiteral("punto_interes"));
    w.setActiveTool(MapTool::DrawPoint);

    QSignalSpy creadas(&w, &MapWidget::featureCreated);

    const QPoint donde(400, 300);
    const QGeoCoordinate esperado = v->coordinateAt(donde);
    clic(v, donde);

    QCOMPARE(creadas.count(), 1);
    QCOMPARE(w.featureCount(), 1);

    const auto f = w.features().first();
    QCOMPARE(f.kind, GeometryKind::Point);
    QCOMPARE(f.layerId, QStringLiteral("puntos"));
    QCOMPARE(f.type, QStringLiteral("punto_interes"));
    // Anclado donde se pulso, no donde se solto.
    QVERIFY(qAbs(f.position().latitude() - esperado.latitude()) < 1e-9);
    QVERIFY(qAbs(f.position().longitude() - esperado.longitude()) < 1e-9);
}

void TstMapWidget::drawsAPolygonClickByClick()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);

    auto *v = qobject_cast<MapView *>(w.customPlot());
    w.setActiveFeatureLayer(QStringLiteral("zonas"));
    w.setActiveTool(MapTool::DrawPolygon);

    const QVector<QPoint> vertices{{200, 200}, {500, 200}, {500, 450}, {250, 420}};
    for (const QPoint &p : vertices) {
        clic(v, p);
        QVERIFY(w.isDrawing());
        // Mientras se traza NO entra en el modelo: si el usuario cancela, no
        // hay que limpiar nada.
        QCOMPARE(w.featureCount(), 0);
    }

    const qint64 id = w.finishDrawing();
    QVERIFY(id > 0);
    QVERIFY(!w.isDrawing());
    QCOMPARE(w.featureCount(), 1);

    const auto f = w.feature(id);
    QVERIFY(f.has_value());
    QCOMPARE(f->kind, GeometryKind::Polygon);
    QCOMPARE(f->geometry.size(), 4);
    QCOMPARE(f->layerId, QStringLiteral("zonas"));

    // Cada vertice donde se pincho.
    for (int i = 0; i < vertices.size(); ++i) {
        const QGeoCoordinate esperado = v->coordinateAt(vertices[i]);
        QVERIFY(qAbs(f->geometry[i].latitude() - esperado.latitude()) < 1e-9);
    }
}

void TstMapWidget::escapeCancelsTheDrawing()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    w.setActiveTool(MapTool::DrawPolygon);

    QSignalSpy canceladas(&w, &MapWidget::drawingCancelled);

    clic(v, QPoint(200, 200));
    clic(v, QPoint(400, 200));
    QVERIFY(w.isDrawing());

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &esc);

    QVERIFY(!w.isDrawing());
    QCOMPARE(canceladas.count(), 1);
    QCOMPARE(w.featureCount(), 0);
}

void TstMapWidget::backspaceUndoesTheLastVertex()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    w.setActiveTool(MapTool::DrawPolygon);

    for (const QPoint &p : {QPoint(200, 200), QPoint(400, 200),
                            QPoint(400, 400), QPoint(200, 400)})
        clic(v, p);

    QKeyEvent supr(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &supr);
    QVERIFY(w.isDrawing());

    const qint64 id = w.finishDrawing();
    QVERIFY(id > 0);
    // Se quito el ultimo vertice: quedan tres.
    QCOMPARE(w.feature(id)->geometry.size(), 3);
}

void TstMapWidget::selectsAndDragsAVertex()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    // Se traza un poligono y despues se edita.
    w.setActiveTool(MapTool::DrawPolygon);
    const QVector<QPoint> vertices{{200, 200}, {500, 200}, {500, 450}};
    for (const QPoint &p : vertices)
        clic(v, p);
    const qint64 id = w.finishDrawing();
    QVERIFY(id > 0);

    w.setActiveTool(MapTool::EditFeature);

    // Un clic dentro lo selecciona.
    clic(v, QPoint(400, 300));
    QCOMPARE(w.selectedFeature(), id);

    // Ahora se arrastra el tirador del segundo vertice.
    const QPoint desde = vertices[1];
    const QPoint hasta(560, 260);
    const QGeoCoordinate destino = v->coordinateAt(hasta);

    QMouseEvent pulsa = mouseEvent(QEvent::MouseButtonPress, desde,
                                   Qt::LeftButton, Qt::LeftButton);
    QMouseEvent mueve = mouseEvent(QEvent::MouseMove, hasta,
                                   Qt::NoButton, Qt::LeftButton);
    QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease, hasta,
                                    Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(v, &pulsa);
    QCoreApplication::sendEvent(v, &mueve);
    QCoreApplication::sendEvent(v, &suelta);

    const auto f = w.feature(id);
    QVERIFY(qAbs(f->geometry[1].latitude() - destino.latitude()) < 1e-6);
    QVERIFY(qAbs(f->geometry[1].longitude() - destino.longitude()) < 1e-6);

    // Los otros vertices no se movieron.
    const QGeoCoordinate primero = v->coordinateAt(vertices[0]);
    QVERIFY(qAbs(f->geometry[0].latitude() - primero.latitude()) < 1e-6);
}

void TstMapWidget::movesAWholeFeature()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    w.setActiveTool(MapTool::DrawPolygon);
    for (const QPoint &p : {QPoint(200, 200), QPoint(500, 200), QPoint(500, 450)})
        clic(v, p);
    const qint64 id = w.finishDrawing();

    const auto antes = w.feature(id)->geometry;

    w.setActiveTool(MapTool::EditFeature);
    // Se pulsa en el interior, lejos de cualquier tirador.
    const QPoint dentro(400, 300);
    const QPoint hasta(430, 330);

    QMouseEvent pulsa = mouseEvent(QEvent::MouseButtonPress, dentro,
                                   Qt::LeftButton, Qt::LeftButton);
    QMouseEvent mueve = mouseEvent(QEvent::MouseMove, hasta,
                                   Qt::NoButton, Qt::LeftButton);
    QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease, hasta,
                                    Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(v, &pulsa);
    QCoreApplication::sendEvent(v, &mueve);
    QCoreApplication::sendEvent(v, &suelta);

    const auto despues = w.feature(id)->geometry;
    QCOMPARE(despues.size(), antes.size());

    // TODOS los vertices se desplazaron lo mismo: la figura no se deforma.
    const double dLat = despues[0].latitude() - antes[0].latitude();
    const double dLon = despues[0].longitude() - antes[0].longitude();
    QVERIFY(qAbs(dLat) > 1e-9);
    for (int i = 1; i < antes.size(); ++i) {
        QVERIFY(qAbs((despues[i].latitude() - antes[i].latitude()) - dLat) < 1e-9);
        QVERIFY(qAbs((despues[i].longitude() - antes[i].longitude()) - dLon) < 1e-9);
    }
}

void TstMapWidget::deleteRemovesTheSelectedFeature()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    w.setActiveTool(MapTool::DrawPoint);
    clic(v, QPoint(400, 300));
    QCOMPARE(w.featureCount(), 1);
    const qint64 id = w.features().first().id;

    w.setActiveTool(MapTool::EditFeature);
    clic(v, QPoint(400, 300));
    QCOMPARE(w.selectedFeature(), id);

    QKeyEvent supr(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &supr);

    QCOMPARE(w.featureCount(), 0);
    QCOMPARE(w.selectedFeature(), qint64(-1));
}

void TstMapWidget::switchingToolDiscardsTheDraft()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    w.setActiveTool(MapTool::DrawPolygon);
    clic(v, QPoint(200, 200));
    clic(v, QPoint(400, 200));
    QVERIFY(w.isDrawing());

    // Cambiar de herramienta abandona lo que hubiera a medias. Si no, el
    // trazado reaparecia al volver a la herramienta.
    w.setActiveTool(MapTool::None);
    QVERIFY(!w.isDrawing());
    QCOMPARE(w.featureCount(), 0);

    w.setActiveTool(MapTool::DrawPolygon);
    QVERIFY(!w.isDrawing());
}

void TstMapWidget::undoAndRedoRestoreTheModel()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    QVERIFY(!w.canUndo());

    w.setActiveTool(MapTool::DrawPoint);
    clic(v, QPoint(300, 250));
    clic(v, QPoint(400, 300));
    QCOMPARE(w.featureCount(), 2);
    QVERIFY(w.canUndo());

    QVERIFY(w.undo());
    QCOMPARE(w.featureCount(), 1);
    QVERIFY(w.canRedo());

    QVERIFY(w.undo());
    QCOMPARE(w.featureCount(), 0);
    QVERIFY(!w.canUndo());

    QVERIFY(w.redo());
    QCOMPARE(w.featureCount(), 1);
    QVERIFY(w.redo());
    QCOMPARE(w.featureCount(), 2);
    QVERIFY(!w.canRedo());

    // Un cambio nuevo invalida la pila de rehacer.
    QVERIFY(w.undo());
    clic(v, QPoint(500, 350));
    QVERIFY(!w.canRedo());
}

void TstMapWidget::draggingIsOneUndoStep()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    w.setActiveTool(MapTool::DrawPolygon);
    for (const QPoint &p : {QPoint(200, 200), QPoint(500, 200), QPoint(500, 450)})
        clic(v, p);
    const qint64 id = w.finishDrawing();
    w.clearUndoHistory();

    const auto antes = w.feature(id)->geometry;

    w.setActiveTool(MapTool::EditFeature);
    clic(v, QPoint(400, 300));
    w.clearUndoHistory();

    // Arrastre con VARIOS movimientos intermedios.
    QMouseEvent pulsa = mouseEvent(QEvent::MouseButtonPress, QPoint(500, 200),
                                   Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(v, &pulsa);
    for (int i = 1; i <= 10; ++i) {
        QMouseEvent mueve = mouseEvent(QEvent::MouseMove,
                                       QPoint(500 + i * 6, 200 + i * 4),
                                       Qt::NoButton, Qt::LeftButton);
        QCoreApplication::sendEvent(v, &mueve);
    }
    QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease, QPoint(560, 240),
                                    Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(v, &suelta);

    QVERIFY(w.feature(id)->geometry[1].latitude() != antes[1].latitude());

    // Un solo deshacer devuelve el vertice a su sitio: los diez movimientos
    // intermedios son UN paso, no diez.
    QVERIFY(w.undo());
    const auto despues = w.feature(id)->geometry;
    for (int i = 0; i < antes.size(); ++i) {
        QVERIFY(qAbs(despues[i].latitude() - antes[i].latitude()) < 1e-9);
        QVERIFY(qAbs(despues[i].longitude() - antes[i].longitude()) < 1e-9);
    }
    QVERIFY(!w.canUndo());
}

void TstMapWidget::savesAndLoadsFeatures()
{
    const QString bd = m_dir.filePath(QStringLiteral("entidades.db"));
    QFile::remove(bd);

    qint64 idOriginal = -1;
    {
        MapWidget w(baseConfig(m_jsonPath));
        QVERIFY(w.isReady());
        w.resize(800, 600);
        w.setZoom(12);
        auto *v = qobject_cast<MapView *>(w.customPlot());

        w.addFeatureLayer(QStringLiteral("zonas"), QStringLiteral("Zonas"), 5);
        w.setActiveFeatureLayer(QStringLiteral("zonas"));
        w.setDraftType(QStringLiteral("zona_prohibida"));

        w.setActiveTool(MapTool::DrawPolygon);
        for (const QPoint &p : {QPoint(200, 200), QPoint(500, 200),
                                QPoint(500, 450), QPoint(220, 430)})
            clic(v, p);
        idOriginal = w.finishDrawing();
        QVERIFY(idOriginal > 0);

        // Un punto con atributos del dominio, que la libreria no interpreta.
        MapFeature pt;
        pt.kind = GeometryKind::Point;
        pt.layerId = QStringLiteral("puntos");
        pt.type = QStringLiteral("punto_interes");
        pt.name = QStringLiteral("O'Brien \"el rapido\"");
        pt.geometry = {QGeoCoordinate(23.1, -82.3)};
        pt.attributes[QStringLiteral("techo_m")] = 120;
        pt.attributes[QStringLiteral("responsable")] = QStringLiteral("CID3");
        pt.style.lineColor = QColor(0xf5, 0x7c, 0x00);
        QVERIFY(w.addFeature(pt) > 0);

        w.setFeatureLayerVisible(QStringLiteral("puntos"), false);

        QVERIFY2(w.saveFeaturesTo(bd), qPrintable(bd));
    }

    // Widget nuevo: se carga desde cero.
    MapWidget w2(baseConfig(m_jsonPath));
    QVERIFY(w2.isReady());
    w2.resize(800, 600);
    QVERIFY(w2.loadFeaturesFrom(bd));

    QCOMPARE(w2.featureCount(), 2);

    const auto zonas = w2.featuresInLayer(QStringLiteral("zonas"));
    QCOMPARE(zonas.size(), 1);
    QCOMPARE(zonas.first().kind, GeometryKind::Polygon);
    QCOMPARE(zonas.first().geometry.size(), 4);
    QCOMPARE(zonas.first().type, QStringLiteral("zona_prohibida"));

    const auto puntos = w2.featuresOfType(QStringLiteral("punto_interes"));
    QCOMPARE(puntos.size(), 1);
    // Comillas y apostrofes sobreviven al viaje.
    QCOMPARE(puntos.first().name, QStringLiteral("O'Brien \"el rapido\""));
    // Los atributos del dominio tambien.
    QCOMPARE(puntos.first().attributes.value(QStringLiteral("techo_m")).toInt(), 120);
    QCOMPARE(puntos.first().attributes.value(QStringLiteral("responsable")).toString(),
             QStringLiteral("CID3"));
    QCOMPARE(puntos.first().style.lineColor, QColor(0xf5, 0x7c, 0x00));

    // La visibilidad de la capa se conserva.
    bool encontrada = false;
    for (const LayerInfo &c : w2.featureLayers())
        if (c.id == QStringLiteral("puntos")) {
            encontrada = true;
            QVERIFY(!c.visible);
        }
    QVERIFY(encontrada);
}

void TstMapWidget::loadsGeoFileAsPolygonLayer()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    // Un .geo cerrado (triangulo): lon,lat por linea, terminador 0,0. El
    // ultimo vertice repite el primero, como en los ficheros reales.
    const QString ruta = m_dir.filePath(QStringLiteral("mini.geo"));
    QFile f(ruta);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&f) << "-82.0,23.0,\n-81.0,23.0,\n-81.5,23.5,\n"
                       "-82.0,23.0,\n0.0,0.0\n";
    f.close();

    const int antes = w.featureCount();
    QString error;
    const qint64 id = w.loadGeoAsLayer(ruta, QStringLiteral("aguas"),
                                       QStringLiteral("Aguas"), FeatureStyle(),
                                       &error);
    QVERIFY2(id > 0, qPrintable(error));
    QCOMPARE(w.featureCount(), antes + 1);

    const auto feat = w.feature(id);
    QVERIFY(feat.has_value());
    QCOMPARE(feat->kind, GeometryKind::Polygon);
    QVERIFY(!feat->isMultiPart());           // un solo trazado
    QCOMPARE(feat->geometry.size(), 3);      // se quita el vertice de cierre

    bool hayCapa = false;
    for (const LayerInfo &c : w.featureLayers())
        if (c.id == QStringLiteral("aguas"))
            hayCapa = true;
    QVERIFY(hayCapa);
}

void TstMapWidget::loadsGeoWithSeveralPolylines()
{
    // Un .geo con VARIOS trazados (0,0 separa) entra como UNA sola entidad
    // multi-parte. El lector no debe pararse en el primer separador.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    const QString ruta = m_dir.filePath(QStringLiteral("corredores.geo"));
    QFile f(ruta);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&f) << "-81.4,23.0,\n-81.5,21.6,\n0.0,0.0\n"
                       "-81.2,23.0,\n-81.4,21.5,\n0.0,0.0\n";
    f.close();

    const int antes = w.featureCount();
    QString error;
    const qint64 id = w.loadGeoAsLayer(ruta, QStringLiteral("corredores"),
                                       QStringLiteral("Corredores"),
                                       FeatureStyle(), &error);
    QVERIFY2(id > 0, qPrintable(error));
    QCOMPARE(w.featureCount(), antes + 1);   // UNA entidad, no dos

    const auto feat = w.feature(id);
    QVERIFY(feat.has_value());
    QCOMPARE(feat->kind, GeometryKind::Polyline);
    QVERIFY(feat->isMultiPart());
    QCOMPARE(feat->parts.size(), 2);         // dos partes
    QCOMPARE(feat->parts.first().size(), 2);
}

void TstMapWidget::savesAndLoadsMultiPartFeature()
{
    // Una entidad multi-parte tiene que sobrevivir a guardar y cargar.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    MapFeature f;
    f.layerId = QStringLiteral("divisiones");
    f.name = QStringLiteral("Ejercitos");
    f.kind = GeometryKind::Polyline;
    f.geometry = { QGeoCoordinate(23.0, -82.0), QGeoCoordinate(23.1, -82.1) };
    f.parts = {
        { QGeoCoordinate(23.0, -82.0), QGeoCoordinate(23.1, -82.1) },
        { QGeoCoordinate(22.0, -80.0), QGeoCoordinate(22.1, -80.1),
          QGeoCoordinate(22.2, -80.0) }
    };
    const qint64 id = w.addFeature(f);
    QVERIFY(id > 0);

    const QString db = m_dir.filePath(QStringLiteral("multi.db"));
    QVERIFY(w.saveFeaturesTo(db));

    w.clearFeatures();
    QCOMPARE(w.featureCount(), 0);
    QVERIFY(w.loadFeaturesFrom(db));
    QCOMPARE(w.featureCount(), 1);

    // Se localiza la entidad cargada (el id puede cambiar).
    const auto cargada = w.features().first();
    QVERIFY(cargada.isMultiPart());
    QCOMPARE(cargada.parts.size(), 2);
    QCOMPARE(cargada.parts.at(0).size(), 2);
    QCOMPARE(cargada.parts.at(1).size(), 3);
}

void TstMapWidget::drawsManyMovingTargets()
{
    // El requisito de 250 objetivos simultaneos: se comprueba que la tuberia
    // de dibujo los pinta sin caerse. grab() fuerza el repintado completo,
    // que recorre TargetLayer::draw con los 250 objetivos y sus trazas.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);

    const int N = 250;
    QVector<qint64> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        MapTarget t;
        t.position = QGeoCoordinate(20.0 + i * 0.01, -84.0 + i * 0.004);
        t.label = QStringLiteral("T%1\nRbo 045\nVel 12").arg(i);  // multilinea
        const qint64 id = w.addTarget(t);
        QVERIFY(id > 0);
        ids.append(id);
    }
    QCOMPARE(w.targetCount(), N);

    // Varias rondas de actualizacion, como el flujo en tiempo real.
    for (int paso = 1; paso <= 5; ++paso)
        for (int i = 0; i < N; ++i)
            QVERIFY(w.updateTarget(ids[i],
                QGeoCoordinate(20.0 + i * 0.01 + paso * 0.002, -84.0 + i * 0.004),
                45.0));

    const QPixmap px = w.grab();       // fuerza el render completo
    QVERIFY(!px.isNull());

    QVERIFY(w.removeTarget(ids.first()));
    QCOMPARE(w.targetCount(), N - 1);
    w.clearTargets();
    QCOMPARE(w.targetCount(), 0);
}

void TstMapWidget::usesTargetSymbolProvider()
{
    // La simbologia la pone la app por un hook: la libreria llama al proveedor
    // por cada objetivo visible al dibujar. Se comprueba que se invoca (con el
    // objetivo completo, para poder elegir por kind) y que el render no revienta.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(400, 300);
    w.setZoom(10);
    const QGeoCoordinate centro(22.0, -79.5);
    w.setCenter(centro);

    MapTarget t;
    t.position = centro;                      // en el centro: pasa el culling
    t.kind = QStringLiteral("uav");
    const qint64 id = w.addTarget(t);
    QVERIFY(id > 0);

    QPixmap icono(16, 16);
    icono.fill(Qt::red);
    int llamadas = 0;
    QString kindVisto;
    w.setTargetSymbolProvider([&](const MapTarget &obj) {
        ++llamadas;
        kindVisto = obj.kind;                 // la app recibe el objetivo completo
        TargetSymbol s;
        if (obj.kind == QStringLiteral("uav"))
            s.icon = icono;                   // icono por tipo
        s.rotateWithHeading = true;
        return s;
    });

    const QPixmap px = w.grab();              // fuerza el render -> TargetLayer::draw
    QVERIFY(!px.isNull());
    QVERIFY2(llamadas > 0, "El proveedor de simbolo no se invoco al dibujar");
    QCOMPARE(kindVisto, QStringLiteral("uav"));
}

void TstMapWidget::scalesToThousandsOfTargets()
{
    // Requisito del alcance: miles de objetivos (ADS-B regional) sin ahogarse.
    // El motor dibuja siempre el simbolo pero, por encima de un presupuesto,
    // APAGA etiquetas y trazas (lo mas caro y lo que a esa densidad se vuelve
    // una mancha ilegible). Se comprueba ese nivel de detalle de forma
    // determinista y se mide el tiempo de un render completo.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.0, -79.5));

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);
    TargetLayer *capa = vista->targetLayer();
    QVERIFY(capa != nullptr);

    // Rejilla de ~3000 objetivos dentro del area visible, con etiqueta y, tras
    // una actualizacion, una traza de 2 puntos.
    const int lado = 55;                       // 55*55 = 3025
    QVector<qint64> ids;
    ids.reserve(lado * lado);
    for (int i = 0; i < lado; ++i)
        for (int j = 0; j < lado; ++j) {
            MapTarget t;
            t.position = QGeoCoordinate(21.8 + i * (0.4 / lado),
                                        -79.8 + j * (0.6 / lado));
            t.label = QStringLiteral("T%1-%2").arg(i).arg(j);
            const qint64 id = w.addTarget(t);
            QVERIFY(id > 0);
            ids.append(id);
        }
    // Un salto perceptible (>2 px a este zoom) para que la traza sobreviva a la
    // decimacion; en uso real el movimiento entre refrescos es de ese orden.
    for (qint64 id : ids) {
        const auto t = w.target(id);
        QVERIFY(w.updateTarget(id, QGeoCoordinate(t->position.latitude() + 0.02,
                                                  t->position.longitude())));
    }

    // La capa es BUFFERED: para forzar un repintado sincrono se replota su capa
    // (lo mismo que hace su temporizador), que ejecuta TargetLayer::draw ya.
    QCPLayer *buffer = capa->layer();
    QVERIFY(buffer != nullptr);

    // Densidad alta: con presupuestos pequenos no se dibuja ninguna etiqueta ni
    // traza, pero si todos los simbolos de los que caen en pantalla.
    w.setTargetDetailBudget(100, 100);
    QElapsedTimer reloj;
    reloj.start();
    buffer->replot();
    const qint64 ms = reloj.elapsed();

    const int visibles = capa->lastDrawnCount();
    QVERIFY2(visibles > 1000,
             qPrintable(QStringLiteral("solo %1 objetivos visibles; se esperaban "
                                       "miles").arg(visibles)));
    QCOMPARE(capa->lastLabelsDrawn(), 0);      // etiquetas apagadas por densidad
    QCOMPARE(capa->lastTrailsDrawn(), 0);      // trazas apagadas por densidad

    // Con presupuesto holgado vuelven, pero el declutter deja MENOS etiquetas
    // que objetivos (una por celda de pantalla), que es justo lo que evita la
    // mancha ilegible.
    w.setTargetDetailBudget(1000000, 1000000);
    buffer->replot();
    QVERIFY(capa->lastLabelsDrawn() > 0);
    QVERIFY2(capa->lastLabelsDrawn() < visibles,
             "El declutter no redujo las etiquetas respecto a los objetivos");
    QVERIFY(capa->lastTrailsDrawn() > 0);      // ahora si hay trazas

    qInfo() << "Render de" << visibles << "objetivos visibles en" << ms
            << "ms (nivel de detalle minimo);" << capa->lastLabelsDrawn()
            << "etiquetas tras declutter con presupuesto holgado";
}

void TstMapWidget::selectsTargetByClick()
{
    // "Clic en un objetivo -> sus datos": targetAt localiza el objetivo bajo el
    // pixel y, sin herramienta activa, un clic lo selecciona y emite targetClicked.
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(11);
    const QGeoCoordinate centro(22.0, -79.5);
    w.setCenter(centro);

    MapTarget t;
    t.position = centro;
    t.kind = QStringLiteral("buque");
    t.attributes.insert(QStringLiteral("mmsi"), QStringLiteral("224123000"));
    const qint64 id = w.addTarget(t);
    QVERIFY(id > 0);

    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);

    // Pixel exacto del objetivo (misma proyeccion que usa la capa).
    const QPointF ax = MapView::toAxis(centro);
    const QPoint px(int(std::lround(vista->xAxis->coordToPixel(ax.x()))),
                    int(std::lround(vista->yAxis->coordToPixel(ax.y()))));

    QCOMPARE(w.targetAt(px), id);                       // justo encima
    QVERIFY(w.targetAt(px + QPoint(200, 200)) < 0);     // lejos -> ninguno

    // Un clic sin herramienta selecciona y emite targetClicked (al pulsar).
    QSignalSpy clic(&w, &MapWidget::targetClicked);
    w.setActiveTool(MapTool::None);
    QMouseEvent pulsar = mouseEvent(QEvent::MouseButtonPress, px,
                                    Qt::LeftButton, Qt::LeftButton);
    QMouseEvent soltar = mouseEvent(QEvent::MouseButtonRelease, px,
                                    Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &pulsar);
    QCoreApplication::sendEvent(vista, &soltar);

    QCOMPARE(clic.count(), 1);
    QCOMPARE(clic.first().at(0).toLongLong(), id);
    QCOMPARE(w.selectedTarget(), id);

    // Deseleccionar desde codigo.
    w.setSelectedTarget(-1);
    QCOMPARE(w.selectedTarget(), qint64(-1));
}

// Un objetivo recorre una «L» con la traza dibujada limitada a 5 puntos: el
// historial conserva el origen y, al seleccionarlo, se dibuja entero (apagar el
// historial del seleccionado cambia la imagen).
void TstMapWidget::selectedTargetShowsFullHistory()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(9);
    w.setCenter(QGeoCoordinate(22.0, -79.5));
    w.setTargetTrailLength(5);

    MapTarget t;
    t.position = QGeoCoordinate(21.8, -79.9);
    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1700000000000LL).toUTC();
    const qint64 id = w.addTarget(t, t0);
    for (int i = 1; i <= 40; ++i)                       // hacia el este...
        QVERIFY(w.updateTarget(id, QGeoCoordinate(21.8, -79.9 + i * 0.01), 90.0, t0.addSecs(i)));
    for (int i = 1; i <= 40; ++i)                       // ...y luego al norte
        QVERIFY(w.updateTarget(id, QGeoCoordinate(21.8 + i * 0.01, -79.5), 0.0, t0.addSecs(40 + i)));

    QCOMPARE(w.targetOrigin(id).position, QGeoCoordinate(21.8, -79.9));
    QCOMPARE(w.targetOrigin(id).time, t0);
    QCOMPARE(w.targetHistory(id).size(), 81);

    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(50);                               // la capa agrupa repintados (~30 fps)
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };
    w.setSelectedTarget(id);
    const QImage conHistorial = captura();
    w.setSelectedTargetHistoryVisible(false);
    const QImage sinHistorial = captura();
    QVERIFY(conHistorial != sinHistorial);
    w.setSelectedTargetHistoryVisible(true);

    w.removeTarget(id);
    QVERIFY(w.targetHistory(id).isEmpty());
}

// Una zona vigilada avisa de quien entra y sale: al empezar (los que ya estan),
// al moverse los objetivos y al cambiar la forma de la zona, incluso por
// deshacer. Borrar un objetivo o la zona no genera salidas.
void TstMapWidget::zoneAlertsFollowTargetsAndZoneEdits()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    MapFeature zona;
    zona.kind = GeometryKind::Polygon;
    zona.type = QStringLiteral("zona_prohibida");
    zona.geometry = { QGeoCoordinate(22.5, -82.5), QGeoCoordinate(22.5, -81.5),
                      QGeoCoordinate(23.5, -81.5), QGeoCoordinate(23.5, -82.5) };
    const qint64 zonaId = w.addFeature(zona);
    QVERIFY(zonaId > 0);

    MapTarget t;
    t.position = QGeoCoordinate(23.0, -82.0);          // dentro
    const qint64 a = w.addTarget(t);
    t.position = QGeoCoordinate(25.0, -82.0);          // fuera
    const qint64 b = w.addTarget(t);

    QSignalSpy entra(&w, &MapWidget::targetEnteredZone);
    QSignalSpy sale(&w, &MapWidget::targetLeftZone);

    // Un punto no es una zona.
    MapFeature punto;
    punto.geometry = { QGeoCoordinate(23.0, -82.0) };
    QVERIFY(!w.watchZone(w.addFeature(punto)));
    QVERIFY(!w.watchZone(999999));

    // Vigilar avisa del que ya esta dentro.
    QVERIFY(w.watchZone(zonaId));
    QVERIFY(w.isZoneWatched(zonaId));
    QCOMPARE(w.watchedZones(), QVector<qint64>({zonaId}));
    QCOMPARE(entra.count(), 1);
    QCOMPARE(entra.at(0).at(0).toLongLong(), a);
    QCOMPARE(entra.at(0).at(1).toLongLong(), zonaId);
    QCOMPARE(w.targetsInZone(zonaId), QVector<qint64>({a}));

    // b entra, a sale.
    QVERIFY(w.updateTarget(b, QGeoCoordinate(22.8, -81.8)));
    QCOMPARE(entra.count(), 2);
    QCOMPARE(entra.at(1).at(0).toLongLong(), b);
    QVERIFY(w.updateTarget(a, QGeoCoordinate(24.0, -82.0)));
    QCOMPARE(sale.count(), 1);
    QCOMPARE(sale.at(0).at(0).toLongLong(), a);
    QCOMPARE(w.zonesOfTarget(b), QVector<qint64>({zonaId}));
    QVERIFY(w.zonesOfTarget(a).isEmpty());

    // La zona se encoge al norte (sin b): b sale. Deshacer la devuelve: b entra.
    QVERIFY(w.moveVertex(zonaId, 0, QGeoCoordinate(23.0, -82.5)));
    QVERIFY(w.moveVertex(zonaId, 1, QGeoCoordinate(23.0, -81.5)));
    QCOMPARE(sale.count(), 2);
    QCOMPARE(sale.at(1).at(0).toLongLong(), b);
    QVERIFY(w.undo());
    QVERIFY(w.undo());
    QCOMPARE(entra.count(), 3);
    QCOMPARE(entra.at(2).at(0).toLongLong(), b);

    // Borrar el objetivo no es "salir".
    QVERIFY(w.removeTarget(b));
    QCOMPARE(sale.count(), 2);
    QVERIFY(w.targetsInZone(zonaId).isEmpty());

    // Borrar la zona la deja de vigilar, sin salidas.
    QVERIFY(w.updateTarget(a, QGeoCoordinate(23.0, -82.0)));
    QCOMPARE(entra.count(), 4);
    QVERIFY(w.removeFeature(zonaId));
    QVERIFY(!w.isZoneWatched(zonaId));
    QCOMPARE(sale.count(), 2);
    QVERIFY(w.updateTarget(a, QGeoCoordinate(26.0, -82.0)));
    QCOMPARE(sale.count(), 2);
}

// El caso del demo: el operador coloca un «Objetivo» (entidad punto, no un
// objetivo movil) dentro de un «Área objetivo». Debe avisar al vigilar, al
// crear o mover puntos y al deshacer; no al borrar. Y no por la señal de
// objetivos moviles.
void TstMapWidget::zoneAlertsCoverPointFeatures()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    MapFeature area;
    area.kind = GeometryKind::Polygon;
    area.type = QStringLiteral("objetivo_area");
    area.geometry = { QGeoCoordinate(22.5, -82.5), QGeoCoordinate(22.5, -81.5),
                      QGeoCoordinate(23.5, -81.5), QGeoCoordinate(23.5, -82.5) };
    const qint64 zona = w.addFeature(area);

    MapFeature punto;
    punto.type = QStringLiteral("objetivo");
    punto.geometry = { QGeoCoordinate(23.0, -82.0) };       // dentro
    const qint64 dentro = w.addFeature(punto);
    punto.geometry = { QGeoCoordinate(25.0, -82.0) };       // fuera
    const qint64 fuera = w.addFeature(punto);

    QSignalSpy entra(&w, &MapWidget::featureEnteredZone);
    QSignalSpy sale(&w, &MapWidget::featureLeftZone);
    QSignalSpy entraObjetivo(&w, &MapWidget::targetEnteredZone);

    // Vigilar avisa del punto que ya estaba dentro (y solo por la señal de entidades).
    QVERIFY(w.watchZone(zona));
    QCOMPARE(entra.count(), 1);
    QCOMPARE(entra.at(0).at(0).toLongLong(), dentro);
    QCOMPARE(entra.at(0).at(1).toLongLong(), zona);
    QCOMPARE(entraObjetivo.count(), 0);
    QCOMPARE(w.featuresInZone(zona), QVector<qint64>({dentro}));
    QVERIFY(w.featuresInZone(zona).indexOf(zona) < 0);      // la zona no es un punto

    // El resalte (activo por defecto) se ve en el mapa: apagarlo cambia la imagen.
    w.resize(600, 450);
    w.setZoom(8);
    w.setCenter(QGeoCoordinate(23.0, -82.0));
    QVERIFY(w.isZoneAlertHighlight());
    QCoreApplication::processEvents();
    const QImage conResalte = w.grab().toImage();
    w.setZoneAlertHighlight(false);
    QCoreApplication::processEvents();
    QVERIFY(w.grab().toImage() != conResalte);
    w.setZoneAlertHighlight(true);

    // Crear un punto dentro avisa al momento.
    punto.geometry = { QGeoCoordinate(22.8, -81.8) };
    const qint64 nuevo = w.addFeature(punto);
    QCOMPARE(entra.count(), 2);
    QCOMPARE(entra.at(1).at(0).toLongLong(), nuevo);

    // Arrastrar (moveVertex, lo que hace el raton) el de fuera hacia dentro.
    QVERIFY(w.moveVertex(fuera, 0, QGeoCoordinate(23.2, -82.2)));
    QCOMPARE(entra.count(), 3);
    QCOMPARE(entra.at(2).at(0).toLongLong(), fuera);
    QCOMPARE(w.zonesOfFeature(fuera), QVector<qint64>({zona}));

    // Deshacer el arrastre lo devuelve fuera: sale.
    QVERIFY(w.undo());
    QCOMPARE(sale.count(), 1);
    QCOMPARE(sale.at(0).at(0).toLongLong(), fuera);

    // Borrar un punto que esta dentro no es «salir».
    QVERIFY(w.removeFeature(nuevo));
    QCOMPARE(sale.count(), 1);
    QCOMPARE(w.featuresInZone(zona), QVector<qint64>({dentro}));

    // Dejar de vigilar: mover puntos ya no avisa.
    QVERIFY(w.unwatchZone(zona));
    QVERIFY(w.moveVertex(dentro, 0, QGeoCoordinate(30.0, -70.0)));
    QCOMPARE(sale.count(), 1);
}

// La escala y la cuadricula no cambian nada hasta encenderlas; encendidas,
// pintan algo en la vista. La resolucion en el centro se mide de verdad: a un
// zoom mas, la mitad de metros por pixel.
void TstMapWidget::scaleBarAndGraticuleDraw()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.0, -79.5));

    QVERIFY(!w.isScaleBarVisible());
    QVERIFY(!w.isGraticuleVisible());
    QCOMPARE(w.scaleBarUnits(), ScaleUnits::Metric);

    const double mpp10 = w.metersPerPixel();
    QVERIFY(mpp10 > 0.0);
    w.setZoom(11);
    const double mpp11 = w.metersPerPixel();
    QVERIFY2(qAbs(mpp11 / mpp10 - 0.5) < 0.02,
             qPrintable(QStringLiteral("%1 -> %2").arg(mpp10).arg(mpp11)));

    // MapWidget repinta en diferido (rpQueuedReplot): se procesan los eventos
    // antes de capturar para ver el estado nuevo.
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };
    const QImage base = captura();
    const QRect esquina(0, 600 - 60, 160, 60);   // donde va la escala

    w.setScaleBarVisible(true);
    QVERIFY(w.isScaleBarVisible());
    const QImage conEscala = captura();
    QVERIFY(conEscala.copy(esquina) != base.copy(esquina));

    w.setScaleBarUnits(ScaleUnits::Nautical);
    QCOMPARE(w.scaleBarUnits(), ScaleUnits::Nautical);
    QVERIFY(!captura().isNull());

    w.setScaleBarVisible(false);
    w.setGraticuleVisible(true);
    w.setGraticuleColor(QColor(255, 255, 255, 180));
    QCOMPARE(w.graticuleColor(), QColor(255, 255, 255, 180));
    QVERIFY(w.isGraticuleVisible());
    QVERIFY(captura() != base);

    // Cuadricula UTM: otro dibujo que la geografica, y vuelta atras.
    QCOMPARE(w.graticuleMode(), GraticuleMode::Geographic);
    const QImage geografica = captura();
    w.setGraticuleMode(GraticuleMode::Utm);
    QCOMPARE(w.graticuleMode(), GraticuleMode::Utm);
    QVERIFY(captura() != geografica);
    w.setZoom(4);                                   // varias zonas: solo limites
    QVERIFY(!captura().isNull());
    w.setGraticuleMode(GraticuleMode::Geographic);

    // A zoom mundial la cuadricula sigue dibujandose sin colgarse.
    w.setZoom(2);
    QVERIFY(!captura().isNull());
}

// Un GeoJSON de otra herramienta entra como capa (añadiendo, no reemplazando);
// esa capa sale a KML, GPX y GeoJSON y vuelve a entrar con las mismas
// entidades. Errores (fichero que no existe, capa vacia) se informan.
void TstMapWidget::importsAndExportsVectorFiles()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString origen = dir.filePath(QStringLiteral("zonas.geojson"));
    {
        QFile f(origen);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"type":"FeatureCollection","features":[
          {"type":"Feature","properties":{"name":"Z1","nivel":"alto"},
           "geometry":{"type":"Polygon","coordinates":[[[-82,23],[-81,23],[-81,24],[-82,23]]]}},
          {"type":"Feature","properties":{"name":"P1"},
           "geometry":{"type":"Point","coordinates":[-80,22]}}]})");
    }

    QString err;
    const qint64 primero = w.importVectorFile(origen, QStringLiteral("importada"),
                                              QStringLiteral("Importada"), &err);
    QVERIFY2(primero > 0, qPrintable(err));
    QCOMPARE(w.featuresInLayer(QStringLiteral("importada")).size(), 2);
    const auto z1 = w.feature(primero);
    QVERIFY(z1.has_value());
    QCOMPARE(z1->name, QStringLiteral("Z1"));
    QCOMPARE(z1->attributes.value(QStringLiteral("nivel")).toString(), QStringLiteral("alto"));

    // Importar otra vez AÑADE a la misma capa.
    QVERIFY(w.importVectorFile(origen, QStringLiteral("importada")) > 0);
    QCOMPARE(w.featuresInLayer(QStringLiteral("importada")).size(), 4);

    for (const QString &ext : { QStringLiteral("kml"), QStringLiteral("gpx"), QStringLiteral("geojson") }) {
        const QString salida = dir.filePath(QStringLiteral("salida.") + ext);
        QVERIFY2(w.exportVectorFile(salida, QStringLiteral("importada"), &err), qPrintable(err));
        const QString capa = QStringLiteral("vuelta_") + ext;
        QVERIFY(w.importVectorFile(salida, capa) > 0);
        QCOMPARE(w.featuresInLayer(capa).size(), 4);
    }

    QVERIFY(w.importVectorFile(dir.filePath(QStringLiteral("no_existe.kml")),
                               QStringLiteral("x"), QString(), &err) < 0);
    QVERIFY(!err.isEmpty());
    QVERIFY(!w.exportVectorFile(dir.filePath(QStringLiteral("vacia.kml")),
                                QStringLiteral("capa_sin_nada"), &err));
}

static MapFeature puntoDe(const QString &nombre, double lat, double lon);

// El historial completo de cada objetivo sale a GPX con su hora y vuelve igual.
void TstMapWidget::exportsTargetHistoryToGpx()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = dir.filePath(QStringLiteral("historial.gpx"));
    QString err;
    QVERIFY(!w.exportTargetHistory(ruta, {}, &err));      // sin objetivos: nada
    QVERIFY(!err.isEmpty());
    QVERIFY(!QFile::exists(ruta));

    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760419413000LL).toUTC();
    MapTarget a;
    a.label = QStringLiteral("Buque A\nrumbo 090");
    a.kind = QStringLiteral("buque");
    a.position = QGeoCoordinate(23.0, -82.0);
    const qint64 ida = w.addTarget(a, t0);
    for (int i = 1; i <= 10; ++i)
        w.updateTarget(ida, QGeoCoordinate(23.0, -82.0 + i * 0.001), 90.0, t0.addSecs(i * 10));
    MapTarget b;
    b.position = QGeoCoordinate(22.0, -80.0);
    const qint64 idb = w.addTarget(b, t0.addSecs(3));

    QVERIFY2(w.exportTargetHistory(ruta, {}, &err), qPrintable(err));
    QFile f(ruta);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QVector<GpxTrack> tracks = parseGpxTracks(f.readAll(), &err);
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks[0].targetId, ida);
    QCOMPARE(tracks[0].name, QStringLiteral("Buque A"));
    QCOMPARE(tracks[0].type, QStringLiteral("buque"));
    QCOMPARE(tracks[0].points.size(), 11);
    QCOMPARE(tracks[0].points.first().time, t0);           // el origen, con su hora
    QCOMPARE(tracks[0].points.last().time, t0.addSecs(100));
    QCOMPARE(tracks[0].points.last().position.longitude(), -81.99);
    QCOMPARE(tracks[1].name, QStringLiteral("Objetivo %1").arg(idb));

    // Solo los pedidos.
    QVERIFY(w.exportTargetHistory(ruta, { idb }, &err));
    QFile g(ruta);
    QVERIFY(g.open(QIODevice::ReadOnly));
    QCOMPARE(parseGpxTracks(g.readAll()).size(), 1);
}

// La vista sale como imagen (del tamaño del widget por la escala, mas las
// bandas de titulo y pie) y como PDF; una extension desconocida se rechaza sin
// dejar fichero.
void TstMapWidget::exportsViewToImageAndPdf()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(640, 480);
    w.setZoom(8);
    w.setCenter(QGeoCoordinate(22.0, -79.5));
    MapFeature zona;
    zona.kind = GeometryKind::Polygon;
    zona.geometry = { QGeoCoordinate(21.8, -79.8), QGeoCoordinate(21.8, -79.2),
                      QGeoCoordinate(22.2, -79.2) };
    w.addFeature(zona);
    w.setScaleBarVisible(true);

    const QImage img = w.renderImage();
    QCOMPARE(img.size(), QSize(640, 480));
    QCOMPARE(w.renderImage(2.0).size(), QSize(1280, 960));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString err;
    MapExportOptions sinNada;
    sinNada.footer = false;
    const QString png = dir.filePath(QStringLiteral("vista.png"));
    QVERIFY2(w.exportView(png, sinNada, &err), qPrintable(err));
    QCOMPARE(QImage(png).size(), QSize(640, 480));

    MapExportOptions con;
    con.title = QStringLiteral("Sector norte");
    con.notes = QStringLiteral("Ejercicio 3");
    con.attribution = QStringLiteral("Datos de prueba");
    con.imageScale = 1.5;
    const QString jpg = dir.filePath(QStringLiteral("vista.jpg"));
    QVERIFY2(w.exportView(jpg, con, &err), qPrintable(err));
    const QImage conBandas(jpg);
    QCOMPARE(conBandas.width(), 960);
    QVERIFY(conBandas.height() > 720);                     // titulo y pie añadidos

    const QString pdf = dir.filePath(QStringLiteral("vista.pdf"));
    QVERIFY2(w.exportView(pdf, con, &err), qPrintable(err));
    QFile f(pdf);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray datos = f.readAll();
    QVERIFY(datos.startsWith("%PDF"));
    QVERIFY(datos.size() > 2000);

    QVERIFY(!w.exportView(dir.filePath(QStringLiteral("vista.xyz")), con, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("vista.xyz"))));
}

// targetsInArea / featuresInArea responden sin vigilar nada, y
// clearAreaSelection no rompe nada aunque no haya marca.
void TstMapWidget::areaQueriesFindTargetsAndFeatures()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    const QVector<QGeoCoordinate> zona = { QGeoCoordinate(22.0, -80.0), QGeoCoordinate(22.0, -79.0),
                                           QGeoCoordinate(23.0, -79.0), QGeoCoordinate(23.0, -80.0) };
    MapTarget t;
    t.position = QGeoCoordinate(22.5, -79.5);
    const qint64 dentro = w.addTarget(t);
    t.id = -1;
    t.position = QGeoCoordinate(24.0, -79.5);
    w.addTarget(t);
    QCOMPARE(w.targetsInArea(zona), QVector<qint64>({ dentro }));

    const qint64 punto = w.addFeature(puntoDe(QStringLiteral("P"), 22.2, -79.2));
    w.addFeature(puntoDe(QStringLiteral("Lejos"), 10.0, 10.0));
    MapFeature linea;
    linea.kind = GeometryKind::Polyline;
    linea.geometry = { QGeoCoordinate(21.0, -81.0), QGeoCoordinate(22.5, -79.5) };  // 1 vertice dentro
    const qint64 idLinea = w.addFeature(linea);
    QCOMPARE(w.featuresInArea(zona), QVector<qint64>({ punto, idLinea }));
    QVERIFY(w.watchedZones().isEmpty());                    // no se vigila nada

    QVERIFY(w.targetsInArea({ QGeoCoordinate(22.0, -80.0) }).isEmpty());   // no es poligono
    w.clearAreaSelection();
}

// Dos objetivos a 6 km en rumbo de colision. Con velocidad dada por la app
// (setTargetVelocity) la alerta salta al activarla; con la estimada por la
// traza, al moverse. Las lineas se dibujan, y apagar emite el fin.
void TstMapWidget::proximityAlertsWarnOfCloseApproach()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(11);
    w.setCenter(QGeoCoordinate(22.0, -79.97));

    QVector<std::pair<qint64, qint64>> empiezan, terminan;
    double cpaVisto = -1.0, tcpaVisto = -1.0;
    connect(&w, &MapWidget::proximityAlertRaised, this,
            [&](qint64 a, qint64 b, double cpa, double tcpa) {
                empiezan.append({ a, b });
                cpaVisto = cpa;
                tcpaVisto = tcpa;
            });
    connect(&w, &MapWidget::proximityAlertCleared, this,
            [&](qint64 a, qint64 b) { terminan.append({ a, b }); });

    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760000000000LL).toUTC();
    const QGeoCoordinate pa(22.0, -80.0);
    const QGeoCoordinate pb = pa.atDistanceAndAzimuth(6000.0, 90.0);
    MapTarget a;
    a.position = pa;
    const qint64 ida = w.addTarget(a, t0);
    MapTarget b;
    b.position = pb;
    const qint64 idb = w.addTarget(b, t0);
    MapTarget lejos;
    lejos.position = QGeoCoordinate(10.0, 10.0);
    w.addTarget(lejos, t0);

    // Velocidades dadas por la app: 5 m/s cada uno, de frente -> TCPA 600 s.
    QVERIFY(w.setTargetVelocity(ida, 5.0, 90.0));
    QVERIFY(w.setTargetVelocity(idb, 5.0, 270.0));
    const CpaResult c = w.targetCpa(ida, idb);
    QVERIFY(c.valid);
    QVERIFY(qAbs(c.timeSeconds - 600.0) < 1.0);
    // ~1 m: "6 km al este" por ortodromica baja ~1 m al sur a 22 grados.
    QVERIFY(c.distanceMeters < 5.0);
    QVERIFY(w.proximityAlerts().isEmpty());                 // apagadas: no avisa

    w.setProximityAlert(1852.0, 900.0);
    QCOMPARE(empiezan.size(), 1);
    QCOMPARE(empiezan[0], std::make_pair(ida, idb));
    QVERIFY(cpaVisto < 5.0 && qAbs(tcpaVisto - 600.0) < 1.0);
    QCOMPARE(w.proximityAlerts().size(), 1);
    QCOMPARE(w.proximityAlertDistance(), 1852.0);

    // Las lineas CPA se dibujan (y desaparecen al ocultarlas).
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };
    const QImage conLineas = captura();
    w.setProximityLinesVisible(false);
    QVERIFY(captura() != conLineas);
    w.setProximityLinesVisible(true);

    // B cae al sur: el CPA ya no baja de 1 NM y la alerta termina.
    QVERIFY(w.setTargetVelocity(idb, 5.0, 180.0));
    QCOMPARE(terminan.size(), 1);
    QVERIFY(w.proximityAlerts().isEmpty());

    // Sin velocidad dada: se estima por la traza. B vuelve a rumbo oeste a
    // 5 m/s durante 20 s (y A al este): vuelve a saltar.
    w.setTargetVelocity(ida, std::numeric_limits<double>::quiet_NaN(), 0.0);
    w.setTargetVelocity(idb, std::numeric_limits<double>::quiet_NaN(), 0.0);
    for (int s = 1; s <= 20; ++s) {
        w.updateTarget(ida, pa.atDistanceAndAzimuth(5.0 * s, 90.0), 90.0, t0.addSecs(s));
        w.updateTarget(idb, pb.atDistanceAndAzimuth(5.0 * s, 270.0), 270.0, t0.addSecs(s));
    }
    QCOMPARE(empiezan.size(), 2);
    QVERIFY(qAbs(w.proximityAlerts().first().cpa.timeSeconds - 580.0) < 3.0);

    // Borrar no emite fin; apagar si, para las que queden.
    w.setProximityAlert(0.0);
    QCOMPARE(terminan.size(), 2);
    QVERIFY(w.proximityAlerts().isEmpty());
    w.removeTarget(ida);
    QCOMPARE(terminan.size(), 2);
    QVERIFY(!w.targetCpa(ida, idb).valid);
}

// Anillos centrados en un objetivo y una marcacion de ese objetivo a otro: al
// moverse, la medida de la marcacion cambia sola. Se dibujan, y se quitan.
void TstMapWidget::rangeRingsAndBearingLinesFollowTargets()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.0, -80.0));
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };

    MapTarget a;
    a.position = QGeoCoordinate(22.0, -80.0);
    const qint64 ida = w.addTarget(a);
    MapTarget b;
    b.position = a.position.atDistanceAndAzimuth(9260.0, 45.0);      // 5 NM al NE
    const qint64 idb = w.addTarget(b);
    const QImage sinNada = captura();

    RangeRings r;
    r.targetId = ida;
    r.spacingMeters = 1852.0;
    r.count = 3;
    r.spokeEveryDeg = 30;
    const qint64 anillos = w.addRangeRings(r);
    QVERIFY(anillos > 0);
    const QImage conAnillos = captura();
    QVERIFY(conAnillos != sinNada);

    BearingLine l;
    l.fromTarget = ida;
    l.toTarget = idb;
    const qint64 marcacion = w.addBearingLine(l);
    QVERIFY(marcacion > 0 && marcacion != anillos);
    QCOMPARE(w.rangeMarks(), QVector<qint64>({ anillos, marcacion }));
    double rumbo = 0.0, dist = 0.0;
    QVERIFY(w.bearingLineMeasure(marcacion, &rumbo, &dist));
    QVERIFY(qAbs(rumbo - 45.0) < 0.1);
    QVERIFY(qAbs(dist - 9260.0) < 1.0);
    QVERIFY(captura() != conAnillos);

    // B se mueve 5 NM al sur de A: la marcacion pasa a 180.
    w.updateTarget(idb, a.position.atDistanceAndAzimuth(9260.0, 180.0));
    QVERIFY(w.bearingLineMeasure(marcacion, &rumbo, &dist));
    QVERIFY(qAbs(rumbo - 180.0) < 0.1);

    // Marcacion fija con rumbo y longitud, sin destino.
    BearingLine fija;
    fija.from = QGeoCoordinate(22.0, -80.1);
    fija.bearingDeg = 270.0;
    fija.lengthMeters = 3000.0;
    const qint64 idFija = w.addBearingLine(fija);
    QVERIFY(w.bearingLineMeasure(idFija, &rumbo, &dist));
    QVERIFY(qAbs(rumbo - 270.0) < 0.1 && qAbs(dist - 3000.0) < 1.0);
    QVERIFY(!w.updateRangeRings(idFija, r));                 // no es de anillos
    QVERIFY(w.updateBearingLine(idFija, l));

    // Sin el objetivo de destino, la marcacion no se puede medir (ni se dibuja).
    w.removeTarget(idb);
    QVERIFY(!w.bearingLineMeasure(marcacion, &rumbo, &dist));

    // Una marcacion sin longitud (mismo objetivo, o el mismo punto, en los dos
    // extremos) se rechaza: solo dibujaria un rotulo "000° · 0 m".
    BearingLine nula;
    nula.fromTarget = ida;
    nula.toTarget = ida;
    QCOMPARE(w.addBearingLine(nula), qint64(-1));
    nula = BearingLine();
    nula.from = QGeoCoordinate(22.0, -80.1);
    nula.to = nula.from;
    QCOMPARE(w.addBearingLine(nula), qint64(-1));
    QVERIFY(!w.updateBearingLine(idFija, nula));

    // Consultas por id: cada una solo devuelve su tipo.
    QVERIFY(w.rangeRings(anillos).has_value());
    QCOMPARE(w.rangeRings(anillos)->targetId, ida);
    QVERIFY(!w.rangeRings(idFija).has_value());
    QVERIFY(w.bearingLine(idFija).has_value());
    QVERIFY(!w.bearingLine(anillos).has_value());
    QCOMPARE(w.rangeRingsSpacing(anillos), 1852.0);          // fija

    // Anillos automaticos en un punto fijo con nombre (un radar): la
    // separacion es redonda, los anillos caben en la vista y al alejar crece.
    RangeRings radar;
    radar.center = QGeoCoordinate(22.0, -80.0);
    radar.spacingMeters = 0.0;
    radar.count = 4;
    radar.name = QStringLiteral("Radar");
    const qint64 idRadar = w.addRangeRings(radar);
    captura();
    const double pasoCerca = w.rangeRingsSpacing(idRadar);
    QVERIFY(pasoCerca > 0.0);
    const double m = pasoCerca / std::pow(10.0, std::floor(std::log10(pasoCerca)));
    QVERIFY(qAbs(m - 1.0) < 1e-6 || qAbs(m - 2.0) < 1e-6 || qAbs(m - 2.5) < 1e-6
            || qAbs(m - 5.0) < 1e-6);
    // A zoom 10 y lat 22 hay ~140 m/px: 4 anillos en el 45 % de 600 px.
    const double mPorPx = 156543.03 * std::cos(qDegreesToRadians(22.0)) / std::pow(2.0, 10);
    QVERIFY(pasoCerca * 4 <= 0.45 * 600 * mPorPx * 1.05);
    QVERIFY(pasoCerca * 4 >= 0.45 * 600 * mPorPx / 2.5 * 0.95);
    w.setZoom(8);
    captura();
    QVERIFY(w.rangeRingsSpacing(idRadar) > pasoCerca * 2.0);
    w.setZoom(10);

    // Anillos en una entidad punto (un observador): la siguen al moverla y
    // dejan de dibujarse si se borra.
    MapFeature obs;
    obs.kind = GeometryKind::Point;
    obs.layerId = w.activeFeatureLayer();
    obs.geometry = { QGeoCoordinate(22.05, -79.95) };
    const qint64 idObs = w.addFeature(obs);
    QVERIFY(idObs >= 0);
    RangeRings enObs;
    enObs.featureId = idObs;
    enObs.spacingMeters = 1000.0;
    const qint64 idEnObs = w.addRangeRings(enObs);
    captura();
    QCOMPARE(w.rangeRingsSpacing(idEnObs), 1000.0);
    QVERIFY(w.moveFeature(idObs, 0.01, 0.01));
    QCOMPARE(w.rangeRings(idEnObs)->featureId, idObs);

    // Marcacion entre una entidad punto y un objetivo: sigue a los dos.
    BearingLine conPunto;
    conPunto.fromFeature = idObs;
    conPunto.toTarget = ida;
    const qint64 idConPunto = w.addBearingLine(conPunto);
    QVERIFY(idConPunto > 0);
    double rumboP = 0.0, distP = 0.0;
    QVERIFY(w.bearingLineMeasure(idConPunto, &rumboP, &distP));
    const double distAntes = distP;
    QVERIFY(w.moveFeature(idObs, 0.02, 0.0));                    // el punto se mueve: la linea cambia
    QVERIFY(w.bearingLineMeasure(idConPunto, &rumboP, &distP));
    QVERIFY(qAbs(distP - distAntes) > 100.0);
    BearingLine mismoPunto;                                      // el mismo punto en los dos extremos
    mismoPunto.fromFeature = idObs;
    mismoPunto.toFeature = idObs;
    QCOMPARE(w.addBearingLine(mismoPunto), qint64(-1));

    w.removeFeature(idObs);
    QCOMPARE(w.rangeRingsSpacing(idEnObs), 0.0);                 // sin centro: no se dibuja
    QVERIFY(!w.bearingLineMeasure(idConPunto, &rumboP, &distP)); // ni se puede medir

    QVERIFY(w.removeRangeMark(anillos));
    QVERIFY(!w.removeRangeMark(anillos));
    QCOMPARE(w.rangeRingsSpacing(anillos), 0.0);
    w.clearRangeMarks();
    QVERIFY(w.rangeMarks().isEmpty());
}

// Clic derecho: la libreria resuelve que hay debajo y la app monta el menu.
void TstMapWidget::hitTestAndContextMenu()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.show();
    w.setZoom(10);
    const QGeoCoordinate centro(22.0, -80.0);
    w.setCenter(centro);
    auto *plot = qobject_cast<QCustomPlot *>(w.customPlot());
    QVERIFY(plot);
    const auto pixel = [&](const QGeoCoordinate &c) {
        plot->replot();
        const QPointF eje = w.toAxisCoords(c);
        return QPoint(qRound(plot->xAxis->coordToPixel(eje.x())),
                      qRound(plot->yAxis->coordToPixel(eje.y())));
    };

    // Un poligono, un objetivo dentro, unos anillos fijos en otro sitio y una
    // marcacion que no cruza nada.
    MapFeature pol;
    pol.kind = GeometryKind::Polygon;
    pol.layerId = w.activeFeatureLayer();
    pol.geometry = { QGeoCoordinate(22.05, -80.05), QGeoCoordinate(22.05, -79.95),
                     QGeoCoordinate(21.95, -79.95), QGeoCoordinate(21.95, -80.05) };
    const qint64 idPol = w.addFeature(pol);
    QVERIFY(idPol >= 0);
    MapTarget t;
    t.position = QGeoCoordinate(22.02, -80.02);
    const qint64 idT = w.addTarget(t);

    RangeRings r;
    r.center = QGeoCoordinate(22.15, -80.25);
    r.spacingMeters = 6000.0;            // ~43 px a zoom 10: anillo y centro bien separados
    r.count = 2;
    const qint64 idR = w.addRangeRings(r);
    BearingLine l;
    l.from = QGeoCoordinate(21.85, -80.30);
    l.to = QGeoCoordinate(21.85, -80.20);
    const qint64 idL = w.addBearingLine(l);

    // Sobre el objetivo: objetivo Y entidad (cada cosa por separado).
    MapHit h = w.hitTest(pixel(t.position));
    QCOMPARE(h.targetId, idT);
    QCOMPARE(h.featureId, idPol);
    QCOMPARE(h.rangeMarkId, qint64(-1));
    QVERIFY(h.coordinate.distanceTo(t.position) < 200.0);

    // Dentro del poligono, lejos del objetivo: solo la entidad.
    h = w.hitTest(pixel(QGeoCoordinate(21.97, -79.97)));
    QCOMPARE(h.targetId, qint64(-1));
    QCOMPARE(h.featureId, idPol);

    // En el centro de los anillos y sobre el primer anillo (6 km al este).
    QCOMPARE(w.hitTest(pixel(r.center)).rangeMarkId, idR);
    QCOMPARE(w.hitTest(pixel(r.center.atDistanceAndAzimuth(6000.0, 90.0))).rangeMarkId, idR);
    // Entre el centro y el anillo no hay nada.
    QCOMPARE(w.hitTest(pixel(r.center.atDistanceAndAzimuth(3000.0, 90.0))).rangeMarkId, qint64(-1));
    // Sobre la marcacion.
    QCOMPARE(w.hitTest(pixel(QGeoCoordinate(21.85, -80.25))).rangeMarkId, idL);

    // Mapa vacio.
    h = w.hitTest(pixel(QGeoCoordinate(21.80, -79.80)));
    QVERIFY(h.isEmpty());
    QVERIFY(h.coordinate.isValid());

    // Clic derecho sobre el objetivo: se emite con lo que hay debajo.
    QSignalSpy spy(&w, &MapWidget::contextMenuRequested);
    const QPoint donde = pixel(t.position);
    QMouseEvent pulsa = mouseEvent(QEvent::MouseButtonPress, donde, Qt::RightButton, Qt::RightButton);
    QCoreApplication::sendEvent(plot, &pulsa);
    QMouseEvent suelta = mouseEvent(QEvent::MouseButtonRelease, donde, Qt::RightButton, Qt::NoButton);
    QCoreApplication::sendEvent(plot, &suelta);
    QCOMPARE(spy.count(), 1);
    const MapHit recibido = spy.at(0).at(0).value<MapHit>();
    QCOMPARE(recibido.targetId, idT);
    QCOMPARE(recibido.featureId, idPol);
    QCOMPARE(recibido.pixel, donde);

    // Con el boton izquierdo no hay menu.
    QMouseEvent izq = mouseEvent(QEvent::MouseButtonPress, donde, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(plot, &izq);
    QMouseEvent izqS = mouseEvent(QEvent::MouseButtonRelease, donde, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(plot, &izqS);
    QCOMPARE(spy.count(), 1);
}

// Importar un historial GPX: el track marca inicio y fin siempre (modo por
// defecto), y al seleccionarlo se rotulan con su hora.
void TstMapWidget::routesShowStartAndEnd()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(10);
    w.setCenter(QGeoCoordinate(22.05, -80.0));
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = dir.filePath(QStringLiteral("historial.gpx"));
    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760419413000LL).toUTC();
    GpxTrack h;
    h.name = QStringLiteral("Buque 1");
    h.targetId = 1;
    for (int i = 0; i <= 10; ++i)
        h.points.append({ QGeoCoordinate(22.0 + 0.01 * i, -80.05 + 0.01 * i), t0.addSecs(60 * i) });
    QVERIFY(writeGpxTracks(ruta, { h }));

    QCOMPARE(w.routeEndpoints(), RouteEndpoints::Routes);
    const qint64 id = w.importVectorFile(ruta, QStringLiteral("historial"));
    QVERIFY(id > 0);
    const auto f = w.feature(id);
    QVERIFY(f && f->attributes.contains(QStringLiteral("time_end")));

    const QImage conMarcas = captura();
    w.setRouteEndpoints(RouteEndpoints::Selected);         // sin seleccionar: sin marcas
    const QImage sinMarcas = captura();
    QVERIFY(conMarcas != sinMarcas);

    w.selectFeature(id);                                   // seleccionada: marcas + rotulos
    const QImage seleccionada = captura();
    w.setRouteEndpoints(RouteEndpoints::Hidden);
    QVERIFY(captura() != seleccionada);
    w.setRouteEndpoints(RouteEndpoints::Routes);
    QCOMPARE(w.routeEndpoints(), RouteEndpoints::Routes);
}

// Dos pistas que empiezan a distinta hora. Al principio solo existe la
// primera; a mitad las dos, interpoladas; ir hacia atras rehace el objetivo
// (historial corto). Reproducir a mucha velocidad llega al final. Las rutas
// van en una capa temporal y el reproductor no toca los demas objetivos.
void TstMapWidget::trackPlayerReplaysHistory()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    MapTarget ajeno;
    ajeno.position = QGeoCoordinate(10.0, 10.0);
    const qint64 idAjeno = w.addTarget(ajeno);

    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760419413000LL).toUTC();
    const QGeoCoordinate a(22.0, -80.0);
    GpxTrack p1;
    p1.name = QStringLiteral("Buque 1");
    p1.type = QStringLiteral("buque");
    p1.targetId = 41;
    for (int i = 0; i <= 10; ++i)                         // 10 min al este, 1 km/min
        p1.points.append({ a.atDistanceAndAzimuth(1000.0 * i, 90.0), t0.addSecs(60 * i) });
    GpxTrack p2;
    p2.name = QStringLiteral("UAV 2");
    for (int i = 0; i <= 4; ++i)                          // empieza a los 5 min
        p2.points.append({ a.atDistanceAndAzimuth(500.0 * i, 0.0), t0.addSecs(300 + 60 * i) });

    TrackPlayer player(&w);
    QSignalSpy horas(&player, &TrackPlayer::timeChanged);
    player.setTracks({ p1, p2 });
    QCOMPARE(player.startTime(), t0);
    QCOMPARE(player.endTime(), t0.addSecs(600));
    QCOMPARE(player.currentTime(), t0);
    QVERIFY(player.targetIdOf(0) >= 0);
    QCOMPARE(player.targetIdOf(1), qint64(-1));           // aun no ha aparecido
    const qint64 id1 = player.targetIdOf(0);
    QCOMPARE(w.target(id1)->label, QStringLiteral("Buque 1"));
    QCOMPARE(w.target(id1)->kind, QStringLiteral("buque"));
    QCOMPARE(w.targetOrigin(id1).time, t0);
    QVERIFY(horas.count() >= 1);

    player.seek(t0.addSecs(390));                         // 6.5 min
    QVERIFY(player.targetIdOf(1) >= 0);
    QVERIFY(qAbs(w.target(id1)->position.distanceTo(a) - 6500.0) < 1.0);
    QVERIFY(qAbs(w.target(player.targetIdOf(1))->position.distanceTo(a) - 750.0) < 1.0);
    QCOMPARE(w.targetOrigin(player.targetIdOf(1)).time, t0.addSecs(300));
    const int largo = int(w.targetHistory(player.targetIdOf(0)).size());
    QVERIFY(largo >= 7);

    player.seek(t0.addSecs(120));                         // hacia atras
    QCOMPARE(player.targetIdOf(1), qint64(-1));           // la segunda ya no esta
    QVERIFY(w.targetHistory(player.targetIdOf(0)).size() < largo);
    QCOMPARE(w.targetOrigin(player.targetIdOf(0)).time, t0);

    // Rutas completas en una capa temporal, como tracks GPX (inicio y fin).
    player.setRoutesVisible(true);
    const QVector<MapFeature> rutas = w.featuresInLayer(QStringLiteral("replay_routes"));
    QCOMPARE(rutas.size(), 2);
    QCOMPARE(rutas.first().attributes.value(QStringLiteral("gpx_kind")).toString(),
             QStringLiteral("trk"));
    QVERIFY(rutas.first().attributes.contains(QStringLiteral("time_end")));

    // A x3600 los 10 min pasan en unos 170 ms reales.
    QSignalSpy fin(&player, &TrackPlayer::finished);
    player.setSpeed(3600.0);
    player.play();
    QVERIFY(player.isPlaying());
    QVERIFY(fin.wait(3000));
    QVERIFY(!player.isPlaying());
    QCOMPARE(player.currentTime(), t0.addSecs(600));
    QVERIFY(player.targetIdOf(0) >= 0);                   // su ultimo punto, incluido

    player.stop();
    QCOMPARE(player.currentTime(), t0);
    QVERIFY(player.isLoaded());
    QSignalSpy salida(&player, &TrackPlayer::cleared);
    player.clear();                                        // salir de la reproduccion
    QCOMPARE(salida.count(), 1);
    QVERIFY(!player.isLoaded());
    player.clear();                                        // ya fuera: no vuelve a avisar
    QCOMPARE(salida.count(), 1);
    QVERIFY(w.featuresInLayer(QStringLiteral("replay_routes")).isEmpty());
    QCOMPARE(w.targets().size(), 1);                       // solo el ajeno
    QVERIFY(w.target(idAjeno).has_value());

    QString err;
    QVERIFY(!player.loadFile(QStringLiteral("/no/existe.gpx"), &err));
    QVERIFY(!err.isEmpty());
}

// Se marcan 4 vertices de un cuadrado de ~2 km con clics reales sobre la vista.
// La medida en vivo cambia lo dibujado; Intro cierra y emite areaMeasured con
// la superficie del poligono marcado (la de polygonArea) y su perimetro; al
// cambiar de herramienta se quita la marca. No crea ninguna entidad.
void TstMapWidget::measureAreaToolMeasuresPolygon()
{
    qRegisterMetaType<libmapa::AreaMeasurement>();
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    const QGeoCoordinate centro(22.0, -79.5);
    w.setCenter(centro);
    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);
    const auto pixel = [vista](const QGeoCoordinate &c) {
        const QPointF ax = MapView::toAxis(c);
        return QPoint(int(std::lround(vista->xAxis->coordToPixel(ax.x()))),
                      int(std::lround(vista->yAxis->coordToPixel(ax.y()))));
    };
    const auto clic = [vista](const QPoint &p) {
        QMouseEvent pulsar = mouseEvent(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        QMouseEvent soltar = mouseEvent(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QCoreApplication::sendEvent(vista, &pulsar);
        QCoreApplication::sendEvent(vista, &soltar);
    };
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(30);
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };

    const int entidades = int(w.features().size());
    QSignalSpy medida(&w, &MapWidget::areaMeasured);
    w.setActiveTool(MapTool::MeasureArea);
    const QGeoCoordinate a = centro.atDistanceAndAzimuth(1000.0, 225.0 + 0.0);
    const QGeoCoordinate b = a.atDistanceAndAzimuth(2000.0, 90.0);
    const QGeoCoordinate c = b.atDistanceAndAzimuth(2000.0, 0.0);
    const QGeoCoordinate d = a.atDistanceAndAzimuth(2000.0, 0.0);
    clic(pixel(a));
    clic(pixel(b));
    clic(pixel(c));
    const QImage tresVertices = captura();
    // Mover el raton cambia la medida en vivo (y su rotulo).
    QMouseEvent mover = mouseEvent(QEvent::MouseMove, pixel(d), Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &mover);
    QVERIFY(captura() != tresVertices);
    clic(pixel(d));
    QCOMPARE(medida.count(), 0);                           // aun abierto

    QKeyEvent intro(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(vista, &intro);
    QCOMPARE(medida.count(), 1);
    const AreaMeasurement m = medida.first().at(0).value<AreaMeasurement>();
    QCOMPARE(m.polygon.size(), 4);
    QVERIFY(qAbs(m.areaSquareMeters - polygonArea(m.polygon)) < 1e-6);
    // ~4 km2 y ~8 km: los clics caen en pixeles enteros (~36 m a zoom 12), y
    // eso mueve cada lado hasta un 2 %.
    QVERIFY(qAbs(m.areaSquareMeters - 4.0e6) < 0.16e6);
    QVERIFY(qAbs(m.perimeterMeters - 8000.0) < 160.0);
    QCOMPARE(int(w.features().size()), entidades);         // no crea entidad

    const QImage cerrada = captura();
    w.setActiveTool(MapTool::None);                        // quita la marca
    QVERIFY(captura() != cerrada);

    // Con menos de 3 vertices no hay medida.
    w.setActiveTool(MapTool::MeasureArea);
    clic(pixel(a));
    clic(pixel(b));
    QCoreApplication::sendEvent(vista, &intro);
    QCOMPARE(medida.count(), 1);
}

// Un GeoJSON de 300 puntos: tras importarlo hay UN paso de deshacer mas, y
// deshacerlo quita las 300 entidades y la capa nueva; rehacer las devuelve.
// Lo que habia antes (una entidad del usuario) sigue en su sitio.
void TstMapWidget::importIsOneUndoStep()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    const qint64 previa = w.addFeature(puntoDe(QStringLiteral("previa"), 21.0, -80.0));
    QVERIFY(previa > 0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = dir.filePath(QStringLiteral("muchos.geojson"));
    {
        QJsonArray feats;
        for (int i = 0; i < 300; ++i) {
            QJsonObject geom{ { QStringLiteral("type"), QStringLiteral("Point") },
                              { QStringLiteral("coordinates"),
                                QJsonArray{ -80.0 + 0.01 * i, 22.0 } } };
            feats.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("Feature") },
                                      { QStringLiteral("properties"), QJsonObject() },
                                      { QStringLiteral("geometry"), geom } });
        }
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("type"), QStringLiteral("FeatureCollection") },
                                           { QStringLiteral("features"), feats } }).toJson());
    }

    QElapsedTimer reloj;
    reloj.start();
    QVERIFY(w.importVectorFile(ruta, QStringLiteral("muchos")) > 0);
    qInfo("importar 300 entidades: %lld ms", reloj.elapsed());
    QCOMPARE(w.featuresInLayer(QStringLiteral("muchos")).size(), 300);

    QVERIFY(w.undo());                                     // UN deshacer basta
    QVERIFY(w.featuresInLayer(QStringLiteral("muchos")).isEmpty());
    bool hayCapa = false;
    for (const LayerInfo &c : w.featureLayers())
        hayCapa = hayCapa || c.id == QStringLiteral("muchos");
    QVERIFY(!hayCapa);                                     // la capa tambien se va
    QVERIFY(w.feature(previa).has_value());                // lo de antes sigue

    QVERIFY(w.redo());
    QCOMPARE(w.featuresInLayer(QStringLiteral("muchos")).size(), 300);
}

// Tres objetivos (buque rapido, aeronave, UAV lento). Filtro "buques y UAV a
// mas de 5 m/s": solo pasa el buque. Oculto = no se pincha; atenuado = si.
// Cambiar la velocidad del UAV lo hace pasar; "solo en zonas" y un predicado
// sobre atributos tambien filtran; un filtro vacio lo quita.
void TstMapWidget::targetFilterHidesOrDims()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(10);
    const QGeoCoordinate centro(22.0, -79.5);
    w.setCenter(centro);
    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);
    const auto pixel = [vista](const QGeoCoordinate &c) {
        const QPointF ax = MapView::toAxis(c);
        return QPoint(int(std::lround(vista->xAxis->coordToPixel(ax.x()))),
                      int(std::lround(vista->yAxis->coordToPixel(ax.y()))));
    };
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };

    MapTarget b;
    b.kind = QStringLiteral("buque");
    b.position = centro;
    b.attributes.insert(QStringLiteral("bandera"), QStringLiteral("CU"));
    const qint64 buque = w.addTarget(b);
    MapTarget a;
    a.kind = QStringLiteral("aeronave");
    a.position = centro.atDistanceAndAzimuth(5000.0, 90.0);
    const qint64 avion = w.addTarget(a);
    MapTarget u;
    u.kind = QStringLiteral("uav");
    u.position = centro.atDistanceAndAzimuth(5000.0, 270.0);
    const qint64 uav = w.addTarget(u);
    w.setTargetVelocity(buque, 8.0, 0.0);
    w.setTargetVelocity(avion, 120.0, 0.0);
    w.setTargetVelocity(uav, 2.0, 0.0);
    QVERIFY(!w.hasTargetFilter());
    QVERIFY(w.targetPassesFilter(avion));
    const QImage todos = captura();

    TargetFilter f;
    f.kinds = QStringList{ QStringLiteral("buque"), QStringLiteral("uav") };
    f.minSpeed = 5.0;
    w.setTargetFilter(f);
    QVERIFY(w.hasTargetFilter());
    QCOMPARE(w.filteredOutTargets(), QVector<qint64>({ avion, uav }));
    QVERIFY(w.targetPassesFilter(buque));
    QVERIFY(!w.targetPassesFilter(avion));
    QCOMPARE(w.targetAt(pixel(a.position)), qint64(-1));    // oculto: no se pincha
    QCOMPARE(w.targetAt(pixel(b.position)), buque);
    const QImage oculto = captura();
    QVERIFY(oculto != todos);
    QCOMPARE(w.targets().size(), 3);                         // siguen existiendo

    // Atenuar: se ven (distinto de ocultos) y se pueden pinchar.
    f.dim = true;
    w.setTargetFilter(f);
    QCOMPARE(w.targetAt(pixel(a.position)), avion);
    const QImage tenue = captura();
    QVERIFY(tenue != oculto && tenue != todos);

    // El UAV acelera: pasa a cumplir sin recalcular nada a mano.
    w.setTargetVelocity(uav, 6.0, 0.0);
    QCOMPARE(w.filteredOutTargets(), QVector<qint64>({ avion }));

    // Solo dentro de zonas vigiladas: zona alrededor del buque.
    MapFeature zona;
    zona.kind = GeometryKind::Polygon;
    zona.geometry = { centro.atDistanceAndAzimuth(2000.0, 45.0), centro.atDistanceAndAzimuth(2000.0, 135.0),
                      centro.atDistanceAndAzimuth(2000.0, 225.0), centro.atDistanceAndAzimuth(2000.0, 315.0) };
    const qint64 idZona = w.addFeature(zona);
    QVERIFY(w.watchZone(idZona));
    TargetFilter enZona;
    enZona.onlyInWatchedZones = true;
    w.setTargetFilter(enZona);
    QCOMPARE(w.filteredOutTargets(), QVector<qint64>({ avion, uav }));
    // El UAV entra en la zona: pasa (lo reevalua la transicion de zona).
    w.updateTarget(uav, centro.atDistanceAndAzimuth(100.0, 0.0));
    QCOMPARE(w.filteredOutTargets(), QVector<qint64>({ avion }));

    // Criterio libre de la app sobre los atributos.
    TargetFilter bandera;
    bandera.predicate = [](const MapTarget &t) {
        return t.attributes.value(QStringLiteral("bandera")).toString() == QLatin1String("CU");
    };
    w.setTargetFilter(bandera);
    QCOMPARE(w.filteredOutTargets(), QVector<qint64>({ avion, uav }));

    w.removeTarget(avion);
    QCOMPARE(w.filteredOutTargets(), QVector<qint64>({ uav }));
    w.clearTargetFilter();
    QVERIFY(!w.hasTargetFilter());
    QVERIFY(w.filteredOutTargets().isEmpty());
    QVERIFY(!w.targetPassesFilter(avion));                   // ya no existe
}

// 20 objetivos en unos 200 m (a zoom 9, un par de pixeles) y uno lejos: con
// la agrupacion activa hay un grupo de 20 y el lejano va suelto. Se pincha el
// grupo (no un objetivo); seleccionar uno lo saca del grupo; por encima de
// maxZoom no se agrupa.
void TstMapWidget::targetClusteringGroupsCrowds()
{
    qRegisterMetaType<QVector<qint64>>();
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(9);
    const QGeoCoordinate centro(22.0, -79.5);
    w.setCenter(centro);
    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);
    const auto pixel = [vista](const QGeoCoordinate &c) {
        const QPointF ax = MapView::toAxis(c);
        return QPoint(int(std::lround(vista->xAxis->coordToPixel(ax.x()))),
                      int(std::lround(vista->yAxis->coordToPixel(ax.y()))));
    };
    const auto captura = [&w] {
        QCoreApplication::processEvents();
        QTest::qWait(60);
        QCoreApplication::processEvents();
        return w.grab().toImage();
    };

    QVector<qint64> monton;
    for (int i = 0; i < 20; ++i) {
        MapTarget t;
        t.position = centro.atDistanceAndAzimuth(10.0 * i, 18.0 * i);
        monton.append(w.addTarget(t));
    }
    MapTarget lejos;
    lejos.position = centro.atDistanceAndAzimuth(60000.0, 90.0);
    const qint64 idLejos = w.addTarget(lejos);
    const QImage sueltos = captura();
    QVERIFY(w.targetClusterAt(pixel(centro)).isEmpty());   // desactivada

    TargetClustering c;
    c.enabled = true;
    w.setTargetClustering(c);
    QVERIFY(w.targetClustering().enabled);
    const QImage agrupados = captura();
    QVERIFY(agrupados != sueltos);
    QVector<qint64> grupo = w.targetClusterAt(pixel(centro));
    std::sort(grupo.begin(), grupo.end());
    QCOMPARE(grupo, monton);
    QCOMPARE(w.targetAt(pixel(centro)), qint64(-1));        // dentro de un grupo
    QCOMPARE(w.targetAt(pixel(lejos.position)), idLejos);   // suelto

    // Clic sobre el grupo: targetClusterClicked con sus 20 ids.
    QSignalSpy clicGrupo(&w, &MapWidget::targetClusterClicked);
    w.setActiveTool(MapTool::None);
    QMouseEvent pulsar = mouseEvent(QEvent::MouseButtonPress, pixel(centro), Qt::LeftButton, Qt::LeftButton);
    QMouseEvent soltar = mouseEvent(QEvent::MouseButtonRelease, pixel(centro), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &pulsar);
    QCoreApplication::sendEvent(vista, &soltar);
    QCOMPARE(clicGrupo.count(), 1);
    QCOMPARE(clicGrupo.first().at(0).value<QVector<qint64>>().size(), 20);

    // El seleccionado sale del grupo.
    w.setSelectedTarget(monton.first());
    captura();
    QCOMPARE(w.targetClusterAt(pixel(centro)).size(), 19);
    w.setSelectedTarget(-1);

    // Por encima de maxZoom, sin grupos.
    c.maxZoom = 8;
    w.setTargetClustering(c);
    captura();
    QVERIFY(w.targetClusterAt(pixel(centro)).isEmpty());
    QCOMPARE(w.targets().size(), 21);                        // solo cambia el dibujo
}

// Crea una entidad de punto minima (como el helper de tst_overlaymodel).
static MapFeature puntoDe(const QString &nombre, double lat, double lon)
{
    MapFeature f;
    f.kind = GeometryKind::Point;
    f.name = nombre;
    f.geometry = {QGeoCoordinate(lat, lon)};
    return f;
}

// Con featuresDbFile activo, anadir una entidad y forzar el guardado deja la
// entidad en la BD (comprobado leyendola con un VectorRepository aparte).
void TstMapWidget::autoPersistsFeatures()
{
    const QString ruta = m_dir.filePath(QStringLiteral("feats_auto.sqlitedb"));
    QFile::remove(ruta);
    {
        MapWidget w(baseConfig(m_jsonPath));
        QVERIFY(w.isReady());
        w.setFeaturesDbFile(ruta);
        QVERIFY(w.addFeature(puntoDe(QStringLiteral("Morro"), 23.15, -82.36)) > 0);
        w.saveFeaturesNow();
    }
    VectorRepository repo;
    QVERIFY(repo.open(ruta));
    const QVector<MapFeature> leidas = repo.loadFeatures();
    QCOMPARE(leidas.size(), 1);
    QCOMPARE(leidas.first().name, QStringLiteral("Morro"));
}

// Un MapWidget creado con un featuresDbFile que YA tiene entidades las carga
// solo al abrir.
void TstMapWidget::reloadsFeaturesOnOpen()
{
    const QString ruta = m_dir.filePath(QStringLiteral("feats_reload.sqlitedb"));
    QFile::remove(ruta);
    {
        MapWidget w(baseConfig(m_jsonPath));
        w.setFeaturesDbFile(ruta);
        w.addFeature(puntoDe(QStringLiteral("A"), 23.1, -82.3));
        w.addFeature(puntoDe(QStringLiteral("B"), 23.2, -82.4));
        w.saveFeaturesNow();
    }
    MapConfig cfg = baseConfig(m_jsonPath);
    cfg.featuresDbFile = ruta;
    MapWidget w2(cfg);
    QVERIFY(w2.isReady());
    QCOMPARE(w2.featureCount(), qint64(2));
}

// Sin llamar a saveFeaturesNow: solo anadir y dejar correr el bucle de eventos;
// el antirebote (~500 ms) debe disparar el guardado por si mismo.
void TstMapWidget::debouncedAutosaveTriggers()
{
    const QString ruta = m_dir.filePath(QStringLiteral("feats_debounce.sqlitedb"));
    QFile::remove(ruta);
    MapWidget w(baseConfig(m_jsonPath));
    w.setFeaturesDbFile(ruta);
    w.addFeature(puntoDe(QStringLiteral("Tardia"), 23.0, -82.0));
    QTest::qWait(900);   // deja correr el bucle de eventos > antirebote (500 ms)
    VectorRepository repo;
    QVERIFY(repo.open(ruta));
    QCOMPARE(repo.loadFeatures().size(), 1);
}

// Paquete de prueba: las rutas a las BD sinteticas son RELATIVAS ("../osm...")
// para comprobar que se resuelven contra la carpeta del paquete. La BD de
// entidades va con ruta absoluta para no tocar la carpeta de datos del usuario.
QString TstMapWidget::makePackage(const QString &carpeta, const QString &entidades)
{
    const QString dir = m_dir.filePath(carpeta);
    QDir().mkpath(dir);

    QFile geo(dir + QStringLiteral("/corredores.geo"));
    if (geo.open(QIODevice::WriteOnly)) {
        geo.write("-81.436057,23.098419,\n-81.597725,21.611786,\n0.0,0.0\n"
                  "-78.862989,22.378492,\n-78.918099,21.477315,\n0.0,0.0\n");
        geo.close();
    }

    QJsonObject osm;
    osm[QStringLiteral("id")] = QStringLiteral("osm");
    osm[QStringLiteral("filePath")] = QStringLiteral("../osm.sqlitedb");
    osm[QStringLiteral("minZoom")] = 3;
    osm[QStringLiteral("maxZoom")] = 12;
    osm[QStringLiteral("baseZoom")] = 3;
    QJsonObject sat;
    sat[QStringLiteral("id")] = QStringLiteral("satelital");
    sat[QStringLiteral("filePath")] = QStringLiteral("../sat.sqlitedb");
    sat[QStringLiteral("zFactor")] = -1;
    sat[QStringLiteral("zOffset")] = 17;
    sat[QStringLiteral("minZoom")] = 3;
    sat[QStringLiteral("maxZoom")] = 12;
    sat[QStringLiteral("recommendedMaxZoom")] = 11;
    sat[QStringLiteral("baseZoom")] = 3;
    sat[QStringLiteral("typicalFill")] = 0.32;

    QJsonObject corredores;
    corredores[QStringLiteral("id")] = QStringLiteral("corredores");
    corredores[QStringLiteral("name")] = QStringLiteral("Corredores");
    corredores[QStringLiteral("file")] = QStringLiteral("corredores.geo");

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("libmapa-package");
    root[QStringLiteral("version")] = 2;
    root[QStringLiteral("package")] = QJsonObject{
        {QStringLiteral("id"), QStringLiteral("prueba")},
        {QStringLiteral("name"), QStringLiteral("Paquete de prueba")},
        {QStringLiteral("attribution"), QStringLiteral("Sintetico")}};
    root[QStringLiteral("start")] = QJsonObject{
        {QStringLiteral("layer"), QStringLiteral("satelital")},
        {QStringLiteral("center"), QJsonArray{21.0, -80.0}},
        {QStringLiteral("zoom"), 9}};
    root[QStringLiteral("datasets")] = QJsonArray{osm, sat};
    root[QStringLiteral("overlays")] = QJsonArray{corredores};
    root[QStringLiteral("features")] = QJsonObject{{QStringLiteral("file"), entidades}};

    QFile f(dir + QStringLiteral("/mapa.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
    return dir;
}

// Con SOLO dataDir el widget sale configurado del paquete: capa y punto de
// arranque, la capa fija cargada y bloqueada, y la BD de entidades del usuario.
// Lo dibujado se guarda SIN la capa fija, y al reabrir no se duplica.
void TstMapWidget::opensDataPackageWithOneSetting()
{
    const QString entidades = m_dir.filePath(QStringLiteral("paquete_entidades.db"));
    QFile::remove(entidades);
    MapConfig cfg;
    cfg.dataDir = makePackage(QStringLiteral("paquete"), entidades);
    cfg.cacheMiB = 32;
    cfg.debounceMs = 0;

    {
        MapWidget w(cfg);
        QVERIFY2(w.isReady(), qPrintable(w.lastError()));
        QCOMPARE(w.baseLayerId(), QStringLiteral("satelital"));
        QCOMPARE(w.zoom(), 9);
        QVERIFY(qAbs(w.center().latitude() - 21.0) < 0.05);
        QVERIFY(qAbs(w.center().longitude() + 80.0) < 0.05);

        QVERIFY(w.packageInfo().isValid());
        QCOMPARE(w.packageInfo().name, QStringLiteral("Paquete de prueba"));
        QCOMPARE(w.packageInfo().attribution, QStringLiteral("Sintetico"));

        const QVector<MapFeature> fijas = w.featuresInLayer(QStringLiteral("corredores"));
        QCOMPARE(fijas.size(), 1);
        QVERIFY(!w.canUndo());                 // cargar la capa fija no se "deshace"

        // Capa fija: bloqueada, oculta y sin poder elegirse con el raton (decoracion).
        for (const LayerInfo &capa : w.featureLayers()) {
            if (capa.id != QStringLiteral("corredores"))
                continue;
            QVERIFY(!capa.editable);
            QVERIFY(!capa.visible);
            QVERIFY(!capa.selectable);
        }

        QVERIFY(w.addFeature(puntoDe(QStringLiteral("Propia"), 22.0, -80.0)) > 0);
        w.saveFeaturesNow();
    }

    VectorRepository repo;
    QVERIFY(repo.open(entidades));
    const QVector<MapFeature> guardadas = repo.loadFeatures();
    QCOMPARE(guardadas.size(), 1);
    QCOMPARE(guardadas.first().name, QStringLiteral("Propia"));
    for (const LayerInfo &c : repo.loadLayers())
        QVERIFY(c.id != QStringLiteral("corredores"));
    repo.close();

    MapWidget w2(cfg);
    QVERIFY(w2.isReady());
    QCOMPARE(w2.featuresInLayer(QStringLiteral("corredores")).size(), 1);
    QCOMPARE(w2.featureCount(), 2);           // la propia + la fija, sin duplicar
}

// Con "visible": true y "selectable": true en el overlay del mapa.json, la capa fija
// sale visible y elegible; sin ellos (el caso normal) sale oculta y no elegible.
void TstMapWidget::packageOverlaysCanBeMadeVisibleAndSelectable()
{
    const QString entidades = m_dir.filePath(QStringLiteral("paquete_flags.db"));
    QFile::remove(entidades);
    MapConfig cfg;
    cfg.dataDir = makePackage(QStringLiteral("paquete_flags"), entidades);
    cfg.cacheMiB = 32;
    cfg.debounceMs = 0;

    const auto capaFija = [](const MapWidget &w) {
        for (const LayerInfo &c : w.featureLayers())
            if (c.id == QStringLiteral("corredores"))
                return c;
        return LayerInfo();
    };
    {
        MapWidget w(cfg);
        QVERIFY(w.isReady());
        QVERIFY(!capaFija(w).visible);
        QVERIFY(!capaFija(w).selectable);
    }

    // Se reescribe el mapa.json pidiendo las dos cosas.
    const QString json = cfg.dataDir + QStringLiteral("/mapa.json");
    QFile f(json);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QJsonArray overlays = root.value(QStringLiteral("overlays")).toArray();
    QJsonObject ov = overlays.first().toObject();
    ov[QStringLiteral("visible")] = true;
    ov[QStringLiteral("selectable")] = true;
    overlays[0] = ov;
    root[QStringLiteral("overlays")] = overlays;
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(root).toJson());
    f.close();

    MapWidget w2(cfg);
    QVERIFY(w2.isReady());
    QVERIFY(capaFija(w2).visible);
    QVERIFY(capaFija(w2).selectable);
    QVERIFY(!capaFija(w2).editable);          // fija sigue siendo fija

    // Y la API lo cambia en caliente.
    QVERIFY(w2.setFeatureLayerSelectable(QStringLiteral("corredores"), false));
    QVERIFY(!capaFija(w2).selectable);
    QVERIFY(!w2.setFeatureLayerSelectable(QStringLiteral("no_existe"), true));
}

// Un poligono grande en una capa NO seleccionable (una provincia de fondo) no se lleva
// el clic ni sale en hitTest, pero featureAt (consulta, p. ej. un tooltip) lo sigue
// viendo. Al volver la capa seleccionable, el clic si lo elige.
void TstMapWidget::unselectableLayerDoesNotStealTheClick()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(v != nullptr);

    w.addFeatureLayer(QStringLiteral("fondo"), QStringLiteral("Fondo"), 50);
    QVERIFY(w.setFeatureLayerSelectable(QStringLiteral("fondo"), false));
    MapFeature prov;
    prov.kind = GeometryKind::Polygon;
    prov.layerId = QStringLiteral("fondo");
    prov.name = QStringLiteral("Provincia");
    prov.geometry = { v->coordinateAt(QPoint(50, 50)), v->coordinateAt(QPoint(750, 50)),
                      v->coordinateAt(QPoint(750, 550)), v->coordinateAt(QPoint(50, 550)) };
    const qint64 id = w.addFeature(prov);
    QVERIFY(id > 0);

    // Sin poder elegirse: el clic dentro no la selecciona ni sale en hitTest...
    clic(v, QPoint(400, 300));
    QCOMPARE(w.selectedFeature(), qint64(-1));
    QCOMPARE(w.hitTest(QPoint(400, 300)).featureId, qint64(-1));
    QCOMPARE(w.featureLabelAt(QPoint(400, 300)), qint64(-1));
    // ...pero la consulta la ve (un tooltip o una lectura de cota).
    QCOMPARE(w.featureAt(QPoint(400, 300)), id);

    // Seleccionable otra vez: el clic la elige.
    QVERIFY(w.setFeatureLayerSelectable(QStringLiteral("fondo"), true));
    clic(v, QPoint(400, 300));
    QCOMPARE(w.selectedFeature(), id);
    QCOMPARE(w.hitTest(QPoint(400, 300)).featureId, id);
}

// Con autosaveFeatures=false (guardado explicito) y un paquete: NO se usa el
// fichero de entidades del paquete. Lo dibujado NO se autoguarda (el fichero no
// llega a existir) y al reabrir no reaparece; la capa fija del paquete si carga.
void TstMapWidget::autosaveFeaturesFalseSkipsPackagePersistence()
{
    const QString entidades = m_dir.filePath(QStringLiteral("paquete_noauto.db"));
    QFile::remove(entidades);
    MapConfig cfg;
    cfg.dataDir = makePackage(QStringLiteral("paquete_noauto"), entidades);
    cfg.cacheMiB = 32;
    cfg.debounceMs = 0;
    cfg.autosaveFeatures = false;          // guardado explicito

    {
        MapWidget w(cfg);
        QVERIFY2(w.isReady(), qPrintable(w.lastError()));
        // La capa fija (overlay) del paquete SI se carga (es independiente).
        QCOMPARE(w.featuresInLayer(QStringLiteral("corredores")).size(), 1);
        QVERIFY(w.addFeature(puntoDe(QStringLiteral("Propia"), 22.0, -80.0)) > 0);
        w.saveFeaturesNow();               // aunque se fuerce, no hay destino
    }
    // No se autoguardo nada: el fichero del paquete ni siquiera se creo.
    QVERIFY(!QFileInfo::exists(entidades));

    // Al reabrir, la entidad propia NO reaparece; solo la fija del paquete.
    MapWidget w2(cfg);
    QVERIFY(w2.isReady());
    QCOMPARE(w2.featuresInLayer(QStringLiteral("corredores")).size(), 1);
    QCOMPARE(w2.featureCount(), qint64(1));
}

// La regla de prioridad: lo que la aplicacion pone a mano gana al paquete.
void TstMapWidget::explicitConfigWinsOverPackage()
{
    const QString entidades = m_dir.filePath(QStringLiteral("paquete2_entidades.db"));
    QFile::remove(entidades);
    MapConfig cfg;
    cfg.dataDir = makePackage(QStringLiteral("paquete2"), entidades);
    cfg.initialLayerId = QStringLiteral("osm");
    cfg.initialZoom = 11;
    cfg.cacheMiB = 32;
    cfg.debounceMs = 0;

    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    QCOMPARE(w.baseLayerId(), QStringLiteral("osm"));
    QCOMPARE(w.zoom(), 11);
    QVERIFY(qAbs(w.center().latitude() - 21.0) < 0.05);   // el centro, del paquete
}

// Un dataDir que no existe no arranca, y el error nombra el manifiesto que falta.
void TstMapWidget::reportsMissingDataPackage()
{
    MapConfig cfg;
    cfg.dataDir = m_dir.filePath(QStringLiteral("no_hay_paquete"));
    MapWidget w(cfg);
    QVERIFY(!w.isReady());
    QVERIFY(w.lastError().contains(QStringLiteral("mapa.json")));
    QVERIFY(!w.packageInfo().isValid());
}

// Un paquete al que le falta la capa fija y una de las bases: el widget arranca
// con lo que si hay, y dataWarnings() nombra exactamente lo que falta (en vez de
// dejar esa parte del mapa en blanco sin explicacion).
void TstMapWidget::reportsDataWarningsButStillOpens()
{
    const QString entidades = m_dir.filePath(QStringLiteral("paquete3_entidades.db"));
    MapConfig cfg;
    cfg.dataDir = makePackage(QStringLiteral("paquete3"), entidades);
    cfg.cacheMiB = 32;
    cfg.debounceMs = 0;
    QFile::remove(cfg.dataDir + QStringLiteral("/corredores.geo"));

    // Se anade una tercera capa base que apunta a un fichero inexistente.
    QFile f(cfg.dataDir + QStringLiteral("/mapa.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QJsonArray capas = root.value(QStringLiteral("datasets")).toArray();
    capas.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("perdida")},
                             {QStringLiteral("filePath"), QStringLiteral("no_esta.sqlitedb")}});
    root[QStringLiteral("datasets")] = capas;
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(root).toJson());
    f.close();

    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));      // arranca igual
    const QString avisos = w.dataWarnings().join(QLatin1Char('\n'));
    QVERIFY2(avisos.contains(QStringLiteral("no_esta.sqlitedb")), qPrintable(avisos));
    QVERIFY2(avisos.contains(QStringLiteral("corredores.geo")), qPrintable(avisos));
    QVERIFY(w.featuresInLayer(QStringLiteral("corredores")).isEmpty());
}

// La API de relieve sombreado conmuta su estado y acepta parametros sin colgar.
// Sin DEM configurado la capa queda vacia (refreshHillshade no dibuja nada), pero
// todas las rutas del API deben ser seguras.
void TstMapWidget::hillshadeApiTogglesSafely()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));

    QVERIFY(!w.isHillshadeVisible());
    w.setHillshadeSun(315.0, 45.0);
    w.setHillshadeOpacity(0.6);
    w.setHillshadeExaggeration(2.0);
    w.setHillshadeColored(true);

    w.setHillshadeVisible(true);
    QVERIFY(w.isHillshadeVisible());
    w.refreshHillshade();                 // sin DEM: no dibuja, no debe colgar
    w.setHillshadeColored(false);
    w.refreshHillshade();

    w.setHillshadeVisible(false);
    QVERIFY(!w.isHillshadeVisible());
}

// loadFeaturesAsLayer AÑADE las entidades de una BD vectorial como capa fija (sin
// reemplazar las existentes, al reves que loadFeaturesFrom), conservando estilo,
// etiqueta y atributos por entidad (como la capa de curvas de nivel del paquete).
void TstMapWidget::loadsFeaturesDbAsFixedLayer()
{
    const QString bd = m_dir.filePath(QStringLiteral("curvas_test.db"));
    QFile::remove(bd);
    {
        VectorRepository repo;
        QVERIFY(repo.open(bd));
        MapFeature a;
        a.layerId = QStringLiteral("curvas");
        a.kind = GeometryKind::Polyline;
        a.type = QStringLiteral("curva_nivel");
        a.name = QStringLiteral("500 m");
        a.geometry = {QGeoCoordinate(20.0, -77.0), QGeoCoordinate(20.1, -77.1)};
        a.attributes[QStringLiteral("cota")] = 500;
        a.style.labelVisible = true;               // curva indice (etiquetada)
        MapFeature b = a;
        b.name.clear();
        b.attributes[QStringLiteral("cota")] = 600;
        b.style.labelVisible = false;              // curva normal
        b.geometry = {QGeoCoordinate(20.0, -77.2), QGeoCoordinate(20.1, -77.3)};
        QVERIFY(repo.saveFeatures({a, b}));
    }

    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(400, 300);

    // Una entidad propia del usuario, que NO debe perderse al añadir la capa fija.
    MapFeature mio;
    mio.kind = GeometryKind::Point;
    mio.layerId = QStringLiteral("mios");
    mio.geometry = {QGeoCoordinate(23.0, -82.0)};
    QVERIFY(w.addFeature(mio) > 0);

    QString err;
    QVERIFY2(w.loadFeaturesAsLayer(bd, QStringLiteral("curvas"),
                                   QStringLiteral("Curvas"), &err) > 0, qPrintable(err));

    QCOMPARE(w.featureCount(), 3);                 // añade, no reemplaza (1 + 2)
    const auto curvas = w.featuresInLayer(QStringLiteral("curvas"));
    QCOMPARE(curvas.size(), 2);

    int conEtiqueta = 0;
    QSet<int> cotas;
    for (const MapFeature &f : curvas) {
        cotas.insert(f.attributes.value(QStringLiteral("cota")).toInt());
        if (f.style.labelVisible)
            ++conEtiqueta;
    }
    QCOMPARE(cotas, (QSet<int>{500, 600}));        // atributos conservados
    QCOMPARE(conEtiqueta, 1);                        // estilo por entidad conservado

    // Rango de zoom por capa: se guarda en la LayerInfo (oculta la capa de
    // detalle a vista general).
    QVERIFY(w.setFeatureLayerZoomRange(QStringLiteral("curvas"), 10, -1));
    bool vista = false;
    for (const LayerInfo &c : w.featureLayers())
        if (c.id == QStringLiteral("curvas")) {
            vista = true;
            QCOMPARE(c.minZoom, 10);
            QCOMPARE(c.maxZoom, -1);
        }
    QVERIFY(vista);
}

// El recorte por viewport NO debe tragarse una entidad que CRUZA la vista con
// los extremos fuera de pantalla (el caso que un simple "¿algún vértice dentro?"
// perdería): se sigue pudiendo seleccionar por el centro.
void TstMapWidget::viewportCullKeepsCrossingFeatures()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    const QGeoCoordinate centro(21.5, -79.5);
    w.setCenter(centro);
    w.setZoom(12);

    w.addFeatureLayer(QStringLiteral("cruza"), QStringLiteral("Cruza"), 5);
    MapFeature linea;
    linea.layerId = QStringLiteral("cruza");
    linea.kind = GeometryKind::Polyline;
    // Extremos MUY fuera de pantalla (±10° de longitud) pero la linea, a la
    // latitud del centro, pasa por el centro de la vista.
    linea.geometry = {QGeoCoordinate(centro.latitude(), centro.longitude() - 10.0),
                      QGeoCoordinate(centro.latitude(), centro.longitude() + 10.0)};
    const qint64 id = w.addFeature(linea);
    QVERIFY(id > 0);

    // En el pixel central debe encontrarse pese a tener los extremos fuera.
    QCOMPARE(w.featureAt(QPoint(400, 300), 8.0), id);
}

// Una capa marcada TEMPORAL (setFeatureLayerTransient) no se guarda: al recargar
// solo quedan las entidades de las capas guardables.
void TstMapWidget::persistSkipsTransientLayers()
{
    const QString bd = m_dir.filePath(QStringLiteral("transient.db"));
    QFile::remove(bd);
    {
        MapWidget w(baseConfig(m_jsonPath));
        QVERIFY(w.isReady());
        w.addFeatureLayer(QStringLiteral("guardable"), QStringLiteral("Guardable"), 5);
        MapFeature a;
        a.kind = GeometryKind::Point;
        a.layerId = QStringLiteral("guardable");
        a.geometry = {QGeoCoordinate(21.0, -79.0)};
        QVERIFY(w.addFeature(a) > 0);

        w.addFeatureLayer(QStringLiteral("tmp"), QStringLiteral("Temporal"), 6);
        w.setFeatureLayerTransient(QStringLiteral("tmp"));
        MapFeature b = a;
        b.layerId = QStringLiteral("tmp");
        b.geometry = {QGeoCoordinate(21.1, -79.1)};
        QVERIFY(w.addFeature(b) > 0);

        QVERIFY(w.saveFeaturesTo(bd));
    }
    MapWidget w2(baseConfig(m_jsonPath));
    QVERIFY(w2.isReady());
    QVERIFY(w2.loadFeaturesFrom(bd));
    QCOMPARE(w2.featureCount(), 1);                               // la temporal no se guardó
    QCOMPARE(w2.featuresInLayer(QStringLiteral("guardable")).size(), 1);
    QCOMPARE(w2.featuresInLayer(QStringLiteral("tmp")).size(), 0);
}

// Con icono anclado por la PUNTA (0.5,1.0), el área sensible es TODO el pixmap:
// se acierta en la punta y en el cuerpo (arriba), pero no por debajo de la punta.
void TstMapWidget::iconAnchorHitTestCoversPin()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    const QGeoCoordinate centro(21.5, -79.5);
    w.setCenter(centro);
    w.setZoom(12);

    w.addFeatureLayer(QStringLiteral("pines"), QStringLiteral("Pines"), 60);
    MapFeature pin;
    pin.kind = GeometryKind::Point;
    pin.layerId = QStringLiteral("pines");
    pin.geometry = {centro};
    QPixmap pm(24, 32);
    pm.fill(Qt::transparent);
    pin.style.icon = pm;
    pin.style.iconAnchor = QPointF(0.5, 31.0 / 32.0);   // la punta marca el punto
    const qint64 id = w.addFeature(pin);
    QVERIFY(id > 0);

    QCOMPARE(w.featureAt(QPoint(400, 300), 4.0), id);        // la punta
    QCOMPARE(w.featureAt(QPoint(400, 280), 4.0), id);        // cuerpo del pin (arriba)
    QCOMPARE(w.featureAt(QPoint(400, 320), 4.0), qint64(-1)); // por debajo: nada
}

void TstMapWidget::movableLabelHitTestAndOffset()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    const QGeoCoordinate centro(21.5, -79.5);
    w.setCenter(centro);
    w.setZoom(12);

    w.addFeatureLayer(QStringLiteral("obj"), QStringLiteral("Objetivos"), 60);
    MapFeature p;
    p.kind = GeometryKind::Point;
    p.layerId = QStringLiteral("obj");
    p.geometry = {centro};               // cae en el centro de la vista (400,300)
    p.name = QStringLiteral("OBJ");
    const qint64 id = w.addFeature(p);
    QVERIFY(id > 0);

    // Fijar el offset de la etiqueta (y comprobar que persiste en el estilo).
    QVERIFY(w.setFeatureLabelOffset(id, QPointF(50.0, -30.0)));
    const auto f = w.feature(id);
    QVERIFY(f.has_value());
    QCOMPARE(f->style.labelOffsetPx, QPointF(50.0, -30.0));

    // El rotulo ya no esta sobre el punto, sino desplazado: su caja arranca en
    // punto + hueco(10,-8) + offset(50,-30). Un pixel dentro de la caja lo acierta.
    QCOMPARE(w.featureLabelAt(QPoint(465, 266)), id);
    // Sobre el propio punto ya no hay etiqueta (se movio).
    QCOMPARE(w.featureLabelAt(QPoint(400, 300)), qint64(-1));
}

// Cuenta los pixeles "azul de marcacion" (2962ff y sus cercanos) de un recuadro.
static int pixelesAzules(const QImage &img, const QRect &r)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() < 110 && c.green() < 160 && c.blue() > 200)
                ++n;
        }
    return n;
}

// Imagen del widget tras dejar que se atiendan los repintados pendientes (las
// capas agrupan repintados y las teselas llegan de un hilo).
static QImage capturaTrasEventos(MapWidget &w)
{
    QCoreApplication::processEvents();
    QTest::qWait(80);
    QCoreApplication::processEvents();
    return w.grab().toImage();
}

void TstMapWidget::featureLabelsDrawAboveRangeMarks()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    const QGeoCoordinate centro(21.5, -79.5);
    w.setCenter(centro);
    w.setZoom(12);

    w.addFeatureLayer(QStringLiteral("obj"), QStringLiteral("Objetivos"), 60);
    MapFeature p;
    p.kind = GeometryKind::Point;
    p.layerId = QStringLiteral("obj");
    p.geometry = {centro};
    p.name = QStringLiteral("OBJ");
    const qint64 id = w.addFeature(p);
    QVERIFY(id > 0);
    QVERIFY(w.setFeatureLabelOffset(id, QPointF(50.0, -30.0)));   // caja hacia (457,260)

    // Marcacion horizontal que cruza justo el rotulo (y = 266, unos 34 px al norte
    // del centro): a ~35.6 m/px en zoom 12 y esa latitud.
    const QGeoCoordinate fila = centro.atDistanceAndAzimuth(34 * 35.56, 0.0);
    BearingLine l;
    l.from = fila.atDistanceAndAzimuth(150 * 35.56, 270.0);
    l.to = fila.atDistanceAndAzimuth(250 * 35.56, 90.0);
    l.label = false;                      // solo la linea, sin su propio rotulo
    QVERIFY(w.addBearingLine(l) > 0);

    // Los primeros pixeles de la caja (margen y comienzo del texto): la linea
    // pasa por ahi, asi que sin rotulo habria azul; con el rotulo encima, no.
    const QRect zona(458, 264, 6, 5);
    const QImage conRotulo = capturaTrasEventos(w);
    QCOMPARE(pixelesAzules(conRotulo, zona), 0);

    // Prueba de que la linea de verdad pasa por esa zona: sin rotulo, se ve.
    auto sinRotulo = w.feature(id);
    QVERIFY(sinRotulo.has_value());
    sinRotulo->style.labelVisible = false;
    QVERIFY(w.updateFeature(*sinRotulo));
    const QImage sinEl = capturaTrasEventos(w);
    QVERIFY2(pixelesAzules(sinEl, zona) > 0, "la marcacion no cruza la zona de prueba");
}

void TstMapWidget::bearingLabelMovesWithMouse()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    const QGeoCoordinate centro(21.5, -79.5);
    w.setCenter(centro);
    w.setZoom(12);

    BearingLine l;
    l.from = centro.atDistanceAndAzimuth(150 * 35.56, 270.0);
    l.to = centro.atDistanceAndAzimuth(150 * 35.56, 90.0);
    const qint64 id = w.addBearingLine(l);
    QVERIFY(id > 0);
    capturaTrasEventos(w);                 // el rotulo se localiza por lo ya pintado

    // Sin desplazar, el rotulo esta en el centro de la linea.
    QCOMPARE(w.bearingLabelAt(QPoint(400, 300)), id);
    QCOMPARE(w.bearingLabelAt(QPoint(400, 200)), qint64(-1));
    QVERIFY(!w.setBearingLabelOffset(9999, QPointF(1, 1)));

    // Con un desplazamiento, ya no esta sobre la linea sino 100 px mas arriba.
    QVERIFY(w.setBearingLabelOffset(id, QPointF(0.0, -100.0)));
    QCOMPARE(w.bearingLine(id)->labelOffsetPx, QPointF(0.0, -100.0));
    capturaTrasEventos(w);
    QCOMPARE(w.bearingLabelAt(QPoint(400, 200)), id);
    QCOMPARE(w.bearingLabelAt(QPoint(400, 300)), qint64(-1));

    // Arrastrandolo con el raton 60 px a la derecha y 40 abajo.
    auto *vista = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(vista != nullptr);
    const QPoint pulsa(400, 200);
    QMouseEvent p1 = mouseEvent(QEvent::MouseButtonPress, pulsa,
                                Qt::LeftButton, Qt::LeftButton);
    QMouseEvent m1 = mouseEvent(QEvent::MouseMove, pulsa + QPoint(60, 40),
                                Qt::NoButton, Qt::LeftButton);
    QMouseEvent r1 = mouseEvent(QEvent::MouseButtonRelease, pulsa + QPoint(60, 40),
                                Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(vista, &p1);
    QCoreApplication::sendEvent(vista, &m1);
    QCoreApplication::sendEvent(vista, &r1);
    QCOMPARE(w.bearingLine(id)->labelOffsetPx, QPointF(60.0, -60.0));

    // El mapa no se desplazo con el arrastre (era el rotulo, no el fondo).
    QVERIFY(qAbs(w.center().latitude() - centro.latitude()) < 1e-9);
    QVERIFY(qAbs(w.center().longitude() - centro.longitude()) < 1e-9);
}

void TstMapWidget::areaLabelHangsFromFirstVertex()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    const QGeoCoordinate centro(21.5, -79.5);
    w.setCenter(centro);
    w.setZoom(12);

    w.addFeatureLayer(QStringLiteral("areas"), QStringLiteral("Areas"), 60);
    MapFeature area;
    area.kind = GeometryKind::Polygon;
    area.layerId = QStringLiteral("areas");
    area.type = QStringLiteral("objetivo_area");
    area.name = QStringLiteral("Area 1");
    // El PRIMER vertice cae en el centro de la vista (400,300); los demas quedan
    // lejos (al sur/este), asi que el centroide esta muy desplazado.
    area.geometry = {centro, QGeoCoordinate(20.5, -79.5),
                     QGeoCoordinate(20.5, -78.5)};
    area.style.labelAnchorVertex = 0;             // cuelga del primer punto
    const qint64 id = w.addFeature(area);
    QVERIFY(id > 0);

    // La caja de la etiqueta arranca junto al primer vertice (400,300)+hueco.
    QCOMPARE(w.featureLabelAt(QPoint(414, 296)), id);
}

// --------------------------------------------------- raton y teclado ---------

// Evento de raton con modificadores (Qt5 y Qt6).
static QMouseEvent mouseEventMod(QEvent::Type tipo, const QPoint &pos, Qt::MouseButton boton,
                                 Qt::MouseButtons botones, Qt::KeyboardModifiers mods)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QMouseEvent(tipo, QPointF(pos), QPointF(pos), boton, botones, mods);
#else
    return QMouseEvent(tipo, pos, boton, botones, mods);
#endif
}

// Pixel de una coordenada en la vista.
static QPoint pixelDe(MapWidget &w, MapView *v, const QGeoCoordinate &c)
{
    v->ensureLayout();
    const QPointF e = w.toAxisCoords(c);
    return QPoint(qRound(v->xAxis->coordToPixel(e.x())), qRound(v->yAxis->coordToPixel(e.y())));
}

// Manda un evento de raton a la vista.
static void enviar(MapView *v, QEvent::Type t, const QPoint &p, Qt::MouseButton b = Qt::LeftButton,
                   Qt::MouseButtons bs = Qt::LeftButton)
{
    QMouseEvent e = mouseEvent(t, p, t == QEvent::MouseMove ? Qt::NoButton : b, bs);
    QCoreApplication::sendEvent(v, &e);
}

// Pulsar, mover (en un solo salto) y soltar con el boton izquierdo.
static void arrastrar(MapView *v, const QPoint &desde, const QPoint &hasta)
{
    enviar(v, QEvent::MouseButtonPress, desde);
    enviar(v, QEvent::MouseMove, hasta, Qt::LeftButton, Qt::LeftButton);
    enviar(v, QEvent::MouseButtonRelease, hasta, Qt::LeftButton, Qt::NoButton);
}

// Tecla enviada a la vista.
static void tecla(MapView *v, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent e(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(v, &e);
}

void TstMapWidget::wheelAccumulatesFineDeltas()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    QVERIFY(w.maxZoom() > w.minZoom() + 1);
    const int z0 = w.minZoom();
    w.setZoom(z0);

    const auto rueda = [&](int delta) {
        QWheelEvent e(QPointF(400, 300), QPointF(400, 300), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(v, &e);
    };
    for (int i = 0; i < 7; ++i)
        rueda(15);
    QCOMPARE(w.zoom(), z0);             // 105 de 120: aun no
    rueda(15);
    QCOMPARE(w.zoom(), z0 + 1);         // 120 en total: un nivel
    rueda(120);
    QCOMPARE(w.zoom(), z0 + 2);         // una muesca entera, como siempre
    rueda(-120);
    QCOMPARE(w.zoom(), z0 + 1);
    // Un cambio de sentido olvida lo acumulado.
    rueda(100);
    rueda(-100);
    QCOMPARE(w.zoom(), z0 + 1);
}

void TstMapWidget::navigateGesturesAreDecidedOnRelease()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(w.minZoom() + 1);
    const QGeoCoordinate c(22.0, -80.0);
    w.setCenter(c);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    MapTarget t;
    t.position = c;
    const qint64 id = w.addTarget(t);
    QSignalSpy clics(&w, &MapWidget::targetClicked);
    const QPoint p = pixelDe(w, v, c);

    // Agarrar el objetivo y arrastrar mueve el mapa (antes, no se movia) y no lo
    // selecciona.
    const QGeoCoordinate antes = w.center();
    arrastrar(v, p, p + QPoint(80, 40));
    QVERIFY(antes.distanceTo(w.center()) > 100.0);
    QCOMPARE(w.selectedTarget(), qint64(-1));
    QCOMPARE(clics.count(), 0);

    // Pulsar y soltar sin mover: se selecciona AL SOLTAR.
    w.setCenter(c);
    const QPoint q = pixelDe(w, v, c);
    enviar(v, QEvent::MouseButtonPress, q);
    QCOMPARE(w.selectedTarget(), qint64(-1));
    enviar(v, QEvent::MouseButtonRelease, q, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(w.selectedTarget(), id);
    QCOMPARE(clics.count(), 1);

    // Un clic en el vacio deselecciona.
    clic(v, q + QPoint(250, 150));
    QCOMPARE(w.selectedTarget(), qint64(-1));

    // Esc tambien (antes un objetivo seleccionado no se podia deseleccionar).
    clic(v, q);
    QCOMPARE(w.selectedTarget(), id);
    tecla(v, Qt::Key_Escape);
    QCOMPARE(w.selectedTarget(), qint64(-1));

    // En Navegar, un clic sobre una entidad la selecciona (antes no).
    MapFeature pt;
    pt.kind = GeometryKind::Point;
    pt.layerId = w.activeFeatureLayer();
    pt.geometry = { c.atDistanceAndAzimuth(600000.0, 90.0) };   // lejos del objetivo
    const qint64 idPt = w.addFeature(pt);
    clic(v, pixelDe(w, v, pt.geometry.first()));
    QCOMPARE(w.selectedFeature(), idPt);
    clic(v, q + QPoint(0, 200));                        // vacio: deselecciona
    QCOMPARE(w.selectedFeature(), qint64(-1));
}

void TstMapWidget::doubleClickZoomsAndNeverLosesTheDrag()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(w.minZoom());
    w.setCenter(QGeoCoordinate(22.0, -80.0));
    auto *v = qobject_cast<MapView *>(w.customPlot());
    const QPoint p(400, 300);

    // Doble clic en el vacio: Qt manda pulsar, soltar, DOBLE CLIC, soltar.
    const int z0 = w.zoom();
    enviar(v, QEvent::MouseButtonPress, p);
    enviar(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    enviar(v, QEvent::MouseButtonDblClick, p);
    enviar(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(w.zoom(), z0 + 1);

    // Un clic y, enseguida, un clic-arrastre: el doble clic ocupa el sitio del
    // segundo pulsado y el arrastre debe mover el mapa igualmente.
    w.setCenter(QGeoCoordinate(22.0, -80.0));
    const QGeoCoordinate antes = w.center();
    enviar(v, QEvent::MouseButtonPress, p);
    enviar(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    enviar(v, QEvent::MouseButtonDblClick, p);
    enviar(v, QEvent::MouseMove, p + QPoint(100, 0), Qt::LeftButton, Qt::LeftButton);
    enviar(v, QEvent::MouseButtonRelease, p + QPoint(100, 0), Qt::LeftButton, Qt::NoButton);
    QVERIFY(antes.distanceTo(w.center()) > 100.0);
    QCOMPARE(w.zoom(), z0 + 1);                         // y no acerco

    // Con una herramienta de dibujo, el segundo clic rapido tampoco se pierde.
    w.setActiveTool(MapTool::DrawPoint);
    const int n0 = w.featureCount();
    enviar(v, QEvent::MouseButtonPress, p);
    enviar(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    enviar(v, QEvent::MouseButtonDblClick, p + QPoint(2, 0));
    enviar(v, QEvent::MouseButtonRelease, p + QPoint(2, 0), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(w.featureCount(), n0 + 2);
}

void TstMapWidget::clickedIsNotEmittedAfterDragging()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(w.minZoom() + 1);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    QSignalSpy clics(&w, &MapWidget::clicked);

    arrastrar(v, QPoint(300, 300), QPoint(500, 380));
    QCOMPARE(clics.count(), 0);
    clic(v, QPoint(300, 300));
    QCOMPARE(clics.count(), 1);
    // Un temblor de 2 px sigue siendo un clic.
    enviar(v, QEvent::MouseButtonPress, QPoint(300, 300));
    enviar(v, QEvent::MouseMove, QPoint(302, 301), Qt::LeftButton, Qt::LeftButton);
    enviar(v, QEvent::MouseButtonRelease, QPoint(302, 301), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(clics.count(), 2);
}

void TstMapWidget::editNeedsARealDragAndRespectsLockedLayers()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(12);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    QSignalSpy bloqueos(&w, &MapWidget::interactionBlocked);

    w.setActiveTool(MapTool::DrawPolygon);
    const QVector<QPoint> vertices{{200, 200}, {500, 200}, {500, 450}, {200, 450}};
    for (const QPoint &p : vertices)
        clic(v, p);
    const qint64 id = w.finishDrawing();
    QVERIFY(id > 0);
    w.setActiveTool(MapTool::EditFeature);
    clic(v, QPoint(350, 300));
    QCOMPARE(w.selectedFeature(), id);

    // Un clic con 1 px de temblor no mueve la entidad ni apila un deshacer.
    const auto original = w.feature(id)->geometry;
    enviar(v, QEvent::MouseButtonPress, QPoint(350, 300));
    enviar(v, QEvent::MouseMove, QPoint(351, 301), Qt::LeftButton, Qt::LeftButton);
    enviar(v, QEvent::MouseButtonRelease, QPoint(351, 301), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(w.feature(id)->geometry, original);

    // Un arrastre real la mueve, y se deshace de una vez.
    arrastrar(v, QPoint(350, 300), QPoint(410, 340));
    QVERIFY(w.feature(id)->geometry != original);
    QVERIFY(w.undo());
    QCOMPARE(w.feature(id)->geometry, original);

    // Ctrl + clic en un tirador quita ese vertice (4 -> 3) y un 4o intento se
    // rechaza con aviso: un poligono no baja de tres.
    {
        QMouseEvent p1 = mouseEventMod(QEvent::MouseButtonPress, vertices[1], Qt::LeftButton,
                                       Qt::LeftButton, Qt::ControlModifier);
        QMouseEvent r1 = mouseEventMod(QEvent::MouseButtonRelease, vertices[1], Qt::LeftButton,
                                       Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(v, &p1);
        QCoreApplication::sendEvent(v, &r1);
    }
    QCOMPARE(w.feature(id)->geometry.size(), qsizetype(3));
    QCOMPARE(bloqueos.count(), 0);
    {
        QMouseEvent p1 = mouseEventMod(QEvent::MouseButtonPress, vertices[2], Qt::LeftButton,
                                       Qt::LeftButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(v, &p1);
    }
    QCOMPARE(w.feature(id)->geometry.size(), qsizetype(3));
    QCOMPARE(bloqueos.count(), 1);

    // Capa bloqueada: se puede seleccionar pero no mover, ni dibujar en ella.
    v->overlayModel()->setLayerEditable(w.activeFeatureLayer(), false);
    const auto antes = w.feature(id)->geometry;
    arrastrar(v, QPoint(300, 400), QPoint(360, 440));      // dentro del triangulo que queda
    QCOMPARE(w.feature(id)->geometry, antes);
    QVERIFY(bloqueos.count() >= 2);
    const int n0 = w.featureCount();
    w.setActiveTool(MapTool::DrawPoint);
    clic(v, QPoint(100, 100));
    QCOMPARE(w.featureCount(), n0);
    QVERIFY(bloqueos.count() >= 3);
}

void TstMapWidget::rightClickAndEscCancelPendingOperations()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(w.minZoom() + 1);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    QSignalSpy menu(&w, &MapWidget::contextMenuRequested);
    QSignalSpy captura(&w, &MapWidget::pickCancelled);
    const auto derecho = [&](const QPoint &p) {
        enviar(v, QEvent::MouseButtonPress, p, Qt::RightButton, Qt::RightButton);
        enviar(v, QEvent::MouseButtonRelease, p, Qt::RightButton, Qt::NoButton);
    };

    // Medir con el primer punto puesto: el clic derecho lo cancela y NO abre el menu.
    w.setActiveTool(MapTool::Measure);
    clic(v, QPoint(300, 300));
    derecho(QPoint(350, 300));
    QCOMPARE(menu.count(), 0);
    derecho(QPoint(350, 300));                          // ya no hay nada pendiente
    QCOMPARE(menu.count(), 1);

    // Esc tambien cancela la medida (y no hace nada mas).
    clic(v, QPoint(300, 300));
    tecla(v, Qt::Key_Escape);
    derecho(QPoint(350, 300));
    QCOMPARE(menu.count(), 2);

    // Captura de un punto: Esc y clic derecho la cancelan y vuelven a Navegar.
    w.setActiveTool(MapTool::PickPoint);
    tecla(v, Qt::Key_Escape);
    QCOMPARE(captura.count(), 1);
    QCOMPARE(w.activeTool(), MapTool::None);
    w.setActiveTool(MapTool::PickPoint);
    derecho(QPoint(350, 300));
    QCOMPARE(captura.count(), 2);
    QCOMPARE(w.activeTool(), MapTool::None);
    QCOMPARE(menu.count(), 2);                          // sin menu
}

void TstMapWidget::middleButtonAndKeysMoveTheView()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    QVERIFY(w.maxZoom() > w.minZoom() + 1);
    w.setZoom(w.minZoom() + 1);
    const QGeoCoordinate c(22.0, -80.0);
    w.setCenter(c);
    auto *v = qobject_cast<MapView *>(w.customPlot());

    // Boton central: mueve el mapa aunque la herramienta sea de dibujo.
    w.setActiveTool(MapTool::DrawPolygon);
    enviar(v, QEvent::MouseButtonPress, QPoint(400, 300), Qt::MiddleButton, Qt::MiddleButton);
    enviar(v, QEvent::MouseMove, QPoint(500, 300), Qt::MiddleButton, Qt::MiddleButton);
    enviar(v, QEvent::MouseButtonRelease, QPoint(500, 300), Qt::MiddleButton, Qt::NoButton);
    QVERIFY(c.distanceTo(w.center()) > 100.0);
    QVERIFY(w.center().longitude() < c.longitude());    // arrastrar a la derecha: la vista va al oeste
    QVERIFY(!w.isDrawing());                            // y no pone vertices

    // Flechas: la vista va hacia donde apunta la tecla; Mayus, mas lejos.
    w.setActiveTool(MapTool::None);
    w.setCenter(c);
    tecla(v, Qt::Key_Right);
    const double este = w.center().longitude();
    QVERIFY(este > c.longitude());
    w.setCenter(c);
    tecla(v, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(w.center().longitude() > este);
    w.setCenter(c);
    tecla(v, Qt::Key_Up);
    QVERIFY(w.center().latitude() > c.latitude());
    tecla(v, Qt::Key_Down);
    tecla(v, Qt::Key_Down);
    QVERIFY(w.center().latitude() < c.latitude());

    // + y -.
    const int z = w.zoom();
    tecla(v, Qt::Key_Plus);
    QCOMPARE(w.zoom(), z + 1);
    tecla(v, Qt::Key_Minus);
    QCOMPARE(w.zoom(), z);
}

void TstMapWidget::hoverCursorAndKeyboardContextMenu()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(w.minZoom() + 1);
    const QGeoCoordinate c(22.0, -80.0);
    w.setCenter(c);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    MapTarget t;
    t.position = c;
    w.addTarget(t);

    const QPoint sobre = pixelDe(w, v, c);
    enviar(v, QEvent::MouseMove, sobre, Qt::NoButton, Qt::NoButton);
    QCOMPARE(v->cursor().shape(), Qt::PointingHandCursor);        // algo que agarrar
    QTest::qWait(45);                                              // el hover se limita a uno cada 30 ms
    enviar(v, QEvent::MouseMove, sobre + QPoint(200, 150), Qt::NoButton, Qt::NoButton);
    QCOMPARE(v->cursor().shape(), Qt::ArrowCursor);

    // Menu con el teclado (tecla de menu): se emite; con el raton, por Qt, no.
    QSignalSpy menu(&w, &MapWidget::contextMenuRequested);
    QContextMenuEvent teclado(QContextMenuEvent::Keyboard, QPoint(300, 200), v->mapToGlobal(QPoint(300, 200)));
    QCoreApplication::sendEvent(v, &teclado);
    QCOMPARE(menu.count(), 1);
    QContextMenuEvent raton(QContextMenuEvent::Mouse, QPoint(300, 200), v->mapToGlobal(QPoint(300, 200)));
    QCoreApplication::sendEvent(v, &raton);
    QCOMPARE(menu.count(), 1);
}

void TstMapWidget::elementClickedOnlyWithoutDragging()
{
    MapWidget w(baseConfig(m_jsonPath));
    QVERIFY(w.isReady());
    w.resize(800, 600);
    w.setZoom(w.minZoom() + 1);
    const QGeoCoordinate c(22.0, -80.0);
    w.setCenter(c);
    auto *v = qobject_cast<MapView *>(w.customPlot());
    MapTarget t;
    t.position = c;
    const qint64 idT = w.addTarget(t);
    MapFeature pt;
    pt.kind = GeometryKind::Point;
    pt.layerId = w.activeFeatureLayer();
    pt.geometry = { c.atDistanceAndAzimuth(600000.0, 90.0) };   // lejos del objetivo
    const qint64 idP = w.addFeature(pt);
    QSignalSpy tarjetas(&w, &MapWidget::elementClicked);

    clic(v, pixelDe(w, v, c));
    QCOMPARE(tarjetas.count(), 1);
    const MapHit h1 = tarjetas.at(0).at(0).value<MapHit>();
    QCOMPARE(h1.targetId, idT);

    clic(v, pixelDe(w, v, pt.geometry.first()));
    QCOMPARE(tarjetas.count(), 2);
    QCOMPARE(tarjetas.at(1).at(0).value<MapHit>().featureId, idP);

    // Arrastrar desde un elemento mueve el mapa y no abre tarjeta.
    arrastrar(v, pixelDe(w, v, c), pixelDe(w, v, c) + QPoint(90, 50));
    QCOMPARE(tarjetas.count(), 2);

    // Clic en vacio: nada. Y solo con Navegar.
    clic(v, QPoint(30, 30));
    QCOMPARE(tarjetas.count(), 2);
    w.setActiveTool(MapTool::Measure);
    clic(v, pixelDe(w, v, pt.geometry.first()));
    QCOMPARE(tarjetas.count(), 2);
}

void TstMapWidget::layerIndexStaysConsistent()
{
    OverlayModel m;
    m.addLayer(QStringLiteral("a"));
    m.addLayer(QStringLiteral("b"));
    const auto punto = [](const QString &capa, double lat) {
        MapFeature f;
        f.kind = GeometryKind::Point;
        f.layerId = capa;
        f.geometry = { QGeoCoordinate(lat, -80.0) };
        return f;
    };
    const auto ids = [&m](const QString &capa) {
        QVector<qint64> v;
        m.forEachInLayer(capa, [&v](const MapFeature &f) { v.append(f.id); return true; });
        return v;
    };
    const qint64 f1 = m.addFeature(punto(QStringLiteral("a"), 22.0));
    const qint64 f2 = m.addFeature(punto(QStringLiteral("a"), 22.1));
    const qint64 f3 = m.addFeature(punto(QStringLiteral("b"), 22.2));
    QCOMPARE(ids(QStringLiteral("a")), QVector<qint64>({ f1, f2 }));
    QCOMPARE(ids(QStringLiteral("b")), QVector<qint64>({ f3 }));
    QVERIFY(ids(QStringLiteral("nada")).isEmpty());

    // Mover un vertice no cambia el indice; cambiar una entidad de capa, si.
    QVERIFY(m.moveVertex(f1, 0, QGeoCoordinate(22.5, -80.0)));
    QCOMPARE(ids(QStringLiteral("a")), QVector<qint64>({ f1, f2 }));
    MapFeature cambiada = *m.feature(f2);
    cambiada.layerId = QStringLiteral("b");
    QVERIFY(m.updateFeature(cambiada));
    QCOMPARE(ids(QStringLiteral("a")), QVector<qint64>({ f1 }));
    QCOMPARE(ids(QStringLiteral("b")), QVector<qint64>({ f2, f3 }));      // siempre por id

    // Baja y deshacer.
    QVERIFY(m.removeFeature(f1));
    QVERIFY(ids(QStringLiteral("a")).isEmpty());
    QVERIFY(m.undo());
    QCOMPARE(ids(QStringLiteral("a")), QVector<qint64>({ f1 }));

    // Parar el recorrido.
    int visitadas = 0;
    m.forEachInLayer(QStringLiteral("b"), [&visitadas](const MapFeature &) { ++visitadas; return false; });
    QCOMPARE(visitadas, 1);
    m.clearLayer(QStringLiteral("b"));
    QVERIFY(ids(QStringLiteral("b")).isEmpty());
}



// --- Mapa híbrido --------------------------------------------------------

namespace {

// Varint de protobuf (para fabricar teselas MVT de prueba).
void pbVarint(QByteArray &b, quint64 v)
{
    while (v >= 0x80) {
        b.append(char((v & 0x7f) | 0x80));
        v >>= 7;
    }
    b.append(char(v));
}

// Campo con longitud (sub-mensaje o texto).
void pbBytes(QByteArray &b, int field, const QByteArray &data)
{
    pbVarint(b, quint64((field << 3) | 2));
    pbVarint(b, quint64(data.size()));
    b.append(data);
}

// Tesela MVT con una capa "boundary" y una línea horizontal por el centro de la
// tesela, admin_level = 2 (frontera de país): la pinta el estilo híbrido en su
// grupo "limites" a cualquier zoom.
QByteArray boundaryTile()
{
    auto zz = [](int v) { return quint64((v << 1) ^ (v >> 31)); };
    QByteArray geom;
    pbVarint(geom, (1 & 7) | (1 << 3));                  // MoveTo 1
    pbVarint(geom, zz(-100));
    pbVarint(geom, zz(2048));
    pbVarint(geom, (2 & 7) | (1 << 3));                  // LineTo 1
    pbVarint(geom, zz(4296));
    pbVarint(geom, zz(0));
    QByteArray tags;
    pbVarint(tags, 0);
    pbVarint(tags, 0);
    QByteArray feat;
    pbBytes(feat, 2, tags);
    pbVarint(feat, (3 << 3) | 0);
    pbVarint(feat, 2);                                   // LineString
    pbBytes(feat, 4, geom);
    QByteArray value;
    pbBytes(value, 1, QByteArray("2"));
    QByteArray layer;
    pbVarint(layer, (15 << 3) | 0);
    pbVarint(layer, 2);
    pbBytes(layer, 1, QByteArray("boundary"));
    pbBytes(layer, 2, feat);
    pbBytes(layer, 3, QByteArray("admin_level"));
    pbBytes(layer, 4, value);
    pbVarint(layer, (5 << 3) | 0);
    pbVarint(layer, 4096);
    QByteArray tile;
    pbBytes(tile, 3, layer);
    return tile;
}

// Tesela MVT con tres POI junto al centro (a z4 cada unidad de tesela son ~600 m, así que
// quedan a menos de 3 km): una farmacia con nombre, otra sin nombre y un hospital. Las
// claves y los valores van en las tablas de la capa.
QByteArray poiTile()
{
    auto zz = [](int v) { return quint64((v << 1) ^ (v >> 31)); };
    const QStringList keys{ QStringLiteral("class"), QStringLiteral("name") };
    const QStringList values{ QStringLiteral("pharmacy"), QStringLiteral("Farmacia Central"),
                              QStringLiteral("hospital"), QStringLiteral("Hospital Calixto García") };
    auto point = [&](int px, int py, int kv1, int vv1, int kv2, int vv2) {
        QByteArray geom;
        pbVarint(geom, (1 & 7) | (1 << 3));
        pbVarint(geom, zz(px));
        pbVarint(geom, zz(py));
        QByteArray tags;
        pbVarint(tags, quint64(kv1)); pbVarint(tags, quint64(vv1));
        if (kv2 >= 0) { pbVarint(tags, quint64(kv2)); pbVarint(tags, quint64(vv2)); }
        QByteArray feat;
        pbBytes(feat, 2, tags);
        pbVarint(feat, (3 << 3) | 0);
        pbVarint(feat, 1);                               // Point
        pbBytes(feat, 4, geom);
        return feat;
    };
    QByteArray layer;
    pbVarint(layer, (15 << 3) | 0);
    pbVarint(layer, 2);
    pbBytes(layer, 1, QByteArray("poi"));
    pbBytes(layer, 2, point(2048, 2048, 0, 0, 1, 1));    // farmacia con nombre, en el centro
    pbBytes(layer, 2, point(2048, 2052, 0, 0, -1, -1));  // farmacia sin nombre, a ~2 km
    pbBytes(layer, 2, point(2052, 2048, 0, 2, 1, 3));    // hospital, a ~2 km
    for (const QString &k : keys)
        pbBytes(layer, 3, k.toUtf8());
    for (const QString &v : values) {
        QByteArray val;
        pbBytes(val, 1, v.toUtf8());
        pbBytes(layer, 4, val);
    }
    pbVarint(layer, (5 << 3) | 0);
    pbVarint(layer, 4096);
    QByteArray tile;
    pbBytes(tile, 3, layer);
    return tile;
}

// Tesela MVT con una calle, «Obispo», que cruza la tesela de lado a lado por el centro
// (capa transportation_name, class minor). La geometría sale 100 unidades por cada lado
// para que, en teselas vecinas, los tramos se toquen.
QByteArray streetTile()
{
    auto zz = [](int v) { return quint64((v << 1) ^ (v >> 31)); };
    QByteArray geom;
    pbVarint(geom, (1 & 7) | (1 << 3));                  // MoveTo 1
    pbVarint(geom, zz(-100));
    pbVarint(geom, zz(2048));
    pbVarint(geom, (2 & 7) | (1 << 3));                  // LineTo 1
    pbVarint(geom, zz(4296));
    pbVarint(geom, zz(0));
    QByteArray tags;
    pbVarint(tags, 0); pbVarint(tags, 0);                // name = Obispo
    pbVarint(tags, 1); pbVarint(tags, 1);                // class = minor
    QByteArray feat;
    pbBytes(feat, 2, tags);
    pbVarint(feat, (3 << 3) | 0);
    pbVarint(feat, 2);                                   // LineString
    pbBytes(feat, 4, geom);
    QByteArray layer;
    pbVarint(layer, (15 << 3) | 0);
    pbVarint(layer, 2);
    pbBytes(layer, 1, QByteArray("transportation_name"));
    pbBytes(layer, 2, feat);
    pbBytes(layer, 3, QByteArray("name"));
    pbBytes(layer, 3, QByteArray("class"));
    for (const char *v : { "Obispo", "minor" }) {
        QByteArray val;
        pbBytes(val, 1, QByteArray(v));
        pbBytes(layer, 4, val);
    }
    pbVarint(layer, (5 << 3) | 0);
    pbVarint(layer, 4096);
    QByteArray tile;
    pbBytes(tile, 3, layer);
    return tile;
}

// Tesela MVT con dos vías que se cruzan en el centro (vértice compartido en 2048,2048): una
// carretera principal horizontal y una calle vertical, de borde a borde (capa transportation).
QByteArray roadTile()
{
    auto zz = [](int v) { return quint64((v << 1) ^ (v >> 31)); };
    // Línea de tres vértices (x0,y0) (x1,y1) (x2,y2): MoveTo y LineTo con dos desplazamientos.
    auto linea = [&](int x0, int y0, int x1, int y1, int x2, int y2, int clase) {
        QByteArray geom;
        pbVarint(geom, (1 & 7) | (1 << 3));
        pbVarint(geom, zz(x0)); pbVarint(geom, zz(y0));
        pbVarint(geom, (2 & 7) | (2 << 3));
        pbVarint(geom, zz(x1 - x0)); pbVarint(geom, zz(y1 - y0));
        pbVarint(geom, zz(x2 - x1)); pbVarint(geom, zz(y2 - y1));
        QByteArray tags;
        pbVarint(tags, 0); pbVarint(tags, quint64(clase));
        QByteArray feat;
        pbBytes(feat, 2, tags);
        pbVarint(feat, (3 << 3) | 0);
        pbVarint(feat, 2);                               // LineString
        pbBytes(feat, 4, geom);
        return feat;
    };
    QByteArray layer;
    pbVarint(layer, (15 << 3) | 0);
    pbVarint(layer, 2);
    pbBytes(layer, 1, QByteArray("transportation"));
    pbBytes(layer, 2, linea(0, 2048, 2048, 2048, 4096, 2048, 0));    // principal horizontal
    pbBytes(layer, 2, linea(2048, 0, 2048, 2048, 2048, 4096, 1));    // calle vertical
    pbBytes(layer, 3, QByteArray("class"));
    for (const char *v : { "primary", "minor" }) {
        QByteArray val;
        pbBytes(val, 1, QByteArray(v));
        pbBytes(layer, 4, val);
    }
    pbVarint(layer, (5 << 3) | 0);
    pbVarint(layer, 4096);
    QByteArray tile;
    pbBytes(tile, 3, layer);
    return tile;
}

// MBTiles de los zooms 3 y 4 del mundo entero: PNG gris liso o, con \a vector,
// la tesela MVT de arriba en todas las posiciones.
bool makeWorldMBTiles(const QString &path, bool vector, const QByteArray &vectorData = QByteArray())
{
    const QString conn = QStringLiteral("mk_%1").arg(path);
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        ok = q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                   "tile_row INTEGER, tile_data BLOB)"))
             && q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)"))
             && q.exec(QStringLiteral("INSERT INTO metadata VALUES ('format','%1'), ('minzoom','3'), "
                                      "('maxzoom','4')").arg(vector ? "pbf" : "png"));
        QByteArray data = vectorData.isEmpty() ? boundaryTile() : vectorData;
        if (!vector) {
            QImage img(256, 256, QImage::Format_RGB32);
            img.fill(QColor(100, 100, 100));
            QBuffer buf(&data);
            data.clear();
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
        }
        db.transaction();
        for (int z = 3; ok && z <= 4; ++z)
            for (int x = 0; x < (1 << z); ++x)
                for (int y = 0; y < (1 << z); ++y) {
                    q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
                    q.bindValue(QStringLiteral(":z"), z);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), y);
                    q.bindValue(QStringLiteral(":d"), data);
                    ok = ok && q.exec();
                }
        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

// MBTiles vectorial con UNA sola tesela, a zoom \a z, en la posición XYZ (x, y) (la fila
// se guarda en TMS, como hace un MBTiles de verdad). Sirve para probar a zoom 14, donde el
// de makeWorldMBTiles (z3-z4) no llega.
bool makeSingleTileMBTiles(const QString &path, const QByteArray &data, int z, int x, int y)
{
    const QString conn = QStringLiteral("mk1_%1").arg(path);
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        ok = q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                   "tile_row INTEGER, tile_data BLOB)"))
             && q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)"))
             && q.exec(QStringLiteral("INSERT INTO metadata VALUES ('format','pbf'), ('minzoom','%1'), "
                                      "('maxzoom','%1')").arg(z));
        q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
        q.bindValue(QStringLiteral(":z"), z);
        q.bindValue(QStringLiteral(":x"), x);
        q.bindValue(QStringLiteral(":y"), (1 << z) - 1 - y);
        q.bindValue(QStringLiteral(":d"), data);
        ok = ok && q.exec();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

// Píxeles de la imagen que no son el gris de la base.
int notGray(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c(img.pixel(x, y));
            n += qAbs(c.red() - 100) > 12 || qAbs(c.green() - 100) > 12 || qAbs(c.blue() - 100) > 12;
        }
    return n;
}

} // namespace

// Mapa híbrido: un MBTiles vectorial ENCIMA de una base ráster cualquiera. Se ve
// (las fronteras del estilo híbrido sobre el gris), se apaga entero o por grupos,
// los grupos que el estilo marca apagados arrancan así, y un fichero ráster no
// sirve de overlay.
void TstMapWidget::hybridOverlayOverAnyBase()
{
    const QString base = m_dir.filePath(QStringLiteral("gris.mbtiles"));
    const QString vec = m_dir.filePath(QStringLiteral("fronteras.mbtiles"));
    QVERIFY(makeWorldMBTiles(base, false));
    QVERIFY(makeWorldMBTiles(vec, true));
    const QString json = m_dir.filePath(QStringLiteral("gris.json"));
    {
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("datasets"),
            QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("gris") },
                                     { QStringLiteral("filePath"), base } } } } }).toJson());
    }
    MapConfig cfg = baseConfig(json);
    cfg.initialZoom = 4;
    cfg.initialCenter = QGeoCoordinate(40.0, -100.0);
    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    w.resize(600, 400);
    QTRY_VERIFY_WITH_TIMEOUT(w.exactCoverage() > 0.99, 20000);
    QVERIFY(!w.hasVectorOverlay());
    // Lo que no es gris con la base sola (en Qt 5 queda alguna costura de 1 px
    // entre teselas): la referencia para saber si el overlay se ve o no.
    QTest::qWait(300);
    const int sinOverlay = notGray(w.grab().toImage());
    QVERIFY2(sinOverlay < 400, qPrintable(QString::number(sinOverlay)));

    QString error;
    QVERIFY(!w.setVectorOverlay(base, &error));                  // ráster: no
    QVERIFY2(error.contains(QStringLiteral("vectorial")), qPrintable(error));

    QSignalSpy cambios(&w, &MapWidget::vectorOverlayChanged);
    QVERIFY2(w.setVectorOverlay(vec, &error), qPrintable(error));
    QVERIFY(w.hasVectorOverlay());
    QVERIFY(w.isVectorOverlayVisible());
    QCOMPARE(QFileInfo(w.vectorOverlayFile()).fileName(), QStringLiteral("fronteras.mbtiles"));
    QVERIFY(cambios.count() >= 1);
    QCOMPARE(w.baseLayerId(), QStringLiteral("gris"));           // la base no cambia

    QStringList grupos;
    bool limites = false;
    for (const BaseLayerGroup &g : w.vectorOverlayGroups()) {
        grupos << g.id;
        if (g.id == QLatin1String("limites")) limites = g.visible;
    }
    QCOMPARE(grupos, QStringList({ "vias", "nombres_calles", "lugares", "cimas", "limites", "poi_salud",
                                   "poi_seguridad", "poi_combustible", "poi_alojamiento", "poi_transporte" }));
    QVERIFY(limites);
    QVERIFY(w.baseLayerGroups().isEmpty());                     // la base es ráster

    // Las fronteras se ven sobre el gris.
    QTRY_VERIFY_WITH_TIMEOUT(notGray(w.grab().toImage()) > sinOverlay + 50, 20000);

    // Apagar su grupo: desaparecen; encenderlo: vuelven.
    QVERIFY(w.setVectorOverlayGroupVisible(QStringLiteral("limites"), false));
    QTRY_VERIFY_WITH_TIMEOUT(notGray(w.grab().toImage()) <= sinOverlay, 20000);
    QVERIFY(w.setVectorOverlayGroupVisible(QStringLiteral("limites"), true));
    QTRY_VERIFY_WITH_TIMEOUT(notGray(w.grab().toImage()) > sinOverlay + 50, 20000);
    QVERIFY(!w.setVectorOverlayGroupVisible(QStringLiteral("no_existe"), false));

    // «Que hay aqui»: la frontera cruza por la mitad de cada tesela, asi que el
    // centro de una tesela z4 la tiene debajo; un poco mas arriba (a un 10 %) no.
    {
        auto lugar = [](int y, double fy) {
            const double pi = 3.14159265358979323846;
            return QGeoCoordinate(std::atan(std::sinh(pi * (1.0 - 2.0 * (y + fy) / 16.0))) * 180.0 / pi,
                                  (7.5 / 16.0) * 360.0 - 180.0);
        };
        const auto aqui = w.vectorFeaturesAt(lugar(6, 0.5), 20000.0);
        QCOMPARE(aqui.size(), 1);
        QCOMPARE(aqui[0].layer, QStringLiteral("boundary"));
        QCOMPARE(aqui[0].geometry, QStringLiteral("line"));
        QCOMPARE(aqui[0].attributes.value(QStringLiteral("admin_level")).toString(), QStringLiteral("2"));
        QVERIFY(aqui[0].fromOverlay);
        QVERIFY(aqui[0].distanceM < 2000.0);
        QVERIFY(w.vectorFeaturesAt(lugar(6, 0.1), 20000.0).isEmpty());
        // Sin radio, usa unos 8 px: en la linea si, a un 10 % de tesela (~25 px) no.
        QCOMPARE(w.vectorFeaturesAt(lugar(6, 0.5)).size(), 1);
        QVERIFY(w.vectorFeaturesAt(lugar(6, 0.1)).isEmpty());
    }

    // Apagar el overlay entero, y quitarlo.
    w.setVectorOverlayVisible(false);
    QVERIFY(w.vectorFeaturesAt(QGeoCoordinate(40.0, -100.0), 1e6).isEmpty());   // base ráster
    w.setVectorOverlayVisible(true);
    w.setVectorOverlayVisible(false);
    QVERIFY(!w.isVectorOverlayVisible());
    QTRY_VERIFY_WITH_TIMEOUT(notGray(w.grab().toImage()) <= sinOverlay, 20000);
    w.setVectorOverlayVisible(true);
    QTRY_VERIFY_WITH_TIMEOUT(notGray(w.grab().toImage()) > sinOverlay + 50, 20000);
    w.clearVectorOverlay();
    QVERIFY(!w.hasVectorOverlay());
    QTRY_VERIFY_WITH_TIMEOUT(notGray(w.grab().toImage()) <= sinOverlay, 20000);
}

// Buscar puntos de interés en el MBTiles vectorial (overlay híbrido) alrededor del
// centro del mapa: por nombre, por tipo en español y en inglés; el orden por relevancia;
// que no salga nada sin capa vectorial, con una letra o fuera del radio; y que ir a
// un POI centre el mapa en él.
void TstMapWidget::searchesPointsOfInterest()
{
    const QString base = m_dir.filePath(QStringLiteral("gris_poi.mbtiles"));
    const QString vec = m_dir.filePath(QStringLiteral("poi.mbtiles"));
    QVERIFY(makeWorldMBTiles(base, false));
    QVERIFY(makeWorldMBTiles(vec, true, poiTile()));
    const QString json = m_dir.filePath(QStringLiteral("gris_poi.json"));
    {
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("datasets"),
            QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("gris") },
                                     { QStringLiteral("filePath"), base } } } } }).toJson());
    }
    // Centro de la tesela z4 (7, 6): ahí está el POI de (2048, 2048).
    const double pi = 3.14159265358979323846;
    const QGeoCoordinate centro(std::atan(std::sinh(pi * (1.0 - 2.0 * 6.5 / 16.0))) * 180.0 / pi,
                                (7.5 / 16.0) * 360.0 - 180.0);
    MapConfig cfg = baseConfig(json);
    cfg.initialZoom = 4;
    cfg.initialCenter = centro;
    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    w.resize(600, 400);

    // Sin capa vectorial (la base es ráster) no hay donde buscar.
    QVERIFY(!w.canSearchPointsOfInterest());
    QVERIFY(w.searchPointsOfInterest(QStringLiteral("farmacia")).isEmpty());

    QString error;
    QVERIFY2(w.setVectorOverlay(vec, &error), qPrintable(error));
    QVERIFY(w.canSearchPointsOfInterest());

    auto r = w.searchPointsOfInterest(QStringLiteral("calixto"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].name, QStringLiteral("Hospital Calixto García"));
    QCOMPARE(r[0].typeLabel(), QStringLiteral("hospital"));
    QVERIFY(r[0].distanceM < 6000.0);

    // «farmacia» da las dos (la sin nombre también); la que tiene nombre, primero.
    r = w.searchPointsOfInterest(QStringLiteral("farmacia"));
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].name, QStringLiteral("Farmacia Central"));
    QVERIFY(r[1].name.isEmpty());
    QCOMPARE(w.searchPointsOfInterest(QStringLiteral("pharmacy")).size(), 2);   // en inglés vale
    QCOMPARE(w.searchPointsOfInterest(QStringLiteral("farmacia"), 1).size(), 1);  // límite
    // Repetir la búsqueda (ahora con las teselas en la caché) da lo mismo.
    QCOMPARE(w.searchPointsOfInterest(QStringLiteral("farmacia")).size(), 2);

    QVERIFY(w.searchPointsOfInterest(QStringLiteral("f")).isEmpty());           // una letra: nada
    QVERIFY(w.searchPointsOfInterest(QStringLiteral("zzzz")).isEmpty());

    // Fuera del radio no sale: con el centro lejos del POI no hay resultados.
    w.setCenter(QGeoCoordinate(centro.latitude() + 8.0, centro.longitude()));
    QVERIFY(w.searchPointsOfInterest(QStringLiteral("farmacia")).isEmpty());

    // Ir a un POI centra el mapa en él.
    w.goToPointOfInterest(r[0]);
    QVERIFY(w.center().distanceTo(r[0].coordinate) < 50.0);

    // Sin overlay vuelve a no haber donde buscar.
    w.clearVectorOverlay();
    QVERIFY(!w.canSearchPointsOfInterest());
}

// Sacar la selección como entidades: los POI de una zona pasan a ser puntos de una capa,
// con su tipo, color y atributos. Se prueba a z14 (una tesela con tres POI: dos farmacias,
// una sin nombre, y un hospital): filtrar por texto, repetir sin duplicar, ampliar, el
// tope de entidades, cortar a medias, zona fuera de los datos, zona demasiado grande y
// los errores (sin mapa vectorial, zona sin área).
void TstMapWidget::importsPointsOfInterestAsFeatures()
{
    const QString base = m_dir.filePath(QStringLiteral("gris_imp.mbtiles"));
    const QString vec = m_dir.filePath(QStringLiteral("poi14.mbtiles"));
    QVERIFY(makeWorldMBTiles(base, false));
    // Tesela z14 que contiene (22.0, -80.0); su centro es donde cae el POI de (2048, 2048).
    const double pi = 3.14159265358979323846;
    const double n = 16384.0;
    const int tx = int(std::floor((-80.0 + 180.0) / 360.0 * n));
    const int ty = int(std::floor((1.0 - std::asinh(std::tan(22.0 * pi / 180.0)) / pi) / 2.0 * n));
    QVERIFY(makeSingleTileMBTiles(vec, poiTile(), 14, tx, ty));
    const QGeoCoordinate centro(std::atan(std::sinh(pi * (1.0 - 2.0 * (ty + 0.5) / n))) * 180.0 / pi,
                                (tx + 0.5) / n * 360.0 - 180.0);
    const QString json = m_dir.filePath(QStringLiteral("gris_imp.json"));
    {
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("datasets"),
            QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("gris") },
                                     { QStringLiteral("filePath"), base } } } } }).toJson());
    }
    MapConfig cfg = baseConfig(json);
    cfg.initialZoom = 4;
    cfg.initialCenter = centro;
    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    w.resize(600, 400);

    auto cuadro = [&](const QGeoCoordinate &c, double d) {
        return QVector<QGeoCoordinate>{ QGeoCoordinate(c.latitude() + d, c.longitude() - d),
                                        QGeoCoordinate(c.latitude() + d, c.longitude() + d),
                                        QGeoCoordinate(c.latitude() - d, c.longitude() + d),
                                        QGeoCoordinate(c.latitude() - d, c.longitude() - d) };
    };
    const auto zona = cuadro(centro, 0.01);

    // Errores: sin mapa vectorial; una zona sin área; sin capa de destino.
    auto r = w.importPointsOfInterest(zona, QStringLiteral("farmacia"), QStringLiteral("sitios"));
    QVERIFY(!r.ok());
    QVERIFY2(r.error.contains(QStringLiteral("vectorial")), qPrintable(r.error));
    QString error;
    QVERIFY2(w.setVectorOverlay(vec, &error), qPrintable(error));
    QVERIFY(!w.importPointsOfInterest({ centro, centro }, QStringLiteral("farmacia"), QStringLiteral("sitios")).ok());
    QVERIFY(!w.importPointsOfInterest(zona, QStringLiteral("farmacia"), QString()).ok());
    QVERIFY(w.featuresInLayer(QStringLiteral("sitios")).isEmpty());          // nada se creó

    // Las dos farmacias (una sin nombre) pasan a ser puntos de la capa, que se crea.
    r = w.importPointsOfInterest(zona, QStringLiteral("farmacia"), QStringLiteral("sitios"));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.found, 2);
    QCOMPARE(r.added, 2);
    QCOMPARE(r.skipped, 0);
    const auto feats = w.featuresInLayer(QStringLiteral("sitios"));
    QCOMPARE(feats.size(), 2);
    for (const MapFeature &f : feats) {
        QCOMPARE(f.kind, GeometryKind::Point);
        QCOMPARE(f.type, QStringLiteral("pharmacy"));
        QCOMPARE(f.attributes.value(QStringLiteral("tipo")).toString(), QStringLiteral("farmacia"));
        QCOMPARE(f.attributes.value(QStringLiteral("clase")).toString(), QStringLiteral("pharmacy"));
        QCOMPARE(f.style.lineColor.name(), QStringLiteral("#d32f2f"));       // el color del tema salud
        QVERIFY(f.position().distanceTo(centro) < 200.0);
    }
    QStringList nombres;
    for (const MapFeature &f : feats)
        nombres << f.name;
    nombres.sort();
    QCOMPARE(nombres, QStringList({ "Farmacia", "Farmacia Central" }));      // la sin nombre usa su tipo

    // Repetir no duplica; ampliar (texto vacío = todos) añade solo el que faltaba.
    r = w.importPointsOfInterest(zona, QStringLiteral("farmacia"), QStringLiteral("sitios"));
    QCOMPARE(r.found, 2);
    QCOMPARE(r.added, 0);
    QCOMPARE(r.skipped, 2);
    r = w.importPointsOfInterest(zona, QString(), QStringLiteral("sitios"));
    QCOMPARE(r.found, 3);
    QCOMPARE(r.added, 1);
    QCOMPARE(r.skipped, 2);
    QCOMPARE(w.featuresInLayer(QStringLiteral("sitios")).size(), 3);

    // Tope de entidades: el resto se cuenta y se marca.
    r = w.importPointsOfInterest(zona, QString(), QStringLiteral("tope"), nullptr, 1);
    QCOMPARE(r.found, 3);
    QCOMPARE(r.added, 1);
    QVERIFY(r.truncated);

    // Cortar a medias: no se añade nada. El progreso informa de filas hechas y totales.
    int llamadas = 0, ultimoTotal = 0;
    r = w.importPointsOfInterest(zona, QString(), QStringLiteral("cortada"),
                                 [&](int, int total) { ++llamadas; ultimoTotal = total; return false; });
    QVERIFY(r.cancelled);
    QVERIFY(!r.ok());
    QCOMPARE(llamadas, 1);
    QVERIFY(ultimoTotal >= 1);
    QVERIFY(w.featuresInLayer(QStringLiteral("cortada")).isEmpty());
    llamadas = 0;
    r = w.importPointsOfInterest(zona, QString(), QStringLiteral("completa"),
                                 [&](int, int) { ++llamadas; return true; });
    QVERIFY(r.ok());
    QVERIFY(llamadas >= 2);                                                    // por fila y al final

    // Una zona sin teselas, o sin POI dentro del polígono: 0 encontrados, sin error.
    r = w.importPointsOfInterest(cuadro(QGeoCoordinate(centro.latitude() + 1.0, centro.longitude()), 0.005),
                                 QString(), QStringLiteral("vacia"));
    QVERIFY(r.ok());
    QCOMPARE(r.found, 0);
    QCOMPARE(r.added, 0);
    // El polígono recorta: un cuadrito junto al centro que deja fuera los POI.
    r = w.importPointsOfInterest(cuadro(QGeoCoordinate(centro.latitude() + 0.005, centro.longitude() + 0.005), 0.0005),
                                 QString(), QStringLiteral("vacia"));
    QCOMPARE(r.found, 0);

    // Demasiado grande: no se lee nada y se dice cuántas teselas abarca.
    r = w.importPointsOfInterest(cuadro(centro, 3.0), QString(), QStringLiteral("grande"));
    QVERIFY(r.tooLarge);
    QVERIFY(!r.ok());
    QVERIFY(r.tiles > MapWidget::kMaxPoiImportTiles);
    QVERIFY(w.featuresInLayer(QStringLiteral("grande")).isEmpty());
}

// Buscar calles en el MBTiles vectorial (overlay híbrido) alrededor del centro: por nombre
// con y sin la palabra genérica, que los tramos de varias teselas salgan como UNA calle,
// el límite, que no salga nada sin capa vectorial / con poco texto / lejos de la calle, y
// que ir a la calle encuadre su extensión.
void TstMapWidget::searchesStreets()
{
    const QString base = m_dir.filePath(QStringLiteral("gris_calles.mbtiles"));
    const QString vec = m_dir.filePath(QStringLiteral("calles.mbtiles"));
    QVERIFY(makeWorldMBTiles(base, false));
    QVERIFY(makeWorldMBTiles(vec, true, streetTile()));
    const QString json = m_dir.filePath(QStringLiteral("gris_calles.json"));
    {
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("datasets"),
            QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("gris") },
                                     { QStringLiteral("filePath"), base } } } } }).toJson());
    }
    // Centro de la tesela z4 (7, 6): la calle la cruza justo por ahí.
    const double pi = 3.14159265358979323846;
    const QGeoCoordinate centro(std::atan(std::sinh(pi * (1.0 - 2.0 * 6.5 / 16.0))) * 180.0 / pi,
                                (7.5 / 16.0) * 360.0 - 180.0);
    MapConfig cfg = baseConfig(json);
    cfg.initialZoom = 4;
    cfg.initialCenter = centro;
    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    w.resize(600, 400);

    // Sin capa vectorial (la base es ráster) no hay donde buscar.
    QVERIFY(!w.canSearchStreets());
    QVERIFY(w.searchStreets(QStringLiteral("obispo")).isEmpty());

    QString error;
    QVERIFY2(w.setVectorOverlay(vec, &error), qPrintable(error));
    QVERIFY(w.canSearchStreets());

    // Las tres teselas de la fila del centro (a z4 el radio de 6 km abarca 3x3) dan tres
    // tramos que se tocan: UNA calle. Las filas de arriba y de abajo quedan a miles de km.
    auto r = w.searchStreets(QStringLiteral("obispo"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].name, QStringLiteral("Obispo"));
    QCOMPARE(r[0].typeLabel(), QStringLiteral("calle"));
    QCOMPARE(r[0].parts.size(), 3);
    QVERIFY(r[0].distanceM < 100.0);
    QVERIFY(r[0].northWest.distanceTo(r[0].southEast) > 1000000.0);          // cubre las tres teselas

    QCOMPARE(w.searchStreets(QStringLiteral("calle obispo")).size(), 1);      // la genérica es opcional
    QCOMPARE(w.searchStreets(QStringLiteral("OBISPO")).size(), 1);
    QCOMPARE(w.searchStreets(QStringLiteral("obispo"), 0).size(), 0);          // límite 0
    QCOMPARE(w.searchStreets(QStringLiteral("obispo")).size(), 1);             // y con la caché, lo mismo
    QVERIFY(w.searchStreets(QStringLiteral("o")).isEmpty());                  // poco texto
    QVERIFY(w.searchStreets(QStringLiteral("zzzz")).isEmpty());

    // Lejos de la calle (a miles de km) no sale.
    w.setCenter(QGeoCoordinate(centro.latitude() + 8.0, centro.longitude()));
    QVERIFY(w.searchStreets(QStringLiteral("obispo")).isEmpty());
    w.setCenter(centro);

    // Ir a la calle encuadra su extensión: el centro queda sobre ella.
    const Street s = w.searchStreets(QStringLiteral("obispo")).first();
    w.goToStreet(s);
    QVERIFY(w.center().distanceTo(s.nearest) < 500000.0);
    QVERIFY(w.zoom() <= 17);

    // Una calle corta (menos de 150 m) se centra con zoom 17.
    Street corta = s;
    corta.northWest = QGeoCoordinate(centro.latitude() + 0.0003, centro.longitude() - 0.0003);
    corta.southEast = QGeoCoordinate(centro.latitude() - 0.0003, centro.longitude() + 0.0003);
    corta.nearest = centro;
    w.goToStreet(corta);
    QVERIFY(w.center().distanceTo(centro) < 50.0);

    w.clearVectorOverlay();
    QVERIFY(!w.canSearchStreets());
}

// Rutas sin conexión por la red vial del MBTiles vectorial: una tesela z14 con una carretera
// horizontal y una calle vertical que se cruzan en el centro. De la punta oeste de la carretera
// al extremo sur de la calle hay que girar en el cruce: recorre las dos y suma lo esperado.
// También los errores (sin mapa vectorial, punto lejos de toda vía, demasiado largo) y el progreso.
void TstMapWidget::findsARoute()
{
    const QString base = m_dir.filePath(QStringLiteral("gris_ruta.mbtiles"));
    const QString vec = m_dir.filePath(QStringLiteral("vias14.mbtiles"));
    QVERIFY(makeWorldMBTiles(base, false));
    const double pi = 3.14159265358979323846;
    const double n = 16384.0;
    const int tx = int(std::floor((-80.0 + 180.0) / 360.0 * n));
    const int ty = int(std::floor((1.0 - std::asinh(std::tan(22.0 * pi / 180.0)) / pi) / 2.0 * n));
    QVERIFY(makeSingleTileMBTiles(vec, roadTile(), 14, tx, ty));
    // Posición geográfica de un punto de la tesela, en unidades de 0..4096.
    auto punto = [&](double ux, double uy) {
        return QGeoCoordinate(std::atan(std::sinh(pi * (1.0 - 2.0 * (ty + uy / 4096.0) / n))) * 180.0 / pi,
                              (tx + ux / 4096.0) / n * 360.0 - 180.0);
    };
    const QString json = m_dir.filePath(QStringLiteral("gris_ruta.json"));
    {
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("datasets"),
            QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("gris") },
                                     { QStringLiteral("filePath"), base } } } } }).toJson());
    }
    MapConfig cfg = baseConfig(json);
    cfg.initialZoom = 4;
    cfg.initialCenter = punto(2048, 2048);
    MapWidget w(cfg);
    QVERIFY2(w.isReady(), qPrintable(w.lastError()));
    w.resize(600, 400);

    const QGeoCoordinate oeste = punto(100, 2048), sur = punto(2048, 3900);

    // Sin capa vectorial no hay red vial.
    auto r = w.findRoute(oeste, sur);
    QVERIFY(!r.ok());
    QVERIFY2(r.error.contains(QStringLiteral("vectorial")), qPrintable(r.error));

    QString error;
    QVERIFY2(w.setVectorOverlay(vec, &error), qPrintable(error));
    QVERIFY(!w.findRoute(QGeoCoordinate(), sur).ok());                         // sin origen válido

    // Oeste -> sur: 1948 unidades por la carretera y 1852 por la calle (~0,55 m cada una) = ~2,1 km.
    int llamadas = 0;
    r = w.findRoute(oeste, sur, [&](int, int) { ++llamadas; return true; });
    QVERIFY2(r.ok(), qPrintable(r.error));
    const double esperado = oeste.distanceTo(punto(2048, 2048)) + punto(2048, 2048).distanceTo(sur);
    QVERIFY2(qAbs(r.distanceM - esperado) < 30.0, qPrintable(QStringLiteral("%1 vs %2").arg(r.distanceM).arg(esperado)));
    QVERIFY(r.distanceM > 1800.0 && r.distanceM < 2400.0);
    QCOMPARE(r.byClass.size(), 2);                                             // las dos vías
    QCOMPARE(r.byClass.first().first, QStringLiteral("primary"));
    QVERIFY(r.durationS > 100.0 && r.durationS < 200.0);                       // ~2 km a 30-60 km/h
    QVERIFY(r.path.size() >= 3);                                               // salida, cruce y llegada
    QVERIFY(r.path.first().distanceTo(oeste) < 2.0);
    QVERIFY(r.path.last().distanceTo(sur) < 2.0);
    QVERIFY(r.tiles >= 1);
    QVERIFY(llamadas >= 2);                                                    // una por fila y al final

    // La ruta de vuelta mide lo mismo.
    const auto vuelta = w.findRoute(sur, oeste);
    QVERIFY(vuelta.ok());
    QVERIFY(qAbs(vuelta.distanceM - r.distanceM) < 5.0);

    // Un punto a más de 500 m de toda vía: error que nombra el origen. Y un punto al lado de la vía sirve.
    r = w.findRoute(punto(100, 500), sur);
    QVERIFY(!r.ok());
    QVERIFY2(r.error.contains(QStringLiteral("origen")), qPrintable(r.error));
    r = w.findRoute(punto(100, 2300), sur);                                     // a ~140 m de la carretera
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.snapFromM > 100.0 && r.snapFromM < 200.0);

    // Cortar con el progreso: error de cancelado.
    r = w.findRoute(oeste, sur, [](int, int) { return false; });
    QVERIFY(!r.ok());
    QVERIFY2(r.error.contains(QStringLiteral("cancel")), qPrintable(r.error));

    // Demasiado largo: dos puntos a ~200 km no caben en el tope de teselas (y no se lee nada).
    r = w.findRoute(oeste, QGeoCoordinate(oeste.latitude(), oeste.longitude() + 2.0));
    QVERIFY(!r.ok());
    QVERIFY2(r.error.contains(QStringLiteral("demasiado larga")), qPrintable(r.error));
    QVERIFY(r.tiles > MapWidget::kMaxRouteTiles);
}

QTEST_MAIN(TstMapWidget)
#include "tst_mapwidget.moc"
