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

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

using namespace libmapa;

namespace {
//! Roles para distinguir capas de entidades en el arbol.
constexpr int RolCapa = Qt::UserRole;        //!< id de capa (en ambos)
constexpr int RolEntidad = Qt::UserRole + 1; //!< id de entidad (solo hojas)
} // namespace

class Ventana : public QMainWindow
{
    Q_OBJECT

public:
    explicit Ventana(const QString &datasetsFile)
    {
        // --- Asi se crea el mapa. Esto es todo. -------------------------
        MapConfig cfg;
        cfg.datasetsFile = datasetsFile;
        cfg.initialCenter = QGeoCoordinate(23.1136, -82.3666);   // La Habana
        cfg.initialZoom = 11;
        cfg.cacheMiB = 192;

        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        actualizarTitulo();
        resize(1200, 800);

        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo iniciar el mapa:\n%1\n\n"
                   "Genera datasets.json con probe_db y pasa su ruta como "
                   "argumento.").arg(m_mapa->lastError()));
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
        construirPanel();
        conectarSenales();
        aplicarEstiloAlTrazo();
        reconstruirPanel();
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

        dock->setWidget(cont);
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
        m_info = new QLabel(this);
        statusBar()->addPermanentWidget(m_coords);
        statusBar()->addPermanentWidget(m_info);
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
        setWindowTitle(tr("libmapa - demostracion  -  %1").arg(f));
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
            QStringLiteral("  %1  |  zoom %2 de %3  |  %4 %% propias  |  %5 entidades  ")
            .arg(m_mapa->baseLayerId())
            .arg(m_mapa->zoom())
            .arg(m_mapa->maxZoom())
            .arg(m_mapa->exactCoverage() * 100.0, 0, 'f', 0)
            .arg(m_mapa->featureCount()));
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
    QLabel *m_info = nullptr;

    bool m_actualizandoPropiedades = false;
    QColor m_colorLinea = QColor(0xd3, 0x2f, 0x2f);
    QColor m_colorRelleno = QColor(0xd3, 0x2f, 0x2f, 70);
    QString m_archivoActual;
    QString m_ultimoDir;
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
