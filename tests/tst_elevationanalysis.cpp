#include "dem/ElevationAnalysis.h"
#include "dem/HgtElevation.h"
#include "geo/GeoMath.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtMath>
#include <QtTest>

#include <cmath>

using namespace libmapa;

// Pruebas del analisis de elevacion (Fase A: perfil de ruta). Sobre un `.hgt`
// SINTETICO pequeno con una rampa conocida -value(row,col) = col*100 + row-, de
// modo que al ir hacia el ESTE (columna creciente) el terreno "sube".
class TstElevationAnalysis : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void profileAlongLine();
    void multiVertexDistanceAddsUp();
    void handlesVoidsEmptyAndNoData();
    void lineOfSightBlockedByHill();
    void lineOfSightCurvatureOnFlatEarth();
    void lineOfSightInvalid();
    void viewshedPeakAtKnownAzimuth();
    void viewshedTargetHeightVisibility();
    void viewshedCurvatureHorizon();
    void viewshedMatchesLineOfSight();
    void viewshedInvalid();

private:
    static bool writeHgt(const QString &path, int side, const QVector<int> &s);

    QTemporaryDir m_dir;
    HgtElevation m_dem;
};

// Un tile N19W077 de lado 7 (6 celdas/grado), rampa col*100+row, con un hueco
// SRTM en el nodo (1,1) para probar el tratamiento de NaN.
void TstElevationAnalysis::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const int side = 7;
    QVector<int> s(side * side);
    for (int row = 0; row < side; ++row)
        for (int col = 0; col < side; ++col)
            s[row * side + col] = col * 100 + row;
    s[1 * side + 1] = -32768;                 // hueco en (1,1): lat~19.83, lon~-76.83
    QVERIFY(writeHgt(m_dir.filePath(QStringLiteral("N19W077.hgt")), side, s));
    m_dem.setDirectory(m_dir.path());
}

bool TstElevationAnalysis::writeHgt(const QString &path, int side,
                                    const QVector<int> &s)
{
    if (s.size() != side * side)
        return false;
    QByteArray bytes;
    bytes.resize(side * side * 2);
    char *p = bytes.data();
    for (int i = 0; i < side * side; ++i)
        qToBigEndian<qint16>(qint16(s[i]), p + i * 2);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(bytes) == bytes.size();
}

// Perfil a lo largo de una linea este-oeste a latitud constante: muestras cada
// 30 m, distancia acumulada creciente, terreno que sube hacia el este (solo
// ganancia, cero perdida), y coincidencia de los extremos con elevationAt.
void TstElevationAnalysis::profileAlongLine()
{
    // Lat 19.5 evita la fila del hueco (fila ~1). Lon de -76.9 (oeste) a -76.6
    // (este): va cuesta arriba.
    const QGeoCoordinate a(19.5, -76.9);
    const QGeoCoordinate b(19.5, -76.6);

    ElevationProfileParams p;
    p.stepMeters = 30.0;
    const ElevationProfile perfil = elevationProfile(m_dem, {a, b}, p);

    QVERIFY(perfil.isValid());
    QVERIFY(perfil.samples.size() > 100);

    // Primer y ultimo punto exactos.
    QCOMPARE(perfil.samples.first().distanceM, 0.0);
    QCOMPARE(perfil.samples.first().position, a);
    QCOMPARE(perfil.samples.last().position, b);

    // Distancia total coincide con la geodesica A-B (±1 paso).
    const double dAB = GeoMath::distanceMeters(a, b);
    QVERIFY(std::abs(perfil.totalDistanceM - dAB) < 30.0);
    QVERIFY(std::abs(perfil.samples.last().distanceM - dAB) < 30.0);

    // Distancias estrictamente crecientes; todas las cotas con dato.
    for (int i = 1; i < perfil.samples.size(); ++i) {
        QVERIFY(perfil.samples[i].distanceM > perfil.samples[i - 1].distanceM);
        QVERIFY(!std::isnan(perfil.samples[i].elevation));
    }

    // Cuesta arriba hacia el este: solo ganancia, sin perdida.
    QVERIFY(perfil.maxElevation > perfil.minElevation);
    QVERIFY(perfil.gain > 0.0);
    QVERIFY(perfil.loss < 1e-6);
    QVERIFY(std::abs(perfil.gain - (perfil.maxElevation - perfil.minElevation)) < 1e-6);

    // El extremo del perfil coincide con la consulta directa de cota.
    QVERIFY(std::abs(perfil.samples.last().elevation - m_dem.elevationAt(b)) < 1e-6);
}

// La distancia total de una ruta de varios tramos es la suma de los tramos.
void TstElevationAnalysis::multiVertexDistanceAddsUp()
{
    const QGeoCoordinate a(19.3, -76.8);
    const QGeoCoordinate b(19.5, -76.8);
    const QGeoCoordinate c(19.5, -76.5);

    const ElevationProfile perfil = elevationProfile(m_dem, {a, b, c});
    QVERIFY(perfil.isValid());

    const double suma = GeoMath::distanceMeters(a, b) + GeoMath::distanceMeters(b, c);
    QVERIFY(std::abs(perfil.totalDistanceM - suma) < 1.0);
    QCOMPARE(perfil.samples.last().position, c);
}

// Huecos -> NaN en esas muestras (sin romper las estadisticas); ruta de menos de
// dos puntos o sin origen de elevacion -> perfil vacio.
void TstElevationAnalysis::handlesVoidsEmptyAndNoData()
{
    // Linea a lat ~19.83 (fila del hueco (1,1)) que arranca sobre el hueco y
    // sigue al este hasta terreno con dato: con interpolacion bilineal TODAS las
    // celdas que tocan el nodo (1,1) dan NaN, asi que el tramo tiene que salir de
    // esa banda para tener tambien muestras validas.
    const QGeoCoordinate a(19.83, -76.90);
    const QGeoCoordinate b(19.83, -76.40);
    const ElevationProfile perfil = elevationProfile(m_dem, {a, b});
    QVERIFY(perfil.isValid());
    int conHueco = 0;
    for (const ElevationSample &m : perfil.samples)
        if (std::isnan(m.elevation))
            ++conHueco;
    QVERIFY2(conHueco > 0, "Se esperaba alguna muestra sobre el hueco SRTM");
    // Aun con huecos, hay estadisticas de las muestras con dato.
    QVERIFY(!std::isnan(perfil.maxElevation));

    // Rutas degeneradas.
    QVERIFY(!elevationProfile(m_dem, {}).isValid());
    QVERIFY(!elevationProfile(m_dem, {a}).isValid());

    // Sin origen de elevacion: el perfil se calcula igual pero todas las cotas
    // son NaN (elevationAt de un HgtElevation sin carpeta devuelve NaN).
    HgtElevation vacio;
    const ElevationProfile sinDato = elevationProfile(vacio, {a, b});
    QVERIFY(sinDato.isValid());                 // la geometria existe
    QVERIFY(std::isnan(sinDato.maxElevation));  // pero no hay cotas
}

// Una colina central bloquea la vision a ras de suelo; subir las antenas por
// encima de ella restablece la vision directa. Tile con una cresta en la columna
// central (col 3 = 1500 m, el resto 0), linea este-oeste a latitud 19.5 que la
// cruza por la cima.
void TstElevationAnalysis::lineOfSightBlockedByHill()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int side = 7;
    QVector<int> s(side * side, 0);
    for (int row = 0; row < side; ++row)
        s[row * side + 3] = 1500;                 // cresta en la columna central
    QVERIFY(writeHgt(dir.filePath(QStringLiteral("N19W077.hgt")), side, s));
    HgtElevation dem;
    dem.setDirectory(dir.path());

    // La cima (col 3) cae en lon -76.5; A y B a ambos lados, sobre cota 0.
    const QGeoCoordinate a(19.5, -76.9);
    const QGeoCoordinate b(19.5, -76.1);

    // A ras de suelo (antenas 0): la colina corta la vision.
    const LineOfSightResult bloqueada = lineOfSight(dem, a, b, 0.0, 0.0);
    QVERIFY(bloqueada.isValid());
    QVERIFY(!bloqueada.clear);
    QVERIFY(bloqueada.clearanceM < -1000.0);      // se queda muy corto
    QVERIFY(bloqueada.blockPosition.isValid());
    // El punto critico esta cerca de la cima (lon -76.5), no en los extremos.
    QVERIFY(std::abs(bloqueada.blockPosition.longitude() + 76.5) < 0.1);
    QVERIFY(bloqueada.blockDistanceM > 0.0
            && bloqueada.blockDistanceM < bloqueada.totalDistanceM);

    // Antenas de 3000 m en ambos extremos: por encima de la colina -> hay vision.
    const LineOfSightResult despejada = lineOfSight(dem, a, b, 3000.0, 3000.0);
    QVERIFY(despejada.isValid());
    QVERIFY(despejada.clear);
    QVERIFY(despejada.clearanceM > 0.0);
}

// Sobre terreno plano a cota 0 y antenas a 0, la unica obstruccion es el
// abombamiento de la Tierra: la holgura minima debe coincidir con -D^2/(8kR) (en
// el punto medio) con k=4/3; desactivar la curvatura deja la vision justo a ras
// (holgura 0). Comprueba la constante de curvatura.
void TstElevationAnalysis::lineOfSightCurvatureOnFlatEarth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int side = 7;
    const QVector<int> s(side * side, 0);          // todo a cota 0
    QVERIFY(writeHgt(dir.filePath(QStringLiteral("N19W077.hgt")), side, s));
    HgtElevation dem;
    dem.setDirectory(dir.path());

    const QGeoCoordinate a(19.5, -76.9);
    const QGeoCoordinate b(19.5, -76.5);
    const double D = GeoMath::distanceMeters(a, b);

    // Con curvatura (4/3 por defecto): bloquea, y la holgura minima ~ -D^2/(8kR).
    const LineOfSightResult conCurva = lineOfSight(dem, a, b, 0.0, 0.0);
    QVERIFY(conCurva.isValid());
    QVERIFY(!conCurva.clear);
    const double k = 4.0 / 3.0, R = 6371000.0;
    const double esperado = -(D * D) / (8.0 * k * R);
    QVERIFY2(std::abs(conCurva.clearanceM - esperado) < 0.5,
             qPrintable(QStringLiteral("holgura=%1 esperado=%2")
                            .arg(conCurva.clearanceM).arg(esperado)));
    // El punto critico, en el medio del trayecto.
    QVERIFY(std::abs(conCurva.blockDistanceM - D / 2.0) < 60.0);

    // Sin curvatura (geometrico puro): recta a ras del terreno plano -> vision.
    LineOfSightParams geom;
    geom.curvature = false;
    const LineOfSightResult sinCurva = lineOfSight(dem, a, b, 0.0, 0.0, geom);
    QVERIFY(sinCurva.isValid());
    QVERIFY(sinCurva.clear);
    QVERIFY(std::abs(sinCurva.clearanceM) < 1e-6);
}

// Sin origen de elevacion, o con un extremo sobre un hueco SRTM, el resultado es
// invalido (no se puede anclar la recta): isValid()==false.
void TstElevationAnalysis::lineOfSightInvalid()
{
    const QGeoCoordinate a(19.5, -76.8);
    const QGeoCoordinate b(19.5, -76.5);

    // Sin carpeta: elevationAt siempre NaN.
    HgtElevation vacio;
    QVERIFY(!lineOfSight(vacio, a, b, 0.0, 0.0).isValid());

    // Extremo justo sobre el hueco (nodo (1,1) del tile compartido): lat ~19.833,
    // lon ~-76.833. Sin cota en A -> invalido.
    const QGeoCoordinate hueco(19.0 + 5.0 / 6.0, -77.0 + 1.0 / 6.0);
    QVERIFY(std::isnan(m_dem.elevationAt(hueco)));
    QVERIFY(!lineOfSight(m_dem, hueco, b, 0.0, 0.0).isValid());
}

// --- Viewshed (Fase C) ------------------------------------------------------

// Crea un tile con terreno 0 salvo una colina (un nodo alto) para los tests del
// viewshed. Devuelve true si se escribio.
static bool writeHillTile(const QString &path, int side, int hillRow, int hillCol,
                          int hillValue)
{
    QVector<int> s(side * side, 0);
    s[hillRow * side + hillCol] = hillValue;
    // Reutiliza el writeHgt de la clase a traves de una instancia temporal.
    QByteArray bytes;
    bytes.resize(side * side * 2);
    char *p = bytes.data();
    for (int i = 0; i < side * side; ++i)
        qToBigEndian<qint16>(qint16(s[i]), p + i * 2);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(bytes) == bytes.size();
}

// Una colina al ESTE (azimut 90) produce en ese rayo un pico de la silueta a
// distancia conocida y un horizonte (angulo de cierre) muy superior al del rayo
// hacia el oeste (terreno plano). Mide ademas el tiempo de un viewshed de 360.
void TstElevationAnalysis::viewshedPeakAtKnownAzimuth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int side = 7;
    // Colina en el nodo (3,5): lat 19.5, lon -76.1667, al este del origen (3,3).
    QVERIFY(writeHillTile(dir.filePath(QStringLiteral("N19W077.hgt")),
                          side, 3, 5, 1500));
    HgtElevation dem;
    dem.setDirectory(dir.path());

    const QGeoCoordinate origen(19.5, -76.5);     // nodo (3,3), cota 0

    ViewshedParams vp;                            // observador a ras (0 m)
    vp.maxRangeM = 50000.0;

    QElapsedTimer t;
    t.start();
    const Viewshed vs = computeViewshed(dem, origen, vp);
    qDebug() << "viewshed 360deg paso" << vp.stepMeters << "m alcance"
             << vp.maxRangeM << "m:" << t.elapsed() << "ms,"
             << vs.rays.size() << "rayos";

    QVERIFY(vs.isValid());
    QCOMPARE(vs.rays.size(), 360);                // 1 grado -> 360 rayos

    const ViewshedRay &este = vs.rays[90];        // indice = azimut con paso 1
    QCOMPARE(int(este.azimuthDeg), 90);
    QVERIFY(!este.peaks.isEmpty());
    // El pico mas alto de la silueta es la colina: lejos del origen y ~1500 m.
    const ClosingAnglePeak &cima = este.peaks.last();
    QVERIFY2(cima.elevation > 1000.0,
             qPrintable(QStringLiteral("cota del pico=%1").arg(cima.elevation)));
    QVERIFY(cima.distanceM > 20000.0 && cima.distanceM < 45000.0);
    QVERIFY(cima.angleDeg > 0.0);                 // por encima del horizonte
    QVERIFY(std::abs(cima.tangent - std::tan(qDegreesToRadians(cima.angleDeg))) < 1e-9);

    // Hacia el este la silueta se "cierra" mucho mas que hacia el oeste (plano).
    const ViewshedRay &oeste = vs.rays[270];
    QVERIFY(este.horizonDeg > oeste.horizonDeg + 1.0);
}

// Un objetivo mas alto se ve mas lejos: en el rayo hacia la colina, la zona de
// visibilidad (visibilityReachM) crece con la altura del objetivo.
void TstElevationAnalysis::viewshedTargetHeightVisibility()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int side = 7;
    QVERIFY(writeHillTile(dir.filePath(QStringLiteral("N19W077.hgt")),
                          side, 3, 5, 1500));
    HgtElevation dem;
    dem.setDirectory(dir.path());

    const QGeoCoordinate origen(19.5, -76.5);

    ViewshedParams bajo;
    bajo.observerHeight = 30.0;
    bajo.targetHeight = 0.0;
    bajo.maxRangeM = 50000.0;
    const Viewshed vsBajo = computeViewshed(dem, origen, bajo);

    ViewshedParams alto = bajo;
    alto.targetHeight = 3000.0;                   // bien por encima de la colina
    const Viewshed vsAlto = computeViewshed(dem, origen, alto);

    QVERIFY(vsBajo.isValid() && vsAlto.isValid());
    const double reachBajo = vsBajo.rays[90].visibilityReachM;
    const double reachAlto = vsAlto.rays[90].visibilityReachM;
    QVERIFY2(reachAlto > reachBajo,
             qPrintable(QStringLiteral("reachAlto=%1 reachBajo=%2")
                            .arg(reachAlto).arg(reachBajo)));
}

// Sobre terreno plano, el unico limite de la vision es la curvatura: con ella, un
// objetivo a altura H tiene alcance FINITO que coincide con el horizonte
// geometrico sqrt(2kR)*(sqrt(hObs)+sqrt(H)) -el 4.12*raiz(h) de DVD EMERGE, no se
// codifica-; sin curvatura y H>=hObs, se ve hasta el alcance maximo.
void TstElevationAnalysis::viewshedCurvatureHorizon()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int side = 7;
    const QVector<int> s(side * side, 0);          // todo plano a cota 0
    QVERIFY(writeHgt(dir.filePath(QStringLiteral("N19W077.hgt")), side, s));
    HgtElevation dem;
    dem.setDirectory(dir.path());

    const QGeoCoordinate origen(19.5, -76.5);
    const double hObs = 10.0, H = 10.0, k = 4.0 / 3.0, R = 6371000.0;

    ViewshedParams conCurva;
    conCurva.observerHeight = hObs;
    conCurva.targetHeight = H;
    // 50 km se mantiene dentro del tile (1x1 grado) en todas las direcciones desde
    // el centro; el horizonte geometrico (~26 km) queda holgadamente por debajo.
    conCurva.maxRangeM = 50000.0;
    const Viewshed vsCurva = computeViewshed(dem, origen, conCurva);
    QVERIFY(vsCurva.isValid());
    const double reachCurva = vsCurva.rays[0].visibilityReachM;

    const double horizonteGeo =
        std::sqrt(2.0 * k * R) * (std::sqrt(hObs) + std::sqrt(H));   // ~26 km
    qDebug() << "reach con curvatura" << reachCurva << "m; horizonte geometrico"
             << horizonteGeo << "m";
    QVERIFY(reachCurva < conCurva.maxRangeM);                 // finito
    QVERIFY2(std::abs(reachCurva - horizonteGeo) < 1000.0,
             qPrintable(QStringLiteral("reach=%1 geo=%2")
                            .arg(reachCurva).arg(horizonteGeo)));

    // Sin curvatura: el terreno plano no tapa y el objetivo (H=hObs) se ve hasta
    // el final del alcance.
    ViewshedParams sinCurva = conCurva;
    sinCurva.curvature = false;
    const Viewshed vsPlano = computeViewshed(dem, origen, sinCurva);
    const double reachPlano = vsPlano.rays[0].visibilityReachM;
    QVERIFY(reachPlano > reachCurva);
    QVERIFY(reachPlano > sinCurva.maxRangeM - 2.0 * sinCurva.stepMeters);
}

// CONSISTENCIA ZVD <-> linea de vision: para CADA distancia del rayo, que el
// objetivo este dentro de un tramo visible del viewshed debe coincidir con que
// lineOfSight(origen, punto) lo declare visible. Terreno con una loma que tapa una
// vaguada y, mas alla, un pico que vuelve a verse: la zona tiene un HUECO y una
// bolsa visible detras, y ambas funciones concuerdan punto a punto.
void TstElevationAnalysis::viewshedMatchesLineOfSight()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int side = 13;                           // 12 celdas/grado (~8.7 km)
    QVector<int> s(side * side, 0);
    // Origen en el nodo (6,6) = (19.5, -76.5). Hacia el este (fila 6):
    s[6 * side + 8] = 400;                         // loma a ~17.5 km
    s[6 * side + 10] = 800;                        // pico a ~35 km (tras la loma)
    QVERIFY(writeHgt(dir.filePath(QStringLiteral("N19W077.hgt")), side, s));
    HgtElevation dem;
    dem.setDirectory(dir.path());

    const QGeoCoordinate origen(19.5, -76.5);
    const double obsH = 50.0, H = 50.0;

    ViewshedParams vp;
    vp.observerHeight = obsH;
    vp.targetHeight = H;
    vp.maxRangeM = 40000.0;                        // dentro del tile
    const Viewshed vs = computeViewshed(dem, origen, vp);
    QVERIFY(vs.isValid());
    const ViewshedRay &este = vs.rays[90];         // azimut 90 (este)

    auto enTramoVisible = [&](double d) {
        for (const VisibleRange &r : este.visibleRanges)
            if (d >= r.startM && d <= r.endM)
                return true;
        return false;
    };

    int oculto = 0, visibleLejos = 0;
    double ultimoOculto = 0.0;
    for (double d = 3000.0; d <= 38000.0; d += 300.0) {   // multiplos del paso (30)
        const QGeoCoordinate p = origen.atDistanceAndAzimuth(d, 90.0);
        const LineOfSightResult v = lineOfSight(dem, origen, p, obsH, H);
        QVERIFY(v.isValid());
        // Cerca del punto de roce (holgura ~0) el veredicto es ambiguo por el
        // muestreo; se compara solo donde esta claramente visible u oculto.
        if (std::abs(v.clearanceM) < 1.0)
            continue;
        QVERIFY2(enTramoVisible(d) == v.clear,
                 qPrintable(QStringLiteral("d=%1 zvd=%2 los=%3 holgura=%4")
                                .arg(d).arg(enTramoVisible(d)).arg(v.clear)
                                .arg(v.clearanceM)));
        if (!v.clear) { ++oculto; ultimoOculto = d; }
        else if (d > ultimoOculto && ultimoOculto > 0.0) ++visibleLejos;
    }

    // Que el escenario se ejercita: hay zona oculta y una bolsa visible mas alla
    // (el hueco), y por tanto mas de un tramo visible.
    QVERIFY2(oculto > 0, "Se esperaba terreno oculto tras la loma");
    QVERIFY2(visibleLejos > 0, "Se esperaba una bolsa visible tras el hueco");
    QVERIFY(este.visibleRanges.size() >= 2);
}

// Sin origen de elevacion, o con el origen sobre un hueco SRTM, el viewshed es
// invalido (no se puede anclar el plano del observador): isValid()==false.
void TstElevationAnalysis::viewshedInvalid()
{
    const QGeoCoordinate origen(19.5, -76.5);

    HgtElevation vacio;
    QVERIFY(!computeViewshed(vacio, origen).isValid());

    // Origen sobre el hueco del tile compartido (nodo (1,1)).
    const QGeoCoordinate hueco(19.0 + 5.0 / 6.0, -77.0 + 1.0 / 6.0);
    QVERIFY(std::isnan(m_dem.elevationAt(hueco)));
    QVERIFY(!computeViewshed(m_dem, hueco).isValid());
}

QTEST_MAIN(TstElevationAnalysis)
#include "tst_elevationanalysis.moc"
