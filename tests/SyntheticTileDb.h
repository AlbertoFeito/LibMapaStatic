#ifndef LIBMAPA_TEST_SYNTHETICTILEDB_H_
#define LIBMAPA_TEST_SYNTHETICTILEDB_H_

#include "geo/TileMatrix.h"
#include "tiles/TileDataset.h"

#include <QBuffer>
#include <QGeoCoordinate>
#include <QImage>
#include <QPainter>
// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que
// hay que pedirlo explicitamente: sin esto, bindValue() no compila en
// Qt 5.14 porque QVariant es un tipo incompleto y no hay conversion
// posible desde int, QString ni QByteArray.
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

namespace libmapa {
namespace test {

/*!
 * \brief Genera una BD de teselas con el mismo formato que las del proyecto.
 *
 * Permite fabricar en el test los dos casos reales:
 *   - OSM       : z directo,   esquema XYZ
 *   - Satelital : z invertido, con la formula "storedZ = 17 - logicalZ"
 * y tambien el caso TMS, para verificar la deteccion.
 */
struct SyntheticSpec
{
    QString path;
    int minLogicalZ = 6;
    int maxLogicalZ = 10;
    bool invertedZ = false;      //!< storedZ = zOffsetForInverted - logicalZ
    int zOffsetForInverted = 17;
    TileScheme scheme = TileScheme::XYZ;
    int sValue = 0;
    bool withSColumn = true;
    int tileSize = 256;
    //! Extension geografica que se va a poblar.
    QGeoCoordinate northWest{23.3, -85.0};
    QGeoCoordinate southEast{19.7, -74.0};
    //! false para anadir niveles a una BD ya creada (util para fabricar
    //! datasets con niveles heterogeneos, como los de las BD reales).
    bool createTable = true;

    /*!
     * \brief true para poblar el MUNDO ENTERO en vez del recuadro dado.
     *
     * Reproduce los niveles gruesos de la BD satelital real (z1..z5), que son
     * los unicos con teselas de mar. Sin ellos, mirar mar abierto no
     * encuentra ningun ancestro y la pantalla queda vacia.
     */
    bool worldCoverage = false;

    /*!
     * \brief Tinte del color de las teselas.
     *
     * Dos datasets distintos deben generar imagenes DISTINTAS para el mismo
     * z/x/y; si no, es imposible comprobar que la cache no los mezcla. Sin
     * esto, el test de cambio de capa no podia detectar el fallo real de que
     * TileKey no lleva el dataset.
     */
    int colorSeed = 0;
};

//! Devuelve el numero de teselas escritas, o -1 si fallo.
inline int buildSyntheticDb(const SyntheticSpec &spec)
{
    static int counter = 0;
    const QString conn = QStringLiteral("synth_%1_%2")
                             .arg(spec.path).arg(++counter);
    int written = 0;

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(spec.path);
        if (!db.open())
            return -1;

        QSqlQuery q(db);
        if (spec.createTable) {
            const QString sCol = spec.withSColumn
                                     ? QStringLiteral("s INTEGER, ")
                                     : QString();
            if (!q.exec(QStringLiteral("CREATE TABLE tiles (version INTEGER, "
                                       "z INTEGER, x INTEGER, y INTEGER, %1"
                                       "image BLOB)").arg(sCol)))
                return -1;

            const QString sIdx = spec.withSColumn ? QStringLiteral(", s") : QString();
            q.exec(QStringLiteral("CREATE INDEX IND ON tiles (z, x, y%1)").arg(sIdx));
        }

        db.transaction();

        for (int lz = spec.minLogicalZ; lz <= spec.maxLogicalZ; ++lz) {
            const int storedZ = spec.invertedZ
                                    ? (spec.zOffsetForInverted - lz)
                                    : lz;

            const auto range = spec.worldCoverage
                ? TileMatrix::rangeFor(QGeoCoordinate(85.0, -180.0),
                                       QGeoCoordinate(-85.0, 180.0), lz)
                : TileMatrix::rangeFor(spec.northWest, spec.southEast, lz);

            for (int x = range.xMin; x <= range.xMax; ++x) {
                for (int y = range.yMin; y <= range.yMax; ++y) {
                    // Imagen minima pero valida y del tamano pedido.
                    QImage img(spec.tileSize, spec.tileSize,
                               QImage::Format_RGB32);
                    img.fill(QColor((x * 37 + spec.colorSeed * 91) % 256,
                                    (y * 53 + spec.colorSeed * 37) % 256,
                                    (lz * 17 + spec.colorSeed * 61) % 256));
                    QByteArray blob;
                    QBuffer buf(&blob);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    buf.close();

                    const int storedY =
                        TileMatrix::toStorageY(y, lz, spec.scheme);

                    if (spec.withSColumn) {
                        q.prepare(QStringLiteral(
                            "INSERT INTO tiles (version,z,x,y,s,image) "
                            "VALUES (0,:z,:x,:y,:s,:img)"));
                        q.bindValue(QStringLiteral(":s"), spec.sValue);
                    } else {
                        q.prepare(QStringLiteral(
                            "INSERT INTO tiles (version,z,x,y,image) "
                            "VALUES (0,:z,:x,:y,:img)"));
                    }
                    q.bindValue(QStringLiteral(":z"), storedZ);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), storedY);
                    q.bindValue(QStringLiteral(":img"), blob);
                    if (!q.exec())
                        return -1;
                    ++written;
                }
            }
        }

        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return written;
}

//! Genera un MBTiles NORMALIZADO (tablas map+images, vista 'tiles', tabla
//! 'metadata') como los de MapTiler/tippecanoe: eje Y en TMS, zoom estandar
//! (storedZ = logicalZ) y 'bounds' en metadata. Sirve para probar que la sonda
//! lo reconoce sin --ref-bbox. Devuelve el numero de teselas, o -1 si fallo.
inline int buildSyntheticMbtiles(const QString &path,
                                 const QGeoCoordinate &nw,
                                 const QGeoCoordinate &se,
                                 int minZ, int maxZ)
{
    static int counter = 0;
    const QString conn = QStringLiteral("mbt_%1_%2").arg(path).arg(++counter);
    int written = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open())
            return -1;

        QSqlQuery q(db);
        q.exec(QStringLiteral("CREATE TABLE map (zoom_level INTEGER, "
                              "tile_column INTEGER, tile_row INTEGER, tile_id TEXT)"));
        q.exec(QStringLiteral("CREATE TABLE images (tile_id TEXT, tile_data BLOB)"));
        q.exec(QStringLiteral(
            "CREATE VIEW tiles AS SELECT map.zoom_level AS zoom_level, "
            "map.tile_column AS tile_column, map.tile_row AS tile_row, "
            "images.tile_data AS tile_data FROM map "
            "JOIN images ON map.tile_id = images.tile_id"));
        q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)"));
        q.prepare(QStringLiteral("INSERT INTO metadata VALUES (:n,:v)"));
        const QString bounds = QStringLiteral("%1,%2,%3,%4")
            .arg(nw.longitude()).arg(se.latitude())
            .arg(se.longitude()).arg(nw.latitude());
        const QList<QPair<QString, QString>> meta{
            {QStringLiteral("name"), QStringLiteral("Prueba")},
            {QStringLiteral("format"), QStringLiteral("png")},
            {QStringLiteral("minzoom"), QString::number(minZ)},
            {QStringLiteral("maxzoom"), QString::number(maxZ)},
            {QStringLiteral("bounds"), bounds}};
        for (const auto &m : meta) {
            q.bindValue(QStringLiteral(":n"), m.first);
            q.bindValue(QStringLiteral(":v"), m.second);
            q.exec();
        }

        db.transaction();
        int tid = 0;
        for (int lz = minZ; lz <= maxZ; ++lz) {
            const auto range = TileMatrix::rangeFor(nw, se, lz);
            for (int x = range.xMin; x <= range.xMax; ++x) {
                for (int y = range.yMin; y <= range.yMax; ++y) {
                    QImage img(256, 256, QImage::Format_RGB32);
                    img.fill(QColor((x * 37) % 256, (y * 53) % 256, (lz * 17) % 256));
                    QByteArray blob;
                    QBuffer buf(&blob);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    buf.close();

                    const QString id = QString::number(tid++);
                    const int yTms = TileMatrix::toStorageY(y, lz, TileScheme::TMS);
                    q.prepare(QStringLiteral("INSERT INTO images VALUES (:id,:img)"));
                    q.bindValue(QStringLiteral(":id"), id);
                    q.bindValue(QStringLiteral(":img"), blob);
                    if (!q.exec())
                        return -1;
                    q.prepare(QStringLiteral("INSERT INTO map VALUES (:z,:x,:y,:id)"));
                    q.bindValue(QStringLiteral(":z"), lz);
                    q.bindValue(QStringLiteral(":x"), x);
                    q.bindValue(QStringLiteral(":y"), yTms);
                    q.bindValue(QStringLiteral(":id"), id);
                    if (!q.exec())
                        return -1;
                    ++written;
                }
            }
        }
        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return written;
}

} // namespace test
} // namespace libmapa

#endif // LIBMAPA_TEST_SYNTHETICTILEDB_H_
