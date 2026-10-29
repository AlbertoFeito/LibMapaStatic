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
    //! Circulos y arcos: validos con centro y radio, sin vertices que insertar o
    //! quitar, y moverlos mueve el centro.
    void roundFeaturesBehaveInTheModel();
    //! Alta en lote, conteo y borrado por tipo y filtro de visibilidad por tipo:
    //! UNA senal de repintado y UN paso de deshacer, no una por entidad.
    void batchAddCountAndClearByType();
    void typeVisibilityIsAViewFilter();
    void keepsDomainTypeAndAttributes();
    void filtersByTypeAcrossLayers();

    // --- Edicion ---------------------------------------------------------
    void movesVertices();
    void insertsAndRemovesVertices();
    void refusesToDegenerateGeometry();
    void movesWholeFeature();
    void refusesMovesOutsideTheWorld();
    //! setLabelOffset guarda el desplazamiento del rotulo y avisa (lo usa el
    //! arrastre de la etiqueta).
    void setsLabelOffsetAndNotifies();
    //! insertVertex respeta el tope 'max_vertices' (área objetivo de ≤N puntos).
    void insertVertexRespectsMaxVerticesCap();

    // --- Seleccion y senales ---------------------------------------------
    void tracksSelection();
    void clearsSelectionWhenFeatureIsRemoved();
    void notifiesObserversOnClear();
    void emitsChangedOnEveryMutation();

    // --- Deshacer --------------------------------------------------------
    //! Cada edicion (mover/insertar/quitar vertice, mover entidad, desplazar
    //! etiqueta) se deshace de verdad y se rehace. Regresion: la instantanea
    //! de deshacer compartia datos con el modelo y la edicion la alteraba.
    void undoRestoresEveryEdit();
    void undoLeavesTransientLayersAlone();
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

void TstOverlayModel::roundFeaturesBehaveInTheModel()
{
    OverlayModel m;

    MapFeature c;
    c.kind = GeometryKind::Circle;
    c.name = QStringLiteral("Zona");
    c.geometry = { QGeoCoordinate(23.0, -82.0) };
    c.radiusMeters = 10000.0;

    // Sin radio, o un arco sin barrido, no entran.
    MapFeature sinRadio = c;
    sinRadio.radiusMeters = 0.0;
    QCOMPARE(m.addFeature(sinRadio), qint64(-1));
    MapFeature arcoPlano = c;
    arcoPlano.kind = GeometryKind::Arc;
    arcoPlano.arcStartDeg = arcoPlano.arcEndDeg = 90.0;
    QCOMPARE(m.addFeature(arcoPlano), qint64(-1));

    const qint64 id = m.addFeature(c);
    QVERIFY(id > 0);
    MapFeature a = c;
    a.kind = GeometryKind::Arc;
    a.arcStartDeg = 0.0;
    a.arcEndDeg = 90.0;
    const qint64 ida = m.addFeature(a);
    QVERIFY(ida > 0);

    // Un circulo o un arco tiene un solo vertice: no se inserta ni se quita ninguno.
    QVERIFY(!m.insertVertex(id, 1, QGeoCoordinate(23.1, -82.1)));
    QVERIFY(!m.insertVertex(ida, 0, QGeoCoordinate(23.1, -82.1)));
    QVERIFY(!m.removeVertex(id, 0));

    // Mover el vertice o la entidad entera mueve el centro, y el radio se conserva.
    QVERIFY(m.moveVertex(id, 0, QGeoCoordinate(23.5, -82.5)));
    QCOMPARE(m.feature(id)->geometry.first(), QGeoCoordinate(23.5, -82.5));
    QVERIFY(m.moveFeature(ida, 0.5, 0.25));
    QVERIFY(qAbs(m.feature(ida)->geometry.first().latitude() - 23.5) < 1e-9);
    QVERIFY(qAbs(m.feature(ida)->radiusMeters - 10000.0) < 1e-9);
    QVERIFY(m.feature(ida)->isValid());
}

void TstOverlayModel::batchAddCountAndClearByType()
{
    OverlayModel m;
    QVector<MapFeature> lote;
    for (int i = 0; i < 6; ++i) {
        MapFeature f = punto(QStringLiteral("P%1").arg(i), 23.0 + 0.01 * i, -82.0,
                             i < 4 ? QStringLiteral("aero") : QStringLiteral("zonas"));
        f.type = (i % 2 == 0) ? QStringLiteral("internos") : QStringLiteral("externos");
        lote.append(f);
    }
    MapFeature malo;                     // sin geometria: no entra
    malo.kind = GeometryKind::Point;
    lote.insert(2, malo);

    QSignalSpy cambios(&m, &OverlayModel::changed);
    QSignalSpy altas(&m, &OverlayModel::featureAdded);
    const QVector<qint64> ids = m.addFeatures(lote);
    QCOMPARE(ids.size(), lote.size());
    QCOMPARE(ids.at(2), qint64(-1));                 // la invalida lleva -1, en su sitio
    QCOMPARE(m.count(), 6);
    QCOMPARE(cambios.count(), 1);                    // UN repintado, no seis
    QCOMPARE(altas.count(), 6);                      // pero cada alta se anuncia (persistencia)
    QCOMPARE(m.undoDepth(), 1);                      // y un solo paso de deshacer

    // Conteo por tipo, en todo el modelo y por capa.
    QMap<QString, int> todo = m.typeCounts();
    QCOMPARE(todo.value(QStringLiteral("internos")), 3);
    QCOMPARE(todo.value(QStringLiteral("externos")), 3);
    QCOMPARE(m.typeCounts(QStringLiteral("aero")).value(QStringLiteral("internos")), 2);
    QCOMPARE(m.typeCounts(QStringLiteral("zonas")).value(QStringLiteral("externos")), 1);
    QVERIFY(m.typeCounts(QStringLiteral("no_existe")).isEmpty());

    // Borrar un tipo de una capa: solo los de esa capa, con un repintado y un paso.
    cambios.clear();
    QSignalSpy bajas(&m, &OverlayModel::featureRemoved);
    QCOMPARE(m.clearType(QStringLiteral("internos"), QStringLiteral("aero")), 2);
    QCOMPARE(m.count(), 4);
    QCOMPARE(cambios.count(), 1);
    QCOMPARE(bajas.count(), 2);
    QCOMPARE(m.undoDepth(), 2);
    QCOMPARE(m.typeCounts().value(QStringLiteral("internos")), 1);   // queda el de «zonas»

    // Deshacer devuelve las dos de golpe.
    QVERIFY(m.undo());
    QCOMPARE(m.count(), 6);

    // Sin capa, borra el tipo en todas; un tipo que no existe no hace nada ni repinta.
    cambios.clear();
    QCOMPARE(m.clearType(QStringLiteral("externos")), 3);
    QCOMPARE(m.count(), 3);
    QCOMPARE(cambios.count(), 1);
    cambios.clear();
    QCOMPARE(m.clearType(QStringLiteral("inventado")), 0);
    QCOMPARE(cambios.count(), 0);
}

void TstOverlayModel::typeVisibilityIsAViewFilter()
{
    OverlayModel m;
    MapFeature f = punto(QStringLiteral("A"), 23.0, -82.0);
    f.type = QStringLiteral("internos");
    const qint64 id = m.addFeature(f);
    QSignalSpy cambios(&m, &OverlayModel::changed);
    const int pasos = m.undoDepth();

    QVERIFY(m.isTypeVisible(QStringLiteral("internos")));
    m.setTypeVisible(QStringLiteral("internos"), false);
    QVERIFY(!m.isTypeVisible(QStringLiteral("internos")));
    QCOMPARE(m.hiddenTypes(), QStringList({ QStringLiteral("internos") }));
    QCOMPARE(cambios.count(), 1);
    m.setTypeVisible(QStringLiteral("internos"), false);   // sin cambio: no repinta
    QCOMPARE(cambios.count(), 1);

    // Es solo un filtro: la entidad sigue ahi, visible por si misma y sin deshacer.
    QVERIFY(m.feature(id)->visible);
    QCOMPARE(m.featuresOfType(QStringLiteral("internos")).size(), 1);
    QCOMPARE(m.undoDepth(), pasos);

    m.setTypeVisible(QStringLiteral("internos"), true);
    QVERIFY(m.hiddenTypes().isEmpty());
    QCOMPARE(cambios.count(), 2);
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

void TstOverlayModel::notifiesObserversOnClear()
{
    // Un vaciado tiene que ser observable entidad a entidad, igual que un
    // borrado individual. Sin esto, un panel de capas no se enteraba de
    // "Vaciar" (solo se emitia changed()) y quedaba desincronizado.
    OverlayModel m;
    m.addFeature(punto(QStringLiteral("A"), 23.0, -82.0, QStringLiteral("zonas")));
    m.addFeature(punto(QStringLiteral("B"), 23.1, -82.1, QStringLiteral("zonas")));
    m.addFeature(punto(QStringLiteral("C"), 23.2, -82.2));   // capa por defecto

    QSignalSpy borradas(&m, &OverlayModel::featureRemoved);
    QSignalSpy capas(&m, &OverlayModel::layersChanged);

    m.clearLayer(QStringLiteral("zonas"));
    QCOMPARE(borradas.count(), 2);          // una senal por entidad de la capa
    QVERIFY(capas.count() >= 1);            // los contadores por capa cambiaron
    QCOMPARE(m.count(), 1);                 // la de la capa por defecto sigue

    m.clear();
    QCOMPARE(borradas.count(), 3);          // + la unica que quedaba
    QCOMPARE(m.count(), 0);

    // Vaciar dos veces no vuelve a avisar: no hay nada que borrar.
    const int antes = static_cast<int>(borradas.count());
    m.clear();
    QCOMPARE(borradas.count(), antes);
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

void TstOverlayModel::setsLabelOffsetAndNotifies()
{
    OverlayModel m;
    const qint64 id = m.addFeature(punto(QStringLiteral("Obj"), 23.0, -82.0));
    QVERIFY(id > 0);

    QSignalSpy upd(&m, &OverlayModel::featureUpdated);
    QVERIFY(m.setLabelOffset(id, QPointF(30.0, -12.0)));
    QCOMPARE(upd.count(), 1);

    const auto f = m.feature(id);
    QVERIFY(f.has_value());
    QCOMPARE(f->style.labelOffsetPx, QPointF(30.0, -12.0));

    // Id inexistente: no hace nada y lo dice.
    QVERIFY(!m.setLabelOffset(99999, QPointF(1.0, 1.0)));
}

void TstOverlayModel::insertVertexRespectsMaxVerticesCap()
{
    OverlayModel m;
    MapFeature area = poligono(QStringLiteral("Area"), 10);  // ya tiene 10 vertices
    area.type = QStringLiteral("objetivo_area");
    area.attributes.insert(QStringLiteral("max_vertices"), 10);
    const qint64 id = m.addFeature(area);
    QVERIFY(id > 0);

    // En el tope (10/10): no se puede insertar un vertice mas.
    QVERIFY(!m.insertVertex(id, 10, QGeoCoordinate(24.0, -83.0)));
    QCOMPARE(m.feature(id)->geometry.size(), 10);

    // Sin tope, un poligono equivalente sí admite el vertice 11.
    MapFeature libre = poligono(QStringLiteral("Libre"), 10);
    const qint64 id2 = m.addFeature(libre);
    QVERIFY(m.insertVertex(id2, 10, QGeoCoordinate(24.0, -83.0)));
    QCOMPARE(m.feature(id2)->geometry.size(), 11);
}

// ---------------------------------------------------------------- deshacer --

void TstOverlayModel::undoRestoresEveryEdit()
{
    OverlayModel m;
    const qint64 p = m.addFeature(punto(QStringLiteral("P"), 23.0, -82.0));
    const qint64 z = m.addFeature(poligono(QStringLiteral("Z"), 4));
    const MapFeature zonaOriginal = *m.feature(z);

    // Mover el vertice de un punto (lo que hace el arrastre) y deshacerlo.
    QVERIFY(m.moveVertex(p, 0, QGeoCoordinate(24.0, -81.0)));
    QVERIFY(m.undo());
    QCOMPARE(m.feature(p)->position(), QGeoCoordinate(23.0, -82.0));
    QVERIFY(m.redo());
    QCOMPARE(m.feature(p)->position(), QGeoCoordinate(24.0, -81.0));

    // Insertar y quitar vertices.
    QVERIFY(m.insertVertex(z, 1, QGeoCoordinate(22.9, -81.95)));
    QVERIFY(m.undo());
    QCOMPARE(m.feature(z)->geometry, zonaOriginal.geometry);
    QVERIFY(m.removeVertex(z, 0));
    QVERIFY(m.undo());
    QCOMPARE(m.feature(z)->geometry, zonaOriginal.geometry);

    // Mover la entidad entera.
    QVERIFY(m.moveFeature(z, 0.5, 0.5));
    QVERIFY(m.undo());
    QCOMPARE(m.feature(z)->geometry, zonaOriginal.geometry);

    // Desplazar la etiqueta.
    QVERIFY(m.setLabelOffset(z, QPointF(30, -20)));
    QVERIFY(m.undo());
    QCOMPARE(m.feature(z)->style.labelOffsetPx, zonaOriginal.style.labelOffsetPx);
}

// Una capa fuera de deshacer: sus altas no apilan nada, y deshacer una edicion
// del usuario (anterior o posterior) no la toca; borrarla tampoco apila.
void TstOverlayModel::undoLeavesTransientLayersAlone()
{
    OverlayModel m;
    m.addLayer(QStringLiteral("temp"));
    m.setLayerUndoable(QStringLiteral("temp"), false);
    QVERIFY(!m.isLayerUndoable(QStringLiteral("temp")));

    const qint64 usuario = m.addFeature(punto(QStringLiteral("U"), 1.0, 1.0));
    QCOMPARE(m.undoDepth(), 1);

    MapFeature t = punto(QStringLiteral("T"), 2.0, 2.0);
    t.layerId = QStringLiteral("temp");
    const qint64 temporal = m.addFeature(t);
    QCOMPARE(m.undoDepth(), 1);                       // no apila

    QVERIFY(m.undo());                                // deshace la del usuario
    QVERIFY(!m.feature(usuario).has_value());
    QVERIFY(m.feature(temporal).has_value());         // la temporal sigue
    QVERIFY(m.redo());
    QVERIFY(m.feature(usuario).has_value());
    QVERIFY(m.feature(temporal).has_value());

    // Se quita la capa temporal y se deshace algo del usuario: no reaparece.
    QVERIFY(m.moveFeature(usuario, 0.5, 0.5));
    m.clearLayer(QStringLiteral("temp"));
    QVERIFY(!m.feature(temporal).has_value());
    QVERIFY(m.undo());
    QVERIFY(!m.feature(temporal).has_value());
    // Un alta nueva no reutiliza el id de la temporal.
    QVERIFY(m.addFeature(punto(QStringLiteral("N"), 3.0, 3.0)) > temporal);
}

QTEST_MAIN(TstOverlayModel)
#include "tst_overlaymodel.moc"
