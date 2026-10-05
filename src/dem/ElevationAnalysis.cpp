#include "dem/ElevationAnalysis.h"

#include "geo/GeoMath.h"

#include <QtMath>

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

// Abombamiento de la Tierra (metros) en un punto intermedio que dista \a d1 de un
// extremo y \a d2 del otro, respecto a la cuerda recta entre ambos: d1·d2/(2·k·R).
// Con radio efectivo k·R (k=4/3 refraccion estandar, k=1 geometrico puro). Es la
// altura que "sube" la superficie sobre la linea recta A-B en ese punto, asi que
// se suma al terreno al comprobar si corta la vision.
double abombamiento(double d1, double d2, double k, double R)
{
    return (d1 * d2) / (2.0 * k * R);
}

// Caida de la superficie terrestre bajo el plano horizontal del observador a
// distancia d: d^2/(2·k·R). Es abombamiento(d, d, k, R), pero con nombre propio
// para el viewshed (la referencia aqui es la tangente en el observador, no la
// cuerda entre dos puntos).
double caida(double d, double k, double R)
{
    return abombamiento(d, d, k, R);
}

// Cota del terreno en \a p, sustituyendo los huecos del DEM por \a voidElev si
// este es finito (p. ej. 0 = mar). Con \a voidElev NaN, devuelve el dato tal cual
// (NaN donde no hay).
double cota(const IElevationSource &src, const QGeoCoordinate &p, double voidElev)
{
    const double t = src.elevationAt(p);
    if (std::isnan(t) && !std::isnan(voidElev))
        return voidElev;
    return t;
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

    const double vacio = params.voidElevation;

    // Primer punto.
    anadirMuestra(perfil, 0.0, pts.first(),
                  cota(src, pts.first(), vacio), ultimaCota);

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
                          cota(src, p, vacio), ultimaCota);
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
                      cota(src, pts.last(), vacio), ultimaCota);
    }

    perfil.totalDistanceM = distGlobal;
    return perfil;
}

// Linea de vision entre dos puntos con altura de antena. Camina el rayo geodesico
// A->B muestreando el terreno cada `paso`; en cada muestra compara la altura de la
// recta entre las cimas de antena con la del terreno MAS el abombamiento de la
// Tierra en ese punto (el terreno "sube" sobre la cuerda). La holgura es recta -
// (terreno+abombamiento); su minimo a lo largo del trayecto decide si hay vision.
// Las muestras sin dato (NaN) se saltan (no se puede afirmar nada ahi).
LineOfSightResult lineOfSight(const IElevationSource &src,
                              const QGeoCoordinate &a, const QGeoCoordinate &b,
                              double antennaA, double antennaB,
                              const LineOfSightParams &params)
{
    LineOfSightResult r;
    if (!a.isValid() || !b.isValid())
        return r;                                   // sin geometria
    const double D = GeoMath::distanceMeters(a, b);
    if (D <= 0.0)
        return r;

    // Anclaje en los extremos: sin cota en A o B no se puede trazar la recta
    // (salvo que voidElevation sustituya el hueco, p. ej. mar = 0).
    const double terrA = cota(src, a, params.voidElevation);
    const double terrB = cota(src, b, params.voidElevation);
    if (std::isnan(terrA) || std::isnan(terrB))
        return r;
    const double zA = terrA + antennaA;             // cima de antena A (absoluta)
    const double zB = terrB + antennaB;             // cima de antena B

    const double paso = params.stepMeters > 0.0 ? params.stepMeters : 30.0;
    const double k = params.k > 0.0 ? params.k : 4.0 / 3.0;
    const double R = params.earthRadiusM > 0.0 ? params.earthRadiusM : 6371000.0;
    const double azimut = GeoMath::azimuthDegrees(a, b);

    double minHolgura = std::numeric_limits<double>::infinity();
    QGeoCoordinate minPos;
    double minDist = 0.0;
    for (double d = paso; d < D; d += paso) {
        const QGeoCoordinate p = a.atDistanceAndAzimuth(d, azimut);
        const double t = cota(src, p, params.voidElevation);
        if (std::isnan(t))
            continue;                               // hueco: no decide
        const double recta = zA + (zB - zA) * (d / D);
        const double bulge = params.curvature ? abombamiento(d, D - d, k, R) : 0.0;
        const double holgura = recta - (t + bulge);
        if (holgura < minHolgura) {
            minHolgura = holgura;
            minPos = p;
            minDist = d;
        }
    }

    r.valid = true;
    r.totalDistanceM = D;
    if (std::isinf(minHolgura)) {
        // Trayecto mas corto que un paso (o solo huecos en medio): no hay terreno
        // intermedio que evaluar; la vision depende solo de los extremos.
        r.clear = true;
        r.clearanceM = std::min(antennaA, antennaB);
    } else {
        r.clearanceM = minHolgura;
        r.clear = minHolgura >= 0.0;
        r.blockPosition = minPos;
        r.blockDistanceM = minDist;
    }
    return r;
}

// Un rayo del viewshed: camina el azimut \a az desde \a origin muestreando el
// terreno y acumulando el horizonte. El OBJETIVO a altura H de un punto a
// distancia d se tapa con el terreno MAS cercano que d, asi que se comprueba su
// angulo contra el horizonte acumulado ANTES de incorporar el terreno de d (el
// terreno de d no se tapa a si mismo). Con H=0 esto es el viewshed del propio
// terreno. Las muestras sin dato (hueco o fuera de cobertura) se saltan.
ViewshedRay rayoViewshed(const IElevationSource &src, const QGeoCoordinate &origin,
                         double az, double zObs, double paso, double maxR,
                         double H, bool curva, double k, double R,
                         double voidElev, bool guardarPerfil)
{
    ViewshedRay ray;
    ray.azimuthDeg = az;

    // Tangente del horizonte acumulado (max del angulo del terreno visto hasta
    // aqui). Empieza en -inf: al principio no hay nada que tape.
    double horizonteTan = -std::numeric_limits<double>::infinity();
    double mejorTan = -std::numeric_limits<double>::infinity();   // para horizonDeg
    bool visibleContinuo = true;                                  // primer tramo sin cortar

    // Seguimiento de los tramos visibles (zona REAL con huecos) y ocultos (con
    // dato), que parten el rayo cubierto sin solape.
    bool tramoVisAbierto = false, tramoOcuAbierto = false;
    double visIni = 0.0, ultimaVisD = 0.0, ocuIni = 0.0, ultimaOcuD = 0.0;
    auto cerrarVisible = [&] {
        if (tramoVisAbierto) {
            ray.visibleRanges.append(VisibleRange{visIni, ultimaVisD});
            tramoVisAbierto = false;
        }
    };
    auto cerrarOculto = [&] {
        if (tramoOcuAbierto) {
            ray.hiddenRanges.append(VisibleRange{ocuIni, ultimaOcuD});
            tramoOcuAbierto = false;
        }
    };

    double ultimaCota = std::numeric_limits<double>::quiet_NaN();
    if (guardarPerfil)
        anadirMuestra(ray.profile, 0.0, origin, cota(src, origin, voidElev), ultimaCota);

    double ultimoD = 0.0;
    for (double d = paso; d <= maxR; d += paso) {
        const QGeoCoordinate p = origin.atDistanceAndAzimuth(d, az);
        const double t = cota(src, p, voidElev);
        if (guardarPerfil)
            anadirMuestra(ray.profile, d, p, t, ultimaCota);
        if (std::isnan(t)) {
            cerrarVisible();            // sin dato: ni visible ni oculto (desconocido)
            cerrarOculto();
            continue;                                   // hueco / fuera de cobertura
        }
        ultimoD = d;

        const double c = curva ? caida(d, k, R) : 0.0;

        // Objetivo a altura H: visible si su angulo supera el horizonte de lo
        // MAS cercano (sin incluir el terreno de este mismo d). Este test es,
        // punto a punto, el mismo que decide lineOfSight.
        const double yTgt = (t + H - zObs) - c;
        const double tanTgt = yTgt / d;
        const bool visibleAqui = tanTgt >= horizonteTan;

        // Primer tramo contiguo desde el origen (ZVD en estrella simple).
        if (visibleContinuo) {
            if (visibleAqui) ray.visibilityReachM = d;
            else             visibleContinuo = false;
        }
        // Zona REAL: abre/cierra tramos visibles y ocultos (con huecos a la vista).
        if (visibleAqui) {
            cerrarOculto();
            if (!tramoVisAbierto) {
                tramoVisAbierto = true;
                // El primer tramo que arranca en la 1a muestra incluye el origen;
                // los demas empiezan exactamente en esta muestra.
                visIni = (d <= paso) ? 0.0 : d;
            }
            ultimaVisD = d;
        } else {
            cerrarVisible();
            if (!tramoOcuAbierto) {
                tramoOcuAbierto = true;
                ocuIni = (d <= paso) ? 0.0 : d;
            }
            ultimaOcuD = d;
        }

        // Ahora incorpora el terreno de d al horizonte; si fija un nuevo maximo,
        // es un pico de la silueta.
        const double yTerr = (t - zObs) - c;
        const double tanTerr = yTerr / d;
        if (tanTerr > horizonteTan) {
            ClosingAnglePeak pk;
            pk.distanceM = d;
            pk.position = p;
            pk.elevation = t;
            pk.angleDeg = qRadiansToDegrees(std::atan2(yTerr, d));
            pk.tangent = tanTerr;
            ray.peaks.append(pk);
            horizonteTan = tanTerr;
        }
        if (tanTerr > mejorTan)
            mejorTan = tanTerr;
    }
    cerrarVisible();                                    // cierra los ultimos tramos
    cerrarOculto();

    if (guardarPerfil)
        ray.profile.totalDistanceM = ultimoD;
    ray.horizonDeg = ray.peaks.isEmpty()
                         ? 0.0
                         : qRadiansToDegrees(std::atan(mejorTan));
    return ray;
}

// Viewshed 360 grados: un rayo por azimut. Invalido si no hay cota en el origen
// (no se puede anclar el plano del observador).
Viewshed computeViewshed(const IElevationSource &src, const QGeoCoordinate &origin,
                         const ViewshedParams &params)
{
    Viewshed vs;
    if (!origin.isValid())
        return vs;
    const double t0 = cota(src, origin, params.voidElevation);
    if (std::isnan(t0))
        return vs;

    const double paso = params.stepMeters > 0.0 ? params.stepMeters : 30.0;
    const double azPaso = params.azimuthStepDeg > 0.0 ? params.azimuthStepDeg : 1.0;
    const double maxR = params.maxRangeM > 0.0 ? params.maxRangeM : 50000.0;
    const double k = params.k > 0.0 ? params.k : 4.0 / 3.0;
    const double R = params.earthRadiusM > 0.0 ? params.earthRadiusM : 6371000.0;
    const double zObs = t0 + params.observerHeight;

    vs.origin = origin;
    vs.observerHeight = params.observerHeight;
    vs.targetHeight = params.targetHeight;

    for (double az = 0.0; az < 360.0; az += azPaso)
        vs.rays.append(rayoViewshed(src, origin, az, zObs, paso, maxR,
                                    params.targetHeight, params.curvature, k, R,
                                    params.voidElevation, params.keepProfiles));
    return vs;
}

} // namespace libmapa
