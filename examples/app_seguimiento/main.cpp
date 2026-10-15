// app_seguimiento - ejemplo de SEGUIMIENTO de objetivos con libmapa.
//
// Hermano de app_minima (que es la plantilla de producto mas pequena): aqui se
// ve, en un solo fichero, como usar lo que la libreria ofrece para seguimiento.
//   - Alimentar objetivos (aqui, tres buques simulados; en un producto real, lo
//     que decodifique la app de AIS, radar, ADS-B...) con su hora y su velocidad.
//   - Alerta de zona (un poligono vigilado) y de proximidad (CPA/TCPA).
//   - Anillos de distancia alrededor del buque propio y una marcacion a otro.
//   - Agrupar objetivos al alejar y filtrarlos por tipo.
//   - Medir superficie, exportar la vista a PDF y el historial a GPX, y
//     reproducir ese historial.
//
//   app_seguimiento [carpeta del paquete de datos]   (por defecto <exe>/datos)

#include <libmapa/MapWidget.h>
#include <libmapa/TrackPlayer.h>

#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>

using namespace libmapa;

namespace {

// Un buque simulado: su id en el mapa, su rumbo y su velocidad en nudos.
struct Buque {
    qint64 id = -1;
    double rumbo = 0.0;
    double nudos = 0.0;
};

// Primera linea de la etiqueta de un objetivo, para los avisos.
QString nombre(MapWidget *mapa, qint64 id)
{
    const auto t = mapa->target(id);
    return t ? t->label.section(QLatin1Char('\n'), 0, 0) : QString::number(id);
}

} // namespace

// Ventana con el mapa, tres buques que se mueven solos y un menu con las
// herramientas. Todo esta en el constructor para leerlo de arriba abajo.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos y todo lo demas sobre el mapa.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
            return;
        }
        const QGeoCoordinate centro(23.20, -82.40);
        m_mapa->setCenter(centro);
        m_mapa->setZoom(11);
        m_mapa->setScaleBarVisible(true);
        m_mapa->setScaleBarUnits(ScaleUnits::Nautical);

        // --- 1. Objetivos: el propio y dos mas, en rumbo de cruce ------------
        const auto alta = [this](const QString &etiqueta, const QGeoCoordinate &pos,
                                 double rumbo, double nudos) {
            MapTarget t;
            t.label = etiqueta;
            t.kind = QStringLiteral("buque");
            t.position = pos;
            t.headingDeg = rumbo;
            Buque b;
            b.id = m_mapa->addTarget(t, QDateTime::currentDateTimeUtc());
            b.rumbo = rumbo;
            b.nudos = nudos;
            // La velocidad real (p. ej. SOG/COG del AIS) manda sobre la estimada.
            m_mapa->setTargetVelocity(b.id, knotsToMetersPerSecond(nudos), rumbo);
            m_buques.append(b);
            return b.id;
        };
        const qint64 propio = alta(tr("Propio"), centro, 45.0, 12.0);
        const qint64 otro = alta(tr("Buque B"), centro.atDistanceAndAzimuth(6000.0, 80.0), 300.0, 10.0);
        alta(tr("Buque C"), centro.atDistanceAndAzimuth(9000.0, 200.0), 20.0, 8.0);

        // --- 2. Zona vigilada: un poligono; avisa al entrar y salir ----------
        MapFeature zona;
        zona.kind = GeometryKind::Polygon;
        zona.name = tr("Zona restringida");
        for (double az : { 0.0, 90.0, 180.0, 270.0 })
            zona.geometry.append(centro.atDistanceAndAzimuth(3000.0, az + 45.0)
                                     .atDistanceAndAzimuth(4000.0, 45.0));
        m_mapa->watchZone(m_mapa->addFeature(zona));
        connect(m_mapa, &MapWidget::targetEnteredZone, this, [this](qint64 t, qint64) {
            statusBar()->showMessage(tr("⚠ %1 entra en la zona restringida").arg(nombre(m_mapa, t)), 8000);
        });

        // --- 3. Proximidad: aviso si dos pasaran a menos de 0.5 NM en 15 min -
        m_mapa->setProximityAlert(0.5 * 1852.0, 15 * 60.0);
        connect(m_mapa, &MapWidget::proximityAlertRaised, this,
                [this](qint64 a, qint64 b, double cpa, double tcpa) {
            statusBar()->showMessage(tr("⚠ CPA: %1 y %2 a %3 NM en %4 min")
                .arg(nombre(m_mapa, a), nombre(m_mapa, b)).arg(cpa / 1852.0, 0, 'f', 2)
                .arg(qRound(tcpa / 60.0)), 8000);
        });

        // --- 4. Anillos alrededor del propio y marcacion hasta B -------------
        RangeRings anillos;
        anillos.targetId = propio;
        anillos.spacingMeters = 1852.0;
        anillos.count = 4;
        anillos.spokeEveryDeg = 30;
        m_mapa->addRangeRings(anillos);
        BearingLine ebl;
        ebl.fromTarget = propio;
        ebl.toTarget = otro;
        m_mapa->addBearingLine(ebl);

        // --- 5. Agrupar al alejar (hasta zoom 9) -----------------------------
        TargetClustering grupos;
        grupos.enabled = true;
        grupos.maxZoom = 9;
        m_mapa->setTargetClustering(grupos);

        // --- 6. Menu -----------------------------------------------------------
        QMenu *herr = menuBar()->addMenu(tr("&Herramientas"));
        connect(herr->addAction(tr("Navegar")), &QAction::triggered, this,
                [this] { m_mapa->setActiveTool(MapTool::None); });
        connect(herr->addAction(tr("Medir superficie")), &QAction::triggered, this,
                [this] { m_mapa->setActiveTool(MapTool::MeasureArea); });
        connect(m_mapa, &MapWidget::areaMeasured, this, [this](const AreaMeasurement &m) {
            statusBar()->showMessage(tr("Superficie %1 km², perímetro %2 km")
                .arg(m.areaSquareMeters / 1e6, 0, 'f', 3).arg(m.perimeterMeters / 1000.0, 0, 'f', 2));
        });
        auto *soloBuques = herr->addAction(tr("Solo buques a más de 9 kn"));
        soloBuques->setCheckable(true);
        connect(soloBuques, &QAction::toggled, this, [this](bool on) {
            if (!on) {
                m_mapa->clearTargetFilter();
                return;
            }
            TargetFilter f;
            f.kinds << QStringLiteral("buque");
            f.minSpeed = knotsToMetersPerSecond(9.0);
            f.dim = true;                       // los demas, tenues
            m_mapa->setTargetFilter(f);
        });

        QMenu *datosMenu = menuBar()->addMenu(tr("&Datos"));
        connect(datosMenu->addAction(tr("Exportar vista a PDF...")), &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar vista"),
                                                              QStringLiteral("vista.pdf"), tr("PDF (*.pdf)"));
            MapExportOptions o;
            o.title = tr("Situación");
            if (!ruta.isEmpty() && !m_mapa->exportView(ruta, o))
                statusBar()->showMessage(tr("No se pudo exportar"), 5000);
        });
        connect(datosMenu->addAction(tr("Exportar historial (GPX)...")), &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar historial"),
                                                              QStringLiteral("historial.gpx"), tr("GPX (*.gpx)"));
            if (!ruta.isEmpty())
                m_mapa->exportTargetHistory(ruta);
        });
        // Reproducir un historial: los objetivos grabados se mueven en el
        // tiempo como si fueran en vivo (aqui se para la simulacion mientras).
        m_player = new TrackPlayer(m_mapa, this);
        connect(datosMenu->addAction(tr("Reproducir historial (GPX)...")), &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getOpenFileName(this, tr("Reproducir"), QString(), tr("GPX (*.gpx)"));
            if (ruta.isEmpty())
                return;
            m_reloj.stop();
            if (m_player->loadFile(ruta)) {
                m_player->setRoutesVisible(true);
                m_player->play();
            }
        });
        connect(datosMenu->addAction(tr("Salir de la reproducción")), &QAction::triggered, this, [this] {
            m_player->clear();                  // quita sus objetivos y rutas
            m_reloj.start();
        });

        // --- 7. "Feed": cada segundo, cada buque avanza lo que le toca --------
        connect(&m_reloj, &QTimer::timeout, this, &Ventana::paso);
        m_reloj.start(1000);
        statusBar()->showMessage(tr("Seguimiento en marcha"));
    }

private:
    // Un paso del "feed": nueva posicion por estima (rumbo y velocidad durante
    // un segundo), con su hora. Es la llamada que haria un decodificador real.
    void paso()
    {
        const QDateTime ahora = QDateTime::currentDateTimeUtc();
        for (const Buque &b : m_buques) {
            const auto t = m_mapa->target(b.id);
            if (!t)
                continue;
            const QGeoCoordinate nueva =
                t->position.atDistanceAndAzimuth(knotsToMetersPerSecond(b.nudos), b.rumbo);
            m_mapa->updateTarget(b.id, nueva, b.rumbo, ahora);
        }
    }

    MapWidget *m_mapa = nullptr;
    TrackPlayer *m_player = nullptr;
    QVector<Buque> m_buques;
    QTimer m_reloj;
};

// Arranca la ventana sobre el paquete de datos (argumento o <exe>/datos).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    const QString datos = args.size() > 1 ? args.at(1)
                                          : QCoreApplication::applicationDirPath() + QStringLiteral("/datos");
    Ventana v(datos);
    v.resize(1100, 750);
    v.show();
    return app.exec();
}
