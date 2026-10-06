#include "libmapa/Contours.h"
#include "dem/IElevationSource.h"

#include <QSet>
#include <QtTest>

#include <cmath>

using namespace libmapa;

// DEM sintetico: CONO de altura H en el centro que baja linealmente a 0 en el
// radio R. Sus curvas de nivel deben ser anillos (cerrados) concentricos cuyo
// radio decrece al subir la cota: r(L) = R·(1 − L/H).
class ConoElevacion : public IElevationSource
{
public:
    ConoElevacion(const QGeoCoordinate &centro, double altura, double radioM)
        : m_c(centro), m_H(altura), m_R(radioM) {}
    double elevationAt(const QGeoCoordinate &p) const override
    {
        const double d = m_c.distanceTo(p);
        const double z = m_H * (1.0 - d / m_R);
        return z > 0.0 ? z : 0.0;
    }
private:
    QGeoCoordinate m_c;
    double m_H, m_R;
};

// DEM sintetico PLANO a una cota constante (ninguna curva lo cruza).
class PlanoElevacion : public IElevationSource
{
public:
    explicit PlanoElevacion(double cota) : m_z(cota) {}
    double elevationAt(const QGeoCoordinate &) const override { return m_z; }
private:
    double m_z;
};

// DEM sintetico en RAMPA segun la longitud (sube hacia el este): las curvas son
// lineas de longitud casi constante (verticales).
class RampaElevacion : public IElevationSource
{
public:
    RampaElevacion(double lonW, double porGrado) : m_lonW(lonW), m_k(porGrado) {}
    double elevationAt(const QGeoCoordinate &p) const override
    {
        return m_k * (p.longitude() - m_lonW);
    }
private:
    double m_lonW, m_k;
};

// Pruebas de computeContours (marching squares + encadenado de segmentos).
class TstContours : public QObject
{
    Q_OBJECT

private slots:
    void coneGivesClosedRingsDecreasingRadius();
    void flatTerrainHasNoContours();
    void rampGivesExpectedLevels();
};

// El cono produce, por cada nivel, un anillo cerrado (primer punto == ultimo) y
// de radio medio aproximado R·(1−L/H), decreciente con la cota.
void TstContours::coneGivesClosedRingsDecreasingRadius()
{
    const QGeoCoordinate centro(20.0, -77.0);
    const double H = 1000.0, R = 40000.0;
    ConoElevacion dem(centro, H, R);

    ContourParams p;
    p.latN = 20.4; p.lonW = -77.4; p.latS = 19.6; p.lonE = -76.6;
    p.interval = 200.0; p.base = 0.0; p.stepMeters = 120.0;
    const QVector<ContourLine> curvas = computeContours(dem, p);
    QVERIFY(!curvas.isEmpty());

    // Para los niveles 200..800, el anillo mas largo debe estar cerrado y su
    // radio medio acercarse al teorico; ademas el radio decrece con la cota.
    double radioPrevio = 1e12;
    for (double L : {200.0, 400.0, 600.0, 800.0}) {
        const ContourLine *mejor = nullptr;
        for (const ContourLine &c : curvas) {
            if (std::abs(c.elevation - L) > 1e-6)
                continue;
            if (!mejor || c.points.size() > mejor->points.size())
                mejor = &c;
        }
        QVERIFY2(mejor, qPrintable(QStringLiteral("falta curva %1").arg(L)));

        // Cerrado: el primer y el ultimo punto coinciden (bucle).
        QVERIFY(mejor->points.first().distanceTo(mejor->points.last()) < 1.0);

        double suma = 0.0;
        for (const QGeoCoordinate &q : mejor->points)
            suma += centro.distanceTo(q);
        const double radioMedio = suma / double(mejor->points.size());
        const double teorico = R * (1.0 - L / H);
        QVERIFY2(std::abs(radioMedio - teorico) < 0.12 * teorico,
                 qPrintable(QStringLiteral("L=%1 radio=%2 teorico=%3")
                                .arg(L).arg(radioMedio).arg(teorico)));
        QVERIFY(radioMedio < radioPrevio);     // decrece con la cota
        radioPrevio = radioMedio;
    }
}

// Un terreno plano a cota 55 (fuera de cualquier nivel) no genera ninguna curva.
void TstContours::flatTerrainHasNoContours()
{
    PlanoElevacion dem(55.0);
    ContourParams p;
    p.latN = 20.2; p.lonW = -77.2; p.latS = 19.8; p.lonE = -76.8;
    p.interval = 100.0; p.stepMeters = 150.0;
    QVERIFY(computeContours(dem, p).isEmpty());
}

// La rampa (sube hacia el este, 0..1000 m en la bbox) da exactamente los niveles
// 200/400/600/800, y cada curva es casi vertical (longitud casi constante).
void TstContours::rampGivesExpectedLevels()
{
    const double lonW = -77.4;
    RampaElevacion dem(lonW, 1125.0);     // 1125 m/grado -> 900 m en 0.8 grados
    ContourParams p;
    p.latN = 20.4; p.lonW = lonW; p.latS = 19.6; p.lonE = -76.6;
    p.interval = 200.0; p.base = 0.0; p.stepMeters = 150.0;
    const QVector<ContourLine> curvas = computeContours(dem, p);
    QVERIFY(!curvas.isEmpty());

    QSet<int> niveles;
    for (const ContourLine &c : curvas) {
        niveles.insert(qRound(c.elevation));
        // Casi vertical: el rango de longitud de la curva es pequeno.
        double lonMin = 1e9, lonMax = -1e9;
        for (const QGeoCoordinate &q : c.points) {
            lonMin = qMin(lonMin, q.longitude());
            lonMax = qMax(lonMax, q.longitude());
        }
        QVERIFY((lonMax - lonMin) < 0.02);
    }
    QCOMPARE(niveles, (QSet<int>{200, 400, 600, 800}));
}

QTEST_MAIN(TstContours)
#include "tst_contours.moc"
