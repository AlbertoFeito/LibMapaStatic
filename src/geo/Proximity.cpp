#include "libmapa/Proximity.h"

#include <QtMath>

#include <cmath>

namespace libmapa {

namespace {

// Radio medio terrestre (el de QGeoCoordinate::distanceTo), para que la
// distancia actual que da el CPA cuadre con la del resto de la libreria.
constexpr double kRadioTierra = 6371007.2;

// Componentes este/norte (m/s) de una velocidad con rumbo en grados.
void componentes(double velocidad, double rumbo, double &este, double &norte)
{
    este = velocidad * std::sin(qDegreesToRadians(rumbo));
    norte = velocidad * std::cos(qDegreesToRadians(rumbo));
}

} // namespace

// Plano tangente en la latitud media: B relativo a A en metros (este, norte) y
// velocidad relativa. El tiempo del CPA minimiza |r + v t|: t = -(r.v)/|v|^2.
// Las posiciones en el CPA se calculan sobre la esfera con el rumbo de cada uno.
CpaResult computeCpa(const QGeoCoordinate &a, double speedA, double courseA,
                     const QGeoCoordinate &b, double speedB, double courseB)
{
    CpaResult r;
    if (!a.isValid() || !b.isValid() || !std::isfinite(speedA) || !std::isfinite(speedB)
        || !std::isfinite(courseA) || !std::isfinite(courseB))
        return r;

    double dLon = b.longitude() - a.longitude();
    if (dLon > 180.0)
        dLon -= 360.0;
    else if (dLon < -180.0)
        dLon += 360.0;
    const double latMedia = qDegreesToRadians((a.latitude() + b.latitude()) / 2.0);
    const double rx = kRadioTierra * std::cos(latMedia) * qDegreesToRadians(dLon);
    const double ry = kRadioTierra * qDegreesToRadians(b.latitude() - a.latitude());

    double vax, vay, vbx, vby;
    componentes(speedA, courseA, vax, vay);
    componentes(speedB, courseB, vbx, vby);
    const double vx = vbx - vax, vy = vby - vay;
    const double v2 = vx * vx + vy * vy;

    // Con velocidad relativa practicamente nula (< 1 mm/s) la distancia no
    // cambia: el CPA es "ahora".
    const double t = v2 < 1e-6 ? 0.0 : -(rx * vx + ry * vy) / v2;
    const double cx = rx + vx * t, cy = ry + vy * t;

    r.valid = true;
    r.timeSeconds = t;
    r.distanceMeters = std::hypot(cx, cy);
    r.currentDistanceMeters = a.distanceTo(b);
    // Rumbo y distancia recorrida hasta el CPA (hacia atras si ya paso).
    const auto avanzar = [t](const QGeoCoordinate &p, double v, double rumbo) {
        const double d = v * t;
        return d >= 0.0 ? p.atDistanceAndAzimuth(d, rumbo)
                        : p.atDistanceAndAzimuth(-d, rumbo + 180.0);
    };
    r.positionA = avanzar(a, speedA, courseA);
    r.positionB = avanzar(b, speedB, courseB);
    return r;
}

} // namespace libmapa
