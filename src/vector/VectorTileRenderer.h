#ifndef LIBMAPA_VECTOR_VECTORTILERENDERER_H_
#define LIBMAPA_VECTOR_VECTORTILERENDERER_H_

#include "vector/MvtTile.h"
#include "vector/VectorStyle.h"

#include <QByteArray>
#include <QImage>
#include <QString>

namespace libmapa {

/*!
 * \brief Convierte una tesela vectorial (MVT) en una imagen, según un estilo.
 *
 * Es lo que permite usar un MBTiles vectorial como capa base sin tocar el resto
 * del motor: el \c TileLoader, en su hilo trabajador, pasa por aquí las teselas
 * de un dataset vectorial y entrega una QImage como si hubiera leído un PNG.
 * Caché, escalera de respaldo, cobertura y capa activa siguen igual.
 *
 * Rótulos: cada tesela se pinta por separado, así que un texto que cruzara el
 * borde quedaría cortado por la vecina. Para evitarlo, solo se rotulan los
 * puntos cuyo ancla cae DENTRO de la tesela (la vecina no lo repite aunque lo
 * traiga en su margen) y el texto se desplaza lo justo para caber entero; los que
 * chocan con otro ya puesto se omiten.
 *
 * Es reentrante: no guarda estado entre llamadas, y se puede usar desde
 * cualquier hilo. Los rótulos necesitan una QGuiApplication (la base de datos de
 * fuentes); sin ella se dibuja la geometría y se omiten.
 */
class VectorTileRenderer
{
public:
    explicit VectorTileRenderer(VectorStyle style = VectorStyle::openMapTiles());

    const VectorStyle &style() const { return m_style; }

    //! Dibuja \a tile, que es del zoom lógico \a zoom, en una imagen de \a sizePx de lado.
    QImage render(const mvt::Tile &tile, int zoom, int sizePx) const;

    /*!
     * \brief Decodifica \a bytes (MVT en claro, gzip o zlib) y los dibuja.
     * \return imagen nula si los bytes no son una tesela MVT; el motivo, en \a error.
     */
    QImage render(const QByteArray &bytes, int zoom, int sizePx, QString *error = nullptr) const;

private:
    VectorStyle m_style;
};

} // namespace libmapa

#endif // LIBMAPA_VECTOR_VECTORTILERENDERER_H_
