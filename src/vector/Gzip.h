#ifndef LIBMAPA_VECTOR_GZIP_H_
#define LIBMAPA_VECTOR_GZIP_H_

#include <QByteArray>

namespace libmapa {

/*!
 * \brief Descompresión en memoria de gzip (RFC 1952) y zlib (RFC 1950).
 *
 * Las teselas vectoriales de un MBTiles (OpenMapTiles, tippecanoe, planetiler…)
 * se guardan casi siempre comprimidas con gzip. Qt solo trae \c qUncompress,
 * que exige su propio prefijo de longitud y no entiende la cabecera gzip, así
 * que se infla con miniz (dominio público, ya vendoreado para \c fill_hgt): sin
 * zlib externa, compila igual con MinGW, MSVC o GCC.
 */
namespace Gzip {

//! ¿Empieza por la firma de gzip (1f 8b)?
bool isGzip(const QByteArray &data);

//! ¿Empieza por una cabecera zlib válida (78 01/5e/9c/da…)?
bool isZlib(const QByteArray &data);

/*!
 * \brief Descomprime \a data si está en gzip o zlib; si no, la devuelve tal cual.
 * \param ok false si parecía comprimida pero no se pudo inflar (dato corrupto o
 *        truncado); en ese caso devuelve un QByteArray vacío.
 */
QByteArray inflateIfCompressed(const QByteArray &data, bool *ok = nullptr);

} // namespace Gzip

} // namespace libmapa

#endif // LIBMAPA_VECTOR_GZIP_H_
