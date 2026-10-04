#include "dem/ElevationAnalysis.h"

#include "geo/GeoMath.h"

#include <cmath>

namespace libmapa {

namespace {

// Añade una muestra al perfil: fija distancia acumulada, posicion y cota, y
// actualiza min/max/ganancia/perdida respecto a la ultima muestra CON dato
// (las muestras sin dato -NaN- no rompen las estadisticas).
void anadirMuestra(ElevationProfile &perfil, double distancia,
                   const QGeoCoordinate &pos, double cota, double &ultimaCota)
{
    ElevationSample m;
    m.distanceM = distancia;
    m.position = pos;
    m.elevation = cota;
    perfil.samples.append(m);

    if (std::isnan(cota))
        return;

    if (std::isnan(perfil.minElevation) || cota < perfil.minElevation)
        perfil.minElevation = cota;
    if (std::isnan(perfil.maxElevation) || cota > perfil.maxElevation)
        perfil.maxElevation = cota;

    if (!std::isnan(ultimaCota)) {
        const double d = cota - ultimaCota;
        if (d > 0.0) perfil.gain += d;
        else         perfil.loss += -d;
    }
    ultimaCota = cota;
}

} // namespace

// Perfil de elevacion a lo largo de una polilinea. El muestreo es uniforme a lo
// largo de TODA la ruta: se lleva un "resto" (distanciaAlProximo) entre tramos
// para que el paso no se reinicie en cada vertice. Siempre entra el primer punto
// y el ultimo exacto.
ElevationProfile elevationProfile(const IElevationSource &src,
                                  const QVector<QGeoCoordinate> &path,
                                  const ElevationProfileParams &params)
{
    ElevationProfile perfil;

    // Nos quedamos solo con los vertices validos, en orden.
    QVector<QGeoCoordinate> pts;
    pts.reserve(path.size());
    for (const QGeoCoordinate &c : path)
        if (c.isValid())
            pts.append(c);
    if (pts.size() < 2)
        return perfil;

    const double paso = params.stepMeters > 0.0 ? params.stepMeters : 30.0;
    double ultimaCota = std::numeric_limits<double>::quiet_NaN();
    double distGlobal = 0.0;

    // Primer punto.
    anadirMuestra(perfil, 0.0, pts.first(),
                  src.elevationAt(pts.first()), ultimaCota);

    double distanciaAlProximo = paso;   // cuanto falta para la siguiente muestra
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const QGeoCoordinate &a = pts[i];
        const QGeoCoordinate &b = pts[i + 1];
        const double largoTramo = GeoMath::distanceMeters(a, b);
        if (largoTramo <= 0.0)
            continue;
        const double azimut = GeoMath::azimuthDegrees(a, b);

        // Coloca muestras dentro del tramo mientras quepan.
        double offset = distanciaAlProximo;
        while (offset < largoTramo) {
            const QGeoCoordinate p = a.atDistanceAndAzimuth(offset, azimut);
            anadirMuestra(perfil, distGlobal + offset, p,
                          src.elevationAt(p), ultimaCota);
            offset += paso;
        }
        // Lo que sobra del paso se arrastra al siguiente tramo.
        distanciaAlProximo = offset - largoTramo;
        distGlobal += largoTramo;
    }

    // El ultimo vertice exacto (si no cayo justo en una muestra).
    if (perfil.samples.isEmpty()
        || perfil.samples.last().position != pts.last()) {
        anadirMuestra(perfil, distGlobal, pts.last(),
                      src.elevationAt(pts.last()), ultimaCota);
    }

    perfil.totalDistanceM = distGlobal;
    return perfil;
}

} // namespace libmapa
