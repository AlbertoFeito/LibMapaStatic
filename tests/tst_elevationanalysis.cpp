#include "dem/ElevationAnalysis.h"
#include "dem/HgtElevation.h"
#include "geo/GeoMath.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
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

QTEST_MAIN(TstElevationAnalysis)
#include "tst_elevationanalysis.moc"
