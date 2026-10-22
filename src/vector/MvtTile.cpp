#include "vector/MvtTile.h"

#include "vector/Gzip.h"

#include <cstring>

namespace libmapa {
namespace mvt {

namespace {

// Tipos de cable del protobuf que aparecen en MVT.
enum WireType : quint32 {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    Fixed32 = 5
};

/*!
 * Lector mínimo de protobuf sobre un bloque de memoria. No reserva nada: los
 * sub-mensajes se leen con otro lector sobre el mismo buffer. Cualquier lectura
 * fuera del bloque deja \c ok a false y a partir de ahí todo devuelve 0, así que
 * un dato corrupto nunca lee memoria ajena.
 */
class PbfReader
{
public:
    // Lector sobre [begin, end): no copia nada.
    PbfReader(const char *begin, const char *end) : m_p(begin), m_end(end) {}

    // ¿Todo lo leído hasta ahora estaba dentro del bloque y bien formado?
    bool ok() const { return m_ok; }
    // Fin del bloque (o error: a partir de un fallo no se lee nada más).
    bool atEnd() const { return !m_ok || m_p >= m_end; }

    // Lee la siguiente cabecera de campo (número y tipo). false al acabar o si falla.
    bool next(quint32 *field, quint32 *wire)
    {
        if (atEnd())
            return false;
        const quint64 key = varint();
        *field = quint32(key >> 3);
        *wire = quint32(key & 7u);
        if (*field == 0)       // el campo 0 no existe en protobuf: dato corrupto
            m_ok = false;
        return m_ok;
    }

    // Varint de hasta 10 bytes (base 128, el bit alto marca «sigue»).
    quint64 varint()
    {
        quint64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_p >= m_end) {
                m_ok = false;
                return 0;
            }
            const quint8 b = quint8(*m_p++);
            v |= quint64(b & 0x7fu) << shift;
            if (!(b & 0x80u))
                return v;
        }
        m_ok = false;          // más de 10 bytes: no es un varint válido
        return 0;
    }

    // Bloque con longitud (cadena, sub-mensaje o campo empaquetado).
    PbfReader block()
    {
        const quint64 len = varint();
        if (!m_ok || len > quint64(m_end - m_p)) {
            m_ok = false;
            return PbfReader(m_end, m_end);
        }
        const char *b = m_p;
        m_p += len;
        return PbfReader(b, m_p);
    }

    // Cadena UTF-8 (claves, valores de texto, nombre de capa).
    QString string()
    {
        const PbfReader b = block();
        return QString::fromUtf8(b.m_p, int(b.m_end - b.m_p));
    }

    // Entero de 32 bits en little-endian (el float de Value).
    quint32 fixed32()
    {
        if (m_end - m_p < 4) {
            m_ok = false;
            return 0;
        }
        quint32 v = 0;
        for (int i = 3; i >= 0; --i)
            v = (v << 8) | quint8(m_p[i]);      // little-endian
        m_p += 4;
        return v;
    }

    // Entero de 64 bits en little-endian (el double de Value).
    quint64 fixed64()
    {
        if (m_end - m_p < 8) {
            m_ok = false;
            return 0;
        }
        quint64 v = 0;
        for (int i = 7; i >= 0; --i)
            v = (v << 8) | quint8(m_p[i]);
        m_p += 8;
        return v;
    }

    // Salta un campo que no interesa, según su tipo de cable.
    void skip(quint32 wire)
    {
        switch (wire) {
        case Varint: varint(); break;
        case Fixed64: fixed64(); break;
        case LengthDelimited: block(); break;
        case Fixed32: fixed32(); break;
        default: m_ok = false; break;     // grupos (3/4) y tipos desconocidos
        }
    }

    // Enteros sin signo de 32 bits de un campo `repeated uint32`: empaquetados
    // (lo normal) o sueltos, que el protobuf también permite.
    void appendUInt32s(quint32 wire, QVector<quint32> *out)
    {
        if (wire == LengthDelimited) {
            PbfReader b = block();
            while (!b.atEnd())
                out->append(quint32(b.varint()));
            if (!b.ok())
                m_ok = false;
        } else if (wire == Varint) {
            out->append(quint32(varint()));
        } else {
            m_ok = false;
        }
    }

private:
    const char *m_p;
    const char *m_end;
    bool m_ok = true;
};

// Decodificación zigzag (sint32/sint64): 0,1,2,3… -> 0,-1,1,-2…
qint64 zigzag(quint64 v)
{
    return qint64(v >> 1) ^ -qint64(v & 1u);
}

// Un mensaje Value: exactamente uno de sus siete campos (§4.1).
QVariant readValue(PbfReader r)
{
    QVariant v;
    quint32 field = 0, wire = 0;
    while (r.next(&field, &wire)) {
        switch (field) {
        case 1: v = r.string(); break;
        case 2: {
            const quint32 bits = r.fixed32();
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof f);
            v = double(f);
            break;
        }
        case 3: {
            const quint64 bits = r.fixed64();
            double d = 0.0;
            std::memcpy(&d, &bits, sizeof d);
            v = d;
            break;
        }
        case 4: v = qint64(r.varint()); break;            // int64: complemento a dos
        case 5: v = quint64(r.varint()); break;
        case 6: v = zigzag(r.varint()); break;
        case 7: v = r.varint() != 0; break;
        default: r.skip(wire); break;
        }
    }
    return v;
}

// Un mensaje Feature: id, tags, tipo y comandos de geometría.
bool readFeature(PbfReader r, Feature *f)
{
    QVector<quint32> geometry;
    quint32 field = 0, wire = 0;
    while (r.next(&field, &wire)) {
        switch (field) {
        case 1: f->id = r.varint(); f->hasId = true; break;
        case 2: r.appendUInt32s(wire, &f->tags); break;
        case 3: {
            const quint64 t = r.varint();
            f->type = t <= 3 ? GeomType(int(t)) : GeomType::Unknown;
            break;
        }
        case 4: r.appendUInt32s(wire, &geometry); break;
        default: r.skip(wire); break;
        }
    }
    if (!r.ok())
        return false;
    // Una geometría incoherente no invalida la tesela entera: la entidad se
    // queda sin piezas y el dibujo la ignora.
    if (!decodeGeometry(geometry, f->type, &f->parts))
        f->parts.clear();
    return true;
}

// Un mensaje Layer completo.
bool readLayer(PbfReader r, Layer *layer)
{
    quint32 field = 0, wire = 0;
    while (r.next(&field, &wire)) {
        switch (field) {
        case 15: layer->version = int(r.varint()); break;
        case 1: layer->name = r.string(); break;
        case 2: {
            Feature f;
            if (!readFeature(r.block(), &f))
                return false;
            layer->features.append(std::move(f));
            break;
        }
        case 3: layer->keys.append(r.string()); break;
        case 4: layer->values.append(readValue(r.block())); break;
        case 5: layer->extent = int(r.varint()); break;
        default: r.skip(wire); break;
        }
    }
    if (layer->extent <= 0)
        layer->extent = 4096;
    return r.ok();
}

// Nombre de una capa (campo 1) sin leer sus entidades: sirve para saltarse las que no
// interesan. Cadena vacía si el bloque no trae nombre.
QString peekLayerName(PbfReader r)
{
    quint32 field = 0, wire = 0;
    while (r.next(&field, &wire)) {
        if (field == 1)
            return r.string();
        r.skip(wire);
    }
    return QString();
}

} // namespace

// Busca el par clave/valor cuya clave es \a keyIdx entre los tags de la entidad.
QVariant Layer::value(const Feature &f, int keyIdx) const
{
    if (keyIdx < 0)
        return QVariant();
    const quint32 k = quint32(keyIdx);
    for (int i = 0; i + 1 < f.tags.size(); i += 2) {
        if (f.tags[i] == k) {
            const quint32 vi = f.tags[i + 1];
            return vi < quint32(values.size()) ? values[int(vi)] : QVariant();
        }
    }
    return QVariant();
}

// Búsqueda lineal: una tesela trae una quincena de capas como mucho.
const Layer *Tile::layer(const QString &name) const
{
    for (const Layer &l : layers)
        if (l.name == name)
            return &l;
    return nullptr;
}

// Recorre los comandos (§4.3.1): cada entero de comando lleva el id en los 3 bits
// bajos y la cuenta en el resto, y le siguen cuenta·2 parámetros en zigzag que
// son desplazamientos relativos al cursor. El cursor se arrastra entre piezas.
bool decodeGeometry(const QVector<quint32> &commands, GeomType type,
                    QVector<QVector<QPoint>> *parts)
{
    parts->clear();
    qint64 x = 0, y = 0;
    QVector<QPoint> current;
    int i = 0;
    const int n = int(commands.size());

    auto flush = [&]() {
        if (!current.isEmpty()) {
            parts->append(current);
            current.clear();
        }
    };

    while (i < n) {
        const quint32 cmd = commands[i++];
        const quint32 id = cmd & 7u;
        const quint32 count = cmd >> 3;
        if (id == 7) {                                    // ClosePath
            if (count != 1 || type != GeomType::Polygon || current.size() < 3)
                return false;
            flush();
            continue;
        }
        if (id != 1 && id != 2)
            return false;
        if (count == 0 || quint64(count) * 2 > quint64(n - i))
            return false;
        if (id == 1 && type != GeomType::Point) {          // MoveTo: empieza pieza
            if (count != 1)
                return false;
            flush();
        }
        if (id == 2 && current.isEmpty())                  // LineTo sin MoveTo
            return false;
        for (quint32 k = 0; k < count; ++k) {
            x += zigzag(commands[i++]);
            y += zigzag(commands[i++]);
            // Las coordenadas válidas son pequeñas (extent + margen); se acotan
            // por si un dato corrupto se sale de lo representable.
            const qint64 lim = qint64(1) << 24;
            current.append(QPoint(int(qBound(-lim, x, lim)), int(qBound(-lim, y, lim))));
        }
    }

    if (type == GeomType::Polygon) {
        if (!current.isEmpty())                            // anillo sin ClosePath
            return false;
    } else {
        flush();
    }
    if (type == GeomType::LineString)
        for (const QVector<QPoint> &p : *parts)
            if (p.size() < 2)
                return false;
    return true;
}

// Descomprime si hace falta y lee las capas (campo 3 del mensaje Tile).
bool decode(const QByteArray &bytes, Tile *out, QString *error)
{
    return decode(bytes, out, error, QStringList());
}

// Igual, saltando las capas que no estén en \a onlyLayers (vacío = todas).
bool decode(const QByteArray &bytes, Tile *out, QString *error, const QStringList &onlyLayers)
{
    out->layers.clear();
    bool okInflate = false;
    const QByteArray raw = Gzip::inflateIfCompressed(bytes, &okInflate);
    if (!okInflate) {
        if (error) *error = QStringLiteral("tesela comprimida corrupta o truncada");
        return false;
    }

    PbfReader r(raw.constData(), raw.constData() + raw.size());
    quint32 field = 0, wire = 0;
    while (r.next(&field, &wire)) {
        if (field == 3 && wire == LengthDelimited) {
            const PbfReader block = r.block();
            if (!onlyLayers.isEmpty() && !onlyLayers.contains(peekLayerName(block)))
                continue;
            Layer layer;
            if (!readLayer(block, &layer)) {
                if (error) *error = QStringLiteral("capa MVT mal formada");
                return false;
            }
            out->layers.append(std::move(layer));
        } else {
            r.skip(wire);
        }
    }
    if (!r.ok()) {
        if (error) *error = QStringLiteral("no es una tesela MVT (protobuf mal formado)");
        return false;
    }
    return true;
}

} // namespace mvt
} // namespace libmapa
