#ifndef LIBMAPA_WIDGET_TARGETMODEL_H_
#define LIBMAPA_WIDGET_TARGETMODEL_H_

#include "libmapa/MapTarget.h"

#include <QHash>
#include <QObject>
#include <QVector>
#include <limits>
#include <optional>

namespace libmapa {

/*!
 * \brief Guarda los objetivos moviles y su traza. Sin dibujar nada.
 *
 * Separado del dibujo igual que OverlayModel: asi se puede probar toda la
 * gestion (altas, actualizaciones, poda de la traza) sin ventanas.
 *
 * Pensado para cientos de objetivos actualizandose varias veces por segundo:
 * las operaciones son O(1) por objetivo y la traza esta acotada.
 */
class TargetModel : public QObject
{
    Q_OBJECT

public:
    explicit TargetModel(QObject *parent = nullptr);

    //! Estado guardado: el objetivo mas su traza (posiciones recientes).
    struct Entry {
        MapTarget target;
        QVector<QGeoCoordinate> trail;
    };

    /*!
     * \brief Da de alta o reemplaza un objetivo (con toda su metadata).
     *
     * Si \a target.id es < 0 se asigna uno. La posicion inicial abre la traza.
     * Devuelve el identificador, o -1 si la posicion no es valida.
     */
    qint64 upsert(MapTarget target);

    /*!
     * \brief Actualiza SOLO la posicion (y opcionalmente el rumbo) de un
     *        objetivo ya existente, anadiendo el punto a su traza.
     *
     * Es la via rapida del flujo en tiempo real. Devuelve false si el objetivo
     * no existe o la posicion no es valida. Con \a headingDeg NaN se conserva
     * el rumbo anterior.
     */
    bool update(qint64 id, const QGeoCoordinate &position,
                double headingDeg = std::numeric_limits<double>::quiet_NaN());

    bool setLabel(qint64 id, const QString &text);
    bool setColor(qint64 id, const QColor &color);
    bool remove(qint64 id);
    void clear();
    void clearTrail(qint64 id);

    std::optional<MapTarget> target(qint64 id) const;
    QVector<MapTarget> targets() const;
    QVector<QGeoCoordinate> trail(qint64 id) const;
    bool contains(qint64 id) const { return m_targets.contains(id); }
    int count() const { return static_cast<int>(m_targets.size()); }

    //! Acceso directo para la capa de dibujo, sin copiar. No modificar.
    const QHash<qint64, Entry> &entries() const { return m_targets; }

    //! Longitud maxima de la traza, en numero de puntos. 0 = sin traza.
    int trailMaxPoints() const { return m_trailMax; }
    void setTrailMaxPoints(int maxPoints);

signals:
    //! Algo cambio y hay que repintar la capa de objetivos.
    void changed();

private:
    void podarTraza(Entry &e);

    QHash<qint64, Entry> m_targets;
    qint64 m_nextId = 1;
    int m_trailMax = 300;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_TARGETMODEL_H_
