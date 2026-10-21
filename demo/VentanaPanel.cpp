// Panel lateral: árbol de capas y entidades, ficha de propiedades, estilo y coordenadas.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

#include <QFileInfo>

using namespace libmapa;

// Construye el panel lateral (dock): pestañas Capas, ficha de la entidad y Elevación.
void Ventana::construirPanel()
{
    auto *dock = new QDockWidget(tr("Capas y entidades"), this);
    m_dock = dock;
    dock->setObjectName(QStringLiteral("panel"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setMinimumWidth(340);

    auto *cont = new QWidget(dock);
    auto *caja = new QVBoxLayout(cont);
    caja->setContentsMargins(6, 6, 6, 6);

    // --- Arbol de capas y entidades ---------------------------------
    // Tres grupos fijos: «Base» (datasets de teselas, selección exclusiva),
    // «Overlays» (vectores de solo lectura, ocultos al iniciar) y
    // «Entidades» (puntos/líneas/polígonos del usuario, editables).
    caja->addWidget(new QLabel(tr("<b>Capas</b> — marca para mostrar/ocultar")));
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

    // === FICHA CONTEXTUAL de la entidad seleccionada =====================
    // Oculta mientras no hay nada seleccionado (panel limpio, solo el árbol).
    // Aparece al seleccionar con: título + Nombre/Tipo/Descripción + Capa, y
    // tres secciones plegables (Atributos, Coordenadas, Estilo).
    m_ficha = new QWidget(cont);
    auto *fichaCaja = new QVBoxLayout(m_ficha);
    fichaCaja->setContentsMargins(0, 6, 0, 0);
    fichaCaja->setSpacing(6);

    m_lblFicha = new QLabel(tr("Entidad"), m_ficha);
    m_lblFicha->setObjectName(QStringLiteral("fichaTitulo"));
    fichaCaja->addWidget(m_lblFicha);

    auto *form = new QFormLayout;
    m_campoNombre = new QLineEdit(m_ficha);
    m_campoTipo = new QLineEdit(m_ficha);
    m_campoDescripcion = new QLineEdit(m_ficha);
    m_lblCapaEntidad = new QLabel(m_ficha);
    m_lblCapaEntidad->setObjectName(QStringLiteral("fichaCapa"));
    m_campoTipo->setPlaceholderText(tr("zona_prohibida, punto_interes..."));
    form->addRow(tr("Nombre:"), m_campoNombre);
    form->addRow(tr("Tipo:"), m_campoTipo);
    form->addRow(tr("Descripción:"), m_campoDescripcion);
    form->addRow(tr("Capa:"), m_lblCapaEntidad);
    fichaCaja->addLayout(form);

    // --- Sección plegable: Atributos ---
    auto *secAtr = new SeccionPlegable(tr("Atributos"), m_ficha);
    auto *vAtr = new QVBoxLayout(secAtr->contenido());
    vAtr->setContentsMargins(0, 0, 0, 0);
    m_tablaAtributos = new QTableWidget(0, 2, secAtr);
    m_tablaAtributos->setHorizontalHeaderLabels({tr("Clave"), tr("Valor")});
    m_tablaAtributos->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tablaAtributos->setMaximumHeight(110);
    vAtr->addWidget(m_tablaAtributos);
    auto *filaAtr = new QHBoxLayout;
    auto *btnAddAtr = new QPushButton(tr("+ Atributo"), secAtr);
    auto *btnDelAtr = new QPushButton(tr("− Atributo"), secAtr);
    filaAtr->addWidget(btnAddAtr); filaAtr->addWidget(btnDelAtr);
    vAtr->addLayout(filaAtr);
    fichaCaja->addWidget(secAtr);

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

    // --- Sección plegable: Coordenadas (lat/lon editables) ---
    // Solo para entidades de usuario (capa editable); datasets y overlays no
    // se tocan. Editar una celda = moveVertex; +/- = insert/removeVertex.
    auto *secCoord = new SeccionPlegable(tr("Coordenadas"), m_ficha);
    auto *vCoord = new QVBoxLayout(secCoord->contenido());
    vCoord->setContentsMargins(0, 0, 0, 0);
    m_tablaCoords = new QTableWidget(0, 2, secCoord);
    m_tablaCoords->setHorizontalHeaderLabels({tr("Latitud"), tr("Longitud")});
    m_tablaCoords->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tablaCoords->setMaximumHeight(140);
    vCoord->addWidget(m_tablaCoords);
    auto *filaVert = new QHBoxLayout;
    m_btnAddVert = new QPushButton(tr("+ Vértice"), secCoord);
    m_btnDelVert = new QPushButton(tr("− Vértice"), secCoord);
    filaVert->addWidget(m_btnAddVert); filaVert->addWidget(m_btnDelVert);
    vCoord->addLayout(filaVert);
    fichaCaja->addWidget(secCoord);

    connect(m_tablaCoords, &QTableWidget::cellChanged, this, &Ventana::coordEditada);
    connect(m_btnAddVert, &QPushButton::clicked, this, &Ventana::anadirVertice);
    connect(m_btnDelVert, &QPushButton::clicked, this, &Ventana::quitarVertice);

    // --- Sección plegable: Estilo ---
    auto *secEstilo = new SeccionPlegable(tr("Estilo"), m_ficha);
    auto *formEstilo = new QFormLayout(secEstilo->contenido());
    formEstilo->setContentsMargins(0, 0, 0, 0);
    m_btnColorLinea = new QPushButton(secEstilo);
    m_btnColorRelleno = new QPushButton(secEstilo);
    formEstilo->addRow(tr("Color de línea:"), m_btnColorLinea);
    formEstilo->addRow(tr("Color de relleno:"), m_btnColorRelleno);

    m_anchoLinea = new QDoubleSpinBox(secEstilo);
    m_anchoLinea->setRange(0.5, 12.0);
    m_anchoLinea->setSingleStep(0.5);
    formEstilo->addRow(tr("Ancho:"), m_anchoLinea);

    m_estiloLinea = new QComboBox(secEstilo);
    m_estiloLinea->addItem(tr("Continua"),   static_cast<int>(Qt::SolidLine));
    m_estiloLinea->addItem(tr("Discontinua"),static_cast<int>(Qt::DashLine));
    m_estiloLinea->addItem(tr("Punteada"),   static_cast<int>(Qt::DotLine));
    m_estiloLinea->addItem(tr("Raya-punto"), static_cast<int>(Qt::DashDotLine));
    formEstilo->addRow(tr("Trazo:"), m_estiloLinea);

    m_radioPunto = new QDoubleSpinBox(secEstilo);
    m_radioPunto->setRange(2.0, 30.0);
    formEstilo->addRow(tr("Radio del punto:"), m_radioPunto);

    m_verEtiqueta = new QCheckBox(tr("Mostrar etiqueta"), secEstilo);
    formEstilo->addRow(QString(), m_verEtiqueta);
    m_verVertices = new QCheckBox(tr("Mostrar vértices"), secEstilo);
    formEstilo->addRow(QString(), m_verVertices);
    m_aplicarAlTrazo = new QCheckBox(tr("Usar este estilo al dibujar"), secEstilo);
    m_aplicarAlTrazo->setChecked(true);
    formEstilo->addRow(QString(), m_aplicarAlTrazo);
    fichaCaja->addWidget(secEstilo);

    m_ficha->setVisible(false);     // contextual: oculta hasta seleccionar
    caja->addWidget(m_ficha);
    caja->addStretch(1);

    // El panel hace scroll si la ficha crece (ventana baja): se envuelve en
    // un QScrollArea para que nunca se corte el contenido.
    auto *scroll = new QScrollArea(dock);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(cont);

    // Dos pestañas en el panel lateral: «Capas» (lo de arriba) y «Elevación».
    m_tabs = new QTabWidget(dock);
    m_tabs->addTab(scroll, tr("Capas"));
    m_tabs->addTab(construirTabElevacion(), tr("Elevación"));
    dock->setWidget(m_tabs);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    // Los pines A/B solo se ven con la pestaña Elevación a la vista: se
    // actualizan al cambiar de pestaña o al mostrar/ocultar el panel.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { marcarPuntosActivos(); });
    connect(dock, &QDockWidget::visibilityChanged, this, [this] { marcarPuntosActivos(); });

    // --- Toggles del panel en el menú «Ver» (m_menuVer lo dejó la Fase A) -
    if (m_menuVer) {
        m_menuVer->addSeparator();
        QAction *accPanel = dock->toggleViewAction();
        accPanel->setText(tr("Panel de capas"));
        m_menuVer->addAction(accPanel);
        auto *accCapas = new QAction(tr("Ir a pestaña Capas"), this);
        connect(accCapas, &QAction::triggered, this, [this] {
            m_dock->show(); m_dock->raise();
            m_tabs->setCurrentIndex(0);
        });
        auto *accElev = new QAction(tr("Ir a pestaña Elevación"), this);
        connect(accElev, &QAction::triggered, this, [this] {
            m_dock->show(); m_dock->raise();
            m_tabs->setCurrentIndex(1);
        });
        m_menuVer->addAction(accCapas);
        m_menuVer->addAction(accElev);
    }

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
QGeoCoordinate Ventana::centroideDe(const MapFeature &f)
{
    double sLa = 0, sLo = 0; int n = 0;
    for (const QVector<QGeoCoordinate> &parte : f.outlines())
        for (const QGeoCoordinate &c : parte)
            if (c.isValid()) { sLa += c.latitude(); sLo += c.longitude(); ++n; }
    return n ? QGeoCoordinate(sLa / n, sLo / n) : QGeoCoordinate();
}

// Centra el mapa en la entidad de una hoja del árbol y la deja seleccionada.
void Ventana::centrarEnItem(QTreeWidgetItem *item)
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

// Rehace el árbol de capas y entidades a partir del estado del mapa (lo pide pedirReconstruir, agrupado).
void Ventana::reconstruirPanel()
{
    if (!m_arbol)
        return;

    const qint64 sel = m_mapa->selectedFeature();
    const QString activa = m_mapa->activeFeatureLayer();

    QSignalBlocker sinSenales(m_arbol);
    m_arbol->clear();

    // --- Grupo «Base»: los datasets de teselas (uno visible a la vez) -----
    // Casillas con comportamiento de radio: marcar una fija la capa base y
    // desmarca las demás (lo resuelve visibilidadCambiada). No son editables.
    QTreeWidgetItem *gBase = nuevoGrupo(tr("Base (mapa)"));
    const QString baseActual = m_mapa->baseLayerId();
    for (const BaseLayerInfo &b : m_mapa->availableBaseLayers()) {
        auto *it = new QTreeWidgetItem(gBase);
        it->setText(0, b.displayName.isEmpty() ? b.id : b.displayName);
        it->setData(0, RolBase, b.id);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                     | Qt::ItemIsUserCheckable);
        it->setCheckState(0, b.id == baseActual ? Qt::Checked : Qt::Unchecked);

        // Una base vectorial (MBTiles pbf) trae sus propias capas: nombres de
        // calles, puntos de interés, edificios... Cada una, una casilla debajo.
        for (const BaseLayerGroup &g : m_mapa->baseLayerGroups(b.id)) {
            auto *hg = new QTreeWidgetItem(it);
            hg->setText(0, g.title);
            hg->setData(0, RolGrupoBase, g.id);
            hg->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            hg->setCheckState(0, g.visible ? Qt::Checked : Qt::Unchecked);
        }
        if (it->childCount() > 0)
            it->setExpanded(b.id == baseActual);
    }

    // --- Capas vectoriales: overlays (no editables) vs entidades ----------
    // featureLayers() viene por zOrder ascendente; se invierte para que
    // arriba en el panel sea arriba en el mapa.
    QTreeWidgetItem *gOver = nuevoGrupo(tr("Overlays (solo lectura)"));
    QTreeWidgetItem *gEnt  = nuevoGrupo(tr("Entidades (editables)"));
    auto capas = m_mapa->featureLayers();
    std::reverse(capas.begin(), capas.end());

    // Mapa híbrido: el MBTiles vectorial encima de la base, arriba del todo de
    // los overlays, con sus grupos (calles, lugares, POI...) como casillas.
    if (m_mapa->hasVectorOverlay()) {
        auto *hib = new QTreeWidgetItem(gOver);
        hib->setText(0, tr("Híbrido: %1").arg(QFileInfo(m_mapa->vectorOverlayFile()).completeBaseName()));
        hib->setData(0, RolHibrido, QStringLiteral("*"));
        hib->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        hib->setCheckState(0, m_mapa->isVectorOverlayVisible() ? Qt::Checked : Qt::Unchecked);
        for (const BaseLayerGroup &g : m_mapa->vectorOverlayGroups()) {
            auto *hg = new QTreeWidgetItem(hib);
            hg->setText(0, g.title);
            hg->setData(0, RolHibrido, g.id);
            hg->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            hg->setCheckState(0, g.visible ? Qt::Checked : Qt::Unchecked);
        }
        hib->setExpanded(true);
    }

    for (const LayerInfo &c : capas) {
        QTreeWidgetItem *grupo = c.editable ? gEnt : gOver;
        auto *nodo = new QTreeWidgetItem(grupo);
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

        // Las entidades del usuario se despliegan hoja a hoja; los overlays
        // NO (pueden ser miles de polígonos: solo se muestra el recuento).
        if (c.editable) {
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
    }

    gBase->setExpanded(true);
    gOver->setExpanded(true);
    gEnt->setExpanded(true);
    actualizarEstado();
}

// Crea una cabecera de grupo del árbol de capas (no seleccionable ni con
// casilla; solo organiza visualmente las tres categorías).
QTreeWidgetItem *Ventana::nuevoGrupo(const QString &titulo)
{
    auto *g = new QTreeWidgetItem(m_arbol);
    g->setText(0, titulo);
    g->setFlags(Qt::ItemIsEnabled);           // ni casilla ni selección
    g->setFirstColumnSpanned(true);
    QFont f = g->font(0);
    f.setBold(true);
    g->setFont(0, f);
    // Leve banda de fondo por grupo (color derivado del tema), para separar
    // visualmente Base / Overlays / Entidades.
    const QColor banda = palette().color(QPalette::AlternateBase);
    for (int col = 0; col < m_arbol->columnCount(); ++col)
        g->setBackground(col, banda);
    return g;
}

// Al elegir un elemento del árbol: activa su capa y, si es entidad, la selecciona en el mapa.
void Ventana::seleccionEnArbol(QTreeWidgetItem *item)
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
void Ventana::seleccionEnMapa(qint64 id)
{
    mostrarPropiedades(id);
    mostrarCoordsArea(id);
    actualizarEstado();
    if (!m_arbol)
        return;

    QSignalBlocker sinSenales(m_arbol);
    if (id < 0) {
        m_arbol->setCurrentItem(nullptr);
        return;
    }
    // Las hojas de entidad cuelgan de grupo→capa→entidad: se recorre todo.
    for (QTreeWidgetItemIterator it(m_arbol); *it; ++it) {
        if ((*it)->data(0, RolEntidad).toLongLong() == id) {
            m_arbol->setCurrentItem(*it);
            m_arbol->scrollToItem(*it);
            return;
        }
    }
}

// Casilla del árbol cambiada: capa base (como radio), capa fija/overlay o capa de entidades.
void Ventana::visibilidadCambiada(QTreeWidgetItem *item, int col)
{
    if (col != 0)
        return;

    // Mapa híbrido: la casilla del overlay entero ("*") o la de uno de sus grupos.
    const QString hibrido = item->data(0, RolHibrido).toString();
    if (!hibrido.isEmpty()) {
        const bool on = item->checkState(0) == Qt::Checked;
        if (hibrido == QLatin1String("*"))
            m_mapa->setVectorOverlayVisible(on);
        else
            m_mapa->setVectorOverlayGroupVisible(hibrido, on);
        return;
    }

    // Una capa propia de una base vectorial (nombres de calles, POI...): cuelga
    // de su capa base, que dice de qué capa es.
    const QString grupo = item->data(0, RolGrupoBase).toString();
    if (!grupo.isEmpty()) {
        const QString de = item->parent() ? item->parent()->data(0, RolBase).toString() : QString();
        m_mapa->setBaseLayerGroupVisible(grupo, item->checkState(0) == Qt::Checked, de);
        return;
    }

    // Grupo «Base»: casillas con comportamiento de radio. Marcar una fija la
    // capa base; desmarcar la activa no hace nada (siempre hay un mapa), así
    // que se vuelve a marcar.
    const QString base = item->data(0, RolBase).toString();
    if (!base.isEmpty()) {
        QSignalBlocker sinSenales(m_arbol);
        if (item->checkState(0) != Qt::Checked) {
            item->setCheckState(0, Qt::Checked);   // no se puede apagar el mapa
            return;
        }
        m_mapa->setBaseLayerId(base);
        if (m_capasBase)
            m_capasBase->setCurrentIndex(m_capasBase->findData(base));
        // Desmarcar las demás bases del mismo grupo.
        if (QTreeWidgetItem *g = item->parent())
            for (int i = 0; i < g->childCount(); ++i)
                if (QTreeWidgetItem *h = g->child(i); h != item)
                    h->setCheckState(0, Qt::Unchecked);
        return;
    }

    // Capas de overlays/entidades: casilla = visibilidad de la capa.
    const QString capa = item->data(0, RolCapa).toString();
    if (!capa.isEmpty() && item->data(0, RolEntidad).toLongLong() == 0)
        m_mapa->setFeatureLayerVisible(capa, item->checkState(0) == Qt::Checked);
}

// Marca en el árbol la base activa (cuando cambia desde la barra o el menú,
// que no reconstruyen el panel). Solo toca las casillas del grupo «Base».
void Ventana::sincronizarBaseEnArbol(const QString &id)
{
    if (!m_arbol)
        return;
    QSignalBlocker sinSenales(m_arbol);
    for (QTreeWidgetItemIterator it(m_arbol); *it; ++it) {
        const QString base = (*it)->data(0, RolBase).toString();
        if (!base.isEmpty())
            (*it)->setCheckState(0, base == id ? Qt::Checked : Qt::Unchecked);
    }
}

// Pone en negrita en el árbol la capa de entidades activa.
void Ventana::resaltarCapaActiva()
{
    const QString activa = m_mapa->activeFeatureLayer();
    // Las capas cuelgan de las cabeceras de grupo: se recorre el árbol y se
    // pone en negrita la capa activa (sin tocar las cabeceras, sin RolCapa).
    for (QTreeWidgetItemIterator it(m_arbol); *it; ++it) {
        const QString capa = (*it)->data(0, RolCapa).toString();
        if (capa.isEmpty() || (*it)->data(0, RolEntidad).toLongLong() != 0)
            continue;
        QFont f = (*it)->font(0);
        f.setBold(capa == activa);
        (*it)->setFont(0, f);
    }
}

// Borra la entidad seleccionada (se puede deshacer).
void Ventana::borrarSeleccion()
{
    const qint64 id = m_mapa->selectedFeature();
    if (id < 0) {
        statusBar()->showMessage(tr("No hay ninguna entidad seleccionada"), 3000);
        return;
    }
    m_mapa->removeFeature(id);           // el panel se refresca por senal
    marcarSucio();
    statusBar()->showMessage(tr("Entidad %1 borrada").arg(id), 3000);
}

// Vacía la capa activa tras confirmarlo.
void Ventana::vaciarCapa()
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
    marcarSucio();
    statusBar()->showMessage(tr("Capa '%1' vaciada").arg(capa), 3000);
}

// Pide un nombre y crea una capa de entidades nueva, que pasa a ser la activa.
void Ventana::crearCapa()
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
    marcarSucio();
    resaltarCapaActiva();
}

// Borra la capa activa con sus entidades, tras confirmarlo.
void Ventana::borrarCapa()
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
    marcarSucio();
}

// Sube o baja la capa activa un puesto en el orden de dibujo (\a direccion = ±1).
void Ventana::moverCapa(int direccion)
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
        marcarSucio();
        return;
    }
}

// Estilo que marcan ahora los controles de la ficha (colores, ancho, trazo, etiqueta).
FeatureStyle Ventana::estiloActual() const
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

// Aplica el estilo de los controles a la entidad seleccionada (y al trazo si se pide).
void Ventana::aplicarEstilo()
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
    marcarSucio();
}

// Copia nombre, tipo, descripción y atributos de la ficha a la entidad seleccionada.
void Ventana::aplicarPropiedades()
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
    marcarSucio();
}

// Vuelca la entidad seleccionada a los campos del panel.
void Ventana::mostrarPropiedades(qint64 id)
{
    const auto f = m_mapa->feature(id);

    // Sin este guardia, rellenar los campos dispararia editingFinished y
    // se reescribiria la entidad con lo que acabamos de leer.
    m_actualizandoPropiedades = true;

    const bool hay = f.has_value();
    // Panel contextual: la ficha solo se ve cuando hay algo seleccionado.
    if (m_ficha)
        m_ficha->setVisible(hay);
    if (hay && m_lblFicha) {
        const QString nombre = f->name.isEmpty()
            ? tr("(sin nombre) #%1").arg(f->id) : f->name;
        m_lblFicha->setText(f->type.isEmpty()
            ? nombre : QStringLiteral("%1  ·  %2").arg(nombre, f->type));
    }
    if (hay && m_lblCapaEntidad)
        m_lblCapaEntidad->setText(f->layerId);

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
    mostrarCoords(id);
    m_actualizandoPropiedades = false;
}

// ¿La capa de esta entidad es editable? (Las de usuario sí; las de overlays
// del paquete, no.) Decide si se pueden tocar las coordenadas.
bool Ventana::capaEditable(const QString &layerId) const
{
    for (const LayerInfo &c : m_mapa->featureLayers())
        if (c.id == layerId)
            return c.editable;
    return false;
}

// Vuelca los vértices de la entidad seleccionada a la tabla de coordenadas.
// Reutiliza el guardia m_actualizandoPropiedades para no reescribir al
// rellenar. Si la entidad no es editable, la tabla queda de solo lectura.
void Ventana::mostrarCoords(qint64 id)
{
    const bool prev = m_actualizandoPropiedades;
    m_actualizandoPropiedades = true;

    m_tablaCoords->setRowCount(0);
    const auto f = id >= 0 ? m_mapa->feature(id) : std::nullopt;
    // Solo geometría de una sola parte (punto/línea/polígono de usuario). Las
    // multiparte (p. ej. .geo) no se editan vértice a vértice aquí.
    const bool editable = f && !f->isMultiPart() && capaEditable(f->layerId);

    if (f && !f->isMultiPart()) {
        for (int i = 0; i < f->geometry.size(); ++i) {
            const QGeoCoordinate &c = f->geometry[i];
            m_tablaCoords->insertRow(i);
            m_tablaCoords->setItem(i, 0, celdaCoord(c.latitude()));
            m_tablaCoords->setItem(i, 1, celdaCoord(c.longitude()));
        }
    }
    m_tablaCoords->setEnabled(editable);
    // En un punto no hay vértices que añadir/quitar; en línea/polígono sí.
    const bool esMulti = f && (f->kind == GeometryKind::Polyline
                               || f->kind == GeometryKind::Polygon);
    m_btnAddVert->setEnabled(editable && esMulti);
    m_btnDelVert->setEnabled(editable && esMulti);

    m_actualizandoPropiedades = prev;
}

// Crea una celda con una coordenada formateada a 6 decimales.
QTableWidgetItem *Ventana::celdaCoord(double v)
{
    return new QTableWidgetItem(QString::number(v, 'f', 6));
}

// Una celda de lat/lon cambió: mueve ese vértice (moveVertex).
void Ventana::coordEditada(int fila, int /*col*/)
{
    if (m_actualizandoPropiedades)
        return;
    const qint64 id = m_mapa->selectedFeature();
    if (id < 0)
        return;
    auto *itLat = m_tablaCoords->item(fila, 0);
    auto *itLon = m_tablaCoords->item(fila, 1);
    if (!itLat || !itLon)
        return;
    bool okLa = false, okLo = false;
    const double la = itLat->text().toDouble(&okLa);
    const double lo = itLon->text().toDouble(&okLo);
    if (!okLa || !okLo || la < -90 || la > 90 || lo < -180 || lo > 180) {
        statusBar()->showMessage(
            tr("Coordenada inválida (lat -90..90, lon -180..180)."), 4000);
        mostrarCoords(id);   // restaura el valor bueno
        return;
    }
    if (m_mapa->moveVertex(id, fila, QGeoCoordinate(la, lo)))
        marcarSucio();
    else
        statusBar()->showMessage(tr("No se pudo mover el vértice."), 4000);
}

// Inserta un vértice tras el seleccionado, en el punto medio hacia el
// siguiente (en polígono cierra el anillo; en línea usa el vecino que haya).
void Ventana::anadirVertice()
{
    const qint64 id = m_mapa->selectedFeature();
    if (id < 0)
        return;
    const auto f = m_mapa->feature(id);
    if (!f || f->isMultiPart() || f->geometry.isEmpty())
        return;
    const int n = static_cast<int>(f->geometry.size());
    int fila = m_tablaCoords->currentRow();
    if (fila < 0)
        fila = n - 1;                      // sin selección: tras el último
    int vecino;
    if (f->kind == GeometryKind::Polygon)
        vecino = (fila + 1) % n;           // cierra el anillo
    else
        vecino = (fila + 1 < n) ? fila + 1 : fila - 1;
    if (vecino < 0)
        vecino = fila;                     // geometría de un solo punto
    const QGeoCoordinate &a = f->geometry[fila];
    const QGeoCoordinate &b = f->geometry[vecino];
    const QGeoCoordinate medio((a.latitude() + b.latitude()) / 2.0,
                               (a.longitude() + b.longitude()) / 2.0);
    if (m_mapa->insertVertex(id, fila + 1, medio)) {
        marcarSucio();
        mostrarCoords(id);                 // cambió el nº de filas
    } else {
        statusBar()->showMessage(tr("No se pudo insertar el vértice."), 4000);
    }
}

// Quita el vértice seleccionado (la librería rechaza bajar del mínimo:
// 2 en línea, 3 en polígono).
void Ventana::quitarVertice()
{
    const qint64 id = m_mapa->selectedFeature();
    if (id < 0)
        return;
    const int fila = m_tablaCoords->currentRow();
    if (fila < 0) {
        statusBar()->showMessage(tr("Elige primero un vértice en la tabla."), 4000);
        return;
    }
    if (m_mapa->removeVertex(id, fila)) {
        marcarSucio();
        mostrarCoords(id);
    } else {
        statusBar()->showMessage(
            tr("No se puede quitar: la geometría quedaría por debajo del "
               "mínimo de vértices."), 4000);
    }
}

// Pinta el botón \a boton con el color \a c para que se vea el color elegido.
void Ventana::pintarBotonColor(QPushButton *boton, const QColor &c)
{
    boton->setStyleSheet(QStringLiteral(
        "background-color: rgba(%1,%2,%3,%4); min-height: 20px;")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
    boton->setText(c.name());
}

// Abre el selector de color y guarda el elegido en \a destino.
void Ventana::elegirColor(QColor *destino, QPushButton *boton)
{
    const QColor c = QColorDialog::getColor(*destino, this, tr("Color"),
                                            QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return;
    *destino = c;
    pintarBotonColor(boton, c);
    aplicarEstilo();
}
