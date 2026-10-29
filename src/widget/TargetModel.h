#ifndef LIBMAPA_WIDGET_TARGETMODEL_H_
#define LIBMAPA_WIDGET_TARGETMODEL_H_

#include "libmapa/MapTarget.h"

#include <QHash>
#include <QObject>
#include <QVariant>
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
    //! Punto del historial guardado de forma compacta (24 bytes): con miles
    //! de objetivos y horas de seguimiento, QGeoCoordinate+QDateTime por punto
    //! costaria varias veces mas memoria.
    struct HistoryPoint {
        double lat = 0.0;
        double lon = 0.0;
        qint64 msecs = 0;      //!< ms desde epoch, UTC
    };

    struct Entry {
        MapTarget target;
        QVector<QGeoCoordinate> trail;     //!< Traza corta que se DIBUJA (acotada)
        QVector<HistoryPoint> history;     //!< Historial completo desde el origen
        qint64 lastMsecs = 0;              //!< Hora de la ultima posicion (ms, UTC)
    };

    /*!
     * \brief Da de alta o reemplaza un objetivo (con toda su metadata).
     *
     * Si \a target.id es < 0 se asigna uno. La posicion inicial abre la traza.
     * Devuelve el identificador, o -1 si la posicion no es valida.
     */
    qint64 upsert(MapTarget target, const QDateTime &time = QDateTime());

    /*!
     * \brief Actualiza SOLO la posicion (y opcionalmente el rumbo) de un
     *        objetivo ya existente, anadiendo el punto a su traza.
     *
     * Es la via rapida del flujo en tiempo real. Devuelve false si el objetivo
     * no existe o la posicion no es valida. Con \a headingDeg NaN se conserva
     * el rumbo anterior.
     */
    bool update(qint64 id, const QGeoCoordinate &position,
                double headingDeg = std::numeric_limits<double>::quiet_NaN(),
                const QDateTime &time = QDateTime());

    bool setLabel(qint64 id, const QString &text);
    //! Desplazamiento de la etiqueta respecto al simbolo (px). false si no existe.
    //! Lo marca como PROPIO del objetivo (MapTarget::labelOffsetSet).
    bool setLabelOffset(qint64 id, const QPointF &offsetPx);
    //! Quita el desplazamiento propio: el objetivo vuelve al por defecto. false si
    //! no existe.
    bool clearLabelOffset(qint64 id);
    //! Restaura el estado EXACTO del desplazamiento propio (valor y marca de
    //! fijado). Lo usa el arrastre al cancelarse. false si no existe.
    bool restoreLabelOffset(qint64 id, const QPointF &offsetPx, bool set);
    //! Desplazamiento por defecto de los objetivos que no fijaron el suyo; (0,0)
    //! al principio (etiqueta pegada al simbolo).
    QPointF defaultLabelOffsetPx() const { return m_defaultLabelOffset; }
    void setDefaultLabelOffsetPx(const QPointF &offsetPx);
    //! Desplazamiento que se dibuja de \a t: el suyo si lo fijo (o si es distinto
    //! de (0,0), por compatibilidad) y, si no, el por defecto.
    QPointF effectiveLabelOffset(const MapTarget &t) const
    {
        return (t.labelOffsetSet || !t.labelOffsetPx.isNull()) ? t.labelOffsetPx
                                                              : m_defaultLabelOffset;
    }
    bool setColor(qint64 id, const QColor &color);

    //! Fija (o reemplaza) un atributo libre del objetivo. Via para colgar datos
    //! de dominio en caliente (AIS, ADS-B, telemetria) sin que el modelo los
    //! interprete. false si el id no existe.
    bool setAttribute(qint64 id, const QString &key, const QVariant &value);
    //! Valor de un atributo del objetivo, o QVariant() invalido si no existe el
    //! id o la clave.
    QVariant attribute(qint64 id, const QString &key) const;

    bool remove(qint64 id);
    void clear();
    void clearTrail(qint64 id);

    std::optional<MapTarget> target(qint64 id) const;
    QVector<MapTarget> targets() const;
    QVector<QGeoCoordinate> trail(qint64 id) const;

    /*!
     * \brief Historial completo del objetivo, desde que se vio por primera vez
     *        (el primer punto es el ORIGEN) hasta su posicion actual.
     *
     * Se guarda mientras el objetivo exista, aparte de la traza que se dibuja
     * (\ref setTrailMaxPoints). Para acotar la memoria: no se anota un punto a
     * menos de \ref historyMinDistance metros del anterior y, si se pasa de
     * \ref historyMaxPoints, se aclaran los puntos intermedios (uno de cada
     * dos) — el origen y el ultimo punto NUNCA se pierden.
     */
    QVector<TargetTrackPoint> history(qint64 id) const;
    //! Origen del objetivo (primer punto del historial); invalido si no existe.
    TargetTrackPoint origin(qint64 id) const;
    int historyMaxPoints() const { return m_historyMax; }
    //! Tope de puntos del historial por objetivo (minimo 2; por defecto 5000).
    void setHistoryMaxPoints(int maxPoints);
    double historyMinDistance() const { return m_historyMinDist; }
    //! Distancia minima (m) entre puntos anotados (por defecto 1 m; 0 = todos).
    void setHistoryMinDistance(double meters);
    //! Hora (ms desde epoch, UTC) de la ultima posicion recibida; 0 si no existe.
    qint64 lastUpdateMsecs(qint64 id) const;

    /*!
     * \brief Velocidad (m/s) y rumbo (grados) estimados por la propia traza.
     *
     * Desplazamiento desde el punto del historial mas antiguo dentro de los
     * ultimos \a windowMs hasta la posicion actual, entre el tiempo
     * transcurrido. Si no hay ningun punto en la ventana se usa el ultimo
     * anterior; si la posicion no ha cambiado desde el, el objetivo esta
     * parado (velocidad 0). false si aun no hay con que estimar (recien
     * creado) o el id no existe.
     */
    bool estimateVelocity(qint64 id, qint64 windowMs, double *speed, double *course) const;
    bool contains(qint64 id) const { return m_targets.contains(id); }
    int count() const { return static_cast<int>(m_targets.size()); }

    //! Acceso directo para la capa de dibujo, sin copiar. No modificar.
    const QHash<qint64, Entry> &entries() const { return m_targets; }

    //! Longitud maxima de la traza: < 0 = toda (ilimitada), 0 = sin traza,
    //! > 0 = las ultimas N posiciones.
    int trailMaxPoints() const { return m_trailMax; }
    void setTrailMaxPoints(int maxPoints);

signals:
    //! Algo cambio y hay que repintar la capa de objetivos.
    void changed();

private:
    void podarTraza(Entry &e);
    void anotarHistoria(Entry &e, const QGeoCoordinate &pos, const QDateTime &time);
    void aclararHistoria(Entry &e) const;

    QHash<qint64, Entry> m_targets;
    qint64 m_nextId = 1;
    int m_trailMax = 300;
    int m_historyMax = 5000;
    double m_historyMinDist = 1.0;
    QPointF m_defaultLabelOffset;   //!< Para los que no fijaron el suyo; (0,0).
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_TARGETMODEL_H_
