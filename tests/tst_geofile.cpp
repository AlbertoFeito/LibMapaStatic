#include "libmapa/GeoFile.h"

#include <QtTest>

using namespace libmapa;

#ifndef LIBMAPA_TESTDATA
#  define LIBMAPA_TESTDATA "."
#endif

class TstGeoFile : public QObject
{
    Q_OBJECT

private slots:
    void readsAguasRing();
    void reportsMissingFile();
    void ordersLonLatCorrectly();
    void readsMultiplePolylines();
    void readsManyAdministrativeSegments();
};

static QString dato(const QString &nombre)
{
    return QStringLiteral(LIBMAPA_TESTDATA "/") + nombre;
}

void TstGeoFile::readsAguasRing()
{
    QString error;
    const QVector<GeoPath> paths =
        readGeoFile(dato(QStringLiteral("aguas.geo")), &error);

    QVERIFY2(!paths.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());

    // Un unico trazado: el anillo de las aguas.
    QCOMPARE(paths.size(), 1);
    QCOMPARE(paths.first().points.size(), 140);   // + el 0,0 que no cuenta
    QVERIFY(paths.first().closed);
}

void TstGeoFile::reportsMissingFile()
{
    QString error;
    const QVector<GeoPath> paths = readGeoFile(QStringLiteral("/no/existe.geo"), &error);
    QVERIFY(paths.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TstGeoFile::ordersLonLatCorrectly()
{
    // El fichero trae longitud,latitud; el primer vertice es
    // -85.163093 (lon), 21.906721 (lat). En Cuba: lat ~+21, lon ~-85.
    const QVector<GeoPath> paths = readGeoFile(dato(QStringLiteral("aguas.geo")));
    const QGeoCoordinate &c = paths.first().points.first();
    QVERIFY(c.latitude() > 21.0 && c.latitude() < 24.0);
    QVERIFY(c.longitude() > -86.0 && c.longitude() < -73.0);
}

void TstGeoFile::readsMultiplePolylines()
{
    // "corredores": seis parejas de lineas paralelas, separadas por 0,0.
    // El lector NO debe pararse en el primer separador.
    const QVector<GeoPath> paths =
        readGeoFile(dato(QStringLiteral("corredores.geo")));
    QCOMPARE(paths.size(), 6);
    for (const GeoPath &p : paths) {
        QCOMPARE(p.points.size(), 2);      // cada corredor son dos puntos
        QVERIFY(!p.closed);                // lineas abiertas
    }
}

void TstGeoFile::readsManyAdministrativeSegments()
{
    // "ejercitos": divisiones administrativas, decenas de polilineas abiertas.
    const QVector<GeoPath> paths =
        readGeoFile(dato(QStringLiteral("ejercitos.geo")));
    QCOMPARE(paths.size(), 39);
    for (const GeoPath &p : paths) {
        QVERIFY(p.points.size() >= 2);     // ninguna degenerada
        QVERIFY(!p.closed);
    }
}

QTEST_MAIN(TstGeoFile)
#include "tst_geofile.moc"
