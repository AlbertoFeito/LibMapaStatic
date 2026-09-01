#include "tiles/TileCache.h"
#include "tiles/TileKey.h"

#include <QBuffer>
#include <QImage>
#include <QtTest>

using namespace libmapa;

static QByteArray makePng(int size = 64, QColor color = Qt::red)
{
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(color);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

class TstTileCache : public QObject
{
    Q_OBJECT

private slots:
    void keyHashIsCollisionFree();
    void keyParentIsCorrect();
    void keyValidityRespectsWorldSize();

    void insertAndTake();
    void rejectsGarbage();
    void evictsLeastRecentlyUsed();
    void limitsByMemoryNotTileCount();
    void pinnedTilesSurviveDetailFlood();
    void remembersMissingTiles();
    void statsAreTracked();
    void clearEmpties();

    //! El motivo por el que la cache existe (falla F-15).
    void avoidsRepeatedDecoding();
};

void TstTileCache::keyHashIsCollisionFree()
{
    // Se genera un conjunto grande de claves y se comprueba que ninguna
    // colisiona en el QHash, que es lo que sustituye al triple bucle O(n^3).
    // Se empieza en z=5 porque por debajo la rejilla es tan pequena que las
    // claves generadas se repiten (no es colision de hash, es que son la
    // misma tesela).
    QHash<TileKey, int> map;
    QSet<TileKey> expected;
    int n = 0;
    for (int z = 5; z <= 17; ++z) {
        const int side = 1 << z;
        for (int i = 0; i < 40; ++i) {
            const TileKey k{z, (i * 7919) % side, (i * 6271) % side};
            map.insert(k, n++);
            expected.insert(k);
        }
    }
    // El QHash guarda exactamente tantas entradas como claves distintas hay:
    // ni una menos, que seria una colision silenciosa.
    QCOMPARE(map.size(), expected.size());
    QVERIFY(map.size() > 300);

    for (const TileKey &k : expected)
        QVERIFY(map.contains(k));

    // Claves distintas en un solo componente no se confunden.
    QVERIFY(TileKey({8, 1, 2}) != TileKey({8, 2, 1}));
    QVERIFY(TileKey({8, 1, 2}) != TileKey({9, 1, 2}));
    QVERIFY(TileKey({8, 1, 2}) == TileKey({8, 1, 2}));
}

void TstTileCache::keyParentIsCorrect()
{
    const TileKey child{10, 501, 383};
    const TileKey parent = child.parent();
    QCOMPARE(parent.z, 9);
    QCOMPARE(parent.x, 250);
    QCOMPARE(parent.y, 191);

    // Las cuatro hijas de una tesela comparten padre.
    for (int dx = 0; dx < 2; ++dx)
        for (int dy = 0; dy < 2; ++dy)
            QCOMPARE(TileKey({10, 250 * 2 + dx, 191 * 2 + dy}).parent(), parent);
}

void TstTileCache::keyValidityRespectsWorldSize()
{
    QVERIFY(TileKey({0, 0, 0}).isValid());
    QVERIFY(TileKey({8, 255, 255}).isValid());
    QVERIFY(!TileKey({8, 256, 0}).isValid());
    QVERIFY(!TileKey({8, 0, -1}).isValid());
    QVERIFY(!TileKey({-1, 0, 0}).isValid());
}

void TstTileCache::insertAndTake()
{
    TileCache cache(10 * 256 * 1024);
    const TileKey k{8, 70, 110};

    QVERIFY(cache.take(k).isNull());
    QVERIFY(!cache.contains(k));

    QVERIFY(cache.insert(k, makePng(64, Qt::blue)));
    QVERIFY(cache.contains(k));
    QCOMPARE(cache.count(), 1);

    const QImage img = cache.take(k);
    QVERIFY(!img.isNull());
    QCOMPARE(img.width(), 64);
    QCOMPARE(img.pixelColor(10, 10), QColor(Qt::blue));
}

void TstTileCache::rejectsGarbage()
{
    TileCache cache(10 * 256 * 1024);
    const TileKey k{8, 1, 1};

    QVERIFY(!cache.insert(k, QByteArray()));
    QVERIFY(!cache.insert(k, QByteArray("esto no es una imagen")));
    QCOMPARE(cache.count(), 0);
    QCOMPARE(cache.stats().decodeFailures, 1);   // el vacio ni se intenta
}

void TstTileCache::evictsLeastRecentlyUsed()
{
    // Cada imagen de 128x128 en RGB32 son 64 KiB. El limite se pone en 4 de
    // ellas para comprobar que la expulsion respeta la MEMORIA, no un conteo.
    const qint64 perImage = 128 * 128 * 4;
    TileCache cache(4 * perImage);

    for (int i = 0; i < 4; ++i)
        QVERIFY(cache.insert(TileKey{8, i, 0}, makePng(128)));
    QCOMPARE(cache.count(), 4);
    QCOMPARE(cache.usedBytes(), 4 * perImage);

    // Se toca la primera para que deje de ser la menos usada.
    QVERIFY(!cache.take(TileKey{8, 0, 0}).isNull());

    // Al insertar una quinta se expulsa alguna, pero nunca se rebasa el
    // limite de memoria.
    QVERIFY(cache.insert(TileKey{8, 99, 0}, makePng(128)));
    QVERIFY(cache.contains(TileKey{8, 99, 0}));
    QVERIFY(cache.usedBytes() <= cache.maxBytes());
    QVERIFY(cache.count() <= 4);
}

void TstTileCache::limitsByMemoryNotTileCount()
{
    // El motivo de todo esto: el peso en disco no predice el coste en RAM.
    // En las BD reales la tesela media de OSM ocupa 1.24 KiB comprimida y la
    // de la satelital 10.6 KiB, pero decodificadas ambas son iguales.
    const qint64 limit = 8 * 256 * 256 * 4;      // 8 teselas de 256 px = 2 MiB
    TileCache big(limit);

    for (int i = 0; i < 20; ++i)
        big.insert(TileKey{10, i, 0}, makePng(256));

    QVERIFY2(big.count() <= 8,
             qPrintable(QStringLiteral("caben %1 teselas de 256 px en 2 MiB")
                            .arg(big.count())));
    QVERIFY(big.usedBytes() <= limit);

    // Con teselas de 128 px, que ocupan la cuarta parte, caben cuatro veces mas.
    TileCache small(limit);
    for (int i = 0; i < 60; ++i)
        small.insert(TileKey{10, i, 0}, makePng(128));

    QVERIFY(small.usedBytes() <= limit);
    QVERIFY2(small.count() > big.count() * 3,
             qPrintable(QStringLiteral("128 px: %1 teselas; 256 px: %2")
                            .arg(small.count()).arg(big.count())));

    qInfo() << "En" << (limit / 1048576) << "MiB caben" << big.count()
            << "teselas de 256 px y" << small.count() << "de 128 px";
}

void TstTileCache::statsAreTracked()
{
    TileCache cache(10 * 256 * 1024);
    cache.resetStats();

    const TileKey k{8, 5, 5};
    cache.take(k);                       // fallo
    cache.insert(k, makePng(16));
    cache.take(k);                       // acierto
    cache.take(k);                       // acierto

    const auto s = cache.stats();
    QCOMPARE(s.misses, 1);
    QCOMPARE(s.hits, 2);
    QCOMPARE(s.inserts, 1);
    QVERIFY(s.hitRatio() > 0.6);
}

void TstTileCache::clearEmpties()
{
    TileCache cache(10 * 256 * 1024);
    for (int i = 0; i < 5; ++i)
        cache.insert(TileKey{8, i, 0}, makePng(16));
    QCOMPARE(cache.count(), 5);

    cache.clear();
    QCOMPARE(cache.count(), 0);
    QVERIFY(cache.take(TileKey{8, 0, 0}).isNull());
}

void TstTileCache::avoidsRepeatedDecoding()
{
    // Escenario del codigo original: 300 teselas visibles, y en cada
    // movimiento del mapa se volvia a llamar a loadFromData sobre todas.
    const int kTiles = 300;
    QVector<QByteArray> blobs;
    blobs.reserve(kTiles);
    for (int i = 0; i < kTiles; ++i)
        blobs.append(makePng(256, QColor(i % 256, 100, 200)));

    // (a) decodificar siempre, como antes
    QElapsedTimer t;
    t.start();
    for (int pass = 0; pass < 5; ++pass) {
        for (int i = 0; i < kTiles; ++i) {
            QImage img;
            QVERIFY(img.loadFromData(blobs[i]));
        }
    }
    const qint64 always = t.elapsed();

    // (b) decodificar una vez y servir de cache
    TileCache cache(static_cast<qint64>(kTiles) * 2 * 256 * 256 * 4);
    t.restart();
    for (int pass = 0; pass < 5; ++pass) {
        for (int i = 0; i < kTiles; ++i) {
            const TileKey k{10, i, 0};
            QImage img = cache.take(k);
            if (img.isNull()) {
                QVERIFY(cache.insert(k, blobs[i]));
                img = cache.take(k);
            }
            QVERIFY(!img.isNull());
        }
    }
    const qint64 cached = t.elapsed();

    qInfo() << "5 pasadas sobre" << kTiles << "teselas ->"
            << "sin cache:" << always << "ms,"
            << "con cache:" << cached << "ms";

    // Cada tesela se decodifica UNA sola vez en las 5 pasadas.
    QCOMPARE(cache.stats().inserts, qint64(kTiles));
    QCOMPARE(cache.stats().misses, qint64(kTiles));
    // Aciertos: 1 por tesela en la primera pasada (tras insertar) + 4 pasadas.
    QCOMPARE(cache.stats().hits, qint64(kTiles * 5));
    QVERIFY2(cached < always,
             "La cache debe ser mas rapida que redecodificar cada vez");
}

void TstTileCache::pinnedTilesSurviveDetailFlood()
{
    // Regresion del fallo que aparecio al acotar la cache por memoria: las
    // teselas de detalle expulsaban por LRU los niveles gruesos de respaldo,
    // y los huecos en blanco que se acababan de cubrir reaparecian.
    const qint64 detail = 8 * 256 * 256 * 4;     // caben 8 teselas de detalle
    const qint64 protectedArea = 4 * 256 * 256 * 4;
    TileCache cache(detail, protectedArea);

    // Cuatro teselas de respaldo, marcadas como protegidas.
    QVector<TileKey> fallback;
    for (int i = 0; i < 4; ++i) {
        const TileKey k{8, i, 0};
        QVERIFY(cache.insert(k, makePng(256), /*pinned=*/true));
        fallback.append(k);
    }
    QCOMPARE(cache.pinnedCount(), 4);

    // Avalancha de detalle: 200 teselas, muchas mas de las que caben.
    for (int i = 0; i < 200; ++i)
        cache.insert(TileKey{11, i, 0}, makePng(256));

    // Las protegidas siguen todas ahi.
    for (const TileKey &k : fallback) {
        QVERIFY2(cache.contains(k),
                 "Una tesela de respaldo fue expulsada por el detalle");
        QVERIFY(!cache.take(k).isNull());
    }
    QCOMPARE(cache.pinnedCount(), 4);

    // Y el area de detalle si respeta su propio limite.
    QVERIFY(cache.count() - cache.pinnedCount() <= 8);

    qInfo() << "Tras 200 teselas de detalle:" << cache.pinnedCount()
            << "protegidas intactas,"
            << (cache.count() - cache.pinnedCount()) << "de detalle";
}

void TstTileCache::remembersMissingTiles()
{
    TileCache cache(4 * 1024 * 1024);
    const TileKey a{10, 1, 1};
    const TileKey b{10, 2, 1};

    QVERIFY(!cache.isKnownMissing(a));
    cache.markMissing(a);
    QVERIFY(cache.isKnownMissing(a));
    QCOMPARE(cache.missingCount(), 1);

    // Una tesela anotada como ausente NO esta en cache: no hay imagen.
    QVERIFY(!cache.contains(a));
    QVERIFY(cache.take(a).isNull());

    // Pero cuenta como resuelta a efectos de no volver a pedirla.
    cache.insert(b, makePng(64));
    QCOMPARE(cache.countCached(10, 1, 2, 1, 1), 2);

    // Si mas adelante llega de verdad, deja de estar ausente.
    QVERIFY(cache.insert(a, makePng(64)));
    QVERIFY(!cache.isKnownMissing(a));
    QVERIFY(cache.contains(a));
    QCOMPARE(cache.missingCount(), 0);

    // Y el registro esta acotado: no crece sin limite.
    for (int i = 0; i < 60000; ++i)
        cache.markMissing(TileKey{12, i, 0});
    QVERIFY2(cache.missingCount() <= 50000,
             qPrintable(QStringLiteral("registro de ausencias sin acotar: %1")
                            .arg(cache.missingCount())));
}

QTEST_MAIN(TstTileCache)
#include "tst_tilecache.moc"
