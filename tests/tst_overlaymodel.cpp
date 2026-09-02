#include "libmapa/MapFeature.h"
#include "widget/OverlayModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace libmapa;

//! Tests del MODELO, sin ventanas. El dibujo se prueba en tst_mapwidget.
class TstOverlayModel : public QObject
{
    Q_OBJECT

private slots:
    // --- Capas -----------------------------------------------------------
    void hasADefaultLayer();
    void createsAndRemovesLayers();
    void ordersLayersByZOrder();
    void countsFeaturesPerLayer();
    void createsMissingLayerOnDemand();

    // --- Entidades -------------------------------------------------------
    void assignsIdsAndStoresFeatures();
    void rejectsInvalidGeometry();
    void keepsDomainTypeAndAttributes();
    void filtersByTypeAcrossLayers();

    // --- Edicion ---------------------------------------------------------
    void movesVertices();
    void insertsAndRemovesVertices();
    void refusesToDegenerateGeometry();
    void movesWholeFeature();
    void refusesMovesOutsideTheWorld();

    // --- Seleccion y senales ---------------------------------------------
    void tracksSelection();
    void clearsSelectionWhenFeatureIsRemoved();
    void emitsChangedOnEveryMutation();
};

static MapFeature punto(const QString &nombre, double lat, double lon,
                        const QString &capa = QString())
{
    MapFeature f;
    f.kind = GeometryKind::Point;
    f.name = nombre;
    f.layerId = capa;
    f.geometry = {QGeoCoordinate(lat, lon)};
    return f;
}

static MapFeature poligono(const QString &nombre, int vertices,
                           const QString &capa = QString())
{
    MapFeature f;
    f.kind = GeometryKind::Polygon;
    f.name = nombre;
    f.layerId = capa;
    for (int i = 0; i < vertices; ++i)
        f.geometry.append(QGeoCoordinate(23.0 + i * 0.01, -82.0 + i * 0.01));
    return f;
}

// ------------------------------------------------------------------ capas --

void TstOverlayModel::hasADefaultLayer()
{
    OverlayModel m;
    QCOMPARE(m.layers().size(), 1);
    QVERIFY(m.hasLayer(OverlayModel::defaultLayerId()));
    QCOMPARE(m.count(), 0);
}

void TstOverlayModel::createsAndRemovesLayers()
{
    OverlayModel m;
    QSignalSpy capas(&m, &OverlayModel::layersChanged);

    QVERIFY(m.addLayer(QStringLiteral("zonas_prohibidas"),
                       QStringLiteral("Zonas prohibidas")));
    QVERIFY(m.addLayer(QStringLiteral("puntos_interes")));
    QCOMPARE(m.layers().size(), 3);
    QCOMPARE(capas.count(), 2);

    // Repetir el id no crea una segunda.
    QVERIFY(!m.addLayer(QStringLiteral("zonas_prohibidas")));
    QCOMPARE(m.layers().size(), 3);

    QVERIFY(m.removeLayer(QStringLiteral("puntos_interes")));
    QCOMPARE(m.layers().size(), 2);

    // La capa por defecto no se puede borrar: siempre hay donde poner algo.
    QVERIFY(!m.removeLayer(OverlayModel::defaultLayerId()));
}

void TstOverlayModel::ordersLayersByZOrder()
{
    OverlayModel m;
    m.addLayer(QStringLiteral("fondo"), QString(), -10);
    m.addLayer(QStringLiteral("encima"), QString(), 100);
    m.addLayer(QStringLiteral("medio"), QString(), 5);

    const auto capas = m.layers();
    QCOMPARE(capas.first().id, QStringLiteral("fondo"));
    QCOMPARE(capas.last().id, QStringLiteral("encima"));

    // Cambiar el orden reordena.
    m.setLayerZOrder(QStringLiteral("fondo"), 200);
    QCOMPARE(m.layers().last().id, QStringLiteral("fondo"));
}

void TstOverlayModel::countsFeaturesPerLayer()
{
    OverlayModel m;
    m.addLayer(QStringLiteral("zonas"));

    m.addFeature(punto(QStringLiteral("A"), 23.0, -82.0, QStringLiteral("zonas")));
    m.addFeature(punto(QStringLiteral("B"), 23.1, -82.1, QStringLiteral("zonas")));
    m.addFeature(punto(QStringLiteral("C"), 23.2, -82.2));

    QCOMPARE(m.layer(QStringLiteral("zonas"))->featureCount, 2);
    QCOMPARE(m.layer(OverlayModel::defaultLayerId())->featureCount, 1);
    QCOMPARE(m.featuresInLayer(QStringLiteral("zonas")).size(), 2);

    m.clearLayer(QStringLiteral("zonas"));
    QCOMPARE(m.featuresInLayer(QStringLiteral("zonas")).size(), 0);
    QCOMPARE(m.count(), 1);      // la de la capa por defecto sigue
}

void TstOverlayModel::createsMissingLayerOnDemand()
{
    OverlayModel m;
    // Una capa nueva se crea sola: obligar a declararlas todas por adelantado
    // no aporta nada.
    const qint64 id = m.addFeature(
        punto(QStringLiteral("X"), 23.0, -82.0, QStringLiteral("rutas_dron")));
    QVERIFY(id > 0);
    QVERIFY(m.hasLayer(QStringLiteral("rutas_dron")));
}

// -------------------------------------------------------------- entidades --

void TstOverlayModel::assignsIdsAndStoresFeatures()
{
    OverlayModel m;
    const qint64 a = m.addFeature(punto(QStringLiteral("Morro"), 23.15, -82.36));
    const qint64 b = m.addFeature(poligono(QStringLiteral("Zona"), 4));

    QVERIFY(a > 0);
    QVERIFY(b > a);
    QCOMPARE(m.count(), 2);

    const auto leida = m.feature(a);
    QVERIFY(leida.has_value());
    QCOMPARE(leida->name, QStringLiteral("Morro"));
    QCOMPARE(leida->kind, GeometryKind::Point);
    QCOMPARE(leida->id, a);

    QVERIFY(m.removeFeature(a));
    QVERIFY(!m.feature(a).has_value());
    QVERIFY(!m.removeFeature(a));       // ya no esta
}

void TstOverlayModel::rejectsInvalidGeometry()
{
    OverlayModel m;

    MapFeature sinGeometria;
    sinGeometria.kind = GeometryKind::Point;
    QCOMPARE(m.addFeature(sinGeometria), qint64(-1));

    // Un poligono necesita tres vertices; una poligonal, dos.
    QCOMPARE(m.addFeature(poligono(QStringLiteral("Corto"), 2)), qint64(-1));

    MapFeature coordenadaMala;
    coordenadaMala.kind = GeometryKind::Point;
    coordenadaMala.geometry = {QGeoCoordinate(200.0, -82.0)};   // invalida
    QCOMPARE(m.addFeature(coordenadaMala), qint64(-1));

    QCOMPARE(m.count(), 0);
}

void TstOverlayModel::keepsDomainTypeAndAttributes()
{
    OverlayModel m;

    // La libreria no interpreta nada de esto: solo lo conserva. Asi se pueden
    // anadir conceptos del dominio sin tocar la libreria.
    MapFeature zona = poligono(QStringLiteral("Sector norte"), 5,
                               QStringLiteral("zonas"));
    zona.type = QStringLiteral("zona_prohibida");
    zona.attributes[QStringLiteral("techo_m")] = 120;
    zona.attributes[QStringLiteral("vigencia")] = QStringLiteral("2026-09-01");
    zona.attributes[QStringLiteral("responsable")] = QStringLiteral("CID3");

    const qint64 id = m.addFeature(zona);
    QVERIFY(id > 0);

    const auto leida = m.feature(id);
    QVERIFY(leida.has_value());
    QCOMPARE(leida->type, QStringLiteral("zona_prohibida"));
    QCOMPARE(leida->attributes.value(QStringLiteral("techo_m")).toInt(), 120);
    QCOMPARE(leida->attributes.value(QStringLiteral("responsable")).toString(),
             QStringLiteral("CID3"));
}

void TstOverlayModel::filtersByTypeAcrossLayers()
{
    OverlayModel m;

    for (int i = 0; i < 3; ++i) {
        MapFeature f = poligono(QStringLiteral("Prohibida %1").arg(i), 4,
                                QStringLiteral("capa%1").arg(i));
        f.type = QStringLiteral("zona_prohibida");
        m.addFeature(f);
    }
    MapFeature otra = punto(QStringLiteral("Faro"), 23.0, -82.0);
    otra.type = QStringLiteral("punto_interes");
    m.addFeature(otra);

    // El filtro por tipo cruza capas: son cosas distintas.
    QCOMPARE(m.featuresOfType(QStringLiteral("zona_prohibida")).size(), 3);
    QCOMPARE(m.featuresOfType(QStringLiteral("punto_interes")).size(), 1);
    QCOMPARE(m.featuresOfType(QStringLiteral("no_existe")).size(), 0);
}

// ---------------------------------------------------------------- edicion --

void TstOverlayModel::movesVertices()
{
    OverlayModel m;
    const qint64 id = m.addFeature(poligono(QStringLiteral("Editable"), 4));

    const QGeoCoordinate destino(24.5, -80.5);
    QVERIFY(m.moveVertex(id, 2, destino));

    const auto f = m.feature(id);
    QVERIFY(qAbs(f->geometry[2].latitude() - 24.5) < 1e-9);
    QVERIFY(qAbs(f->geometry[2].longitude() + 80.5) < 1e-9);

    // Indices fuera de rango y coordenadas invalidas se rechazan.
    QVERIFY(!m.moveVertex(id, 99, destino));
    QVERIFY(!m.moveVertex(id, -1, destino));
    QVERIFY(!m.moveVertex(id, 0, QGeoCoordinate()));
    QVERIFY(!m.moveVertex(9999, 0, destino));
}

void TstOverlayModel::insertsAndRemovesVertices()
{
    OverlayModel m;
    const qint64 id = m.addFeature(poligono(QStringLiteral("Editable"), 4));

    QVERIFY(m.insertVertex(id, 2, QGeoCoordinate(23.5, -81.5)));
    QCOMPARE(m.feature(id)->geometry.size(), 5);
    QVERIFY(qAbs(m.feature(id)->geometry[2].latitude() - 23.5) < 1e-9);

    QVERIFY(m.removeVertex(id, 2));
    QCOMPARE(m.feature(id)->geometry.size(), 4);

    // Un punto tiene un solo vertice: no admite insercion.
    const qint64 p = m.addFeature(punto(QStringLiteral("P"), 23.0, -82.0));
    QVERIFY(!m.insertVertex(p, 0, QGeoCoordinate(23.1, -82.1)));
}

void TstOverlayModel::refusesToDegenerateGeometry()
{
    OverlayModel m;
    const qint64 id = m.addFeature(poligono(QStringLiteral("Triangulo"), 3));

    // Un poligono de dos vertices no es un poligono. Se rechaza en vez de
    // dejar una geometria invalida que reventaria al dibujar.
    QVERIFY(!m.removeVertex(id, 0));
    QCOMPARE(m.feature(id)->geometry.size(), 3);

    // Quien quiera deshacerse de el tiene removeFeature.
    QVERIFY(m.removeFeature(id));
}

void TstOverlayModel::movesWholeFeature()
{
    OverlayModel m;
    const qint64 id = m.addFeature(poligono(QStringLiteral("Movible"), 4));
    const auto antes = m.feature(id)->geometry;

    QVERIFY(m.moveFeature(id, 0.5, -1.0));

    const auto despues = m.feature(id)->geometry;
    QCOMPARE(despues.size(), antes.size());
    for (int i = 0; i < antes.size(); ++i) {
        QVERIFY(qAbs(despues[i].latitude() - (antes[i].latitude() + 0.5)) < 1e-9);
        QVERIFY(qAbs(despues[i].longitude() - (antes[i].longitude() - 1.0)) < 1e-9);
    }
}

void TstOverlayModel::refusesMovesOutsideTheWorld()
{
    OverlayModel m;
    const qint64 id = m.addFeature(punto(QStringLiteral("Polar"), 84.0, 0.0));

    // Un desplazamiento que saque la geometria del mundo se rechaza ENTERO.
    // QGeoCoordinate se marcaria invalida y devolveria NaN sin avisar, que es
    // lo que colgaba la aplicacion al arrastrar el mapa en zoom 3.
    QVERIFY(!m.moveFeature(id, 20.0, 0.0));
    QVERIFY(qAbs(m.feature(id)->position().latitude() - 84.0) < 1e-9);

    QVERIFY(m.moveFeature(id, 1.0, 0.0));
    QVERIFY(qAbs(m.feature(id)->position().latitude() - 85.0) < 1e-9);
}

// ------------------------------------------------------ seleccion y senales --

void TstOverlayModel::tracksSelection()
{
    OverlayModel m;
    QSignalSpy seleccion(&m, &OverlayModel::selectionChanged);

    const qint64 a = m.addFeature(punto(QStringLiteral("A"), 23.0, -82.0));
    const qint64 b = m.addFeature(punto(QStringLiteral("B"), 23.1, -82.1));

    QCOMPARE(m.selectedId(), qint64(-1));

    m.setSelected(a);
    QCOMPARE(m.selectedId(), a);
    QCOMPARE(seleccion.count(), 1);

    m.setSelected(a);                      // repetir no emite
    QCOMPARE(seleccion.count(), 1);

    m.setSelected(b);
    QCOMPARE(seleccion.count(), 2);

    m.setSelected(9999);                   // inexistente: se ignora
    QCOMPARE(m.selectedId(), b);

    m.clearSelection();
    QCOMPARE(m.selectedId(), qint64(-1));
}

void TstOverlayModel::clearsSelectionWhenFeatureIsRemoved()
{
    OverlayModel m;
    const qint64 id = m.addFeature(punto(QStringLiteral("A"), 23.0, -82.0));
    m.setSelected(id);

    QSignalSpy seleccion(&m, &OverlayModel::selectionChanged);
    QVERIFY(m.removeFeature(id));

    // Sin esto quedaria un identificador seleccionado que ya no existe.
    QCOMPARE(m.selectedId(), qint64(-1));
    QCOMPARE(seleccion.count(), 1);
}

void TstOverlayModel::emitsChangedOnEveryMutation()
{
    OverlayModel m;
    QSignalSpy cambios(&m, &OverlayModel::changed);

    const qint64 id = m.addFeature(poligono(QStringLiteral("Z"), 4));
    int esperados = 1;
    QCOMPARE(cambios.count(), esperados);

    m.moveVertex(id, 0, QGeoCoordinate(24.0, -81.0));   ++esperados;
    m.insertVertex(id, 1, QGeoCoordinate(23.5, -81.5)); ++esperados;
    m.removeVertex(id, 1);                              ++esperados;
    m.moveFeature(id, 0.1, 0.1);                        ++esperados;
    m.setSelected(id);                                  ++esperados;
    m.addLayer(QStringLiteral("otra"));                 ++esperados;
    m.setLayerVisible(QStringLiteral("otra"), false);   ++esperados;

    // Una sola senal para todo: al que dibuja no le importa que cambio.
    QCOMPARE(cambios.count(), esperados);
}

QTEST_MAIN(TstOverlayModel)
#include "tst_overlaymodel.moc"
