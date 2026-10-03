// fill_hgt - Descarga ficheros de elevacion SRTM `.hgt` (30 m) de AWS Skadi, sin
// clave, para una zona (bbox o el preset de Cuba). Descomprime el `.hgt.gz` al
// vuelo (miniz, sin zlib) y deja los `.hgt` en una carpeta, lista para `dem_to_db`.
//
// Uso:
//   fill_hgt --cuba --out carpeta
//   fill_hgt --bbox latN,lonO,latS,lonE --out carpeta [--res 30]
//
// Es reanudable (salta los `.hgt` que ya existen) y salta los tiles que la fuente
// no tiene (mar abierto -> 404). Fuente por defecto: elevation-tiles-prod (Skadi).

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "miniz.h"

#include <cmath>
#include <cstdio>

namespace {

// Nombre SRTM del tile cuya esquina SO es (latFloor, lonFloor): N19W077.
QString tileName(int latFloor, int lonFloor)
{
    const QChar ns = latFloor >= 0 ? QLatin1Char('N') : QLatin1Char('S');
    const QChar ew = lonFloor >= 0 ? QLatin1Char('E') : QLatin1Char('W');
    return QStringLiteral("%1%2%3%4")
        .arg(ns)
        .arg(qAbs(latFloor), 2, 10, QLatin1Char('0'))
        .arg(ew)
        .arg(qAbs(lonFloor), 3, 10, QLatin1Char('0'));
}

// Descomprime en memoria un buffer gzip (como los `.hgt.gz` de Skadi). Como miniz
// no entiende el envoltorio gzip, se parsea a mano la cabecera (RFC 1952) para
// quedarnos con el DEFLATE crudo, y se infla con tinfl de miniz. Vacio si falla.
// Asi no hace falta zlib: la herramienta es autonoma en cualquier plataforma.
QByteArray gunzip(const QByteArray &in)
{
    const qsizetype n = in.size();
    if (n < 18)   // 10 de cabecera minima + algo + 8 de cola (CRC32 + ISIZE)
        return {};
    const uchar *p = reinterpret_cast<const uchar *>(in.constData());
    if (p[0] != 0x1f || p[1] != 0x8b || p[2] != 0x08)   // magia gzip + DEFLATE
        return {};
    const uchar flg = p[3];
    qsizetype pos = 10;                                  // cabecera fija
    if (flg & 0x04) {                                    // FEXTRA
        if (pos + 2 > n) return {};
        const int xlen = int(p[pos]) | (int(p[pos + 1]) << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { while (pos < n && p[pos] != 0) ++pos; ++pos; }  // FNAME
    if (flg & 0x10) { while (pos < n && p[pos] != 0) ++pos; ++pos; }  // FCOMMENT
    if (flg & 0x02) pos += 2;                            // FHCRC
    if (pos < 0 || pos >= n - 8)
        return {};

    const qsizetype deflateLen = n - 8 - pos;
    size_t outLen = 0;
    void *out = tinfl_decompress_mem_to_heap(p + pos, size_t(deflateLen),
                                             &outLen, 0);
    if (!out)
        return {};
    const QByteArray raw(reinterpret_cast<const char *>(out), qsizetype(outLen));
    mz_free(out);
    return raw;
}

// GET sincrono con timeout. Devuelve los bytes (vacio si error) y deja el codigo
// HTTP en \a status (0 = fallo de red/timeout, 404 = la fuente no tiene el tile).
QByteArray httpGet(QNetworkAccessManager &nam, const QUrl &url, int timeoutMs,
                   int &status)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QByteArrayLiteral("LibMapaStatic-fillhgt/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = nam.get(req);

    QEventLoop loop;
    QTimer t;
    t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, [r] { r->abort(); });
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    t.start(timeoutMs);
    loop.exec();

    status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data;
    if (r->error() == QNetworkReply::NoError)
        data = r->readAll();
    r->deleteLater();
    return data;
}

// Raiz cuadrada entera exacta (para validar el tamano del .hgt descargado).
int isqrtExact(qint64 v)
{
    if (v < 0)
        return 0;
    qint64 n = qint64(std::llround(std::sqrt(double(v))));
    while (n * n > v) --n;
    while ((n + 1) * (n + 1) <= v) ++n;
    return (n * n == v) ? int(n) : 0;
}

} // namespace

// Punto de entrada: arma la lista de tiles del bbox (o del preset), y por cada
// uno que falte baja el `.hgt.gz`, lo descomprime y lo valida. Reanudable.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    double latN = 0, lonO = 0, latS = 0, lonE = 0;
    bool tieneZona = false;
    QString salida;
    QString urlBase = QStringLiteral(
        "https://s3.amazonaws.com/elevation-tiles-prod/skadi");
    int timeoutMs = 60000;   // tiles de 30 m pesan varios MB
    int reintentos = 2;

    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--cuba")) {
            latN = 24; lonO = -85; latS = 19; lonE = -74;   // Cuba + margen
            tieneZona = true;
        } else if (a == QLatin1String("--bbox") && i + 1 < argc) {
            const QStringList p = QString::fromLocal8Bit(argv[++i]).split(
                QLatin1Char(','));
            if (p.size() == 4) {
                latN = p[0].toDouble(); lonO = p[1].toDouble();
                latS = p[2].toDouble(); lonE = p[3].toDouble();
                tieneZona = true;
            }
        } else if (a == QLatin1String("--out") && i + 1 < argc) {
            salida = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--res") && i + 1 < argc) {
            ++i;   // de momento solo 30 m (Skadi); se acepta y se ignora
        } else if (a == QLatin1String("--url") && i + 1 < argc) {
            urlBase = QString::fromLocal8Bit(argv[++i]);
        }
    }

    if (!tieneZona || salida.isEmpty()) {
        std::fprintf(stderr,
            "Uso: fill_hgt (--cuba | --bbox latN,lonO,latS,lonE) --out carpeta\n"
            "     [--url base] [--res 30]\n"
            "Baja .hgt de 30 m de AWS Skadi (sin clave). Reanudable.\n");
        return 2;
    }

    QDir dir(salida);
    if (!dir.exists() && !QDir().mkpath(salida)) {
        std::fprintf(stderr, "No se pudo crear la carpeta: %s\n",
                     qPrintable(salida));
        return 1;
    }

    const int latLo = int(std::floor(qMin(latN, latS)));
    const int latHi = int(std::floor(qMax(latN, latS)));
    const int lonLo = int(std::floor(qMin(lonO, lonE)));
    const int lonHi = int(std::floor(qMax(lonO, lonE)));

    QNetworkAccessManager nam;
    QElapsedTimer reloj; reloj.start();
    int bajados = 0, existian = 0, sinFuente = 0, fallidos = 0;
    qint64 bytesBajados = 0;

    for (int lat = latLo; lat <= latHi; ++lat) {
        for (int lon = lonLo; lon <= lonHi; ++lon) {
            const QString nombre = tileName(lat, lon);
            const QString destino = dir.filePath(nombre + QStringLiteral(".hgt"));
            if (QFile::exists(destino)) {
                ++existian;
                continue;
            }

            const QString sub = tileName(lat, lon).left(3);   // "N19"
            const QUrl url(QStringLiteral("%1/%2/%3.hgt.gz")
                               .arg(urlBase, sub, nombre));

            QByteArray gz;
            int status = 0;
            for (int intento = 0; intento <= reintentos; ++intento) {
                gz = httpGet(nam, url, timeoutMs, status);
                if (status == 200 && !gz.isEmpty())
                    break;
                if (status == 404)
                    break;                       // el origen no tiene el tile
                QThread::msleep(static_cast<unsigned long>(500 * (intento + 1)));  // backoff
            }

            if (status == 404) {
                ++sinFuente;
                continue;                        // mar abierto: normal
            }
            if (status != 200 || gz.isEmpty()) {
                std::fprintf(stderr, "  FALLO %s (HTTP %d)\n",
                             qPrintable(nombre), status);
                ++fallidos;
                continue;
            }

            const QByteArray raw = gunzip(gz);
            const int side = isqrtExact(qint64(raw.size()) / 2);
            if (side < 2 || qint64(side) * side * 2 != raw.size()) {
                std::fprintf(stderr, "  FALLO %s (descompresion/tamano)\n",
                             qPrintable(nombre));
                ++fallidos;
                continue;
            }

            QFile f(destino);
            if (!f.open(QIODevice::WriteOnly) || f.write(raw) != raw.size()) {
                std::fprintf(stderr, "  FALLO %s (no se pudo escribir)\n",
                             qPrintable(nombre));
                ++fallidos;
                continue;
            }
            f.close();
            ++bajados;
            bytesBajados += gz.size();
            std::printf("  OK %s  (%dx%d, %.1f MB)\n", qPrintable(nombre),
                        side, side, double(raw.size()) / 1048576.0);
            std::fflush(stdout);
        }
    }

    std::printf("\nListo en %.1fs: %d bajados, %d ya estaban, %d sin fuente, "
                "%d fallidos.\n", double(reloj.elapsed()) / 1000.0,
                bajados, existian, sinFuente, fallidos);
    std::printf("Descargados %.1f MB (gz). Ahora: dem_to_db \"%s\" --out dem.sqlitedb\n",
                double(bytesBajados) / 1048576.0, qPrintable(salida));
    return fallidos > 0 ? 1 : 0;
}
