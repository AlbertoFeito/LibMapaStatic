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

QTEST_MAIN(TstElevationAnalysis)
#include "tst_elevationanalysis.moc"
