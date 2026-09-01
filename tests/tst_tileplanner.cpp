#include "geo/TileMatrix.h"
#include "tiles/TileCache.h"
#include "tiles/TilePlanner.h"

#include <QBuffer>
#include <QtTest>

using namespace libmapa;

static QImage makeImage(int size = 256, QColor c = Qt::red)
{
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(c);
    return img;
}

class TstTilePlanner : public QObject
{
    Q_OBJECT

private slots:
    void allExactWhenCacheIsFull();
    void fallsBackToParent();
    void fallsBackToNearestAncestorAvailable();
    void reportsMissingTiles();
    void emptyWhenNothingAvailable();

    void sourceRectSplitsParentInFour();
    void sourceRectCoversGrandparentSixteenth();
    void sourceRectsTileTheParentExactly();

    void respectsMinZoom();
    void respectsMaxAncestorDepth();

    void exactTilesPaintedLast();

    /*! El escenario que motivo toda esta clase: la BD satelital tiene un
     *  32 % de relleno porque no guarda teselas de mar. */
    void handlesSatelliteLikeSparsity();

    void coverageAccounting();
};

void TstTilePlanner::allExactWhenCacheIsFull()
{
    TileCache cache(64 * 1024 * 1024);
    TileMatrix::TileRange range{10, 100, 104, 200, 204};

    for (int x = range.xMin; x <= range.xMax; ++x)
        for (int y = range.yMin; y <= range.yMax; ++y)
            cache.insertImage(TileKey{10, x, y}, makeImage());

    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    QCOMPARE(plan.items.size(), 25);
    QCOMPARE(plan.exactCount, 25);
    QCOMPARE(plan.fallbackCount, 0);
    QCOMPARE(plan.emptyCount, 0);
    QVERIFY(plan.missing.isEmpty());
    QCOMPARE(plan.coverage(), 1.0);

    for (const TileDrawItem &i : plan.items) {
        QVERIFY(i.isExact());
        QCOMPARE(i.slot, i.source);
        QCOMPARE(i.sourceRect, QRectF(0, 0, 256, 256));
    }
}

void TstTilePlanner::fallsBackToParent()
{
    TileCache cache(64 * 1024 * 1024);
    // Solo el padre esta en cache.
    cache.insertImage(TileKey{9, 50, 100}, makeImage());

    TileMatrix::TileRange range{10, 100, 101, 200, 201};   // las 4 hijas
    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    QCOMPARE(plan.items.size(), 4);
    QCOMPARE(plan.exactCount, 0);
    QCOMPARE(plan.fallbackCount, 4);
    QCOMPARE(plan.missing.size(), 4);

    for (const TileDrawItem &i : plan.items) {
        QCOMPARE(i.ancestorDepth, 1);
        QCOMPARE(i.source, TileKey({9, 50, 100}));
        QCOMPARE(i.sourceRect.width(), 128.0);
        QCOMPARE(i.sourceRect.height(), 128.0);
    }
}

void TstTilePlanner::fallsBackToNearestAncestorAvailable()
{
    TileCache cache(64 * 1024 * 1024);
    // Ni la tesela ni el padre, pero si el bisabuelo.
    cache.insertImage(TileKey{7, 12, 25}, makeImage());
    cache.insertImage(TileKey{5, 3, 6}, makeImage());

    TileMatrix::TileRange range{10, 100, 100, 200, 200};
    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    QCOMPARE(plan.items.size(), 1);
    // 100>>3 = 12, 200>>3 = 25  -> el de z=7 esta a profundidad 3
    QCOMPARE(plan.items.first().ancestorDepth, 3);
    QCOMPARE(plan.items.first().source, TileKey({7, 12, 25}));
    // Se elige el MAS cercano, no el mas lejano.
    QVERIFY(plan.items.first().source.z > 5);
}

void TstTilePlanner::reportsMissingTiles()
{
    TileCache cache(64 * 1024 * 1024);
    cache.insertImage(TileKey{10, 100, 200}, makeImage());

    TileMatrix::TileRange range{10, 100, 101, 200, 200};
    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    // La que esta en cache no se pide; la otra si.
    QCOMPARE(plan.missing.size(), 1);
    QCOMPARE(plan.missing.first(), TileKey({10, 101, 200}));
}

void TstTilePlanner::emptyWhenNothingAvailable()
{
    TileCache cache(64 * 1024 * 1024);
    TileMatrix::TileRange range{10, 100, 102, 200, 202};

    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    QVERIFY(plan.items.isEmpty());
    QCOMPARE(plan.emptyCount, 9);
    QCOMPARE(plan.missing.size(), 9);
    QCOMPARE(plan.coverage(), 0.0);
}

void TstTilePlanner::sourceRectSplitsParentInFour()
{
    TilePlanner planner(256);

    // Padre {9,50,100} -> hijas {10, 100..101, 200..201}
    QCOMPARE(planner.sourceRectFor(TileKey{10, 100, 200}, 1), QRectF(0, 0, 128, 128));
    QCOMPARE(planner.sourceRectFor(TileKey{10, 101, 200}, 1), QRectF(128, 0, 128, 128));
    QCOMPARE(planner.sourceRectFor(TileKey{10, 100, 201}, 1), QRectF(0, 128, 128, 128));
    QCOMPARE(planner.sourceRectFor(TileKey{10, 101, 201}, 1), QRectF(128, 128, 128, 128));
}

void TstTilePlanner::sourceRectCoversGrandparentSixteenth()
{
    TilePlanner planner(256);
    const QRectF r = planner.sourceRectFor(TileKey{10, 103, 205}, 2);

    QCOMPARE(r.width(), 64.0);
    QCOMPARE(r.height(), 64.0);
    // 103 % 4 = 3, 205 % 4 = 1
    QCOMPARE(r.x(), 192.0);
    QCOMPARE(r.y(), 64.0);
}

void TstTilePlanner::sourceRectsTileTheParentExactly()
{
    TilePlanner planner(256);

    // Las 16 nietas de un abuelo deben recubrirlo sin solaparse ni dejar hueco.
    const TileKey g{8, 25, 50};
    double area = 0.0;
    QVector<QRectF> rects;

    for (int dx = 0; dx < 4; ++dx) {
        for (int dy = 0; dy < 4; ++dy) {
            const TileKey slot{10, (g.x << 2) + dx, (g.y << 2) + dy};
            const QRectF r = planner.sourceRectFor(slot, 2);
            QVERIFY(QRectF(0, 0, 256, 256).contains(r));
            for (const QRectF &other : rects)
                QVERIFY2(!r.intersects(other), "Los trozos no deben solaparse");
            rects.append(r);
            area += r.width() * r.height();
        }
    }
    QCOMPARE(area, 256.0 * 256.0);
}

void TstTilePlanner::respectsMinZoom()
{
    TileCache cache(64 * 1024 * 1024);
    cache.insertImage(TileKey{5, 3, 6}, makeImage());

    TileMatrix::TileRange range{10, 100, 100, 200, 200};
    TilePlanner planner(256);

    // Con minZoom 5 se llega hasta el ancestro de z=5.
    const auto ok = planner.plan(range, cache, /*minZoom=*/5);
    QCOMPARE(ok.fallbackCount, 1);

    // Con minZoom 8 no se permite subir tan arriba: queda vacio.
    const auto capped = planner.plan(range, cache, /*minZoom=*/8);
    QCOMPARE(capped.fallbackCount, 0);
    QCOMPARE(capped.emptyCount, 1);
}

void TstTilePlanner::respectsMaxAncestorDepth()
{
    TileCache cache(64 * 1024 * 1024);
    cache.insertImage(TileKey{5, 3, 6}, makeImage());   // profundidad 5

    TileMatrix::TileRange range{10, 100, 100, 200, 200};
    TilePlanner planner(256);

    QCOMPARE(planner.plan(range, cache, 0, /*maxDepth=*/5).fallbackCount, 1);
    QCOMPARE(planner.plan(range, cache, 0, /*maxDepth=*/4).fallbackCount, 0);
}

void TstTilePlanner::exactTilesPaintedLast()
{
    TileCache cache(64 * 1024 * 1024);
    cache.insertImage(TileKey{8, 25, 50}, makeImage());    // abuelo
    cache.insertImage(TileKey{9, 50, 100}, makeImage());   // padre
    cache.insertImage(TileKey{10, 100, 200}, makeImage()); // exacta

    TileMatrix::TileRange range{10, 100, 103, 200, 203};
    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    // El orden debe ir de menor a mayor zoom de origen, para que lo nitido
    // quede encima y no haya parpadeo al llegar teselas nuevas.
    for (int i = 1; i < plan.items.size(); ++i)
        QVERIFY(plan.items[i - 1].source.z <= plan.items[i].source.z);

    QCOMPARE(plan.items.last().source.z, 10);
    QVERIFY(plan.items.last().isExact());
}

void TstTilePlanner::handlesSatelliteLikeSparsity()
{
    // Se reproduce la densidad medida en la BD satelital real: alrededor de
    // un 32 % en el nivel pedido, con el nivel padre mejor cubierto.
    TileCache cache(1024LL * 1024 * 1024);
    TileMatrix::TileRange range{13, 2200, 2229, 3560, 3589};   // 900 huecos

    int seeded = 0;
    for (int x = range.xMin; x <= range.xMax; ++x)
        for (int y = range.yMin; y <= range.yMax; ++y)
            if ((x * 31 + y * 17) % 100 < 32) {       // ~32 %
                cache.insertImage(TileKey{13, x, y}, makeImage());
                ++seeded;
            }

    // Padres: todos disponibles (el nivel 12 tiene mejor relleno).
    for (int x = range.xMin / 2; x <= range.xMax / 2; ++x)
        for (int y = range.yMin / 2; y <= range.yMax / 2; ++y)
            cache.insertImage(TileKey{12, x, y}, makeImage());

    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    QCOMPARE(plan.slotCount(), 900);
    QCOMPARE(plan.exactCount, seeded);
    QCOMPARE(plan.emptyCount, 0);

    // Lo esencial: NO queda ni un hueco en blanco, pese a que solo un tercio
    // de las teselas exactas existe. Sin respaldo, dos tercios de la pantalla
    // estarian vacios.
    QCOMPARE(plan.coverage(), 1.0);
    QVERIFY(plan.fallbackCount > plan.exactCount);

    qInfo() << "Densidad tipo satelital:" << plan.exactCount << "exactas,"
            << plan.fallbackCount << "por respaldo, 0 huecos";
}

void TstTilePlanner::coverageAccounting()
{
    TileCache cache(64 * 1024 * 1024);
    TileMatrix::TileRange range{10, 100, 101, 200, 201};   // 4 huecos

    cache.insertImage(TileKey{10, 100, 200}, makeImage());  // 1 exacta
    cache.insertImage(TileKey{9, 50, 100}, makeImage());    // padre de las 4

    TilePlanner planner(256);
    const auto plan = planner.plan(range, cache);

    QCOMPARE(plan.exactCount, 1);
    QCOMPARE(plan.fallbackCount, 3);
    QCOMPARE(plan.emptyCount, 0);
    QCOMPARE(plan.slotCount(), 4);
    QCOMPARE(plan.coverage(), 1.0);
    QCOMPARE(plan.missing.size(), 3);
}

QTEST_MAIN(TstTilePlanner)
#include "tst_tileplanner.moc"
