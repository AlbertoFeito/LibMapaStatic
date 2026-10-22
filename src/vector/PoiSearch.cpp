#include "vector/PoiSearch.h"

#include "io/PlaceIndex.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace libmapa {
namespace poisearch {

const char *const kLayer = "poi";

namespace {

const double kPi = 3.14159265358979323846;

// Nombres en español de las clases (y, donde sirve, subclases) de OpenMapTiles que
// se ven en un mapa; lo que no esté se muestra con su nombre tal cual. Cada entrada
// lleva también palabras de uso común que no son la traducción literal, separadas
// por espacios: lo que alguien escribiría para buscarlo.
struct Tipo
{
    const char *clave;      // "class" o "subclass" tal cual viene en el MBTiles
    const char *texto;      // nombre en español
    const char *sinonimos;  // palabras de búsqueda extra, sin tildes
};

const Tipo kTipos[] = {
    // Clases.
    { "hospital", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "hospital"), "policlinico" },
    { "doctors", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "consultorio médico"), "medico doctor consultorio" },
    { "clinic", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "clínica"), "clinica salud" },
    { "pharmacy", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "farmacia"), "botica medicinas" },
    { "dentist", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "dentista"), "estomatologia odontologo" },
    { "veterinary", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "veterinario"), "veterinaria" },
    { "police", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "policía"), "comisaria unidad pnr" },
    { "fire_station", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "bomberos"), "parque de bomberos" },
    { "fuel", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "gasolinera"), "combustible gasolina cupet servicentro" },
    { "charging_station", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "cargador eléctrico"), "electrolinera carga" },
    { "lodging", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "alojamiento"), "hospedaje hotel hostal" },
    { "campsite", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "camping"), "campismo acampar" },
    { "bus", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "parada de autobús"), "guagua omnibus paradero" },
    { "railway", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "estación de tren"), "ferrocarril tren apeadero" },
    { "ferry_terminal", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "terminal de ferris"), "ferry lancha muelle" },
    { "harbor", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "puerto"), "marina muelle embarcadero" },
    { "bank", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "banco"), "sucursal" },
    { "atm", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "cajero automático"), "cajero" },
    { "school", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "escuela"), "colegio primaria secundaria" },
    { "college", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "universidad"), "instituto facultad" },
    { "kindergarten", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "círculo infantil"), "guarderia" },
    { "library", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "biblioteca"), "libros" },
    { "restaurant", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "restaurante"), "paladar comer" },
    { "cafe", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "cafetería"), "cafeteria cafe" },
    { "bar", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "bar"), "cantina" },
    { "fast_food", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "comida rápida"), "comida rapida" },
    { "place_of_worship", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "lugar de culto"), "iglesia templo catedral" },
    { "theatre", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "teatro"), "" },
    { "cinema", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "cine"), "" },
    { "stadium", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "estadio"), "deporte" },
    { "museum", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "museo"), "" },
    { "art_gallery", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "galería de arte"), "galeria" },
    { "post", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "correos"), "oficina postal correo" },
    { "shop", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "tienda"), "comercio" },
    { "grocery", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "mercado"), "bodega agro supermercado alimentos" },
    { "park", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "parque"), "jardin" },
    { "playground", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "parque infantil"), "juegos" },
    { "swimming", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "piscina"), "natacion" },
    { "parking", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "aparcamiento"), "parqueo estacionamiento" },
    { "toilets", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "aseos"), "bano baño sanitario" },
    // Subclases más específicas que su clase.
    { "hotel", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "hotel"), "alojamiento" },
    { "hostel", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "hostal"), "alojamiento albergue" },
    { "motel", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "motel"), "alojamiento" },
    { "guest_house", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "casa de huéspedes"), "alojamiento casa particular" },
    { "bakery", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "panadería"), "panaderia pan" },
    { "supermarket", QT_TRANSLATE_NOOP("libmapa::PointOfInterest", "supermercado"), "tienda" },
};

// Fila de la tabla para una clave, o null.
const Tipo *buscarTipo(const QString &clave)
{
    static const QHash<QString, const Tipo *> indice = [] {
        QHash<QString, const Tipo *> h;
        for (const Tipo &t : kTipos)
            h.insert(QLatin1String(t.clave), &t);
        return h;
    }();
    return indice.value(clave, nullptr);
}

// Texto en el que se busca el TIPO de un POI: su nombre en español, el tipo tal
// cual viene (en inglés) y las palabras de uso común, todo normalizado.
QString textoDeTipo(const PointOfInterest &p)
{
    QString t = PlaceIndex::normalize(p.type) + QLatin1Char(' ') + PlaceIndex::normalize(p.subtype);
    for (const QString &clave : { p.subtype, p.type }) {
        if (const Tipo *tipo = buscarTipo(clave))
            t += QLatin1Char(' ') + PlaceIndex::normalize(QCoreApplication::translate("libmapa::PointOfInterest", tipo->texto))
                 + QLatin1Char(' ') + PlaceIndex::normalize(QString::fromUtf8(tipo->sinonimos));
    }
    return t;
}

} // namespace

// Las entidades punto de la capa `poi`, pasadas de coordenadas de tesela a geográficas.
QVector<PointOfInterest> inTile(const mvt::Tile &tile, const TileKey &key)
{
    QVector<PointOfInterest> out;
    const mvt::Layer *layer = tile.layer(QLatin1String(kLayer));
    if (!layer)
        return out;
    const int kName = layer->keyIndex(QStringLiteral("name"));
    const int kNameEs = layer->keyIndex(QStringLiteral("name:es"));
    const int kNameLatin = layer->keyIndex(QStringLiteral("name:latin"));
    const int kClass = layer->keyIndex(QStringLiteral("class"));
    const int kSub = layer->keyIndex(QStringLiteral("subclass"));
    const double n = double(1 << key.z);
    const double ext = double(layer->extent);

    for (const mvt::Feature &f : layer->features) {
        if (f.type != mvt::GeomType::Point)
            continue;
        QString name = layer->value(f, kNameEs).toString();
        if (name.isEmpty())
            name = layer->value(f, kName).toString();
        if (name.isEmpty())
            name = layer->value(f, kNameLatin).toString();
        for (const QVector<QPoint> &part : f.parts)
            for (const QPoint &pt : part) {
                // Los del margen de la tesela son de la vecina, que los trae ella.
                if (pt.x() < 0 || pt.y() < 0 || pt.x() >= layer->extent || pt.y() >= layer->extent)
                    continue;
                PointOfInterest p;
                p.name = name;
                p.type = layer->value(f, kClass).toString();
                p.subtype = layer->value(f, kSub).toString();
                const double lon = (key.x + pt.x() / ext) / n * 360.0 - 180.0;
                const double lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * (key.y + pt.y() / ext) / n))) * 180.0 / kPi;
                p.coordinate = QGeoCoordinate(lat, lon);
                out.append(p);
            }
    }
    return out;
}

// Filtra por palabras (nombre o tipo), ordena por relevancia y luego por cercanía.
QVector<PointOfInterest> search(const QVector<PointOfInterest> &pois, const QString &text,
                                const QGeoCoordinate &from, int limit)
{
    const QString norm = PlaceIndex::normalize(text);
    if (norm.size() < 2 || limit <= 0)
        return {};
    const QStringList palabras = norm.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    struct Candidato
    {
        PointOfInterest poi;
        int grupo;      // 0 empieza por lo escrito, 1 lo contiene en el nombre, 2 solo por tipo
    };
    QVector<Candidato> cands;
    for (const PointOfInterest &p : pois) {
        const QString nombre = PlaceIndex::normalize(p.name);
        QString tipo;                       // se calcula solo si hace falta
        bool todasEnNombre = !nombre.isEmpty();
        bool todas = true;
        for (const QString &w : palabras) {
            const bool enNombre = !nombre.isEmpty() && nombre.contains(w);
            todasEnNombre = todasEnNombre && enNombre;
            if (enNombre)
                continue;
            if (tipo.isEmpty())
                tipo = textoDeTipo(p);
            if (!tipo.contains(w)) {
                todas = false;
                break;
            }
        }
        if (!todas)
            continue;
        const int grupo = todasEnNombre ? (nombre.startsWith(palabras.first()) ? 0 : 1) : 2;
        PointOfInterest r = p;
        r.distanceM = from.isValid() ? from.distanceTo(p.coordinate) : 0.0;
        cands.append({ r, grupo });
    }

    std::stable_sort(cands.begin(), cands.end(), [](const Candidato &a, const Candidato &b) {
        if (a.grupo != b.grupo)
            return a.grupo < b.grupo;
        return a.poi.distanceM < b.poi.distanceM;
    });
    QVector<PointOfInterest> out;
    for (int i = 0; i < cands.size() && i < limit; ++i)
        out.append(cands[i].poi);
    return out;
}

} // namespace poisearch

// Tipo en español: primero por la subclase (hotel, panadería), luego por la clase.
QString PointOfInterest::typeLabel() const
{
    for (const QString &clave : { subtype, type })
        if (const poisearch::Tipo *t = poisearch::buscarTipo(clave))
            return QCoreApplication::translate("libmapa::PointOfInterest", t->texto);
    return type;
}

// El nombre; sin él, el tipo con la primera letra en mayúscula.
QString PointOfInterest::displayName() const
{
    if (!name.isEmpty())
        return name;
    const QString t = typeLabel();
    return t.isEmpty() ? QCoreApplication::translate("libmapa::PointOfInterest", "Sin nombre")
                       : t.left(1).toUpper() + t.mid(1);
}

} // namespace libmapa
