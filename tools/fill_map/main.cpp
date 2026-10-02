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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGeoCoordinate>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QProgressBar>
#include <QSslSocket>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVector>

#include <cmath>

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
    // Monta la ventana: carga la codificacion de cada capa, crea el MapWidget
    // centrado en Cuba, prepara el antirebote del modo "al navegar", construye la
    // barra de herramientas y la de estado, y conecta las senales del mapa
    // (seleccion de area, cambios de zoom/centro) a los slots correspondientes.
    Ventana(const QString &datasetsFile, const QString &demDir = QString())
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
        cfg.elevationDir = demDir;                         // vacio = sin elevacion
        cfg.initialCenter = QGeoCoordinate(21.5, -79.5);   // Cuba entera
        cfg.initialZoom = 6;
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);

        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo abrir el mapa:\n%1").arg(m_mapa->lastError()));
        }

        // Antirebote del modo "al navegar": espera a que el mapa se pare antes
        // de descargar, para no lanzar en cada pixel del arrastre.
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(500);
        connect(m_debounce, &QTimer::timeout, this, &Ventana::dispararAuto);

        construirBarra();
        construirEstado();

        connect(m_mapa, &MapWidget::areaSelected,
                this, &Ventana::alSeleccionarArea);
        connect(m_mapa, &MapWidget::polygonSelected,
                this, &Ventana::alSeleccionarPoligono);
        connect(m_mapa, &MapWidget::zoomChanged, this, [this](int) {
            if (!m_running) sincronizarZoomDesde();
            if (m_autoOn) m_debounce->start();
        });
        connect(m_mapa, &MapWidget::centerChanged, this,
                [this](const QGeoCoordinate &) {
            if (m_autoOn) m_debounce->start();
        });

        // Cota del terreno bajo el cursor: cada movimiento del raton consulta la
        // elevacion de esa coordenada en los `.hgt` (si hay carpeta configurada).
        connect(m_mapa, &MapWidget::mouseMoved, this,
                [this](const QGeoCoordinate &p) { mostrarCota(p); });
        if (!demDir.isEmpty())
            m_demActivo = true;
    }

private:
    // Construye las dos filas de la barra de herramientas: la primera con la capa,
    // el boton de seleccionar area, el rango de zoom, la velocidad y los botones
    // Rellenar / Nueva base / Descargar al navegar; la segunda con el campo de
    // bbox a mano y la URL de la fuente.
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
            // La cobertura es por capa: si esta visible, recalcular para la nueva.
            if (m_btnCobertura && m_btnCobertura->isChecked())
                m_mapa->refreshCoverage();
        });
        tb->addWidget(m_capa);

        m_btnArea = new QPushButton(tr("Seleccionar area"), this);
        m_btnArea->setCheckable(true);
        connect(m_btnArea, &QPushButton::toggled, this, [this](bool on) {
            if (on && m_btnPoly) m_btnPoly->setChecked(false);  // excluyentes
            m_mapa->setActiveTool(on ? MapTool::SelectArea : MapTool::None);
            statusBar()->showMessage(on
                ? tr("Arrastra sobre el mapa para marcar la zona.")
                : QString());
        });
        tb->addWidget(m_btnArea);

        // Seleccion por POLIGONO: clic a clic, doble clic (o Enter) lo cierra.
        // Solo se descargan las teselas dentro del poligono.
        m_btnPoly = new QPushButton(tr("Poligono"), this);
        m_btnPoly->setCheckable(true);
        m_btnPoly->setToolTip(tr("Marca un poligono clic a clic; doble clic o "
                                 "Enter lo cierra. Solo baja lo de dentro."));
        connect(m_btnPoly, &QPushButton::toggled, this, [this](bool on) {
            if (on && m_btnArea) m_btnArea->setChecked(false);  // excluyentes
            m_mapa->setActiveTool(on ? MapTool::SelectPolygon : MapTool::None);
            statusBar()->showMessage(on
                ? tr("Clic a clic marca el poligono; doble clic o Enter lo cierra.")
                : QString());
        });
        tb->addWidget(m_btnPoly);

        tb->addWidget(new QLabel(tr("  Zoom: ")));
        m_zDesde = new QSpinBox(this); m_zDesde->setRange(0, 22);
        m_zHasta = new QSpinBox(this); m_zHasta->setRange(0, 22);
        tb->addWidget(m_zDesde);
        tb->addWidget(new QLabel(tr(" a ")));
        tb->addWidget(m_zHasta);

        tb->addWidget(new QLabel(tr("  Vel(t/s): ")));
        m_rate = new QDoubleSpinBox(this);
        m_rate->setRange(0.5, 50.0); m_rate->setValue(2.0); m_rate->setDecimals(1);
        m_rate->setToolTip(tr("Lanzamientos por segundo (tope suave con la fuente)."));
        tb->addWidget(m_rate);

        tb->addWidget(new QLabel(tr("  Conex: ")));
        m_conns = new QSpinBox(this);
        m_conns->setRange(1, 8); m_conns->setValue(2);
        m_conns->setToolTip(tr("Peticiones en vuelo a la vez (oculta la latencia). "
                               "Para ir mas rapido, sube tambien la velocidad."));
        tb->addWidget(m_conns);

        m_btnRellenar = new QPushButton(tr("Rellenar"), this);
        connect(m_btnRellenar, &QPushButton::clicked, this, &Ventana::alRellenar);
        tb->addWidget(m_btnRellenar);

        m_btnNueva = new QPushButton(tr("Nueva base..."), this);
        m_btnNueva->setToolTip(tr("Crea un .sqlitedb nuevo, todo de la fuente "
                                  "elegida, en el area marcada (o la vista actual)."));
        connect(m_btnNueva, &QPushButton::clicked, this, &Ventana::alNuevaBase);
        tb->addWidget(m_btnNueva);

        m_btnAuto = new QPushButton(tr("Descargar al navegar"), this);
        m_btnAuto->setCheckable(true);
        m_btnAuto->setToolTip(tr("Mientras navegas, baja las teselas que falten "
                                 "en la capa activa, al ZOOM ACTUAL. Ideal para ir "
                                 "llenando lo que miras. (El rectangulo sirve para "
                                 "un rango de zoom en un area.)"));
        connect(m_btnAuto, &QPushButton::toggled, this, [this](bool on) {
            m_autoOn = on;
            if (on) m_debounce->start();   // rellena ya la vista actual
        });
        tb->addWidget(m_btnAuto);

        // Rejilla de depuracion (como render_map --grid): dibuja el borde de cada
        // tesela con su z/x/y. Verde = tesela EXACTA (esta en la BD); rojo = se
        // esta pintando con un ANCESTRO ampliado porque falta a este zoom. Asi se
        // ve de un vistazo que teselas faltan en la zona visible.
        m_btnGrid = new QPushButton(tr("Rejilla"), this);
        m_btnGrid->setCheckable(true);
        m_btnGrid->setToolTip(tr("Muestra la rejilla de teselas con z/x/y.\n"
                                 "Verde: tesela propia (en la BD).\n"
                                 "Rojo: falta a este zoom (se ve con un ancestro "
                                 "ampliado)."));
        connect(m_btnGrid, &QPushButton::toggled, this, [this](bool on) {
            m_mapa->setDebugGridVisible(on);
        });
        tb->addWidget(m_btnGrid);

        // Mancha de COBERTURA: capa fija que muestra que zonas del zoom objetivo
        // ya estan en la BD, coloreadas por completitud (verde = llena). Se ve
        // aunque estes mirando a un zoom mucho menor. El spin elige ese zoom.
        m_btnCobertura = new QPushButton(tr("Cobertura"), this);
        m_btnCobertura->setCheckable(true);
        m_btnCobertura->setToolTip(tr("Mancha fija con las zonas que ya tienen "
                                      "teselas del zoom elegido al lado.\n"
                                      "Verde: celda llena. Ambar: a medias.\n"
                                      "Visible aunque mires a otro zoom."));
        connect(m_btnCobertura, &QPushButton::toggled, this, [this](bool on) {
            m_mapa->setCoverageZoom(m_zCobertura->value());
            m_mapa->setCoverageVisible(on);
        });
        tb->addWidget(m_btnCobertura);

        tb->addWidget(new QLabel(tr(" z:")));
        m_zCobertura = new QSpinBox(this);
        m_zCobertura->setRange(0, 22);
        m_zCobertura->setValue(14);
        // Por defecto, el zoom maximo recomendado de la capa activa.
        {
            const QString id = m_capa->currentData().toString();
            if (m_datasets.contains(id))
                m_zCobertura->setValue(
                    qBound(0, m_datasets.value(id).recommendedMaxZoom, 22));
        }
        m_zCobertura->setToolTip(tr("Zoom cuya cobertura se dibuja en la mancha."));
        connect(m_zCobertura, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
            if (m_btnCobertura->isChecked())
                m_mapa->setCoverageZoom(v);
        });
        tb->addWidget(m_zCobertura);

        // Segunda fila: bbox escrito a mano y la fuente (URL).
        QToolBar *tb2 = new QToolBar(tr("Zona / Fuente"), this);
        tb2->setMovable(false);
        addToolBar(Qt::TopToolBarArea, tb2);
        insertToolBarBreak(tb2);

        tb2->addWidget(new QLabel(tr("  BBox (latN,lonO,latS,lonE): ")));
        m_bbox = new QLineEdit(this);
        m_bbox->setMinimumWidth(230);
        m_bbox->setPlaceholderText(QStringLiteral("24.86,-86.61,17.40,-72.44"));
        m_bbox->setToolTip(tr("Escribe o pega el rectangulo como en fill_tiles y "
                              "pulsa Intro (o \"Usar\"). Tambien se rellena solo al "
                              "dibujar con \"Seleccionar area\"."));
        connect(m_bbox, &QLineEdit::returnPressed, this, &Ventana::aplicarBboxTexto);
        tb2->addWidget(m_bbox);
        QPushButton *btnUsar = new QPushButton(tr("Usar"), this);
        connect(btnUsar, &QPushButton::clicked, this, &Ventana::aplicarBboxTexto);
        tb2->addWidget(btnUsar);

        tb2->addWidget(new QLabel(tr("   Fuente (URL {z}/{x}/{y}): ")));
        m_url = new QLineEdit(QString::fromLatin1(kFuenteDefecto), this);
        m_url->setMinimumWidth(520);
        tb2->addWidget(m_url);

        // Elevacion del terreno: elige la carpeta con los `.hgt` (SRTM). Una vez
        // puesta, la cota aparece bajo el cursor en la barra de estado.
        m_btnDem = new QPushButton(tr("DEM..."), this);
        m_btnDem->setToolTip(tr("Carpeta con ficheros de elevacion SRTM .hgt "
                                "(N19W077.hgt ...). Muestra la cota bajo el cursor."));
        connect(m_btnDem, &QPushButton::clicked, this, &Ventana::alElegirDem);
        tb2->addWidget(m_btnDem);

        sincronizarZoomDesde();
    }

    // Coloca en la barra de estado la barra de progreso y el boton de cancelar
    // (ocultos hasta que arranca una descarga).
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
        // Cota del terreno bajo el cursor (se rellena al mover el raton si hay DEM).
        m_cota = new QLabel(this);
        m_cota->setMinimumWidth(90);
        m_cota->setToolTip(tr("Altura del terreno bajo el cursor (necesita DEM)."));
        statusBar()->addPermanentWidget(m_cota);
        statusBar()->addPermanentWidget(m_barra);
        statusBar()->addPermanentWidget(m_btnCancelar);
    }

    // Abre un dialogo para elegir la carpeta de los `.hgt` y la activa en caliente.
    void alElegirDem()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Carpeta de ficheros de elevacion (.hgt)"));
        if (dir.isEmpty())
            return;
        m_mapa->setElevationDir(dir);
        m_demActivo = true;
        statusBar()->showMessage(tr("Elevacion: %1").arg(dir), 4000);
    }

    // Muestra la cota de una coordenada en la etiqueta de la barra de estado. Sin
    // DEM, o si no hay dato (mar, hueco), deja un guion.
    void mostrarCota(const QGeoCoordinate &p)
    {
        if (!m_cota)
            return;
        if (!m_demActivo) {
            m_cota->clear();
            return;
        }
        const double m = m_mapa->elevationAt(p);
        m_cota->setText(std::isnan(m)
            ? QStringLiteral("  --- m  ")
            : QStringLiteral("  %1 m  ").arg(m, 0, 'f', 0));
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
        m_poly.clear();                 // un rectangulo nuevo anula el poligono
        // Refleja el rectangulo en el campo de texto (mismo formato que fill_tiles).
        m_bbox->setText(QStringLiteral("%1,%2,%3,%4")
            .arg(no.latitude(), 0, 'f', 5).arg(no.longitude(), 0, 'f', 5)
            .arg(se.latitude(), 0, 'f', 5).arg(se.longitude(), 0, 'f', 5));
        statusBar()->showMessage(
            tr("Zona: N %1  O %2  ->  S %3  E %4")
                .arg(no.latitude(), 0, 'f', 3).arg(no.longitude(), 0, 'f', 3)
                .arg(se.latitude(), 0, 'f', 3).arg(se.longitude(), 0, 'f', 3),
            8000);
    }

    //! Poligono cerrado con la herramienta "Poligono": queda como zona activa
    //! (tiene prioridad sobre el rectangulo hasta que se marque uno nuevo).
    void alSeleccionarPoligono(const QVector<QGeoCoordinate> &poly)
    {
        m_poly = poly;
        m_hayArea = true;               // hay zona (aunque sea poligono)
        statusBar()->showMessage(
            tr("Poligono de %1 vertices. Pulsa Rellenar para bajar solo su interior.")
                .arg(poly.size()), 8000);
    }

    //! Lee el bbox escrito a mano (latN,lonO,latS,lonE), fija la zona y encuadra.
    void aplicarBboxTexto()
    {
        const QStringList p = m_bbox->text().split(QLatin1Char(','));
        bool ok = p.size() == 4;
        double v[4] = {0, 0, 0, 0};
        for (int i = 0; ok && i < 4; ++i) {
            bool o = false;
            v[i] = p.at(i).trimmed().toDouble(&o);
            ok = ok && o;
        }
        if (!ok) {
            QMessageBox::warning(this, tr("BBox invalido"),
                tr("Formato: latN,lonO,latS,lonE\nEjemplo: 24.86,-86.61,17.40,-72.44"));
            return;
        }
        // v = latN, lonO, latS, lonE.  no = (latN,lonO)  se = (latS,lonE)
        m_no = QGeoCoordinate(qMax(v[0], v[2]), qMin(v[1], v[3]));
        m_se = QGeoCoordinate(qMin(v[0], v[2]), qMax(v[1], v[3]));
        m_hayArea = true;
        m_poly.clear();                  // un bbox escrito anula el poligono
        m_mapa->fitBounds(m_no, m_se);   // encuadra para que se vea la zona
        statusBar()->showMessage(tr("Zona fijada desde el texto."), 5000);
    }

    //! "Rellenar": completa la capa ACTUAL en el area marcada.
    void alRellenar()
    {
        if (ocupado()) {
            QMessageBox::information(this, tr("Ocupado"),
                tr("Ya hay una descarga en curso."));
            return;
        }
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
        if (m_poly.size() >= 3) {
            p.polygon = m_poly;        // el bbox lo calcula TileFiller del poligono
        } else {
            p.latN = m_no.latitude();  p.lonW = m_no.longitude();
            p.latS = m_se.latitude();  p.lonE = m_se.longitude();
        }
        p.minZoom = qMin(m_zDesde->value(), m_zHasta->value());
        p.maxZoom = qMax(m_zDesde->value(), m_zHasta->value());
        p.url = m_url->text().trimmed();
        p.rate = m_rate->value();
        p.connections = m_conns->value();

        ejecutar(p, /*nueva=*/false);
    }

    //! "Nueva base": crea un .sqlitedb NUEVO todo de la fuente elegida, con una
    //! codificacion limpia (XYZ, z=z logico, sin columna s). Reanudable.
    void alNuevaBase()
    {
        if (ocupado()) {
            QMessageBox::information(this, tr("Ocupado"),
                tr("Ya hay una descarga en curso."));
            return;
        }

        const QString file = QFileDialog::getSaveFileName(
            this, tr("Nueva base de teselas"),
            QStringLiteral("Nueva_Clarity.sqlitedb"),
            tr("SQLite (*.sqlitedb *.db *.sqlite)"));
        if (file.isEmpty())
            return;

        // Zona: el poligono marcado si lo hay; si no el rectangulo; si no, la
        // vista actual.
        QGeoCoordinate no = m_hayArea ? m_no : m_mapa->visibleNorthWest();
        QGeoCoordinate se = m_hayArea ? m_se : m_mapa->visibleSouthEast();

        TileFiller::Params p;
        p.ds.id = QStringLiteral("clarity");
        p.ds.displayName = QStringLiteral("Satelital (Esri Clarity)");
        p.ds.filePath = file;
        p.ds.tableName = QStringLiteral("tiles");
        p.ds.zFactor = 1; p.ds.zOffset = 0;
        p.ds.scheme = TileScheme::XYZ;
        p.ds.hasSColumn = false;
        p.ds.tileSize = 256;
        p.ds.colX = QStringLiteral("x");
        p.ds.colY = QStringLiteral("y");
        p.ds.colZ = QStringLiteral("z");
        p.ds.colImage = QStringLiteral("image");
        p.ds.minZoom = qMin(m_zDesde->value(), m_zHasta->value());
        p.ds.maxZoom = qMax(m_zDesde->value(), m_zHasta->value());
        p.ds.recommendedMaxZoom = p.ds.maxZoom;
        p.ds.baseZoom = p.ds.minZoom;
        p.createSchema = true;

        if (m_poly.size() >= 3) {
            p.polygon = m_poly;        // el bbox lo calcula TileFiller del poligono
        } else {
            p.latN = no.latitude();  p.lonW = no.longitude();
            p.latS = se.latitude();  p.lonE = se.longitude();
        }
        p.minZoom = p.ds.minZoom;
        p.maxZoom = p.ds.maxZoom;
        p.url = m_url->text().trimmed();
        p.rate = m_rate->value();
        p.connections = m_conns->value();

        ejecutar(p, /*nueva=*/true);
    }

    // ¿Hay ya una descarga en curso? (evita lanzar dos a la vez).
    bool ocupado() const { return m_filler != nullptr; }

    /*!
     * \brief Flujo comun de descarga.
     * \param silencioso  true = modo "al navegar": sin confirmacion ni dialogos,
     *        sin bloquear los controles; el progreso va en la barra de estado.
     */
    void ejecutar(TileFiller::Params p, bool nueva, bool silencioso = false)
    {
        auto *filler = new TileFiller(this);
        QString err;
        if (!filler->prepare(p, &err)) {
            if (!silencioso) QMessageBox::warning(this, tr("Error"), err);
            filler->deleteLater();
            return;
        }

        const qint64 total = filler->totalToDownload();
        if (total == 0) {
            if (silencioso)
                statusBar()->showMessage(tr("Vista completa (nada que bajar)."), 2000);
            else
                QMessageBox::information(this, tr("Nada que hacer"),
                    tr("No falta ninguna tesela en esa zona y ese rango de zoom."));
            filler->deleteLater();
            return;
        }

        if (!silencioso) {
            // Estima el tamano (muestreo asincrono); se espera con un QEventLoop
            // local -la interfaz sigue viva, es un bucle anidado como el de un
            // dialogo modal- para poder mostrar los MB en la confirmacion.
            qint64 estBytes = 0;
            int sampled = 0;
            {
                QEventLoop espera;
                connect(filler, &TileFiller::sizeEstimated, &espera,
                        [&](double, qint64 eb, int s) {
                            estBytes = eb; sampled = s; espera.quit();
                        });
                statusBar()->showMessage(tr("Estimando tamano..."));
                filler->estimateSize();
                espera.exec();
                statusBar()->clearMessage();
            }

            QString detalle;
            for (const auto &pz : filler->perZoomMissing())
                if (pz.second > 0)
                    detalle += tr("  z%1: %2\n").arg(pz.first).arg(pz.second);
            QString aviso = tr("Se descargaran %1 teselas (~%2 MB%3).\n\n%4")
                                .arg(total)
                                .arg(double(estBytes) / 1048576.0, 0, 'f', 1)
                                .arg(sampled > 0 ? QString() : tr(" aprox."))
                                .arg(detalle);
            if (total > 50000)
                aviso += tr("\nATENCION: son muchas; puede tardar bastante.");
            aviso += tr("\nA %1 t/s son ~%2 minutos.\n\n¿Continuar?")
                         .arg(p.rate, 0, 'f', 1)
                         .arg(double(total) / p.rate / 60.0, 0, 'f', 1);
            if (QMessageBox::question(this, tr("Confirmar descarga"), aviso)
                != QMessageBox::Yes) {
                filler->deleteLater();
                return;
            }
        }

        m_filler = filler;
        if (!silencioso) {
            m_running = true;
            // Sale del modo "seleccionar area": el area ya quedo guardada en
            // m_no/m_se, y mantener la herramienta activa impediria DESPLAZAR el
            // mapa durante la descarga (el arrastre dibujaria otro rectangulo).
            m_mapa->setActiveTool(MapTool::None);
            m_btnArea->setChecked(false);
            m_btnPoly->setChecked(false);
            ponerControles(false);
            m_barra->setRange(0, int(qMin<qint64>(total, 1000000)));
            m_barra->setValue(0);
            m_barra->setVisible(true);
            m_btnCancelar->setVisible(true);
        }

        connect(filler, &TileFiller::progress, this,
                [this, silencioso](qint64 done, qint64 tot, double tps) {
            if (!silencioso)
                m_barra->setValue(int(qMin<qint64>(done, 1000000)));
            const double restan = tps > 0 ? double(tot - done) / tps / 60.0 : 0.0;
            statusBar()->showMessage(silencioso
                ? tr("Navegar: bajando %1/%2  %3 t/s").arg(done).arg(tot).arg(tps, 0, 'f', 1)
                : tr("%1/%2  %3 t/s  ~%4 min restantes")
                      .arg(done).arg(tot).arg(tps, 0, 'f', 1).arg(restan, 0, 'f', 1));
            // Si la mancha de cobertura esta visible, se va refrescando sola segun
            // llegan teselas (limitado a una vez cada ~2.5 s: la consulta es un
            // GROUP BY y no conviene por cada tesela). El estado exacto final lo
            // deja el refresco de 'finished'.
            if (m_btnCobertura && m_btnCobertura->isChecked()
                && (!m_lastCov.isValid() || m_lastCov.elapsed() > 2500)) {
                m_lastCov.restart();
                m_mapa->refreshCoverage();
            }
        });
        connect(filler, &TileFiller::zoomFinished, this, [this, nueva](int, qint64 added) {
            if (!nueva && added > 0) m_mapa->reloadBaseLayer();
        });
        connect(filler, &TileFiller::throttling, this, [this](int pauseSec, qint64 racha) {
            statusBar()->showMessage(
                tr("Auto-freno: %1 fallos seguidos (la fuente limita). "
                   "Pausa %2 s y reanudo...").arg(racha).arg(pauseSec), pauseSec * 1000);
        });
        connect(filler, &TileFiller::finished, this,
                [this, filler, nueva, silencioso, p](const TileFiller::Stats &s, bool cancelled) {
            if (!nueva) m_mapa->reloadBaseLayer();
            // Si la mancha de cobertura esta a la vista, recalcularla: acaba de
            // cambiar lo que hay en la BD.
            if (m_btnCobertura && m_btnCobertura->isChecked())
                m_mapa->refreshCoverage();
            m_filler = nullptr;

            if (silencioso) {
                statusBar()->showMessage(tr("Navegar: +%1 teselas").arg(s.downloaded), 2000);
                // Por si la vista se movio mientras bajaba, re-comprueba.
                if (m_autoOn) m_debounce->start();
            } else {
                m_barra->setVisible(false);
                m_btnCancelar->setVisible(false);
                ponerControles(true);
                m_running = false;

                QMessageBox box(this);
                box.setWindowTitle(cancelled ? tr("Cancelado") : tr("Terminado"));
                box.setText(tr("Descargadas: %1\nSin origen (404): %2\nFallidas: %3")
                                .arg(s.downloaded).arg(s.notFound).arg(s.failed));
                if (nueva) {
                    const QString snip = QStringLiteral(
                        "    {\n"
                        "      \"id\": \"%1\",\n"
                        "      \"displayName\": \"%2\",\n"
                        "      \"filePath\": \"%3\",\n"
                        "      \"tableName\": \"tiles\",\n"
                        "      \"zFactor\": 1, \"zOffset\": 0,\n"
                        "      \"minZoom\": %4, \"maxZoom\": %5, \"recommendedMaxZoom\": %5,\n"
                        "      \"typicalFill\": 1.0, \"scheme\": \"XYZ\",\n"
                        "      \"hasSColumn\": false, \"tileSize\": 256,\n"
                        "      \"colZ\": \"z\", \"colX\": \"x\", \"colY\": \"y\",\n"
                        "      \"colImage\": \"image\", \"baseZoom\": %4\n"
                        "    }")
                        .arg(p.ds.id, p.ds.displayName, QFileInfo(p.ds.filePath).fileName())
                        .arg(p.minZoom).arg(p.maxZoom);
                    box.setInformativeText(
                        tr("Anade esta entrada al array \"datasets\" de tu datasets.json "
                           "(boton \"Show Details\" para copiarla):"));
                    box.setDetailedText(snip);
                }
                box.exec();
            }
            filler->deleteLater();
        });

        filler->start();
    }

    //! Modo "descargar al navegar": baja las teselas del viewport actual (zoom
    //! actual) que falten en la BD de la capa activa. Silencioso.
    void dispararAuto()
    {
        if (!m_autoOn || ocupado())
            return;
        const QString id = m_capa->currentData().toString();
        if (!m_datasets.contains(id))
            return;

        TileFiller::Params p;
        p.ds = m_datasets.value(id);
        const QGeoCoordinate no = m_mapa->visibleNorthWest();
        const QGeoCoordinate se = m_mapa->visibleSouthEast();
        p.latN = no.latitude();  p.lonW = no.longitude();
        p.latS = se.latitude();  p.lonE = se.longitude();
        p.minZoom = p.maxZoom = m_mapa->zoom();   // solo el zoom actual
        p.url = m_url->text().trimmed();
        p.rate = m_rate->value();
        p.connections = m_conns->value();

        ejecutar(p, /*nueva=*/false, /*silencioso=*/true);
    }

    // Habilita o deshabilita en bloque los controles de la barra, para bloquear la
    // interfaz mientras hay una descarga (no silenciosa) en marcha.
    void ponerControles(bool on)
    {
        m_btnRellenar->setEnabled(on);
        m_btnNueva->setEnabled(on);
        m_btnAuto->setEnabled(on);
        m_capa->setEnabled(on);
        m_btnArea->setEnabled(on);
        m_btnPoly->setEnabled(on);
        m_zDesde->setEnabled(on);
        m_zHasta->setEnabled(on);
        m_rate->setEnabled(on);
        m_conns->setEnabled(on);
        m_bbox->setEnabled(on);
        m_url->setEnabled(on);
    }

    QString m_datasetsFile;
    QHash<QString, TileDataset> m_datasets;
    MapWidget *m_mapa = nullptr;

    QComboBox *m_capa = nullptr;
    QPushButton *m_btnArea = nullptr;
    QPushButton *m_btnPoly = nullptr;
    QSpinBox *m_zDesde = nullptr;
    QSpinBox *m_zHasta = nullptr;
    QDoubleSpinBox *m_rate = nullptr;
    QSpinBox *m_conns = nullptr;
    QLineEdit *m_bbox = nullptr;
    QLineEdit *m_url = nullptr;
    QPushButton *m_btnRellenar = nullptr;
    QPushButton *m_btnNueva = nullptr;
    QPushButton *m_btnAuto = nullptr;
    QPushButton *m_btnGrid = nullptr;
    QPushButton *m_btnCobertura = nullptr;
    QSpinBox *m_zCobertura = nullptr;
    QPushButton *m_btnDem = nullptr;
    QLabel *m_cota = nullptr;
    bool m_demActivo = false;   // hay carpeta de elevacion cargada
    QTimer *m_debounce = nullptr;
    bool m_autoOn = false;
    QProgressBar *m_barra = nullptr;
    QPushButton *m_btnCancelar = nullptr;

    QGeoCoordinate m_no, m_se;
    QVector<QGeoCoordinate> m_poly;   //!< Poligono de seleccion (>=3 = activo)
    bool m_hayArea = false;
    bool m_running = false;
    TileFiller *m_filler = nullptr;
    QElapsedTimer m_lastCov;   // limita el refresco de la cobertura al descargar
};

// Punto de entrada de la version con ventana: crea la QApplication, abre la
// Ventana sobre el datasets.json indicado (o ./datasets.json), avisa si falta
// soporte TLS (toda descarga HTTPS fallaria) y entra en el bucle de eventos.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    // Argumentos: el primer positional es el datasets.json; --dem <carpeta> fija
    // la carpeta de ficheros de elevacion SRTM `.hgt` (opcional).
    QString datasets;
    QString demDir;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if ((a == QLatin1String("--dem") || a == QLatin1String("--elev"))
            && i + 1 < argc)
            demDir = QString::fromLocal8Bit(argv[++i]);
        else if (datasets.isEmpty() && !a.startsWith(QLatin1String("--")))
            datasets = a;
    }
    if (datasets.isEmpty())
        datasets = QStringLiteral("datasets.json");

    Ventana v(datasets, demDir);
    v.show();
    // Sin TLS, toda descarga HTTPS falla: avisar en claro (Windows: falta OpenSSL).
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::warning(&v, QObject::tr("Sin TLS/SSL"),
            QObject::tr("Este Qt no tiene soporte TLS/SSL, asi que las descargas "
                        "HTTPS van a fallar.\n\nEn Windows suele faltar OpenSSL: "
                        "copia libssl-3-x64.dll y libcrypto-3-x64.dll (OpenSSL 3, "
                        "64-bit) junto al .exe o en el PATH.\n\nQt esperaba: %1")
                .arg(QSslSocket::sslLibraryBuildVersionString()));
    }
    return app.exec();
}

#include "main.moc"
