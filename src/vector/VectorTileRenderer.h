#ifndef LIBMAPA_VECTOR_VECTORTILERENDERER_H_
#define LIBMAPA_VECTOR_VECTORTILERENDERER_H_

#include "tiles/TileKey.h"
#include "vector/LandMask.h"
#include "vector/MvtTile.h"
#include "vector/VectorStyle.h"

#include <QByteArray>
#include <QImage>
#include <QSet>
#include <QString>

#include <memory>

namespace libmapa {

/*!
 * \brief Convierte una tesela vectorial (MVT) en una imagen, según un estilo.
 *
 * Es lo que permite usar un MBTiles vectorial como capa base sin tocar el resto
 * del motor: el \c TileLoader, en su hilo trabajador, pasa por aquí las teselas
 * de un dataset vectorial y entrega una QImage como si hubiera leído un PNG.
 * Caché, escalera de respaldo, cobertura y capa activa siguen igual.
 *
 * Los nombres de las líneas (calles, ríos) van a lo largo de ellas, letra a letra,
 * y solo si caben enteros en la tesela sin doblar demasiado.
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

    /*!
     * \brief Máscara de tierra para pintar el mar (null = sin máscara, como antes).
     *
     * Con máscara, el fondo de cada tesela es \c VectorStyle::sea y la tierra de la
     * máscara se pinta encima con \c VectorStyle::background; después, las reglas.
     * Solo la usan las variantes de \c render que reciben la tesela entera (\c TileKey),
     * porque hace falta saber DÓNDE está la tesela.
     */
    void setLandMask(std::shared_ptr<const LandMask> mask) { m_landMask = std::move(mask); }
    const LandMask *landMask() const { return m_landMask.get(); }

    /*!
     * \brief Grupos del estilo que NO se dibujan ("nombres_calles", "poi"…; ver
     *        \c VectorRule::group). Vacío = todo visible.
     */
    void setHiddenGroups(const QSet<QString> &groups) { m_hiddenGroups = groups; }
    QSet<QString> hiddenGroups() const { return m_hiddenGroups; }

    //! Dibuja \a tile, que es la tesela XYZ \a key, con mar y tierra si hay máscara.
    QImage render(const mvt::Tile &tile, const TileKey &key, int sizePx) const;

    //! Decodifica \a bytes y los dibuja como la tesela XYZ \a key (null si no es MVT).
    QImage render(const QByteArray &bytes, const TileKey &key, int sizePx,
                  QString *error = nullptr) const;

    //! Dibuja \a tile, que es del zoom lógico \a zoom, en una imagen de \a sizePx de lado.
    QImage render(const mvt::Tile &tile, int zoom, int sizePx) const;

    /*!
     * \brief Decodifica \a bytes (MVT en claro, gzip o zlib) y los dibuja.
     * \return imagen nula si los bytes no son una tesela MVT; el motivo, en \a error.
     */
    QImage render(const QByteArray &bytes, int zoom, int sizePx, QString *error = nullptr) const;

private:
    QImage renderImpl(const mvt::Tile &tile, int zoom, const TileKey *key, int sizePx) const;

    VectorStyle m_style;
    std::shared_ptr<const LandMask> m_landMask;
    QSet<QString> m_hiddenGroups;
};

} // namespace libmapa

#endif // LIBMAPA_VECTOR_VECTORTILERENDERER_H_
