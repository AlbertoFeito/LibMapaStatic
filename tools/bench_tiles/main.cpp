/*!
 * bench_tiles - mide el motor de teselas contra BD reales.
 *
 * Uso:
 *   bench_tiles --datasets datasets.json [--zoom 12] [--width 1280] [--height 800]
 *               [--center 23.1136,-82.3666] [--pans 40]
 *
 * Reporta, para cada capa:
 *   - cobertura exacta / por respaldo / huecos en el nivel pedido
 *   - tiempo de la primera carga y de la segunda (cache caliente)
 *   - efecto de la cancelacion al simular un arrastre
 */

#include "tiles/TileService.h"
#include "db/SqliteConnectionPool.h"
#include "core/Logging.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

using namespace libmapa;

static bool waitForTiles(TileService &svc, int timeoutMs = 30000)
{
    QEventLoop loop;
    bool got = false;
    QMetaObject::Connection c =
        QObject::connect(&svc, &TileService::tilesReady, &loop,
                         [&](quint64, int, int) { got = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    return got;
}

//! Espera al nivel concreto: con la escalera de respaldo llegan varias
//! senales antes que la del nivel de detalle.
static bool waitForZoom(TileService &svc, int zoom, int timeoutMs = 30000)
{
    QEventLoop loop;
    bool got = false;
    QMetaObject::Connection c =
        QObject::connect(&svc, &TileService::tilesReady, &loop,
                         [&](quint64, int z, int) {
                             if (z == zoom) { got = true; loop.quit(); }
                         });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    return got;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Banco de pruebas del motor de teselas"));
    parser.addHelpOption();

    QCommandLineOption optJson(QStringLiteral("datasets"),
        QStringLiteral("Ruta a datasets.json"), QStringLiteral("ruta"),
        QStringLiteral("datasets.json"));
    QCommandLineOption optZoom(QStringLiteral("zoom"),
        QStringLiteral("Zoom a medir (por defecto, el recomendado menos 2)"),
        QStringLiteral("z"), QStringLiteral("-1"));
    QCommandLineOption optW(QStringLiteral("width"),
        QStringLiteral("Ancho del viewport en pixeles"),
        QStringLiteral("px"), QStringLiteral("1280"));
    QCommandLineOption optH(QStringLiteral("height"),
        QStringLiteral("Alto del viewport en pixeles"),
        QStringLiteral("px"), QStringLiteral("800"));
    QCommandLineOption optCenter(QStringLiteral("center"),
        QStringLiteral("lat,lon del centro"), QStringLiteral("coord"),
        QStringLiteral("23.1136,-82.3666"));
    QCommandLineOption optPans(QStringLiteral("pans"),
        QStringLiteral("Numero de desplazamientos a simular"),
        QStringLiteral("n"), QStringLiteral("40"));

    parser.addOption(optJson);
    parser.addOption(optZoom);
    parser.addOption(optW);
    parser.addOption(optH);
    parser.addOption(optCenter);
    parser.addOption(optPans);
    parser.process(app);

    QString error;
    const auto datasets = TileService::loadDatasets(parser.value(optJson), &error);
    if (datasets.isEmpty()) {
        err << "No se pudieron cargar los datasets: " << error << "\n";
        return 2;
    }

    const QStringList centerParts = parser.value(optCenter).split(QLatin1Char(','));
    if (centerParts.size() != 2) {
        err << "--center mal formado. Formato: lat,lon\n";
        return 2;
    }
    const QGeoCoordinate center(centerParts[0].toDouble(),
                                centerParts[1].toDouble());

    const int viewW = parser.value(optW).toInt();
    const int viewH = parser.value(optH).toInt();
    const int pans = parser.value(optPans).toInt();
    const int forcedZoom = parser.value(optZoom).toInt();

    for (const TileDataset &ds : datasets) {
        TileService svc;
        svc.setDebounceMs(0);
        if (!svc.start({ds}, 256)) {
            err << "No se pudo arrancar " << ds.id << "\n";
            continue;
        }

        const int z = (forcedZoom >= 0)
                          ? forcedZoom
                          : qMax(ds.minZoom, ds.recommendedMaxZoom - 2);

        // Extension geografica que ocuparia el viewport a ese zoom.
        const double worldPx = 256.0 * (1 << z);
        const double spanLon = 360.0 * viewW / worldPx;
        const double spanLat = spanLon * viewH / viewW;

        const QGeoCoordinate nw(center.latitude() + spanLat / 2,
                                center.longitude() - spanLon / 2);
        const QGeoCoordinate se(center.latitude() - spanLat / 2,
                                center.longitude() + spanLon / 2);

        out << "==========================================================\n";
        out << " " << ds.displayName << "  (" << ds.id << ")\n";
        out << "==========================================================\n";
        out << " zoom " << z << ", viewport " << viewW << "x" << viewH << "\n";
        out << " extension: lat " << QString::number(se.latitude(), 'f', 4)
            << ".." << QString::number(nw.latitude(), 'f', 4)
            << "  lon " << QString::number(nw.longitude(), 'f', 4)
            << ".." << QString::number(se.longitude(), 'f', 4) << "\n\n";

        // --- Carga en frio -------------------------------------------------
        QElapsedTimer timer;
        timer.start();
        svc.requestViewport(nw, se, z);
        const qint64 msReturn = timer.elapsed();

        // Primera senal: el nivel grueso de respaldo.
        const bool okBase = waitForTiles(svc);
        const qint64 msBase = timer.elapsed();

        if (!okBase) {
            out << " No llegaron teselas (timeout).\n\n";
            continue;
        }

        const auto early = svc.planFor(nw, se, z);

        // Y ahora se espera al nivel de detalle.
        waitForZoom(svc, z);
        const qint64 msTotal = timer.elapsed();

        out << " CARGA EN FRIO\n";
        out << "   requestViewport devolvio en " << msReturn << " ms\n";
        out << "   nivel grueso de respaldo tras " << msBase << " ms\n";
        out << "   nivel de detalle tras " << msTotal << " ms\n";
        out << "   en cache: " << svc.cache().count() << "\n\n";

        out << " COBERTURA CON SOLO EL NIVEL GRUESO (a los " << msBase << " ms)\n";
        out << "   exactas " << early.exactCount
            << ", por respaldo " << early.fallbackCount
            << ", huecos " << early.emptyCount
            << " -> " << QString::number(early.coverage() * 100.0, 'f', 1)
            << " %\n\n";

        // --- Cobertura -----------------------------------------------------
        const auto plan = svc.planFor(nw, se, z);
        out << " COBERTURA EN PANTALLA\n";
        out << "   huecos de la rejilla : " << plan.slotCount() << "\n";
        out << "   con tesela exacta    : " << plan.exactCount << "\n";
        out << "   con respaldo de padre: " << plan.fallbackCount << "\n";
        out << "   sin nada que dibujar : " << plan.emptyCount << "\n";
        out << "   cobertura total      : "
            << QString::number(plan.coverage() * 100.0, 'f', 1) << " %\n";
        if (plan.slotCount() > 0) {
            const double exactPct = 100.0 * plan.exactCount / plan.slotCount();
            out << "   (sin respaldo, la pantalla estaria al "
                << QString::number(exactPct, 'f', 1) << " %)\n";
        }

        // Cuanto se esta ampliando cada respaldo. Un ancestro a profundidad d
        // se agranda 2^d veces: a partir de 4 o 5 niveles deja de parecer un
        // mapa y es practicamente un color plano.
        if (plan.fallbackCount > 0) {
            QMap<int, int> porProfundidad;
            for (const TileDrawItem &i : plan.items)
                if (!i.isExact())
                    ++porProfundidad[i.ancestorDepth];

            out << "\n   respaldo por profundidad:\n";
            for (auto it = porProfundidad.constBegin();
                 it != porProfundidad.constEnd(); ++it) {
                out << "     " << it.value() << " huecos con un ancestro "
                    << it.key() << " niveles arriba (ampliado "
                    << (1 << it.key()) << "x)";
                if (it.key() >= 5) {
                    // A esta ampliacion la tesela es practicamente un color
                    // plano. Sobre MAR eso es exactamente lo correcto: la BD
                    // satelital no guarda teselas de mar por encima de los
                    // niveles gruesos, y un azul uniforme es la imagen buena.
                    // Sobre TIERRA, en cambio, indica que faltan niveles.
                    out << "  <- color casi plano"
                           " (correcto si es mar, revisar si es tierra)";
                }
                out << "\n";
            }
        }
        out << "\n";

        // --- Refinamiento por demanda --------------------------------------
        // Si el respaldo disponible era demasiado ampliado, el servicio pide
        // por su cuenta peldanos intermedios. Es asincrono, asi que hay que
        // dejarle un momento antes de volver a medir.
        {
            QEventLoop espera;
            QTimer::singleShot(1500, &espera, &QEventLoop::quit);
            espera.exec();

            const auto refinado = svc.planFor(nw, se, z);
            if (refinado.exactCount != plan.exactCount
                || refinado.fallbackCount != plan.fallbackCount
                || refinado.emptyCount != plan.emptyCount) {
                out << " TRAS EL REFINAMIENTO POR DEMANDA\n";
                out << "   exactas " << refinado.exactCount
                    << ", por respaldo " << refinado.fallbackCount
                    << ", huecos " << refinado.emptyCount
                    << " -> " << QString::number(refinado.coverage() * 100.0, 'f', 1)
                    << " %\n";

                QMap<int, int> prof;
                for (const TileDrawItem &i : refinado.items)
                    if (!i.isExact())
                        ++prof[i.ancestorDepth];
                for (auto it = prof.constBegin(); it != prof.constEnd(); ++it)
                    out << "     " << it.value() << " huecos a " << it.key()
                        << " niveles (ampliado " << (1 << it.key()) << "x)\n";
                out << "\n";
            }
        }

        // --- Segunda vuelta, con la cache caliente -------------------------
        timer.restart();
        const quint64 again = svc.requestViewport(nw, se, z);
        if (again == 0) {
            out << " SEGUNDA CARGA: 0 ms, no se consulto la base de datos\n";
            out << "   (todo resuelto en memoria: " << svc.cache().count()
                << " teselas y " << svc.cache().missingCount()
                << " ausencias anotadas)\n\n";
        } else {
            waitForZoom(svc, z, 15000);
            out << " SEGUNDA CARGA (cache caliente): " << timer.elapsed()
                << " ms\n\n";
        }

        // --- Arrastre ------------------------------------------------------
        int served = 0;
        QMetaObject::Connection c =
            QObject::connect(&svc, &TileService::tilesReady, &svc,
                             [&](quint64, int, int) { ++served; });

        timer.restart();
        for (int i = 0; i < pans; ++i) {
            const double dx = spanLon * 0.15 * i;
            svc.requestViewport(QGeoCoordinate(nw.latitude(), nw.longitude() + dx),
                                QGeoCoordinate(se.latitude(), se.longitude() + dx),
                                z);
            QCoreApplication::processEvents();
        }
        // Se deja que se asiente.
        QEventLoop settle;
        QTimer::singleShot(2000, &settle, &QEventLoop::quit);
        settle.exec();
        QObject::disconnect(c);

        out << " ARRASTRE SIMULADO\n";
        out << "   peticiones lanzadas : " << pans << "\n";
        out << "   lecturas servidas   : " << served << "\n";
        out << "   descartadas por obsoletas: " << (pans - served) << "\n";
        out << "   tiempo total        : " << timer.elapsed() << " ms\n\n";

        const auto st = svc.cache().stats();
        out << " CACHE\n";
        out << "   teselas insertadas: " << st.inserts
            << " (decodificadas una sola vez)\n";
        out << "   ocupacion actual  : " << svc.cache().count() << " teselas, "
            << QString::number(static_cast<double>(svc.cache().usedBytes()) / 1048576.0, 'f', 1)
            << " de " << (svc.cache().maxBytes() / 1048576) << " MiB\n";
        if (st.decodeFailures > 0)
            out << "   fallos de decodificacion: " << st.decodeFailures << "\n";
        out << "\n";
    }

    SqliteConnectionPool::closeAllForCurrentThread();
    return 0;
}
