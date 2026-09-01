#ifndef LIBMAPA_GEO_TILEMATRIX_H_
#define LIBMAPA_GEO_TILEMATRIX_H_

#include "tiles/TileDataset.h"
#include "tiles/TileKey.h"

#include <QGeoCoordinate>
#include <QPointF>
#include <QRectF>

namespace libmapa {

/*!
 * \brief Conversion geografico <-> tesela. UNICA fuente de verdad.
 *
 * En el codigo original esta logica estaba repartida y descoordinada:
 *
 *   - CCalculos::Convertir_Long_a_TilesX / Convertir_Lat_a_TilesY
 *   - CCalculos::Convertir_Tiles_a_Long / Convertir_Tiles_a_Lat
 *   - CGoogleMercator::forward / inverse
 *   - CMapaPlot::ValidaZoomActivo, que calculaba los indices con
 *     "ZoomValido - 1" mientras CBDatos::Cargar_BD_IMG consultaba
 *     "WHERE z = Zoom_Level" sin el -1  (falla F-7: los indices se calculaban
 *     a un nivel y se pedian a otro, de ahi los huecos en el mapa).
 *
 * Aqui hay un solo sitio donde se decide que tesela corresponde a que
 * coordenada, y el nivel de zoom que entra es el mismo que sale.
 */
class TileMatrix
{
public:
    explicit TileMatrix(int tileSize = 256) : m_tileSize(tileSize) {}

    int tileSize() const { return m_tileSize; }

    //! Numero de teselas por lado en un nivel: 2^z.
    static int tilesPerSide(int z) { return 1 << z; }

    // --- Coordenada geografica -> indice de tesela (continuo) --------------
    static double longitudeToTileX(double lonDeg, int z);
    static double latitudeToTileY(double latDeg, int z);

    // --- Indice de tesela (continuo) -> coordenada geografica --------------
    //! Devuelve la esquina NOROESTE de la tesela (x, y).
    static double tileXToLongitude(double x, int z);
    static double tileYToLatitude(double y, int z);

    /*!
     * \brief Indice de tesela <-> ordenada de Mercator en grados.
     *
     * La relacion es LINEAL, que es justo lo que hace falta para que las
     * teselas se dibujen sin deformar:  mercDeg = 180 - 360 * y / 2^z
     */
    static double tileYToMercatorDegrees(double y, int z);
    static double mercatorDegreesToTileY(double mercatorDeg, int z);

    //! Tesela XYZ que contiene la coordenada dada.
    static TileKey tileAt(const QGeoCoordinate &coord, int z);

    //! Extension geografica de una tesela: topLeft = NO, bottomRight = SE.
    //! Ojo: en QRectF con eje Y hacia abajo, top = latitud mayor.
    static QRectF tileBounds(const TileKey &key);

    //! Esquinas NO y SE de una tesela, ya como coordenadas geograficas.
    static QGeoCoordinate tileNorthWest(const TileKey &key);
    static QGeoCoordinate tileSouthEast(const TileKey &key);

    /*!
     * \brief Latitud -> coordenada del eje Y, en "grados de Mercator".
     *
     * El eje de QCustomPlot es LINEAL en la coordenada que se le da. Si se le
     * dan grados de latitud, el mapa sale deformado, porque Mercator no es
     * lineal en latitud: a 23 grados el desfase es de 0.64 grados, a 60 es de
     * 15.5 y a 75 de 41. A zoom alto el viewport abarca decimas de grado y no
     * se nota; a zoom 3 abarca decenas y el mapa se estira verticalmente.
     *
     * En estas unidades el mundo entero va de -180 a +180, igual que la
     * longitud, asi que ambos ejes tienen la MISMA escala y las teselas salen
     * cuadradas sin trucos de relacion de aspecto.
     *
     * Es la transformada de Gudermann inversa, expresada en grados.
     */
    static double latitudeToAxisY(double latitudeDeg);
    static double axisYToLatitude(double axisY);

    // --- Esquema de eje Y ---------------------------------------------------
    //! Convierte y logico (XYZ) al y que hay que pedirle a la BD.
    static int toStorageY(int yXyz, int z, TileScheme scheme);
    //! Convierte el y que devuelve la BD a y logico (XYZ).
    static int fromStorageY(int yStored, int z, TileScheme scheme);

    // --- Rango visible ------------------------------------------------------
    /*!
     * \brief Rejilla de teselas que cubre un rectangulo geografico.
     * \param northWest esquina superior izquierda
     * \param southEast esquina inferior derecha
     * \param z nivel de zoom logico
     * \param margin teselas extra a cada lado (prefetch)
     *
     * Los indices salen ya recortados a [0, 2^z-1], asi que nunca se piden
     * teselas fuera del mundo.
     */
    struct TileRange {
        int z = 0;
        int xMin = 0, xMax = -1;
        int yMin = 0, yMax = -1;
        bool isEmpty() const { return xMax < xMin || yMax < yMin; }
        int count() const {
            return isEmpty() ? 0 : (xMax - xMin + 1) * (yMax - yMin + 1);
        }
        bool contains(const TileKey &k) const {
            return k.z == z && k.x >= xMin && k.x <= xMax
                   && k.y >= yMin && k.y <= yMax;
        }
    };

    static TileRange rangeFor(const QGeoCoordinate &northWest,
                              const QGeoCoordinate &southEast,
                              int z,
                              int margin = 0);

    /*!
     * \brief Nivel de zoom cuya resolucion mejor se ajusta al viewport.
     *
     * Sustituye a CMapaPlot::ValidaZoomActivo, que dependia de un fichero
     * externo (DatosZoom.txt) con una tabla de rangos cableada a mano.
     * Aqui se calcula: se busca el z cuya anchura en pixeles del mundo
     * proyectado mas se acerque a la anchura real del widget.
     */
    int bestZoomFor(double spanLongitudeDeg, int viewportWidthPx,
                    int minZoom, int maxZoom) const;

private:
    int m_tileSize;
};

} // namespace libmapa

#endif // LIBMAPA_GEO_TILEMATRIX_H_
