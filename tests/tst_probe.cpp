#include "SyntheticTileDb.h"

#include "db/SqliteConnectionPool.h"
#include "tiles/RMapsTileSource.h"
#include "tiles/TileDatasetProbe.h"

#include <QGeoRectangle>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;
using namespace libmapa::test;

class TstProbe : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    /*! Lo esencial: la sonda descubre sola que la BD "satelital" guarda el
     *  zoom invertido, sin que nadie se lo diga. Es lo que sustituye a los
     *  #define invertidos de cutiles.h (falla F-4). */
    void detectsDirectZoom();
    void detectsInvertedZoom();

    void detectsXyzScheme();
    void detectsTmsScheme();

    void detectsTileSize();
    void detectsSValue();
    void detectsMissingSColumn();

    void reportsCoverage();
    void failsOnMissingFile();

    //! El descriptor detectado debe servir para leer de verdad.
    void detectedDatasetActuallyReads();

    void jsonRoundTrip();

    /*! Las dos BD reales tienen niveles finales a medio poblar que daban
     *  extensiones falsas cuando se calculaban desde el nivel mas profundo. */
    void coverageComesFromReferenceLevel();
    void detectsSliverLevelAndCapsZoom();

private:
    QTemporaryDir m_dir;
    QGeoRectangle m_cuba;
    QString m_direct, m_inverted, m_tms, m_noS, m_big;
};

void TstProbe::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_cuba = QGeoRectangle(QGeoCoordinate(23.3, -85.0),
                           QGeoCoordinate(19.7, -74.0));

    SyntheticSpec direct;
    direct.path = m_dir.filePath(QStringLiteral("direct.sqlitedb"));
    direct.minLogicalZ = 6;
    direct.maxLogicalZ = 10;
    m_direct = direct.path;
    QVERIFY(buildSyntheticDb(direct) > 0);

    SyntheticSpec inverted;
    inverted.path = m_dir.filePath(QStringLiteral("inverted.sqlitedb"));
    inverted.minLogicalZ = 6;
    inverted.maxLogicalZ = 10;
    inverted.invertedZ = true;
    inverted.zOffsetForInverted = 17;
    m_inverted = inverted.path;
    QVERIFY(buildSyntheticDb(inverted) > 0);

    SyntheticSpec tms;
    tms.path = m_dir.filePath(QStringLiteral("tms.sqlitedb"));
    tms.minLogicalZ = 7;
    tms.maxLogicalZ = 9;
    tms.scheme = TileScheme::TMS;
    m_tms = tms.path;
    QVERIFY(buildSyntheticDb(tms) > 0);

    SyntheticSpec noS;
    noS.path = m_dir.filePath(QStringLiteral("nos.sqlitedb"));
    noS.minLogicalZ = 7;
    noS.maxLogicalZ = 8;
    noS.withSColumn = false;
    m_noS = noS.path;
    QVERIFY(buildSyntheticDb(noS) > 0);

    SyntheticSpec big;
    big.path = m_dir.filePath(QStringLiteral("big.sqlitedb"));
    big.minLogicalZ = 7;
    big.maxLogicalZ = 8;
    big.tileSize = 512;
    big.sValue = 3;
    m_big = big.path;
    QVERIFY(buildSyntheticDb(big) > 0);
}

void TstProbe::cleanupTestCase()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

void TstProbe::detectsDirectZoom()
{
    auto r = TileDatasetProbe::probe(m_direct, QStringLiteral("osm"), m_cuba);
    QVERIFY(r.has_value());

    QCOMPARE(r->dataset.zFactor, 1);
    QCOMPARE(r->dataset.zOffset, 0);
    QCOMPARE(r->dataset.minZoom, 6);
    QCOMPARE(r->dataset.maxZoom, 10);

    QCOMPARE(r->dataset.storedZ(8), 8);
    QCOMPARE(r->levels.size(), 5);

    // Los niveles salen ordenados por detalle creciente.
    for (int i = 1; i < r->levels.size(); ++i)
        QVERIFY(r->levels[i].tileCount >= r->levels[i - 1].tileCount);
}

void TstProbe::detectsInvertedZoom()
{
    auto r = TileDatasetProbe::probe(m_inverted, QStringLiteral("satelital"), m_cuba);
    QVERIFY(r.has_value());

    // La BD guarda storedZ = 17 - logicalZ. La sonda lo descubre sola,
    // incluido el OFFSET ABSOLUTO: no basta con saber que esta invertido,
    // porque los indices x/y siguen estando en la rejilla del zoom real.
    QCOMPARE(r->dataset.zFactor, -1);
    QCOMPARE(r->dataset.zOffset, 17);

    // Y por tanto el zoom logico recuperado es el VERDADERO (6..10), el mismo
    // con el que se generaron los indices x/y.
    QCOMPARE(r->dataset.minZoom, 6);
    QCOMPARE(r->dataset.maxZoom, 10);
    QCOMPARE(r->levels.size(), 5);

    QCOMPARE(r->dataset.storedZ(6),  11);
    QCOMPARE(r->dataset.storedZ(10), 7);
    QCOMPARE(r->dataset.logicalZ(11), 6);
    QCOMPARE(r->dataset.logicalZ(7),  10);

    // El nivel logico mas alto tiene mas teselas que el mas bajo.
    QVERIFY(r->levels.last().tileCount > r->levels.first().tileCount);

    QVERIFY(r->zMappingReason.contains(QStringLiteral("INVERTIDO")));
}

void TstProbe::detectsXyzScheme()
{
    auto r = TileDatasetProbe::probe(m_direct, QStringLiteral("osm"), m_cuba);
    QVERIFY(r.has_value());
    QCOMPARE(r->dataset.scheme, TileScheme::XYZ);
}

void TstProbe::detectsTmsScheme()
{
    auto r = TileDatasetProbe::probe(m_tms, QStringLiteral("tms"), m_cuba);
    QVERIFY(r.has_value());
    QCOMPARE(r->dataset.scheme, TileScheme::TMS);
    QVERIFY(r->schemeReason.contains(QStringLiteral("TMS")));
}

void TstProbe::detectsTileSize()
{
    auto small = TileDatasetProbe::probe(m_direct, QStringLiteral("a"), m_cuba);
    QVERIFY(small.has_value());
    QCOMPARE(small->dataset.tileSize, 256);

    auto big = TileDatasetProbe::probe(m_big, QStringLiteral("b"), m_cuba);
    QVERIFY(big.has_value());
    QCOMPARE(big->dataset.tileSize, 512);
}

void TstProbe::detectsSValue()
{
    auto r = TileDatasetProbe::probe(m_big, QStringLiteral("b2"), m_cuba);
    QVERIFY(r.has_value());
    QVERIFY(r->dataset.hasSColumn);
    QCOMPARE(r->dataset.sValue, 3);
}

void TstProbe::detectsMissingSColumn()
{
    auto r = TileDatasetProbe::probe(m_noS, QStringLiteral("nos"), m_cuba);
    QVERIFY(r.has_value());
    QVERIFY(!r->dataset.hasSColumn);

    bool warned = false;
    for (const QString &w : r->warnings)
        if (w.contains(QStringLiteral("'s'")))
            warned = true;
    QVERIFY(warned);
}

void TstProbe::reportsCoverage()
{
    auto r = TileDatasetProbe::probe(m_direct, QStringLiteral("osm"), m_cuba);
    QVERIFY(r.has_value());
    QVERIFY(r->coverage.isValid());

    // La extension detectada debe contener el area que se poblo.
    const double north = r->coverage.topLeft().latitude();
    const double south = r->coverage.bottomRight().latitude();
    const double west  = r->coverage.topLeft().longitude();
    const double east  = r->coverage.bottomRight().longitude();

    QVERIFY(north >= 23.3 - 1.0);
    QVERIFY(south <= 19.7 + 1.0);
    QVERIFY(west  <= -85.0 + 1.0);
    QVERIFY(east  >= -74.0 - 1.0);

    QVERIFY(r->totalTiles > 0);
    QVERIFY(r->fileSizeBytes > 0);
    QVERIFY(r->pageSize > 0);

    // El informe se genera sin reventar y menciona lo importante.
    const QString report = TileDatasetProbe::formatReport(*r);
    QVERIFY(report.contains(QStringLiteral("MAPEO DE ZOOM")));
    QVERIFY(report.contains(QStringLiteral("ESQUEMA EJE Y")));
    QVERIFY(report.contains(QStringLiteral("NIVELES")));
}

void TstProbe::failsOnMissingFile()
{
    auto r = TileDatasetProbe::probe(m_dir.filePath(QStringLiteral("nope.db")),
                                     QStringLiteral("x"), m_cuba);
    QVERIFY(!r.has_value());
}

void TstProbe::detectedDatasetActuallyReads()
{
    // Este es el test que cierra el circulo: se sondea a ciegas la BD con el
    // zoom invertido y el descriptor resultante se usa tal cual para leer.
    auto r = TileDatasetProbe::probe(m_inverted, QStringLiteral("sat_auto"), m_cuba);
    QVERIFY(r.has_value());

    RMapsTileSource src(r->dataset);
    QVERIFY2(src.open(), qPrintable(src.lastError()));

    const int z = r->dataset.maxZoom;
    const auto range = TileMatrix::rangeFor(QGeoCoordinate(23.0, -84.0),
                                            QGeoCoordinate(20.0, -75.0), z);
    const auto tiles = src.fetchRange(z, range.xMin, range.xMax,
                                      range.yMin, range.yMax);

    QVERIFY2(!tiles.isEmpty(),
             "El descriptor autodetectado debe permitir leer teselas reales");
    for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it) {
        QCOMPARE(it.key().z, z);
        QVERIFY(it.value().startsWith("\x89PNG"));
    }
}

void TstProbe::jsonRoundTrip()
{
    auto r = TileDatasetProbe::probe(m_inverted, QStringLiteral("sat"), m_cuba);
    QVERIFY(r.has_value());

    const QJsonObject json = r->dataset.toJson();
    const TileDataset back = TileDataset::fromJson(json);

    QCOMPARE(back.id,         r->dataset.id);
    QCOMPARE(back.zFactor,    r->dataset.zFactor);
    QCOMPARE(back.zOffset,    r->dataset.zOffset);
    QCOMPARE(back.minZoom,    r->dataset.minZoom);
    QCOMPARE(back.maxZoom,    r->dataset.maxZoom);
    QCOMPARE(back.recommendedMaxZoom, r->dataset.recommendedMaxZoom);
    QCOMPARE(back.scheme,     r->dataset.scheme);
    QCOMPARE(back.sValue,     r->dataset.sValue);
    QCOMPARE(back.tileSize,   r->dataset.tileSize);
    QCOMPARE(back.hasSColumn, r->dataset.hasSColumn);
    QVERIFY(back.isValid());
}

void TstProbe::coverageComesFromReferenceLevel()
{
    auto r = TileDatasetProbe::probe(m_direct, QStringLiteral("cov"), m_cuba);
    QVERIFY(r.has_value());
    QVERIFY(r->referenceLevel >= 0);

    // El nivel de referencia es el mas poblado, no el mas profundo.
    const ZoomLevelStats &ref = r->levels.at(r->referenceLevel);
    for (const ZoomLevelStats &l : r->levels)
        QVERIFY(l.tileCount <= ref.tileCount);

    // Y la extension sale de el.
    QCOMPARE(r->coverage.topLeft().longitude(), ref.lonWest);
    QCOMPARE(r->coverage.bottomRight().longitude(), ref.lonEast);

    // Cada nivel trae su propia extension, y todas cubren Cuba.
    for (const ZoomLevelStats &l : r->levels) {
        QVERIFY(l.lonEast > l.lonWest);
        QVERIFY(l.latNorth > l.latSouth);
    }
}

void TstProbe::detectsSliverLevelAndCapsZoom()
{
    // Se fabrica el caso real de la BD de OSM: un nivel extra con muchas
    // teselas pero concentradas en una franja estrechisima, fuera del area
    // util. Antes ese nivel dictaba la extension de toda la BD.
    const QString path = m_dir.filePath(QStringLiteral("sliver.sqlitedb"));

    SyntheticSpec base;
    base.path = path;
    base.minLogicalZ = 8;
    base.maxLogicalZ = 11;
    QVERIFY(buildSyntheticDb(base) > 0);

    // Franja de 4 columnas al oeste, en el nivel 12.
    SyntheticSpec sliver;
    sliver.path = path;
    sliver.minLogicalZ = 12;
    sliver.maxLogicalZ = 12;
    sliver.northWest = QGeoCoordinate(23.3, -85.0);
    sliver.southEast = QGeoCoordinate(19.7, -84.93);   // franja muy estrecha
    sliver.createTable = false;                        // se anade a la existente
    QVERIFY(buildSyntheticDb(sliver) > 0);

    auto r = TileDatasetProbe::probe(path, QStringLiteral("sliver"), m_cuba);
    QVERIFY(r.has_value());

    // Las teselas del nivel 12 existen y se pueden pedir...
    QCOMPARE(r->dataset.maxZoom, 12);
    QVERIFY(r->dataset.zoomInRange(12));

    // ...pero el zoom que deberia ofrecer la interfaz se queda en 11.
    QCOMPARE(r->dataset.recommendedMaxZoom, 11);
    QVERIFY(!r->dataset.zoomRecommended(12));

    bool warned = false;
    for (const QString &w : r->warnings)
        if (w.contains(QStringLiteral("Zoom maximo recomendado")))
            warned = true;
    QVERIFY(warned);

    // Y la extension NO sale de la franja: sale del nivel de referencia.
    QVERIFY(r->coverage.topLeft().longitude() < -84.0);
    QVERIFY(r->coverage.bottomRight().longitude() > -76.0);
}

QTEST_MAIN(TstProbe)
#include "tst_probe.moc"
