#include "libmapa/VectorFormats.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace libmapa;

//! Tests de lectura/escritura GeoJSON, KML y GPX (sin ventanas).
class TstVectorFormats : public QObject
{
    Q_OBJECT

private slots:
    void detectsFormatByExtension();
    void readsGeoJsonFeatureCollection();
    void geoJsonRoundTripKeepsEverything();
    void readsKmlWithSharedStyleAndFolders();
    void kmlRoundTrip();
    void readsGpxWaypointsRoutesAndTracks();
    void gpxRoundTripKeepsPolygons();
    void reportsBadInput();
    void writesAndReadsFiles();
    void gpxTracksKeepTimes();
    void gpxRoutesKnowStartAndEnd();
};

// Tres entidades de prueba: un punto con atributos, una linea de dos partes y
// un poligono con estilo propio.
static QVector<MapFeature> muestra()
{
    MapFeature p;
    p.kind = GeometryKind::Point;
    p.name = QStringLiteral("Faro");
    p.description = QStringLiteral("Morro de La Habana");
    p.type = QStringLiteral("faro");
    p.geometry = { QGeoCoordinate(23.15, -82.3567) };
    p.attributes.insert(QStringLiteral("altura_m"), 44);
    p.attributes.insert(QStringLiteral("codigo"), QStringLiteral("F-01"));
    p.style.lineColor = QColor(0x12, 0x34, 0x56);

    MapFeature l;
    l.kind = GeometryKind::Polyline;
    l.name = QStringLiteral("Corredor");
    l.geometry = { QGeoCoordinate(22.0, -80.0), QGeoCoordinate(22.5, -79.5) };
    l.parts = { l.geometry, { QGeoCoordinate(21.0, -78.0), QGeoCoordinate(21.2, -77.8),
                              QGeoCoordinate(21.4, -77.9) } };
    l.style.lineWidth = 3.5;

    MapFeature z;
    z.kind = GeometryKind::Polygon;
    z.name = QStringLiteral("Zona A");
    z.type = QStringLiteral("zona_prohibida");
    z.geometry = { QGeoCoordinate(23.0, -82.5), QGeoCoordinate(23.0, -81.5),
                   QGeoCoordinate(23.5, -81.5), QGeoCoordinate(23.5, -82.5) };
    z.style.lineColor = QColor(200, 0, 0);
    z.style.fillColor = QColor(200, 0, 0, 64);
    return { p, l, z };
}

// Compara dos listas de vertices con tolerancia de ~1 cm.
static bool mismasCoords(const QVector<QGeoCoordinate> &a, const QVector<QGeoCoordinate> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < int(a.size()); ++i)
        if (qAbs(a[i].latitude() - b[i].latitude()) > 1e-7
            || qAbs(a[i].longitude() - b[i].longitude()) > 1e-7)
            return false;
    return true;
}

// La extension decide el formato, sin distinguir mayusculas.
void TstVectorFormats::detectsFormatByExtension()
{
    QCOMPARE(vectorFormatForPath(QStringLiteral("a.geojson")), VectorFormat::GeoJson);
    QCOMPARE(vectorFormatForPath(QStringLiteral("a.JSON")), VectorFormat::GeoJson);
    QCOMPARE(vectorFormatForPath(QStringLiteral("dir/b.kml")), VectorFormat::Kml);
    QCOMPARE(vectorFormatForPath(QStringLiteral("c.GPX")), VectorFormat::Gpx);
    QCOMPARE(vectorFormatForPath(QStringLiteral("d.shp")), VectorFormat::Unknown);
}

// GeoJSON tipico de otra herramienta: lon/lat, anillo cerrado, hueco, Multi* y
// propiedades con estilo simplestyle.
void TstVectorFormats::readsGeoJsonFeatureCollection()
{
    const QByteArray json = R"({
      "type": "FeatureCollection",
      "features": [
        { "type": "Feature",
          "properties": { "name": "P", "marker-color": "#ff0000", "pob": 1200 },
          "geometry": { "type": "Point", "coordinates": [-82.0, 23.0, 15.0] } },
        { "type": "Feature", "properties": { "stroke": "#00ff00", "stroke-width": 4 },
          "geometry": { "type": "Polygon", "coordinates": [
             [[-82,23],[-81,23],[-81,24],[-82,24],[-82,23]],
             [[-81.6,23.4],[-81.4,23.4],[-81.4,23.6],[-81.6,23.4]] ] } },
        { "type": "Feature", "properties": null,
          "geometry": { "type": "MultiLineString", "coordinates": [
             [[0,0],[1,1]], [[2,2],[3,3],[4,4]] ] } },
        { "type": "Feature", "properties": {},
          "geometry": { "type": "LineString", "coordinates": [[0,0]] } }
      ] })";
    QString err;
    const QVector<MapFeature> f = parseGeoJson(json, &err);
    QCOMPARE(f.size(), 3);   // la linea de un solo vertice se descarta

    QCOMPARE(f[0].kind, GeometryKind::Point);
    QCOMPARE(f[0].name, QStringLiteral("P"));
    QCOMPARE(f[0].position(), QGeoCoordinate(23.0, -82.0));
    QCOMPARE(f[0].style.lineColor, QColor(255, 0, 0));
    QCOMPARE(f[0].attributes.value(QStringLiteral("pob")).toInt(), 1200);

    QCOMPARE(f[1].kind, GeometryKind::Polygon);
    QCOMPARE(f[1].geometry.size(), 4);       // sin el vertice de cierre
    QVERIFY(!f[1].isMultiPart());            // el hueco se ignora
    QCOMPARE(f[1].style.lineWidth, 4.0);
    QCOMPARE(f[1].style.lineColor, QColor(0, 255, 0));

    QCOMPARE(f[2].kind, GeometryKind::Polyline);
    QCOMPARE(f[2].parts.size(), 2);
    QCOMPARE(f[2].parts[1].size(), 3);
}

// Escribir y releer GeoJSON conserva geometria, partes, nombre, descripcion,
// tipo, atributos y estilo.
void TstVectorFormats::geoJsonRoundTripKeepsEverything()
{
    const QVector<MapFeature> orig = muestra();
    const QVector<MapFeature> f = parseGeoJson(toGeoJson(orig));
    QCOMPARE(f.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(f[i].kind, orig[i].kind);
        QCOMPARE(f[i].name, orig[i].name);
        QCOMPARE(f[i].type, orig[i].type);
        QCOMPARE(f[i].outlines().size(), orig[i].outlines().size());
        for (int k = 0; k < int(f[i].outlines().size()); ++k)
            QVERIFY(mismasCoords(f[i].outlines()[k], orig[i].outlines()[k]));
    }
    QCOMPARE(f[0].description, orig[0].description);
    QCOMPARE(f[0].attributes.value(QStringLiteral("altura_m")).toInt(), 44);
    QCOMPARE(f[0].attributes.value(QStringLiteral("codigo")).toString(), QStringLiteral("F-01"));
    QCOMPARE(f[0].style.lineColor, orig[0].style.lineColor);
    QCOMPARE(f[1].style.lineWidth, 3.5);
    QCOMPARE(f[2].style.fillColor.rgb(), orig[2].style.fillColor.rgb());
    QVERIFY(qAbs(f[2].style.fillColor.alpha() - 64) <= 1);
}

// KML como lo escribe Google Earth: estilos compartidos por id, carpetas,
// colores aabbggrr, ExtendedData y un MultiGeometry.
void TstVectorFormats::readsKmlWithSharedStyleAndFolders()
{
    const QByteArray kml = R"(<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2"><Document>
  <Style id="rojo"><LineStyle><color>ff0000ff</color><width>3</width></LineStyle>
    <PolyStyle><color>4000ff00</color></PolyStyle></Style>
  <Folder><name>Zonas</name>
    <Placemark><name>Z1</name><styleUrl>#rojo</styleUrl>
      <ExtendedData><Data name="nivel"><value>alto</value></Data></ExtendedData>
      <Polygon><outerBoundaryIs><LinearRing><coordinates>
        -82,23,0 -81,23,0 -81,24,0 -82,24,0 -82,23,0
      </coordinates></LinearRing></outerBoundaryIs>
      <innerBoundaryIs><LinearRing><coordinates>-81.6,23.4 -81.4,23.4 -81.5,23.6 -81.6,23.4</coordinates></LinearRing></innerBoundaryIs>
      </Polygon></Placemark>
  </Folder>
  <Placemark><name>Dos puntos</name><MultiGeometry>
    <Point><coordinates>-80,22</coordinates></Point>
    <Point><coordinates>-79,21</coordinates></Point></MultiGeometry></Placemark>
</Document></kml>)";
    QString err;
    const QVector<MapFeature> f = parseKml(kml, &err);
    QCOMPARE(f.size(), 2);
    QCOMPARE(f[0].name, QStringLiteral("Z1"));
    QCOMPARE(f[0].kind, GeometryKind::Polygon);
    QCOMPARE(f[0].geometry.size(), 4);
    QVERIFY(!f[0].isMultiPart());                       // el hueco no es una parte
    QCOMPARE(f[0].style.lineColor, QColor(255, 0, 0));  // ff0000ff = rojo opaco
    QCOMPARE(f[0].style.lineWidth, 3.0);
    QCOMPARE(f[0].style.fillColor, QColor(0, 255, 0, 0x40));
    QCOMPARE(f[0].attributes.value(QStringLiteral("nivel")).toString(), QStringLiteral("alto"));
    QCOMPARE(f[1].kind, GeometryKind::Point);
    QCOMPARE(f[1].parts.size(), 2);
    QCOMPARE(f[1].parts[1].first(), QGeoCoordinate(21.0, -79.0));
}

// KML escrito por la libreria se relee igual (tipo y atributos en ExtendedData).
void TstVectorFormats::kmlRoundTrip()
{
    const QVector<MapFeature> orig = muestra();
    const QVector<MapFeature> f = parseKml(toKml(orig));
    QCOMPARE(f.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(f[i].kind, orig[i].kind);
        QCOMPARE(f[i].name, orig[i].name);
        QCOMPARE(f[i].type, orig[i].type);
        QCOMPARE(f[i].outlines().size(), orig[i].outlines().size());
        for (int k = 0; k < int(f[i].outlines().size()); ++k)
            QVERIFY(mismasCoords(f[i].outlines()[k], orig[i].outlines()[k]));
    }
    QCOMPARE(f[0].attributes.value(QStringLiteral("codigo")).toString(), QStringLiteral("F-01"));
    QCOMPARE(f[0].style.lineColor, orig[0].style.lineColor);
    QCOMPARE(f[2].style.fillColor, orig[2].style.fillColor);
    QCOMPARE(f[1].style.lineWidth, 3.5);
}

// GPX de un GPS: un waypoint con elevacion, una ruta (cuyos puntos tienen su
// propio nombre, que no debe pisar el de la ruta) y un track de dos segmentos.
void TstVectorFormats::readsGpxWaypointsRoutesAndTracks()
{
    const QByteArray gpx = R"(<?xml version="1.0"?>
<gpx version="1.1" creator="gps" xmlns="http://www.topografix.com/GPX/1/1">
  <wpt lat="23.1" lon="-82.3"><ele>12.5</ele><name>Base</name><type>campamento</type></wpt>
  <rte><name>Ruta 1</name>
    <rtept lat="22" lon="-80"><name>R1</name></rtept>
    <rtept lat="22.1" lon="-80.1"><name>R2</name></rtept></rte>
  <trk><name>Track</name>
    <trkseg><trkpt lat="21" lon="-79"/><trkpt lat="21.1" lon="-79.1"/></trkseg>
    <trkseg><trkpt lat="20" lon="-78"/><trkpt lat="20.1" lon="-78.1"/></trkseg></trk>
</gpx>)";
    QString err;
    const QVector<MapFeature> f = parseGpx(gpx, &err);
    QCOMPARE(f.size(), 3);
    QCOMPARE(f[0].kind, GeometryKind::Point);
    QCOMPARE(f[0].name, QStringLiteral("Base"));
    QCOMPARE(f[0].type, QStringLiteral("campamento"));
    QCOMPARE(f[0].attributes.value(QStringLiteral("ele")).toDouble(), 12.5);
    QCOMPARE(f[1].kind, GeometryKind::Polyline);
    QCOMPARE(f[1].name, QStringLiteral("Ruta 1"));
    QCOMPARE(f[1].geometry.size(), 2);
    QCOMPARE(f[2].name, QStringLiteral("Track"));
    QCOMPARE(f[2].parts.size(), 2);
}

// GPX no tiene poligonos: la libreria los escribe como track cerrado con su
// marca y, al releerlos, vuelven a ser poligonos con los mismos vertices.
void TstVectorFormats::gpxRoundTripKeepsPolygons()
{
    const QVector<MapFeature> orig = muestra();
    const QVector<MapFeature> f = parseGpx(toGpx(orig));
    QCOMPARE(f.size(), 3);
    QCOMPARE(f[0].kind, GeometryKind::Point);
    QCOMPARE(f[0].type, QStringLiteral("faro"));
    QCOMPARE(f[1].kind, GeometryKind::Polyline);
    QCOMPARE(f[1].parts.size(), 2);
    QCOMPARE(f[2].kind, GeometryKind::Polygon);
    QCOMPARE(f[2].name, QStringLiteral("Zona A"));
    QVERIFY(mismasCoords(f[2].geometry, orig[2].geometry));
}

// Contenido roto o vacio: lista vacia con motivo, sin colgarse.
void TstVectorFormats::reportsBadInput()
{
    QString err;
    QVERIFY(parseGeoJson("{ no es json", &err).isEmpty());
    QVERIFY(!err.isEmpty());
    err.clear();
    QVERIFY(parseGeoJson(R"({"type":"FeatureCollection","features":[]})", &err).isEmpty());
    QVERIFY(!err.isEmpty());
    err.clear();
    QVERIFY(parseKml("<kml><Document><Placemark>", &err).isEmpty());
    QVERIFY(!err.isEmpty());
    err.clear();
    QVERIFY(parseGpx("<gpx></gpx>", &err).isEmpty());
    QVERIFY(!err.isEmpty());
}

// Por fichero: escribe en los tres formatos y relee; extension desconocida y
// fichero inexistente devuelven error.
void TstVectorFormats::writesAndReadsFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &ext : { QStringLiteral("geojson"), QStringLiteral("kml"), QStringLiteral("gpx") }) {
        const QString ruta = dir.filePath(QStringLiteral("capa.") + ext);
        QString err;
        QVERIFY2(writeVectorFile(ruta, muestra(), &err), qPrintable(err));
        QCOMPARE(readVectorFile(ruta, &err).size(), 3);
    }
    QString err;
    QVERIFY(!writeVectorFile(dir.filePath(QStringLiteral("x.shp")), muestra(), &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(readVectorFile(dir.filePath(QStringLiteral("no_existe.kml")), &err).isEmpty());
}

// Recorridos con hora: se escriben con <time> UTC con milisegundos y se releen
// iguales (nombre, tipo, id del objetivo, posiciones y horas). Un punto sin
// hora sale sin <time>; uno invalido no se escribe.
void TstVectorFormats::gpxTracksKeepTimes()
{
    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760419413120LL).toUTC();
    GpxTrack a;
    a.name = QStringLiteral("Buque <1> & co");
    a.type = QStringLiteral("buque");
    a.targetId = 42;
    a.points = { { QGeoCoordinate(23.1, -82.3), t0 },
                 { QGeoCoordinate(), t0.addSecs(5) },                // invalido: fuera
                 { QGeoCoordinate(23.2, -82.25), t0.addMSecs(10500) },
                 { QGeoCoordinate(23.3, -82.2), QDateTime() } };     // sin hora
    GpxTrack b;
    b.name = QStringLiteral("UAV");
    b.points = { { QGeoCoordinate(-33.9, 151.2), t0.addDays(1) } };

    const QByteArray gpx = tracksToGpx({ a, b });
    QVERIFY(gpx.contains("<time>2025-10-14T05:23:33.120Z</time>"));
    QVERIFY(gpx.contains("<time>2025-10-14T05:23:43.620Z</time>"));
    QVERIFY(gpx.contains("targetId>42<"));
    QCOMPARE(gpx.count("<trkpt"), 4);
    QCOMPARE(gpx.count("<time>"), 3);

    QString err;
    const QVector<GpxTrack> leidos = parseGpxTracks(gpx, &err);
    QVERIFY2(leidos.size() == 2, qPrintable(err));
    QCOMPARE(leidos[0].name, a.name);
    QCOMPARE(leidos[0].type, a.type);
    QCOMPARE(leidos[0].targetId, qint64(42));
    QCOMPARE(leidos[0].points.size(), 3);
    QCOMPARE(leidos[0].points[0].position, QGeoCoordinate(23.1, -82.3));
    QCOMPARE(leidos[0].points[0].time, t0);
    QCOMPARE(leidos[0].points[1].time, t0.addMSecs(10500));
    QVERIFY(!leidos[0].points[2].time.isValid());
    QCOMPARE(leidos[1].targetId, qint64(-1));
    QCOMPARE(leidos[1].points[0].time, t0.addDays(1));

    // parseGpx lee el mismo fichero como entidades (sin horas): el track de un
    // solo punto no es una linea valida y se queda fuera.
    QCOMPARE(parseGpx(gpx).size(), 1);

    // Hora con zona horaria distinta de Z y sin milisegundos: se pasa a UTC.
    const QByteArray ajeno =
        "<gpx><trk><name>x</name><trkseg>"
        "<trkpt lat=\"1\" lon=\"2\"><ele>5</ele><time>2025-10-14T07:00:00+02:00</time></trkpt>"
        "</trkseg><trkseg><trkpt lat=\"1.5\" lon=\"2.5\"/></trkseg></trk></gpx>";
    const QVector<GpxTrack> otro = parseGpxTracks(ajeno);
    QCOMPARE(otro.size(), 1);
    QCOMPARE(otro[0].points.size(), 2);                   // segmentos concatenados
    QCOMPARE(otro[0].points[0].time,
             QDateTime::fromString(QStringLiteral("2025-10-14T05:00:00Z"), Qt::ISODate));

    QVERIFY(parseGpxTracks("<gpx><wpt lat=\"1\" lon=\"2\"/></gpx>", &err).isEmpty());
    QVERIFY(!err.isEmpty());
}

// Una ruta o track leido como entidad sabe que lo es (gpx_kind) y, si sus
// puntos traen hora, cuando empezo y acabo (time_start / time_end, UTC). Un
// historial exportado trae ademas el id del objetivo.
void TstVectorFormats::gpxRoutesKnowStartAndEnd()
{
    const QDateTime t0 = QDateTime::fromMSecsSinceEpoch(1760419413000LL).toUTC();
    GpxTrack h;
    h.name = QStringLiteral("Buque 7");
    h.targetId = 7;
    h.points = { { QGeoCoordinate(23.0, -82.0), t0 },
                 { QGeoCoordinate(23.1, -82.0), t0.addSecs(600) },
                 { QGeoCoordinate(23.2, -82.1), t0.addSecs(1800) } };
    const QVector<MapFeature> f = parseGpx(tracksToGpx({ h }));
    QCOMPARE(f.size(), 1);
    QCOMPARE(f[0].kind, GeometryKind::Polyline);
    QCOMPARE(f[0].attributes.value(QStringLiteral("gpx_kind")).toString(), QStringLiteral("trk"));
    QCOMPARE(f[0].attributes.value(QStringLiteral("target_id")).toLongLong(), 7LL);
    QCOMPARE(QDateTime::fromString(f[0].attributes.value(QStringLiteral("time_start")).toString(),
                                   Qt::ISODateWithMs), t0);
    QCOMPARE(QDateTime::fromString(f[0].attributes.value(QStringLiteral("time_end")).toString(),
                                   Qt::ISODateWithMs), t0.addSecs(1800));

    // Ruta sin horas: es ruta, pero sin inicio ni fin con hora. Los waypoints
    // y los poligonos no se marcan como ruta.
    const QByteArray otro =
        "<gpx><rte><name>R</name><rtept lat=\"1\" lon=\"2\"/><rtept lat=\"1.1\" lon=\"2.1\"/></rte>"
        "<wpt lat=\"3\" lon=\"4\"/></gpx>";
    const QVector<MapFeature> g = parseGpx(otro);
    QCOMPARE(g.size(), 2);
    for (const MapFeature &e : g) {
        if (e.kind == GeometryKind::Polyline) {
            QCOMPARE(e.attributes.value(QStringLiteral("gpx_kind")).toString(), QStringLiteral("rte"));
            QVERIFY(!e.attributes.contains(QStringLiteral("time_start")));
        } else {
            QVERIFY(!e.attributes.contains(QStringLiteral("gpx_kind")));
        }
    }
}

QTEST_MAIN(TstVectorFormats)
#include "tst_vectorformats.moc"
