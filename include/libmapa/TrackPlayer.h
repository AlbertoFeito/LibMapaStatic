#ifndef LIBMAPA_TRACKPLAYER_H_
#define LIBMAPA_TRACKPLAYER_H_

#include "libmapa/TrackReplay.h"
#include "libmapa/VectorFormats.h"
#include "libmapa/libmapa_export.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

namespace libmapa {

class MapWidget;

/*!
 * \brief Reproduce en el tiempo historiales grabados (p. ej. un GPX exportado
 *        con MapWidget::exportTargetHistory).
 *
 * Cada pista se convierte en un OBJETIVO del mapa mientras exista, entre su
 * primer y su ultimo punto: aparece en su origen, se mueve interpolando entre
 * los puntos grabados y desaparece al acabar. Como son objetivos normales,
 * funciona todo lo demas sin hacer nada: iconos (por su \c type como kind),
 * traza, historial con su origen al seleccionarlo, alertas de zona y de
 * proximidad (con las horas del fichero) y anillos o marcaciones colgados de
 * ellos.
 *
 * - play / pause / stop (vuelve al principio) y seek a cualquier hora; al ir
 *   hacia atras se rehace cada objetivo desde su origen, para que su traza y
 *   su historial sean los de ese momento.
 * - Velocidad: factor sobre el tiempo real (1 = tiempo real, 60 = un minuto
 *   por segundo...).
 * - Rutas (\ref setRoutesVisible): cada pista completa como linea en una capa
 *   temporal (no se guarda), con su inicio y su fin marcados y rotulados.
 *
 * Solo toca los objetivos que crea; los demas del mapa siguen como estaban.
 */
class LIBMAPA_EXPORT TrackPlayer : public QObject
{
    Q_OBJECT

public:
    explicit TrackPlayer(MapWidget *map, QObject *parent = nullptr);
    //! Quita del mapa sus objetivos y sus rutas.
    ~TrackPlayer() override;

    //! Carga los tracks con hora de un GPX (parseGpxTracks). false con el
    //! motivo en \a error si no se puede leer o no hay ninguno con horas.
    bool loadFile(const QString &path, QString *error = nullptr);
    //! Pistas a reproducir; deja la reproduccion parada al principio.
    void setTracks(const QVector<GpxTrack> &tracks);
    QVector<GpxTrack> tracks() const { return m_tracks; }
    //! ¿Hay un historial cargado? Es el "modo reproduccion": de true a false
    //! solo con \ref clear (o al destruir el reproductor).
    bool isLoaded() const { return !m_tracks.isEmpty(); }

    QDateTime startTime() const { return m_start; }
    QDateTime endTime() const { return m_end; }
    QDateTime currentTime() const { return m_time; }
    bool isPlaying() const { return m_timer.isActive(); }

    //! Factor de velocidad (> 0; por defecto 60: un minuto por segundo).
    void setSpeed(double factor);
    double speed() const { return m_speed; }

    //! Objetivo del mapa que representa la pista \a index ahora (-1 si no
    //! esta activa en el instante actual).
    qint64 targetIdOf(int index) const;

    //! Rutas completas como lineas en una capa temporal "replay_routes".
    void setRoutesVisible(bool on);
    bool routesVisible() const { return m_routesVisible; }

public slots:
    void play();
    void pause();
    //! Pausa y vuelve al principio.
    void stop();
    //! Lleva la reproduccion a \a time (se recorta al rango de las pistas).
    void seek(const QDateTime &time);
    //! SALIR de la reproduccion: para, quita del mapa sus objetivos y sus
    //! rutas, y olvida las pistas. Emite \ref cleared.
    void clear();

signals:
    void timeChanged(const QDateTime &time);
    void playingChanged(bool playing);
    //! Se llego al final reproduciendo.
    void finished();
    //! Se salio de la reproduccion (\ref clear): el mapa ya no tiene nada suyo.
    void cleared();

private:
    void tick();
    void advanceTo(const QDateTime &time);
    void resetTargets();
    void syncRoutes();

    QPointer<MapWidget> m_map;
    QVector<GpxTrack> m_tracks;
    QVector<qint64> m_ids;           //!< Objetivo de cada pista (-1 = no esta).
    QVector<int> m_fed;              //!< Ultimo punto grabado ya pasado al mapa.
    QDateTime m_start, m_end, m_time;
    double m_speed = 60.0;
    bool m_routesVisible = false;
    QTimer m_timer;
    QElapsedTimer m_clock;           //!< Tiempo real entre fotogramas.
};

} // namespace libmapa

#endif // LIBMAPA_TRACKPLAYER_H_
