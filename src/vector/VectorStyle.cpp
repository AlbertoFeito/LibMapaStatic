#include "vector/VectorStyle.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace libmapa {

namespace {

/*
 * Estilo por defecto para el esquema OpenMapTiles (https://openmaptiles.org/schema/),
 * con colores parecidos a los de OSM Bright. Va en JSON, y no en C++, para que sea
 * a la vez el ejemplo del formato: `vector_tile --print-style` lo vuelca tal cual
 * para copiarlo y retocarlo. Orden: fondo natural, agua, edificios, vías (bordes
 * antes que relleno, de menor a mayor categoría), límites y, al final, rótulos.
 */
const char kOpenMapTilesStyle[] = R"JSON({
  "name": "OpenMapTiles (libmapa)",
  "background": "#f8f4f0",
  "sea": "#a5c9ea",
  "groups": [
    { "id": "nombres_calles", "title": "Nombres de calles" },
    { "id": "lugares", "title": "Nombres de lugares" },
    { "id": "edificios", "title": "Edificios" },
    { "id": "cimas", "title": "Cimas" },
    { "id": "limites", "title": "Límites administrativos" }
  ],
  "rules": [
    { "layer": "landcover", "filter": { "class": ["wood", "forest"] }, "fill": "#d6e6c3" },
    { "layer": "landcover", "filter": { "class": "grass" }, "fill": "#e2edd2" },
    { "layer": "landcover", "filter": { "class": "farmland" }, "fill": "#eef0d8" },
    { "layer": "landcover", "filter": { "class": "wetland" }, "fill": "#dbe8e0" },
    { "layer": "landcover", "filter": { "class": "sand" }, "fill": "#f4ecc4" },
    { "layer": "landcover", "filter": { "class": "rock" }, "fill": "#e2ded9" },
    { "layer": "landcover", "filter": { "class": "ice" }, "fill": "#ffffff" },
    { "layer": "landuse", "filter": { "class": ["residential", "suburb", "neighbourhood"] },
      "minZoom": 9, "fill": "#ece7e1" },
    { "layer": "landuse", "filter": { "class": ["commercial", "retail"] }, "minZoom": 11, "fill": "#f1e4e4" },
    { "layer": "landuse", "filter": { "class": ["industrial", "garages", "railway", "quarry"] },
      "minZoom": 11, "fill": "#ebe3ea" },
    { "layer": "landuse", "filter": { "class": ["cemetery"] }, "minZoom": 12, "fill": "#d9e5d0" },
    { "layer": "landuse", "filter": { "class": ["hospital", "school", "university", "college", "kindergarten"] },
      "minZoom": 12, "fill": "#f2eaf4" },
    { "layer": "landuse", "filter": { "class": ["military"] }, "minZoom": 10, "fill": "#f3e0dc" },
    { "layer": "landuse", "filter": { "class": ["stadium", "pitch", "playground", "track", "zoo", "theme_park"] },
      "minZoom": 13, "fill": "#dcebcf" },
    { "layer": "park", "fill": "#d4e7c2" },

    { "layer": "water", "fill": "#a5c9ea" },
    { "layer": "waterway", "filter": { "class": "river" }, "line": "#a5c9ea",
      "width": [[8, 0.6], [12, 1.5], [16, 5]] },
    { "layer": "waterway", "exclude": { "class": "river" }, "minZoom": 12, "line": "#a5c9ea",
      "width": [[12, 0.5], [16, 2]] },

    { "layer": "aeroway", "geometry": "polygon", "minZoom": 11, "fill": "#e9e6ee" },
    { "layer": "aeroway", "geometry": "line", "filter": { "class": "runway" }, "line": "#d8d4de",
      "width": [[11, 1.5], [13, 5], [16, 30]] },
    { "layer": "aeroway", "geometry": "line", "filter": { "class": "taxiway" }, "minZoom": 13,
      "line": "#d8d4de", "width": [[13, 0.8], [16, 6]] },

    { "layer": "building", "group": "edificios", "minZoom": 13, "fill": "#dfd9d2" },
    { "layer": "building", "group": "edificios", "minZoom": 15, "line": "#cdc5bc", "width": 0.6 },

    { "layer": "transportation", "filter": { "class": "ferry" }, "minZoom": 9, "line": "#7d9cc4",
      "dash": true, "width": 1 },
    { "layer": "transportation", "filter": { "class": ["path", "track"] }, "minZoom": 14,
      "line": "#a99a85", "dash": true, "width": [[14, 0.8], [16, 1.4]] },

    { "layer": "transportation", "filter": { "class": "service" }, "minZoom": 14, "line": "#cbc4bb",
      "width": [[14, 0.8], [16, 3]], "widthOffset": 1 },
    { "layer": "transportation", "filter": { "class": "minor" }, "minZoom": 13, "line": "#cbc4bb",
      "width": [[12, 0.6], [14, 2], [16, 6]], "widthOffset": 1.2 },
    { "layer": "transportation", "filter": { "class": "tertiary" }, "minZoom": 12, "line": "#c3bab0",
      "width": [[10, 0.6], [13, 2.4], [16, 8]], "widthOffset": 1.4 },
    { "layer": "transportation", "filter": { "class": "secondary" }, "minZoom": 11, "line": "#c8b88e",
      "width": [[8, 0.6], [11, 1.3], [13, 3], [16, 9]], "widthOffset": 1.4 },
    { "layer": "transportation", "filter": { "class": "primary" }, "minZoom": 10, "line": "#d3a76e",
      "width": [[6, 0.6], [10, 1.6], [13, 3.6], [16, 10]], "widthOffset": 1.6 },
    { "layer": "transportation", "filter": { "class": "trunk" }, "minZoom": 9, "line": "#d38a6e",
      "width": [[6, 0.6], [10, 1.6], [13, 3.6], [16, 10]], "widthOffset": 1.6 },
    { "layer": "transportation", "filter": { "class": "motorway" }, "minZoom": 8, "line": "#c86e7e",
      "width": [[5, 0.7], [10, 2], [13, 4], [16, 11]], "widthOffset": 1.6 },

    { "layer": "transportation", "filter": { "class": "service" }, "minZoom": 14, "line": "#ffffff",
      "width": [[14, 0.8], [16, 3]] },
    { "layer": "transportation", "filter": { "class": "minor" }, "minZoom": 12, "line": "#ffffff",
      "width": [[12, 0.6], [14, 2], [16, 6]] },
    { "layer": "transportation", "filter": { "class": "tertiary" }, "minZoom": 10, "line": "#ffffff",
      "width": [[10, 0.6], [13, 2.4], [16, 8]] },
    { "layer": "transportation", "filter": { "class": "secondary" }, "minZoom": 8, "line": "#f6f2b8",
      "width": [[8, 0.6], [11, 1.3], [13, 3], [16, 9]] },
    { "layer": "transportation", "filter": { "class": "primary" }, "minZoom": 6, "line": "#fbd6a0",
      "width": [[6, 0.6], [10, 1.6], [13, 3.6], [16, 10]] },
    { "layer": "transportation", "filter": { "class": "trunk" }, "minZoom": 5, "line": "#f9b89b",
      "width": [[6, 0.6], [10, 1.6], [13, 3.6], [16, 10]] },
    { "layer": "transportation", "filter": { "class": "motorway" }, "minZoom": 4, "line": "#e8919f",
      "width": [[5, 0.7], [10, 2], [13, 4], [16, 11]] },
    { "layer": "transportation", "filter": { "class": ["rail", "transit"] }, "minZoom": 10,
      "line": "#9a9a9a", "dash": true, "width": [[10, 0.6], [16, 1.6]] },

    { "layer": "boundary", "group": "limites", "filter": { "admin_level": ["5", "6", "7", "8"] }, "minZoom": 11,
      "exclude": { "maritime": ["1", "true"] }, "line": "#c4bccf", "dash": true, "width": 0.8 },
    { "layer": "boundary", "group": "limites", "filter": { "admin_level": ["3", "4"] }, "minZoom": 5,
      "exclude": { "maritime": ["1", "true"] }, "line": "#a597b6", "dash": true,
      "width": [[5, 0.6], [10, 1.2], [16, 1.8]] },
    { "layer": "boundary", "group": "limites", "filter": { "admin_level": "2" },
      "exclude": { "maritime": ["1", "true"] }, "line": "#8a7aa0", "dash": true,
      "width": [[3, 0.8], [10, 1.6], [16, 2.4]] },

    { "layer": "place", "group": "lugares", "filter": { "class": "country" }, "maxZoom": 6, "label": "name:es|name|name:latin",
      "textSize": 13, "bold": true, "textColor": "#4a4a4a" },
    { "layer": "place", "group": "lugares", "filter": { "class": "state" }, "minZoom": 5, "maxZoom": 9,
      "label": "name:es|name|name:latin", "textSize": 11, "textColor": "#7a6f8a" },
    { "layer": "place", "group": "lugares", "filter": { "class": "city" }, "minZoom": 5, "label": "name:es|name|name:latin",
      "textSize": 13, "bold": true, "pointRadius": 2.5, "pointColor": "#555555" },
    { "layer": "place", "group": "lugares", "filter": { "class": "town" }, "minZoom": 8, "label": "name:es|name|name:latin",
      "textSize": 12 },
    { "layer": "place", "group": "lugares", "filter": { "class": "village" }, "minZoom": 11, "label": "name:es|name|name:latin",
      "textSize": 11 },
    { "layer": "place", "group": "lugares", "filter": { "class": ["hamlet", "suburb", "neighbourhood", "quarter", "locality",
                                              "isolated_dwelling"] },
      "minZoom": 13, "label": "name:es|name|name:latin", "textSize": 10, "textColor": "#666666" },
    { "layer": "water_name", "group": "lugares", "label": "name:es|name|name:latin", "textColor": "#4f73a3",
      "italic": true, "textSize": 11 },
    { "layer": "aerodrome_label", "group": "lugares", "minZoom": 10, "label": "name:es|name|name:latin|iata",
      "textColor": "#6b5f85", "textSize": 10, "pointRadius": 2.5, "pointColor": "#8e7fb0" },
    { "layer": "transportation_name", "group": "nombres_calles",
      "filter": { "class": ["motorway", "trunk", "primary", "secondary"] }, "minZoom": 13,
      "label": "name:es|name|name:latin|ref", "textSize": 10.5, "textColor": "#4a4036" },
    { "layer": "transportation_name", "group": "nombres_calles",
      "filter": { "class": ["tertiary", "minor", "service"] }, "minZoom": 15,
      "label": "name:es|name|name:latin", "textSize": 10, "textColor": "#5a5047" },
    { "layer": "mountain_peak", "group": "cimas", "minZoom": 11, "label": "name:es|name|name:latin",
      "textSize": 10, "textColor": "#6b4f2f", "pointRadius": 2.5, "pointColor": "#8b6b45" }
  ]
})JSON";

/*
 * Estilo «híbrido»: para pintar el MBTiles vectorial ENCIMA de otra capa base
 * (satélite, relieve, OSM ráster). Fondo transparente, sin rellenos: solo vías
 * finas semitransparentes, límites y rótulos en blanco con halo oscuro, que se
 * leen sobre una foto. Los grupos con "visible": false arrancan apagados.
 */
const char kHybridStyle[] = R"JSON({
  "name": "Híbrido (libmapa)",
  "background": "#00000000",
  "groups": [
    { "id": "vias", "title": "Carreteras y calles" },
    { "id": "nombres_calles", "title": "Nombres de calles" },
    { "id": "lugares", "title": "Nombres de lugares" },
    { "id": "cimas", "title": "Cimas" },
    { "id": "limites", "title": "Límites administrativos" }
  ],
  "rules": [
    { "layer": "boundary", "group": "limites", "filter": { "admin_level": ["3", "4"] }, "minZoom": 5,
      "line": "#b0ffffff", "dash": true, "width": [[5, 0.8], [12, 1.4]] },
    { "layer": "boundary", "group": "limites", "filter": { "admin_level": "2" },
      "line": "#d0ffffff", "dash": true, "width": [[3, 1], [12, 2]] },

    { "layer": "transportation", "group": "vias", "filter": { "class": ["minor", "service"] }, "minZoom": 14,
      "line": "#70ffffff", "width": [[14, 0.6], [17, 2.2]] },
    { "layer": "transportation", "group": "vias", "filter": { "class": "tertiary" }, "minZoom": 12,
      "line": "#90ffffff", "width": [[12, 0.6], [17, 3]] },
    { "layer": "transportation", "group": "vias", "filter": { "class": "secondary" }, "minZoom": 9,
      "line": "#b0fff3a0", "width": [[9, 0.6], [17, 3.5]] },
    { "layer": "transportation", "group": "vias", "filter": { "class": ["primary", "trunk"] }, "minZoom": 7,
      "line": "#c0ffd27a", "width": [[7, 0.7], [17, 4]] },
    { "layer": "transportation", "group": "vias", "filter": { "class": "motorway" }, "minZoom": 5,
      "line": "#d0ffb04a", "width": [[5, 0.8], [17, 4.5]] },
    { "layer": "transportation", "group": "vias", "filter": { "class": ["rail", "transit"] }, "minZoom": 11,
      "line": "#a0e0e0e0", "dash": true, "width": 1 },

    { "layer": "place", "group": "lugares", "filter": { "class": "country" }, "maxZoom": 6,
      "label": "name:es|name|name:latin", "textSize": 13, "bold": true, "textColor": "#ffffff", "halo": "#d0000000" },
    { "layer": "place", "group": "lugares", "filter": { "class": "state" }, "minZoom": 5, "maxZoom": 9,
      "label": "name:es|name|name:latin", "textSize": 11, "textColor": "#f0f0f0", "halo": "#d0000000" },
    { "layer": "place", "group": "lugares", "filter": { "class": "city" }, "minZoom": 5,
      "label": "name:es|name|name:latin", "textSize": 13, "bold": true, "textColor": "#ffffff", "halo": "#d0000000",
      "pointRadius": 2.5, "pointColor": "#ffffff" },
    { "layer": "place", "group": "lugares", "filter": { "class": "town" }, "minZoom": 8,
      "label": "name:es|name|name:latin", "textSize": 12, "textColor": "#ffffff", "halo": "#d0000000" },
    { "layer": "place", "group": "lugares", "filter": { "class": "village" }, "minZoom": 11,
      "label": "name:es|name|name:latin", "textSize": 11, "textColor": "#ffffff", "halo": "#d0000000" },
    { "layer": "place", "group": "lugares",
      "filter": { "class": ["hamlet", "suburb", "neighbourhood", "quarter", "locality", "isolated_dwelling"] },
      "minZoom": 13, "label": "name:es|name|name:latin", "textSize": 10, "textColor": "#f0f0f0", "halo": "#d0000000" },
    { "layer": "water_name", "group": "lugares", "label": "name:es|name|name:latin", "italic": true,
      "textSize": 11, "textColor": "#d6ecff", "halo": "#c0002040" },
    { "layer": "aerodrome_label", "group": "lugares", "minZoom": 10, "label": "name:es|name|name:latin|iata",
      "textSize": 10, "textColor": "#ffffff", "halo": "#d0000000", "pointRadius": 2.5, "pointColor": "#ffffff" },
    { "layer": "transportation_name", "group": "nombres_calles",
      "filter": { "class": ["motorway", "trunk", "primary", "secondary"] }, "minZoom": 13,
      "label": "name:es|name|name:latin|ref", "textSize": 10.5, "textColor": "#ffffff", "halo": "#d0000000" },
    { "layer": "transportation_name", "group": "nombres_calles",
      "filter": { "class": ["tertiary", "minor", "service"] }, "minZoom": 15,
      "label": "name:es|name|name:latin", "textSize": 10, "textColor": "#ffffff", "halo": "#d0000000" },
    { "layer": "mountain_peak", "group": "cimas", "minZoom": 11, "label": "name:es|name|name:latin",
      "textSize": 10, "textColor": "#fff0d0", "halo": "#d0000000", "pointRadius": 2.5, "pointColor": "#fff0d0" }
  ]
})JSON";


// "#rrggbb", "#aarrggbb" o un nombre SVG. Inválido si no se reconoce.
QColor parseColor(const QJsonValue &v)
{
    if (!v.isString())
        return QColor();
    return QColor(v.toString());
}

// Un filtro {"clave": "valor"} o {"clave": ["v1", "v2"]} -> lista de pares. Los
// valores numéricos o booleanos se comparan como texto ("1", "true").
QVector<QPair<QString, QStringList>> parseFilter(const QJsonValue &v)
{
    QVector<QPair<QString, QStringList>> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        QStringList values;
        const QJsonArray arr = it.value().isArray() ? it.value().toArray()
                                                    : QJsonArray{ it.value() };
        for (const QJsonValue &x : arr)
            values << (x.isString() ? x.toString() : x.toVariant().toString());
        out.append(qMakePair(it.key(), values));
    }
    return out;
}

// "point" | "line" | "polygon" (o una lista) -> máscara de bits por GeomType.
int parseGeometry(const QJsonValue &v)
{
    const QJsonArray arr = v.isArray() ? v.toArray() : QJsonArray{ v };
    int mask = 0;
    for (const QJsonValue &x : arr) {
        const QString s = x.toString().toLower();
        if (s == QLatin1String("point"))
            mask |= 1 << int(mvt::GeomType::Point);
        else if (s == QLatin1String("line") || s == QLatin1String("linestring"))
            mask |= 1 << int(mvt::GeomType::LineString);
        else if (s == QLatin1String("polygon"))
            mask |= 1 << int(mvt::GeomType::Polygon);
    }
    return mask;
}

// "width": 1.5  o  [[zoom, px], …] -> paradas.
QVector<QPointF> parseWidth(const QJsonValue &v)
{
    QVector<QPointF> stops;
    if (v.isDouble()) {
        stops.append(QPointF(0, v.toDouble()));
    } else if (v.isArray()) {
        for (const QJsonValue &s : v.toArray()) {
            const QJsonArray p = s.toArray();
            if (p.size() == 2)
                stops.append(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
        }
    }
    return stops;
}

} // namespace

// Zoom dentro de [minZoom, maxZoom] y tipo admitido por la máscara.
bool VectorRule::appliesTo(int zoom, mvt::GeomType type) const
{
    if (zoom < minZoom || zoom > maxZoom)
        return false;
    return geometryMask == 0 || (geometryMask & (1 << int(type))) != 0;
}

// Interpolación lineal entre paradas; fuera del rango se mantiene la extrema.
double VectorRule::widthAt(int zoom) const
{
    double w = 1.0;
    if (width.size() == 1 || (!width.isEmpty() && zoom <= width.first().x())) {
        w = width.first().y();
    } else if (!width.isEmpty() && zoom >= width.last().x()) {
        w = width.last().y();
    } else {
        for (int i = 1; i < width.size(); ++i) {
            const QPointF a = width[i - 1], b = width[i];
            if (zoom <= b.x()) {
                const double t = b.x() > a.x() ? (zoom - a.x()) / (b.x() - a.x()) : 1.0;
                w = a.y() + t * (b.y() - a.y());
                break;
            }
        }
    }
    return w + widthOffset;
}

// El estilo por defecto sale de su JSON embebido; si alguien lo rompe al editarlo,
// el test de estilos lo detecta (aquí se devolvería un estilo vacío).
VectorStyle VectorStyle::openMapTiles()
{
    VectorStyle s;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(kOpenMapTilesStyle));
    fromJson(doc.object(), &s);
    return s;
}

// Lee nombre, fondo y reglas. Una regla sin "layer" o sin nada que dibujar se
// ignora; si no queda ninguna, el estilo no vale.
bool VectorStyle::fromJson(const QJsonObject &json, VectorStyle *out, QString *error)
{
    VectorStyle s;
    s.name = json.value(QStringLiteral("name")).toString();
    const QColor bg = parseColor(json.value(QStringLiteral("background")));
    if (bg.isValid())
        s.background = bg;
    const QColor sea = parseColor(json.value(QStringLiteral("sea")));
    if (sea.isValid())
        s.sea = sea;

    for (const QJsonValue &v : json.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject o = v.toObject();
        VectorRule r;
        r.layer = o.value(QStringLiteral("layer")).toString();
        r.group = o.value(QStringLiteral("group")).toString();
        r.filter = parseFilter(o.value(QStringLiteral("filter")));
        r.exclude = parseFilter(o.value(QStringLiteral("exclude")));
        r.minZoom = o.value(QStringLiteral("minZoom")).toInt(r.minZoom);
        r.maxZoom = o.value(QStringLiteral("maxZoom")).toInt(r.maxZoom);
        if (o.contains(QStringLiteral("geometry")))
            r.geometryMask = parseGeometry(o.value(QStringLiteral("geometry")));
        r.fill = parseColor(o.value(QStringLiteral("fill")));
        r.line = parseColor(o.value(QStringLiteral("line")));
        r.width = parseWidth(o.value(QStringLiteral("width")));
        r.widthOffset = o.value(QStringLiteral("widthOffset")).toDouble(0.0);
        r.dashed = o.value(QStringLiteral("dash")).toBool(false);
        const QString label = o.value(QStringLiteral("label")).toString();
        if (!label.isEmpty())
            r.labelFields = label.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        const QColor tc = parseColor(o.value(QStringLiteral("textColor")));
        if (tc.isValid())
            r.textColor = tc;
        const QColor halo = parseColor(o.value(QStringLiteral("halo")));
        if (halo.isValid())
            r.haloColor = halo;
        r.textSize = o.value(QStringLiteral("textSize")).toDouble(r.textSize);
        r.bold = o.value(QStringLiteral("bold")).toBool(false);
        r.italic = o.value(QStringLiteral("italic")).toBool(false);
        r.pointRadius = o.value(QStringLiteral("pointRadius")).toDouble(0.0);
        const QColor pc = parseColor(o.value(QStringLiteral("pointColor")));
        if (pc.isValid())
            r.pointColor = pc;

        if (r.layer.isEmpty() || (!r.fill.isValid() && !r.line.isValid() && r.labelFields.isEmpty()))
            continue;
        s.rules.append(r);
    }

    // Grupos: primero los declarados (con su título), luego los que solo salen en
    // alguna regla.
    for (const QJsonValue &g : json.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject go = g.toObject();
        const QString id = go.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !s.groupIds().contains(id)) {
            s.groups.append(qMakePair(id, go.value(QStringLiteral("title")).toString(id)));
            if (!go.value(QStringLiteral("visible")).toBool(true))
                s.defaultHidden << id;
        }
    }
    for (const VectorRule &r : s.rules)
        if (!r.group.isEmpty() && !s.groupIds().contains(r.group))
            s.groups.append(qMakePair(r.group, r.group));

    if (s.rules.isEmpty()) {
        if (error) *error = QStringLiteral("el estilo no tiene reglas válidas");
        return false;
    }
    *out = s;
    return true;
}

// Abre el fichero, lo interpreta como JSON y delega en fromJson.
bool VectorStyle::fromFile(const QString &path, VectorStyle *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("No se pudo abrir el estilo %1").arg(path);
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) {
        if (error) *error = QStringLiteral("JSON inválido en %1: %2").arg(path, pe.errorString());
        return false;
    }
    QString motivo;
    if (!fromJson(doc.object(), out, &motivo)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, motivo);
        return false;
    }
    return true;
}

// Ids de los grupos, en el orden en que se ofrecen.
QStringList VectorStyle::groupIds() const
{
    QStringList ids;
    for (const auto &g : groups)
        ids << g.first;
    return ids;
}

// El estilo híbrido, de su JSON embebido (como openMapTiles).
VectorStyle VectorStyle::hybrid()
{
    VectorStyle s;
    fromJson(QJsonDocument::fromJson(QByteArray(kHybridStyle)).object(), &s);
    return s;
}

// Texto del estilo híbrido, para usarlo de plantilla.
QByteArray VectorStyle::hybridJson()
{
    return QByteArray(kHybridStyle);
}

// Estilo por defecto según el uso: híbrido para un overlay, OpenMapTiles para una base.
VectorStyle VectorStyle::defaultFor(bool overlay)
{
    return overlay ? hybrid() : openMapTiles();
}

// Texto del estilo por defecto, para volcarlo y usarlo de plantilla.
QByteArray VectorStyle::openMapTilesJson()
{
    return QByteArray(kOpenMapTilesStyle);
}

} // namespace libmapa
