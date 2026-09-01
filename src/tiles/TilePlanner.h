#ifndef LIBMAPA_TILES_TILEPLANNER_H_
#define LIBMAPA_TILES_TILEPLANNER_H_

#include "tiles/TileCache.h"
#include "tiles/TileKey.h"
#include "geo/TileMatrix.h"

#include <QRectF>
#include <QVector>

namespace libmapa {

/*!
 * \brief Una instruccion de dibujo: que imagen va en que hueco de la rejilla.
 *
 * Cuando la tesela exacta no esta disponible se usa un ANCESTRO escalado, y
 * \a sourceRect indica que trozo de esa imagen corresponde a este hueco.
 */
struct TileDrawItem
{
    TileKey slot;        //!< Hueco de la rejilla visible, siempre al zoom pedido.
    TileKey source;      //!< Tesela realmente disponible (igual a slot, o un ancestro).
    QRectF  sourceRect;  //!< Region de la imagen de \a source, en pixeles.
    int     ancestorDepth = 0;  //!< 0 = exacta; 1 = padre; 2 = abuelo...

    bool isExact() const { return ancestorDepth == 0; }
};

/*!
 * \brief Decide que dibujar en cada hueco de la rejilla visible.
 *
 * El respaldo de tesela padre NO es una optimizacion opcional en este
 * proyecto: es el caso normal. El sondeo de las BD reales lo dejo claro.
 *
 *   - La BD satelital se estabiliza en un 32 % de relleno entre los niveles
 *     12 y 15, que es aproximadamente la proporcion entre la superficie de
 *     Cuba y su rectangulo envolvente: sencillamente no hay teselas de mar.
 *     En el nivel 16 baja al 8 %, en el 17 al 1.6 % y en el 18 al 0.2 %.
 *   - La verificacion de lectura devolvio 19 de 25 teselas pedidas.
 *
 * Sin respaldo, mas de la mitad de la pantalla quedaria en blanco al usar la
 * capa satelital. El codigo original no lo contemplaba: dibujaba solo las
 * teselas que encontraba y dejaba el resto vacio.
 *
 * La clase es pura: no toca la BD ni la interfaz, solo consulta la cache.
 * Por eso se puede probar sin ventanas ni ficheros.
 */
class TilePlanner
{
public:
    struct Plan
    {
        QVector<TileDrawItem> items;   //!< Que dibujar, en orden de zoom creciente.
        QVector<TileKey> missing;      //!< Teselas exactas que faltan en cache.

        int exactCount = 0;            //!< Huecos resueltos con la tesela propia.
        int fallbackCount = 0;         //!< Huecos resueltos con un ancestro.
        int emptyCount = 0;            //!< Huecos sin nada que dibujar.

        int slotCount() const { return exactCount + fallbackCount + emptyCount; }
        double coverage() const
        {
            const int n = slotCount();
            return n > 0 ? static_cast<double>(exactCount + fallbackCount)
                               / static_cast<double>(n)
                         : 0.0;
        }
    };

    explicit TilePlanner(int tileSize = 256) : m_tileSize(tileSize) {}

    int tileSize() const { return m_tileSize; }
    void setTileSize(int px) { m_tileSize = px > 0 ? px : 256; }

    /*!
     * \param range          rejilla visible
     * \param cache          de donde salen las imagenes ya decodificadas
     * \param minZoom        no se sube por encima de este nivel al buscar ancestros
     * \param maxAncestorDepth cuantos niveles se permite subir
     *
     * Los items salen ordenados por zoom del origen ASCENDENTE, de modo que
     * al pintarlos en orden los ancestros muy escalados (borrosos) quedan
     * debajo y las teselas exactas encima. Asi, cuando llega una tesela nueva,
     * tapa al sucedaneo sin parpadeos.
     */
    Plan plan(const TileMatrix::TileRange &range,
              const TileCache &cache,
              int minZoom = 0,
              int maxAncestorDepth = 5) const;

    /*!
     * \brief Region de la imagen del ancestro que corresponde a \a slot.
     *
     * A profundidad d, el ancestro cubre 2^d x 2^d huecos, asi que a cada uno
     * le toca un cuadrado de lado tileSize / 2^d.
     */
    QRectF sourceRectFor(const TileKey &slot, int ancestorDepth) const;

    //! Ancestro de \a key a la profundidad indicada.
    static TileKey ancestorOf(const TileKey &key, int depth);

private:
    int m_tileSize;
};

} // namespace libmapa

#endif // LIBMAPA_TILES_TILEPLANNER_H_
