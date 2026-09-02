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

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <algorithm>

#include <QCheckBox>

#include <QCheckBox>
#include <QColorDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QListWidget>
#include <QVBoxLayout>
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

//! Alias para no repetir el tipo largo en el bucle del panel de capas.
using LayerInfo = libmapa::LayerInfo;

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
        construirBarraEntidades();
        construirPanelCapas();
        conectarSenales();
        actualizarEstado();
        actualizarPanelCapas();
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

    //! Barra de dibujo y edicion de entidades.
    void construirBarraEntidades()
    {
        auto *barra = addToolBar(tr("Entidades"));
        barra->setMovable(false);

        // Las herramientas son excluyentes entre si.
        m_grupoHerramientas = new QActionGroup(this);
        m_grupoHerramientas->setExclusive(true);

        auto anadirHerramienta = [&](const QString &texto, MapTool h,
                                     const QString &atajo = QString()) {
            QAction *a = barra->addAction(texto);
            a->setCheckable(true);
            a->setData(static_cast<int>(h));
            if (!atajo.isEmpty())
                a->setShortcut(QKeySequence(atajo));
            m_grupoHerramientas->addAction(a);
            return a;
        };

        m_accNavegar = anadirHerramienta(tr("Navegar"), MapTool::None,
                                         QStringLiteral("Esc"));
        m_accNavegar->setChecked(true);
        anadirHerramienta(tr("Punto"), MapTool::DrawPoint, QStringLiteral("1"));
        anadirHerramienta(tr("Linea"), MapTool::DrawPolyline, QStringLiteral("2"));
        anadirHerramienta(tr("Poligono"), MapTool::DrawPolygon, QStringLiteral("3"));
        anadirHerramienta(tr("Editar"), MapTool::EditFeature, QStringLiteral("4"));

        barra->addSeparator();

        m_accDeshacer = barra->addAction(tr("Deshacer"));
        m_accDeshacer->setShortcut(QKeySequence::Undo);
        m_accRehacer = barra->addAction(tr("Rehacer"));
        m_accRehacer->setShortcut(QKeySequence::Redo);

        barra->addSeparator();

        m_accBorrar = barra->addAction(tr("Borrar"));
        m_accBorrar->setShortcut(QKeySequence::Delete);
        auto *nuevaCapa = barra->addAction(tr("Nueva capa"));
        connect(nuevaCapa, &QAction::triggered, this, &Ventana::crearCapa);

        barra->addSeparator();

        auto *guardar = barra->addAction(tr("Guardar"));
        guardar->setShortcut(QKeySequence::Save);
        auto *cargar = barra->addAction(tr("Cargar"));
        guardar->setStatusTip(tr("Vuelca las entidades a un fichero SQLite"));

        connect(guardar, &QAction::triggered, this, &Ventana::guardar);
        connect(cargar, &QAction::triggered, this, &Ventana::cargar);
    }

    /*!
     * \brief Panel lateral: capas, entidades, propiedades y estilo.
     *
     * La demo existe para poder ejercitar TODA la API sin escribir codigo,
     * asi que expone tambien lo que una aplicacion real llevaria en dialogos
     * propios: el tipo de dominio, los atributos y el estilo de cada entidad.
     */
    void construirPanelCapas()
    {
        auto *dock = new QDockWidget(tr("Capas y entidades"), this);
        dock->setObjectName(QStringLiteral("panel"));
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *contenedor = new QWidget(dock);
        auto *caja = new QVBoxLayout(contenedor);
        caja->setContentsMargins(6, 6, 6, 6);

        // --- Arbol de capas y entidades ---------------------------------
        caja->addWidget(new QLabel(tr("<b>Capas y entidades</b>")));
        m_arbol = new QTreeWidget(contenedor);
        m_arbol->setColumnCount(3);
        m_arbol->setHeaderLabels({tr("Nombre"), tr("Tipo"), tr("Vert.")});
        m_arbol->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_arbol->setColumnWidth(1, 90);
        m_arbol->setColumnWidth(2, 40);
        caja->addWidget(m_arbol, 3);

        auto *botonesCapa = new QHBoxLayout;
        auto *btnSubir = new QPushButton(tr("Subir"), contenedor);
        auto *btnBajar = new QPushButton(tr("Bajar"), contenedor);
        auto *btnBorrarCapa = new QPushButton(tr("Borrar capa"), contenedor);
        auto *btnVaciarCapa = new QPushButton(tr("Vaciar"), contenedor);
        botonesCapa->addWidget(btnSubir);
        botonesCapa->addWidget(btnBajar);
        botonesCapa->addWidget(btnVaciarCapa);
        botonesCapa->addWidget(btnBorrarCapa);
        caja->addLayout(botonesCapa);

        // --- Propiedades de la entidad seleccionada ---------------------
        caja->addWidget(new QLabel(tr("<b>Entidad seleccionada</b>")));
        auto *form = new QFormLayout;
        m_campoNombre = new QLineEdit(contenedor);
        m_campoTipo = new QLineEdit(contenedor);
        m_campoDescripcion = new QLineEdit(contenedor);
        m_campoTipo->setPlaceholderText(tr("zona_prohibida, punto_interes..."));
        form->addRow(tr("Nombre:"), m_campoNombre);
        form->addRow(tr("Tipo:"), m_campoTipo);
        form->addRow(tr("Descripcion:"), m_campoDescripcion);
        caja->addLayout(form);

        // Atributos de dominio: la libreria no los interpreta, solo los
        // conserva. Aqui se pueden anadir y quitar para comprobarlo.
        caja->addWidget(new QLabel(tr("Atributos:")));
        m_tablaAtributos = new QTableWidget(0, 2, contenedor);
        m_tablaAtributos->setHorizontalHeaderLabels({tr("Clave"), tr("Valor")});
        m_tablaAtributos->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
        m_tablaAtributos->setMaximumHeight(110);
        caja->addWidget(m_tablaAtributos);

        auto *botonesAtr = new QHBoxLayout;
        auto *btnAddAtr = new QPushButton(tr("+ Atributo"), contenedor);
        auto *btnDelAtr = new QPushButton(tr("- Atributo"), contenedor);
        botonesAtr->addWidget(btnAddAtr);
        botonesAtr->addWidget(btnDelAtr);
        caja->addLayout(botonesAtr);

        // --- Estilo ------------------------------------------------------
        caja->addWidget(new QLabel(tr("<b>Estilo</b>")));
        auto *formEstilo = new QFormLayout;

        m_btnColorLinea = new QPushButton(contenedor);
        m_btnColorRelleno = new QPushButton(contenedor);
        formEstilo->addRow(tr("Color de linea:"), m_btnColorLinea);
        formEstilo->addRow(tr("Color de relleno:"), m_btnColorRelleno);

        m_anchoLinea = new QDoubleSpinBox(contenedor);
        m_anchoLinea->setRange(0.5, 12.0);
        m_anchoLinea->setSingleStep(0.5);
        formEstilo->addRow(tr("Ancho:"), m_anchoLinea);

        m_estiloLinea = new QComboBox(contenedor);
        m_estiloLinea->addItem(tr("Continua"), static_cast<int>(Qt::SolidLine));
        m_estiloLinea->addItem(tr("Discontinua"), static_cast<int>(Qt::DashLine));
        m_estiloLinea->addItem(tr("Punteada"), static_cast<int>(Qt::DotLine));
        m_estiloLinea->addItem(tr("Raya-punto"), static_cast<int>(Qt::DashDotLine));
        formEstilo->addRow(tr("Trazo:"), m_estiloLinea);

        m_radioPunto = new QDoubleSpinBox(contenedor);
        m_radioPunto->setRange(2.0, 30.0);
        formEstilo->addRow(tr("Radio del punto:"), m_radioPunto);

        m_verEtiqueta = new QCheckBox(tr("Mostrar etiqueta"), contenedor);
        formEstilo->addRow(QString(), m_verEtiqueta);
        m_verVertices = new QCheckBox(tr("Mostrar vertices"), contenedor);
        formEstilo->addRow(QString(), m_verVertices);

        caja->addLayout(formEstilo);

        m_aplicarAlTrazo = new QCheckBox(
            tr("Usar este estilo para lo que dibuje"), contenedor);
        m_aplicarAlTrazo->setChecked(true);
        caja->addWidget(m_aplicarAlTrazo);

        caja->addStretch(1);

        dock->setWidget(contenedor);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->setMinimumWidth(320);

        // --- Conexiones ---------------------------------------------------
        connect(m_arbol, &QTreeWidget::itemChanged, this,
                [this](QTreeWidgetItem *item, int col) {
                    if (m_actualizandoCapas || col != 0)
                        return;
                    const QString capa = item->data(0, Qt::UserRole).toString();
                    if (!capa.isEmpty())
                        m_mapa->setFeatureLayerVisible(
                            capa, item->checkState(0) == Qt::Checked);
                });

        connect(m_arbol, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem *item) {
                    if (!item || m_actualizandoCapas)
                        return;
                    const QString capa = item->data(0, Qt::UserRole).toString();
                    if (!capa.isEmpty()) {
                        m_mapa->setActiveFeatureLayer(capa);
                        statusBar()->showMessage(
                            tr("Se dibujara en la capa '%1'").arg(capa), 3000);
                    }
                    const qint64 id =
                        item->data(0, Qt::UserRole + 1).toLongLong();
                    if (id > 0)
                        m_mapa->selectFeature(id);
                });

        connect(btnSubir, &QPushButton::clicked, this, [this] { moverCapa(+1); });
        connect(btnBajar, &QPushButton::clicked, this, [this] { moverCapa(-1); });
        connect(btnBorrarCapa, &QPushButton::clicked, this, &Ventana::borrarCapa);
        connect(btnVaciarCapa, &QPushButton::clicked, this, [this] {
            const QString capa = m_mapa->activeFeatureLayer();
            m_mapa->clearFeatureLayer(capa);
            statusBar()->showMessage(tr("Capa '%1' vaciada").arg(capa), 3000);
        });

        connect(m_campoNombre, &QLineEdit::editingFinished,
                this, &Ventana::aplicarPropiedades);
        connect(m_campoTipo, &QLineEdit::editingFinished,
                this, &Ventana::aplicarPropiedades);
        connect(m_campoDescripcion, &QLineEdit::editingFinished,
                this, &Ventana::aplicarPropiedades);
        connect(m_tablaAtributos, &QTableWidget::cellChanged,
                this, [this](int, int) { aplicarPropiedades(); });

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

        connect(m_btnColorLinea, &QPushButton::clicked, this, [this] {
            elegirColor(&m_colorLinea, m_btnColorLinea);
        });
        connect(m_btnColorRelleno, &QPushButton::clicked, this, [this] {
            elegirColor(&m_colorRelleno, m_btnColorRelleno);
        });
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
    }

    // --- Capas -----------------------------------------------------------

    void moverCapa(int direccion)
    {
        const auto capas = m_mapa->featureLayers();
        const QString actual = m_mapa->activeFeatureLayer();
        for (int i = 0; i < capas.size(); ++i) {
            if (capas[i].id != actual)
                continue;
            // Se intercambia el orden con la vecina.
            const int j = i + direccion;
            if (j < 0 || j >= capas.size())
                return;
            m_mapa->setFeatureLayerZOrder(capas[i].id, capas[j].zOrder);
            m_mapa->setFeatureLayerZOrder(capas[j].id, capas[i].zOrder);
            actualizarPanelCapas();
            return;
        }
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
        if (QMessageBox::question(this, tr("Borrar capa"),
                tr("Se borrara la capa '%1' y todo su contenido.").arg(capa))
            != QMessageBox::Yes)
            return;
        m_mapa->removeFeatureLayer(capa);
        actualizarPanelCapas();
    }

    // --- Propiedades y estilo --------------------------------------------

    void pintarBotonColor(QPushButton *boton, const QColor &c)
    {
        boton->setStyleSheet(QStringLiteral(
            "background-color: rgba(%1,%2,%3,%4); min-height: 20px;")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
        boton->setText(c.name());
    }

    void elegirColor(QColor *destino, QPushButton *boton)
    {
        const QColor c = QColorDialog::getColor(
            *destino, this, tr("Color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid())
            return;
        *destino = c;
        pintarBotonColor(boton, c);
        aplicarEstilo();
    }

    FeatureStyle estiloActual() const
    {
        FeatureStyle e;
        e.lineColor = m_colorLinea;
        e.fillColor = m_colorRelleno;
        e.lineWidth = m_anchoLinea->value();
        e.lineStyle = static_cast<Qt::PenStyle>(
            m_estiloLinea->currentData().toInt());
        e.pointRadiusPx = m_radioPunto->value();
        e.labelVisible = m_verEtiqueta->isChecked();
        e.verticesVisible = m_verVertices->isChecked();
        return e;
    }

    void aplicarEstilo()
    {
        if (m_actualizandoPropiedades)
            return;

        // El estilo se aplica a lo que se dibuje a partir de ahora...
        if (m_aplicarAlTrazo->isChecked())
            m_mapa->setDraftStyle(estiloActual());

        // ...y tambien a lo que este seleccionado, para poder retocarlo.
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
                m_tablaAtributos->setItem(fila, 0,
                    new QTableWidgetItem(it.key()));
                m_tablaAtributos->setItem(fila, 1,
                    new QTableWidgetItem(it.value().toString()));
            }

            // El estilo del panel pasa a ser el de la entidad seleccionada.
            m_colorLinea = f->style.lineColor;
            m_colorRelleno = f->style.fillColor;
            pintarBotonColor(m_btnColorLinea, m_colorLinea);
            pintarBotonColor(m_btnColorRelleno, m_colorRelleno);
            m_anchoLinea->setValue(f->style.lineWidth);
            m_radioPunto->setValue(f->style.pointRadiusPx);
            m_estiloLinea->setCurrentIndex(m_estiloLinea->findData(
                static_cast<int>(f->style.lineStyle)));
            m_verEtiqueta->setChecked(f->style.labelVisible);
            m_verVertices->setChecked(f->style.verticesVisible);
        }

        m_actualizandoPropiedades = false;
    }

    void crearCapa()
    {
        bool ok = false;
        const QString nombre = QInputDialog::getText(
            this, tr("Nueva capa"), tr("Identificador:"),
            QLineEdit::Normal, QStringLiteral("capa%1")
                .arg(m_mapa->featureLayers().size()), &ok);
        if (!ok || nombre.isEmpty())
            return;
        m_mapa->addFeatureLayer(nombre, nombre,
                                static_cast<int>(m_mapa->featureLayers().size()) * 10);
        m_mapa->setActiveFeatureLayer(nombre);
        actualizarPanelCapas();
    }

    void guardar()
    {
        const QString ruta = QFileDialog::getSaveFileName(
            this, tr("Guardar entidades"),
            QDir::currentPath() + QStringLiteral("/entidades.db"),
            tr("Base de datos SQLite (*.db)"));
        if (ruta.isEmpty())
            return;

        if (m_mapa->saveFeaturesTo(ruta))
            statusBar()->showMessage(
                tr("Guardadas %1 entidades en %2")
                    .arg(m_mapa->featureCount()).arg(ruta), 6000);
    }

    void cargar()
    {
        const QString ruta = QFileDialog::getOpenFileName(
            this, tr("Cargar entidades"), QDir::currentPath(),
            tr("Base de datos SQLite (*.db)"));
        if (ruta.isEmpty())
            return;

        if (m_mapa->loadFeaturesFrom(ruta)) {
            statusBar()->showMessage(
                tr("Cargadas %1 entidades").arg(m_mapa->featureCount()), 6000);
            actualizarPanelCapas();
        }
    }

    void actualizarPanelCapas()
    {
        // Bandera para no reaccionar a los itemChanged que provoca el propio
        // repoblado: sin ella, reconstruir el arbol apagaria las capas.
        m_actualizandoCapas = true;
        m_arbol->clear();

        const QString activa = m_mapa->activeFeatureLayer();
        const qint64 seleccionada = m_mapa->selectedFeature();

        // Las capas llegan ordenadas por zOrder: la ultima se dibuja encima.
        // Se muestran al reves para que arriba en el panel sea arriba en el
        // mapa, que es lo que espera cualquiera.
        auto capas = m_mapa->featureLayers();
        std::reverse(capas.begin(), capas.end());

        for (const LayerInfo &c : capas) {
            auto *nodo = new QTreeWidgetItem(m_arbol);
            nodo->setText(0, QStringLiteral("%1  (%2)")
                                 .arg(c.displayName).arg(c.featureCount));
            nodo->setText(1, QStringLiteral("z=%1").arg(c.zOrder));
            nodo->setData(0, Qt::UserRole, c.id);
            nodo->setFlags(nodo->flags() | Qt::ItemIsUserCheckable);
            nodo->setCheckState(0, c.visible ? Qt::Checked : Qt::Unchecked);
            nodo->setExpanded(true);

            QFont negrita = nodo->font(0);
            negrita.setBold(c.id == activa);
            nodo->setFont(0, negrita);

            for (const MapFeature &f : m_mapa->featuresInLayer(c.id)) {
                auto *hijo = new QTreeWidgetItem(nodo);
                hijo->setText(0, f.name.isEmpty()
                                     ? tr("(sin nombre) #%1").arg(f.id)
                                     : f.name);
                hijo->setText(1, f.type);
                hijo->setText(2, QString::number(f.geometry.size()));
                hijo->setData(0, Qt::UserRole + 1, f.id);

                // Un cuadrito con el color de la entidad, para distinguirlas
                // de un vistazo.
                QPixmap muestra(12, 12);
                muestra.fill(f.style.lineColor);
                hijo->setIcon(0, QIcon(muestra));

                if (f.id == seleccionada) {
                    m_arbol->setCurrentItem(hijo);
                    hijo->setSelected(true);
                }
            }
        }
        m_actualizandoCapas = false;
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

        connect(m_grupoHerramientas, &QActionGroup::triggered, this,
                [this](QAction *a) {
                    m_mapa->setActiveTool(
                        static_cast<MapTool>(a->data().toInt()));
                });

        connect(m_accDeshacer, &QAction::triggered, this, [this] {
            m_mapa->undo();
            actualizarPanelCapas();
        });
        connect(m_accRehacer, &QAction::triggered, this, [this] {
            m_mapa->redo();
            actualizarPanelCapas();
        });
        connect(m_accBorrar, &QAction::triggered, this, [this] {
            const qint64 id = m_mapa->selectedFeature();
            if (id >= 0) {
                m_mapa->removeFeature(id);
                actualizarPanelCapas();
            }
        });

        connect(m_mapa, &MapWidget::featureCreated, this, [this](qint64 id) {
            const auto f = m_mapa->feature(id);
            statusBar()->showMessage(
                tr("Creada entidad %1 con %2 vertices en la capa '%3'")
                    .arg(id).arg(f ? f->geometry.size() : 0)
                    .arg(f ? f->layerId : QString()), 5000);
            actualizarPanelCapas();
            actualizarEstado();
        });

        connect(m_mapa, &MapWidget::featureSelected, this,
                [this](qint64 id) { mostrarPropiedades(id); });
        connect(m_mapa, &MapWidget::featureSelected, this, [this](qint64 id) {
            if (id < 0) {
                statusBar()->clearMessage();
                return;
            }
            const auto f = m_mapa->feature(id);
            if (f)
                statusBar()->showMessage(
                    tr("Seleccionada: %1 (%2 vertices, capa '%3')")
                        .arg(f->name.isEmpty() ? tr("sin nombre") : f->name)
                        .arg(f->geometry.size()).arg(f->layerId));
        });

        connect(m_mapa, &MapWidget::featureRemoved, this, [this](qint64) {
            actualizarPanelCapas();
            actualizarEstado();
        });

        connect(m_mapa, &MapWidget::drawingCancelled, this, [this] {
            statusBar()->showMessage(tr("Trazado cancelado"), 3000);
        });

        m_coords = new QLabel(this);
        m_info = new QLabel(this);
        statusBar()->addPermanentWidget(m_coords);
        statusBar()->addPermanentWidget(m_info);
    }

    void actualizarEstado()
    {
        if (m_accDeshacer) {
            m_accDeshacer->setEnabled(m_mapa->canUndo());
            m_accRehacer->setEnabled(m_mapa->canRedo());
            m_accBorrar->setEnabled(m_mapa->selectedFeature() >= 0);
        }
        if (!m_info)
            return;
        m_info->setText(QStringLiteral("  %1  |  zoom %2 de %3  |  %4 % propias  |  %5 entidades  ")
            .arg(m_mapa->baseLayerId())
            .arg(m_mapa->zoom())
            .arg(m_mapa->maxZoom())
            .arg(m_mapa->exactCoverage() * 100.0, 0, 'f', 0)
            .arg(m_mapa->featureCount()));
    }

    MapWidget *m_mapa = nullptr;
    QComboBox *m_capas = nullptr;
    QAction *m_medir = nullptr;
    QAction *m_zoomArea = nullptr;
    QLabel *m_coords = nullptr;
    QLabel *m_info = nullptr;

    QActionGroup *m_grupoHerramientas = nullptr;
    QAction *m_accNavegar = nullptr;
    QAction *m_accDeshacer = nullptr;
    QAction *m_accRehacer = nullptr;
    QAction *m_accBorrar = nullptr;
    bool m_actualizandoCapas = false;
    bool m_actualizandoPropiedades = false;

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
    QColor m_colorLinea = QColor(0xd3, 0x2f, 0x2f);
    QColor m_colorRelleno = QColor(0xd3, 0x2f, 0x2f, 70);
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
