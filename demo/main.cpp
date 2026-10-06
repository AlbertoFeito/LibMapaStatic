/*!
 * demo - banco de pruebas de libmapa::MapWidget.
 *
 * Uso:
 *     demo [ruta/a/datasets.json]
 *
 * Ejercita TODA la API de la Fase 6 sin escribir codigo: capas con
 * visibilidad y orden, dibujo y edicion de entidades, propiedades y atributos
 * de dominio, estilo, deshacer/rehacer y persistencia en SQLite.
 *
 * El panel lateral se mantiene al dia SOLO a partir de las senales del
 * MapWidget (featureAdded/featureRemoved/featureLayersChanged): no hay que
 * acordarse de refrescarlo tras cada operacion, asi que da igual que el cambio
 * venga de un boton, del teclado sobre el mapa, de deshacer o de una carga.
 */

#include "libmapa/MapWidget.h"

#include "qcustomplot.h"      // ya compilado dentro de libmapa_widget; aquí solo se usa

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QGroupBox>
#include <QHash>
#include <QMouseEvent>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <optional>

using namespace libmapa;

namespace {
//! Roles para distinguir capas de entidades en el arbol.
constexpr int RolCapa = Qt::UserRole;        //!< id de capa (en ambos)
constexpr int RolEntidad = Qt::UserRole + 1; //!< id de entidad (solo hojas)

// --- Iconos de objetivos, dibujados en codigo -----------------------------
// La libreria es agnostica del dominio: el JUEGO de iconos lo pone la app. Aqui
// se dibujan a mano (sin ficheros) apuntando al NORTE (arriba); la libreria los
// gira segun el rumbo. Es justo lo que hace un producto real con su simbologia.

// Casco de buque: proa arriba, popa abajo.
QPixmap iconoBuque(const QColor &c)
{
    const int S = 22;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, 1.0));
    p.setBrush(c);
    QPolygonF casco;
    casco << QPointF(S / 2.0, 2) << QPointF(S - 6, S - 5)
          << QPointF(S / 2.0, S - 2) << QPointF(5, S - 5);
    p.drawPolygon(casco);
    return pm;
}

// Silueta de aeronave: fuselaje, alas y cola, apuntando arriba.
QPixmap iconoAereo(const QColor &c)
{
    const int S = 22;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, 1.0));
    p.setBrush(c);
    const double cx = S / 2.0;
    QPolygonF av;
    av << QPointF(cx, 1)
       << QPointF(cx + 2, S * 0.45)
       << QPointF(S - 2, S * 0.62)
       << QPointF(cx + 2, S * 0.62)
       << QPointF(cx + 2, S - 4)
       << QPointF(cx + 4, S - 1)
       << QPointF(cx - 4, S - 1)
       << QPointF(cx - 2, S - 4)
       << QPointF(cx - 2, S * 0.62)
       << QPointF(2, S * 0.62)
       << QPointF(cx - 2, S * 0.45);
    p.drawPolygon(av);
    return pm;
}

// Cuadricoptero: cuatro brazos en X con sus rotores y un nucleo.
QPixmap iconoUav(const QColor &c)
{
    const int S = 22;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double m = 4, M = S - 4, cx = S / 2.0;
    p.setPen(QPen(c.darker(160), 2.0));
    p.drawLine(QPointF(m, m), QPointF(M, M));
    p.drawLine(QPointF(M, m), QPointF(m, M));
    p.setPen(QPen(Qt::black, 0.8));
    p.setBrush(c);
    for (const QPointF &o : {QPointF(m, m), QPointF(M, m),
                             QPointF(m, M), QPointF(M, M)})
        p.drawEllipse(o, 3.2, 3.2);
    p.setBrush(c.darker(130));
    p.drawEllipse(QPointF(cx, cx), 2.5, 2.5);
    return pm;
}


} // namespace

class Ventana : public QMainWindow
{
    Q_OBJECT

    //! Punto que se está capturando del mapa (pestaña Elevación).
    enum class Pick { Ninguno, A, B };

public:
    //! \a origen es un datasets.json o, si \a esPaquete, la carpeta de un
    //! paquete de datos (mapa.json), que ya trae elevacion, capas fijas y
    //! entidades: entonces basta con esa linea.
    explicit Ventana(const QString &origen, bool esPaquete,
                     const QString &demDir = QString(),
                     const QString &demDb = QString(),
                     const QString &featuresDb = QString())
    {
        // --- Asi se crea el mapa. Esto es todo. -------------------------
        MapConfig cfg;
        if (esPaquete) {
            cfg.dataDir = origen;           // el resto lo pone el paquete
        } else {
            cfg.datasetsFile = origen;
            cfg.initialCenter = QGeoCoordinate(23.1136, -82.3666);   // La Habana
            cfg.initialZoom = 11;
        }
        cfg.elevationDir = demDir;          // carpeta .hgt para la cota (opcional)
        cfg.elevationDbFile = demDb;        // BD de elevacion (prioritaria si viene)
        cfg.featuresDbFile = featuresDb;    // persistencia automatica (opcional)
        cfg.cacheMiB = 192;

        m_demActivo = esPaquete || !demDir.isEmpty() || !demDb.isEmpty();
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        actualizarTitulo();
        resize(1200, 800);

        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo iniciar el mapa:\n%1\n\n"
                   "Pasa como argumento la carpeta de un paquete de datos "
                   "(con su mapa.json, ver probe_db --package) o un "
                   "datasets.json.").arg(m_mapa->lastError()));
            return;
        }

        // Coalesce de reconstrucciones: un "Vaciar" o una carga emiten una
        // senal por entidad; sin esto reconstruiriamos el panel N veces.
        m_reconstruir = new QTimer(this);
        m_reconstruir->setSingleShot(true);
        m_reconstruir->setInterval(0);
        connect(m_reconstruir, &QTimer::timeout, this, &Ventana::reconstruirPanel);

        construirBarraMapa();
        construirBarraEntidades();
        construirBarraDatos();
        construirPanel();
        conectarSenales();
        aplicarEstiloAlTrazo();
        prepararSeguimiento();
        reconstruirPanel();

        // Asi deberia hacerlo una app real: el mapa arranca con lo que funcione
        // y avisa de lo que no (fichero que falta, imagenes sin plugin...).
        const QStringList problemas = m_mapa->dataWarnings();
        if (!problemas.isEmpty())
            QMessageBox::warning(this, tr("Datos del mapa"),
                tr("El paquete de datos tiene problemas; el mapa usara lo que "
                   "funcione:\n\n%1").arg(problemas.join(QLatin1Char('\n'))));
    }

private:
    // ==================================================== barras ==========
    void construirBarraMapa()
    {
        auto *barra = addToolBar(tr("Mapa"));
        barra->setObjectName(QStringLiteral("barraMapa"));
        barra->setMovable(false);

        barra->addWidget(new QLabel(tr("  Base: ")));
        m_capasBase = new QComboBox(this);
        for (const BaseLayerInfo &c : m_mapa->availableBaseLayers())
            m_capasBase->addItem(c.displayName, c.id);
        m_capasBase->setCurrentIndex(m_capasBase->findData(m_mapa->baseLayerId()));
        barra->addWidget(m_capasBase);

        barra->addSeparator();
        auto *masZoom = barra->addAction(tr("Ampliar"));
        auto *menosZoom = barra->addAction(tr("Reducir"));
        connect(masZoom, &QAction::triggered, m_mapa, &MapWidget::zoomIn);
        connect(menosZoom, &QAction::triggered, m_mapa, &MapWidget::zoomOut);
        auto *cuba = barra->addAction(tr("Toda Cuba"));
        connect(cuba, &QAction::triggered, this, [this] {
            m_mapa->fitBounds(QGeoCoordinate(23.3, -85.0),
                              QGeoCoordinate(19.7, -74.0));
        });

        barra->addSeparator();
        auto *rejilla = barra->addAction(tr("Rejilla"));
        rejilla->setCheckable(true);
        connect(rejilla, &QAction::toggled, m_mapa, &MapWidget::setDebugGridVisible);

        // Mancha de COBERTURA por zoom (como en fill_map, pero sin descarga):
        // muestra que zonas del zoom elegido ya estan en la BD, visible aunque
        // mires a otro zoom. Verde = llena, ambar = a medias.
        auto *cobertura = barra->addAction(tr("Cobertura"));
        cobertura->setCheckable(true);
        cobertura->setToolTip(tr("Mancha fija de las zonas que ya tienen teselas\n"
                                 "del zoom elegido al lado (verde: llena; ambar: a medias)."));
        barra->addWidget(new QLabel(tr(" z:")));
        m_zCobertura = new QSpinBox(this);
        m_zCobertura->setRange(0, 22);
        m_zCobertura->setValue(14);
        m_zCobertura->setToolTip(tr("Zoom cuya cobertura se dibuja en la mancha."));
        barra->addWidget(m_zCobertura);
        connect(cobertura, &QAction::toggled, this, [this](bool on) {
            m_mapa->setCoverageZoom(m_zCobertura->value());
            m_mapa->setCoverageVisible(on);
        });
        connect(m_zCobertura, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, cobertura](int v) {
            if (cobertura->isChecked())
                m_mapa->setCoverageZoom(v);
        });

        // Elevacion del terreno: elige una CARPETA de `.hgt` o una BASE DE DATOS
        // `.sqlitedb`; la cota aparece bajo el cursor en la barra de estado.
        m_btnDem = new QPushButton(tr("DEM..."), this);
        m_btnDem->setToolTip(tr("Origen de elevacion: carpeta de .hgt o base de "
                                "datos .sqlitedb. Muestra la cota bajo el cursor."));
        QMenu *menuDem = new QMenu(m_btnDem);
        connect(menuDem->addAction(tr("Carpeta de .hgt...")),
                &QAction::triggered, this, &Ventana::alElegirDemCarpeta);
        connect(menuDem->addAction(tr("Base de datos .sqlitedb...")),
                &QAction::triggered, this, &Ventana::alElegirDemDb);
        m_btnDem->setMenu(menuDem);
        barra->addWidget(m_btnDem);

        barra->addSeparator();
        // Persistencia: guardar y abrir un fichero SQLite de entidades.
        auto *guardar = barra->addAction(tr("Guardar"));
        guardar->setShortcut(QKeySequence::Save);
        auto *guardarComo = barra->addAction(tr("Guardar como..."));
        auto *abrir = barra->addAction(tr("Abrir..."));
        abrir->setShortcut(QKeySequence::Open);
        connect(guardar, &QAction::triggered, this, &Ventana::guardar);
        connect(guardarComo, &QAction::triggered, this, &Ventana::guardarComo);
        connect(abrir, &QAction::triggered, this, &Ventana::abrir);
    }

    void construirBarraEntidades()
    {
        auto *barra = addToolBar(tr("Entidades"));
        barra->setObjectName(QStringLiteral("barraEntidades"));
        barra->setMovable(false);

        // Todas las herramientas en UN grupo exclusivo: elegir una desmarca la
        // anterior, tambien entre navegacion y dibujo (en el demo anterior
        // "Medir" no se desmarcaba al empezar a dibujar).
        m_grupo = new QActionGroup(this);
        m_grupo->setExclusive(true);

        m_accNavegar = nuevaHerramienta(barra, tr("Navegar"),   MapTool::None);
        nuevaHerramienta(barra, tr("Medir"),     MapTool::Measure);
        nuevaHerramienta(barra, tr("Zoom area"), MapTool::AreaZoom);
        barra->addSeparator();
        nuevaHerramienta(barra, tr("Punto"),     MapTool::DrawPoint,    QStringLiteral("1"));
        nuevaHerramienta(barra, tr("Linea"),     MapTool::DrawPolyline, QStringLiteral("2"));
        nuevaHerramienta(barra, tr("Poligono"),  MapTool::DrawPolygon,  QStringLiteral("3"));
        nuevaHerramienta(barra, tr("Editar"),    MapTool::EditFeature,  QStringLiteral("4"));
        m_accNavegar->setChecked(true);

        barra->addSeparator();
        m_accDeshacer = barra->addAction(tr("Deshacer"));
        m_accDeshacer->setShortcut(QKeySequence::Undo);
        m_accRehacer = barra->addAction(tr("Rehacer"));
        m_accRehacer->setShortcut(QKeySequence::Redo);
        connect(m_accDeshacer, &QAction::triggered, m_mapa, [this] { m_mapa->undo(); });
        connect(m_accRehacer,  &QAction::triggered, m_mapa, [this] { m_mapa->redo(); });

        barra->addSeparator();
        m_accBorrar = barra->addAction(tr("Borrar entidad"));
        m_accBorrar->setToolTip(tr("Borra la entidad seleccionada. Tambien Supr "
                                   "con el panel enfocado."));
        connect(m_accBorrar, &QAction::triggered, this, &Ventana::borrarSeleccion);
    }

    QAction *nuevaHerramienta(QToolBar *barra, const QString &texto, MapTool tool,
                              const QString &atajo = QString())
    {
        QAction *a = barra->addAction(texto);
        a->setCheckable(true);
        a->setData(static_cast<int>(tool));
        if (!atajo.isEmpty())
            a->setShortcut(QKeySequence(atajo));
        m_grupo->addAction(a);
        return a;
    }

    //! Barra: cargar ficheros .geo y simular objetivos moviles.
    void construirBarraDatos()
    {
        auto *barra = addToolBar(tr("Datos"));
        barra->setObjectName(QStringLiteral("barraDatos"));
        barra->setMovable(false);

        auto *geo = barra->addAction(tr("Cargar .geo..."));
        geo->setToolTip(tr("Carga un contorno .geo como una capa nueva"));
        connect(geo, &QAction::triggered, this, &Ventana::cargarGeo);

        barra->addSeparator();
        barra->addWidget(new QLabel(tr("  Objetivos: ")));
        m_numObjetivos = new QSpinBox(this);
        m_numObjetivos->setRange(1, 5000);
        m_numObjetivos->setValue(250);
        barra->addWidget(m_numObjetivos);

        m_accSimular = barra->addAction(tr("Simular"));
        m_accSimular->setCheckable(true);
        m_accSimular->setToolTip(tr("Crea objetivos moviles con traza y los "
                                    "actualiza en tiempo real"));
        connect(m_accSimular, &QAction::toggled, this, &Ventana::alternarSimulacion);

        // Opciones de traza: sin traza / N puntos / toda.
        barra->addWidget(new QLabel(tr("  Traza: ")));
        m_traza = new QComboBox(this);
        m_traza->addItem(tr("Sin traza"), 0);
        m_traza->addItem(tr("10"), 10);
        m_traza->addItem(tr("100"), 100);
        m_traza->addItem(tr("500"), 500);
        m_traza->addItem(tr("Toda"), -1);
        m_traza->setCurrentIndex(2);            // 100 por defecto
        barra->addWidget(m_traza);
        m_mapa->setTargetTrailLength(100);
        connect(m_traza, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int i) {
                    m_mapa->setTargetTrailLength(m_traza->itemData(i).toInt());
                });

        // Reloj de la simulacion: ~10 pasos por segundo.
        m_simReloj = new QTimer(this);
        m_simReloj->setInterval(100);
        connect(m_simReloj, &QTimer::timeout, this, &Ventana::pasoSimulacion);
    }

    // ==================================================== panel ===========
    void construirPanel()
    {
        auto *dock = new QDockWidget(tr("Capas y entidades"), this);
        dock->setObjectName(QStringLiteral("panel"));
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        dock->setMinimumWidth(330);

        auto *cont = new QWidget(dock);
        auto *caja = new QVBoxLayout(cont);
        caja->setContentsMargins(6, 6, 6, 6);

        // --- Arbol de capas y entidades ---------------------------------
        caja->addWidget(new QLabel(tr("<b>Capas y entidades</b>")));
        m_arbol = new QTreeWidget(cont);
        m_arbol->setColumnCount(3);
        m_arbol->setHeaderLabels({tr("Nombre"), tr("Tipo"), tr("Vert.")});
        m_arbol->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_arbol->setColumnWidth(1, 90);
        m_arbol->setColumnWidth(2, 44);
        caja->addWidget(m_arbol, 3);

        auto *fila = new QHBoxLayout;
        auto *btnNueva  = new QPushButton(tr("Nueva"), cont);
        auto *btnSubir  = new QPushButton(tr("Subir"), cont);
        auto *btnBajar  = new QPushButton(tr("Bajar"), cont);
        auto *btnVaciar = new QPushButton(tr("Vaciar"), cont);
        auto *btnBorrar = new QPushButton(tr("Borrar capa"), cont);
        for (QPushButton *b : {btnNueva, btnSubir, btnBajar, btnVaciar, btnBorrar})
            fila->addWidget(b);
        caja->addLayout(fila);

        connect(btnNueva,  &QPushButton::clicked, this, &Ventana::crearCapa);
        connect(btnSubir,  &QPushButton::clicked, this, [this] { moverCapa(+1); });
        connect(btnBajar,  &QPushButton::clicked, this, [this] { moverCapa(-1); });
        connect(btnVaciar, &QPushButton::clicked, this, &Ventana::vaciarCapa);
        connect(btnBorrar, &QPushButton::clicked, this, &Ventana::borrarCapa);

        // Borrado con Supr, pero solo cuando el foco esta en el panel: asi no
        // choca con el Supr del editor, que sobre el mapa borra vertices.
        auto *accSupr = new QAction(this);
        accSupr->setShortcut(QKeySequence::Delete);
        accSupr->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(accSupr, &QAction::triggered, this, &Ventana::borrarSeleccion);
        m_arbol->addAction(accSupr);

        // --- Propiedades de la entidad seleccionada ---------------------
        caja->addWidget(new QLabel(tr("<b>Entidad seleccionada</b>")));
        auto *form = new QFormLayout;
        m_campoNombre = new QLineEdit(cont);
        m_campoTipo = new QLineEdit(cont);
        m_campoDescripcion = new QLineEdit(cont);
        m_campoTipo->setPlaceholderText(tr("zona_prohibida, punto_interes..."));
        form->addRow(tr("Nombre:"), m_campoNombre);
        form->addRow(tr("Tipo:"), m_campoTipo);
        form->addRow(tr("Descripcion:"), m_campoDescripcion);
        caja->addLayout(form);

        caja->addWidget(new QLabel(tr("Atributos (la libreria no los interpreta):")));
        m_tablaAtributos = new QTableWidget(0, 2, cont);
        m_tablaAtributos->setHorizontalHeaderLabels({tr("Clave"), tr("Valor")});
        m_tablaAtributos->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_tablaAtributos->setMaximumHeight(110);
        caja->addWidget(m_tablaAtributos);

        auto *filaAtr = new QHBoxLayout;
        auto *btnAddAtr = new QPushButton(tr("+ Atributo"), cont);
        auto *btnDelAtr = new QPushButton(tr("- Atributo"), cont);
        filaAtr->addWidget(btnAddAtr);
        filaAtr->addWidget(btnDelAtr);
        caja->addLayout(filaAtr);

        connect(btnAddAtr, &QPushButton::clicked, this, [this] {
            m_tablaAtributos->insertRow(m_tablaAtributos->rowCount());
        });
        connect(btnDelAtr, &QPushButton::clicked, this, [this] {
            const int f = m_tablaAtributos->currentRow();
            if (f >= 0) {
                m_tablaAtributos->removeRow(f);
                aplicarPropiedades();
            }
        });

        // --- Estilo ------------------------------------------------------
        caja->addWidget(new QLabel(tr("<b>Estilo</b>")));
        auto *formEstilo = new QFormLayout;
        m_btnColorLinea = new QPushButton(cont);
        m_btnColorRelleno = new QPushButton(cont);
        formEstilo->addRow(tr("Color de linea:"), m_btnColorLinea);
        formEstilo->addRow(tr("Color de relleno:"), m_btnColorRelleno);

        m_anchoLinea = new QDoubleSpinBox(cont);
        m_anchoLinea->setRange(0.5, 12.0);
        m_anchoLinea->setSingleStep(0.5);
        formEstilo->addRow(tr("Ancho:"), m_anchoLinea);

        m_estiloLinea = new QComboBox(cont);
        m_estiloLinea->addItem(tr("Continua"),   static_cast<int>(Qt::SolidLine));
        m_estiloLinea->addItem(tr("Discontinua"),static_cast<int>(Qt::DashLine));
        m_estiloLinea->addItem(tr("Punteada"),   static_cast<int>(Qt::DotLine));
        m_estiloLinea->addItem(tr("Raya-punto"), static_cast<int>(Qt::DashDotLine));
        formEstilo->addRow(tr("Trazo:"), m_estiloLinea);

        m_radioPunto = new QDoubleSpinBox(cont);
        m_radioPunto->setRange(2.0, 30.0);
        formEstilo->addRow(tr("Radio del punto:"), m_radioPunto);

        m_verEtiqueta = new QCheckBox(tr("Mostrar etiqueta"), cont);
        formEstilo->addRow(QString(), m_verEtiqueta);
        m_verVertices = new QCheckBox(tr("Mostrar vertices"), cont);
        formEstilo->addRow(QString(), m_verVertices);
        caja->addLayout(formEstilo);

        m_aplicarAlTrazo = new QCheckBox(tr("Usar este estilo para lo que dibuje"), cont);
        m_aplicarAlTrazo->setChecked(true);
        caja->addWidget(m_aplicarAlTrazo);
        caja->addStretch(1);

        // Dos pestañas en el panel lateral: «Capas» (lo de arriba) y «Elevación».
        m_tabs = new QTabWidget(dock);
        m_tabs->addTab(cont, tr("Capas"));
        m_tabs->addTab(construirTabElevacion(), tr("Elevación"));
        dock->setWidget(m_tabs);
        addDockWidget(Qt::RightDockWidgetArea, dock);

        // Propiedades -> modelo.
        connect(m_campoNombre, &QLineEdit::editingFinished, this, &Ventana::aplicarPropiedades);
        connect(m_campoTipo, &QLineEdit::editingFinished, this, &Ventana::aplicarPropiedades);
        connect(m_campoDescripcion, &QLineEdit::editingFinished, this, &Ventana::aplicarPropiedades);
        connect(m_tablaAtributos, &QTableWidget::cellChanged, this,
                [this](int, int) { aplicarPropiedades(); });

        // Estilo -> modelo.
        connect(m_btnColorLinea, &QPushButton::clicked, this,
                [this] { elegirColor(&m_colorLinea, m_btnColorLinea); });
        connect(m_btnColorRelleno, &QPushButton::clicked, this,
                [this] { elegirColor(&m_colorRelleno, m_btnColorRelleno); });
        connect(m_anchoLinea, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { aplicarEstilo(); });
        connect(m_radioPunto, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { aplicarEstilo(); });
        connect(m_estiloLinea, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { aplicarEstilo(); });
        connect(m_verEtiqueta, &QCheckBox::toggled, this, [this](bool) { aplicarEstilo(); });
        connect(m_verVertices, &QCheckBox::toggled, this, [this](bool) { aplicarEstilo(); });

        pintarBotonColor(m_btnColorLinea, m_colorLinea);
        pintarBotonColor(m_btnColorRelleno, m_colorRelleno);

        // Interaccion con el arbol.
        connect(m_arbol, &QTreeWidget::itemChanged, this, &Ventana::visibilidadCambiada);
        connect(m_arbol, &QTreeWidget::currentItemChanged, this,
                &Ventana::seleccionEnArbol);
        // Clic en una entidad de la lista: centra el mapa en ella (y la resalta,
        // que ya lo hace la selección). Se usa itemClicked (acción del usuario),
        // no currentItemChanged, para no centrar al reconstruir el árbol.
        connect(m_arbol, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem *it, int) { centrarEnItem(it); });
        connect(m_arbol, &QTreeWidget::itemActivated, this,
                [this](QTreeWidgetItem *it, int) { centrarEnItem(it); });
    }

    // Centroide (lat/lon medios) de una entidad; para un punto, su posición.
    static QGeoCoordinate centroideDe(const MapFeature &f)
    {
        double sLa = 0, sLo = 0; int n = 0;
        for (const QVector<QGeoCoordinate> &parte : f.outlines())
            for (const QGeoCoordinate &c : parte)
                if (c.isValid()) { sLa += c.latitude(); sLo += c.longitude(); ++n; }
        return n ? QGeoCoordinate(sLa / n, sLo / n) : QGeoCoordinate();
    }

    // Centra el mapa en la entidad de una hoja del árbol y la deja seleccionada.
    void centrarEnItem(QTreeWidgetItem *item)
    {
        if (!item || !m_mapa)
            return;
        const qint64 id = item->data(0, RolEntidad).toLongLong();
        if (id <= 0)
            return;
        if (const auto f = m_mapa->feature(id)) {
            const QGeoCoordinate c = centroideDe(*f);
            if (c.isValid())
                m_mapa->setCenter(c);
            if (m_mapa->selectedFeature() != id)
                m_mapa->selectFeature(id);
        }
    }

    // ==================================================== senales =========
    void conectarSenales()
    {
        connect(m_capasBase, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int i) {
                    m_mapa->setBaseLayerId(m_capasBase->itemData(i).toString());
                });

        connect(m_mapa, &MapWidget::mouseMoved, this,
                [this](const QGeoCoordinate &p) {
                    m_coords->setText(QStringLiteral("  %1, %2  ")
                        .arg(p.latitude(), 0, 'f', 5)
                        .arg(p.longitude(), 0, 'f', 5));
                    mostrarCota(p);
                });

        connect(m_mapa, &MapWidget::zoomChanged, this, &Ventana::actualizarEstado);
        connect(m_mapa, &MapWidget::baseLayerChanged, this, &Ventana::actualizarEstado);
        connect(m_mapa, &MapWidget::centerChanged, this, &Ventana::actualizarEstado);

        connect(m_mapa, &MapWidget::measurementFinished, this,
                [this](const Measurement &m) {
                    statusBar()->showMessage(
                        tr("Distancia: %1 km   Marcacion: %2 grados")
                            .arg(m.distanceMeters / 1000.0, 0, 'f', 2)
                            .arg(m.azimuthDegrees, 0, 'f', 1), 8000);
                    m_accNavegar->setChecked(true);
                    m_mapa->setActiveTool(MapTool::None);
                });

        connect(m_mapa, &MapWidget::errorOccurred, this,
                [this](const QString &e) {
                    statusBar()->showMessage(tr("Error: %1").arg(e), 8000);
                });

        connect(m_grupo, &QActionGroup::triggered, this, [this](QAction *a) {
            m_mapa->setActiveTool(static_cast<MapTool>(a->data().toInt()));
        });

        // --- El panel se reconstruye SOLO desde las senales del modelo. ---
        connect(m_mapa, &MapWidget::featureAdded, this, &Ventana::pedirReconstruir);
        connect(m_mapa, &MapWidget::featureRemoved, this, &Ventana::pedirReconstruir);
        connect(m_mapa, &MapWidget::featureUpdated, this, &Ventana::pedirReconstruir);
        connect(m_mapa, &MapWidget::featureLayersChanged, this, &Ventana::pedirReconstruir);

        connect(m_mapa, &MapWidget::featureSelected, this, &Ventana::seleccionEnMapa);

        connect(m_mapa, &MapWidget::featureCreated, this, [this](qint64 id) {
            m_mapa->selectFeature(id);
            const auto f = m_mapa->feature(id);
            statusBar()->showMessage(
                tr("Creada entidad %1 con %2 vertices en la capa '%3'")
                    .arg(id).arg(f ? f->geometry.size() : 0)
                    .arg(f ? f->layerId : QString()), 5000);
        });

        connect(m_mapa, &MapWidget::drawingCancelled, this,
                [this] { statusBar()->showMessage(tr("Trazado cancelado"), 3000); });

        m_coords = new QLabel(this);
        m_cota = new QLabel(this);
        m_cota->setMinimumWidth(80);
        m_cota->setToolTip(tr("Altura del terreno bajo el cursor (necesita DEM)."));
        m_info = new QLabel(this);
        statusBar()->addPermanentWidget(m_coords);
        statusBar()->addPermanentWidget(m_cota);
        statusBar()->addPermanentWidget(m_info);
    }

    // Muestra la cota del terreno bajo el cursor (o "—" si no hay DEM/dato).
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

    // Elige una CARPETA de ficheros `.hgt` como origen de elevacion (en caliente).
    void alElegirDemCarpeta()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Carpeta de ficheros de elevacion (.hgt)"));
        if (dir.isEmpty())
            return;
        m_mapa->setElevationDir(dir);
        m_demActivo = true;
        statusBar()->showMessage(tr("Elevacion (carpeta): %1").arg(dir), 4000);
    }

    // Elige una BASE DE DATOS `.sqlitedb` de elevacion como origen (en caliente).
    void alElegirDemDb()
    {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Base de datos de elevacion"), QString(),
            tr("Base de datos de elevacion (*.sqlitedb *.db);;Todos (*)"));
        if (file.isEmpty())
            return;
        m_mapa->setElevationDb(file);
        m_demActivo = true;
        statusBar()->showMessage(tr("Elevacion (BD): %1").arg(file), 4000);
    }

    // ============================================= analisis de elevacion ==
    // Pestaña lateral para PROBAR los tres calculos sobre el DEM activo: perfil de
    // una ruta, linea de vision entre dos puntos y viewshed 360. La libreria
    // devuelve los datos; aqui se dibujan (ventana flotante del perfil; linea de
    // vision y zona de visibilidad como entidades sobre el mapa).
    QWidget *construirTabElevacion()
    {
        auto *tab = new QWidget;
        auto *caja = new QVBoxLayout(tab);
        caja->setContentsMargins(8, 8, 8, 8);

        // Selector de análisis: cada uno pide SOLO sus datos (las demás filas se
        // ocultan). Así la entrada no es fija: cambia según lo que vayas a calcular.
        m_tipoAnalisis = new QComboBox(tab);
        m_tipoAnalisis->addItems(QStringList{ tr("Perfil"), tr("Visión A→B"),
                                              tr("Viewshed"), tr("10 picos altos") });
        {
            auto *fc = new QFormLayout;
            fc->addRow(tr("Análisis:"), m_tipoAnalisis);
            caja->addLayout(fc);
        }

        // Crea una fila [etiqueta + contenido] que se puede mostrar/ocultar entera.
        auto crearFila = [&](const QString &etiqueta, QWidget *contenido, QLabel **outLbl) {
            auto *lbl = new QLabel(etiqueta, tab);
            lbl->setMinimumWidth(96);
            auto *h = new QHBoxLayout; h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(lbl, 0); h->addWidget(contenido, 1);
            auto *w = new QWidget(tab); w->setLayout(h);
            if (outLbl) *outLbl = lbl;
            caja->addWidget(w);
            return w;
        };

        // Contenido de un punto: lat/lon + botón «Mapa» (pin del color del punto).
        auto contenidoPunto = [&](QDoubleSpinBox *&lat, QDoubleSpinBox *&lon, Pick cual) {
            lat = new QDoubleSpinBox(tab);
            lat->setRange(-90.0, 90.0); lat->setDecimals(5); lat->setSingleStep(0.001);
            lat->setToolTip(tr("Latitud"));
            lon = new QDoubleSpinBox(tab);
            lon->setRange(-180.0, 180.0); lon->setDecimals(5); lon->setSingleStep(0.001);
            lon->setToolTip(tr("Longitud"));
            auto *pick = new QPushButton(tr("Mapa"), tab);
            pick->setIcon(QIcon(pinPixmap(cual == Pick::A ? m_colorA : m_colorB)));
            pick->setToolTip(tr("Fija este punto con el siguiente clic en el mapa."));
            connect(pick, &QPushButton::clicked, this, [this, cual] { iniciarPick(cual); });
            connect(lat, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { actualizarInfoPuntos(); });
            connect(lon, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { actualizarInfoPuntos(); });
            auto *h = new QHBoxLayout; h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(lat, 1); h->addWidget(lon, 1); h->addWidget(pick, 0);
            auto *w = new QWidget(tab); w->setLayout(h);
            return w;
        };

        m_altA = new QDoubleSpinBox(tab); m_altA->setRange(0.0, 20000.0); m_altA->setValue(10.0); m_altA->setSuffix(tr(" m"));
        m_altB = new QDoubleSpinBox(tab); m_altB->setRange(0.0, 20000.0); m_altB->setValue(10.0); m_altB->setSuffix(tr(" m"));
        m_rumbo = new QDoubleSpinBox(tab); m_rumbo->setRange(0.0, 359.9); m_rumbo->setDecimals(1); m_rumbo->setSuffix(tr(" °")); m_rumbo->setValue(180.0); m_rumbo->setWrapping(true);
        m_alcanceKm = new QDoubleSpinBox(tab); m_alcanceKm->setRange(1.0, 400.0); m_alcanceKm->setValue(40.0); m_alcanceKm->setSuffix(tr(" km"));
        m_picosRadioKm = new QDoubleSpinBox(tab); m_picosRadioKm->setRange(1.0, 50.0); m_picosRadioKm->setValue(10.0); m_picosRadioKm->setSuffix(tr(" km"));
        m_picosSepM = new QSpinBox(tab); m_picosSepM->setRange(50, 5000); m_picosSepM->setValue(800); m_picosSepM->setSingleStep(50); m_picosSepM->setSuffix(tr(" m"));

        m_rowPosA   = crearFila(tr("Posición A:"),  contenidoPunto(m_latA, m_lonA, Pick::A), &m_lblPosA);
        m_rowPosB   = crearFila(tr("Posición B:"),  contenidoPunto(m_latB, m_lonB, Pick::B), nullptr);
        m_rowAzimut = crearFila(tr("Azimut:"),       m_rumbo,        nullptr);
        m_rowDist   = crearFila(tr("Distancia:"),    m_alcanceKm,    &m_lblDist);
        m_rowAnt1   = crearFila(tr("Antena 1:"),     m_altA,         &m_lblAnt1);
        m_rowAnt2   = crearFila(tr("Antena 2:"),     m_altB,         &m_lblAnt2);
        m_rowRadio  = crearFila(tr("Radio:"),        m_picosRadioKm, nullptr);
        m_rowSep    = crearFila(tr("Separación:"),   m_picosSepM,    nullptr);

        // Lectura: cota del terreno y, en Visión, rumbo/distancia A→B.
        m_infoPuntos = new QLabel(tab);
        m_infoPuntos->setTextFormat(Qt::RichText);
        m_infoPuntos->setWordWrap(true);
        caja->addWidget(m_infoPuntos);

        // Puntos A y B por defecto: centro del mapa y 20 km al este (así la Visión
        // A→B arranca con una línea válida sin tener que pinchar primero).
        const QGeoCoordinate c = m_mapa->center();
        if (c.isValid()) {
            m_latA->setValue(c.latitude()); m_lonA->setValue(c.longitude());
            const QGeoCoordinate b = c.atDistanceAndAzimuth(20000.0, 90.0);
            m_latB->setValue(b.latitude()); m_lonB->setValue(b.longitude());
        }
        actualizarInfoPuntos();
        connect(m_mapa, &MapWidget::pointPicked, this,
                [this](const QGeoCoordinate &p) { onPointPicked(p); });
        // El filtro va sobre la MapView (el QCustomPlot interno) para interceptar
        // el clic ANTES de que lo use para desplazar: así se arrastran los pines.
        m_plotObj = m_mapa->customPlot();
        if (m_plotObj)
            m_plotObj->installEventFilter(this);

        // Las capas de análisis y los pines A/B son TEMPORALES: viven solo
        // durante la sesión y NO se guardan con las entidades del usuario.
        for (const QString &id : {kCapaPuntos, kCapaVision, kCapaViewshed, kCapaPicos})
            m_mapa->setFeatureLayerTransient(id);

        m_marComo0 = new QCheckBox(tr("Mar / sin dato = 0 m"), tab);
        m_marComo0->setChecked(true);
        m_marComo0->setToolTip(tr("Trata los huecos del DEM (mar, fuera de cobertura) "
                                  "como cota 0, para analizar objetivos sobre el mar."));
        m_curvatura = new QCheckBox(tr("Curvatura 4/3"), tab);
        m_curvatura->setChecked(true);
        m_curvatura->setToolTip(tr("Con curvatura, el mar llano se oculta tras el "
                                   "horizonte geométrico. Desactívala para analizar "
                                   "solo el enmascaramiento por terreno (sobre mar sin "
                                   "obstáculos se ve hasta el alcance máximo)."));
        {
            auto *h = new QHBoxLayout; h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(m_marComo0); h->addWidget(m_curvatura);
            m_rowChecks = new QWidget(tab); m_rowChecks->setLayout(h);
            caja->addWidget(m_rowChecks);
        }

        // Botones: «Calcular» lanza el análisis del selector; «Limpiar» borra.
        auto *filaBtn = new QHBoxLayout;
        auto *bCalcular = new QPushButton(tr("Calcular"), tab);
        auto *bLimpiar = new QPushButton(tr("Limpiar"), tab);
        filaBtn->addWidget(bCalcular, 1); filaBtn->addWidget(bLimpiar, 0);
        caja->addLayout(filaBtn);

        connect(bCalcular, &QPushButton::clicked, this, [this] {
            switch (m_tipoAnalisis->currentIndex()) {
            case 0: analizarPerfil();     break;
            case 1: analizarVision();     break;
            case 2: analizarViewshed();   break;
            case 3: analizarPicosAltos(); break;
            }
        });
        connect(bLimpiar, &QPushButton::clicked, this, [this] {
            m_mapa->removeFeatureLayer(kCapaVision);
            m_mapa->removeFeatureLayer(kCapaViewshed);
            m_mapa->removeFeatureLayer(kCapaPicos);
            m_picoIds.clear(); m_picoBase.clear(); m_picoHover = -1;
            if (m_resultado) m_resultado->clear();
            m_ultimo = Analisis::Ninguno;
            marcarPuntosActivos();      // deja los pines de los puntos de entrada
            statusBar()->showMessage(tr("Análisis de elevación limpiado"), 3000);
        });
        // Los checks re-aplican al instante el último análisis.
        connect(m_marComo0, &QCheckBox::toggled, this, [this] { reejecutar(); });
        connect(m_curvatura, &QCheckBox::toggled, this, [this] { reejecutar(); });
        // Al cambiar de análisis, se muestran solo sus campos y se actualizan pines.
        connect(m_tipoAnalisis, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int i) { actualizarEntradaAnalisis(i); });
        actualizarEntradaAnalisis(0);   // estado inicial (Perfil)

        // --- Relieve sombreado (hillshade) en vivo, desde el DEM local ---------
        auto *grupoHs = new QGroupBox(tr("Relieve sombreado (hillshade)"), tab);
        grupoHs->setCheckable(true);
        grupoHs->setChecked(false);
        auto *fHs = new QFormLayout(grupoHs);
        auto *az = new QDoubleSpinBox(grupoHs);
        az->setRange(0.0, 359.0); az->setValue(315.0); az->setSuffix(tr(" °")); az->setWrapping(true);
        auto *alt = new QDoubleSpinBox(grupoHs);
        alt->setRange(1.0, 89.0); alt->setValue(45.0); alt->setSuffix(tr(" °"));
        auto *filaSol = new QHBoxLayout; filaSol->setContentsMargins(0, 0, 0, 0);
        filaSol->addWidget(az, 1); filaSol->addWidget(alt, 1);
        auto *wSol = new QWidget(grupoHs); wSol->setLayout(filaSol);
        fHs->addRow(tr("Sol (azim/alt):"), wSol);
        auto *inten = new QSpinBox(grupoHs);
        inten->setRange(0, 100); inten->setValue(60); inten->setSuffix(tr(" %"));
        fHs->addRow(tr("Intensidad:"), inten);
        auto *exag = new QDoubleSpinBox(grupoHs);
        exag->setRange(1.0, 6.0); exag->setValue(2.0); exag->setSingleStep(0.5); exag->setSuffix(tr("×"));
        fHs->addRow(tr("Exageración:"), exag);
        auto *color = new QCheckBox(tr("Tintar por altura"), grupoHs);
        fHs->addRow(QString(), color);
        caja->addWidget(grupoHs);

        grupoHs->setToolTip(tr("Sombreado del terreno calculado en vivo del DEM local (sin conexión). "
                               "Se recalcula al mover la vista. A vista muy general (p. ej. Cuba "
                               "entera) no se dibuja: acércate para verlo."));
        m_mapa->setHillshadeSun(az->value(), alt->value());
        m_mapa->setHillshadeOpacity(inten->value() / 100.0);
        m_mapa->setHillshadeExaggeration(exag->value());
        connect(grupoHs, &QGroupBox::toggled, this, [this](bool on) { m_mapa->setHillshadeVisible(on); });
        auto aplicarSol = [this, az, alt] { m_mapa->setHillshadeSun(az->value(), alt->value()); };
        connect(az, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [aplicarSol](double) { aplicarSol(); });
        connect(alt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [aplicarSol](double) { aplicarSol(); });
        connect(inten, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) { m_mapa->setHillshadeOpacity(v / 100.0); });
        connect(exag, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this](double v) { m_mapa->setHillshadeExaggeration(v); });
        connect(color, &QCheckBox::toggled, this, [this](bool on) { m_mapa->setHillshadeColored(on); });

        m_resultado = new QLabel(tab);
        m_resultado->setWordWrap(true);
        m_resultado->setTextFormat(Qt::RichText);
        m_resultado->setMinimumHeight(60);
        m_resultado->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        caja->addWidget(m_resultado);

        auto *ayuda = new QLabel(
            tr("<span style='color:#777'>Elige el <b>Análisis</b> arriba: solo pide sus datos. "
               "Fija las posiciones con «Mapa» (el cursor toma el pin; azul=A, rojo=B) o "
               "escríbelas. <b>Perfil</b>: posición + azimut + distancia. <b>Visión A→B</b>: "
               "posiciones 1 y 2 + antenas. <b>Viewshed</b>: observador + antena + alcance + "
               "altura objetivo. <b>10 picos</b>: posición + radio/separación. Pulsa "
               "<b>Calcular</b>. El perfil se abre en una ventana aparte (arrastrar = "
               "desplazar, rueda = zoom).</span>"), tab);
        ayuda->setWordWrap(true);
        caja->addWidget(ayuda);
        caja->addStretch(1);
        return tab;
    }

    // Re-ejecuta el último análisis (p. ej. al cambiar un check). Nada si ninguno.
    void reejecutar()
    {
        switch (m_ultimo) {
        case Analisis::Perfil:   analizarPerfil();   break;
        case Analisis::Vision:   analizarVision();   break;
        case Analisis::Viewshed: analizarViewshed(); break;
        case Analisis::Picos:    analizarPicosAltos(); break;
        case Analisis::Ninguno:  break;
        }
    }

    // Abre (o reutiliza) la ventana flotante del perfil y la actualiza. Con
    // \a conVision dibuja la línea de visión recta entre antenas (cimas absolutas
    // \a zA en A y \a zB en B, a distancia \a D) y marca el obstáculo \a critD;
    // \a maxInicial fija la ventana inicial en X.
    void mostrarPerfil(const ElevationProfile &p, const QString &resumen,
                       double maxInicial = -1.0, bool conVision = false,
                       double zA = 0.0, double zB = 0.0, double D = 0.0,
                       double critD = -1.0, bool bloqueado = false)
    {
        if (!m_perfilWin) {
            m_perfilWin = new QDialog(this);
            m_perfilWin->setWindowTitle(tr("Perfil del terreno"));
            m_perfilWin->resize(960, 500);
            auto *lay = new QVBoxLayout(m_perfilWin);
            m_perfilInfo = new QLabel(m_perfilWin);
            m_perfilInfo->setWordWrap(true);
            lay->addWidget(m_perfilInfo);
            // Gráfica con QCustomPlot: arrastre y zoom (rueda) en ambos ejes.
            m_plot = new QCustomPlot(m_perfilWin);
            m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend);
            m_plot->legend->setVisible(true);
            m_plot->xAxis->setLabel(tr("Distancia (m)"));
            m_plot->yAxis->setLabel(tr("Altura (m)"));
            lay->addWidget(m_plot, 1);
            auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, m_perfilWin);
            connect(bb, &QDialogButtonBox::rejected, m_perfilWin, &QDialog::hide);
            lay->addWidget(bb);
        }
        m_perfilInfo->setText(resumen);
        pintarPerfil(p, conVision, zA, zB, D, critD, bloqueado, maxInicial);
        m_perfilWin->show();
        m_perfilWin->raise();
        m_perfilWin->activateWindow();
    }

    // Dibuja el perfil en el QCustomPlot al estilo DVD_potencial: terreno CRUDO
    // (verde, relleno hasta la curva de curvatura), curva de curvatura de la Tierra
    // (−d²/2kR), y —si hay visión— la recta de visibilidad A→B, los mástiles de
    // antena, el obstáculo y la línea de sombra hasta el final.
    void pintarPerfil(const ElevationProfile &p, bool conVision,
                      double zA, double zB, double D, double critD, bool bloqueado,
                      double maxInicialM)
    {
        m_plot->clearGraphs();
        m_plot->clearItems();
        if (p.samples.size() < 2) { m_plot->replot(); return; }

        const bool curv = curvaturaOn();
        const double k = 4.0 / 3.0, R = 6371000.0;
        auto caida = [&](double d) { return curv ? (d * d) / (2.0 * k * R) : 0.0; };

        // Modelo al estilo DVD: el eje Y refleja la CURVATURA de la Tierra. Todo se
        // hunde con la distancia restando caida(d)=d²/2kR. Así el NIVEL DEL MAR es
        // una sola línea que baja con la distancia (y = −caida(d)); el mar NO se
        // rellena, es esa línea. El terreno sobre el mar (cota ≤ 0: mar o batimetría)
        // se trata como superficie 0 y RIELA sobre esa línea; la tierra firme (cota
        // > 0) va a su altura, también hundida. terreno dibujado = max(cota,0) −
        // caida(d); nivel del mar = −caida(d).
        auto superf = [](double e) { return std::isnan(e) ? 0.0 : qMax(e, 0.0); };
        QVector<double> dx, mar, terr, objetivo;
        double dFin = 0.0, yTop = -1e18, yBot = 1e18;
        for (const ElevationSample &s : p.samples) {
            const double d = s.distanceM;
            const double nivelMar = -caida(d);                 // mar hundido por curvatura
            const double yt = superf(s.elevation) - caida(d);  // terreno (mar = nivel del mar)
            dx << d; mar << nivelMar; terr << yt;
            if (conVision) objetivo << yt + m_altB->value();
            dFin = d;
            yTop = qMax(yTop, yt); yBot = qMin(yBot, nivelMar);
        }
        if (dx.size() < 2) { m_plot->replot(); return; }

        // Nivel del mar (una línea azul fina que baja con la curvatura). El mar es
        // SOLO esta línea: no se rellena.
        QCPGraph *gMar = m_plot->addGraph();
        gMar->setName(tr("Nivel del mar"));
        gMar->setPen(QPen(QColor(0x1e, 0x88, 0xe5), 1));
        gMar->setData(dx, mar);

        // Terreno (verde), relleno por canal hasta el nivel del mar = el cuerpo de
        // tierra sobre el agua. Donde hay mar, terreno = nivel del mar → relleno nulo.
        QCPGraph *gTerr = m_plot->addGraph();
        gTerr->setName(tr("Perfil del terreno"));
        gTerr->setPen(QPen(QColor(0x1b, 0x5e, 0x20), 1.4));
        gTerr->setBrush(QBrush(QColor(0x2e, 0x7d, 0x32, 90)));
        gTerr->setChannelFillGraph(gMar);
        gTerr->setData(dx, terr);

        // Terreno-superficie hundido a una distancia d (para mástiles y obstáculo).
        auto terrPlot = [&](double d) {
            return superf(terrEn(p, d)) - caida(d);
        };
        if (conVision && D > 0.0) {
            const double zBp = zB - caida(D);        // cima del objetivo, ya hundida
            // Recta de visibilidad A→B (naranja): recta en el plano hundido, de la
            // cima de antena en A a la del objetivo en B (que riela sobre el mar).
            QCPGraph *gVis = m_plot->addGraph();
            gVis->setName(tr("Recta de visibilidad"));
            gVis->setPen(QPen(QColor(0xef, 0x6c, 0x00), 2, Qt::DashLine));
            gVis->setData({0.0, D}, {zA, zBp});
            yTop = qMax(yTop, qMax(zA, zBp));

            // Objetivo a Alt2 sobre el terreno (ya hundido).
            QCPGraph *gObj = m_plot->addGraph();
            gObj->setName(tr("Objetivo a %1 m sobre el terreno").arg(m_altB->value(), 0, 'f', 0));
            gObj->setPen(QPen(QColor(0x15, 0x65, 0xc0), 1, Qt::DotLine));
            gObj->setData(dx, objetivo);

            // Mástiles de antena en A y B (del terreno hundido a la cima).
            mastil(0.0, terrPlot(0.0), zA);
            mastil(D, terrPlot(D), zBp);

            // ÁNGULO DE CIERRE y picos de la silueta. Desde el observador en (0, zA),
            // la tangente del ángulo de cierre a una muestra es (y − zA)/d; cada
            // muestra que supera el máximo acumulado es un PICO que eleva el horizonte.
            // El de mayor ángulo hasta B es el que DE VERDAD tapa el objetivo.
            QVector<double> picoD, picoY;
            double domD = -1.0, domY = 0.0;
            double maxTan = -std::numeric_limits<double>::infinity();
            for (int i = 0; i < dx.size(); ++i) {
                if (dx[i] <= 1.0) continue;
                const double tang = (terr[i] - zA) / dx[i];
                if (tang > maxTan) {                       // nuevo máximo → pico
                    maxTan = tang;
                    picoD << dx[i]; picoY << terr[i];
                    if (dx[i] <= D) { domD = dx[i]; domY = terr[i]; }   // dominante hasta B
                }
            }
            if (domD < 0.0 && critD >= 0.0) {              // sin pico hasta B: corte más justo
                domD = critD; domY = terrPlot(critD);
            }

            // Tracers (círculos) en los picos de la silueta.
            QCPGraph *gPicos = m_plot->addGraph();
            gPicos->setName(tr("Picos (ángulo de cierre)"));
            gPicos->setLineStyle(QCPGraph::lsNone);
            gPicos->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle,
                                                    QColor(0x37, 0x47, 0x4f), 6));
            gPicos->setData(picoD, picoY);

            // Recta tangente del observador por el PICO DOMINANTE (el que tapa),
            // EXTENDIDA hasta la distancia máxima: es la línea de cierre que limita
            // la visibilidad (lo que queda por debajo detrás del pico está oculto).
            // Rojo si bloquea, morado si no.
            if (domD > 0.0) {
                const QColor c = bloqueado ? QColor(0xc6, 0x28, 0x28)
                                           : QColor(0x6a, 0x1b, 0x9a);
                const double pend = (domY - zA) / domD;   // pendiente del ángulo de cierre
                const double yFin = zA + pend * dFin;     // extendida hasta el final
                QCPGraph *gCierre = m_plot->addGraph();
                gCierre->setName(bloqueado ? tr("Recta al pico que tapa")
                                           : tr("Recta al pico dominante"));
                gCierre->setPen(QPen(c, 1.8));
                gCierre->setData({0.0, dFin}, {zA, yFin});
                marcaObstaculo(domD, domY, c);            // tracer en el pico dominante
                yTop = qMax(yTop, qMax(domY, yFin));
            }
        }

        const double margen = qMax(10.0, (yTop - yBot) * 0.08);
        m_plot->yAxis->setRange(yBot - margen, yTop + margen);
        const double xMax = (maxInicialM > 0.0) ? qMin(maxInicialM, dFin) : dFin;
        m_plot->xAxis->setRange(0.0, xMax > 0.0 ? xMax : dFin);
        m_plot->replot();
    }

    // Cota del terreno (interpolada) a distancia d del perfil, o NaN.
    static double terrEn(const ElevationProfile &p, double d)
    {
        const auto &s = p.samples;
        for (int i = 1; i < s.size(); ++i) {
            if (s[i].distanceM >= d) {
                const double e0 = s[i - 1].elevation, e1 = s[i].elevation;
                if (std::isnan(e0) || std::isnan(e1)) return std::numeric_limits<double>::quiet_NaN();
                const double d0 = s[i - 1].distanceM, d1 = s[i].distanceM;
                const double t = (d1 > d0) ? (d - d0) / (d1 - d0) : 0.0;
                return e0 + (e1 - e0) * t;
            }
        }
        return s.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : s.last().elevation;
    }

    // Mástil de antena: línea vertical del terreno a la cima, con un punto arriba.
    void mastil(double d, double base, double cima)
    {
        if (std::isnan(base)) base = cima;
        auto *l = new QCPItemLine(m_plot);
        l->setPen(QPen(QColor(0x33, 0x33, 0x33), 2));
        l->start->setCoords(d, base);
        l->end->setCoords(d, cima);
    }

    // Marca del pico/obstáculo: punto sobre el terreno (rojo por defecto).
    void marcaObstaculo(double d, double cota, const QColor &c = QColor(0xc6, 0x28, 0x28))
    {
        if (std::isnan(cota)) return;
        auto *t = new QCPItemTracer(m_plot);
        t->setStyle(QCPItemTracer::tsCircle);
        t->setPen(QPen(c));
        t->setBrush(c);
        t->setSize(8);
        t->position->setCoords(d, cota);
    }

    // Comprueba que hay DEM; si no, avisa y devuelve false.
    bool exigirDem()
    {
        if (m_demActivo)
            return true;
        statusBar()->showMessage(
            tr("No hay elevación activa: carga un DEM con «DEM…» primero."), 5000);
        return false;
    }

    // Punto A / B leídos de los campos lat/lon de la pestaña.
    QGeoCoordinate puntoA() const { return QGeoCoordinate(m_latA->value(), m_lonA->value()); }
    QGeoCoordinate puntoB() const { return QGeoCoordinate(m_latB->value(), m_lonB->value()); }

    // Dibuja un pin (gota de mapa) del color dado. Sirve de icono del botón, de
    // cursor al capturar y de marca fija del punto sobre el mapa. La punta queda
    // abajo-centro (el «hotspot» del cursor).
    static QPixmap pinPixmap(const QColor &c)
    {
        const int w = 24, h = 32;
        QPixmap pm(w, h);
        pm.fill(Qt::transparent);
        QPainter g(&pm);
        g.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const double r = 9.0, cx = w / 2.0, cy = r + 1.0;
        path.addEllipse(QPointF(cx, cy), r, r);          // cabeza
        path.moveTo(cx - 6.0, cy + 4.0);                 // punta hacia abajo
        path.lineTo(cx, h - 1.0);
        path.lineTo(cx + 6.0, cy + 4.0);
        path.closeSubpath();
        g.setPen(QPen(Qt::white, 1.5));
        g.setBrush(c);
        g.drawPath(path.simplified());
        g.setBrush(Qt::white);                           // ojal central
        g.setPen(Qt::NoPen);
        g.drawEllipse(QPointF(cx, cy), 3.2, 3.2);
        return pm;
    }

    // Icono de un pico: disco del color dado con borde blanco y un punto central
    // blanco. \a hl (resaltado por hover) lo agranda y le añade un halo amarillo.
    static QPixmap picoIcon(const QColor &c, bool hl)
    {
        const int s = hl ? 26 : 18;
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter g(&pm);
        g.setRenderHint(QPainter::Antialiasing);
        const double cx = s / 2.0, cy = s / 2.0;
        if (hl) {                                   // halo
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(0xff, 0xee, 0x58, 180));
            g.drawEllipse(QPointF(cx, cy), s / 2.0 - 1.0, s / 2.0 - 1.0);
        }
        g.setPen(QPen(Qt::white, 2.0));
        g.setBrush(c);
        g.drawEllipse(QPointF(cx, cy), hl ? 8.0 : 7.0, hl ? 8.0 : 7.0);
        g.setPen(Qt::NoPen);
        g.setBrush(Qt::white);                      // punto central
        g.drawEllipse(QPointF(cx, cy), 2.4, 2.4);
        return pm;
    }

    // Paleta de 10 colores distintos para los picos (1..10).
    static const QVector<QColor> &paletaPicos()
    {
        static const QVector<QColor> p = {
            QColor(0xe5, 0x39, 0x35), QColor(0x8e, 0x24, 0xaa),
            QColor(0x34, 0x49, 0xab), QColor(0x03, 0x9b, 0xe5),
            QColor(0x00, 0x89, 0x7b), QColor(0x7c, 0xb3, 0x42),
            QColor(0xf9, 0xa8, 0x25), QColor(0xfb, 0x8c, 0x00),
            QColor(0x6d, 0x4c, 0x41), QColor(0x54, 0x6e, 0x7a)
        };
        return p;
    }

    // Resalta el pico cuyo marcador está bajo el cursor (y restaura el anterior).
    // \a id es lo que devuelve featureAt; si no es un pico, solo restaura.
    void resaltarPico(qint64 id)
    {
        if (!m_picoIds.contains(id)) id = -1;      // fuera de un pico
        if (id == m_picoHover) return;
        auto reponer = [this](qint64 x) {          // vuelve al icono base
            if (x >= 0 && m_picoBase.contains(x))
                m_mapa->updateFeature(m_picoBase.value(x));
        };
        reponer(m_picoHover);
        if (id >= 0 && m_picoBase.contains(id)) {  // aplica el icono resaltado
            MapFeature f = m_picoBase.value(id);
            f.style.icon = picoIcon(f.style.labelColor, /*hl=*/true);
            m_mapa->updateFeature(f);
        }
        m_picoHover = id;
    }

    // Filtro de eventos del mapa: al mover el ratón, resalta el pico que haya bajo
    // el cursor (si hay picos dibujados).
    // Texto de tooltip de una entidad: nombre, tipo y cota si los trae.
    static QString tooltipDe(const MapFeature &f)
    {
        QStringList lin;
        if (!f.name.isEmpty())
            lin << QStringLiteral("<b>") + f.name.toHtmlEscaped() + QStringLiteral("</b>");
        const QVariant cota = f.attributes.value(QStringLiteral("cota"));
        if (cota.isValid())
            lin << QString::number(qRound(cota.toDouble())) + QStringLiteral(" m");
        if (!f.type.isEmpty())
            lin << QStringLiteral("<span style='color:gray'>") + f.type.toHtmlEscaped()
                       + QStringLiteral("</span>");
        return lin.join(QStringLiteral("<br>"));
    }

    // Intercepta el ratón sobre la MapView: tooltip de la entidad bajo el cursor
    // (TODA entidad, no solo curvas), resalte de picos por hover, y ARRASTRE de
    // los pines A/B (su punta sigue al cursor sobre el terreno).
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj != m_plotObj)
            return QMainWindow::eventFilter(obj, ev);

        const QEvent::Type t = ev->type();

        // --- arrastre de un pin A/B ---
        if (t == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(ev);
            if (me->button() == Qt::LeftButton && m_picking == Pick::Ninguno) {
                const qint64 id = m_mapa->featureAt(me->pos(), 8.0);
                if (id >= 0) {
                    if (const auto f = m_mapa->feature(id);
                        f && f->type == QStringLiteral("punto_ab")) {
                        m_dragPick = (f->name == tr("B")) ? Pick::B : Pick::A;
                        return true;            // no desplazar el mapa: arrastramos
                    }
                }
            }
        } else if (t == QEvent::MouseMove && m_dragPick != Pick::Ninguno) {
            auto *me = static_cast<QMouseEvent *>(ev);
            moverPinA(m_dragPick, me->pos());
            return true;
        } else if (t == QEvent::MouseButtonRelease && m_dragPick != Pick::Ninguno) {
            m_dragPick = Pick::Ninguno;
            reejecutar();                        // recalcula el análisis con el punto movido
            return true;
        }

        // --- hover: resalte de picos + tooltip de cualquier entidad ---
        if (t == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(ev);
            const qint64 id = m_mapa->featureAt(me->pos(), 8.0);
            if (!m_picoIds.isEmpty())
                resaltarPico(id);
            QString tip;
            if (id >= 0)
                if (const auto f = m_mapa->feature(id))
                    tip = tooltipDe(*f);
            if (!tip.isEmpty())
                QToolTip::showText(me->globalPosition().toPoint(), tip, m_mapa);
            else
                QToolTip::hideText();
        }
        return QMainWindow::eventFilter(obj, ev);
    }

    // Mueve el pin A o B a la coordenada bajo el píxel dado (arrastre): vuelca la
    // posición en sus campos lat/lon, que redibujan el pin y la lectura al vuelo.
    void moverPinA(Pick cual, const QPoint &pixel)
    {
        auto *v = m_mapa ? qobject_cast<QCustomPlot *>(m_mapa->customPlot()) : nullptr;
        if (!v) return;
        const QPointF axis(v->xAxis->pixelToCoord(pixel.x()),
                           v->yAxis->pixelToCoord(pixel.y()));
        const QGeoCoordinate g = m_mapa->fromAxisCoords(axis);
        if (!g.isValid()) return;
        QDoubleSpinBox *lat = (cual == Pick::A) ? m_latA : m_latB;
        QDoubleSpinBox *lon = (cual == Pick::A) ? m_lonA : m_lonB;
        lat->setValue(g.latitude());
        lon->setValue(g.longitude());         // dispara actualizarInfoPuntos()->marcarPuntosActivos()
    }

    // Arranca la captura de un punto (A o B) con el siguiente clic en el mapa.
    // Cambia el cursor del mapa al pin del color del punto (azul A, rojo B).
    void iniciarPick(Pick cual)
    {
        m_picking = cual;
        m_mapa->setActiveTool(MapTool::PickPoint);
        const QPixmap pm = pinPixmap(cual == Pick::A ? m_colorA : m_colorB);
        m_mapa->setCursor(QCursor(pm, pm.width() / 2, pm.height() - 1));  // punta = hotspot
        statusBar()->showMessage(
            tr("Pincha en el mapa para fijar el punto %1…")
                .arg(cual == Pick::A ? tr("A") : tr("B")), 8000);
    }

    // Recibe el clic capturado: vuelca la coordenada en los campos del punto en
    // curso, restaura el cursor y re-ejecuta el último análisis para verlo al vuelo.
    void onPointPicked(const QGeoCoordinate &p)
    {
        if (m_picking == Pick::Ninguno) return;
        QDoubleSpinBox *lat = (m_picking == Pick::A) ? m_latA : m_latB;
        QDoubleSpinBox *lon = (m_picking == Pick::A) ? m_lonA : m_lonB;
        lat->setValue(p.latitude());
        lon->setValue(p.longitude());
        const Pick cual = m_picking;
        m_picking = Pick::Ninguno;
        m_mapa->setActiveTool(MapTool::None);
        m_mapa->unsetCursor();
        statusBar()->showMessage(
            tr("Punto %1 fijado en %2, %3")
                .arg(cual == Pick::A ? tr("A") : tr("B"))
                .arg(p.latitude(), 0, 'f', 5).arg(p.longitude(), 0, 'f', 5), 4000);
        reejecutar();     // refresca el análisis con el nuevo punto
    }

    // Actualiza la lectura de los puntos: cota del terreno en A y B (bajo cada
    // punto seleccionado), y rumbo y distancia de A a B. Se llama al cambiar
    // cualquier campo lat/lon.
    void actualizarInfoPuntos()
    {
        if (!m_infoPuntos) return;
        auto cota = [this](const QGeoCoordinate &q) {
            const double e = m_mapa ? m_mapa->elevationAt(q) : std::numeric_limits<double>::quiet_NaN();
            return std::isnan(e) ? QStringLiteral("—") : QString::number(e, 'f', 0) + tr(" m");
        };
        const bool vision = m_tipoAnalisis && m_tipoAnalisis->currentIndex() == 1;
        const QGeoCoordinate a = puntoA();
        QString t = tr("<span style='color:%1'>●</span> Cota: <b>%2</b>")
                        .arg(m_colorA.name(), cota(a));
        if (vision) {                       // en Visión, también B y el rumbo/distancia
            const QGeoCoordinate b = puntoB();
            const double d = a.isValid() && b.isValid() ? a.distanceTo(b) : 0.0;
            const double az = a.isValid() && b.isValid() ? a.azimuthTo(b) : 0.0;
            t += tr(" &nbsp; <span style='color:%1'>●</span> Cota B: <b>%2</b><br>"
                    "A→B: <b>%3 km</b> · rumbo <b>%4°</b>")
                     .arg(m_colorB.name(), cota(b))
                     .arg(d / 1000.0, 0, 'f', 2).arg(az, 0, 'f', 0);
        }
        m_infoPuntos->setText(t);
        marcarPuntosActivos();   // los pines siguen a los puntos (al pinchar o escribir)
    }

    // Muestra/oculta los campos de entrada según el análisis elegido y relabela los
    // comunes (un observador/posición se llama distinto en cada caso).
    void actualizarEntradaAnalisis(int idx)
    {
        const bool perfil = idx == 0, vision = idx == 1, viewshed = idx == 2, picos = idx == 3;
        if (m_rowPosA) m_rowPosA->setVisible(true);
        if (m_rowPosB) m_rowPosB->setVisible(vision);
        if (m_rowAzimut) m_rowAzimut->setVisible(perfil);
        if (m_rowDist) m_rowDist->setVisible(perfil || viewshed);
        if (m_rowAnt1) m_rowAnt1->setVisible(vision || viewshed);
        if (m_rowAnt2) m_rowAnt2->setVisible(vision || viewshed);
        if (m_rowRadio) m_rowRadio->setVisible(picos);
        if (m_rowSep) m_rowSep->setVisible(picos);
        if (m_rowChecks) m_rowChecks->setVisible(!picos);
        if (m_lblPosA) m_lblPosA->setText(vision ? tr("Posición 1 (A):")
                                        : viewshed ? tr("Observador:")
                                        : picos ? tr("Posición:")
                                                : tr("Posición A:"));
        if (m_lblDist) m_lblDist->setText(viewshed ? tr("Alcance:") : tr("Distancia:"));
        if (m_lblAnt1) m_lblAnt1->setText(viewshed ? tr("Antena obs.:") : tr("Antena 1 (Alt1):"));
        if (m_lblAnt2) m_lblAnt2->setText(viewshed ? tr("Alt. objetivo:") : tr("Antena 2 (Alt2):"));
        actualizarInfoPuntos();   // redibuja los pines activos y la lectura
    }

    // Dibuja el/los pin(es) del ANÁLISIS ACTIVO: siempre la posición A (azul) y, solo
    // en Visión A→B, también B (rojo). Así el punto seleccionado se ve sin analizar,
    // pero no sobran pines (B no aparece en perfil/viewshed/picos).
    void marcarPuntosActivos()
    {
        if (!m_mapa) return;
        prepararCapa(kCapaPuntos, tr("Puntos"), 60);
        auto pin = [this](const QGeoCoordinate &p, const QColor &c, const QString &txt) {
            if (!p.isValid()) return;
            MapFeature f;
            f.layerId = kCapaPuntos;
            f.kind = GeometryKind::Point;
            f.type = QStringLiteral("punto_ab");
            f.name = txt;
            f.geometry = { p };
            f.style.icon = pinPixmap(c);
            f.style.iconAnchor = QPointF(0.5, 31.0 / 32.0);  // la PUNTA marca el punto
            f.style.labelVisible = true;
            f.style.labelColor = c;
            f.selectable = true;        // arrastrable (ver eventFilter de arrastre)
            m_mapa->addFeature(f);
        };
        pin(puntoA(), m_colorA, tr("A"));
        if (m_tipoAnalisis && m_tipoAnalisis->currentIndex() == 1)   // solo Visión usa B
            pin(puntoB(), m_colorB, tr("B"));
    }

    // (Re)crea vacía una capa donde volcar un resultado de análisis.
    void prepararCapa(const QString &id, const QString &nombre, int z)
    {
        if (!m_mapa->addFeatureLayer(id, nombre, z))
            m_mapa->clearFeatureLayer(id);
    }

    // Parámetros de elevación con la opción «mar/sin dato = 0 m» de la pestaña.
    ElevationProfileParams paramsPerfil() const
    {
        ElevationProfileParams pp;
        if (m_marComo0 && m_marComo0->isChecked()) pp.voidElevation = 0.0;
        return pp;
    }
    double voidElev() const
    {
        return (m_marComo0 && m_marComo0->isChecked())
                   ? 0.0 : std::numeric_limits<double>::quiet_NaN();
    }
    bool curvaturaOn() const { return !m_curvatura || m_curvatura->isChecked(); }

    // Perfil del terreno a lo largo de la ruta seleccionada -> ventana flotante.
    void analizarPerfil()
    {
        if (!exigirDem())
            return;
        // Perfil RADIAL desde A por el rumbo indicado, hasta el alcance.
        const QGeoCoordinate a = puntoA();
        if (!a.isValid()) {
            statusBar()->showMessage(tr("Punto A no válido (fíjalo en el mapa o escríbelo)."), 5000);
            return;
        }
        marcarPuntosActivos();              // pin azul fijo en A
        const double alcanceM = m_alcanceKm->value() * 1000.0;
        const QGeoCoordinate fin = a.atDistanceAndAzimuth(alcanceM, m_rumbo->value());
        // El perfil se DIBUJA con la cota cruda (muestra la batimetría en azul); el
        // check «Mar = 0» solo afecta al ANÁLISIS de visibilidad, no a la gráfica.
        const ElevationProfile p = m_mapa->elevationProfile({ a, fin }, ElevationProfileParams());
        if (!p.isValid() || std::isnan(p.maxElevation)) {
            statusBar()->showMessage(
                tr("El perfil no tiene cota en el DEM activo (fuera de cobertura)."),
                5000);
            return;
        }
        const QString resumen =
            tr("<b>Perfil</b> · rumbo %1° · %2 km · mín %3 m · máx %4 m · subida +%5 m · bajada −%6 m")
                .arg(m_rumbo->value(), 0, 'f', 0)
                .arg(p.totalDistanceM / 1000.0, 0, 'f', 2)
                .arg(p.minElevation, 0, 'f', 0).arg(p.maxElevation, 0, 'f', 0)
                .arg(p.gain, 0, 'f', 0).arg(p.loss, 0, 'f', 0);
        if (m_resultado) m_resultado->setText(resumen);
        m_ultimo = Analisis::Perfil;
        mostrarPerfil(p, resumen);           // perfil radial: sin visión ni techo 5000
    }

    // Línea de visión de A (Alt1) a B (Alt2), ambos de los campos de la pestaña.
    void analizarVision()
    {
        if (!exigirDem())
            return;
        const QGeoCoordinate a = puntoA();
        const QGeoCoordinate b = puntoB();
        if (!a.isValid() || !b.isValid() || a == b) {
            statusBar()->showMessage(
                tr("Fija los puntos A y B (distintos) en el mapa o escríbelos."), 5000);
            return;
        }
        marcarPuntosActivos();               // pines fijos azul (A) y rojo (B)
        LineOfSightParams lp;
        lp.voidElevation = voidElev();
        lp.curvature = curvaturaOn();
        const LineOfSightResult v =
            m_mapa->lineOfSight(a, b, m_altA->value(), m_altB->value(), lp);
        if (!v.isValid()) {
            statusBar()->showMessage(
                tr("No se pudo calcular la visión (falta cota en A o B)."), 5000);
            return;
        }

        prepararCapa(kCapaVision, tr("Análisis: visión"), 50);
        const QColor azul(0x15, 0x65, 0xc0);        // VISIBLE (llega la vista)
        const QColor rojo(0xc6, 0x28, 0x28);        // OCULTO (tras el obstáculo)

        if (v.clear || !v.blockPosition.isValid()) {
            // Visión directa: toda la línea en azul.
            MapFeature linea;
            linea.layerId = kCapaVision;
            linea.kind = GeometryKind::Polyline;
            linea.type = QStringLiteral("linea_vision");
            linea.name = tr("Visión directa");
            linea.geometry = { a, b };
            linea.style.lineColor = azul;
            linea.style.lineWidth = 3.0;
            linea.selectable = false;
            m_mapa->addFeature(linea);
        } else {
            // Bloqueada: tramo VISIBLE (azul) hasta el obstáculo y tramo OCULTO
            // (rojo, discontinuo) por detrás, para que se vea dónde corta la vista.
            MapFeature visible;
            visible.layerId = kCapaVision;
            visible.kind = GeometryKind::Polyline;
            visible.type = QStringLiteral("vision_visible");
            visible.name = tr("Visible");
            visible.geometry = { a, v.blockPosition };
            visible.style.lineColor = azul;
            visible.style.lineWidth = 3.0;
            visible.style.labelVisible = false;
            visible.selectable = false;
            m_mapa->addFeature(visible);

            MapFeature oculto;
            oculto.layerId = kCapaVision;
            oculto.kind = GeometryKind::Polyline;
            oculto.type = QStringLiteral("vision_oculta");
            oculto.name = tr("Oculto");
            oculto.geometry = { v.blockPosition, b };
            oculto.style.lineColor = rojo;
            oculto.style.lineWidth = 2.0;
            oculto.style.lineStyle = Qt::DashLine;
            oculto.style.labelVisible = false;
            oculto.selectable = false;
            m_mapa->addFeature(oculto);

            MapFeature corte;
            corte.layerId = kCapaVision;
            corte.kind = GeometryKind::Point;
            corte.type = QStringLiteral("obstaculo");
            corte.name = tr("Obstáculo a %1 km (falta %2 m)")
                             .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                             .arg(-v.clearanceM, 0, 'f', 0);
            corte.geometry = { v.blockPosition };
            corte.style.lineColor = rojo;
            corte.style.pointRadiusPx = 7.0;
            corte.selectable = false;
            m_mapa->addFeature(corte);
        }

        const QString resumen =
            v.clear
                ? tr("<b>Visión DIRECTA</b> · %1 km · holgura mínima %2 m · paso más "
                     "justo a %3 km. <span style='color:#777'>Naranja = línea de visión "
                     "(Alt1 %4 m en A, Alt2 %5 m en B).</span>")
                      .arg(v.totalDistanceM / 1000.0, 0, 'f', 2)
                      .arg(v.clearanceM, 0, 'f', 0)
                      .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                      .arg(m_altA->value(), 0, 'f', 0).arg(m_altB->value(), 0, 'f', 0)
                : tr("<b>BLOQUEADA</b> · obstáculo a %1 km de %2 km · faltan %3 m de "
                     "altura. <span style='color:#777'>Azul = visible, rojo = oculto; "
                     "naranja = línea de visión.</span>")
                      .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                      .arg(v.totalDistanceM / 1000.0, 0, 'f', 2)
                      .arg(-v.clearanceM, 0, 'f', 0);
        if (m_resultado) m_resultado->setText(resumen);
        statusBar()->showMessage(QString(resumen).remove(QRegularExpression(
                                     QStringLiteral("<[^>]*>"))), 9000);

        // Perfil con la LÍNEA DE VISIÓN y el obstáculo, en la ventana flotante. El
        // perfil se extiende hasta ALCANCE + 5 km por el rumbo A→B (para ver todo el
        // terreno y poder arrastrar); la vista inicial se centra en [0, alcance].
        const double D = v.totalDistanceM;                  // distancia A→B
        const double alcanceM = m_alcanceKm->value() * 1000.0;
        const double largo = qMax(alcanceM + 5000.0, D + 5000.0);
        const QGeoCoordinate fin = a.atDistanceAndAzimuth(largo, a.azimuthTo(b));
        // Perfil CRUDO para dibujar (batimetría en azul). La visibilidad (v, arriba)
        // ya se calculó con «Mar = 0» clampeando el fondo marino a la superficie.
        const ElevationProfile perfil = m_mapa->elevationProfile({ a, fin }, ElevationProfileParams());
        if (!perfil.isValid() || std::isnan(perfil.maxElevation))
            return;

        // Terreno (ya con «mar = 0» si procede) en A (primera muestra) y en B.
        const double tA = perfil.samples.first().elevation;
        double tB = std::numeric_limits<double>::quiet_NaN();
        for (int i = 1; i < perfil.samples.size(); ++i) {
            if (perfil.samples[i].distanceM >= D) {
                const ElevationSample &s0 = perfil.samples[i - 1];
                const ElevationSample &s1 = perfil.samples[i];
                if (!std::isnan(s0.elevation) && !std::isnan(s1.elevation)) {
                    const double t = (s1.distanceM > s0.distanceM)
                        ? (D - s0.distanceM) / (s1.distanceM - s0.distanceM) : 0.0;
                    tB = s0.elevation + (s1.elevation - s0.elevation) * t;
                }
                break;
            }
        }
        m_ultimo = Analisis::Vision;
        // La recta de visibilidad busca el objetivo SOBRE EL MAR: si un extremo no
        // tiene dato (NaN) o cae bajo el nivel del mar (mar o batimetría negativa),
        // la base de la antena se asienta en la SUPERFICIE (0), no en el fondo
        // marino. Los objetivos están por encima del nivel del mar (p. ej. buques),
        // así que su mástil arranca en 0, no a −profundidad.
        const double baseA = std::isnan(tA) ? 0.0 : qMax(tA, 0.0);
        const double baseB = std::isnan(tB) ? 0.0 : qMax(tB, 0.0);
        if (D > 0.0) {
            const double zA = baseA + m_altA->value();   // cima de antena en A (abs)
            const double zB = baseB + m_altB->value();   // cima de antena en B (abs)
            mostrarPerfil(perfil, resumen, alcanceM, true, zA, zB, D,
                          v.blockDistanceM, !v.clear);
        } else {
            mostrarPerfil(perfil, resumen, alcanceM);
        }
    }

    // Viewshed 360° desde el vértice de la entidad seleccionada (o el centro del
    // mapa): dibuja el polígono de la zona visible para un objetivo a Alt2.
    void analizarViewshed()
    {
        if (!exigirDem())
            return;
        const QGeoCoordinate origen = puntoA();
        if (!origen.isValid()) {
            statusBar()->showMessage(tr("Punto (A) no válido (fíjalo en el mapa o escríbelo)."), 5000);
            return;
        }
        marcarPuntosActivos();              // pin azul fijo en A (observador)

        ViewshedParams vp;
        vp.observerHeight = m_altA->value();
        vp.targetHeight = m_altB->value();
        vp.maxRangeM = m_alcanceKm->value() * 1000.0;
        vp.voidElevation = voidElev();      // mar / sin dato = 0 (objetivos en el mar)
        vp.curvature = curvaturaOn();       // sin curvatura: mar sin obstáculos = alcance

        // Barra de progreso cancelable: el viewshed de 360° a largo alcance (hasta
        // 400 km) puede tardar. Solo aparece si pasa de ~0,4 s (alcances cortos no
        // la ven). El callback corre en este mismo hilo, así que procesa eventos
        // para repintar y atender «Cancelar».
        QProgressDialog prog(tr("Calculando viewshed (%1 km)…")
                                 .arg(m_alcanceKm->value(), 0, 'f', 0),
                             tr("Cancelar"), 0, 360, this);
        prog.setWindowModality(Qt::WindowModal);
        prog.setMinimumDuration(400);
        auto progreso = [&prog](int done, int total) -> bool {
            prog.setMaximum(total);
            prog.setValue(done);
            QApplication::processEvents();
            return !prog.wasCanceled();
        };

        QElapsedTimer reloj;
        reloj.start();
        const Viewshed vs = m_mapa->viewshed(origen, vp, progreso);
        const qint64 ms = reloj.elapsed();
        const bool cancelado = prog.wasCanceled();
        prog.reset();
        if (!vs.isValid()) {
            statusBar()->showMessage(
                cancelado ? tr("Viewshed cancelado.")
                          : tr("No hay cota en el origen del viewshed (fuera de cobertura)."),
                5000);
            return;
        }
        m_ultimo = Analisis::Viewshed;

        // Zona de visibilidad REAL: una cuña por cada TRAMO de cada rayo, como
        // partes de UNA entidad por color. AZUL = visible, AMARILLO = oculto; lo
        // que no tiene dato (y el check de mar está apagado) se deja sin pintar.
        // Coincide, azimut a azimut, con la Visión A→B.
        const double half = (vp.azimuthStepDeg > 0.0 ? vp.azimuthStepDeg : 1.0) / 2.0;
        auto cunasDe = [&](const QVector<VisibleRange> &rs, double az) {
            QVector<QVector<QGeoCoordinate>> out;
            for (const VisibleRange &vr : rs) {
                if (vr.endM - vr.startM < 1.0) continue;
                const double d0 = qMax(vr.startM, 1.0);
                out.append({
                    origen.atDistanceAndAzimuth(d0,      az - half),
                    origen.atDistanceAndAzimuth(vr.endM, az - half),
                    origen.atDistanceAndAzimuth(vr.endM, az + half),
                    origen.atDistanceAndAzimuth(d0,      az + half)
                });
            }
            return out;
        };
        QVector<QVector<QGeoCoordinate>> cunasVis, cunasOcu;
        double maxKm = 0.0;
        for (const ViewshedRay &r : vs.rays) {
            cunasVis += cunasDe(r.visibleRanges, r.azimuthDeg);
            cunasOcu += cunasDe(r.hiddenRanges, r.azimuthDeg);
            for (const VisibleRange &vr : r.visibleRanges)
                maxKm = qMax(maxKm, vr.endM / 1000.0);
        }

        prepararCapa(kCapaViewshed, tr("Análisis: viewshed"), 40);
        auto pintarZona = [&](const QVector<QVector<QGeoCoordinate>> &cunas,
                              const QString &tipo, const QString &nombre,
                              const QColor &fill) {
            if (cunas.isEmpty()) return;
            MapFeature zona;
            zona.layerId = kCapaViewshed;
            zona.kind = GeometryKind::Polygon;
            zona.type = tipo;
            zona.name = nombre;
            zona.parts = cunas;
            zona.geometry = cunas.first();
            zona.style.lineColor = QColor(0, 0, 0, 0);   // sin borde por cuña
            zona.style.fillColor = fill;
            zona.style.labelVisible = false;
            zona.selectable = false;
            m_mapa->addFeature(zona);
        };
        // Oculto primero (debajo), visible encima.
        pintarZona(cunasOcu, QStringLiteral("zona_oculta"), tr("Oculto"),
                   QColor(0xff, 0xc1, 0x07, 90));                 // amarillo
        pintarZona(cunasVis, QStringLiteral("zona_visibilidad"),
                   tr("Visible a %1 m").arg(vp.targetHeight, 0, 'f', 0),
                   QColor(0x42, 0xa5, 0xf5, 80));                 // azul

        MapFeature centro;
        centro.layerId = kCapaViewshed;
        centro.kind = GeometryKind::Point;
        centro.type = QStringLiteral("observador");
        centro.name = tr("Observador");
        centro.geometry = { origen };
        centro.style.lineColor = QColor(0x0d, 0x47, 0xa1);
        centro.selectable = false;
        m_mapa->addFeature(centro);

        const QString resumen =
            tr("<b>Viewshed</b> · %1 rayos · obs %2 m, obj %3 m · alcance máx visible "
               "%4 km · %5 ms. <span style='color:#777'>Azul = visible, amarillo = "
               "oculto.</span>")
                .arg(vs.rays.size())
                .arg(vp.observerHeight, 0, 'f', 0).arg(vp.targetHeight, 0, 'f', 0)
                .arg(maxKm, 0, 'f', 1).arg(ms);
        if (m_resultado) m_resultado->setText(resumen);
        statusBar()->showMessage(QString(resumen).remove(QRegularExpression(
                                     QStringLiteral("<[^>]*>"))), 9000);
    }

    // Marca los 10 puntos MÁS ALTOS del terreno en un radio de 10 km desde A.
    // Muestrea una rejilla (paso 100 m) dentro del círculo, ordena por cota y toma
    // los 10 más altos exigiendo una separación mínima (para que sean cumbres
    // distintas y no 10 muestras de la misma loma). Los pinta numerados sobre el
    // mapa y los lista (cota · distancia · rumbo) en el panel.
    void analizarPicosAltos()
    {
        if (!exigirDem())
            return;
        const QGeoCoordinate a = puntoA();
        if (!a.isValid()) {
            statusBar()->showMessage(tr("Punto A no válido (fíjalo en el mapa o escríbelo)."), 5000);
            return;
        }
        m_ultimo = Analisis::Picos;
        marcarPuntosActivos();              // pin azul fijo en A (centro)
        m_picoIds.clear(); m_picoBase.clear(); m_picoHover = -1;

        const double R = m_picosRadioKm->value() * 1000.0;
        const double sepMin = double(m_picosSepM->value());
        // La rejilla LOCALIZA cumbres; su paso se escala con el radio para acotar el
        // coste (~500×500 muestras). La EXACTITUD la da luego el refinamiento fino.
        const double paso = qMax(30.0, (2.0 * R) / 500.0);
        const double radioKm = m_picosRadioKm->value();

        struct Pico { QGeoCoordinate p; double ele; double dist; double az; };
        auto cotaEn = [&](const QGeoCoordinate &q) { return m_mapa->elevationAt(q); };

        // Barra de progreso cancelable (radios grandes tardan).
        const int filas = int((2.0 * R) / paso) + 1;
        QProgressDialog prog(tr("Buscando cumbres en %1 km…").arg(radioKm, 0, 'f', 0),
                             tr("Cancelar"), 0, filas, this);
        prog.setWindowModality(Qt::WindowModal);
        prog.setMinimumDuration(400);

        QVector<Pico> cand;
        cand.reserve(qsizetype(filas) * qsizetype(filas) / 2);
        int fila = 0;
        for (double dy = -R; dy <= R; dy += paso, ++fila) {
            for (double dx = -R; dx <= R; dx += paso) {
                const double dd = std::hypot(dx, dy);
                if (dd > R) continue;
                const double az = (dd < 1.0) ? 0.0
                    : std::fmod(qRadiansToDegrees(std::atan2(dx, dy)) + 360.0, 360.0);
                const QGeoCoordinate q = a.atDistanceAndAzimuth(dd, az);
                const double e = cotaEn(q);
                if (std::isnan(e)) continue;
                cand.push_back({ q, e, dd, az });
            }
            prog.setValue(fila);
            QApplication::processEvents();
            if (prog.wasCanceled()) { statusBar()->showMessage(tr("Búsqueda de picos cancelada."), 4000); return; }
        }
        prog.reset();
        if (cand.isEmpty()) {
            statusBar()->showMessage(tr("No hay cota en %1 km alrededor de A (fuera de cobertura).").arg(radioKm,0,'f',0), 5000);
            return;
        }
        std::sort(cand.begin(), cand.end(),
                  [](const Pico &x, const Pico &y) { return x.ele > y.ele; });

        // Top-10 con separación mínima sobre la rejilla gruesa.
        QVector<Pico> top;
        for (const Pico &c : cand) {
            bool lejos = true;
            for (const Pico &t : top)
                if (c.p.distanceTo(t.p) < sepMin) { lejos = false; break; }
            if (lejos) { top.push_back(c); if (top.size() >= 10) break; }
        }

        // Refinamiento fino: alrededor de cada cumbre, ventana ±150 m a paso 10 m
        // para clavar la cota exacta (la rejilla gruesa se salta el nodo del pico).
        auto refinar = [&](Pico t) {
            const double win = qMax(150.0, paso), st = 10.0;   // cubre ≥ una celda
            Pico best = t;
            for (double dy = -win; dy <= win; dy += st)
                for (double dx = -win; dx <= win; dx += st) {
                    const double dd = std::hypot(dx, dy);
                    if (dd > win) continue;
                    const double az = (dd < 1.0) ? 0.0
                        : std::fmod(qRadiansToDegrees(std::atan2(dx, dy)) + 360.0, 360.0);
                    const QGeoCoordinate q = t.p.atDistanceAndAzimuth(dd, az);
                    const double e = cotaEn(q);
                    if (!std::isnan(e) && e > best.ele) {
                        best.ele = e; best.p = q;
                        best.dist = a.distanceTo(q); best.az = a.azimuthTo(q);
                    }
                }
            return best;
        };
        for (Pico &t : top) t = refinar(t);
        std::sort(top.begin(), top.end(),
                  [](const Pico &x, const Pico &y) { return x.ele > y.ele; });

        // Dibuja los picos numerados, cada uno de un color distinto (disco con
        // punto central), y guarda su estilo base para el resaltado por hover.
        prepararCapa(kCapaPicos, tr("Picos más altos"), 55);
        QString lista = tr("<b>%1 puntos más altos (radio %2 km)</b><br>")
                            .arg(top.size()).arg(radioKm, 0, 'f', 0);
        for (int i = 0; i < top.size(); ++i) {
            const Pico &t = top[i];
            const QColor c = paletaPicos().at(i % paletaPicos().size());
            MapFeature f;
            f.layerId = kCapaPicos;
            f.kind = GeometryKind::Point;
            f.type = QStringLiteral("pico_alto");
            f.name = QString::number(i + 1);
            f.geometry = { t.p };
            f.style.icon = picoIcon(c, /*hl=*/false);
            f.style.labelColor = c;             // (reutilizado por resaltarPico)
            f.selectable = false;
            const qint64 id = m_mapa->addFeature(f);
            if (id >= 0) {
                m_picoIds.push_back(id);
                if (auto fg = m_mapa->feature(id)) m_picoBase.insert(id, *fg);
            }
            lista += tr("<span style='color:%1'>%2.</span> <b>%3 m</b> · %4 km · rumbo %5°<br>")
                         .arg(c.name()).arg(i + 1).arg(t.ele, 0, 'f', 0)
                         .arg(t.dist / 1000.0, 0, 'f', 2).arg(t.az, 0, 'f', 0);
        }
        if (m_resultado) m_resultado->setText(lista);
        statusBar()->showMessage(
            tr("%1 picos en %2 km; el más alto a %3 m.")
                .arg(top.size()).arg(radioKm, 0, 'f', 0)
                .arg(top.isEmpty() ? 0.0 : top.first().ele, 0, 'f', 0), 8000);
    }

    // ==================================================== arbol ===========
    void pedirReconstruir() { m_reconstruir->start(); }

    void reconstruirPanel()
    {
        if (!m_arbol)
            return;

        const qint64 sel = m_mapa->selectedFeature();
        const QString activa = m_mapa->activeFeatureLayer();

        QSignalBlocker sinSenales(m_arbol);
        m_arbol->clear();

        // featureLayers() viene por zOrder ascendente; se invierte para que
        // arriba en el panel sea arriba en el mapa.
        auto capas = m_mapa->featureLayers();
        std::reverse(capas.begin(), capas.end());

        for (const LayerInfo &c : capas) {
            auto *nodo = new QTreeWidgetItem(m_arbol);
            nodo->setText(0, QStringLiteral("%1  (%2)")
                .arg(c.displayName.isEmpty() ? c.id : c.displayName)
                .arg(c.featureCount));
            nodo->setText(1, QStringLiteral("z=%1").arg(c.zOrder));
            nodo->setData(0, RolCapa, c.id);
            nodo->setFlags(nodo->flags() | Qt::ItemIsUserCheckable);
            nodo->setCheckState(0, c.visible ? Qt::Checked : Qt::Unchecked);

            QFont f = nodo->font(0);
            f.setBold(c.id == activa);
            nodo->setFont(0, f);

            for (const MapFeature &ent : m_mapa->featuresInLayer(c.id)) {
                auto *hoja = new QTreeWidgetItem(nodo);
                hoja->setText(0, ent.name.isEmpty()
                    ? tr("(sin nombre) #%1").arg(ent.id) : ent.name);
                hoja->setText(1, ent.type);
                hoja->setText(2, QString::number(ent.geometry.size()));
                hoja->setData(0, RolCapa, c.id);
                hoja->setData(0, RolEntidad, ent.id);

                QPixmap muestra(12, 12);
                muestra.fill(ent.style.lineColor);
                hoja->setIcon(0, QIcon(muestra));

                if (ent.id == sel) {
                    hoja->setSelected(true);
                    m_arbol->setCurrentItem(hoja);
                }
            }
            nodo->setExpanded(true);
        }

        actualizarEstado();
    }

    // Item marcado en el arbol, o nullptr.
    QTreeWidgetItem *itemActual() const { return m_arbol->currentItem(); }

    void seleccionEnArbol(QTreeWidgetItem *item)
    {
        if (!item)
            return;
        const QString capa = item->data(0, RolCapa).toString();
        if (!capa.isEmpty())
            m_mapa->setActiveFeatureLayer(capa);

        const qint64 id = item->data(0, RolEntidad).toLongLong();
        if (id > 0) {
            if (m_mapa->selectedFeature() != id)
                m_mapa->selectFeature(id);
        } else if (m_mapa->selectedFeature() != -1) {
            m_mapa->clearSelection();
        }
        resaltarCapaActiva();
    }

    // Llega desde el mapa: reflejar la seleccion en el arbol y en el panel.
    void seleccionEnMapa(qint64 id)
    {
        mostrarPropiedades(id);
        actualizarEstado();
        if (!m_arbol)
            return;

        QSignalBlocker sinSenales(m_arbol);
        if (id < 0) {
            m_arbol->setCurrentItem(nullptr);
            return;
        }
        for (int i = 0; i < m_arbol->topLevelItemCount(); ++i) {
            QTreeWidgetItem *capa = m_arbol->topLevelItem(i);
            for (int j = 0; j < capa->childCount(); ++j) {
                QTreeWidgetItem *hoja = capa->child(j);
                if (hoja->data(0, RolEntidad).toLongLong() == id) {
                    m_arbol->setCurrentItem(hoja);
                    m_arbol->scrollToItem(hoja);
                    return;
                }
            }
        }
    }

    void visibilidadCambiada(QTreeWidgetItem *item, int col)
    {
        if (col != 0)
            return;
        const QString capa = item->data(0, RolCapa).toString();
        if (!capa.isEmpty() && item->data(0, RolEntidad).toLongLong() == 0)
            m_mapa->setFeatureLayerVisible(capa, item->checkState(0) == Qt::Checked);
    }

    void resaltarCapaActiva()
    {
        const QString activa = m_mapa->activeFeatureLayer();
        for (int i = 0; i < m_arbol->topLevelItemCount(); ++i) {
            QTreeWidgetItem *c = m_arbol->topLevelItem(i);
            QFont f = c->font(0);
            f.setBold(c->data(0, RolCapa).toString() == activa);
            c->setFont(0, f);
        }
    }

    // ==================================================== borrado =========
    void borrarSeleccion()
    {
        const qint64 id = m_mapa->selectedFeature();
        if (id < 0) {
            statusBar()->showMessage(tr("No hay ninguna entidad seleccionada"), 3000);
            return;
        }
        m_mapa->removeFeature(id);           // el panel se refresca por senal
        statusBar()->showMessage(tr("Entidad %1 borrada").arg(id), 3000);
    }

    void vaciarCapa()
    {
        const QString capa = m_mapa->activeFeatureLayer();
        const int n = static_cast<int>(m_mapa->featuresInLayer(capa).size());
        if (n == 0) {
            statusBar()->showMessage(tr("La capa '%1' ya esta vacia").arg(capa), 3000);
            return;
        }
        if (QMessageBox::question(this, tr("Vaciar capa"),
                tr("Se borraran %1 entidad(es) de la capa '%2'.\n"
                   "La capa se conserva. Continuar?").arg(n).arg(capa))
                != QMessageBox::Yes)
            return;
        m_mapa->clearFeatureLayer(capa);
        statusBar()->showMessage(tr("Capa '%1' vaciada").arg(capa), 3000);
    }

    // ==================================================== capas ===========
    void crearCapa()
    {
        bool ok = false;
        const int nCapas = static_cast<int>(m_mapa->featureLayers().size());
        const QString nombre = QInputDialog::getText(this, tr("Nueva capa"),
            tr("Identificador:"), QLineEdit::Normal,
            QStringLiteral("capa%1").arg(nCapas), &ok).trimmed();
        if (!ok || nombre.isEmpty())
            return;
        m_mapa->addFeatureLayer(nombre, nombre, nCapas * 10);
        m_mapa->setActiveFeatureLayer(nombre);
        resaltarCapaActiva();
    }

    void borrarCapa()
    {
        const QString capa = m_mapa->activeFeatureLayer();
        if (capa == QStringLiteral("default")) {
            QMessageBox::information(this, tr("Borrar capa"),
                tr("La capa por defecto no se puede borrar: siempre tiene que "
                   "haber donde poner algo."));
            return;
        }
        const int n = static_cast<int>(m_mapa->featuresInLayer(capa).size());
        if (n > 0 && QMessageBox::question(this, tr("Borrar capa"),
                tr("Se borrara la capa '%1' y sus %2 entidad(es). Continuar?")
                    .arg(capa).arg(n)) != QMessageBox::Yes)
            return;
        m_mapa->removeFeatureLayer(capa);
    }

    void moverCapa(int direccion)
    {
        const auto capas = m_mapa->featureLayers();     // asc por zOrder
        const QString actual = m_mapa->activeFeatureLayer();
        for (int i = 0; i < capas.size(); ++i) {
            if (capas[i].id != actual)
                continue;
            const int j = i + direccion;
            if (j < 0 || j >= capas.size())
                return;
            m_mapa->setFeatureLayerZOrder(capas[i].id, capas[j].zOrder);
            m_mapa->setFeatureLayerZOrder(capas[j].id, capas[i].zOrder);
            return;
        }
    }

    // =============================================== propiedades / estilo ==
    FeatureStyle estiloActual() const
    {
        FeatureStyle e;
        e.lineColor = m_colorLinea;
        e.fillColor = m_colorRelleno;
        e.lineWidth = m_anchoLinea->value();
        e.lineStyle = static_cast<Qt::PenStyle>(m_estiloLinea->currentData().toInt());
        e.pointRadiusPx = m_radioPunto->value();
        e.labelVisible = m_verEtiqueta->isChecked();
        e.verticesVisible = m_verVertices->isChecked();
        return e;
    }

    void aplicarEstiloAlTrazo() { m_mapa->setDraftStyle(estiloActual()); }

    void aplicarEstilo()
    {
        if (m_actualizandoPropiedades)
            return;
        if (m_aplicarAlTrazo->isChecked())
            m_mapa->setDraftStyle(estiloActual());

        const qint64 id = m_mapa->selectedFeature();
        if (id < 0)
            return;
        auto f = m_mapa->feature(id);
        if (!f)
            return;
        f->style = estiloActual();
        m_mapa->updateFeature(*f);
    }

    void aplicarPropiedades()
    {
        if (m_actualizandoPropiedades)
            return;
        const qint64 id = m_mapa->selectedFeature();
        if (id < 0)
            return;
        auto f = m_mapa->feature(id);
        if (!f)
            return;

        f->name = m_campoNombre->text();
        f->type = m_campoTipo->text();
        f->description = m_campoDescripcion->text();

        QVariantMap atributos;
        for (int i = 0; i < m_tablaAtributos->rowCount(); ++i) {
            auto *clave = m_tablaAtributos->item(i, 0);
            auto *valor = m_tablaAtributos->item(i, 1);
            if (clave && !clave->text().isEmpty())
                atributos[clave->text()] = valor ? valor->text() : QString();
        }
        f->attributes = atributos;
        m_mapa->updateFeature(*f);
    }

    //! Vuelca la entidad seleccionada a los campos del panel.
    void mostrarPropiedades(qint64 id)
    {
        const auto f = m_mapa->feature(id);

        // Sin este guardia, rellenar los campos dispararia editingFinished y
        // se reescribiria la entidad con lo que acabamos de leer.
        m_actualizandoPropiedades = true;

        const bool hay = f.has_value();
        m_campoNombre->setEnabled(hay);
        m_campoTipo->setEnabled(hay);
        m_campoDescripcion->setEnabled(hay);
        m_tablaAtributos->setEnabled(hay);

        m_campoNombre->setText(hay ? f->name : QString());
        m_campoTipo->setText(hay ? f->type : QString());
        m_campoDescripcion->setText(hay ? f->description : QString());

        m_tablaAtributos->setRowCount(0);
        if (hay) {
            for (auto it = f->attributes.constBegin();
                 it != f->attributes.constEnd(); ++it) {
                const int fila = m_tablaAtributos->rowCount();
                m_tablaAtributos->insertRow(fila);
                m_tablaAtributos->setItem(fila, 0, new QTableWidgetItem(it.key()));
                m_tablaAtributos->setItem(fila, 1,
                    new QTableWidgetItem(it.value().toString()));
            }
            m_colorLinea = f->style.lineColor;
            m_colorRelleno = f->style.fillColor;
            pintarBotonColor(m_btnColorLinea, m_colorLinea);
            pintarBotonColor(m_btnColorRelleno, m_colorRelleno);
            m_anchoLinea->setValue(f->style.lineWidth);
            m_radioPunto->setValue(f->style.pointRadiusPx);
            m_estiloLinea->setCurrentIndex(
                m_estiloLinea->findData(static_cast<int>(f->style.lineStyle)));
            m_verEtiqueta->setChecked(f->style.labelVisible);
            m_verVertices->setChecked(f->style.verticesVisible);
        }
        m_actualizandoPropiedades = false;
    }

    void pintarBotonColor(QPushButton *boton, const QColor &c)
    {
        boton->setStyleSheet(QStringLiteral(
            "background-color: rgba(%1,%2,%3,%4); min-height: 20px;")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
        boton->setText(c.name());
    }

    void elegirColor(QColor *destino, QPushButton *boton)
    {
        const QColor c = QColorDialog::getColor(*destino, this, tr("Color"),
                                                QColorDialog::ShowAlphaChannel);
        if (!c.isValid())
            return;
        *destino = c;
        pintarBotonColor(boton, c);
        aplicarEstilo();
    }

    // =============================================== persistencia =========
    void guardar()
    {
        if (m_archivoActual.isEmpty()) {
            guardarComo();
            return;
        }
        if (m_mapa->saveFeaturesTo(m_archivoActual))
            statusBar()->showMessage(tr("Guardadas %1 entidades en %2")
                .arg(m_mapa->featureCount()).arg(m_archivoActual), 6000);
        else
            QMessageBox::warning(this, tr("Guardar"),
                tr("No se pudo guardar en:\n%1").arg(m_archivoActual));
    }

    void guardarComo()
    {
        const QString base = m_archivoActual.isEmpty()
            ? dirTrabajo() + QStringLiteral("/entidades.db") : m_archivoActual;
        const QString ruta = QFileDialog::getSaveFileName(this,
            tr("Guardar entidades"), base, tr("Base de datos SQLite (*.db)"));
        if (ruta.isEmpty())
            return;
        if (m_mapa->saveFeaturesTo(ruta)) {
            m_archivoActual = ruta;
            m_ultimoDir = QFileInfo(ruta).absolutePath();
            actualizarTitulo();
            statusBar()->showMessage(tr("Guardadas %1 entidades en %2")
                .arg(m_mapa->featureCount()).arg(ruta), 6000);
        } else {
            QMessageBox::warning(this, tr("Guardar como"),
                tr("No se pudo guardar en:\n%1").arg(ruta));
        }
    }

    void abrir()
    {
        // Cargar sustituye TODO. Se avisa si hay trabajo sin guardar; de todos
        // modos es deshacible (queda en la pila de deshacer).
        if (m_mapa->featureCount() > 0 &&
            QMessageBox::question(this, tr("Abrir"),
                tr("Se reemplazaran las %1 entidad(es) actuales.\n"
                   "La carga se puede deshacer. Continuar?")
                    .arg(m_mapa->featureCount())) != QMessageBox::Yes)
            return;

        const QString ruta = QFileDialog::getOpenFileName(this,
            tr("Abrir entidades"), dirTrabajo(),
            tr("Base de datos SQLite (*.db)"));
        if (ruta.isEmpty())
            return;

        if (m_mapa->loadFeaturesFrom(ruta)) {
            m_archivoActual = ruta;
            m_ultimoDir = QFileInfo(ruta).absolutePath();
            actualizarTitulo();
            // El panel se refresca solo: loadFeaturesFrom -> setContents emite
            // layersChanged. No hace falta reconstruir a mano.
            statusBar()->showMessage(
                tr("Cargadas %1 entidades desde %2")
                    .arg(m_mapa->featureCount()).arg(ruta), 6000);
        } else {
            QMessageBox::warning(this, tr("Abrir"),
                tr("No se pudo abrir el fichero:\n%1").arg(ruta));
        }
    }

    QString dirTrabajo() const
    {
        return m_ultimoDir.isEmpty() ? QDir::currentPath() : m_ultimoDir;
    }

    void actualizarTitulo()
    {
        const QString f = m_archivoActual.isEmpty()
            ? tr("(sin guardar)") : QFileInfo(m_archivoActual).fileName();
        const DataPackageInfo paquete = m_mapa ? m_mapa->packageInfo() : DataPackageInfo();
        const QString datos = paquete.isValid()
            ? tr("  -  datos: %1 %2").arg(paquete.name, paquete.dataVersion) : QString();
        setWindowTitle(tr("libmapa - demostracion  -  %1%2").arg(f, datos));
    }

    // ============================================ datos / simulacion ======
    void cargarGeo()
    {
        const QString ruta = QFileDialog::getOpenFileName(this,
            tr("Cargar fichero .geo"), dirTrabajo(),
            tr("Contornos (*.geo);;Todos (*)"));
        if (ruta.isEmpty())
            return;

        const QString id = QFileInfo(ruta).completeBaseName().toLower();
        FeatureStyle estilo;
        estilo.lineColor = QColor(0x00, 0x69, 0x94);
        estilo.fillColor = QColor(0x00, 0x69, 0x94, 40);

        QString error;
        const qint64 fid = m_mapa->loadGeoAsLayer(ruta, id,
            QFileInfo(ruta).completeBaseName(), estilo, &error);
        if (fid < 0) {
            QMessageBox::warning(this, tr("Cargar .geo"),
                tr("No se pudo cargar:\n%1").arg(error));
            return;
        }
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        m_mapa->setActiveFeatureLayer(id);
        statusBar()->showMessage(tr("Cargado %1 como una entidad en la capa '%2'")
            .arg(QFileInfo(ruta).fileName(), id), 5000);
    }

    // Prepara el seguimiento de objetivos: dibuja el juego de iconos (una vez) y
    // registra la simbologia de la app. Demuestra el contrato de la libreria:
    // ella es agnostica del dominio; la app elige el icono por 'kind' y por
    // estado (un UAV con poca bateria va en rojo), y fija el nivel de detalle
    // para escalar a miles. Un 'kind' sin icono cae al galon por defecto.
    void prepararSeguimiento()
    {
        m_icoBuque   = iconoBuque(QColor(0x20, 0x6a, 0xd0));
        m_icoAereo   = iconoAereo(QColor(0x0c, 0x97, 0x8a));
        m_icoUav     = iconoUav(QColor(0x3c, 0xb0, 0x4a));
        m_icoUavBajo = iconoUav(QColor(0xd0, 0x3a, 0x2a));

        m_mapa->setTargetSymbolProvider([this](const MapTarget &t) {
            TargetSymbol s;                         // icono nulo => galon
            if (t.kind == QLatin1String("buque"))
                s.icon = m_icoBuque;
            else if (t.kind == QLatin1String("aeronave"))
                s.icon = m_icoAereo;
            else if (t.kind == QLatin1String("uav"))
                s.icon = t.attributes.value(QStringLiteral("bateria")).toInt() < 20
                             ? m_icoUavBajo : m_icoUav;
            s.rotateWithHeading = true;
            return s;
        });

        // Con miles de objetivos se dejan de rotular/trazar al amontonarse.
        m_mapa->setTargetDetailBudget(200, 600);

        // Clic sobre un objetivo -> la libreria lo resalta y emite targetClicked;
        // aqui mostramos sus datos de dominio (sus attributes) en la barra de
        // estado. Es el cierre del ciclo ver -> seleccionar -> consultar.
        connect(m_mapa, &MapWidget::targetClicked, this,
                [this](qint64 id, const QGeoCoordinate &) {
                    const auto t = m_mapa->target(id);
                    if (!t)
                        return;
                    QStringList datos;
                    for (auto it = t->attributes.constBegin();
                         it != t->attributes.constEnd(); ++it)
                        datos << QStringLiteral("%1=%2")
                                     .arg(it.key(), it.value().toString());
                    statusBar()->showMessage(
                        tr("Objetivo %1 [%2]  %3")
                            .arg(id).arg(t->kind, datos.join(QStringLiteral("   "))),
                        8000);
                });
    }

    void alternarSimulacion(bool on)
    {
        if (on) {
            iniciarSimulacion(m_numObjetivos->value());
            m_simReloj->start();
        } else {
            m_simReloj->stop();
            m_mapa->clearTargets();
            m_simIds.clear();
            m_simVel.clear();
            statusBar()->showMessage(tr("Simulacion detenida"), 3000);
        }
        m_numObjetivos->setEnabled(!on);
    }

    void iniciarSimulacion(int n)
    {
        m_mapa->clearTargets();
        m_simIds.clear();
        m_simVel.clear();
        m_simIds.reserve(n);
        m_simVel.reserve(n);

        auto *r = QRandomGenerator::global();
        // Tres clases de objetivo repartidas. Cada una lleva SUS datos en
        // attributes (la libreria no los interpreta) y su etiqueta se compone de
        // ellos: asi se ven las Fases 1 (datos) y 2 (icono por kind/estado).
        static const char *clases[] = { "buque", "aeronave", "uav" };

        for (int i = 0; i < n; ++i) {
            // Repartidos por el mar alrededor de Cuba.
            const double lat = 19.5 + r->bounded(4.5);       // 19.5 .. 24.0
            const double lon = -85.0 + r->bounded(11.0);     // -85 .. -74
            const double rumbo = r->bounded(360.0);
            const double velGrados = 0.002 + r->bounded(0.004);  // por paso

            const QString kind = QLatin1String(clases[i % 3]);
            MapTarget t;
            t.position = QGeoCoordinate(lat, lon);
            t.headingDeg = rumbo;
            t.kind = kind;

            if (kind == QLatin1String("buque")) {
                const int mmsi = 224000000 + r->bounded(999999);
                const int nudos = 8 + r->bounded(14);
                t.speed = nudos;
                t.attributes.insert(QStringLiteral("mmsi"), mmsi);
                t.attributes.insert(QStringLiteral("eslora"), 40 + r->bounded(260));
                t.label = QStringLiteral("Buque %1\nMMSI %2\n%3 kn")
                              .arg(i + 1).arg(mmsi).arg(nudos);
                t.color = QColor(0x20, 0x6a, 0xd0);
            } else if (kind == QLatin1String("aeronave")) {
                const QString cs = QStringLiteral("CUB%1").arg(100 + r->bounded(900));
                const int fl = 80 + r->bounded(320);        // nivel de vuelo
                t.attributes.insert(QStringLiteral("callsign"), cs);
                t.attributes.insert(QStringLiteral("squawk"),
                                    QStringLiteral("%1").arg(1000 + r->bounded(6000)));
                t.attributes.insert(QStringLiteral("fl"), fl);
                t.label = QStringLiteral("%1\nFL%2")
                              .arg(cs).arg(fl, 3, 10, QLatin1Char('0'));
                t.color = QColor(0x0c, 0x97, 0x8a);
            } else {                                         // uav
                const int bat = r->bounded(100);
                t.attributes.insert(QStringLiteral("bateria"), bat);
                t.attributes.insert(QStringLiteral("enlace"), 60 + r->bounded(40));
                t.label = QStringLiteral("UAV %1\nBat %2%").arg(i + 1).arg(bat);
                t.color = bat < 20 ? QColor(0xd0, 0x3a, 0x2a)
                                   : QColor(0x3c, 0xb0, 0x4a);
            }

            const qint64 id = m_mapa->addTarget(t);
            m_simIds.append(id);

            const double rad = qDegreesToRadians(rumbo);
            // Componentes: x = variacion de longitud, y = variacion de latitud.
            m_simVel.append(QPointF(std::sin(rad) * velGrados,
                                    std::cos(rad) * velGrados));
        }
        statusBar()->showMessage(tr("Simulando %1 objetivos").arg(n), 3000);
    }

    void pasoSimulacion()
    {
        auto *r = QRandomGenerator::global();
        for (int i = 0; i < m_simIds.size(); ++i) {
            QPointF &v = m_simVel[i];
            const auto t = m_mapa->target(m_simIds[i]);
            if (!t)
                continue;

            double lat = t->position.latitude() + v.y();
            double lon = t->position.longitude() + v.x();

            // Rebote contra los limites del area para que no se escapen.
            if (lat < 19.0 || lat > 24.5) { v.setY(-v.y()); lat = t->position.latitude(); }
            if (lon < -86.0 || lon > -73.0) { v.setX(-v.x()); lon = t->position.longitude(); }

            // Un pequeno viraje aleatorio de vez en cuando.
            if (r->bounded(100) < 3) {
                const double giro = (r->bounded(2.0) - 1.0) * 0.2;
                const double c = std::cos(giro), s = std::sin(giro);
                const double nx = v.x() * c - v.y() * s;
                const double ny = v.x() * s + v.y() * c;
                v.setX(nx);
                v.setY(ny);
            }

            const double rumbo = qRadiansToDegrees(std::atan2(v.x(), v.y()));
            m_mapa->updateTarget(m_simIds[i], QGeoCoordinate(lat, lon), rumbo);
        }
        actualizarEstado();
    }

    // ==================================================== estado ==========
    void actualizarEstado()
    {
        if (m_accDeshacer) {
            m_accDeshacer->setEnabled(m_mapa->canUndo());
            m_accRehacer->setEnabled(m_mapa->canRedo());
            m_accBorrar->setEnabled(m_mapa->selectedFeature() >= 0);
        }
        if (!m_info)
            return;
        m_info->setText(
            QStringLiteral("  %1  |  zoom %2 de %3  |  %4 %% propias  |  %5 entidades  |  %6 objetivos  ")
            .arg(m_mapa->baseLayerId())
            .arg(m_mapa->zoom())
            .arg(m_mapa->maxZoom())
            .arg(m_mapa->exactCoverage() * 100.0, 0, 'f', 0)
            .arg(m_mapa->featureCount())
            .arg(m_mapa->targetCount()));
    }

    // ==================================================== miembros ========
    MapWidget *m_mapa = nullptr;
    QTimer *m_reconstruir = nullptr;

    QComboBox *m_capasBase = nullptr;
    QActionGroup *m_grupo = nullptr;
    QAction *m_accNavegar = nullptr;
    QAction *m_accDeshacer = nullptr;
    QAction *m_accRehacer = nullptr;
    QAction *m_accBorrar = nullptr;

    QTreeWidget *m_arbol = nullptr;
    QLineEdit *m_campoNombre = nullptr;
    QLineEdit *m_campoTipo = nullptr;
    QLineEdit *m_campoDescripcion = nullptr;
    QTableWidget *m_tablaAtributos = nullptr;
    QPushButton *m_btnColorLinea = nullptr;
    QPushButton *m_btnColorRelleno = nullptr;
    QDoubleSpinBox *m_anchoLinea = nullptr;
    QDoubleSpinBox *m_radioPunto = nullptr;
    QComboBox *m_estiloLinea = nullptr;
    QCheckBox *m_verEtiqueta = nullptr;
    QCheckBox *m_verVertices = nullptr;
    QCheckBox *m_aplicarAlTrazo = nullptr;

    QLabel *m_coords = nullptr;
    QLabel *m_cota = nullptr;
    QLabel *m_info = nullptr;
    QSpinBox *m_zCobertura = nullptr;
    QPushButton *m_btnDem = nullptr;
    bool m_demActivo = false;

    // Analisis de elevacion (pestaña "Elevacion" del panel lateral).
    QTabWidget *m_tabs = nullptr;
    QDoubleSpinBox *m_latA = nullptr;       //!< punto A: latitud (perfil/visión/zvd)
    QDoubleSpinBox *m_lonA = nullptr;       //!< punto A: longitud
    QDoubleSpinBox *m_latB = nullptr;       //!< punto B: latitud (visión)
    QDoubleSpinBox *m_lonB = nullptr;       //!< punto B: longitud
    QDoubleSpinBox *m_rumbo = nullptr;      //!< rumbo (azimut) del perfil radial
    QDoubleSpinBox *m_altA = nullptr;       //!< antena A / altura del observador
    QDoubleSpinBox *m_altB = nullptr;       //!< antena B / altura del objetivo
    QDoubleSpinBox *m_alcanceKm = nullptr;  //!< alcance del viewshed / perfil
    Pick m_picking = Pick::Ninguno;
    Pick m_dragPick = Pick::Ninguno;        //!< pin A/B que se está arrastrando
    QObject *m_plotObj = nullptr;           //!< la MapView, objetivo del eventFilter
    QLabel *m_infoPuntos = nullptr;         //!< cota A/B, rumbo y distancia A→B
    QCheckBox *m_marComo0 = nullptr;        //!< tratar mar/sin dato como 0 m
    QCheckBox *m_curvatura = nullptr;       //!< aplicar curvatura 4/3
    QLabel *m_resultado = nullptr;          //!< lectura del ultimo analisis
    QDialog *m_perfilWin = nullptr;         //!< ventana flotante del perfil
    QLabel *m_perfilInfo = nullptr;
    QCustomPlot *m_plot = nullptr;          //!< gráfica del perfil (QCustomPlot)
    enum class Analisis { Ninguno, Perfil, Vision, Viewshed, Picos };
    Analisis m_ultimo = Analisis::Ninguno;  //!< para re-aplicar al cambiar un check
    const QString kCapaVision = QStringLiteral("elev_vision");
    const QString kCapaViewshed = QStringLiteral("elev_viewshed");
    const QString kCapaPuntos = QStringLiteral("elev_puntos");   //!< pines fijos A/B
    const QString kCapaPicos = QStringLiteral("elev_picos");     //!< 10 puntos más altos
    const QColor m_colorA = QColor(0x15, 0x65, 0xc0);            //!< azul = punto A
    const QColor m_colorB = QColor(0xc6, 0x28, 0x28);            //!< rojo = punto B
    QDoubleSpinBox *m_picosRadioKm = nullptr;  //!< radio de búsqueda de picos
    QSpinBox *m_picosSepM = nullptr;           //!< separación mínima entre cumbres
    // Entrada por análisis: un selector y filas que se muestran/ocultan según el tipo.
    QComboBox *m_tipoAnalisis = nullptr;
    QWidget *m_rowPosA = nullptr, *m_rowPosB = nullptr, *m_rowAzimut = nullptr;
    QWidget *m_rowDist = nullptr, *m_rowAnt1 = nullptr, *m_rowAnt2 = nullptr;
    QWidget *m_rowRadio = nullptr, *m_rowSep = nullptr, *m_rowChecks = nullptr;
    QLabel *m_lblPosA = nullptr, *m_lblDist = nullptr, *m_lblAnt1 = nullptr, *m_lblAnt2 = nullptr;
    QVector<qint64> m_picoIds;                 //!< ids de los marcadores de pico
    QHash<qint64, MapFeature> m_picoBase;      //!< estilo base (para restaurar tras hover)
    qint64 m_picoHover = -1;                   //!< pico resaltado bajo el cursor

    bool m_actualizandoPropiedades = false;
    QColor m_colorLinea = QColor(0xd3, 0x2f, 0x2f);
    QColor m_colorRelleno = QColor(0xd3, 0x2f, 0x2f, 70);
    QString m_archivoActual;
    QString m_ultimoDir;

    // Simulacion de objetivos moviles.
    QSpinBox *m_numObjetivos = nullptr;
    QComboBox *m_traza = nullptr;
    QAction *m_accSimular = nullptr;
    QTimer *m_simReloj = nullptr;
    QVector<qint64> m_simIds;
    QVector<QPointF> m_simVel;      //!< x = dLon, y = dLat por paso.
    // Juego de iconos de la app para la simbologia de objetivos (Fase 2).
    QPixmap m_icoBuque, m_icoAereo, m_icoUav, m_icoUavBajo;
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

    // Primer positional: la carpeta de un paquete de datos (o su mapa.json), o un
    // datasets.json. Opcionales: --dem <carpeta> / --dem-db <fichero>
    // (elevacion) y --features <fichero> (persistencia automatica de entidades:
    // lo que dibujes se guarda y recarga solo); con paquete, mandan sobre el suyo.
    QString datasets;
    QString demDir, demDb, featuresDb;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--dem-db") && i + 1 < argc)
            demDb = QString::fromLocal8Bit(argv[++i]);
        else if ((a == QLatin1String("--dem") || a == QLatin1String("--elev"))
                 && i + 1 < argc)
            demDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == QLatin1String("--features") && i + 1 < argc)
            featuresDb = QString::fromLocal8Bit(argv[++i]);
        else if (datasets.isEmpty() && !a.startsWith(QLatin1String("--")))
            datasets = a;
    }
    // Sin argumento: un mapa.json en la carpeta actual, si lo hay; si no, el
    // datasets.json de siempre.
    if (datasets.isEmpty())
        datasets = QFile::exists(QDir::currentPath() + QStringLiteral("/mapa.json"))
            ? QDir::currentPath()
            : QDir::currentPath() + QStringLiteral("/datasets.json");
    const QFileInfo origen(datasets);
    const bool esPaquete = origen.isDir()
        || origen.fileName().compare(QLatin1String("mapa.json"), Qt::CaseInsensitive) == 0;

    Ventana v(datasets, esPaquete, demDir, demDb, featuresDb);
    v.show();
    return app.exec();
}

#include "main.moc"
