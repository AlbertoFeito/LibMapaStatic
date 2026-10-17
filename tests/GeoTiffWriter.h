#ifndef LIBMAPA_TEST_GEOTIFFWRITER_H_
#define LIBMAPA_TEST_GEOTIFFWRITER_H_

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>
#include <QtEndian>

#include <cstring>
#include <functional>

namespace libmapa {
namespace test {

/*!
 * \brief Parámetros de un GeoTIFF sintético para los tests.
 *
 * Permite fabricar todas las variantes que el lector debe entender: franjas o
 * teselas, TIFF clásico o BigTIFF, cualquier orden de bytes, las compresiones
 * y predictores soportados, muestras enteras o de coma flotante, planos
 * separados y las distintas formas de georreferenciar.
 */
struct TiffSpec
{
    int width = 16;
    int height = 12;
    int spp = 1;                 //!< bandas
    int bps = 16;
    int sampleFormat = 2;        //!< 1 sin signo, 2 con signo, 3 coma flotante
    int compression = 1;         //!< 1 ninguna, 5 LZW, 8 Deflate, 32773 PackBits, otro = tal cual
    int predictor = 1;
    bool tiled = false;
    int tile = 16;               //!< lado de la tesela (múltiplo de 16 por norma)
    int rowsPerStrip = 4;
    bool bigEndian = false;
    bool bigTiff = false;
    bool planar = false;

    // Georreferencia: (originX, originY) es el valor del punto de enlace.
    double originX = -77.0;
    double originY = 20.0;
    double dx = 0.1;             //!< tamaño de píxel en X (positivo)
    double dy = 0.1;             //!< tamaño de píxel en Y (positivo; la imagen va de norte a sur)
    bool pixelIsPoint = false;
    bool useTransformation = false;   //!< ModelTransformation en vez de escala + enlace
    bool geoKeys = true;
    int modelType = 2;           //!< 1 proyectado, 2 geográfico
    int geographicEpsg = 4326;
    int projectedEpsg = 0;
    bool georeference = true;    //!< false: sin etiquetas de georreferencia
    QString nodata;              //!< valor GDAL_NODATA, vacío = ninguno
};

// Codificador LZW de TIFF (MSB primero, cambio de anchura anticipado), el espejo
// del descodificador de la librería. Sirve solo para fabricar datos de prueba.
inline QByteArray lzwEncode(const QByteArray &in)
{
    QByteArray out;
    quint32 acc = 0;
    int accBits = 0;
    int bits = 9;
    auto put = [&](int code) {
        acc = (acc << bits) | quint32(code);
        accBits += bits;
        while (accBits >= 8) {
            out.append(char((acc >> (accBits - 8)) & 0xFF));
            accBits -= 8;
        }
        acc &= (quint32(1) << accBits) - 1;
    };
    QHash<QByteArray, int> dict;
    int next = 258;
    put(256);
    QByteArray w;
    for (int i = 0; i < in.size(); ++i) {
        QByteArray wc = w;
        wc.append(in[i]);
        if (wc.size() == 1 || dict.contains(wc)) {
            w = wc;
            continue;
        }
        put(w.size() == 1 ? uchar(w[0]) : dict.value(w));
        dict.insert(wc, next++);
        if (next > (1 << bits) - 1 && bits < 12)
            ++bits;
        if (next >= 4094) {                 // tabla llena: reiniciar
            put(256);
            dict.clear();
            next = 258;
            bits = 9;
        }
        w = QByteArray(1, in[i]);
    }
    if (!w.isEmpty())
        put(w.size() == 1 ? uchar(w[0]) : dict.value(w));
    put(257);
    if (accBits > 0)
        out.append(char((acc << (8 - accBits)) & 0xFF));
    return out;
}

// Codificador PackBits: repite las rachas de 3 o más bytes iguales y copia el resto.
inline QByteArray packBitsEncode(const QByteArray &in)
{
    QByteArray out;
    int i = 0;
    const int n = int(in.size());
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && in[i + run] == in[i])
            ++run;
        if (run >= 3) {
            out.append(char(257 - run));
            out.append(in[i]);
            i += run;
            continue;
        }
        int lit = 0;
        while (i + lit < n && lit < 128) {
            int r = 1;
            while (i + lit + r < n && r < 3 && in[i + lit + r] == in[i + lit])
                ++r;
            if (r >= 3)
                break;
            ++lit;
        }
        if (lit == 0)
            lit = 1;
        out.append(char(lit - 1));
        out.append(in.constData() + i, lit);
        i += lit;
    }
    return out;
}

// Patrón de bits de una muestra según su formato y ancho.
inline quint64 sampleBits(double v, int bps, int format)
{
    if (format == 3) {
        if (bps == 32) {
            const float f = float(v);
            quint32 u;
            std::memcpy(&u, &f, 4);
            return u;
        }
        quint64 u;
        std::memcpy(&u, &v, 8);
        return u;
    }
    const qint64 iv = qint64(v);
    const quint64 mask = bps >= 64 ? ~quint64(0) : ((quint64(1) << bps) - 1);
    return quint64(iv) & mask;
}

// Escribe en \a out el valor \a v con \a bytes bytes y el orden de bytes pedido.
inline void putUInt(QByteArray &out, quint64 v, int bytes, bool bigEndian)
{
    uchar tmp[8];
    for (int i = 0; i < bytes; ++i) {
        const int shift = bigEndian ? (bytes - 1 - i) * 8 : i * 8;
        tmp[i] = uchar((v >> shift) & 0xFF);
    }
    out.append(reinterpret_cast<const char *>(tmp), bytes);
}

// Fabrica el fichero GeoTIFF \a path con \a value(col, row, banda) como contenido.
inline bool writeGeoTiff(const QString &path, const TiffSpec &s,
                         const std::function<double(int, int, int)> &value)
{
    const bool be = s.bigEndian;
    const int bytesPer = s.bps / 8;

    QByteArray blocks;                 // datos de los bloques, tras la cabecera
    QVector<quint64> blockOff, blockLen;
    const int headSize = s.bigTiff ? 16 : 8;

    const int across = s.tiled ? (s.width + s.tile - 1) / s.tile : 1;
    const int down = s.tiled ? (s.height + s.tile - 1) / s.tile
                             : (s.height + s.rowsPerStrip - 1) / s.rowsPerStrip;
    const int bandsPerBlock = s.planar ? 1 : s.spp;
    const int planes = s.planar ? s.spp : 1;

    for (int plane = 0; plane < planes; ++plane) {
        for (int by = 0; by < down; ++by) {
            for (int bx = 0; bx < across; ++bx) {
                const int bw = s.tiled ? s.tile : s.width;
                const int bh = s.tiled ? s.tile : qMin(s.rowsPerStrip, s.height - by * s.rowsPerStrip);
                QByteArray raw;
                for (int r = 0; r < bh; ++r) {
                    const int row = (s.tiled ? by * s.tile : by * s.rowsPerStrip) + r;
                    const int rowSamples = bw * bandsPerBlock;
                    QVector<quint64> bitsRow(rowSamples);
                    for (int c = 0; c < bw; ++c) {
                        const int col = (s.tiled ? bx * s.tile : 0) + c;
                        const bool inside = col < s.width && row < s.height;
                        for (int b = 0; b < bandsPerBlock; ++b) {
                            const int band = s.planar ? plane : b;
                            const double v = inside ? value(col, row, band) : 0.0;
                            bitsRow[c * bandsPerBlock + b] = sampleBits(v, s.bps, s.sampleFormat);
                        }
                    }
                    if (s.predictor == 3) {
                        // Muestras en big-endian, por planos de bytes, con diferencias.
                        QByteArray shuffled(rowSamples * bytesPer, 0);
                        for (int i = 0; i < rowSamples; ++i)
                            for (int k = 0; k < bytesPer; ++k)
                                shuffled[k * rowSamples + i] =
                                    char((bitsRow[i] >> ((bytesPer - 1 - k) * 8)) & 0xFF);
                        for (int i = int(shuffled.size()) - 1; i >= bandsPerBlock; --i)
                            shuffled[i] = char(uchar(shuffled[i]) - uchar(shuffled[i - bandsPerBlock]));
                        raw.append(shuffled);
                    } else {
                        if (s.predictor == 2) {
                            const quint64 mask = s.bps >= 64 ? ~quint64(0) : ((quint64(1) << s.bps) - 1);
                            for (int i = rowSamples - 1; i >= bandsPerBlock; --i)
                                bitsRow[i] = (bitsRow[i] - bitsRow[i - bandsPerBlock]) & mask;
                        }
                        for (int i = 0; i < rowSamples; ++i)
                            putUInt(raw, bitsRow[i], bytesPer, be);
                    }
                }
                QByteArray comp;
                switch (s.compression) {
                case 5: comp = lzwEncode(raw); break;
                case 8: case 32946: comp = qCompress(raw).mid(4); break;
                case 32773: comp = packBitsEncode(raw); break;
                default: comp = raw; break;
                }
                blockOff.append(quint64(headSize + blocks.size()));
                blockLen.append(quint64(comp.size()));
                blocks.append(comp);
            }
        }
    }

    // --- directorio ---
    struct Tag { quint16 type; quint64 count; QByteArray data; };
    QMap<quint16, Tag> tags;
    auto packNums = [&](int type, const QVector<double> &vals) {
        QByteArray d;
        for (const double v : vals) {
            switch (type) {
            case 2: d.append(char(qint64(v))); break;                           // ASCII / BYTE
            case 3: putUInt(d, quint64(qint64(v)), 2, be); break;               // SHORT
            case 4: putUInt(d, quint64(qint64(v)), 4, be); break;               // LONG
            case 16: putUInt(d, quint64(qint64(v)), 8, be); break;              // LONG8
            case 12: { quint64 u; std::memcpy(&u, &v, 8); putUInt(d, u, 8, be); break; }
            default: break;
            }
        }
        return d;
    };
    auto addNums = [&](quint16 tag, int type, const QVector<double> &vals) {
        tags.insert(tag, Tag{ quint16(type), quint64(vals.size()), packNums(type, vals) });
    };

    const int offType = s.bigTiff ? 16 : 4;
    addNums(256, 4, { double(s.width) });
    addNums(257, 4, { double(s.height) });
    addNums(258, 3, QVector<double>(s.spp, double(s.bps)));
    addNums(259, 3, { double(s.compression) });
    addNums(262, 3, { s.spp >= 3 ? 2.0 : 1.0 });
    addNums(277, 3, { double(s.spp) });
    addNums(284, 3, { s.planar ? 2.0 : 1.0 });
    if (s.predictor != 1)
        addNums(317, 3, { double(s.predictor) });
    addNums(339, 3, QVector<double>(s.spp, double(s.sampleFormat)));
    QVector<double> offs, lens;
    for (int i = 0; i < blockOff.size(); ++i) {
        offs.append(double(blockOff[i]));
        lens.append(double(blockLen[i]));
    }
    if (s.tiled) {
        addNums(322, 4, { double(s.tile) });
        addNums(323, 4, { double(s.tile) });
        addNums(324, offType, offs);
        addNums(325, offType, lens);
    } else {
        addNums(273, offType, offs);
        addNums(278, 4, { double(s.rowsPerStrip) });
        addNums(279, offType, lens);
    }
    if (s.georeference) {
        if (s.useTransformation) {
            addNums(34264, 12, { s.dx, 0, 0, s.originX,
                                 0, -s.dy, 0, s.originY,
                                 0, 0, 0, 0,
                                 0, 0, 0, 1 });
        } else {
            addNums(33550, 12, { s.dx, s.dy, 0.0 });
            addNums(33922, 12, { 0, 0, 0, s.originX, s.originY, 0 });
        }
        if (s.geoKeys) {
            QVector<double> k = { 1, 1, 0, 0 };           // cabecera; se rellena el nº de claves
            int n = 0;
            auto key = [&](int id, int val) { k << id << 0 << 1 << val; ++n; };
            key(1024, s.modelType);
            key(1025, s.pixelIsPoint ? 2 : 1);
            if (s.modelType == 2)
                key(2048, s.geographicEpsg);
            else
                key(3072, s.projectedEpsg);
            k[3] = n;
            addNums(34735, 3, k);
        }
    }
    if (!s.nodata.isEmpty()) {
        QByteArray t = s.nodata.toLatin1();
        t.append('\0');
        tags.insert(42113, Tag{ 2, quint64(t.size()), t });
    }

    // Datos que no caben en el campo de la entrada: van tras los bloques.
    const int fieldSize = s.bigTiff ? 8 : 4;
    QByteArray extra;
    QByteArray table;
    const quint64 extraBase = quint64(headSize + blocks.size());
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        const Tag &t = it.value();
        putUInt(table, it.key(), 2, be);
        putUInt(table, t.type, 2, be);
        putUInt(table, t.count, s.bigTiff ? 8 : 4, be);
        if (t.data.size() <= fieldSize) {
            QByteArray f = t.data;
            f.append(QByteArray(fieldSize - int(f.size()), '\0'));
            table.append(f);
        } else {
            putUInt(table, extraBase + quint64(extra.size()), fieldSize, be);
            extra.append(t.data);
            if (extra.size() % 2)
                extra.append('\0');
        }
    }
    const quint64 ifdOff = extraBase + quint64(extra.size());

    QByteArray file;
    file.append(be ? "MM" : "II", 2);
    putUInt(file, s.bigTiff ? 43 : 42, 2, be);
    if (s.bigTiff) {
        putUInt(file, 8, 2, be);
        putUInt(file, 0, 2, be);
        putUInt(file, ifdOff, 8, be);
    } else {
        putUInt(file, ifdOff, 4, be);
    }
    file.append(blocks);
    file.append(extra);
    putUInt(file, quint64(tags.size()), s.bigTiff ? 8 : 2, be);
    file.append(table);
    putUInt(file, 0, s.bigTiff ? 8 : 4, be);           // sin más directorios

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(file) == file.size();
}

} // namespace test
} // namespace libmapa

#endif // LIBMAPA_TEST_GEOTIFFWRITER_H_
