/*!
 * demo - ejemplo minimo de integracion de libmapa::MapWidget.
 *
 * Uso:
 *     demo [ruta/a/datasets.json]
 *
 * Muestra todo lo que hace falta para embeber el mapa en un proyecto: crear
 * el widget, meterlo en un layout y conectar unas cuantas senales.
 */

#include "libmapa/MapWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

using namespace libmapa;

class Ventana : public QMainWindow
{
    Q_OBJECT

public:
    explicit Ventana(const QString &datasetsFile)
    {
        setWindowTitle(tr("libmapa - demostracion"));
        resize(1100, 750);

        // --- Asi se crea el mapa. Esto es todo. -------------------------
        MapConfig cfg;
        cfg.datasetsFile = datasetsFile;
        cfg.initialCenter = QGeoCoordinate(23.1136, -82.3666);   // La Habana
        cfg.initialZoom = 11;
        cfg.cacheMiB = 192;

        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);

        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo iniciar el mapa:\n%1\n\n"
                   "Genera datasets.json con probe_db y pasa su ruta como "
                   "argumento.").arg(m_mapa->lastError()));
            return;
        }

        construirBarra();
        conectarSenales();
        actualizarEstado();
    }

private:
    void construirBarra()
    {
        auto *barra = addToolBar(tr("Mapa"));
        barra->setMovable(false);

        // Selector de capa base: el requisito central de la libreria.
        barra->addWidget(new QLabel(tr("  Capa: ")));
        m_capas = new QComboBox(this);
        for (const BaseLayerInfo &c : m_mapa->availableBaseLayers())
            m_capas->addItem(c.displayName, c.id);
        m_capas->setCurrentIndex(
            m_capas->findData(m_mapa->baseLayerId()));
        barra->addWidget(m_capas);

        barra->addSeparator();

        auto *masZoom = barra->addAction(tr("Ampliar"));
        auto *menosZoom = barra->addAction(tr("Reducir"));
        connect(masZoom, &QAction::triggered, m_mapa, &MapWidget::zoomIn);
        connect(menosZoom, &QAction::triggered, m_mapa, &MapWidget::zoomOut);

        barra->addSeparator();

        m_medir = barra->addAction(tr("Medir"));
        m_medir->setCheckable(true);
        m_zoomArea = barra->addAction(tr("Zoom a area"));
        m_zoomArea->setCheckable(true);

        barra->addSeparator();
        auto *rejilla = barra->addAction(tr("Rejilla de teselas"));
        rejilla->setCheckable(true);
        connect(rejilla, &QAction::toggled,
                m_mapa, &MapWidget::setDebugGridVisible);

        auto *cuba = barra->addAction(tr("Toda Cuba"));
        connect(cuba, &QAction::triggered, this, [this] {
            m_mapa->fitBounds(QGeoCoordinate(23.3, -85.0),
                              QGeoCoordinate(19.7, -74.0));
        });
    }

    void conectarSenales()
    {
        connect(m_capas, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int i) {
                    m_mapa->setBaseLayerId(m_capas->itemData(i).toString());
                });

        connect(m_mapa, &MapWidget::mouseMoved, this,
                [this](const QGeoCoordinate &p) {
                    m_coords->setText(QStringLiteral("  %1, %2  ")
                        .arg(p.latitude(), 0, 'f', 5)
                        .arg(p.longitude(), 0, 'f', 5));
                });

        connect(m_mapa, &MapWidget::zoomChanged, this, &Ventana::actualizarEstado);
        connect(m_mapa, &MapWidget::baseLayerChanged, this, &Ventana::actualizarEstado);
        connect(m_mapa, &MapWidget::centerChanged, this, &Ventana::actualizarEstado);

        // Diagnostico: al pulsar se anota donde cayo el punto, para poder
        // comparar con donde estaba el cursor.
        connect(m_mapa, &MapWidget::clicked, this,
                [this](const QGeoCoordinate &p, Qt::MouseButton) {
                    statusBar()->showMessage(
                        tr("Clic en %1, %2   (escalado de pantalla: %3)")
                            .arg(p.latitude(), 0, 'f', 6)
                            .arg(p.longitude(), 0, 'f', 6)
                            .arg(devicePixelRatioF()), 5000);
                });

        connect(m_mapa, &MapWidget::measurementFinished, this,
                [this](const Measurement &m) {
                    statusBar()->showMessage(
                        tr("Distancia: %1 km   Marcacion: %2 grados")
                            .arg(m.distanceMeters / 1000.0, 0, 'f', 2)
                            .arg(m.azimuthDegrees, 0, 'f', 1), 8000);
                    m_medir->setChecked(false);
                    m_mapa->setActiveTool(MapTool::None);
                });

        connect(m_mapa, &MapWidget::errorOccurred, this,
                [this](const QString &e) {
                    statusBar()->showMessage(tr("Error: %1").arg(e), 8000);
                });

        connect(m_medir, &QAction::toggled, this, [this](bool on) {
            if (on) m_zoomArea->setChecked(false);
            m_mapa->setActiveTool(on ? MapTool::Measure : MapTool::None);
        });
        connect(m_zoomArea, &QAction::toggled, this, [this](bool on) {
            if (on) m_medir->setChecked(false);
            m_mapa->setActiveTool(on ? MapTool::AreaZoom : MapTool::None);
        });

        m_coords = new QLabel(this);
        m_info = new QLabel(this);
        statusBar()->addPermanentWidget(m_coords);
        statusBar()->addPermanentWidget(m_info);
    }

    void actualizarEstado()
    {
        if (!m_info)
            return;
        m_info->setText(QStringLiteral("  %1  |  zoom %2 de %3  |  %4 % propias  ")
            .arg(m_mapa->baseLayerId())
            .arg(m_mapa->zoom())
            .arg(m_mapa->maxZoom())
            .arg(m_mapa->exactCoverage() * 100.0, 0, 'f', 0));
    }

    MapWidget *m_mapa = nullptr;
    QComboBox *m_capas = nullptr;
    QAction *m_medir = nullptr;
    QAction *m_zoomArea = nullptr;
    QLabel *m_coords = nullptr;
    QLabel *m_info = nullptr;
};

int main(int argc, char *argv[])
{
    // Escalado de pantalla. En Windows con el zoom de pantalla al 125 % o
    // 150 %, sin esto Qt 5 entrega las coordenadas del raton en pixeles
    // fisicos mientras dibuja en pixeles logicos (o al reves, segun la
    // version), y todo lo que se coloque con el raton queda desplazado
    // respecto al cursor. En Qt 6 ya es el comportamiento por defecto.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);

    const QString datasets = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir::currentPath() + QStringLiteral("/datasets.json");

    Ventana v(datasets);
    v.show();
    return app.exec();
}

#include "main.moc"
