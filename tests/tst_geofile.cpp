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
};

static QString dato(const QString &nombre)
{
    return QStringLiteral(LIBMAPA_TESTDATA "/") + nombre;
}

void TstGeoFile::readsAguasRing()
{
    QString error;
    const GeoData geo = readGeoFile(dato(QStringLiteral("aguas.geo")), &error);

    QVERIFY2(!geo.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());

    // 140 vertices + el terminador "0.0,0.0" (que no cuenta).
    QCOMPARE(geo.points.size(), 140);

    // Es un anillo: el ultimo vertice coincide con el primero.
    QVERIFY(geo.closed);
    QVERIFY(qFuzzyCompare(geo.points.first().latitude() + 1.0,
                          geo.points.last().latitude() + 1.0));
    QVERIFY(qFuzzyCompare(geo.points.first().longitude() + 1.0,
                          geo.points.last().longitude() + 1.0));
}

void TstGeoFile::reportsMissingFile()
{
    QString error;
    const GeoData geo = readGeoFile(QStringLiteral("/no/existe.geo"), &error);
    QVERIFY(geo.isEmpty());
    QVERIFY(!error.isEmpty());          // el motivo queda escrito
}

void TstGeoFile::ordersLonLatCorrectly()
{
    // El fichero trae longitud,latitud; el primer vertice es
    // -85.163093 (lon), 21.906721 (lat). En Cuba: lat ~+21, lon ~-85.
    const GeoData geo = readGeoFile(dato(QStringLiteral("aguas.geo")));
    const QGeoCoordinate &c = geo.points.first();
    QVERIFY(c.latitude() > 21.0 && c.latitude() < 24.0);
    QVERIFY(c.longitude() > -86.0 && c.longitude() < -73.0);
}

QTEST_MAIN(TstGeoFile)
#include "tst_geofile.moc"
