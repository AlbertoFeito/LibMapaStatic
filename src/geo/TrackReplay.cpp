#include "libmapa/TrackReplay.h"

#include <algorithm>

namespace libmapa {

namespace {

// ¿Sirve el punto para reproducir? Necesita posicion y hora.
bool util(const TargetTrackPoint &p)
{
    return p.position.isValid() && p.time.isValid();
}

} // namespace

// Se quedan los puntos utiles (con posicion y hora) y se busca por tiempo con
// busqueda binaria el tramo que contiene \a time; dentro, interpolacion por la
// ortodromica. Coste O(n) por el filtrado: de sobra para muestrear unas
// decenas de pistas de miles de puntos a 10 fotogramas por segundo.
TrackSample sampleTrack(const GpxTrack &track, const QDateTime &time)
{
    TrackSample s;
    if (!time.isValid())
        return s;
    QVector<int> idx;
    idx.reserve(track.points.size());
    for (int i = 0; i < track.points.size(); ++i)
        if (util(track.points.at(i)))
            idx.append(i);
    if (idx.isEmpty())
        return s;

    const qint64 t = time.toMSecsSinceEpoch();
    const auto ms = [&](int k) { return track.points.at(idx.at(k)).time.toMSecsSinceEpoch(); };
    if (t < ms(0) || t > ms(int(idx.size()) - 1))
        return s;

    // Primer util con hora > t; el anterior es el ultimo alcanzado.
    int lo = 0, hi = int(idx.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (ms(mid) <= t)
            lo = mid + 1;
        else
            hi = mid;
    }
    const int k = lo - 1;                      // >= 0 porque t >= ms(0)
    const TargetTrackPoint &a = track.points.at(idx.at(k));
    s.active = true;
    s.pointIndex = idx.at(k);
    if (k + 1 >= idx.size()) {                 // justo en el ultimo punto
        s.position = a.position;
        if (k > 0)
            s.headingDeg = track.points.at(idx.at(k - 1)).position.azimuthTo(a.position);
        return s;
    }
    const TargetTrackPoint &b = track.points.at(idx.at(k + 1));
    const double dt = double(ms(k + 1) - ms(k));
    const double f = dt > 0.0 ? double(t - ms(k)) / dt : 0.0;
    const double d = a.position.distanceTo(b.position);
    s.headingDeg = d > 0.0 ? a.position.azimuthTo(b.position)
                           : (k > 0 ? track.points.at(idx.at(k - 1)).position.azimuthTo(a.position)
                                    : s.headingDeg);
    s.position = d > 0.0 ? a.position.atDistanceAndAzimuth(d * f, s.headingDeg) : a.position;
    return s;
}

// Recorre todas las pistas buscando el primer y el ultimo punto con hora.
bool trackTimeRange(const QVector<GpxTrack> &tracks, QDateTime *start, QDateTime *end)
{
    QDateTime ini, fin;
    for (const GpxTrack &t : tracks)
        for (const TargetTrackPoint &p : t.points) {
            if (!util(p))
                continue;
            if (!ini.isValid() || p.time < ini)
                ini = p.time;
            if (!fin.isValid() || p.time > fin)
                fin = p.time;
        }
    if (start)
        *start = ini;
    if (end)
        *end = fin;
    return ini.isValid();
}

} // namespace libmapa
