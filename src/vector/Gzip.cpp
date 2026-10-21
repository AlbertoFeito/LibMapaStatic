#include "vector/Gzip.h"

#include "miniz.h"

namespace libmapa {
namespace Gzip {

namespace {

// Recibe cada trozo inflado por tinfl y lo añade al QByteArray de salida.
int appendChunk(const void *buf, int len, void *user)
{
    static_cast<QByteArray *>(user)->append(static_cast<const char *>(buf), len);
    return 1;
}

// Infla un bloque DEFLATE (crudo o con envoltorio zlib, según \a flags) con tinfl
// de miniz. Vacío y *ok=false si el dato no es válido. Se usa la variante con
// callback y no tinfl_decompress_mem_to_heap porque esta devuelve NULL también
// cuando el resultado tiene cero bytes, y una tesela vectorial VACÍA (gzip de
// nada: mar abierto en OpenMapTiles) es válida y abunda.
QByteArray inflateBlock(const uchar *p, qsizetype n, int flags, bool *ok)
{
    QByteArray raw;
    size_t inLen = size_t(n);
    const int status = tinfl_decompress_mem_to_callback(p, &inLen, appendChunk, &raw, flags);
    if (ok) *ok = status != 0;
    return status != 0 ? raw : QByteArray();
}

// Quita la cabecera gzip (RFC 1952: fija de 10 bytes + campos opcionales) y la
// cola de 8 (CRC32 + ISIZE), e infla el DEFLATE crudo de en medio. Es la misma
// lógica que ya usa fill_hgt con los .hgt.gz de Skadi.
QByteArray gunzip(const QByteArray &in, bool *ok)
{
    if (ok) *ok = false;
    const qsizetype n = in.size();
    if (n < 18)   // 10 de cabecera mínima + algo + 8 de cola
        return {};
    const uchar *p = reinterpret_cast<const uchar *>(in.constData());
    if (p[2] != 0x08)                                    // método DEFLATE
        return {};
    const uchar flg = p[3];
    qsizetype pos = 10;
    if (flg & 0x04) {                                    // FEXTRA
        if (pos + 2 > n) return {};
        const int xlen = int(p[pos]) | (int(p[pos + 1]) << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { while (pos < n && p[pos] != 0) ++pos; ++pos; }  // FNAME
    if (flg & 0x10) { while (pos < n && p[pos] != 0) ++pos; ++pos; }  // FCOMMENT
    if (flg & 0x02) pos += 2;                            // FHCRC
    if (pos >= n - 8)
        return {};
    return inflateBlock(p + pos, n - 8 - pos, 0, ok);
}

} // namespace

// Firma gzip: los dos primeros bytes son 1f 8b.
bool isGzip(const QByteArray &data)
{
    return data.size() >= 2 && uchar(data[0]) == 0x1f && uchar(data[1]) == 0x8b;
}

// Cabecera zlib: método 8 (DEFLATE) en el nibble bajo de CMF y (CMF·256+FLG)
// múltiplo de 31, como exige el RFC 1950. Un protobuf MVT nunca empieza así: su
// primer byte es 0x1a (campo 3, «layers»).
bool isZlib(const QByteArray &data)
{
    if (data.size() < 2)
        return false;
    const uint cmf = uchar(data[0]), flg = uchar(data[1]);
    return (cmf & 0x0f) == 8 && (cmf >> 4) <= 7 && ((cmf << 8) | flg) % 31 == 0;
}

// Decide por la firma y descomprime; lo que no está comprimido pasa intacto.
QByteArray inflateIfCompressed(const QByteArray &data, bool *ok)
{
    if (isGzip(data))
        return gunzip(data, ok);
    if (isZlib(data))
        return inflateBlock(reinterpret_cast<const uchar *>(data.constData()), data.size(),
                            TINFL_FLAG_PARSE_ZLIB_HEADER, ok);
    if (ok) *ok = true;
    return data;
}

} // namespace Gzip
} // namespace libmapa
