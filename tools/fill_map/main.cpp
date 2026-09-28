/*!
 * fill_map - ventana con MAPA para rellenar huecos de teselas.
 *
 * Muestra el mapa (MapWidget) y deja marcar un rectangulo encima con la
 * herramienta SelectArea; se eligen las capas y el rango de zoom, se estima
 * cuantas teselas faltan, se pide confirmacion y se descargan (de Esri satelite,
 * sin API key) con barra de progreso y boton de cancelar. Al terminar cada
 * nivel, el mapa se refresca para ver el relleno.
 *
 * El motor de descarga es el MISMO que la herramienta de consola fill_tiles
 * (libmapa::TileFiller): misma codificacion probada, solo cambia la carcasa.
 *
 * Uso:  fill_map [ruta/a/datasets.json]      (por defecto ./datasets.json)
 */

#include "TileFiller.h"

#include "libmapa/MapWidget.h"
#include "libmapa/MapTypes.h"
#include "tiles/TileService.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGeoCoordinate>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>

using namespace libmapa;

namespace {
// Esri "Clarity": misma imagen satelital sin clave, mas clara y viva que la
// "World_Imagery" normal -casa mejor con las bases de Google-. Editable en la
// casilla "Fuente (URL)".
const char *kFuenteDefecto =
    "https://clarity.maptiles.arcgis.com/arcgis/rest/services/"
    "World_Imagery/MapServer/tile/{z}/{y}/{x}";
}

class Ventana : public QMainWindow
{
    Q_OBJECT
public:
    Ventana(const QString &datasetsFile)
        : m_datasetsFile(datasetsFile)
    {
        setWindowTitle(tr("Rellenar teselas - libmapa"));
        resize(1200, 800);

        // Codificacion de cada capa (para pasarsela al motor de descarga).
        for (const TileDataset &d : TileService::loadDatasets(datasetsFile))
            m_datasets.insert(d.id, d);

        // --- Mapa -----------------------------------------------------------
        MapConfig cfg;
        cfg.datasetsFile = datasetsFile;
        cfg.initialCenter = QGeoCoordinate(21.5, -79.5);   // Cuba entera
        cfg.initialZoom = 6;
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);

        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo abrir el mapa:\n%1").arg(m_mapa->lastError()));
        }

        construirBarra();
        construirEstado();

        connect(m_mapa, &MapWidget::areaSelected,
                this, &Ventana::alSeleccionarArea);
        connect(m_mapa, &MapWidget::zoomChanged, this, [this](int) {
            if (!m_running) sincronizarZoomDesde();
        });
    }

private:
    void construirBarra()
    {
        QToolBar *tb = addToolBar(tr("Relleno"));
        tb->setMovable(false);

        tb->addWidget(new QLabel(tr("  Capa: ")));
        m_capa = new QComboBox(this);
        for (const BaseLayerInfo &l : m_mapa->availableBaseLayers())
            m_capa->addItem(l.displayName, l.id);
        // Selecciona la capa activa del mapa.
        const int idx = m_capa->findData(m_mapa->baseLayerId());
        if (idx >= 0) m_capa->setCurrentIndex(idx);
        connect(m_capa, &QComboBox::currentTextChanged, this, [this] {
            const QString id = m_capa->currentData().toString();
            m_mapa->setBaseLayerId(id);
            sincronizarZoomDesde();
        });
        tb->addWidget(m_capa);

        m_btnArea = new QPushButton(tr("Seleccionar area"), this);
        m_btnArea->setCheckable(true);
        connect(m_btnArea, &QPushButton::toggled, this, [this](bool on) {
            m_mapa->setActiveTool(on ? MapTool::SelectArea : MapTool::None);
            statusBar()->showMessage(on
                ? tr("Arrastra sobre el mapa para marcar la zona.")
                : QString());
        });
        tb->addWidget(m_btnArea);

        tb->addWidget(new QLabel(tr("  Zoom: ")));
        m_zDesde = new QSpinBox(this); m_zDesde->setRange(0, 22);
        m_zHasta = new QSpinBox(this); m_zHasta->setRange(0, 22);
        tb->addWidget(m_zDesde);
        tb->addWidget(new QLabel(tr(" a ")));
        tb->addWidget(m_zHasta);

        tb->addWidget(new QLabel(tr("  Vel(t/s): ")));
        m_rate = new QDoubleSpinBox(this);
        m_rate->setRange(0.5, 50.0); m_rate->setValue(2.0); m_rate->setDecimals(1);
        tb->addWidget(m_rate);

        m_btnRellenar = new QPushButton(tr("Rellenar"), this);
        connect(m_btnRellenar, &QPushButton::clicked, this, &Ventana::alRellenar);
        tb->addWidget(m_btnRellenar);

        // Segunda fila: la fuente (URL), por si se quiere cambiar.
        QToolBar *tb2 = new QToolBar(tr("Fuente"), this);
        tb2->setMovable(false);
        addToolBar(Qt::TopToolBarArea, tb2);
        insertToolBarBreak(tb2);
        tb2->addWidget(new QLabel(tr("  Fuente (URL {z}/{x}/{y}): ")));
        m_url = new QLineEdit(QString::fromLatin1(kFuenteDefecto), this);
        m_url->setMinimumWidth(600);
        tb2->addWidget(m_url);

        sincronizarZoomDesde();
    }

    void construirEstado()
    {
        m_barra = new QProgressBar(this);
        m_barra->setVisible(false);
        m_barra->setMinimumWidth(300);
        m_btnCancelar = new QPushButton(tr("Cancelar"), this);
        m_btnCancelar->setVisible(false);
        connect(m_btnCancelar, &QPushButton::clicked, this, [this] {
            if (m_filler) m_filler->cancel();
        });
        statusBar()->addPermanentWidget(m_barra);
        statusBar()->addPermanentWidget(m_btnCancelar);
    }

    //! Ajusta "zoom desde" al zoom actual del mapa y "hasta" al recomendado.
    void sincronizarZoomDesde()
    {
        const QString id = m_capa->currentData().toString();
        m_zDesde->setValue(m_mapa->zoom());
        int hasta = m_mapa->zoom() + 2;
        if (m_datasets.contains(id))
            hasta = m_datasets.value(id).recommendedMaxZoom;
        m_zHasta->setValue(qMax(hasta, m_zDesde->value()));
    }

    void alSeleccionarArea(const QGeoCoordinate &no, const QGeoCoordinate &se)
    {
        m_no = no; m_se = se; m_hayArea = true;
        statusBar()->showMessage(
            tr("Zona: N %1  O %2  ->  S %3  E %4")
                .arg(no.latitude(), 0, 'f', 3).arg(no.longitude(), 0, 'f', 3)
                .arg(se.latitude(), 0, 'f', 3).arg(se.longitude(), 0, 'f', 3),
            8000);
    }

    void alRellenar()
    {
        if (m_running)
            return;
        if (!m_hayArea) {
            QMessageBox::information(this, tr("Falta la zona"),
                tr("Pulsa \"Seleccionar area\" y arrastra un rectangulo sobre "
                   "el mapa primero."));
            return;
        }
        const QString id = m_capa->currentData().toString();
        if (!m_datasets.contains(id)) {
            QMessageBox::warning(this, tr("Sin datos"),
                tr("No tengo la configuracion del dataset '%1'.").arg(id));
            return;
        }

        TileFiller::Params p;
        p.ds = m_datasets.value(id);
        p.latN = m_no.latitude();  p.lonW = m_no.longitude();
        p.latS = m_se.latitude();  p.lonE = m_se.longitude();
        p.minZoom = qMin(m_zDesde->value(), m_zHasta->value());
        p.maxZoom = qMax(m_zDesde->value(), m_zHasta->value());
        p.url = m_url->text().trimmed();
        p.rate = m_rate->value();

        auto *filler = new TileFiller(this);
        QString err;
        if (!filler->prepare(p, &err)) {
            QMessageBox::warning(this, tr("Error"), err);
            filler->deleteLater();
            return;
        }

        const qint64 total = filler->totalToDownload();
        if (total == 0) {
            QMessageBox::information(this, tr("Nada que hacer"),
                tr("No falta ninguna tesela en esa zona y ese rango de zoom."));
            filler->deleteLater();
            return;
        }

        // Desglose + aviso si es una descarga grande.
        QString detalle;
        for (const auto &pz : filler->perZoomMissing())
            if (pz.second > 0)
                detalle += tr("  z%1: %2\n").arg(pz.first).arg(pz.second);
        QString aviso = tr("Se descargaran %1 teselas.\n\n%2").arg(total).arg(detalle);
        if (total > 50000)
            aviso += tr("\nATENCION: son muchas; puede tardar bastante y ocupar "
                        "varios cientos de MB.");
        aviso += tr("\nA %1 t/s son ~%2 minutos.\n\n¿Continuar?")
                     .arg(p.rate, 0, 'f', 1)
                     .arg(double(total) / p.rate / 60.0, 0, 'f', 1);

        if (QMessageBox::question(this, tr("Confirmar descarga"), aviso)
            != QMessageBox::Yes) {
            filler->deleteLater();
            return;
        }

        // --- Lanzar ---------------------------------------------------------
        m_filler = filler;
        m_running = true;
        ponerControles(false);
        m_barra->setRange(0, int(qMin<qint64>(total, 1000000)));
        m_barra->setValue(0);
        m_barra->setVisible(true);
        m_btnCancelar->setVisible(true);

        connect(filler, &TileFiller::progress, this,
                [this, total](qint64 done, qint64 tot, double tps) {
            m_barra->setValue(int(qMin<qint64>(done, 1000000)));
            const double restan = tps > 0 ? double(tot - done) / tps / 60.0 : 0.0;
            statusBar()->showMessage(
                tr("%1/%2  %3 t/s  ~%4 min restantes")
                    .arg(done).arg(tot).arg(tps, 0, 'f', 1).arg(restan, 0, 'f', 1));
        });
        connect(filler, &TileFiller::zoomFinished, this, [this](int, qint64 added) {
            if (added > 0) m_mapa->reloadBaseLayer();   // ver el relleno en vivo
        });
        connect(filler, &TileFiller::finished, this,
                [this, filler](const TileFiller::Stats &s, bool cancelled) {
            m_mapa->reloadBaseLayer();
            m_barra->setVisible(false);
            m_btnCancelar->setVisible(false);
            ponerControles(true);
            m_running = false;
            m_filler = nullptr;
            QMessageBox::information(this,
                cancelled ? tr("Cancelado") : tr("Terminado"),
                tr("Descargadas: %1\nSin origen (404): %2\nFallidas: %3")
                    .arg(s.downloaded).arg(s.notFound).arg(s.failed));
            filler->deleteLater();
        });

        filler->start();
    }

    void ponerControles(bool on)
    {
        m_btnRellenar->setEnabled(on);
        m_capa->setEnabled(on);
        m_btnArea->setEnabled(on);
        m_zDesde->setEnabled(on);
        m_zHasta->setEnabled(on);
        m_rate->setEnabled(on);
        m_url->setEnabled(on);
    }

    QString m_datasetsFile;
    QHash<QString, TileDataset> m_datasets;
    MapWidget *m_mapa = nullptr;

    QComboBox *m_capa = nullptr;
    QPushButton *m_btnArea = nullptr;
    QSpinBox *m_zDesde = nullptr;
    QSpinBox *m_zHasta = nullptr;
    QDoubleSpinBox *m_rate = nullptr;
    QLineEdit *m_url = nullptr;
    QPushButton *m_btnRellenar = nullptr;
    QProgressBar *m_barra = nullptr;
    QPushButton *m_btnCancelar = nullptr;

    QGeoCoordinate m_no, m_se;
    bool m_hayArea = false;
    bool m_running = false;
    TileFiller *m_filler = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    const QString datasets = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                      : QStringLiteral("datasets.json");
    Ventana v(datasets);
    v.show();
    return app.exec();
}

#include "main.moc"
