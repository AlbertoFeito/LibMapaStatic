#ifndef LIBMAPA_VECTOR_LANDMASK_H_
#define LIBMAPA_VECTOR_LANDMASK_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class QPainter;

namespace libmapa {

/*!
 * \brief Máscara de tierra: polígonos de costa para pintar el MAR en las teselas
 *        vectoriales.
 *
 * Hay MBTiles vectoriales sin polígonos de océano (el de Cuba de OpenMapTiles solo
 * trae lagos y ríos): el mar queda del color de fondo, igual que la tierra. Con
 * una máscara, \c VectorTileRenderer pinta primero el mar y encima la tierra, y
 * después lo de siempre.
 *
 * La máscara es la misma BD de entidades que ya usan `dem_to_hillshade
 * --water-mask` y la capa «costa exacta» (`herramientas/osm_costa_a_sqlitedb.py`):
 * polígonos de tierra, uno o varios, multi-parte. Se rellenan con la regla
 * par-impar, así los anillos interiores (lagunas, bahías cerradas) quedan como
 * agua sin tener que orientar nada.
 *
 * Se carga una vez y después solo se lee: se puede usar desde varios hilos a la vez.
 */
class LandMask
{
public:
    /*!
     * \brief Lee los polígonos de la BD de entidades \a path (de la capa \a layer,
     *        o de todas si está vacía).
     * \return null si no se puede abrir o no trae ningún polígono; el motivo, en \a error.
     */
    static std::shared_ptr<LandMask> fromFile(const QString &path, const QString &layer = QString(),
                                              QString *error = nullptr);

    //! Máscara a partir de anillos ya en memoria (cada uno, lista de coordenadas).
    static std::shared_ptr<LandMask> fromRings(const QVector<QVector<QGeoCoordinate>> &rings);

    int ringCount() const { return int(m_rings.size()); }
    qint64 vertexCount() const { return m_vertices; }

    /*!
     * \brief Rellena con \a land la tierra de la tesela XYZ (\a z, \a x, \a y) en un
     *        lienzo de \a sizePx de lado (el resto no se toca).
     * \return true si había algo de tierra que pintar.
     */
    bool paint(QPainter &p, int z, int x, int y, int sizePx, const QColor &land) const;

private:
    // Un anillo en Web Mercator normalizado (x e y de 0 a 1, y hacia el sur, como
    // las teselas XYZ) y su recuadro, para descartar rápido los que no tocan.
    struct Ring
    {
        QVector<QPointF> pts;
        QRectF box;
    };

    void addRing(const QVector<QGeoCoordinate> &ring);

    std::vector<Ring> m_rings;
    qint64 m_vertices = 0;
};

} // namespace libmapa

#endif // LIBMAPA_VECTOR_LANDMASK_H_
