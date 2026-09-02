#include "SyntheticTileDb.h"

#include "libmapa/MapWidget.h"
#include "db/SqliteConnectionPool.h"
#include "widget/MapView.h"
#include "geo/TileMatrix.h"

#include <climits>

#include <QMouseEvent>

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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    void reportsErrorOnMissingJson();

    void listsAvailableLayers();

    //! El requisito central: alternar OSM <-> Satelital en caliente.
    void switchesBaseLayerInPlace();

    void navigationKeepsCenter();
    void zoomIsClampedToRecommendedRange();
    void fitBoundsFramesTheArea();

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

private:
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

QTEST_MAIN(TstMapWidget)
#include "tst_mapwidget.moc"
