#include "dem/HgtElevation.h"
#include "dem/SqliteElevation.h"

#include <QByteArray>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <QtEndian>
#include <QtTest>

#include <cmath>

using namespace libmapa;

// Pruebas del lector de elevacion desde base de datos (SqliteElevation). La idea
// central: dar EXACTAMENTE la misma cota que el lector de ficheros (HgtElevation)
// para el mismo dato. Se construye en un temporal un `.hgt` sintetico y una BD
// con ESE mismo tile (blob comprimido), y se comparan los dos lectores.
class TstSqliteElevation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sameAsFileReader();      // BD y ficheros dan lo mismo en varios puntos
    void voidReturnsNan();        // hueco SRTM -> NaN
    void missingTileReturnsNan(); // tile no presente en la BD -> NaN

private:
    // Muestras del tile sintetico N19W077 (lado 7, rampa col*100+row, hueco en (1,1)).
    static QVector<int> rampa7(int side);
    // Escribe un `.hgt` (int16 big-endian) con esas muestras.
    static bool writeHgt(const QString &path, int side, const QVector<int> &s);
    // Crea una BD DEM con un tile (blob = qCompress de las muestras big-endian).
    static bool writeDb(const QString &path, int lat, int lon,
                        int side, const QVector<int> &s);

    QTemporaryDir m_dir;
    HgtElevation m_file;     // lector de ficheros (referencia)
    SqliteElevation m_db;    // lector de BD (lo que se prueba)
};

void TstSqliteElevation::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const int side = 7;
    const QVector<int> s = rampa7(side);

    // Fichero de referencia.
    QVERIFY(writeHgt(m_dir.filePath(QStringLiteral("N19W077.hgt")), side, s));
    m_file.setDirectory(m_dir.path());

    // BD con el mismo tile.
    const QString dbPath = m_dir.filePath(QStringLiteral("dem.sqlitedb"));
    QVERIFY(writeDb(dbPath, 19, -77, side, s));
    m_db.setDatabase(dbPath);
}

// Rampa conocida col*100+row, con un hueco SRTM en el nodo (1,1).
QVector<int> TstSqliteElevation::rampa7(int side)
{
    QVector<int> s(side * side);
    for (int row = 0; row < side; ++row)
        for (int col = 0; col < side; ++col)
            s[row * side + col] = col * 100 + row;
    s[1 * side + 1] = -32768;
    return s;
}

bool TstSqliteElevation::writeHgt(const QString &path, int side,
                                  const QVector<int> &s)
{
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

// Crea la BD DEM con el esquema real (dem_tiles) e inserta un tile cuyo blob es
// qCompress de las mismas muestras big-endian que el `.hgt`.
bool TstSqliteElevation::writeDb(const QString &path, int lat, int lon,
                                 int side, const QVector<int> &s)
{
    QByteArray raw;
    raw.resize(side * side * 2);
    char *p = raw.data();
    for (int i = 0; i < side * side; ++i)
        qToBigEndian<qint16>(qint16(s[i]), p + i * 2);

    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("demtest"));
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        ok = ok && q.exec(QStringLiteral(
            "CREATE TABLE dem_tiles (lat INTEGER, lon INTEGER, side INTEGER,"
            " data BLOB, PRIMARY KEY(lat,lon))"));
        ok = ok && q.prepare(QStringLiteral(
            "INSERT INTO dem_tiles (lat,lon,side,data)"
            " VALUES (:lat,:lon,:side,:data)"));
        q.bindValue(QStringLiteral(":lat"), lat);
        q.bindValue(QStringLiteral(":lon"), lon);
        q.bindValue(QStringLiteral(":side"), side);
        q.bindValue(QStringLiteral(":data"), qCompress(raw));
        ok = ok && q.exec();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("demtest"));
    return ok;
}

// En varios puntos (nodo exacto, intermedio bilineal, varias coords) la BD debe
// dar lo MISMO que el lector de ficheros, bit a bit (misma matematica compartida).
void TstSqliteElevation::sameAsFileReader()
{
    const QVector<QGeoCoordinate> puntos = {
        QGeoCoordinate(19.5, -76.5),                       // nodo (3,3) = 303
        QGeoCoordinate(20.0 - 3.5 / 6.0, -77.0 + 3.5 / 6.0), // intermedio = 353.5
        QGeoCoordinate(19.2, -76.1),
        QGeoCoordinate(19.87, -76.33),
    };
    for (const QGeoCoordinate &p : puntos) {
        const double a = m_file.elevationAt(p);
        const double b = m_db.elevationAt(p);
        QVERIFY(!std::isnan(a));
        QVERIFY(!std::isnan(b));
        QVERIFY(std::abs(a - b) < 1e-9);
    }
    // Un valor concreto para fijar que no es casualidad (nodo 3,3 = 303).
    QVERIFY(std::abs(m_db.elevationAt(QGeoCoordinate(19.5, -76.5)) - 303.0) < 1e-3);
}

// Un punto rodeado por el hueco (1,1) devuelve NaN, igual que los ficheros.
void TstSqliteElevation::voidReturnsNan()
{
    QVERIFY(std::isnan(m_db.elevationAt(QGeoCoordinate(19.75, -76.75))));
}

// Una coordenada cuyo tile no esta en la BD devuelve NaN.
void TstSqliteElevation::missingTileReturnsNan()
{
    QVERIFY(std::isnan(m_db.elevationAt(QGeoCoordinate(0.5, 0.5))));
}

QTEST_MAIN(TstSqliteElevation)
#include "tst_sqliteelevation.moc"
