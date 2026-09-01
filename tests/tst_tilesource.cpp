#include "SyntheticTileDb.h"

#include "db/SqliteConnectionPool.h"
#include "geo/TileMatrix.h"
#include "tiles/RMapsTileSource.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;
using namespace libmapa::test;

class TstTileSource : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void fetchesSingleTile();
    void fetchRangeReturnsAllTiles();

    /*! El nucleo del requisito: la MISMA clase sirve las dos BD, con
     *  convenciones de zoom distintas, sin ningun "if" en el llamador. */
    void bothDatasetsShareTheSameCode();

    void invertedZoomMapsCorrectly();
    void tmsSchemeIsTransparent();
    void worksWithoutSColumn();

    void outOfRangeZoomReturnsEmpty();
    void emptyRegionReturnsEmpty();
    void availableDoesNotLoadBlobs();

    void repeatedQueriesReuseConnection();

private:
    QTemporaryDir m_dir;
    QString m_osmPath, m_satPath, m_tmsPath, m_noSPath;
};

void TstTileSource::initTestCase()
{
    QVERIFY(m_dir.isValid());

    // Dataset "OSM": z directo, XYZ. Como Cuba_OSM_CID3.sqlitedb.
    SyntheticSpec osm;
    osm.path = m_dir.filePath(QStringLiteral("osm.sqlitedb"));
    osm.minLogicalZ = 6;
    osm.maxLogicalZ = 10;
    osm.invertedZ = false;
    m_osmPath = osm.path;
    QVERIFY(buildSyntheticDb(osm) > 0);

    // Dataset "Satelital": storedZ = 17 - logicalZ, tal y como hacia el
    // original con "int Zoom = 18 - Zoom_Level".
    SyntheticSpec sat;
    sat.path = m_dir.filePath(QStringLiteral("sat.sqlitedb"));
    sat.minLogicalZ = 6;
    sat.maxLogicalZ = 10;
    sat.invertedZ = true;
    sat.zOffsetForInverted = 17;
    m_satPath = sat.path;
    QVERIFY(buildSyntheticDb(sat) > 0);

    // Dataset con eje Y invertido (TMS).
    SyntheticSpec tms;
    tms.path = m_dir.filePath(QStringLiteral("tms.sqlitedb"));
    tms.minLogicalZ = 7;
    tms.maxLogicalZ = 9;
    tms.scheme = TileScheme::TMS;
    m_tmsPath = tms.path;
    QVERIFY(buildSyntheticDb(tms) > 0);

    // Dataset sin columna 's'.
    SyntheticSpec noS;
    noS.path = m_dir.filePath(QStringLiteral("nos.sqlitedb"));
    noS.minLogicalZ = 7;
    noS.maxLogicalZ = 8;
    noS.withSColumn = false;
    m_noSPath = noS.path;
    QVERIFY(buildSyntheticDb(noS) > 0);
}

void TstTileSource::cleanupTestCase()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

static TileDataset osmDataset(const QString &path)
{
    TileDataset d;
    d.id = QStringLiteral("osm");
    d.filePath = path;
    d.zFactor = 1;
    d.zOffset = 0;
    d.minZoom = 6;
    d.maxZoom = 10;
    d.scheme = TileScheme::XYZ;
    return d;
}

static TileDataset satDataset(const QString &path)
{
    TileDataset d;
    d.id = QStringLiteral("satelital");
    d.filePath = path;
    d.zFactor = -1;     // storedZ = 17 - logicalZ
    d.zOffset = 17;
    d.minZoom = 6;
    d.maxZoom = 10;
    d.scheme = TileScheme::XYZ;
    return d;
}

void TstTileSource::fetchesSingleTile()
{
    RMapsTileSource src(osmDataset(m_osmPath));
    QVERIFY2(src.open(), qPrintable(src.lastError()));

    const TileKey k = TileMatrix::tileAt(QGeoCoordinate(22.0, -80.0), 8);
    const QByteArray blob = src.fetch(k);

    QVERIFY(!blob.isEmpty());
    QVERIFY(blob.startsWith("\x89PNG"));
}

void TstTileSource::fetchRangeReturnsAllTiles()
{
    RMapsTileSource src(osmDataset(m_osmPath));
    QVERIFY(src.open());

    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), 8);
    const auto tiles = src.fetchRange(8, range.xMin, range.xMax,
                                      range.yMin, range.yMax);

    QCOMPARE(tiles.size(), range.count());

    // Todas las claves devueltas caen dentro del rango pedido y en el nivel
    // pedido: no hay desfase de z ni de y.
    for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it) {
        QCOMPARE(it.key().z, 8);
        QVERIFY(range.contains(it.key()));
        QVERIFY(!it.value().isEmpty());
    }
}

void TstTileSource::bothDatasetsShareTheSameCode()
{
    RMapsTileSource osm(osmDataset(m_osmPath));
    RMapsTileSource sat(satDataset(m_satPath));
    QVERIFY(osm.open());
    QVERIFY(sat.open());

    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), 9);

    // Codigo IDENTICO para ambas: no hay "if (tipoBD == ...)" por ningun lado.
    // Cada fuente traduce internamente el zoom segun su propio TileDataset.
    QVector<ITileSource *> sources{&osm, &sat};
    for (ITileSource *s : sources) {
        const auto tiles = s->fetchRange(9, range.xMin, range.xMax,
                                         range.yMin, range.yMax);
        QVERIFY2(tiles.size() == range.count(),
                 qPrintable(QStringLiteral("dataset %1: %2 de %3")
                                .arg(s->dataset().id)
                                .arg(tiles.size()).arg(range.count())));
        for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it)
            QCOMPARE(it.key().z, 9);
    }
}

void TstTileSource::invertedZoomMapsCorrectly()
{
    const TileDataset ds = satDataset(m_satPath);

    // storedZ = 17 - logicalZ, en los dos sentidos.
    QCOMPARE(ds.storedZ(6),  11);
    QCOMPARE(ds.storedZ(10), 7);
    QCOMPARE(ds.logicalZ(11), 6);
    QCOMPARE(ds.logicalZ(7),  10);

    RMapsTileSource src(ds);
    QVERIFY(src.open());

    // A mayor zoom logico, mas teselas: si la inversion estuviera al reves,
    // esta relacion saldria invertida.
    const qint64 n6  = src.tileCount(6);
    const qint64 n10 = src.tileCount(10);
    QVERIFY(n6 > 0);
    QVERIFY(n10 > n6);
}

void TstTileSource::tmsSchemeIsTransparent()
{
    TileDataset ds;
    ds.id = QStringLiteral("tms");
    ds.filePath = m_tmsPath;
    ds.minZoom = 7;
    ds.maxZoom = 9;
    ds.scheme = TileScheme::TMS;

    RMapsTileSource src(ds);
    QVERIFY(src.open());

    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), 8);
    const auto tiles = src.fetchRange(8, range.xMin, range.xMax,
                                      range.yMin, range.yMax);

    QCOMPARE(tiles.size(), range.count());

    // Las claves salen en coordenadas LOGICAS (XYZ), aunque la BD guarde TMS.
    // El resto del programa nunca se entera de la diferencia.
    for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it)
        QVERIFY(range.contains(it.key()));

    // Verificacion cruzada: leer la misma BD interpretandola como XYZ debe
    // dar un resultado distinto (o vacio), demostrando que el flip actua.
    TileDataset wrong = ds;
    wrong.id = QStringLiteral("tms_mal");
    wrong.scheme = TileScheme::XYZ;
    RMapsTileSource bad(wrong);
    QVERIFY(bad.open());
    const auto badTiles = bad.fetchRange(8, range.xMin, range.xMax,
                                         range.yMin, range.yMax);
    QVERIFY2(badTiles.size() != tiles.size() || badTiles.isEmpty(),
             "Interpretar TMS como XYZ deberia dar un resultado distinto");
}

void TstTileSource::worksWithoutSColumn()
{
    TileDataset ds;
    ds.id = QStringLiteral("nos");
    ds.filePath = m_noSPath;
    ds.minZoom = 7;
    ds.maxZoom = 8;
    ds.hasSColumn = false;      // el WHERE no debe incluir 's'

    RMapsTileSource src(ds);
    QVERIFY2(src.open(), qPrintable(src.lastError()));

    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), 7);
    const auto tiles = src.fetchRange(7, range.xMin, range.xMax,
                                      range.yMin, range.yMax);
    QCOMPARE(tiles.size(), range.count());
}

void TstTileSource::outOfRangeZoomReturnsEmpty()
{
    RMapsTileSource src(osmDataset(m_osmPath));
    QVERIFY(src.open());

    // El dataset declara z[6..10]. Fuera de ahi, respuesta vacia y sin
    // consultar la BD. Esto sustituye al parche "if (ZoomValido == 17)
    // ZoomValido = 16;" del original.
    QVERIFY(src.fetchRange(3, 0, 4, 0, 4).isEmpty());
    QVERIFY(src.fetchRange(15, 0, 4, 0, 4).isEmpty());
    QVERIFY(src.fetch(TileKey{15, 100, 100}).isEmpty());
}

void TstTileSource::emptyRegionReturnsEmpty()
{
    RMapsTileSource src(osmDataset(m_osmPath));
    QVERIFY(src.open());

    // Zona del Pacifico, fuera de lo poblado: consulta valida, cero filas.
    const auto range = TileMatrix::rangeFor(QGeoCoordinate(10.0, 150.0),
                                            QGeoCoordinate(5.0, 155.0), 8);
    QVERIFY(src.fetchRange(8, range.xMin, range.xMax,
                           range.yMin, range.yMax).isEmpty());
}

void TstTileSource::availableDoesNotLoadBlobs()
{
    RMapsTileSource src(osmDataset(m_osmPath));
    QVERIFY(src.open());

    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), 10);

    QElapsedTimer t;
    t.start();
    const auto keys = src.available(10, range.xMin, range.xMax,
                                    range.yMin, range.yMax);
    const qint64 tAvailable = t.elapsed();

    t.restart();
    const auto tiles = src.fetchRange(10, range.xMin, range.xMax,
                                      range.yMin, range.yMax);
    const qint64 tFetch = t.elapsed();

    QCOMPARE(keys.size(), tiles.size());
    for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it)
        QVERIFY(keys.contains(it.key()));

    qInfo() << "available:" << tAvailable << "ms, fetchRange:" << tFetch
            << "ms para" << keys.size() << "teselas";
    // No se afirma una relacion estricta de tiempos (depende de la maquina),
    // pero available nunca debe ser mas lenta.
    QVERIFY(tAvailable <= tFetch + 50);
}

void TstTileSource::repeatedQueriesReuseConnection()
{
    SqliteConnectionPool::closeAllForCurrentThread();

    RMapsTileSource src(osmDataset(m_osmPath));
    QVERIFY(src.open());
    const int after1 = SqliteConnectionPool::openConnectionCount();

    // Simula 200 movimientos de mapa: en el codigo original esto eran 200
    // pares open()/close() sobre un fichero de 1.4 GB.
    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), 8);
    for (int i = 0; i < 200; ++i) {
        const auto tiles = src.fetchRange(8, range.xMin, range.xMax,
                                          range.yMin, range.yMax);
        QVERIFY(!tiles.isEmpty());
    }

    QCOMPARE(SqliteConnectionPool::openConnectionCount(), after1);
}

QTEST_MAIN(TstTileSource)
#include "tst_tilesource.moc"
