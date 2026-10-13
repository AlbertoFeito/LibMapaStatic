#ifndef LIBMAPA_GEO_ZONEMONITOR_H_
#define LIBMAPA_GEO_ZONEMONITOR_H_

#include <QGeoCoordinate>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QVector>

namespace libmapa {

/*!
 * \brief Que objetivos estan dentro de que zonas, y cuando entran o salen.
 *
 * Es la logica de las alertas de zona de MapWidget, separada del widget (como
 * TargetModel u OverlayModel) para poder probarla sin ventanas. No sabe nada de
 * entidades ni de objetivos: solo de identificadores, posiciones y contornos.
 *
 * Una zona es uno o varios anillos (las partes de un poligono multi-parte). Un
 * punto esta dentro si cae dentro de CUALQUIERA de ellos. La prueba se hace en
 * coordenadas Web Mercator, que es como se dibujan los poligonos: lo que se ve
 * dentro en el mapa es lo que cuenta como dentro, aunque el lado sea largo y
 * este inclinado. No se tratan zonas que cruzan el antimeridiano (+-180).
 *
 * Cada metodo que puede cambiar la pertenencia devuelve las transiciones que
 * provoca; quien lo usa decide que hacer con ellas (MapWidget emite senales).
 * Coste por actualizacion de objetivo: O(zonas), con un descarte rapido por
 * caja envolvente antes de recorrer los lados.
 */
class ZoneMonitor
{
public:
    //! Un cambio de pertenencia: \a targetId entro (o salio) de \a zoneId.
    struct Transition {
        qint64 targetId = -1;
        qint64 zoneId = -1;
        bool entered = false;

        bool operator==(const Transition &o) const
        {
            return targetId == o.targetId && zoneId == o.zoneId && entered == o.entered;
        }
    };

    /*!
     * \brief Da de alta una zona o le cambia el contorno.
     *
     * Se reevalua contra las posiciones de todos los objetivos conocidos y se
     * devuelven las entradas y salidas resultantes. Una zona nueva empieza
     * vacia, asi que los objetivos que ya estaban dentro producen una ENTRADA:
     * vigilar una zona avisa de lo que ya hay en ella. Anillos con menos de 3
     * vertices validos se ignoran; si no queda ninguno, la zona se quita (con
     * las salidas que toque).
     */
    QVector<Transition> setZone(qint64 zoneId,
                                const QVector<QVector<QGeoCoordinate>> &rings);

    //! Quita una zona SIN producir salidas (quien la quita ya lo sabe).
    //! false si no existia.
    bool removeZone(qint64 zoneId);
    bool hasZone(qint64 zoneId) const { return m_zones.contains(zoneId); }
    QVector<qint64> zones() const;
    //! Contorno guardado de una zona (vacio si no existe). Sirve para saber si
    //! la geometria de la entidad cambio y hay que reevaluar.
    QVector<QVector<QGeoCoordinate>> zoneRings(qint64 zoneId) const;

    /*!
     * \brief Nueva posicion de un objetivo (alta o movimiento).
     *
     * Devuelve las entradas y salidas que provoca. Una posicion invalida saca
     * al objetivo de todas sus zonas.
     */
    QVector<Transition> updateTarget(qint64 targetId, const QGeoCoordinate &position);

    //! Olvida un objetivo SIN producir salidas (se ha borrado, no se ha ido).
    void removeTarget(qint64 targetId);
    //! Olvida todos los objetivos, sin producir salidas.
    void clearTargets();

    //! Objetivos que estan ahora dentro de la zona (vacio si no existe).
    QVector<qint64> targetsIn(qint64 zoneId) const;
    //! Zonas en las que esta ahora el objetivo.
    QVector<qint64> zonesOf(qint64 targetId) const;
    bool isInside(qint64 targetId, qint64 zoneId) const;

private:
    struct Zone {
        QVector<QVector<QGeoCoordinate>> rings;  //!< Tal cual llegaron.
        QVector<QVector<QPointF>> projected;     //!< En Web Mercator (lon, y en grados).
        QRectF bounds;                           //!< Caja de \a projected.
        QSet<qint64> inside;                     //!< Objetivos dentro.
    };

    static bool contains(const Zone &z, const QPointF &p);
    static QPointF project(const QGeoCoordinate &c);

    QHash<qint64, Zone> m_zones;
    QHash<qint64, QGeoCoordinate> m_positions;   //!< Ultima posicion valida.
};

} // namespace libmapa

#endif // LIBMAPA_GEO_ZONEMONITOR_H_
