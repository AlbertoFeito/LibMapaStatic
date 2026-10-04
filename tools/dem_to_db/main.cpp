// dem_to_db - Construye una base de datos de elevacion (.sqlitedb) a partir de
// una carpeta de ficheros SRTM `.hgt`. La BD resultante la lee SqliteElevation y
// es un unico fichero comodo para empaquetar dentro de una app.
//
// Uso:
//   dem_to_db <carpeta_hgt> --out cuba_dem.sqlitedb [--overwrite]
//
// Cada tile se guarda como un blob qCompress de sus muestras int16 big-endian
// (las MISMAS que el .hgt), con su lado, en la tabla dem_tiles. El mar y las
// llanuras comprimen muchisimo, asi que la BD pesa bastante menos que los .hgt.

#include "db/Transaction.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>   // Qt5 solo declara QVariant aqui para bindValue
#include <QtEndian>

#include <cmath>
#include <cstdio>

using namespace libmapa;

namespace {

// Raiz cuadrada entera exacta: n si n*n == v, si no 0. Deduce el lado del tile.
int isqrtExact(qint64 v)
{
    if (v < 0)
        return 0;
    qint64 n = qint64(std::llround(std::sqrt(double(v))));
    while (n * n > v)
        --n;
    while ((n + 1) * (n + 1) <= v)
        ++n;
    return (n * n == v) ? int(n) : 0;
}

// Saca (lat,lon) de la esquina SO del nombre SRTM `N19W077` (sin extension).
// Devuelve false si el nombre no encaja con el patron.
bool parseName(const QString &base, int &lat, int &lon)
{
    static const QRegularExpression re(
        QStringLiteral("^([NS])(\\d{2})([EW])(\\d{3})$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(base.toUpper());
    if (!m.hasMatch())
        return false;
    lat = m.captured(2).toInt();
    if (m.captured(1).compare(QLatin1String("S"), Qt::CaseInsensitive) == 0)
        lat = -lat;
    lon = m.captured(4).toInt();
    if (m.captured(3).compare(QLatin1String("W"), Qt::CaseInsensitive) == 0)
        lon = -lon;
    return true;
}

// Resolucion nominal en metros a partir del lado (solo para el metadato).
QString resolucionDe(int side)
{
    if (side == 3601) return QStringLiteral("30");
    if (side == 1201) return QStringLiteral("90");
    return QString::number(side);   // otro lado: se deja el lado como pista
}

} // namespace

// Punto de entrada: valida argumentos, crea la BD, recorre los `.hgt` de la
// carpeta y los inserta comprimidos dentro de una transaccion; al final escribe
// los metadatos y un resumen con el ahorro de tamano.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString carpeta;
    QString salida;
    bool overwrite = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--out") && i + 1 < argc)
            salida = QString::fromLocal8Bit(argv[++i]);
        else if (a == QLatin1String("--overwrite"))
            overwrite = true;
        else if (!a.startsWith(QLatin1String("--")) && carpeta.isEmpty())
            carpeta = a;
    }

    if (carpeta.isEmpty() || salida.isEmpty()) {
        std::fprintf(stderr,
            "Uso: dem_to_db <carpeta_hgt> --out fichero.sqlitedb [--overwrite]\n");
        return 2;
    }

    QDir dir(carpeta);
    if (!dir.exists()) {
        std::fprintf(stderr, "No existe la carpeta: %s\n",
                     qPrintable(carpeta));
        return 1;
    }
    const QStringList hgts = dir.entryList({QStringLiteral("*.hgt")}, QDir::Files,
                                           QDir::Name);
    if (hgts.isEmpty()) {
        std::fprintf(stderr, "No hay ficheros .hgt en: %s\n", qPrintable(carpeta));
        return 1;
    }

    if (QFile::exists(salida)) {
        if (!overwrite) {
            std::fprintf(stderr,
                "Ya existe %s (usa --overwrite para rehacerla).\n",
                qPrintable(salida));
            return 1;
        }
        QFile::remove(salida);
    }

    // Crear la BD nueva (patron de geo_to_tiles: addDatabase propio, sin journal).
    int rc = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("demgen"));
        db.setDatabaseName(salida);
        if (!db.open()) {
            std::fprintf(stderr, "No se pudo crear %s: %s\n", qPrintable(salida),
                         qPrintable(db.lastError().text()));
            return 1;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA journal_mode=OFF"));
        q.exec(QStringLiteral("PRAGMA synchronous=OFF"));
        if (!q.exec(QStringLiteral(
                "CREATE TABLE dem_tiles (lat INTEGER, lon INTEGER, side INTEGER,"
                " data BLOB, PRIMARY KEY(lat,lon))"))
            || !q.exec(QStringLiteral(
                "CREATE TABLE dem_meta (key TEXT PRIMARY KEY, value TEXT)"))) {
            std::fprintf(stderr, "No se pudo crear el esquema: %s\n",
                         qPrintable(q.lastError().text()));
            rc = 1;
        }

        QElapsedTimer reloj; reloj.start();
        qint64 insertados = 0, saltados = 0;
        qint64 bytesCrudos = 0, bytesComp = 0;
        int sidePredominante = 0;

        if (rc == 0) {
            Transaction tx(db);
            if (!tx.isActive()) {
                std::fprintf(stderr, "No se pudo abrir la transaccion.\n");
                rc = 1;
            } else {
                QSqlQuery ins(db);
                ins.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO dem_tiles (lat,lon,side,data)"
                    " VALUES (:lat,:lon,:side,:data)"));

                for (const QString &nombre : hgts) {
                    const QString base = QFileInfo(nombre).completeBaseName();
                    int lat = 0, lon = 0;
                    if (!parseName(base, lat, lon)) {
                        std::fprintf(stderr, "  (omito, nombre raro) %s\n",
                                     qPrintable(nombre));
                        ++saltados;
                        continue;
                    }
                    QFile f(dir.filePath(nombre));
                    if (!f.open(QIODevice::ReadOnly)) {
                        std::fprintf(stderr, "  (no abre) %s\n", qPrintable(nombre));
                        ++saltados;
                        continue;
                    }
                    const QByteArray raw = f.readAll();
                    const int side = isqrtExact(qint64(raw.size()) / 2);
                    if (side < 2 || qint64(side) * side * 2 != raw.size()) {
                        std::fprintf(stderr, "  (tamano raro) %s\n",
                                     qPrintable(nombre));
                        ++saltados;
                        continue;
                    }
                    const QByteArray comp = qCompress(raw, 9);
                    ins.bindValue(QStringLiteral(":lat"), lat);
                    ins.bindValue(QStringLiteral(":lon"), lon);
                    ins.bindValue(QStringLiteral(":side"), side);
                    ins.bindValue(QStringLiteral(":data"), comp);
                    if (!ins.exec()) {
                        std::fprintf(stderr, "  (INSERT fallo) %s: %s\n",
                                     qPrintable(nombre),
                                     qPrintable(ins.lastError().text()));
                        ++saltados;
                        continue;
                    }
                    ++insertados;
                    bytesCrudos += raw.size();
                    bytesComp += comp.size();
                    if (!sidePredominante) sidePredominante = side;
                    std::printf("  %s  (%dx%d, %.0f%% del crudo)\n",
                                qPrintable(base), side, side,
                                100.0 * double(comp.size()) / double(raw.size()));
                }

                // Metadatos (informativos).
                auto meta = [&](const QString &k, const QString &v) {
                    QSqlQuery m(db);
                    m.prepare(QStringLiteral(
                        "INSERT OR REPLACE INTO dem_meta (key,value) VALUES (:k,:v)"));
                    m.bindValue(QStringLiteral(":k"), k);
                    m.bindValue(QStringLiteral(":v"), v);
                    m.exec();
                };
                meta(QStringLiteral("resolution_m"), resolucionDe(sidePredominante));
                meta(QStringLiteral("source"), QStringLiteral("SRTM .hgt"));
                meta(QStringLiteral("tile_count"), QString::number(insertados));
                meta(QStringLiteral("created_utc"),
                     QString::number(QDateTime::currentSecsSinceEpoch()));

                if (!tx.commit()) {
                    std::fprintf(stderr, "No se pudo confirmar: %s\n",
                                 qPrintable(db.lastError().text()));
                    rc = 1;
                }
            }
        }

        if (rc == 0) {
            const qint64 tam = QFileInfo(salida).size();
            std::printf("\nListo: %lld tiles en %s (%lld omitidos) en %.1fs\n",
                        static_cast<long long>(insertados), qPrintable(salida),
                        static_cast<long long>(saltados),
                        double(reloj.elapsed()) / 1000.0);
            std::printf("Tamano: BD %.1f MB  (muestras crudas %.1f MB; "
                        "comprimidas %.1f MB)\n",
                        double(tam) / 1048576.0, double(bytesCrudos) / 1048576.0,
                        double(bytesComp) / 1048576.0);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("demgen"));
    return rc;
}
