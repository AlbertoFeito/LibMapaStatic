#include "dem/GeoTiff.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace libmapa {

namespace {

// Radio de la esfera de Web Mercator (EPSG:3857), en metros.
const double kMercatorR = 6378137.0;
const double kPi = 3.14159265358979323846;

// Etiquetas TIFF / GeoTIFF que se usan.
enum Tag : quint16 {
    TagWidth = 256,
    TagLength = 257,
    TagBitsPerSample = 258,
    TagCompression = 259,
    TagPhotometric = 262,
    TagStripOffsets = 273,
    TagSamplesPerPixel = 277,
    TagRowsPerStrip = 278,
    TagStripByteCounts = 279,
    TagPlanarConfig = 284,
    TagPredictor = 317,
    TagTileWidth = 322,
    TagTileLength = 323,
    TagTileOffsets = 324,
    TagTileByteCounts = 325,
    TagSampleFormat = 339,
    TagPixelScale = 33550,
    TagTiepoint = 33922,
    TagTransformation = 34264,
    TagGeoKeys = 34735,
    TagGdalNoData = 42113
};

// Tipos de dato de una entrada del directorio TIFF.
enum Type : quint16 {
    TByte = 1, TAscii = 2, TShort = 3, TLong = 4, TRational = 5, TSByte = 6,
    TUndefined = 7, TSShort = 8, TSLong = 9, TSRational = 10, TFloat = 11,
    TDouble = 12, TLong8 = 16, TSLong8 = 17, TIfd8 = 18
};

// Bytes que ocupa un valor de cada tipo (0 si no se conoce).
int typeSize(quint16 type)
{
    switch (type) {
    case TByte: case TAscii: case TSByte: case TUndefined: return 1;
    case TShort: case TSShort: return 2;
    case TLong: case TSLong: case TFloat: return 4;
    case TRational: case TSRational: case TDouble:
    case TLong8: case TSLong8: case TIfd8: return 8;
    default: return 0;
    }
}

// Una entrada del directorio de la imagen: etiqueta, tipo, cuántos valores y el
// campo de 4 u 8 bytes que guarda el valor o su posición en el fichero.
struct Entry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint64 count = 0;
    QByteArray field;
};

// Datos de un bloque descodificado (franja o tesela), listos para consultar.
struct Block
{
    QVector<float> v;
    bool ok = false;
};

// Parámetros de la cuadrícula de la imagen que necesita el descodificador.
struct Layout
{
    int width = 0, height = 0;
    int spp = 1;               // muestras por píxel
    int bps = 8;               // bits por muestra
    int sampleFormat = 1;      // 1 entero sin signo, 2 con signo, 3 coma flotante
    int compression = 1;
    int predictor = 1;
    bool planar = false;       // true: cada banda en sus propios bloques
    bool tiled = false;
    int tileW = 0, tileH = 0;  // tesela, o (ancho, filas por franja) si va por franjas
};

// Descomprime PackBits (RLE de TIFF). false si el flujo está truncado.
bool unpackBits(const QByteArray &in, QByteArray &out, int expected)
{
    out.clear();
    out.reserve(expected);
    int i = 0;
    const int n = int(in.size());
    while (i < n && out.size() < expected) {
        const int c = int(qint8(in[i++]));
        if (c >= 0) {
            const int len = c + 1;
            if (i + len > n)
                return false;
            out.append(in.constData() + i, len);
            i += len;
        } else if (c != -128) {
            if (i >= n)
                return false;
            out.append(-c + 1, in[i++]);
        }
    }
    return out.size() >= expected;
}

// Descomprime LZW de TIFF (códigos de 9 a 12 bits, MSB primero, con el cambio de
// anchura «anticipado» de la especificación TIFF 6). false si el flujo es
// inválido o termina antes de dar \a expected bytes.
bool lzwDecode(const QByteArray &in, QByteArray &out, int expected)
{
    out.clear();
    out.reserve(expected);

    const uchar *src = reinterpret_cast<const uchar *>(in.constData());
    const qint64 totalBits = qint64(in.size()) * 8;
    qint64 bitPos = 0;

    int prefix[4096];
    uchar suffix[4096];
    int nextCode = 258;
    int bits = 9;
    int prev = -1;
    QByteArray str;

    while (out.size() < expected) {
        if (bitPos + bits > totalBits)
            break;
        int code = 0;
        for (int i = 0; i < bits; ++i) {
            const qint64 p = bitPos + i;
            code = (code << 1) | ((src[p >> 3] >> (7 - (p & 7))) & 1);
        }
        bitPos += bits;

        if (code == 257)                    // fin de datos
            break;
        if (code == 256) {                  // reinicio de la tabla
            nextCode = 258;
            bits = 9;
            prev = -1;
            continue;
        }
        if (prev < 0) {                     // primer código tras un reinicio
            if (code > 255)
                return false;
            out.append(char(code));
            prev = code;
            continue;
        }

        // Cadena de \a code (o, si aún no existe, la de prev + su primer byte).
        str.clear();
        int cur = code < nextCode ? code : prev;
        if (code > nextCode)
            return false;
        while (cur >= 258) {
            str.append(char(suffix[cur]));
            cur = prefix[cur];
        }
        str.append(char(cur));
        std::reverse(str.begin(), str.end());
        if (code == nextCode)
            str.append(str[0]);

        out.append(str);
        if (nextCode < 4096) {
            prefix[nextCode] = prev;
            suffix[nextCode] = uchar(str[0]);
            ++nextCode;
        }
        prev = code;
        if (nextCode + 1 >= (1 << bits) && bits < 12)
            ++bits;
    }
    return out.size() >= expected;
}

// Descomprime un flujo zlib (Deflate de TIFF). Qt exige delante el tamaño
// esperado en 4 bytes big-endian: se le antepone.
bool inflateZlib(const QByteArray &in, QByteArray &out, int expected)
{
    QByteArray framed;
    framed.resize(4);
    qToBigEndian<quint32>(quint32(expected), reinterpret_cast<uchar *>(framed.data()));
    framed.append(in);
    out = qUncompress(framed);
    return out.size() >= expected;
}

// Deshace el predictor 2 (diferencias horizontales) en datos YA en el orden de
// bytes de la máquina: cada muestra se suma a la anterior de su misma banda,
// con desbordamiento del ancho de la muestra.
void undoHorizontalPredictor(QByteArray &data, int rowSamples, int stride, int bps)
{
    const int rows = bps >= 8 ? int(data.size() / (qint64(rowSamples) * (bps / 8))) : 0;
    for (int r = 0; r < rows; ++r) {
        if (bps == 8) {
            quint8 *p = reinterpret_cast<quint8 *>(data.data()) + qint64(r) * rowSamples;
            for (int i = stride; i < rowSamples; ++i)
                p[i] = quint8(p[i] + p[i - stride]);
        } else if (bps == 16) {
            quint16 *p = reinterpret_cast<quint16 *>(data.data()) + qint64(r) * rowSamples;
            for (int i = stride; i < rowSamples; ++i)
                p[i] = quint16(p[i] + p[i - stride]);
        } else if (bps == 32) {
            quint32 *p = reinterpret_cast<quint32 *>(data.data()) + qint64(r) * rowSamples;
            for (int i = stride; i < rowSamples; ++i)
                p[i] = quint32(p[i] + p[i - stride]);
        } else if (bps == 64) {
            quint64 *p = reinterpret_cast<quint64 *>(data.data()) + qint64(r) * rowSamples;
            for (int i = stride; i < rowSamples; ++i)
                p[i] = quint64(p[i] + p[i - stride]);
        }
    }
}

// Deshace el predictor 3 (coma flotante, Adobe TIFF Tech Note 3): cada fila
// viene como planos de bytes (todos los más significativos, luego los
// siguientes…) con diferencias entre bytes. Deja las muestras en big-endian,
// una tras otra.
void undoFloatPredictor(QByteArray &data, int rowSamples, int stride, int bps)
{
    const int bytes = bps / 8;
    const int rowBytes = rowSamples * bytes;
    if (rowBytes <= 0)
        return;
    const int rows = int(data.size() / rowBytes);
    QByteArray tmp(rowBytes, 0);
    for (int r = 0; r < rows; ++r) {
        uchar *row = reinterpret_cast<uchar *>(data.data()) + qint64(r) * rowBytes;
        for (int i = stride; i < rowBytes; ++i)
            row[i] = uchar(row[i] + row[i - stride]);
        for (int s = 0; s < rowSamples; ++s)
            for (int b = 0; b < bytes; ++b)
                tmp[s * bytes + b] = char(row[b * rowSamples + s]);
        memcpy(row, tmp.constData(), size_t(rowBytes));
    }
}

// Convierte \a raw (muestras ya en el orden de bytes indicado) a floats.
// \a bigEndian dice cómo vienen los bytes.
QVector<float> toFloats(const QByteArray &raw, int count, int bps, int format, bool bigEndian)
{
    QVector<float> out(count);
    const uchar *p = reinterpret_cast<const uchar *>(raw.constData());
    for (int i = 0; i < count; ++i) {
        float v = 0.0f;
        if (bps == 8) {
            v = format == 2 ? float(qint8(p[i])) : float(p[i]);
        } else if (bps == 16) {
            const quint16 u = bigEndian ? qFromBigEndian<quint16>(p + i * 2)
                                        : qFromLittleEndian<quint16>(p + i * 2);
            v = format == 2 ? float(qint16(u)) : float(u);
        } else if (bps == 32) {
            const quint32 u = bigEndian ? qFromBigEndian<quint32>(p + i * 4)
                                        : qFromLittleEndian<quint32>(p + i * 4);
            if (format == 3) {
                float f;
                memcpy(&f, &u, 4);
                v = f;
            } else {
                v = format == 2 ? float(qint32(u)) : float(u);
            }
        } else if (bps == 64) {
            const quint64 u = bigEndian ? qFromBigEndian<quint64>(p + i * 8)
                                        : qFromLittleEndian<quint64>(p + i * 8);
            double dv;
            memcpy(&dv, &u, 8);
            v = float(dv);
        }
        out[i] = v;
    }
    return out;
}

// ¿Esta máquina guarda los enteros con el byte menos significativo primero?
bool hostIsLittleEndian()
{
    const quint16 x = 1;
    return *reinterpret_cast<const uchar *>(&x) == 1;
}

// Pasa a Web Mercator (metros) una longitud/latitud en grados.
void toMercator(double lon, double lat, double *x, double *y)
{
    const double clampedLat = std::max(-85.0511287798, std::min(85.0511287798, lat));
    *x = kMercatorR * lon * kPi / 180.0;
    *y = kMercatorR * std::log(std::tan(kPi / 4.0 + clampedLat * kPi / 360.0));
}

// Pasa de Web Mercator (metros) a longitud/latitud en grados.
void fromMercator(double x, double y, double *lon, double *lat)
{
    *lon = x / kMercatorR * 180.0 / kPi;
    *lat = std::atan(std::sinh(y / kMercatorR)) * 180.0 / kPi;
}

} // namespace

// Estado interno de la imagen: cabecera leída, georreferencia y caché de bloques.
struct GeoTiffImage::Impl
{
    QString path;
    bool open = false;
    bool bigEndian = false;
    bool bigTiff = false;

    Layout lay;
    QVector<quint64> offsets;
    QVector<quint64> counts;
    int blocksPerBand = 0;

    // Georreferencia en convención «esquina»: el píxel (c, r) cubre
    // [x0 + c*dx, x0 + (c+1)*dx] x [y0 + r*dy, y0 + (r+1)*dy] (dy suele ser < 0).
    double x0 = 0, y0 = 0, dx = 1, dy = -1;
    Crs crs = Crs::Geographic;
    double lonMin = 0, lonMax = 0, latMin = 0, latMax = 0;

    bool hasNoData = false;
    double noData = 0.0;

    mutable QMutex mutex;
    mutable QHash<quint64, Block> cache;
    mutable QVector<quint64> lru;        // más reciente al final
    mutable qint64 cacheBytes = 0;

    // Lee el fichero entero hasta el directorio y rellena todo lo anterior.
    bool load(const QString &file, QString *error);
    // Lee \a n bytes desde \a off. false si el fichero no da tantos.
    bool readAt(QFile &f, qint64 off, qint64 n, QByteArray &out) const;
    // Los 4 u 8 bytes enteros que dependen del orden y del tipo de TIFF.
    quint16 u16(const uchar *p) const;
    quint32 u32(const uchar *p) const;
    quint64 u64(const uchar *p) const;
    // Todos los bytes de datos de una entrada (inline o leídos de su posición).
    bool entryBytes(QFile &f, const Entry &e, QByteArray &out) const;
    // Valores de una entrada como números (para escalas, puntos de enlace…).
    QVector<double> entryDoubles(QFile &f, const Entry &e) const;
    // Valores de una entrada como enteros sin signo (desplazamientos, tamaños).
    QVector<quint64> entryUInts(QFile &f, const Entry &e) const;
    // Decodifica la georreferencia y el sistema de coordenadas de las etiquetas.
    bool parseGeo(QFile &f, const QMap<quint16, Entry> &entries, QString *error);
    // Descodifica el bloque \a index (lo lee del fichero y lo descomprime).
    Block decodeBlock(int index) const;
    // Valor de una muestra o NaN; \a lock ya tomado por el llamador.
    double sampleLocked(int col, int row, int band) const;
    // Devuelve el bloque \a index de la caché, descodificándolo si falta.
    const Block &blockFor(int index) const;
    // Posición continua (en píxeles, respecto al centro del píxel 0) de x,y en el CRS.
    bool locateXY(double x, double y, double *col, double *row) const;
};

// Orden de bytes: entero de 16 bits.
quint16 GeoTiffImage::Impl::u16(const uchar *p) const
{
    return bigEndian ? qFromBigEndian<quint16>(p) : qFromLittleEndian<quint16>(p);
}

// Orden de bytes: entero de 32 bits.
quint32 GeoTiffImage::Impl::u32(const uchar *p) const
{
    return bigEndian ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
}

// Orden de bytes: entero de 64 bits.
quint64 GeoTiffImage::Impl::u64(const uchar *p) const
{
    return bigEndian ? qFromBigEndian<quint64>(p) : qFromLittleEndian<quint64>(p);
}

// Lee \a n bytes a partir de \a off en \a f.
bool GeoTiffImage::Impl::readAt(QFile &f, qint64 off, qint64 n, QByteArray &out) const
{
    if (off < 0 || n < 0 || !f.seek(off))
        return false;
    out = f.read(n);
    return out.size() == n;
}

// Bytes de datos de una entrada: están dentro del propio campo si caben (4 bytes
// en TIFF clásico, 8 en BigTIFF) y, si no, en la posición que el campo indica.
bool GeoTiffImage::Impl::entryBytes(QFile &f, const Entry &e, QByteArray &out) const
{
    const qint64 total = qint64(e.count) * typeSize(e.type);
    if (total <= 0 || total > (qint64(1) << 31))
        return false;
    if (total <= e.field.size()) {
        out = e.field.left(int(total));
        return true;
    }
    const quint64 off = bigTiff ? u64(reinterpret_cast<const uchar *>(e.field.constData()))
                                : u32(reinterpret_cast<const uchar *>(e.field.constData()));
    return readAt(f, qint64(off), total, out);
}

// Valores numéricos de una entrada, en coma flotante.
QVector<double> GeoTiffImage::Impl::entryDoubles(QFile &f, const Entry &e) const
{
    QByteArray raw;
    QVector<double> out;
    if (!entryBytes(f, e, raw))
        return out;
    const uchar *p = reinterpret_cast<const uchar *>(raw.constData());
    const int sz = typeSize(e.type);
    out.reserve(int(e.count));
    for (quint64 i = 0; i < e.count; ++i, p += sz) {
        double v = 0.0;
        switch (e.type) {
        case TByte: case TUndefined: v = *p; break;
        case TSByte: v = qint8(*p); break;
        case TShort: v = u16(p); break;
        case TSShort: v = qint16(u16(p)); break;
        case TLong: v = u32(p); break;
        case TSLong: v = qint32(u32(p)); break;
        case TLong8: case TIfd8: v = double(u64(p)); break;
        case TSLong8: v = double(qint64(u64(p))); break;
        case TFloat: { const quint32 u = u32(p); float f32; memcpy(&f32, &u, 4); v = f32; break; }
        case TDouble: { const quint64 u = u64(p); memcpy(&v, &u, 8); break; }
        case TRational: v = u32(p + 4) ? double(u32(p)) / double(u32(p + 4)) : 0.0; break;
        default: break;
        }
        out.append(v);
    }
    return out;
}

// Valores enteros sin signo de una entrada (tamaños, desplazamientos de bloques).
QVector<quint64> GeoTiffImage::Impl::entryUInts(QFile &f, const Entry &e) const
{
    QByteArray raw;
    QVector<quint64> out;
    if (!entryBytes(f, e, raw))
        return out;
    const uchar *p = reinterpret_cast<const uchar *>(raw.constData());
    const int sz = typeSize(e.type);
    out.reserve(int(e.count));
    for (quint64 i = 0; i < e.count; ++i, p += sz) {
        quint64 v = 0;
        switch (e.type) {
        case TByte: v = *p; break;
        case TShort: v = u16(p); break;
        case TLong: v = u32(p); break;
        case TLong8: case TIfd8: v = u64(p); break;
        default: break;
        }
        out.append(v);
    }
    return out;
}

// Georreferencia: transformación afín (sin rotación) y sistema de coordenadas.
bool GeoTiffImage::Impl::parseGeo(QFile &f, const QMap<quint16, Entry> &entries, QString *error)
{
    // --- transformación píxel -> coordenadas del CRS ---
    bool pixelIsPoint = false;
    int modelType = 0, geographicEpsg = 0, projectedEpsg = 0;
    if (entries.contains(TagGeoKeys)) {
        const QVector<double> k = entryDoubles(f, entries.value(TagGeoKeys));
        const int nKeys = k.size() >= 4 ? int(k[3]) : 0;
        for (int i = 0; i < nKeys && 4 + (i + 1) * 4 <= k.size(); ++i) {
            const int id = int(k[4 + i * 4]);
            const int loc = int(k[4 + i * 4 + 1]);
            const int val = int(k[4 + i * 4 + 3]);
            if (loc != 0)
                continue;                   // el valor vive en otra etiqueta: no hace falta
            if (id == 1024) modelType = val;
            else if (id == 1025) pixelIsPoint = val == 2;
            else if (id == 2048) geographicEpsg = val;
            else if (id == 3072) projectedEpsg = val;
        }
    }

    if (entries.contains(TagTransformation)) {
        const QVector<double> m = entryDoubles(f, entries.value(TagTransformation));
        if (m.size() < 16) {
            *error = QStringLiteral("etiqueta ModelTransformation incompleta");
            return false;
        }
        if (std::fabs(m[1]) > 1e-12 || std::fabs(m[4]) > 1e-12) {
            *error = QStringLiteral("la imagen está rotada o sesgada; no se admite");
            return false;
        }
        dx = m[0];
        x0 = m[3];
        dy = m[5];
        y0 = m[7];
    } else if (entries.contains(TagPixelScale) && entries.contains(TagTiepoint)) {
        const QVector<double> s = entryDoubles(f, entries.value(TagPixelScale));
        const QVector<double> t = entryDoubles(f, entries.value(TagTiepoint));
        if (s.size() < 2 || t.size() < 6) {
            *error = QStringLiteral("georreferencia incompleta");
            return false;
        }
        if (t.size() > 6) {
            *error = QStringLiteral("la imagen usa varios puntos de control (GCP); no se admite");
            return false;
        }
        dx = s[0];
        dy = -s[1];
        x0 = t[3] - t[0] * dx;
        y0 = t[4] - t[1] * dy;
    } else {
        *error = QStringLiteral("no es un GeoTIFF: falta la georreferencia");
        return false;
    }
    if (dx == 0.0 || dy == 0.0) {
        *error = QStringLiteral("tamaño de píxel nulo");
        return false;
    }
    if (pixelIsPoint) {                      // pasar a la convención «esquina»
        x0 -= dx / 2.0;
        y0 -= dy / 2.0;
    }

    // --- sistema de coordenadas ---
    const bool mercatorEpsg = projectedEpsg == 3857 || projectedEpsg == 900913
                              || projectedEpsg == 3785 || projectedEpsg == 102100
                              || projectedEpsg == 102113;
    if (modelType == 2 || (modelType == 0 && geographicEpsg != 0)) {
        crs = Crs::Geographic;
    } else if (modelType == 1 && mercatorEpsg) {
        crs = Crs::WebMercator;
    } else if (modelType == 1) {
        *error = QStringLiteral("sistema de coordenadas proyectado EPSG:%1 no soportado; "
                                "reproyecta el fichero a EPSG:4326").arg(projectedEpsg);
        return false;
    } else {
        // Sin claves GeoTIFF: se deduce de los números (grados o metros).
        const double xr = std::max(std::fabs(x0), std::fabs(x0 + dx * lay.width));
        const double yr = std::max(std::fabs(y0), std::fabs(y0 + dy * lay.height));
        if (xr <= 360.0 && yr <= 90.0) {
            crs = Crs::Geographic;
        } else {
            *error = QStringLiteral("no se reconoce el sistema de coordenadas");
            return false;
        }
    }

    // --- extensión en grados ---
    const double xa = x0, xb = x0 + dx * lay.width;
    const double ya = y0, yb = y0 + dy * lay.height;
    if (crs == Crs::Geographic) {
        lonMin = std::min(xa, xb);
        lonMax = std::max(xa, xb);
        latMin = std::min(ya, yb);
        latMax = std::max(ya, yb);
    } else {
        double lo1, la1, lo2, la2;
        fromMercator(std::min(xa, xb), std::min(ya, yb), &lo1, &la1);
        fromMercator(std::max(xa, xb), std::max(ya, yb), &lo2, &la2);
        lonMin = lo1;
        lonMax = lo2;
        latMin = la1;
        latMax = la2;
    }

    if (entries.contains(TagGdalNoData)) {
        QByteArray raw;
        if (entryBytes(f, entries.value(TagGdalNoData), raw)) {
            bool ok = false;
            const double v = QString::fromLatin1(raw).trimmed().remove(QChar(0)).toDouble(&ok);
            if (ok) {
                hasNoData = true;
                noData = v;
            }
        }
    }
    return true;
}

// Lee y valida el directorio de la primera imagen, y deja el objeto listo para
// descodificar bloques.
bool GeoTiffImage::Impl::load(const QString &file, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("no se pudo abrir el fichero");
        return false;
    }
    QByteArray head;
    if (!readAt(f, 0, 16, head) && !readAt(f, 0, 8, head)) {
        *error = QStringLiteral("fichero demasiado corto para ser un TIFF");
        return false;
    }
    if (head.startsWith("II"))
        bigEndian = false;
    else if (head.startsWith("MM"))
        bigEndian = true;
    else {
        *error = QStringLiteral("no es un TIFF");
        return false;
    }
    const uchar *h = reinterpret_cast<const uchar *>(head.constData());
    const quint16 magic = u16(h + 2);
    quint64 ifd = 0;
    if (magic == 42) {
        bigTiff = false;
        ifd = u32(h + 4);
    } else if (magic == 43) {
        bigTiff = true;
        if (head.size() < 16 || u16(h + 4) != 8) {
            *error = QStringLiteral("BigTIFF no válido");
            return false;
        }
        ifd = u64(h + 8);
    } else {
        *error = QStringLiteral("no es un TIFF");
        return false;
    }

    // --- entradas del primer directorio ---
    QByteArray cntBytes;
    if (!readAt(f, qint64(ifd), bigTiff ? 8 : 2, cntBytes)) {
        *error = QStringLiteral("directorio TIFF fuera del fichero");
        return false;
    }
    const quint64 nEntries = bigTiff ? u64(reinterpret_cast<const uchar *>(cntBytes.constData()))
                                     : u16(reinterpret_cast<const uchar *>(cntBytes.constData()));
    const int entrySize = bigTiff ? 20 : 12;
    const int fieldSize = bigTiff ? 8 : 4;
    QByteArray table;
    if (nEntries == 0 || nEntries > 4096
        || !readAt(f, qint64(ifd) + (bigTiff ? 8 : 2), qint64(nEntries) * entrySize, table)) {
        *error = QStringLiteral("directorio TIFF dañado");
        return false;
    }
    QMap<quint16, Entry> entries;
    for (quint64 i = 0; i < nEntries; ++i) {
        const uchar *p = reinterpret_cast<const uchar *>(table.constData()) + i * quint64(entrySize);
        Entry e;
        e.tag = u16(p);
        e.type = u16(p + 2);
        e.count = bigTiff ? u64(p + 4) : u32(p + 4);
        e.field = QByteArray(reinterpret_cast<const char *>(p + (bigTiff ? 12 : 8)), fieldSize);
        entries.insert(e.tag, e);
    }

    // Un entero de una etiqueta (o el valor por defecto si falta).
    auto number = [&](quint16 tag, double def) {
        if (!entries.contains(tag))
            return def;
        const QVector<double> v = entryDoubles(f, entries.value(tag));
        return v.isEmpty() ? def : v.first();
    };

    lay.width = int(number(TagWidth, 0));
    lay.height = int(number(TagLength, 0));
    lay.spp = int(number(TagSamplesPerPixel, 1));
    lay.compression = int(number(TagCompression, 1));
    lay.predictor = int(number(TagPredictor, 1));
    lay.planar = int(number(TagPlanarConfig, 1)) == 2;
    lay.sampleFormat = int(number(TagSampleFormat, 1));
    lay.bps = int(number(TagBitsPerSample, 1));
    if (lay.width <= 0 || lay.height <= 0 || lay.spp <= 0 || lay.spp > 16) {
        *error = QStringLiteral("dimensiones de imagen inválidas");
        return false;
    }
    const bool intOk = (lay.sampleFormat == 1 || lay.sampleFormat == 2)
                       && (lay.bps == 8 || lay.bps == 16 || lay.bps == 32);
    const bool floatOk = lay.sampleFormat == 3 && (lay.bps == 32 || lay.bps == 64);
    if (!intOk && !floatOk) {
        *error = QStringLiteral("formato de muestra no soportado (%1 bits, formato %2)")
                     .arg(lay.bps).arg(lay.sampleFormat);
        return false;
    }
    const int c = lay.compression;
    if (c != 1 && c != 5 && c != 8 && c != 32946 && c != 32773) {
        *error = QStringLiteral("compresión TIFF %1 no soportada (solo ninguna, LZW, Deflate y PackBits)")
                     .arg(c);
        return false;
    }
    if (lay.predictor != 1 && lay.predictor != 2 && lay.predictor != 3) {
        *error = QStringLiteral("predictor %1 no soportado").arg(lay.predictor);
        return false;
    }
    if (lay.predictor == 3 && lay.sampleFormat != 3) {
        *error = QStringLiteral("predictor de coma flotante en datos enteros");
        return false;
    }

    // --- organización en bloques ---
    lay.tiled = entries.contains(TagTileWidth) && entries.contains(TagTileOffsets);
    if (lay.tiled) {
        lay.tileW = int(number(TagTileWidth, 0));
        lay.tileH = int(number(TagTileLength, 0));
        if (lay.tileW <= 0 || lay.tileH <= 0) {
            *error = QStringLiteral("tamaño de tesela inválido");
            return false;
        }
        offsets = entryUInts(f, entries.value(TagTileOffsets));
        counts = entryUInts(f, entries.value(TagTileByteCounts));
        const int across = (lay.width + lay.tileW - 1) / lay.tileW;
        const int down = (lay.height + lay.tileH - 1) / lay.tileH;
        blocksPerBand = across * down;
    } else {
        if (!entries.contains(TagStripOffsets)) {
            *error = QStringLiteral("la imagen no tiene franjas ni teselas");
            return false;
        }
        lay.tileW = lay.width;
        lay.tileH = std::max(1, int(std::min<double>(number(TagRowsPerStrip, lay.height), lay.height)));
        offsets = entryUInts(f, entries.value(TagStripOffsets));
        counts = entryUInts(f, entries.value(TagStripByteCounts));
        blocksPerBand = (lay.height + lay.tileH - 1) / lay.tileH;
    }
    const int need = blocksPerBand * (lay.planar ? lay.spp : 1);
    if (offsets.size() < need || counts.size() < need) {
        *error = QStringLiteral("faltan desplazamientos de bloques (%1 de %2)")
                     .arg(offsets.size()).arg(need);
        return false;
    }

    if (!parseGeo(f, entries, error))
        return false;
    open = true;
    path = file;
    return true;
}

// Lee un bloque del fichero, lo descomprime, deshace el predictor y lo pasa a floats.
Block GeoTiffImage::Impl::decodeBlock(int index) const
{
    Block out;
    const int local = index % blocksPerBand;
    const int bw = lay.tileW;
    int bh = lay.tileH;
    if (!lay.tiled) {
        const int row0 = local * lay.tileH;
        bh = std::min(lay.tileH, lay.height - row0);
    }
    const int samplesPerPixel = lay.planar ? 1 : lay.spp;
    const int bytesPer = lay.bps / 8;
    const qint64 count = qint64(bw) * bh * samplesPerPixel;
    const qint64 expected = count * bytesPer;
    if (index >= offsets.size() || counts[index] == 0 || expected > (qint64(1) << 30))
        return out;

    QFile f(path);
    QByteArray comp;
    if (!f.open(QIODevice::ReadOnly) || !readAt(f, qint64(offsets[index]), qint64(counts[index]), comp))
        return out;

    QByteArray raw;
    bool ok = true;
    switch (lay.compression) {
    case 1: raw = comp; ok = raw.size() >= expected; break;
    case 5: ok = lzwDecode(comp, raw, int(expected)); break;
    case 8: case 32946: ok = inflateZlib(comp, raw, int(expected)); break;
    case 32773: ok = unpackBits(comp, raw, int(expected)); break;
    default: ok = false; break;
    }
    if (!ok)
        return out;
    raw.truncate(int(expected));

    const int rowSamples = bw * samplesPerPixel;
    bool dataIsBigEndian = bigEndian;
    if (lay.predictor == 3) {
        undoFloatPredictor(raw, rowSamples, samplesPerPixel, lay.bps);
        dataIsBigEndian = true;             // el predictor 3 las deja en big-endian
    } else if (lay.predictor == 2) {
        // Las sumas se hacen con los enteros ya en el orden de la máquina.
        if (lay.bps > 8 && dataIsBigEndian == hostIsLittleEndian()) {
            uchar *p = reinterpret_cast<uchar *>(raw.data());
            for (qint64 i = 0; i < count; ++i)
                std::reverse(p + i * bytesPer, p + (i + 1) * bytesPer);
            dataIsBigEndian = !hostIsLittleEndian();
        }
        undoHorizontalPredictor(raw, rowSamples, samplesPerPixel, lay.bps);
    }
    out.v = toFloats(raw, int(count), lay.bps, lay.sampleFormat, dataIsBigEndian);
    out.ok = true;
    return out;
}

// Bloque \a index de la caché; si falta lo descodifica y, si la caché pasa de
// ~96 MiB, expulsa los menos recientes. Lo llama quien ya tiene el cerrojo.
const Block &GeoTiffImage::Impl::blockFor(int index) const
{
    const quint64 key = quint64(index);
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (lru.isEmpty() || lru.last() != key) {
            lru.removeAll(key);
            lru.append(key);
        }
        return it.value();
    }
    Block b = decodeBlock(index);
    const qint64 bytes = qint64(b.v.size()) * 4 + 64;
    const qint64 budget = qint64(96) * 1024 * 1024;
    while (cacheBytes + bytes > budget && !lru.isEmpty()) {
        const quint64 old = lru.first();
        lru.removeFirst();
        const auto oit = cache.find(old);
        if (oit != cache.end()) {
            cacheBytes -= qint64(oit.value().v.size()) * 4 + 64;
            cache.erase(oit);
        }
    }
    cacheBytes += bytes;
    lru.append(key);
    return *cache.insert(key, b);
}

// Valor de la muestra (col,row,band) o NaN. Requiere el cerrojo tomado.
double GeoTiffImage::Impl::sampleLocked(int col, int row, int band) const
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (col < 0 || row < 0 || col >= lay.width || row >= lay.height
        || band < 0 || band >= lay.spp)
        return nan;

    int bx = 0, by = 0;
    int across = 1;
    if (lay.tiled) {
        across = (lay.width + lay.tileW - 1) / lay.tileW;
        bx = col / lay.tileW;
        by = row / lay.tileH;
    } else {
        by = row / lay.tileH;
    }
    int index = by * across + bx + (lay.planar ? band * blocksPerBand : 0);
    const Block &b = blockFor(index);
    if (!b.ok)
        return nan;
    const int ci = col - bx * lay.tileW;
    const int ri = row - by * lay.tileH;
    const qint64 at = lay.planar ? qint64(ri) * lay.tileW + ci
                                 : (qint64(ri) * lay.tileW + ci) * lay.spp + band;
    if (at < 0 || at >= b.v.size())
        return nan;
    const float v = b.v[int(at)];
    if (!std::isfinite(v))
        return nan;
    if (hasNoData && v == float(noData))
        return nan;
    return double(v);
}

// Posición continua (respecto al centro del píxel 0,0) de un punto x,y del CRS
// de la imagen; false si cae fuera de la extensión.
bool GeoTiffImage::Impl::locateXY(double x, double y, double *col, double *row) const
{
    const double u = (x - x0) / dx;           // en píxeles desde el borde de la imagen
    const double v = (y - y0) / dy;
    const double eps = 1e-9;
    if (u < -eps || v < -eps || u > lay.width + eps || v > lay.height + eps)
        return false;
    *col = u - 0.5;
    *row = v - 0.5;
    return true;
}

GeoTiffImage::GeoTiffImage() : d(new Impl) {}
GeoTiffImage::~GeoTiffImage() = default;

// Abre el fichero y lee su cabecera. Deja el motivo en \a error si falla.
bool GeoTiffImage::open(const QString &path, QString *error)
{
    QString motivo;
    d.reset(new Impl);
    if (!d->load(path, &motivo)) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, motivo);
        d->open = false;
        return false;
    }
    return true;
}

bool GeoTiffImage::isOpen() const { return d->open; }
QString GeoTiffImage::path() const { return d->path; }
int GeoTiffImage::width() const { return d->lay.width; }
int GeoTiffImage::height() const { return d->lay.height; }
int GeoTiffImage::bands() const { return d->lay.spp; }
GeoTiffImage::Crs GeoTiffImage::crs() const { return d->crs; }
double GeoTiffImage::lonMin() const { return d->lonMin; }
double GeoTiffImage::lonMax() const { return d->lonMax; }
double GeoTiffImage::latMin() const { return d->latMin; }
double GeoTiffImage::latMax() const { return d->latMax; }
bool GeoTiffImage::hasNoData() const { return d->hasNoData; }
double GeoTiffImage::noData() const { return d->noData; }

// Valor de una muestra (NaN fuera, sin dato o ilegible).
double GeoTiffImage::sample(int col, int row, int band) const
{
    if (!d->open)
        return std::numeric_limits<double>::quiet_NaN();
    QMutexLocker lock(&d->mutex);
    return d->sampleLocked(col, row, band);
}

// Posición continua de una coordenada geográfica en la imagen. En geográficas
// prueba también la longitud ±360° por si el fichero usa 0..360.
bool GeoTiffImage::locate(double lon, double lat, double *col, double *row) const
{
    if (!d->open)
        return false;
    if (d->crs == Crs::WebMercator) {
        double x, y;
        toMercator(lon, lat, &x, &y);
        return d->locateXY(x, y, col, row);
    }
    for (const double shift : { 0.0, -360.0, 360.0 })
        if (d->locateXY(lon + shift, lat, col, row))
            return true;
    return false;
}

// Interpolación bilineal entre las cuatro muestras que rodean la coordenada.
// En los bordes (media muestra hacia fuera) se usa la muestra más cercana.
double GeoTiffImage::interpolate(double lon, double lat, int band) const
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double u, v;
    if (!locate(lon, lat, &u, &v))
        return nan;
    u = std::max(0.0, std::min(u, double(d->lay.width - 1)));
    v = std::max(0.0, std::min(v, double(d->lay.height - 1)));
    const int c0 = int(std::floor(u));
    const int r0 = int(std::floor(v));
    const int c1 = std::min(c0 + 1, d->lay.width - 1);
    const int r1 = std::min(r0 + 1, d->lay.height - 1);
    const double tx = u - c0;
    const double ty = v - r0;

    QMutexLocker lock(&d->mutex);
    const double v00 = d->sampleLocked(c0, r0, band);
    const double v01 = d->sampleLocked(c1, r0, band);
    const double v10 = d->sampleLocked(c0, r1, band);
    const double v11 = d->sampleLocked(c1, r1, band);
    if (std::isnan(v00) || std::isnan(v01) || std::isnan(v10) || std::isnan(v11))
        return nan;
    const double top = v00 * (1.0 - tx) + v01 * tx;
    const double bottom = v10 * (1.0 - tx) + v11 * tx;
    return top * (1.0 - ty) + bottom * ty;
}

} // namespace libmapa
