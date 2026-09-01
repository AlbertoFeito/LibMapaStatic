/*!
 * render_map - dibuja el mapa a un PNG, sin abrir ninguna ventana.
 *
 * Sirve para comprobar que la libreria pinta de verdad, en un servidor sin
 * pantalla o dentro de un script de verificacion.
 *
 * Uso:
 *   render_map --datasets datasets.json --out mapa.png
 *              [--layer satelital] [--center 23.1136,-82.3666] [--zoom 12]
 *              [--size 1280x800] [--wait 5000] [--grid]
 */

#include "libmapa/MapWidget.h"
#include "widget/MapView.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

using namespace libmapa;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser p;
    p.setApplicationDescription(
        QStringLiteral("Dibuja el mapa a un PNG, sin ventana"));
    p.addHelpOption();

    QCommandLineOption oDatasets(QStringLiteral("datasets"),
        QStringLiteral("Ruta a datasets.json"), QStringLiteral("ruta"),
        QStringLiteral("datasets.json"));
    QCommandLineOption oOut(QStringLiteral("out"),
        QStringLiteral("PNG de salida"), QStringLiteral("ruta"),
        QStringLiteral("mapa.png"));
    QCommandLineOption oLayer(QStringLiteral("layer"),
        QStringLiteral("Capa base ('osm', 'satelital')"), QStringLiteral("id"));
    QCommandLineOption oCenter(QStringLiteral("center"),
        QStringLiteral("lat,lon"), QStringLiteral("coord"),
        QStringLiteral("23.1136,-82.3666"));
    QCommandLineOption oZoom(QStringLiteral("zoom"),
        QStringLiteral("Nivel de zoom"), QStringLiteral("z"),
        QStringLiteral("12"));
    QCommandLineOption oSize(QStringLiteral("size"),
        QStringLiteral("Tamano en pixeles, AxB"), QStringLiteral("wxh"),
        QStringLiteral("1280x800"));
    QCommandLineOption oWait(QStringLiteral("wait"),
        QStringLiteral("Milisegundos de espera a las teselas"),
        QStringLiteral("ms"), QStringLiteral("5000"));
    QCommandLineOption oGrid(QStringLiteral("grid"),
        QStringLiteral("Dibujar la rejilla de teselas con z/x/y"));

    p.addOption(oDatasets); p.addOption(oOut); p.addOption(oLayer);
    p.addOption(oCenter); p.addOption(oZoom); p.addOption(oSize);
    p.addOption(oWait); p.addOption(oGrid);
    p.process(app);

    const QStringList c = p.value(oCenter).split(QLatin1Char(','));
    if (c.size() != 2) {
        err << "--center mal formado. Formato: lat,lon\n";
        return 2;
    }
    const QStringList s = p.value(oSize).split(QLatin1Char('x'));
    if (s.size() != 2) {
        err << "--size mal formado. Formato: anchoxalto\n";
        return 2;
    }

    MapConfig cfg;
    cfg.datasetsFile = p.value(oDatasets);
    cfg.initialCenter = QGeoCoordinate(c[0].toDouble(), c[1].toDouble());
    cfg.initialZoom = p.value(oZoom).toInt();
    cfg.debounceMs = 0;
    cfg.cacheMiB = 256;
    if (p.isSet(oLayer))
        cfg.initialLayerId = p.value(oLayer);

    MapWidget mapa(cfg);
    if (!mapa.isReady()) {
        err << "No se pudo iniciar el mapa: " << mapa.lastError() << "\n";
        return 1;
    }

    mapa.resize(s[0].toInt(), s[1].toInt());
    if (p.isSet(oGrid))
        mapa.setDebugGridVisible(true);

    // Se deja llegar a las teselas: la carga es asincrona, en otro hilo.
    QElapsedTimer t;
    t.start();
    QEventLoop bucle;
    QTimer::singleShot(p.value(oWait).toInt(), &bucle, &QEventLoop::quit);
    bucle.exec();

    auto *plot = qobject_cast<QCustomPlot *>(mapa.customPlot());
    if (!plot) {
        err << "No hay vista que dibujar\n";
        return 1;
    }

    const QString destino = p.value(oOut);
    if (!plot->savePng(destino, s[0].toInt(), s[1].toInt())) {
        err << "No se pudo escribir " << destino << "\n";
        return 1;
    }

    out << "Escrito " << destino << "\n";
    out << "  capa            : " << mapa.baseLayerId() << "\n";
    out << "  zoom            : " << mapa.zoom()
        << " (maximo recomendado " << mapa.maxZoom() << ")\n";
    out << "  centro          : "
        << QString::number(mapa.center().latitude(), 'f', 5) << ", "
        << QString::number(mapa.center().longitude(), 'f', 5) << "\n";
    out << "  teselas propias : "
        << QString::number(mapa.exactCoverage() * 100.0, 'f', 1) << " %\n";
    out << "  tiempo de espera: " << t.elapsed() << " ms\n";

    // Diagnostico del dibujo, para poder comprobar sin mirar la imagen.
    if (auto *vista = qobject_cast<MapView *>(plot)) {
        const auto &plan = vista->tileLayer()->plan();
        out << "  --- plan de dibujo ---\n";
        out << "  celdas          : " << plan.slotCount()
            << "  (exactas " << plan.exactCount
            << ", respaldo " << plan.fallbackCount
            << ", vacias " << plan.emptyCount << ")\n";
        out << "  items dibujados : " << vista->tileLayer()->lastDrawnCount() << "\n";
        out << "  viewport        : " << vista->viewport().width() << "x"
            << vista->viewport().height() << "\n";
        out << "  rango x (lon)   : " << vista->xAxis->range().lower << " .. "
            << vista->xAxis->range().upper << "\n";
        out << "  rango y (lat)   : " << vista->yAxis->range().lower << " .. "
            << vista->yAxis->range().upper << "\n";
    }
    return 0;
}
