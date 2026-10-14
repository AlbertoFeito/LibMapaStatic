#include "libmapa/TrackPlayer.h"

#include "libmapa/MapWidget.h"

#include <QFile>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace libmapa {

namespace {

const QString kCapaRutas = QStringLiteral("replay_routes");

// Colores de las pistas, por orden: distinguibles entre si y sobre la base.
QColor colorPista(int i)
{
    static const QColor paleta[] = {
        QColor(0x1e, 0x88, 0xe5), QColor(0xe5, 0x39, 0x35), QColor(0x43, 0xa0, 0x47),
        QColor(0xfb, 0x8c, 0x00), QColor(0x8e, 0x24, 0xaa), QColor(0x00, 0xac, 0xc1),
        QColor(0x6d, 0x4c, 0x41), QColor(0xd8, 0x1b, 0x60)
    };
    return paleta[i % int(sizeof(paleta) / sizeof(paleta[0]))];
}

} // namespace

// Fotogramas cada 100 ms (10 fps): fluido para objetivos que se mueven
// despacio en pantalla y barato con decenas de pistas.
TrackPlayer::TrackPlayer(MapWidget *map, QObject *parent)
    : QObject(parent)
    , m_map(map)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &TrackPlayer::tick);
}

// Deja el mapa como estaba: sin sus objetivos ni su capa de rutas.
TrackPlayer::~TrackPlayer()
{
    m_timer.stop();
    resetTargets();
    if (m_map && m_routesVisible)
        m_map->removeFeatureLayer(kCapaRutas);
}

// Lee los tracks con hora del fichero y los carga.
bool TrackPlayer::loadFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("No se pudo abrir %1: %2").arg(path, f.errorString());
        return false;
    }
    QString motivo;
    const QVector<GpxTrack> t = parseGpxTracks(f.readAll(), &motivo);
    if (t.isEmpty() || !trackTimeRange(t, nullptr, nullptr)) {
        if (error)
            *error = motivo.isEmpty() ? QStringLiteral("%1: ningun track tiene horas").arg(path)
                                      : motivo;
        return false;
    }
    setTracks(t);
    return true;
}

// Cambia las pistas: quita lo anterior del mapa y se coloca al principio (sin
// reproducir), con los objetivos que existan en ese instante ya puestos.
void TrackPlayer::setTracks(const QVector<GpxTrack> &tracks)
{
    pause();
    resetTargets();
    m_tracks = tracks;
    m_ids = QVector<qint64>(m_tracks.size(), -1);
    m_fed = QVector<int>(m_tracks.size(), -1);
    trackTimeRange(m_tracks, &m_start, &m_end);
    m_time = QDateTime();
    syncRoutes();
    if (m_start.isValid())
        advanceTo(m_start);
}

// Factor de velocidad; valores no positivos o no finitos se ignoran.
void TrackPlayer::setSpeed(double factor)
{
    if (factor > 0.0 && std::isfinite(factor))
        m_speed = factor;
}

// Objetivo de la pista en este instante, o -1.
qint64 TrackPlayer::targetIdOf(int index) const
{
    return index >= 0 && index < m_ids.size() ? m_ids.at(index) : -1;
}

// Empieza (o sigue) reproduciendo. Desde el final, vuelve a empezar.
void TrackPlayer::play()
{
    if (m_tracks.isEmpty() || !m_start.isValid() || isPlaying())
        return;
    if (!m_time.isValid() || m_time >= m_end)
        seek(m_start);
    m_clock.start();
    m_timer.start();
    emit playingChanged(true);
}

// Pausa sin moverse.
void TrackPlayer::pause()
{
    if (!isPlaying())
        return;
    m_timer.stop();
    emit playingChanged(false);
}

// Pausa y vuelve al principio.
void TrackPlayer::stop()
{
    pause();
    if (m_start.isValid())
        seek(m_start);
}

// Salto a una hora. Hacia atras se rehacen los objetivos desde su origen (su
// traza y su historial deben ser los de ese momento, no los de un futuro que
// aun "no ha pasado"); hacia delante basta con avanzar.
void TrackPlayer::seek(const QDateTime &time)
{
    if (!m_start.isValid() || !time.isValid())
        return;
    const QDateTime t = std::clamp(time, m_start, m_end);
    if (m_time.isValid() && t < m_time)
        resetTargets();
    advanceTo(t);
}

// Quita todo del mapa y olvida las pistas.
void TrackPlayer::clear()
{
    pause();
    resetTargets();
    m_tracks.clear();
    m_ids.clear();
    m_fed.clear();
    m_start = m_end = m_time = QDateTime();
    syncRoutes();
}

// Fotograma: avanza el tiempo de la reproduccion lo que manda la velocidad
// segun el tiempo REAL transcurrido (no el teorico del temporizador, que se
// retrasa si el equipo va cargado). Al llegar al final se para.
void TrackPlayer::tick()
{
    const qint64 realMs = m_clock.restart();
    const qint64 paso = qint64(std::llround(double(realMs) * m_speed));
    const QDateTime t = std::min(m_time.addMSecs(std::max<qint64>(paso, 1)), m_end);
    advanceTo(t);
    if (t >= m_end) {
        pause();
        emit finished();
    }
}

// Lleva cada pista a \a time. Una pista que empieza a existir se da de alta en
// su origen (con su hora) y se le pasan uno a uno los puntos grabados ya
// alcanzados, con sus horas: asi su traza, su historial y las alertas ven el
// recorrido real, no saltos. Despues, la posicion interpolada del instante.
// Una pista que deja de existir se quita del mapa.
void TrackPlayer::advanceTo(const QDateTime &time)
{
    if (!m_map)
        return;
    m_time = time;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const GpxTrack &pista = m_tracks.at(i);
        const TrackSample s = sampleTrack(pista, time);
        if (!s.active) {
            if (m_ids[i] >= 0)
                m_map->removeTarget(m_ids[i]);
            m_ids[i] = -1;
            m_fed[i] = -1;
            continue;
        }
        for (int k = m_fed[i] + 1; k <= s.pointIndex; ++k) {
            const TargetTrackPoint &p = pista.points.at(k);
            if (!p.position.isValid() || !p.time.isValid())
                continue;
            if (m_ids[i] < 0) {
                MapTarget t;
                t.position = p.position;
                t.label = pista.name.isEmpty() ? tr("Pista %1").arg(i + 1) : pista.name;
                t.kind = pista.type;
                t.color = colorPista(i);
                t.attributes.insert(QStringLiteral("replay"), true);
                if (pista.targetId >= 0)
                    t.attributes.insert(QStringLiteral("target_id"), pista.targetId);
                m_ids[i] = m_map->addTarget(t, p.time);
            } else {
                const auto prev = m_map->target(m_ids[i]);
                const double rumbo = prev && prev->position.distanceTo(p.position) > 0.0
                    ? prev->position.azimuthTo(p.position)
                    : std::numeric_limits<double>::quiet_NaN();
                m_map->updateTarget(m_ids[i], p.position, rumbo, p.time);
            }
        }
        m_fed[i] = std::max(m_fed[i], s.pointIndex);
        if (m_ids[i] >= 0)
            m_map->updateTarget(m_ids[i], s.position, s.headingDeg, time);
    }
    emit timeChanged(time);
}

// Quita del mapa los objetivos que puso el reproductor.
void TrackPlayer::resetTargets()
{
    for (int i = 0; i < m_ids.size(); ++i) {
        if (m_ids[i] >= 0 && m_map)
            m_map->removeTarget(m_ids[i]);
        m_ids[i] = -1;
        m_fed[i] = -1;
    }
}

// Muestra u oculta las rutas completas.
void TrackPlayer::setRoutesVisible(bool on)
{
    m_routesVisible = on;
    syncRoutes();
}

// Rehace la capa de rutas: una linea por pista con su color, marcada como
// track GPX (gpx_kind) y con sus horas de inicio y fin, para que el mapa
// señale y rotule su primer y su ultimo punto. La capa es temporal: no se
// guarda con las entidades del usuario.
void TrackPlayer::syncRoutes()
{
    if (!m_map)
        return;
    m_map->removeFeatureLayer(kCapaRutas);
    if (!m_routesVisible || m_tracks.isEmpty())
        return;
    m_map->addFeatureLayer(kCapaRutas, tr("Rutas reproducidas"), -10);
    m_map->setFeatureLayerTransient(kCapaRutas, true);
    for (int i = 0; i < m_tracks.size(); ++i) {
        const GpxTrack &pista = m_tracks.at(i);
        MapFeature f;
        f.kind = GeometryKind::Polyline;
        f.layerId = kCapaRutas;
        f.name = pista.name;
        f.type = pista.type;
        QDateTime ini, fin;
        for (const TargetTrackPoint &p : pista.points) {
            if (!p.position.isValid())
                continue;
            f.geometry.append(p.position);
            if (p.time.isValid()) {
                if (!ini.isValid())
                    ini = p.time;
                fin = p.time;
            }
        }
        if (f.geometry.size() < 2)
            continue;
        f.attributes.insert(QStringLiteral("gpx_kind"), QStringLiteral("trk"));
        if (ini.isValid())
            f.attributes.insert(QStringLiteral("time_start"), ini.toUTC().toString(Qt::ISODateWithMs));
        if (fin.isValid())
            f.attributes.insert(QStringLiteral("time_end"), fin.toUTC().toString(Qt::ISODateWithMs));
        QColor c = colorPista(i);
        f.style.lineColor = QColor(c.red(), c.green(), c.blue(), 150);
        f.style.lineWidth = 1.5;
        f.style.lineStyle = Qt::DashLine;
        f.style.labelVisible = false;
        m_map->addFeature(f);
    }
}

} // namespace libmapa
