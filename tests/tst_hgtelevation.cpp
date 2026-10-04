#include "dem/HgtElevation.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <cmath>

using namespace libmapa;

// Pruebas del lector de elevacion SRTM (`.hgt`). No metemos un `.hgt` real (son
// decenas de MB) en el repo: cada test escribe uno SINTETICO pequeno en un
// directorio temporal, con una rampa conocida, y comprueba la lectura, la
// autodeteccion del lado por tamano, la interpolacion bilineal y los huecos.
class TstHgtElevation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void exactNodeValue();        // nodo exacto de la rejilla
    void autodetectsResolution(); // un tile con OTRO lado se lee bien
    void bilinearBetweenNodes();  // punto intermedio: mezcla de 4 nodos
    void voidReturnsNan();        // hueco SRTM -> NaN
    void missingTileReturnsNan(); // sin fichero (mar) -> NaN
    void invalidCoordReturnsNan();

private:
    // Escribe un `.hgt` de lado \a side (muestras int16 big-endian, row-major;
    // fila 0 = norte, columna 0 = oeste) a partir de un vector de valores.
    static bool writeHgt(const QString &path, int side, const QVector<int> &s);

    QTemporaryDir m_dir;
    HgtElevation m_dem;
};

// Prepara el directorio temporal con dos tiles de DISTINTA resolucion:
//  - N19W077.hgt, lado 7 (6 celdas/grado), con un hueco en el nodo (1,1).
//  - N20W076.hgt, lado 5 (4 celdas/grado), para probar la autodeteccion.
// La rampa es value(row,col) = col*100 + row, asi cada nodo es inconfundible.
void TstHgtElevation::initTestCase()
{
    QVERIFY(m_dir.isValid());

    const int side7 = 7;
    QVector<int> s7(side7 * side7);
    for (int row = 0; row < side7; ++row)
        for (int col = 0; col < side7; ++col)
            s7[row * side7 + col] = col * 100 + row;
    s7[1 * side7 + 1] = -32768;   // hueco SRTM en el nodo (1,1)
    QVERIFY(writeHgt(m_dir.filePath(QStringLiteral("N19W077.hgt")), side7, s7));

    const int side5 = 5;
    QVector<int> s5(side5 * side5);
    for (int row = 0; row < side5; ++row)
        for (int col = 0; col < side5; ++col)
            s5[row * side5 + col] = col * 100 + row;
    QVERIFY(writeHgt(m_dir.filePath(QStringLiteral("N20W076.hgt")), side5, s5));

    m_dem.setDirectory(m_dir.path());
}

// Serializa el vector a big-endian int16 y lo vuelca al fichero.
bool TstHgtElevation::writeHgt(const QString &path, int side, const QVector<int> &s)
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

// Un nodo exacto de la rejilla devuelve su valor sin interpolar. El nodo (3,3)
// del tile N19W077 (lado 7, 6 celdas) cae en lat 19.5, lon -76.5 y vale 303.
void TstHgtElevation::exactNodeValue()
{
    const double m = m_dem.elevationAt(QGeoCoordinate(19.5, -76.5));
    QVERIFY(!std::isnan(m));
    QVERIFY(std::abs(m - 303.0) < 1e-3);
}

// Autodeteccion: el tile N20W076 tiene lado 5 (no 7). Si el lado se dedujo bien
// del tamano del fichero, su nodo (2,2) -lat 20.5, lon -75.5- vale 202.
void TstHgtElevation::autodetectsResolution()
{
    const double m = m_dem.elevationAt(QGeoCoordinate(20.5, -75.5));
    QVERIFY(!std::isnan(m));
    QVERIFY(std::abs(m - 202.0) < 1e-3);
}

// Punto intermedio: a mitad de camino en X y en Y entre los nodos (3,3),(3,4),
// (4,3),(4,4) del tile de lado 7. Esos valores son 303,403,304,404; la bilineal
// da 353.5. Se elige lat/lon como -77/20 +- 3.5/6 para reproducir fx=fy=3.5.
void TstHgtElevation::bilinearBetweenNodes()
{
    const double lon = -77.0 + 3.5 / 6.0;
    const double lat = 20.0 - 3.5 / 6.0;
    const double m = m_dem.elevationAt(QGeoCoordinate(lat, lon));
    QVERIFY(!std::isnan(m));
    QVERIFY(std::abs(m - 353.5) < 1e-3);
}

// Un punto cuyos 4 nodos de alrededor incluyen el hueco (1,1) debe dar NaN: no se
// inventa terreno. lat 19.75 / lon -76.75 cae en fx=fy=1.5 (nodos 1..2 x 1..2).
void TstHgtElevation::voidReturnsNan()
{
    const double m = m_dem.elevationAt(QGeoCoordinate(19.75, -76.75));
    QVERIFY(std::isnan(m));
}

// Una coordenada cuyo tile no esta en la carpeta (aqui N00E000) devuelve NaN.
void TstHgtElevation::missingTileReturnsNan()
{
    const double m = m_dem.elevationAt(QGeoCoordinate(0.5, 0.5));
    QVERIFY(std::isnan(m));
}

// Una coordenada invalida tambien devuelve NaN (sin tocar el disco).
void TstHgtElevation::invalidCoordReturnsNan()
{
    const double m = m_dem.elevationAt(QGeoCoordinate());
    QVERIFY(std::isnan(m));
}

QTEST_MAIN(TstHgtElevation)
#include "tst_hgtelevation.moc"
