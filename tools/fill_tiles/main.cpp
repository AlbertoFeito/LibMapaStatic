/*!
 * fill_tiles - rellena los huecos de una base de teselas SQLite descargando las
 * teselas que faltan de una fuente XYZ sin clave (por defecto Esri World
 * Imagery, satelite).
 *
 * Por que: la BD satelital del proyecto solo trae teselas donde hay tierra y a
 * zoom alto queda al 32 % (mucho mar y detalle sin cubrir). Esta herramienta
 * abre la BD EXISTENTE, mira que teselas faltan en un rectangulo y rango de
 * zoom, las baja y las inserta CON LA MISMA CODIFICACION que ya usa esa base
 * (columna z con zFactor/zOffset, esquema XYZ/TMS y columna s).
 *
 * La codificacion se toma del datasets.json (--id): asi no hay que teclear a
 * mano zFactor/sValue y coincide con lo que lee la libreria al pintar.
 *
 * Uso tipico (rellenar la satelital sobre Cuba, z6..z12):
 *   fill_tiles --datasets datasets.json --id satelital \
 *              --bbox 23.3,-85.0,19.7,-74.0 --minzoom 6 --maxzoom 12
 *
 * Fuente por defecto (sin API key):
 *   https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}
 *
 * AVISO: respeta los terminos de uso de la fuente que utilices. La descarga
 * masiva desde servidores publicos suele estar limitada o prohibida; Esri World
 * Imagery permite muchos usos, pero revisa sus condiciones. No uses
 * tile.openstreetmap.org para descarga masiva (su politica lo prohibe).
 */

#include "geo/TileMatrix.h"
#include "tiles/TileDataset.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>

using namespace libmapa;

namespace {

QTextStream &cout()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream &cerr()
{
    static QTextStream s(stderr);
    return s;
}

//! Sustituye {z} {x} {y} en la plantilla (en cualquier orden).
QString buildUrl(const QString &tpl, int z, int x, int y)
{
    QString u = tpl;
    u.replace(QStringLiteral("{z}"), QString::number(z));
    u.replace(QStringLiteral("{x}"), QString::number(x));
    u.replace(QStringLiteral("{y}"), QString::number(y));
    return u;
}

//! Resultado de una descarga.
enum class Fetch { Ok, NotFound, Failed };

/*!
 * \brief GET sincrono de una tesela. Sigue redirecciones y comprueba que la
 *        respuesta sea realmente una imagen (no una pagina de error).
 */
Fetch httpGetTile(QNetworkAccessManager &nam, const QUrl &url,
                  const QByteArray &userAgent, int timeoutMs,
                  QByteArray *out, QString *err)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // En Qt 6 la politica por defecto ya sigue redirecciones seguras.
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif

    QNetworkReply *reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    const bool timedOut = !timer.isActive();  // salto el temporizador
    timer.stop();

    if (reply->isRunning()) {
        reply->abort();
        if (err) *err = timedOut ? QStringLiteral("timeout")
                                 : QStringLiteral("abortada");
        reply->deleteLater();
        return Fetch::Failed;
    }

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();

    if (status == 404 || status == 204) {
        reply->deleteLater();
        return Fetch::NotFound;   // el origen no tiene esa tesela
    }
    if (netErr != QNetworkReply::NoError || (status != 0 && status != 200)) {
        if (err) *err = QStringLiteral("HTTP %1 %2")
                            .arg(status).arg(reply->errorString());
        reply->deleteLater();
        return Fetch::Failed;
    }

    const QByteArray body = reply->readAll();
    const QString ctype =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    reply->deleteLater();

    if (body.isEmpty()) {
        if (err) *err = QStringLiteral("respuesta vacia");
        return Fetch::Failed;
    }
    // Si el servidor devuelve HTML/JSON de error con 200, no lo guardamos.
    if (!ctype.isEmpty() && !ctype.startsWith(QLatin1String("image"))) {
        if (err) *err = QStringLiteral("no es imagen (%1)").arg(ctype);
        return Fetch::Failed;
    }
    *out = body;
    return Fetch::Ok;
}

//! Rango de teselas [x0..x1] x [y0..y1] (indices XYZ) que cubre la bbox en z.
struct XYRange { int x0, x1, y0, y1; };
XYRange tileRange(double latN, double lonW, double latS, double lonE, int z)
{
    const int n = TileMatrix::tilesPerSide(z);
    int x0 = int(std::floor(TileMatrix::longitudeToTileX(lonW, z)));
    int x1 = int(std::floor(TileMatrix::longitudeToTileX(lonE, z)));
    int y0 = int(std::floor(TileMatrix::latitudeToTileY(latN, z)));  // lat alta -> y baja
    int y1 = int(std::floor(TileMatrix::latitudeToTileY(latS, z)));
    x0 = qBound(0, x0, n - 1); x1 = qBound(0, x1, n - 1);
    y0 = qBound(0, y0, n - 1); y1 = qBound(0, y1, n - 1);
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    return {x0, x1, y0, y1};
}

/*!
 * \brief Lee un datasets.json y devuelve el dataset con ese id.
 *
 * Reutiliza TileDataset::fromJson (la MISMA lectura que hace la libreria al
 * pintar) y resuelve la ruta relativa del .sqlitedb respecto al propio JSON,
 * igual que TileService::loadDatasets. Se hace aqui para que la herramienta no
 * dependa de todo el motor de teselas.
 */
bool loadDataset(const QString &jsonPath, const QString &id,
                 TileDataset *out, QString *err)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *err = QStringLiteral("No se pudo abrir %1").arg(jsonPath);
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (doc.isNull()) {
        *err = QStringLiteral("JSON invalido: %1").arg(pe.errorString());
        return false;
    }
    const QDir baseDir = QFileInfo(jsonPath).absoluteDir();
    const QJsonArray arr = doc.object().value(QStringLiteral("datasets")).toArray();
    for (const QJsonValue &v : arr) {
        TileDataset d = TileDataset::fromJson(v.toObject());
        if (d.id != id)
            continue;
        if (!d.filePath.isEmpty() && QFileInfo(d.filePath).isRelative())
            d.filePath = baseDir.absoluteFilePath(d.filePath);
        if (!d.isValid()) {
            *err = QStringLiteral("El dataset '%1' no es valido").arg(id);
            return false;
        }
        *out = d;
        return true;
    }
    *err = QStringLiteral("No hay un dataset con id '%1' en %2").arg(id, jsonPath);
    return false;
}

//! Conjunto de teselas ya presentes en un zoom (indices de ALMACENAMIENTO).
QSet<QPair<int, int>> existentes(QSqlDatabase &db, const TileDataset &ds,
                                 int storedZ, int x0, int x1,
                                 int syMin, int syMax)
{
    QSet<QPair<int, int>> set;
    QString sql = QStringLiteral(
        "SELECT %1,%2 FROM %3 WHERE %4=:z AND %1 BETWEEN :x0 AND :x1 "
        "AND %2 BETWEEN :y0 AND :y1")
        .arg(ds.colX, ds.colY, ds.tableName, ds.colZ);
    if (ds.hasSColumn)
        sql += QStringLiteral(" AND %1=:s").arg(ds.colS);

    QSqlQuery q(db);
    q.prepare(sql);
    q.bindValue(QStringLiteral(":z"), storedZ);
    q.bindValue(QStringLiteral(":x0"), x0);
    q.bindValue(QStringLiteral(":x1"), x1);
    q.bindValue(QStringLiteral(":y0"), syMin);
    q.bindValue(QStringLiteral(":y1"), syMax);
    if (ds.hasSColumn)
        q.bindValue(QStringLiteral(":s"), ds.sValue);
    if (!q.exec()) {
        cerr() << "Aviso: no se pudo leer lo existente en z=" << storedZ
               << ": " << q.lastError().text() << '\n';
        return set;
    }
    while (q.next())
        set.insert(qMakePair(q.value(0).toInt(), q.value(1).toInt()));
    return set;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // Que Qt use el proxy del sistema (variables HTTP(S)_PROXY, etc.).
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    QString datasetsPath, id, bbox;
    QString url = QStringLiteral(
        "https://server.arcgisonline.com/ArcGIS/rest/services/"
        "World_Imagery/MapServer/tile/{z}/{y}/{x}");
    QByteArray userAgent = "LibMapaStatic-fill_tiles/1.0";
    int minZoom = -1, maxZoom = -1;
    double rate = 2.0;     // peticiones por segundo
    int retries = 3;
    int timeoutMs = 20000;
    bool overwrite = false;
    bool assumeYes = false;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &k = args.at(i);
        const auto val = [&]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (k == QLatin1String("--datasets")) datasetsPath = val();
        else if (k == QLatin1String("--id")) id = val();
        else if (k == QLatin1String("--bbox")) bbox = val();
        else if (k == QLatin1String("--url")) url = val();
        else if (k == QLatin1String("--minzoom")) minZoom = val().toInt();
        else if (k == QLatin1String("--maxzoom")) maxZoom = val().toInt();
        else if (k == QLatin1String("--rate")) rate = val().toDouble();
        else if (k == QLatin1String("--retries")) retries = val().toInt();
        else if (k == QLatin1String("--timeout")) timeoutMs = val().toInt();
        else if (k == QLatin1String("--user-agent")) userAgent = val().toUtf8();
        else if (k == QLatin1String("--overwrite")) overwrite = true;
        else if (k == QLatin1String("--only-missing")) overwrite = false;
        else if (k == QLatin1String("--yes") || k == QLatin1String("-y")) assumeYes = true;
        else { cerr() << "Opcion desconocida: " << k << '\n'; return 2; }
    }

    if (datasetsPath.isEmpty() || id.isEmpty() || bbox.isEmpty()) {
        cout() << "Uso: fill_tiles --datasets datasets.json --id satelital \\\n"
                  "                --bbox latN,lonO,latS,lonE \\\n"
                  "                [--minzoom N --maxzoom N] \\\n"
                  "                [--url \"...{z}/{y}/{x}...\"]  (por defecto Esri satelite)\n"
                  "                [--only-missing (def) | --overwrite]\n"
                  "                [--rate 2] [--retries 3] [--timeout 20000] [--yes]\n\n"
                  "La codificacion (z/s/esquema) se toma del dataset indicado con --id.\n"
                  "AVISO: respeta los terminos de uso de la fuente que utilices.\n";
        return 2;
    }

    // --- bbox: latN,lonO,latS,lonE ------------------------------------------
    const QStringList bp = bbox.split(QLatin1Char(','));
    if (bp.size() != 4) {
        cerr() << "bbox debe ser latN,lonO,latS,lonE\n";
        return 2;
    }
    const double latN = bp.at(0).trimmed().toDouble();
    const double lonW = bp.at(1).trimmed().toDouble();
    const double latS = bp.at(2).trimmed().toDouble();
    const double lonE = bp.at(3).trimmed().toDouble();

    // --- dataset (codificacion + ruta de la BD) -----------------------------
    QString err;
    TileDataset ds;
    if (!loadDataset(datasetsPath, id, &ds, &err)) {
        cerr() << err << '\n';
        return 1;
    }
    if (!QFileInfo::exists(ds.filePath)) {
        cerr() << "No existe la BD del dataset: " << ds.filePath << '\n';
        return 1;
    }

    if (minZoom < 0) minZoom = ds.minZoom;
    if (maxZoom < 0) maxZoom = ds.maxZoom;
    minZoom = qBound(0, minZoom, 22);
    maxZoom = qBound(minZoom, maxZoom, 22);

    cout() << "Dataset '" << ds.id << "'  BD: " << ds.filePath << '\n';
    cout() << "Esquema " << tileSchemeToString(ds.scheme)
           << "  storedZ = " << ds.zFactor << "*z + " << ds.zOffset
           << (ds.hasSColumn ? QStringLiteral("  s=%1").arg(ds.sValue) : QString())
           << '\n';
    cout() << "BBox latN=" << latN << " lonO=" << lonW
           << " latS=" << latS << " lonE=" << lonE
           << "  zoom " << minZoom << ".." << maxZoom << '\n';
    cout().flush();

    // --- Abrir la BD EXISTENTE (sin borrarla) -------------------------------
    // Todo el uso de la BD va dentro de esta IIFE para que la QSqlDatabase se
    // destruya ANTES de removeDatabase (patron de geo_to_tiles): asi Qt no
    // avisa de que la conexion "sigue en uso".
    const int rc = [&]() -> int {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("fill"));
    db.setDatabaseName(ds.filePath);
    if (!db.open()) {
        cerr() << "No se pudo abrir " << ds.filePath << ": "
               << db.lastError().text() << '\n';
        return 1;
    }

    // --- 1a pasada: contar lo que falta por zoom ----------------------------
    struct PlanZ { int z, x0, x1, y0, y1, syMin, syMax; qint64 faltan; };
    QVector<PlanZ> plan;
    qint64 totalFaltan = 0, totalCeldas = 0;

    for (int z = minZoom; z <= maxZoom; ++z) {
        const XYRange r = tileRange(latN, lonW, latS, lonE, z);
        const int sy0 = TileMatrix::toStorageY(r.y0, z, ds.scheme);
        const int sy1 = TileMatrix::toStorageY(r.y1, z, ds.scheme);
        const int syMin = std::min(sy0, sy1);
        const int syMax = std::max(sy0, sy1);
        const int storedZ = ds.storedZ(z);

        const qint64 celdas = qint64(r.x1 - r.x0 + 1) * qint64(r.y1 - r.y0 + 1);
        qint64 faltan = celdas;
        if (!overwrite) {
            const QSet<QPair<int, int>> yaHay =
                existentes(db, ds, storedZ, r.x0, r.x1, syMin, syMax);
            faltan = celdas - yaHay.size();
            if (faltan < 0) faltan = 0;
        }
        plan.append({z, r.x0, r.x1, r.y0, r.y1, syMin, syMax, faltan});
        totalFaltan += faltan;
        totalCeldas += celdas;
        cout() << "  z=" << z << ": " << (r.x1 - r.x0 + 1) << "x"
               << (r.y1 - r.y0 + 1) << " = " << celdas << " teselas, "
               << (overwrite ? celdas : faltan)
               << (overwrite ? " a reescribir" : " faltan") << '\n';
    }
    cout() << "TOTAL a descargar: " << totalFaltan
           << " teselas (de " << totalCeldas << " en la zona)\n";
    cout().flush();

    if (totalFaltan == 0) {
        cout() << "No falta ninguna tesela en esa zona y zoom. Nada que hacer.\n";
        return 0;
    }

    // --- Confirmacion -------------------------------------------------------
    if (!assumeYes) {
        cout() << "Descargar " << totalFaltan << " teselas de " << url
               << " ? [s/N] ";
        cout().flush();
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "s" && resp != "S" && resp != "y" && resp != "Y") {
            cout() << "Cancelado.\n";
            return 0;
        }
    }

    // --- 2a pasada: descargar e insertar ------------------------------------
    QNetworkAccessManager nam;
    QSqlQuery ins(db);
    {
        QString sql = QStringLiteral("INSERT OR REPLACE INTO %1 (%2,%3,%4")
                          .arg(ds.tableName, ds.colX, ds.colY, ds.colZ);
        if (ds.hasSColumn) sql += QStringLiteral(",%1").arg(ds.colS);
        sql += QStringLiteral(",%1) VALUES (:x,:y,:z").arg(ds.colImage);
        if (ds.hasSColumn) sql += QStringLiteral(",:s");
        sql += QStringLiteral(",:img)");
        ins.prepare(sql);
    }

    const qint64 minIntervalMs = rate > 0 ? qint64(1000.0 / rate) : 0;
    QElapsedTimer sinceLast;

    qint64 bajadas = 0, saltadas = 0, sinOrigen = 0, fallidas = 0, hechas = 0;

    for (const PlanZ &pz : plan) {
        const int storedZ = ds.storedZ(pz.z);
        QSet<QPair<int, int>> yaHay;
        if (!overwrite)
            yaHay = existentes(db, ds, storedZ, pz.x0, pz.x1, pz.syMin, pz.syMax);

        QSqlQuery tx(db);
        tx.exec(QStringLiteral("BEGIN"));
        qint64 enZ = 0;

        for (int x = pz.x0; x <= pz.x1; ++x) {
            for (int y = pz.y0; y <= pz.y1; ++y) {
                const int sy = TileMatrix::toStorageY(y, pz.z, ds.scheme);
                if (!overwrite && yaHay.contains(qMakePair(x, sy))) {
                    ++saltadas;
                    continue;
                }

                // Respeta el limite de velocidad.
                if (minIntervalMs > 0 && sinceLast.isValid()) {
                    const qint64 waited = sinceLast.elapsed();
                    if (waited < minIntervalMs)
                        QThread::msleep(static_cast<unsigned long>(minIntervalMs - waited));
                }
                sinceLast.restart();

                // La URL usa indices XYZ (x, y estandar); la fuente decide el
                // orden con la plantilla ({z}/{y}/{x} en Esri).
                const QUrl u(buildUrl(url, pz.z, x, y));
                QByteArray img;
                Fetch res = Fetch::Failed;
                QString e;
                for (int intento = 0; intento <= retries; ++intento) {
                    res = httpGetTile(nam, u, userAgent, timeoutMs, &img, &e);
                    if (res != Fetch::Failed) break;
                    if (intento < retries)
                        QThread::msleep(static_cast<unsigned long>(300 * (intento + 1)));  // backoff
                }

                if (res == Fetch::NotFound) { ++sinOrigen; continue; }
                if (res == Fetch::Failed) {
                    ++fallidas;
                    if (fallidas <= 10)
                        cerr() << "  fallo z" << pz.z << " x" << x << " y" << y
                               << ": " << e << '\n';
                    continue;
                }

                ins.bindValue(QStringLiteral(":x"), x);
                ins.bindValue(QStringLiteral(":y"), sy);
                ins.bindValue(QStringLiteral(":z"), storedZ);
                if (ds.hasSColumn)
                    ins.bindValue(QStringLiteral(":s"), ds.sValue);
                ins.bindValue(QStringLiteral(":img"), img);
                if (!ins.exec()) {
                    ++fallidas;
                    cerr() << "  INSERT fallo: " << ins.lastError().text() << '\n';
                    continue;
                }
                ++bajadas;
                ++enZ;

                if (++hechas % 200 == 0) {
                    cout() << "\r  progreso: " << hechas << "/" << totalFaltan
                           << " (z" << pz.z << ")   ";
                    cout().flush();
                }
            }
        }
        tx.exec(QStringLiteral("COMMIT"));
        cout() << "\r  z=" << pz.z << ": +" << enZ << " teselas nuevas        \n";
        cout().flush();
    }

    cout() << "\nListo. Descargadas: " << bajadas
           << "  ya existian: " << saltadas
           << "  sin origen(404): " << sinOrigen
           << "  fallidas: " << fallidas << '\n';
    if (fallidas > 0)
        cout() << "Puedes volver a ejecutar para reintentar solo las que faltan.\n";

    return fallidas > 0 ? 3 : 0;
    }();  // fin de la IIFE: aqui muere 'db' (y sus QSqlQuery)

    // Con 'db' ya destruido, quitar la conexion no avisa de "still in use".
    QSqlDatabase::removeDatabase(QStringLiteral("fill"));
    return rc;
}
