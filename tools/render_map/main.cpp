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
#include "libmapa/MapFeature.h"

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
    QCommandLineOption oFeatures(QStringLiteral("features"),
        QStringLiteral("Anadir entidades de ejemplo: puntos, area y trazado"));

    p.addOption(oDatasets); p.addOption(oOut); p.addOption(oLayer);
    p.addOption(oCenter); p.addOption(oZoom); p.addOption(oSize);
    p.addOption(oWait); p.addOption(oGrid); p.addOption(oFeatures);
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

    if (p.isSet(oFeatures)) {
        // Entidades de ejemplo alrededor del centro, para comprobar que se
        // dibujan donde deben y con el estilo pedido.
        const double lat = cfg.initialCenter.latitude();
        const double lon = cfg.initialCenter.longitude();
        const double d = 0.02;

        mapa.addFeatureLayer(QStringLiteral("zonas"),
                             QStringLiteral("Zonas"), 0);
        mapa.addFeatureLayer(QStringLiteral("puntos"),
                             QStringLiteral("Puntos"), 10);

        MapFeature area;
        area.kind = GeometryKind::Polygon;
        area.layerId = QStringLiteral("zonas");
        area.type = QStringLiteral("area_interes");
        area.name = QStringLiteral("Area de interes");
        area.geometry = {QGeoCoordinate(lat + d, lon - d),
                         QGeoCoordinate(lat + d, lon + d),
                         QGeoCoordinate(lat - d * 0.3, lon + d * 1.4),
                         QGeoCoordinate(lat - d, lon)};
        area.style.lineColor = QColor(0x1b, 0x5e, 0x20);
        area.style.fillColor = QColor(0x4c, 0xaf, 0x50, 70);
        mapa.addFeature(area);

        MapFeature prohibida;
        prohibida.kind = GeometryKind::Polygon;
        prohibida.layerId = QStringLiteral("zonas");
        prohibida.type = QStringLiteral("zona_prohibida");
        prohibida.name = QStringLiteral("Zona prohibida");
        prohibida.geometry = {QGeoCoordinate(lat - d * 1.6, lon - d * 1.6),
                              QGeoCoordinate(lat - d * 0.6, lon - d * 1.6),
                              QGeoCoordinate(lat - d * 0.6, lon - d * 0.6),
                              QGeoCoordinate(lat - d * 1.6, lon - d * 0.6)};
        prohibida.style.lineColor = QColor(0xd3, 0x2f, 0x2f);
        prohibida.style.fillColor = QColor(0xd3, 0x2f, 0x2f, 70);
        prohibida.style.lineStyle = Qt::DashLine;
        mapa.addFeature(prohibida);

        MapFeature linea;
        linea.kind = GeometryKind::Polyline;
        linea.layerId = QStringLiteral("zonas");
        linea.name = QStringLiteral("Trazado");
        linea.geometry = {QGeoCoordinate(lat + d * 1.5, lon - d * 1.8),
                          QGeoCoordinate(lat + d * 0.5, lon - d * 0.9),
                          QGeoCoordinate(lat + d * 1.2, lon + d * 0.2),
                          QGeoCoordinate(lat + d * 0.2, lon + d * 1.5)};
        linea.style.lineColor = QColor(0x15, 0x65, 0xc0);
        linea.style.lineWidth = 3;
        mapa.addFeature(linea);

        const char *nombres[] = {"Faro", "Muelle", "Torre"};
        for (int i = 0; i < 3; ++i) {
            MapFeature pt;
            pt.kind = GeometryKind::Point;
            pt.layerId = QStringLiteral("puntos");
            pt.type = QStringLiteral("punto_interes");
            pt.name = QString::fromUtf8(nombres[i]);
            pt.geometry = {QGeoCoordinate(lat + (i - 1) * d * 0.8,
                                          lon + d * 0.9)};
            pt.style.lineColor = QColor(0xf5, 0x7c, 0x00);
            pt.style.fillColor = QColor(0xff, 0xb7, 0x4d);
            pt.style.pointRadiusPx = 7;
            mapa.addFeature(pt);
        }

        // Una seleccionada, para ver el resalte.
        if (!mapa.features().isEmpty())
            mapa.selectFeature(mapa.features().first().id);
    }
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
    if (mapa.featureCount() > 0) {
        out << "  entidades       : " << mapa.featureCount() << " en "
            << mapa.featureLayers().size() << " capas\n";
        if (auto *v = qobject_cast<MapView *>(mapa.customPlot()))
            out << "  dibujadas       : "
                << v->featureLayer()->lastDrawnCount() << "\n";
    }

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
