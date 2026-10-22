#ifndef LIBMAPA_VECTOR_ROADGRAPH_H_
#define LIBMAPA_VECTOR_ROADGRAPH_H_

#include "libmapa/Places.h"
#include "tiles/TileKey.h"
#include "vector/MvtTile.h"

#include <QGeoCoordinate>
#include <QHash>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Red vial armada con las teselas vectoriales (capa `transportation` de
 *        OpenMapTiles) y búsqueda de la ruta más rápida en coche. Solo núcleo.
 *
 * Una tesela solo trae un trozo de cada vía, y los trozos de teselas vecinas no
 * coinciden exactamente: cada tesela redondea sus coordenadas a su propia rejilla (a
 * z14 unos 0,6 m) y trae un margen que repite lo de la vecina. Por eso el grafo
 * SUELDA los vértices que quedan a menos de 1,5 m (4 m si son extremos de una vía,
 * que es donde se juntan las calles), y una vía partida en teselas vuelve a ser una.
 */
namespace roadgraph {

//! Capa del MVT de la que sale la red vial.
extern const char *const kLayer;

//! Un tramo de vía tal como viene en UNA tesela.
struct Segment
{
    QVector<QGeoCoordinate> line;
    QString cls;            //!< `class`: "motorway", "primary", "minor"…
    int oneway = 0;         //!< 1 = solo en el sentido del trazado, -1 = en el contrario, 0 = doble sentido.
    bool blocked = false;   //!< `access` = no / private: no se entra en coche.
    int level = 0;          //!< 0 a nivel, 1 puente, 2 túnel/paso inferior: un puente no cruza lo de abajo.
};

//! Los tramos de la capa `transportation` de \a tile (que es \a key), con su geometría geográfica.
QVector<Segment> inTile(const mvt::Tile &tile, const TileKey &key);

//! Velocidad media (km/h) con que se estima el tiempo en coche por un tipo de vía;
//! 0 si el coche no puede ir (sendero, vía de tren, ferri…).
double carSpeedKmh(const QString &cls);

/*!
 * \brief El grafo. Se le dan los tramos (de una o de muchas teselas) y se le pide la ruta.
 *
 * \a refLatDeg es la latitud aproximada de la zona (para convertir grados a metros).
 */
class Graph
{
public:
    explicit Graph(double refLatDeg);

    //! Añade tramos: los vértices cercanos a los ya puestos se sueldan.
    void add(const QVector<Segment> &segments);

    int nodeCount() const { return int(m_nodes.size()); }

    /*!
     * \brief La ruta más rápida en coche de \a from a \a to.
     *
     * Ambos puntos se enganchan al vértice de vía más cercano (hasta \a maxSnapM m). Respeta
     * el sentido único y las vías cerradas al coche. Con error en \c RouteResult::error si
     * un punto queda lejos de toda vía o si no hay camino (tramos sin unir, otra isla…).
     */
    RouteResult route(const QGeoCoordinate &from, const QGeoCoordinate &to, double maxSnapM = 500.0) const;

private:
    struct Node
    {
        double lat = 0, lon = 0;
        quint8 levels = 0;          //!< bits de nivel (0 suelo, 1 puente, 2 túnel) de lo que pasa por aquí
        bool bridgeEnd = false;     //!< extremo de un puente/túnel: ahí se une con el suelo
        bool onRoad = false;        //!< algún tramo transitable acaba o pasa por aquí (aunque sea solo de entrada)
    };
    struct Edge
    {
        int to = 0;
        float lengthM = 0;
        float timeS = 0;
        quint8 cls = 0;             //!< índice en la lista de clases
    };

    int nodeFor(const QGeoCoordinate &c, int level, bool endpoint);
    qint64 cellKey(double lat, double lon) const;
    double meters(double lat1, double lon1, double lat2, double lon2) const;
    int classIndex(const QString &cls);

    double m_cosRef;
    QVector<Node> m_nodes;
    QVector<QVector<Edge>> m_adj;
    QHash<qint64, QVector<int>> m_cells;
    QStringList m_classes;
};

} // namespace roadgraph

} // namespace libmapa

#endif // LIBMAPA_VECTOR_ROADGRAPH_H_
