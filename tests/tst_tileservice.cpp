#include "SyntheticTileDb.h"

#include "db/SqliteConnectionPool.h"
#include "tiles/TileService.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;
using namespace libmapa::test;

class TstTileService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void startsAndReportsDatasets();
    void rejectsMissingFiles();

    void loadsTilesAsynchronously();

    //! El hilo de la GUI no debe bloquearse mientras se lee. (F-17)
    void guiThreadIsNotBlocked();

    //! Arrastrar el mapa no debe encolar decenas de lecturas inutiles.
    void staleRequestsAreDropped();

    //! Alternar OSM <-> Satelital en caliente, que es el requisito central.
    void switchesBaseLayerKeepingCache();

    void zoomIsClampedToDatasetRange();
    void planUsesFallbackAfterPartialLoad();

    //! La carga progresiva debe cubrir la pantalla entera desde el principio,
    //! incluso con la densidad del 32 % de la capa satelital.
    void progressiveLoadCoversScreenImmediately();

    /*! Regresion: las teselas de detalle expulsaban de la cache los niveles
     *  gruesos de respaldo, y los huecos en blanco reaparecian. */
    void detailDoesNotEvictFallbackLevels();

    /*! La escalera se dimensiona sola segun el relleno de la BD: una densa
     *  no la necesita y solo pagaria latencia. */
    void fallbackLadderScalesWithDensity();

    /*! Una rejilla ya resuelta no se vuelve a leer ni a decodificar. */
    void cachedRangesAreNotRefetched();

    /*! Las teselas inexistentes se anotan: con el 68 % de huecos de la capa
     *  satelital, no anotarlas significa reconsultar la BD eternamente. */
    void missingTilesAreRememberedNotRefetched();

    /*! La escalera es por demanda: solo se precarga el peldano mas grueso, y
     *  los intermedios se piden si al llegar el detalle quedan huecos. */
    void ladderIsDemandDriven();

    /*! Las BD solo guardan teselas donde hay tierra: sobre mar abierto el
     *  unico respaldo posible son los niveles gruesos de cobertura mundial. */
    void openSeaFallsBackToWorldLevel();
    void areaWithNoCoverageAtAllDoesNotLoop();

    /*! Un hueco tapado con un ancestro muy ampliado esta cubierto en el
     *  papel, pero se ve como un color plano. Debe refinarse. */
    void blurryFallbackTriggersRefinement();
    void loadsDatasetsFromJson();

    //! Espera a que llegue el nivel indicado. Con carga progresiva, la
    //! primera senal corresponde al nivel grueso de respaldo, no al pedido.
    static bool waitForZoom(QSignalSpy &spy, int zoom, int timeoutMs = 15000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            for (const auto &args : spy)
                if (args.at(1).toInt() == zoom)
                    return true;
            if (!spy.wait(500) && t.elapsed() > timeoutMs)
                break;
        }
        for (const auto &args : spy)
            if (args.at(1).toInt() == zoom)
                return true;
        return false;
    }

private:
    QTemporaryDir m_dir;
    QVector<TileDataset> m_datasets;
    QGeoCoordinate m_nw{23.0, -84.0};
    QGeoCoordinate m_se{20.0, -75.0};
};

void TstTileService::initTestCase()
{
    QVERIFY(m_dir.isValid());

    // Replica de las dos BD reales: convenciones de zoom distintas.
    SyntheticSpec osm;
    osm.path = m_dir.filePath(QStringLiteral("osm.sqlitedb"));
    osm.minLogicalZ = 6;
    osm.maxLogicalZ = 11;
    QVERIFY(buildSyntheticDb(osm) > 0);

    SyntheticSpec sat;
    sat.path = m_dir.filePath(QStringLiteral("sat.sqlitedb"));
    sat.minLogicalZ = 6;
    sat.maxLogicalZ = 11;
    sat.colorSeed = 7;          // teselas distinguibles de las de OSM
    sat.invertedZ = true;
    sat.zOffsetForInverted = 17;
    QVERIFY(buildSyntheticDb(sat) > 0);

    TileDataset dOsm;
    dOsm.id = QStringLiteral("osm");
    dOsm.displayName = QStringLiteral("Open Street Map");
    dOsm.filePath = osm.path;
    dOsm.zFactor = 1; dOsm.zOffset = 0;
    dOsm.minZoom = 6; dOsm.maxZoom = 11; dOsm.recommendedMaxZoom = 11;

    TileDataset dSat;
    dSat.id = QStringLiteral("satelital");
    dSat.displayName = QStringLiteral("Satelital");
    dSat.filePath = sat.path;
    dSat.zFactor = -1; dSat.zOffset = 17;
    dSat.minZoom = 6; dSat.maxZoom = 11; dSat.recommendedMaxZoom = 11;

    m_datasets = {dOsm, dSat};
}

void TstTileService::cleanupTestCase()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

void TstTileService::startsAndReportsDatasets()
{
    TileService svc;
    QVERIFY(svc.start(m_datasets, 64));

    QCOMPARE(svc.datasetIds().size(), 2);
    QVERIFY(svc.dataset(QStringLiteral("osm")) != nullptr);
    QVERIFY(svc.dataset(QStringLiteral("satelital")) != nullptr);
    QVERIFY(svc.dataset(QStringLiteral("noexiste")) == nullptr);
    QCOMPARE(svc.activeDatasetId(), QStringLiteral("osm"));
}

void TstTileService::rejectsMissingFiles()
{
    TileDataset bad;
    bad.id = QStringLiteral("fantasma");
    bad.filePath = m_dir.filePath(QStringLiteral("noexiste.sqlitedb"));

    TileService svc;
    QSignalSpy errors(&svc, &TileService::errorOccurred);
    QVERIFY(!svc.start({bad}));
    QCOMPARE(errors.count(), 1);
}

void TstTileService::loadsTilesAsynchronously()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 64));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    const quint64 id = svc.requestViewport(m_nw, m_se, 9);
    QVERIFY(id > 0);

    // La cache aun esta vacia: la lectura ocurre en otro hilo.
    QCOMPARE(svc.cache().count(), 0);

    QVERIFY(ready.wait(10000));
    QCOMPARE(ready.first().at(0).toULongLong(), id);
    QVERIFY(svc.cache().count() > 0);
}

void TstTileService::guiThreadIsNotBlocked()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 128));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    QElapsedTimer timer;
    timer.start();
    svc.requestViewport(m_nw, m_se, 11);      // el nivel mas poblado
    const qint64 msToReturn = timer.elapsed();

    // requestViewport solo encola: debe volver de inmediato.
    QVERIFY2(msToReturn < 50,
             qPrintable(QStringLiteral("requestViewport tardo %1 ms").arg(msToReturn)));

    QVERIFY(ready.wait(15000));
    qInfo() << "requestViewport devolvio en" << msToReturn
            << "ms; las teselas llegaron despues, en otro hilo";
}

void TstTileService::staleRequestsAreDropped()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 128));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Simula un arrastre: 30 peticiones seguidas sin dar tiempo a servirlas.
    quint64 last = 0;
    for (int i = 0; i < 30; ++i) {
        const QGeoCoordinate nw(23.0 - i * 0.01, -84.0 + i * 0.01);
        const QGeoCoordinate se(20.0 - i * 0.01, -75.0 + i * 0.01);
        last = svc.requestViewport(nw, se, 10);
    }
    QVERIFY(last > 0);

    // Se espera a que el sistema se calme.
    QTRY_VERIFY_WITH_TIMEOUT(ready.count() > 0, 15000);
    QTest::qWait(1500);

    // Lo importante: NO se sirven las 30. Las obsoletas se descartan.
    QVERIFY2(ready.count() < 30,
             qPrintable(QStringLiteral("se sirvieron %1 de 30 peticiones")
                            .arg(ready.count())));

    // Y la ultima, que es la que el usuario esta viendo, si llega.
    bool lastServed = false;
    for (const auto &args : ready)
        if (args.at(0).toULongLong() == last)
            lastServed = true;
    QVERIFY2(lastServed, "La peticion mas reciente debe servirse siempre");

    qInfo() << "De 30 peticiones encadenadas se sirvieron" << ready.count();
}

void TstTileService::switchesBaseLayerKeepingCache()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 64));

    QSignalSpy ready(&svc, &TileService::tilesReady);
    QSignalSpy changed(&svc, &TileService::activeDatasetChanged);

    svc.requestViewport(m_nw, m_se, 9);
    QVERIFY(waitForZoom(ready, 9));
    const int enOsm = svc.cache().count();
    QVERIFY(enOsm > 0);

    // Cambio de capa: no se reconstruye nada.
    QVERIFY(svc.setActiveDataset(QStringLiteral("satelital")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(svc.activeDatasetId(), QStringLiteral("satelital"));

    // Cada capa tiene su PROPIA cache. Antes habia una sola y las claves son
    // (z,x,y) a secas, asi que la tesela 9/70/220 de OSM y la 9/70/220 de la
    // satelital eran la misma entrada: al alternar capas se veian teselas
    // mezcladas de las dos.
    QCOMPARE(svc.cache().count(), 0);                    // la satelital, vacia
    QCOMPARE(svc.cacheFor(QStringLiteral("osm")).count(), enOsm);   // OSM intacta

    ready.clear();
    svc.requestViewport(m_nw, m_se, 9);
    QVERIFY(waitForZoom(ready, 9));
    const int enSat = svc.cache().count();
    QVERIFY(enSat > 0);

    // Y las imagenes son distintas: cada cache guarda las suyas.
    const auto claves = TileMatrix::rangeFor(m_nw, m_se, 9);
    const TileKey k{9, claves.xMin, claves.yMin};
    const QImage imgSat = svc.cacheFor(QStringLiteral("satelital")).take(k);
    const QImage imgOsm = svc.cacheFor(QStringLiteral("osm")).take(k);
    if (!imgSat.isNull() && !imgOsm.isNull())
        QVERIFY2(imgSat != imgOsm,
                 "La misma clave devuelve la misma imagen en las dos capas");

    // Volver atras es inmediato: la cache de OSM sigue ahi.
    QVERIFY(svc.setActiveDataset(QStringLiteral("osm")));
    QCOMPARE(svc.cache().count(), enOsm);
    QCOMPARE(svc.requestViewport(m_nw, m_se, 9), quint64(0));   // nada que pedir

    // Una capa inexistente se rechaza sin romper el estado.
    QVERIFY(!svc.setActiveDataset(QStringLiteral("relieve")));
    QCOMPARE(svc.activeDatasetId(), QStringLiteral("osm"));
}

void TstTileService::zoomIsClampedToDatasetRange()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 64));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Zoom 25 no existe; debe recortarse a 11 y devolver teselas igualmente.
    QVERIFY(svc.requestViewport(m_nw, m_se, 25) > 0);
    QVERIFY(ready.wait(15000));
    QVERIFY(svc.cache().count() > 0);

    // bestZoomFor tambien respeta el rango.
    QVERIFY(svc.bestZoomFor(0.0001, 1024) <= 11);
    QVERIFY(svc.bestZoomFor(360.0, 1024) >= 6);
}

void TstTileService::planUsesFallbackAfterPartialLoad()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 256));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Se desactiva la carga progresiva para poder comprobar el respaldo
    // manualmente: se carga solo el nivel 8 y se planifica el 9.
    svc.setFallbackLevels(0);
    svc.requestViewport(m_nw, m_se, 8);
    QVERIFY(waitForZoom(ready, 8));

    // El plan a un nivel mas profundo, aun sin cargar, se resuelve entero
    // con ancestros: ni un hueco en blanco.
    const auto plan = svc.planFor(m_nw, m_se, 9);
    QVERIFY(plan.slotCount() > 0);
    QCOMPARE(plan.exactCount, 0);
    QVERIFY(plan.fallbackCount > 0);
    QCOMPARE(plan.emptyCount, 0);
    QVERIFY(!plan.missing.isEmpty());

    // Tras cargarlo de verdad, pasan a ser exactas.
    ready.clear();
    svc.requestViewport(m_nw, m_se, 9);
    QVERIFY(waitForZoom(ready, 9));

    const auto plan2 = svc.planFor(m_nw, m_se, 9);
    QVERIFY(plan2.exactCount > 0);
    QCOMPARE(plan2.coverage(), 1.0);
}

void TstTileService::loadsDatasetsFromJson()
{
    const QString jsonPath = m_dir.filePath(QStringLiteral("datasets.json"));
    {
        QJsonArray arr;
        for (const TileDataset &d : m_datasets) {
            QJsonObject o = d.toJson();
            // Ruta relativa: debe resolverse respecto al propio JSON.
            o[QStringLiteral("filePath")] = QFileInfo(d.filePath).fileName();
            arr.append(o);
        }
        QJsonObject root;
        root[QStringLiteral("version")] = 1;
        root[QStringLiteral("datasets")] = arr;

        QFile f(jsonPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
    }

    QString error;
    const auto loaded = TileService::loadDatasets(jsonPath, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).id, QStringLiteral("osm"));
    QCOMPARE(loaded.at(1).zOffset, 17);
    QVERIFY(QFileInfo::exists(loaded.at(0).filePath));

    // Y sirven para arrancar el servicio tal cual.
    TileService svc;
    QVERIFY(svc.start(loaded, 32));

    // Un fichero inexistente da error, no un cuelgue.
    QString error2;
    QVERIFY(TileService::loadDatasets(m_dir.filePath(QStringLiteral("nope.json")),
                                      &error2).isEmpty());
    QVERIFY(!error2.isEmpty());
}

void TstTileService::progressiveLoadCoversScreenImmediately()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 256));
    QVERIFY(svc.setActiveDataset(QStringLiteral("satelital")));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Arranque en frio sobre una zona nueva, al nivel de mas detalle.
    svc.requestViewport(m_nw, m_se, 11);

    // La PRIMERA senal es la del nivel grueso de respaldo (11 - 2 = 9).
    QVERIFY(ready.wait(15000));
    QVERIFY(ready.first().at(1).toInt() < 11);

    // Y con solo eso, el plan del nivel 11 ya no deja huecos en blanco.
    const auto early = svc.planFor(m_nw, m_se, 11);
    QVERIFY(early.slotCount() > 0);
    QCOMPARE(early.emptyCount, 0);
    QCOMPARE(early.coverage(), 1.0);
    QVERIFY(early.fallbackCount > 0);

    qInfo() << "Tras el nivel grueso:" << early.exactCount << "exactas,"
            << early.fallbackCount << "por respaldo, 0 huecos";

    // Al llegar el detalle, las exactas sustituyen a los sucedaneos.
    QVERIFY(waitForZoom(ready, 11));
    const auto full = svc.planFor(m_nw, m_se, 11);
    QCOMPARE(full.emptyCount, 0);
    QVERIFY(full.exactCount > early.exactCount);

    qInfo() << "Tras el detalle:" << full.exactCount << "exactas,"
            << full.fallbackCount << "por respaldo, 0 huecos";
}

void TstTileService::detailDoesNotEvictFallbackLevels()
{
    // Viewport del tamano de una pantalla real (unos 1280x800 al zoom 11),
    // no un rectangulo enorme: la escalera de respaldo solo es barata en
    // relacion al viewport, no en terminos absolutos.
    const double spanLon = 360.0 * 1280.0 / (256.0 * (1 << 11));
    const double spanLat = spanLon * 800.0 / 1280.0;
    const QGeoCoordinate center(23.1136, -82.3666);
    const QGeoCoordinate nw(center.latitude() + spanLat / 2,
                            center.longitude() - spanLon / 2);
    const QGeoCoordinate se(center.latitude() - spanLat / 2,
                            center.longitude() + spanLon / 2);

    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 16));          // 16 MiB, ajustado
    QVERIFY(svc.setActiveDataset(QStringLiteral("satelital")));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    svc.requestViewport(nw, se, 11);
    QVERIFY(waitForZoom(ready, 11, 20000));

    const auto plan = svc.planFor(nw, se, 11);
    QVERIFY(plan.slotCount() > 0);
    QCOMPARE(plan.emptyCount, 0);
    QCOMPARE(plan.coverage(), 1.0);

    QVERIFY2(svc.cache().pinnedCount() > 0,
             "Los niveles de respaldo deben vivir en el area protegida");

    qInfo() << "Viewport de pantalla, 16 MiB:" << svc.cache().count()
            << "teselas (" << svc.cache().pinnedCount() << "protegidas),"
            << plan.exactCount << "exactas," << plan.fallbackCount
            << "por respaldo, 0 huecos";
}

void TstTileService::fallbackLadderScalesWithDensity()
{
    TileService svc;
    svc.setDebounceMs(0);

    QVector<TileDataset> ds = m_datasets;
    ds[0].typicalFill = 1.00;    // como la BD de OSM real
    ds[1].typicalFill = 0.32;    // como la satelital real
    QVERIFY(svc.start(ds, 64));

    // Densa: un solo peldano, solo para cubrir la latencia.
    QCOMPARE(svc.activeDatasetId(), QStringLiteral("osm"));
    QCOMPARE(svc.effectiveFallbackLevels(), 1);

    // Hueca: escalera completa.
    QVERIFY(svc.setActiveDataset(QStringLiteral("satelital")));
    QCOMPARE(svc.effectiveFallbackLevels(), 3);

    // Y se puede forzar a mano si hace falta.
    svc.setFallbackLevels(0);
    QCOMPARE(svc.effectiveFallbackLevels(), 0);
    svc.setFallbackLevels(-1);
    QCOMPARE(svc.effectiveFallbackLevels(), 3);
}

void TstTileService::cachedRangesAreNotRefetched()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 128));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    const quint64 first = svc.requestViewport(m_nw, m_se, 9);
    QVERIFY(first > 0);
    QVERIFY(waitForZoom(ready, 9));

    const qint64 decodedFirst = svc.cache().stats().inserts;
    QVERIFY(decodedFirst > 0);

    // Exactamente el mismo viewport: ya esta todo en memoria, asi que no
    // debe generarse ninguna peticion.
    ready.clear();
    const quint64 second = svc.requestViewport(m_nw, m_se, 9);
    QCOMPARE(second, quint64(0));

    QTest::qWait(300);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(svc.cache().stats().inserts, decodedFirst);

    // Y el plan sigue estando completo, claro.
    const auto plan = svc.planFor(m_nw, m_se, 9);
    QCOMPARE(plan.emptyCount, 0);

    qInfo() << "Segunda peticion identica:" << decodedFirst
            << "teselas decodificadas en total, ninguna repetida";
}

void TstTileService::missingTilesAreRememberedNotRefetched()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 128));
    QVERIFY(svc.setActiveDataset(QStringLiteral("satelital")));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Zona parcialmente fuera de lo poblado: habra huecos reales.
    const QGeoCoordinate nw(24.5, -86.0);
    const QGeoCoordinate se(22.0, -83.0);

    QVERIFY(svc.requestViewport(nw, se, 9) > 0);
    QVERIFY(waitForZoom(ready, 9));
    QTest::qWait(200);

    QVERIFY2(svc.cache().missingCount() > 0,
             "Deberian haberse anotado teselas inexistentes");
    const int missing = svc.cache().missingCount();
    const qint64 decoded = svc.cache().stats().inserts;

    // Repetir el mismo viewport: ya esta todo resuelto, existan o no las
    // teselas, asi que no debe generarse ninguna peticion.
    ready.clear();
    QCOMPARE(svc.requestViewport(nw, se, 9), quint64(0));
    QTest::qWait(300);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(svc.cache().stats().inserts, decoded);

    qInfo() << "Anotadas" << missing << "teselas inexistentes;"
            << "la segunda peticion identica no consulto la BD";
}

void TstTileService::ladderIsDemandDriven()
{
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 128));
    QVERIFY(svc.setActiveDataset(QStringLiteral("satelital")));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Viewport de pantalla sobre una zona bien cubierta.
    const double spanLon = 360.0 * 1280.0 / (256.0 * (1 << 11));
    const double spanLat = spanLon * 800.0 / 1280.0;
    const QGeoCoordinate c(22.0, -79.5);
    const QGeoCoordinate nw(c.latitude() + spanLat / 2, c.longitude() - spanLon / 2);
    const QGeoCoordinate se(c.latitude() - spanLat / 2, c.longitude() + spanLon / 2);

    svc.requestViewport(nw, se, 11);
    QVERIFY(waitForZoom(ready, 11, 20000));

    // De entrada se sirven tres niveles como mucho: el fondo garantizado, un
    // peldano intermedio y el detalle. Con la escalera fija anterior eran
    // cuatro o cinco, y los intermedios no tapaban nada.
    QSet<int> zoomsIniciales;
    for (const auto &args : ready)
        zoomsIniciales.insert(args.at(1).toInt());
    QVERIFY2(zoomsIniciales.size() <= 3,
             qPrintable(QStringLiteral("se sirvieron %1 niveles de entrada")
                            .arg(zoomsIniciales.size())));
    QVERIFY(zoomsIniciales.contains(11));

    // Y el resultado sigue sin huecos.
    QTest::qWait(500);
    const auto plan = svc.planFor(nw, se, 11);
    QCOMPARE(plan.emptyCount, 0);

    qInfo() << "Escalera por demanda:" << zoomsIniciales.size()
            << "niveles servidos de entrada,"
            << plan.exactCount << "exactas," << plan.fallbackCount
            << "por respaldo, 0 huecos";
}

void TstTileService::openSeaFallsBackToWorldLevel()
{
    // BD como la satelital real: niveles gruesos con el mundo entero (unicos
    // que tienen mar) y niveles de detalle solo sobre el recuadro de Cuba.
    const QString path = m_dir.filePath(QStringLiteral("conmar.sqlitedb"));

    SyntheticSpec mundo;
    mundo.path = path;
    mundo.minLogicalZ = 3;
    mundo.maxLogicalZ = 4;
    mundo.worldCoverage = true;          // z3 y z4 completos, con mar
    QVERIFY(buildSyntheticDb(mundo) > 0);

    SyntheticSpec tierra;
    tierra.path = path;
    tierra.minLogicalZ = 8;
    tierra.maxLogicalZ = 10;
    tierra.createTable = false;          // solo el recuadro de Cuba
    QVERIFY(buildSyntheticDb(tierra) > 0);

    TileDataset ds;
    ds.id = QStringLiteral("conmar");
    ds.filePath = path;
    ds.minZoom = 3;
    ds.maxZoom = 10;
    ds.baseZoom = 3;                     // el nivel de fondo garantizado
    ds.typicalFill = 0.32;

    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start({ds}, 64));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Mar abierto al norte de Cuba: no hay ni una tesela de detalle ahi.
    const QGeoCoordinate nw(28.0, -80.0);
    const QGeoCoordinate se(26.5, -78.0);

    QVERIFY(svc.requestViewport(nw, se, 10) > 0);
    QVERIFY(waitForZoom(ready, 10, 20000));
    QTest::qWait(400);

    const auto plan = svc.planFor(nw, se, 10);
    QVERIFY(plan.slotCount() > 0);
    QCOMPARE(plan.exactCount, 0);        // no hay detalle sobre el mar

    // Lo esencial: NO queda la pantalla en blanco. Se resuelve entera con el
    // nivel de cobertura mundial, siete niveles mas arriba.
    QCOMPARE(plan.emptyCount, 0);
    QCOMPARE(plan.coverage(), 1.0);
    QVERIFY(plan.fallbackCount > 0);

    // Y el respaldo viene precisamente del nivel de fondo.
    for (const TileDrawItem &i : plan.items)
        QVERIFY(i.source.z <= 4);

    qInfo() << "Mar abierto:" << plan.exactCount << "exactas,"
            << plan.fallbackCount << "resueltas con el nivel de cobertura"
            << "mundial, 0 huecos";
}

void TstTileService::areaWithNoCoverageAtAllDoesNotLoop()
{
    // Zona fuera de todo lo que cubre la BD, a cualquier nivel.
    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start(m_datasets, 64));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    const QGeoCoordinate nw(35.5, 139.0);      // Tokio
    const QGeoCoordinate se(35.4, 139.2);

    QVERIFY(svc.requestViewport(nw, se, 10) > 0);
    QVERIFY(waitForZoom(ready, 10, 20000));
    QTest::qWait(600);

    const auto plan = svc.planFor(nw, se, 10);
    QVERIFY(plan.slotCount() > 0);
    QCOMPARE(plan.exactCount, 0);
    QCOMPARE(plan.fallbackCount, 0);
    QCOMPARE(plan.emptyCount, plan.slotCount());   // nada que dibujar, y es correcto

    // Lo importante: no se entra en bucle pidiendo niveles intermedios que
    // tampoco existen. Repetir la peticion no consulta la BD.
    const int llamadas = static_cast<int>(ready.count());
    ready.clear();
    QCOMPARE(svc.requestViewport(nw, se, 10), quint64(0));
    QTest::qWait(400);
    QCOMPARE(ready.count(), 0);

    qInfo() << "Zona sin cobertura:" << llamadas
            << "lecturas y ninguna repeticion; se anotaron"
            << svc.cache().missingCount() << "ausencias";
}

void TstTileService::blurryFallbackTriggersRefinement()
{
    // BD con un nivel de detalle lleno de huecos y un nivel intermedio
    // COMPLETO justo encima. Sin refinamiento, los huecos se taparian con el
    // nivel de fondo, muchisimo mas arriba.
    const QString path = m_dir.filePath(QStringLiteral("borroso.sqlitedb"));

    SyntheticSpec fondo;
    fondo.path = path;
    fondo.minLogicalZ = 4;
    fondo.maxLogicalZ = 4;
    fondo.worldCoverage = true;
    QVERIFY(buildSyntheticDb(fondo) > 0);

    SyntheticSpec intermedio;        // completo, a 2 niveles del detalle
    intermedio.path = path;
    intermedio.minLogicalZ = 8;
    intermedio.maxLogicalZ = 8;
    intermedio.createTable = false;
    QVERIFY(buildSyntheticDb(intermedio) > 0);

    SyntheticSpec detalle;           // con huecos: solo la mitad este
    detalle.path = path;
    detalle.minLogicalZ = 10;
    detalle.maxLogicalZ = 10;
    detalle.createTable = false;
    detalle.northWest = QGeoCoordinate(23.0, -79.5);
    detalle.southEast = QGeoCoordinate(21.0, -78.0);
    QVERIFY(buildSyntheticDb(detalle) > 0);

    TileDataset ds;
    ds.id = QStringLiteral("borroso");
    ds.filePath = path;
    ds.minZoom = 4;
    ds.maxZoom = 10;
    ds.baseZoom = 4;
    ds.typicalFill = 0.32;           // fuerza 3 peldanos -> el rudo seria z4

    TileService svc;
    svc.setDebounceMs(0);
    QVERIFY(svc.start({ds}, 64));

    QSignalSpy ready(&svc, &TileService::tilesReady);

    // Zona SIN detalle: al oeste de lo poblado en z10.
    const QGeoCoordinate nw(23.0, -81.5);
    const QGeoCoordinate se(22.5, -81.0);

    svc.requestViewport(nw, se, 10);
    QVERIFY(waitForZoom(ready, 10, 20000));

    const auto antes = svc.planFor(nw, se, 10);
    QCOMPARE(antes.exactCount, 0);
    QCOMPARE(antes.emptyCount, 0);          // cubierto... pero mal

    int profundoAntes = 0;
    for (const TileDrawItem &i : antes.items)
        profundoAntes = qMax(profundoAntes, i.ancestorDepth);
    QVERIFY2(profundoAntes >= 4,
             qPrintable(QStringLiteral("se esperaba respaldo lejano, fue %1")
                            .arg(profundoAntes)));

    // El servicio debe pedir por su cuenta el nivel intermedio.
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            const auto p = svc.planFor(nw, se, 10);
            int d = 0;
            for (const TileDrawItem &i : p.items)
                d = qMax(d, i.ancestorDepth);
            return d < profundoAntes;
        }(), 15000);

    const auto despues = svc.planFor(nw, se, 10);
    int profundoDespues = 0;
    for (const TileDrawItem &i : despues.items)
        profundoDespues = qMax(profundoDespues, i.ancestorDepth);

    QCOMPARE(despues.emptyCount, 0);
    QVERIFY(profundoDespues < profundoAntes);

    qInfo() << "Refinamiento: el respaldo paso de" << profundoAntes
            << "niveles (ampliado" << (1 << profundoAntes) << "x) a"
            << profundoDespues << "(" << (1 << profundoDespues) << "x)";
}

QTEST_MAIN(TstTileService)
#include "tst_tileservice.moc"
