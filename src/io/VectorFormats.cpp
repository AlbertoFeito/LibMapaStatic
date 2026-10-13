#include "libmapa/VectorFormats.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>

namespace libmapa {

namespace {

// Claves de "properties" que se traducen a campos de MapFeature; el resto van
// a attributes tal cual.
const QString kName = QStringLiteral("name");
const QString kDescription = QStringLiteral("description");
const QString kType = QStringLiteral("libmapa_type");
const QString kStroke = QStringLiteral("stroke");
const QString kStrokeWidth = QStringLiteral("stroke-width");
const QString kStrokeOpacity = QStringLiteral("stroke-opacity");
const QString kFill = QStringLiteral("fill");
const QString kFillOpacity = QStringLiteral("fill-opacity");
const QString kMarkerColor = QStringLiteral("marker-color");

// Espacio de nombres de la extension GPX propia (marca de poligono).
const QString kNsLibmapa = QStringLiteral("https://github.com/AlbertoFeito/LibMapaStatic");

// Numero con 7 decimales (~1 cm) sin ceros sobrantes: suficiente para
// cualquier mapa y deja ficheros legibles.
QString coord(double v)
{
    QString s = QString::number(v, 'f', 7);
    while (s.endsWith(QLatin1Char('0')))
        s.chop(1);
    if (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s;
}

// Dos vertices iguales (para quitar el cierre repetido de un anillo).
bool mismoPunto(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    return std::fabs(a.latitude() - b.latitude()) < 1e-9
        && std::fabs(a.longitude() - b.longitude()) < 1e-9;
}

// Anillo de poligono sin el vertice de cierre repetido (GeoJSON y KML lo
// repiten; MapFeature no).
QVector<QGeoCoordinate> sinCierre(QVector<QGeoCoordinate> anillo)
{
    if (anillo.size() > 1 && mismoPunto(anillo.first(), anillo.last()))
        anillo.removeLast();
    return anillo;
}

// Anillo cerrado (repite el primer vertice al final), como lo piden GeoJSON y KML.
QVector<QGeoCoordinate> conCierre(QVector<QGeoCoordinate> anillo)
{
    if (!anillo.isEmpty() && !mismoPunto(anillo.first(), anillo.last()))
        anillo.append(anillo.first());
    return anillo;
}

// Monta una entidad a partir de sus partes ya leidas y la anade a la lista si
// es valida (descarta partes degeneradas). Una parte = geometry; varias =
// multi-parte.
void anadir(QVector<MapFeature> &out, MapFeature f, GeometryKind kind,
            const QVector<QVector<QGeoCoordinate>> &partes)
{
    f.kind = kind;
    QVector<QVector<QGeoCoordinate>> validas;
    for (const QVector<QGeoCoordinate> &p : partes)
        if (f.partIsValid(p))
            validas.append(p);
    if (validas.isEmpty())
        return;
    f.geometry = validas.first();
    f.parts.clear();
    if (validas.size() > 1)
        f.parts = validas;
    out.append(f);
}

// Color con su opacidad (0..1) aplicada como alfa. En enteros 0..255: setAlphaF
// toma qreal en Qt 5 y float en Qt 6.
QColor conOpacidad(QColor c, double opacidad)
{
    c.setAlpha(qRound(qBound(0.0, opacidad, 1.0) * 255.0));
    return c;
}

// Opacidad 0..1 de un color (inversa de conOpacidad), redondeada a 3 decimales
// para no escribir 0.250980392... en el fichero.
double opacidad(const QColor &c)
{
    return std::round(c.alpha() / 255.0 * 1000.0) / 1000.0;
}

// ============================================================== GeoJSON ===

// Posicion GeoJSON [lon, lat(, alt)] -> coordenada (lat, lon). Invalida si no
// trae al menos dos numeros.
QGeoCoordinate posicion(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2 || !a.at(0).isDouble() || !a.at(1).isDouble())
        return QGeoCoordinate();
    return QGeoCoordinate(a.at(1).toDouble(), a.at(0).toDouble());
}

// Lista de posiciones GeoJSON -> vertices (salta las invalidas).
QVector<QGeoCoordinate> posiciones(const QJsonValue &v)
{
    QVector<QGeoCoordinate> out;
    const QJsonArray a = v.toArray();
    out.reserve(int(a.size()));
    for (const QJsonValue &p : a) {
        const QGeoCoordinate c = posicion(p);
        if (c.isValid())
            out.append(c);
    }
    return out;
}

// Traduce una geometria GeoJSON a entidad(es). Los poligonos se quedan con el
// anillo exterior (el primero); las colecciones de geometrias se recorren y
// cada miembro sale como entidad aparte con las mismas propiedades.
void geometriaGeoJson(const QJsonObject &g, const MapFeature &base, QVector<MapFeature> &out)
{
    const QString t = g.value(QStringLiteral("type")).toString();
    const QJsonValue c = g.value(QStringLiteral("coordinates"));
    if (t == QLatin1String("Point")) {
        anadir(out, base, GeometryKind::Point, { { posicion(c) } });
    } else if (t == QLatin1String("MultiPoint")) {
        QVector<QVector<QGeoCoordinate>> partes;
        for (const QGeoCoordinate &p : posiciones(c))
            partes.append({ p });
        anadir(out, base, GeometryKind::Point, partes);
    } else if (t == QLatin1String("LineString")) {
        anadir(out, base, GeometryKind::Polyline, { posiciones(c) });
    } else if (t == QLatin1String("MultiLineString")) {
        QVector<QVector<QGeoCoordinate>> partes;
        for (const QJsonValue &l : c.toArray())
            partes.append(posiciones(l));
        anadir(out, base, GeometryKind::Polyline, partes);
    } else if (t == QLatin1String("Polygon")) {
        anadir(out, base, GeometryKind::Polygon,
               { sinCierre(posiciones(c.toArray().at(0))) });
    } else if (t == QLatin1String("MultiPolygon")) {
        QVector<QVector<QGeoCoordinate>> partes;
        for (const QJsonValue &poly : c.toArray())
            partes.append(sinCierre(posiciones(poly.toArray().at(0))));
        anadir(out, base, GeometryKind::Polygon, partes);
    } else if (t == QLatin1String("GeometryCollection")) {
        for (const QJsonValue &sub : g.value(QStringLiteral("geometries")).toArray())
            geometriaGeoJson(sub.toObject(), base, out);
    }
}

// Lee las propiedades de un Feature: nombre, descripcion, tipo de dominio y
// estilo "simplestyle" a sus campos; el resto a attributes.
MapFeature baseGeoJson(const QJsonObject &props)
{
    MapFeature f;
    for (auto it = props.begin(); it != props.end(); ++it) {
        const QString &k = it.key();
        if (k == kName)
            f.name = it.value().toString();
        else if (k == kDescription)
            f.description = it.value().toString();
        else if (k == kType)
            f.type = it.value().toString();
        else if (k == kStrokeWidth)
            f.style.lineWidth = it.value().toDouble(f.style.lineWidth);
        else if (k != kStroke && k != kStrokeOpacity && k != kFill
                 && k != kFillOpacity && k != kMarkerColor)
            f.attributes.insert(k, it.value().toVariant());
    }
    const QColor stroke(props.value(kMarkerColor).toString(props.value(kStroke).toString()));
    if (stroke.isValid())
        f.style.lineColor = conOpacidad(stroke, props.value(kStrokeOpacity).toDouble(1.0));
    const QColor fill(props.value(kFill).toString());
    if (fill.isValid())
        f.style.fillColor = conOpacidad(fill, props.value(kFillOpacity).toDouble(0.25));
    return f;
}

// Coordenadas de una parte como array GeoJSON [[lon,lat], ...].
QJsonArray arrayDe(const QVector<QGeoCoordinate> &parte)
{
    QJsonArray a;
    for (const QGeoCoordinate &c : parte)
        a.append(QJsonArray{ c.longitude(), c.latitude() });
    return a;
}

// Geometria GeoJSON de una entidad: tipo simple con una parte, Multi* con varias.
QJsonObject geometriaDe(const MapFeature &f)
{
    const QVector<QVector<QGeoCoordinate>> partes = f.outlines();
    const bool multi = partes.size() > 1;
    QJsonObject g;
    QJsonArray coords;
    switch (f.kind) {
    case GeometryKind::Point:
        g.insert(QStringLiteral("type"), multi ? QStringLiteral("MultiPoint") : QStringLiteral("Point"));
        if (multi) {
            for (const auto &p : partes)
                coords.append(QJsonArray{ p.first().longitude(), p.first().latitude() });
            g.insert(QStringLiteral("coordinates"), coords);
        } else {
            g.insert(QStringLiteral("coordinates"),
                     QJsonArray{ partes.first().first().longitude(),
                                 partes.first().first().latitude() });
        }
        return g;
    case GeometryKind::Polyline:
        g.insert(QStringLiteral("type"), multi ? QStringLiteral("MultiLineString") : QStringLiteral("LineString"));
        if (multi) {
            for (const auto &p : partes)
                coords.append(arrayDe(p));
            g.insert(QStringLiteral("coordinates"), coords);
        } else {
            g.insert(QStringLiteral("coordinates"), arrayDe(partes.first()));
        }
        return g;
    case GeometryKind::Polygon:
        g.insert(QStringLiteral("type"), multi ? QStringLiteral("MultiPolygon") : QStringLiteral("Polygon"));
        if (multi) {
            for (const auto &p : partes)
                coords.append(QJsonArray{ arrayDe(conCierre(p)) });
        } else {
            coords.append(arrayDe(conCierre(partes.first())));
        }
        g.insert(QStringLiteral("coordinates"), coords);
        return g;
    }
    return g;
}

// ================================================================== KML ===

// Estilo KML resuelto: lo que se haya encontrado de linea y relleno.
struct EstiloKml {
    bool linea = false;
    QColor colorLinea;
    double ancho = 0.0;
    bool relleno = false;
    QColor colorRelleno;
    bool icono = false;
    QColor colorIcono;
};

// Color KML "aabbggrr" (hex, orden inverso al habitual) -> QColor.
QColor colorKml(const QString &txt)
{
    const QString s = txt.trimmed();
    if (s.size() != 8)
        return QColor();
    bool ok = false;
    const uint v = s.toUInt(&ok, 16);
    if (!ok)
        return QColor();
    return QColor(int(v & 0xff), int((v >> 8) & 0xff), int((v >> 16) & 0xff),
                  int((v >> 24) & 0xff));
}

// QColor -> "aabbggrr" de KML.
QString textoColorKml(const QColor &c)
{
    return QStringLiteral("%1%2%3%4")
        .arg(c.alpha(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.red(), 2, 16, QLatin1Char('0'));
}

// Texto "lon,lat[,alt] lon,lat ..." de un <coordinates> -> vertices.
QVector<QGeoCoordinate> coordenadasKml(const QString &txt)
{
    QVector<QGeoCoordinate> out;
    const QStringList tuplas = txt.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &t : tuplas) {
        const QStringList v = t.split(QLatin1Char(','));
        if (v.size() < 2)
            continue;
        bool okLon = false, okLat = false;
        const double lon = v.at(0).toDouble(&okLon);
        const double lat = v.at(1).toDouble(&okLat);
        if (okLon && okLat)
            out.append(QGeoCoordinate(lat, lon));
    }
    return out;
}

// Lee un <Style> (el lector esta sobre su apertura) hasta su cierre.
EstiloKml leerEstiloKml(QXmlStreamReader &xml)
{
    EstiloKml e;
    QString seccion;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("Style"))
            break;
        if (!xml.isStartElement())
            continue;
        const auto n = xml.name();
        if (n == QLatin1String("LineStyle") || n == QLatin1String("PolyStyle")
            || n == QLatin1String("IconStyle")) {
            seccion = n.toString();
        } else if (n == QLatin1String("color")) {
            const QColor c = colorKml(xml.readElementText());
            if (!c.isValid())
                continue;
            if (seccion == QLatin1String("LineStyle")) { e.linea = true; e.colorLinea = c; }
            else if (seccion == QLatin1String("PolyStyle")) { e.relleno = true; e.colorRelleno = c; }
            else if (seccion == QLatin1String("IconStyle")) { e.icono = true; e.colorIcono = c; }
        } else if (n == QLatin1String("width") && seccion == QLatin1String("LineStyle")) {
            e.ancho = xml.readElementText().toDouble();
        }
    }
    return e;
}

// Aplica a una entidad lo que traiga un estilo KML.
void aplicarEstilo(MapFeature &f, const EstiloKml &e)
{
    if (e.linea)
        f.style.lineColor = e.colorLinea;
    if (e.ancho > 0.0)
        f.style.lineWidth = e.ancho;
    if (e.relleno)
        f.style.fillColor = e.colorRelleno;
    if (e.icono && !e.linea)
        f.style.lineColor = e.colorIcono;   // un punto se pinta con el color de linea
}

// Lee un <Placemark> (el lector esta sobre su apertura) y anade sus entidades.
// Recoge nombre, descripcion, estilo (en linea o por styleUrl) y ExtendedData;
// las geometrias de un MultiGeometry se juntan como partes si son del mismo
// tipo (si no, salen como entidades separadas).
void leerPlacemark(QXmlStreamReader &xml, const QHash<QString, EstiloKml> &estilos,
                   QVector<MapFeature> &out)
{
    MapFeature base;
    QVector<QPair<GeometryKind, QVector<QGeoCoordinate>>> geoms;
    GeometryKind actual = GeometryKind::Point;
    bool enHueco = false;            // dentro de innerBoundaryIs: se ignora
    QString dato;                    // nombre del <Data> en curso

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("Placemark"))
                break;
            if (xml.name() == QLatin1String("innerBoundaryIs"))
                enHueco = false;
            continue;
        }
        if (!xml.isStartElement())
            continue;
        const auto n = xml.name();
        if (n == QLatin1String("name")) {
            base.name = xml.readElementText().trimmed();
        } else if (n == QLatin1String("description")) {
            base.description = xml.readElementText().trimmed();
        } else if (n == QLatin1String("styleUrl")) {
            const QString id = xml.readElementText().trimmed().remove(QLatin1Char('#'));
            if (estilos.contains(id))
                aplicarEstilo(base, estilos.value(id));
        } else if (n == QLatin1String("Style")) {
            aplicarEstilo(base, leerEstiloKml(xml));
        } else if (n == QLatin1String("Data")) {
            dato = xml.attributes().value(QLatin1String("name")).toString();
        } else if (n == QLatin1String("value") && !dato.isEmpty()) {
            const QString v = xml.readElementText();
            if (dato == kType)
                base.type = v;
            else
                base.attributes.insert(dato, v);
            dato.clear();
        } else if (n == QLatin1String("SimpleData")) {
            const QString k = xml.attributes().value(QLatin1String("name")).toString();
            base.attributes.insert(k, xml.readElementText());
        } else if (n == QLatin1String("Point")) {
            actual = GeometryKind::Point;
        } else if (n == QLatin1String("LineString")) {
            actual = GeometryKind::Polyline;
        } else if (n == QLatin1String("Polygon")) {
            actual = GeometryKind::Polygon;
        } else if (n == QLatin1String("innerBoundaryIs")) {
            enHueco = true;
        } else if (n == QLatin1String("coordinates")) {
            QVector<QGeoCoordinate> pts = coordenadasKml(xml.readElementText());
            if (enHueco)
                continue;
            if (actual == GeometryKind::Polygon)
                pts = sinCierre(pts);
            if (actual == GeometryKind::Point) {
                for (const QGeoCoordinate &p : pts)
                    geoms.append({ actual, { p } });
            } else {
                geoms.append({ actual, pts });
            }
        }
    }

    // Se agrupan las partes por tipo: lo normal es un solo tipo -> una entidad.
    for (GeometryKind k : { GeometryKind::Point, GeometryKind::Polyline, GeometryKind::Polygon }) {
        QVector<QVector<QGeoCoordinate>> partes;
        for (const auto &g : geoms)
            if (g.first == k)
                partes.append(g.second);
        if (!partes.isEmpty())
            anadir(out, base, k, partes);
    }
}

// Escribe un <Style> en linea con el aspecto de la entidad.
void escribirEstiloKml(QXmlStreamWriter &w, const MapFeature &f)
{
    w.writeStartElement(QStringLiteral("Style"));
    if (f.kind == GeometryKind::Point) {
        w.writeStartElement(QStringLiteral("IconStyle"));
        w.writeTextElement(QStringLiteral("color"), textoColorKml(f.style.lineColor));
        w.writeEndElement();
    }
    w.writeStartElement(QStringLiteral("LineStyle"));
    w.writeTextElement(QStringLiteral("color"), textoColorKml(f.style.lineColor));
    w.writeTextElement(QStringLiteral("width"), coord(f.style.lineWidth));
    w.writeEndElement();
    if (f.kind == GeometryKind::Polygon) {
        w.writeStartElement(QStringLiteral("PolyStyle"));
        w.writeTextElement(QStringLiteral("color"), textoColorKml(f.style.fillColor));
        w.writeEndElement();
    }
    w.writeEndElement();
}

// Texto de <coordinates> de una parte: "lon,lat lon,lat ...".
QString textoCoordenadasKml(const QVector<QGeoCoordinate> &parte)
{
    QStringList t;
    t.reserve(int(parte.size()));
    for (const QGeoCoordinate &c : parte)
        t.append(coord(c.longitude()) + QLatin1Char(',') + coord(c.latitude()));
    return t.join(QLatin1Char(' '));
}

// Escribe la geometria KML de una parte (Point, LineString o Polygon exterior).
void escribirParteKml(QXmlStreamWriter &w, GeometryKind k, const QVector<QGeoCoordinate> &parte)
{
    if (k == GeometryKind::Point) {
        w.writeStartElement(QStringLiteral("Point"));
        w.writeTextElement(QStringLiteral("coordinates"), textoCoordenadasKml(parte));
        w.writeEndElement();
    } else if (k == GeometryKind::Polyline) {
        w.writeStartElement(QStringLiteral("LineString"));
        w.writeTextElement(QStringLiteral("coordinates"), textoCoordenadasKml(parte));
        w.writeEndElement();
    } else {
        w.writeStartElement(QStringLiteral("Polygon"));
        w.writeStartElement(QStringLiteral("outerBoundaryIs"));
        w.writeStartElement(QStringLiteral("LinearRing"));
        w.writeTextElement(QStringLiteral("coordinates"), textoCoordenadasKml(conCierre(parte)));
        w.writeEndElement();
        w.writeEndElement();
        w.writeEndElement();
    }
}

// Escribe un <Data name=..><value>..</value></Data> de ExtendedData.
void escribirDatoKml(QXmlStreamWriter &w, const QString &nombre, const QString &valor)
{
    w.writeStartElement(QStringLiteral("Data"));
    w.writeAttribute(QStringLiteral("name"), nombre);
    w.writeTextElement(QStringLiteral("value"), valor);
    w.writeEndElement();
}

// ================================================================== GPX ===

// Lee los puntos de un <rte>/<trkseg> (el lector esta sobre su apertura) hasta
// su cierre: cada rtept/trkpt con lat/lon.
QVector<QGeoCoordinate> puntosGpx(QXmlStreamReader &xml, const QString &cierre,
                                  const QString &punto)
{
    QVector<QGeoCoordinate> pts;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == cierre)
            break;
        if (xml.isStartElement() && xml.name() == punto) {
            bool okLat = false, okLon = false;
            const double lat = xml.attributes().value(QLatin1String("lat")).toDouble(&okLat);
            const double lon = xml.attributes().value(QLatin1String("lon")).toDouble(&okLon);
            if (okLat && okLon)
                pts.append(QGeoCoordinate(lat, lon));
        }
    }
    return pts;
}

// Lee un <wpt>, <rte> o <trk> (el lector esta sobre su apertura) y anade su
// entidad: nombre (name), descripcion (desc), tipo de dominio (type) y, en un
// track, la marca libmapa:polygon que lo devuelve a poligono.
void leerElementoGpx(QXmlStreamReader &xml, QVector<MapFeature> &out)
{
    const QString raiz = xml.name().toString();
    MapFeature base;
    QVector<QVector<QGeoCoordinate>> partes;
    QVector<QGeoCoordinate> ruta;    // puntos de un <rte>
    bool poligono = false;

    if (raiz == QLatin1String("wpt")) {
        bool okLat = false, okLon = false;
        const double lat = xml.attributes().value(QLatin1String("lat")).toDouble(&okLat);
        const double lon = xml.attributes().value(QLatin1String("lon")).toDouble(&okLon);
        if (okLat && okLon)
            partes.append({ QGeoCoordinate(lat, lon) });
    }

    // nivel = profundidad respecto a la raiz: nombre, descripcion y tipo se
    // toman SOLO de los hijos directos (un <rtept> tambien puede llevar <name>,
    // y no debe pisar el de la ruta). readElementText y puntosGpx consumen el
    // cierre de su elemento, asi que ahi se baja el nivel a mano.
    int nivel = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement()) {
            if (nivel == 0 && xml.name() == raiz)
                break;
            --nivel;
            continue;
        }
        if (!xml.isStartElement())
            continue;
        ++nivel;
        const bool directo = nivel == 1;
        const auto n = xml.name();
        if (directo && n == QLatin1String("name")) {
            base.name = xml.readElementText().trimmed();
            --nivel;
        } else if (directo && n == QLatin1String("desc")) {
            base.description = xml.readElementText().trimmed();
            --nivel;
        } else if (directo && n == QLatin1String("type")) {
            base.type = xml.readElementText().trimmed();
            --nivel;
        } else if (directo && n == QLatin1String("ele") && raiz == QLatin1String("wpt")) {
            base.attributes.insert(QStringLiteral("ele"), xml.readElementText().toDouble());
            --nivel;
        } else if (n == QLatin1String("polygon")) {
            poligono = xml.readElementText().trimmed() == QLatin1String("true");
            --nivel;
        } else if (directo && n == QLatin1String("rtept") && raiz == QLatin1String("rte")) {
            bool okLat = false, okLon = false;
            const double lat = xml.attributes().value(QLatin1String("lat")).toDouble(&okLat);
            const double lon = xml.attributes().value(QLatin1String("lon")).toDouble(&okLon);
            if (okLat && okLon)
                ruta.append(QGeoCoordinate(lat, lon));
        } else if (directo && n == QLatin1String("trkseg")) {
            partes.append(puntosGpx(xml, QStringLiteral("trkseg"), QStringLiteral("trkpt")));
            --nivel;
        }
    }

    if (!ruta.isEmpty())
        partes.append(ruta);
    if (raiz == QLatin1String("wpt")) {
        anadir(out, base, GeometryKind::Point, partes);
    } else if (poligono) {
        for (auto &p : partes)
            p = sinCierre(p);
        anadir(out, base, GeometryKind::Polygon, partes);
    } else {
        anadir(out, base, GeometryKind::Polyline, partes);
    }
}

// Escribe nombre, descripcion y tipo GPX de una entidad (en el orden del esquema).
void escribirCabeceraGpx(QXmlStreamWriter &w, const MapFeature &f)
{
    if (!f.name.isEmpty())
        w.writeTextElement(QStringLiteral("name"), f.name);
    if (!f.description.isEmpty())
        w.writeTextElement(QStringLiteral("desc"), f.description);
    if (!f.type.isEmpty())
        w.writeTextElement(QStringLiteral("type"), f.type);
}

} // namespace

// ============================================================ API publica ===

// Formato por extension, sin distinguir mayusculas. ".json" se toma como
// GeoJSON porque es como lo exportan muchas herramientas.
VectorFormat vectorFormatForPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("geojson") || ext == QLatin1String("json"))
        return VectorFormat::GeoJson;
    if (ext == QLatin1String("kml"))
        return VectorFormat::Kml;
    if (ext == QLatin1String("gpx"))
        return VectorFormat::Gpx;
    return VectorFormat::Unknown;
}

// Abre el fichero, elige el lector por la extension y comprueba que haya
// salido al menos una entidad (un fichero sin geometrias validas es un error
// para quien lo importa: el mapa no cambiaria y no sabria por que).
QVector<MapFeature> readVectorFile(const QString &path, QString *error)
{
    const VectorFormat fmt = vectorFormatForPath(path);
    if (fmt == VectorFormat::Unknown) {
        if (error)
            *error = QStringLiteral("%1: formato no reconocido (usa .geojson, .kml o .gpx)").arg(path);
        return {};
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("No se pudo abrir %1: %2").arg(path, f.errorString());
        return {};
    }
    const QByteArray datos = f.readAll();
    QString motivo;
    QVector<MapFeature> out;
    switch (fmt) {
    case VectorFormat::GeoJson: out = parseGeoJson(datos, &motivo); break;
    case VectorFormat::Kml:     out = parseKml(datos, &motivo); break;
    case VectorFormat::Gpx:     out = parseGpx(datos, &motivo); break;
    case VectorFormat::Unknown: break;
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("%1: %2").arg(path,
            motivo.isEmpty() ? QStringLiteral("no hay geometrias validas") : motivo);
    return out;
}

// Serializa con el escritor del formato y guarda de forma atomica (QSaveFile):
// un fallo a medias no deja un fichero truncado encima del bueno.
bool writeVectorFile(const QString &path, const QVector<MapFeature> &features,
                     QString *error)
{
    QByteArray datos;
    switch (vectorFormatForPath(path)) {
    case VectorFormat::GeoJson: datos = toGeoJson(features); break;
    case VectorFormat::Kml:     datos = toKml(features); break;
    case VectorFormat::Gpx:     datos = toGpx(features); break;
    case VectorFormat::Unknown:
        if (error)
            *error = QStringLiteral("%1: formato no reconocido (usa .geojson, .kml o .gpx)").arg(path);
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(datos) != datos.size() || !f.commit()) {
        if (error)
            *error = QStringLiteral("No se pudo escribir %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

// GeoJSON: acepta FeatureCollection, un Feature suelto o una geometria suelta.
QVector<MapFeature> parseGeoJson(const QByteArray &data, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("GeoJSON no valido: %1").arg(pe.errorString());
        return {};
    }
    QVector<MapFeature> out;
    const QJsonObject raiz = doc.object();
    const QString t = raiz.value(QStringLiteral("type")).toString();
    QJsonArray feats;
    if (t == QLatin1String("FeatureCollection"))
        feats = raiz.value(QStringLiteral("features")).toArray();
    else if (t == QLatin1String("Feature"))
        feats.append(raiz);
    else
        geometriaGeoJson(raiz, MapFeature(), out);

    for (const QJsonValue &v : feats) {
        const QJsonObject o = v.toObject();
        geometriaGeoJson(o.value(QStringLiteral("geometry")).toObject(),
                         baseGeoJson(o.value(QStringLiteral("properties")).toObject()), out);
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("no hay geometrias validas");
    return out;
}

// KML: recorre el documento entero (carpetas incluidas, se aplanan). Los
// <Style id> se recuerdan para resolver los styleUrl de los Placemark que
// vienen despues, que es como los escriben Google Earth y QGIS.
QVector<MapFeature> parseKml(const QByteArray &data, QString *error)
{
    QXmlStreamReader xml(data);
    QHash<QString, EstiloKml> estilos;
    QVector<MapFeature> out;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("Style")) {
            const QString id = xml.attributes().value(QLatin1String("id")).toString();
            const EstiloKml e = leerEstiloKml(xml);
            if (!id.isEmpty())
                estilos.insert(id, e);
        } else if (xml.name() == QLatin1String("Placemark")) {
            leerPlacemark(xml, estilos, out);
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("KML no valido (linea %1): %2")
                         .arg(xml.lineNumber()).arg(xml.errorString());
        return {};
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("no hay geometrias validas");
    return out;
}

// GPX: puntos de paso, rutas y tracks, en el orden del fichero.
QVector<MapFeature> parseGpx(const QByteArray &data, QString *error)
{
    QXmlStreamReader xml(data);
    QVector<MapFeature> out;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()
            && (xml.name() == QLatin1String("wpt") || xml.name() == QLatin1String("rte")
                || xml.name() == QLatin1String("trk")))
            leerElementoGpx(xml, out);
    }
    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("GPX no valido (linea %1): %2")
                         .arg(xml.lineNumber()).arg(xml.errorString());
        return {};
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("no hay geometrias validas");
    return out;
}

// FeatureCollection con una Feature por entidad. Propiedades: nombre,
// descripcion, tipo de dominio, estilo "simplestyle" y los atributos (los que
// tengan equivalente JSON).
QByteArray toGeoJson(const QVector<MapFeature> &features)
{
    QJsonArray feats;
    for (const MapFeature &f : features) {
        if (!f.isValid())
            continue;
        QJsonObject props = QJsonObject::fromVariantMap(f.attributes);
        if (!f.name.isEmpty())
            props.insert(kName, f.name);
        if (!f.description.isEmpty())
            props.insert(kDescription, f.description);
        if (!f.type.isEmpty())
            props.insert(kType, f.type);
        const bool punto = f.kind == GeometryKind::Point;
        props.insert(punto ? kMarkerColor : kStroke, f.style.lineColor.name(QColor::HexRgb));
        if (!punto) {
            props.insert(kStrokeWidth, f.style.lineWidth);
            props.insert(kStrokeOpacity, opacidad(f.style.lineColor));
        }
        if (f.kind == GeometryKind::Polygon) {
            props.insert(kFill, f.style.fillColor.name(QColor::HexRgb));
            props.insert(kFillOpacity, opacidad(f.style.fillColor));
        }
        QJsonObject o;
        o.insert(QStringLiteral("type"), QStringLiteral("Feature"));
        o.insert(QStringLiteral("properties"), props);
        o.insert(QStringLiteral("geometry"), geometriaDe(f));
        feats.append(o);
    }
    QJsonObject raiz;
    raiz.insert(QStringLiteral("type"), QStringLiteral("FeatureCollection"));
    raiz.insert(QStringLiteral("features"), feats);
    return QJsonDocument(raiz).toJson(QJsonDocument::Compact);
}

// Documento KML con un Placemark por entidad: estilo en linea, tipo de dominio
// y atributos en ExtendedData, y MultiGeometry si tiene varias partes.
QByteArray toKml(const QVector<MapFeature> &features)
{
    QByteArray buf;
    QXmlStreamWriter w(&buf);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("kml"));
    w.writeDefaultNamespace(QStringLiteral("http://www.opengis.net/kml/2.2"));
    w.writeStartElement(QStringLiteral("Document"));
    for (const MapFeature &f : features) {
        if (!f.isValid())
            continue;
        w.writeStartElement(QStringLiteral("Placemark"));
        if (!f.name.isEmpty())
            w.writeTextElement(QStringLiteral("name"), f.name);
        if (!f.description.isEmpty())
            w.writeTextElement(QStringLiteral("description"), f.description);
        escribirEstiloKml(w, f);
        if (!f.type.isEmpty() || !f.attributes.isEmpty()) {
            w.writeStartElement(QStringLiteral("ExtendedData"));
            if (!f.type.isEmpty())
                escribirDatoKml(w, kType, f.type);
            for (auto it = f.attributes.cbegin(); it != f.attributes.cend(); ++it)
                escribirDatoKml(w, it.key(), it.value().toString());
            w.writeEndElement();
        }
        const QVector<QVector<QGeoCoordinate>> partes = f.outlines();
        if (partes.size() > 1)
            w.writeStartElement(QStringLiteral("MultiGeometry"));
        for (const auto &p : partes)
            escribirParteKml(w, f.kind, p);
        if (partes.size() > 1)
            w.writeEndElement();
        w.writeEndElement();   // Placemark
    }
    w.writeEndElement();       // Document
    w.writeEndElement();       // kml
    w.writeEndDocument();
    return buf;
}

// GPX 1.1: primero los puntos (wpt; uno por parte), despues los tracks (uno por
// linea o poligono, un trkseg por parte), que es el orden del esquema. Los
// poligonos van cerrados y con la marca libmapa:polygon.
QByteArray toGpx(const QVector<MapFeature> &features)
{
    QByteArray buf;
    QXmlStreamWriter w(&buf);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("gpx"));
    w.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
    w.writeNamespace(kNsLibmapa, QStringLiteral("libmapa"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("creator"), QStringLiteral("libmapa"));

    for (const MapFeature &f : features) {
        if (!f.isValid() || f.kind != GeometryKind::Point)
            continue;
        for (const auto &p : f.outlines()) {
            w.writeStartElement(QStringLiteral("wpt"));
            w.writeAttribute(QStringLiteral("lat"), coord(p.first().latitude()));
            w.writeAttribute(QStringLiteral("lon"), coord(p.first().longitude()));
            // La elevacion leida de un GPS (atributo "ele") vuelve a su sitio;
            // el esquema la pide antes que el nombre.
            if (f.attributes.contains(QStringLiteral("ele")))
                w.writeTextElement(QStringLiteral("ele"),
                                   coord(f.attributes.value(QStringLiteral("ele")).toDouble()));
            escribirCabeceraGpx(w, f);
            w.writeEndElement();
        }
    }
    for (const MapFeature &f : features) {
        if (!f.isValid() || f.kind == GeometryKind::Point)
            continue;
        const bool poligono = f.kind == GeometryKind::Polygon;
        w.writeStartElement(QStringLiteral("trk"));
        escribirCabeceraGpx(w, f);
        if (poligono) {
            w.writeStartElement(QStringLiteral("extensions"));
            w.writeTextElement(kNsLibmapa, QStringLiteral("polygon"), QStringLiteral("true"));
            w.writeEndElement();
        }
        for (const auto &p : f.outlines()) {
            w.writeStartElement(QStringLiteral("trkseg"));
            for (const QGeoCoordinate &c : poligono ? conCierre(p) : p) {
                w.writeStartElement(QStringLiteral("trkpt"));
                w.writeAttribute(QStringLiteral("lat"), coord(c.latitude()));
                w.writeAttribute(QStringLiteral("lon"), coord(c.longitude()));
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return buf;
}

} // namespace libmapa
