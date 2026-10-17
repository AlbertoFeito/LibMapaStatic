// Menús, barras de herramientas y temas claro/oscuro.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

using namespace libmapa;

// Crea una acción de herramienta (grupo exclusivo de dibujo/navegación).
QAction *Ventana::accHerr(const QString &texto, MapTool tool,
                 const QString &atajo,
                 const QString &claveIcono)
{
    auto *a = new QAction(texto, this);
    a->setCheckable(true);
    a->setData(static_cast<int>(tool));
    if (!atajo.isEmpty())
        a->setShortcut(QKeySequence(atajo));
    if (!claveIcono.isEmpty())
        a->setProperty("glifo", claveIcono);   // lo recolorea aplicarIconos
    m_grupo->addAction(a);
    return a;
}

// Barra de menú + una barra de herramientas principal + una secundaria de
// datos/terreno. Todo a partir de acciones compartidas (un solo origen).
void Ventana::construirMenuYBarra()
{
    m_grupo = new QActionGroup(this);
    m_grupo->setExclusive(true);

    // --- Acciones de navegación y dibujo (grupo exclusivo) ---------------
    m_accNavegar  = accHerr(tr("Navegar"), MapTool::None, QString(), QStringLiteral("navegar"));
    m_accNavegar->setChecked(true);
    m_accMedir    = accHerr(tr("Medir"), MapTool::Measure, QString(), QStringLiteral("medir"));
    m_accMedirArea = accHerr(tr("Medir superficie"), MapTool::MeasureArea, QString(),
                             QStringLiteral("medirarea"));
    m_accZoomArea = accHerr(tr("Zoom área"), MapTool::AreaZoom, QString(), QStringLiteral("zoom"));
    m_accPunto    = accHerr(tr("Punto"), MapTool::DrawPoint, QStringLiteral("1"), QStringLiteral("punto"));
    m_accLinea    = accHerr(tr("Línea"), MapTool::DrawPolyline, QStringLiteral("2"), QStringLiteral("linea"));
    m_accPoligono = accHerr(tr("Polígono"), MapTool::DrawPolygon, QStringLiteral("3"), QStringLiteral("poligono"));
    m_accEditar   = accHerr(tr("Editar"), MapTool::EditFeature, QStringLiteral("4"), QStringLiteral("editar"));
    m_accObjetivo = accHerr(tr("Objetivo"), MapTool::DrawPoint, QString(), QStringLiteral("objetivo"));
    m_accArea     = accHerr(tr("Área objetivo"), MapTool::DrawPolygon, QString(), QStringLiteral("area"));
    for (QAction *a : {m_accPunto, m_accLinea, m_accPoligono})
        connect(a, &QAction::triggered, this, &Ventana::nuevaEntidadGenerica);
    connect(m_accObjetivo, &QAction::triggered, this, &Ventana::modoObjetivo);
    connect(m_accArea, &QAction::triggered, this, &Ventana::modoAreaObjetivo);

    // Ayudas en barra/menú: qué hace cada herramienta (y la diferencia entre
    // Polígono genérico y Área objetivo, que no es obvia).
    m_accNavegar->setStatusTip(tr("Mover el mapa; arrastrar etiquetas y pines."));
    m_accMedir->setStatusTip(tr("Medir distancia y marcación entre dos puntos."));
    m_accMedirArea->setStatusTip(tr("Medir superficie y perímetro: marca el polígono clic "
                                    "a clic (la medida sale en vivo); doble clic, clic "
                                    "derecho o Intro lo cierra."));
    m_accZoomArea->setStatusTip(tr("Ampliar arrastrando un rectángulo."));
    m_accPunto->setStatusTip(tr("Dibujar un punto (pide nombre, capa y colores)."));
    m_accLinea->setStatusTip(tr("Dibujar una línea; doble clic para cerrar."));
    m_accPoligono->setStatusTip(tr("Polígono genérico sin límite de vértices."));
    m_accObjetivo->setStatusTip(tr("Punto «objetivo»: etiqueta movible unida por "
                                   "una línea guía."));
    m_accArea->setStatusTip(tr("Polígono «objetivo»: hasta 10 vértices, etiqueta "
                               "y selección en el primer punto."));
    m_accEditar->setStatusTip(tr("Seleccionar y editar: mover vértices, insertar "
                                 "(doble clic en un lado) o borrar (Supr)."));
    for (QAction *a : {m_accNavegar, m_accMedir, m_accMedirArea, m_accZoomArea, m_accPunto, m_accLinea,
                       m_accPoligono, m_accObjetivo, m_accArea, m_accEditar})
        a->setToolTip(a->statusTip());

    // --- Acciones de archivo / edición -----------------------------------
    m_accGuardar = new QAction(tr("Guardar entidades"), this);
    m_accGuardar->setShortcut(QKeySequence::Save);
    connect(m_accGuardar, &QAction::triggered, this, &Ventana::guardar);
    m_accGuardarComo = new QAction(tr("Guardar entidades como..."), this);
    connect(m_accGuardarComo, &QAction::triggered, this, &Ventana::guardarComo);
    m_accAbrir = new QAction(tr("Abrir entidades..."), this);
    m_accAbrir->setShortcut(QKeySequence::Open);
    connect(m_accAbrir, &QAction::triggered, this, &Ventana::abrir);
    auto *accSalir = new QAction(tr("Salir"), this);
    accSalir->setShortcut(QKeySequence::Quit);
    connect(accSalir, &QAction::triggered, this, &QWidget::close);

    m_accDeshacer = new QAction(tr("Deshacer"), this);
    m_accDeshacer->setShortcut(QKeySequence::Undo);
    connect(m_accDeshacer, &QAction::triggered, this, [this] { m_mapa->undo(); });
    m_accRehacer = new QAction(tr("Rehacer"), this);
    m_accRehacer->setShortcut(QKeySequence::Redo);
    connect(m_accRehacer, &QAction::triggered, this, [this] { m_mapa->redo(); });
    m_accBorrar = new QAction(tr("Borrar entidad"), this);
    m_accBorrar->setShortcut(QKeySequence::Delete);
    connect(m_accBorrar, &QAction::triggered, this, &Ventana::borrarSeleccion);

    // --- Acciones de vista -----------------------------------------------
    auto *accAmpliar = new QAction(tr("Ampliar"), this);
    connect(accAmpliar, &QAction::triggered, m_mapa, &MapWidget::zoomIn);
    auto *accReducir = new QAction(tr("Reducir"), this);
    connect(accReducir, &QAction::triggered, m_mapa, &MapWidget::zoomOut);
    auto *accCuba = new QAction(tr("Toda Cuba"), this);
    connect(accCuba, &QAction::triggered, this, [this] {
        m_mapa->fitBounds(QGeoCoordinate(23.3, -85.0), QGeoCoordinate(19.7, -74.0));
    });
    m_accRejilla = new QAction(tr("Rejilla"), this);
    m_accRejilla->setCheckable(true);
    connect(m_accRejilla, &QAction::toggled, m_mapa, &MapWidget::setDebugGridVisible);
    // Escala gráfica y cuadrícula de coordenadas (capas de la librería). La
    // escala arranca encendida; la cuadrícula, a petición.
    m_accEscala = new QAction(tr("Escala gráfica"), this);
    m_accEscala->setCheckable(true);
    connect(m_accEscala, &QAction::toggled, m_mapa, &MapWidget::setScaleBarVisible);
    m_accEscala->setChecked(true);
    m_accEscalaNm = new QAction(tr("Escala en millas náuticas"), this);
    m_accEscalaNm->setCheckable(true);
    connect(m_accEscalaNm, &QAction::toggled, this, [this](bool nm) {
        m_mapa->setScaleBarUnits(nm ? ScaleUnits::Nautical : ScaleUnits::Metric);
    });
    m_accCuadricula = new QAction(tr("Cuadrícula de coordenadas"), this);
    m_accCuadricula->setCheckable(true);
    connect(m_accCuadricula, &QAction::toggled, m_mapa, &MapWidget::setGraticuleVisible);
    // Sobre la capa satelital (oscura) la cuadrícula va en blanco.
    auto colorCuadricula = [this](const QString &capa) {
        const bool oscura = capa.contains(QLatin1String("sat"), Qt::CaseInsensitive);
        m_mapa->setGraticuleColor(oscura ? QColor(255, 255, 255, 200)
                                         : QColor(20, 40, 60, 150));
    };
    colorCuadricula(m_mapa->baseLayerId());
    connect(m_mapa, &MapWidget::baseLayerChanged, this, colorCuadricula);

    m_accCobertura = new QAction(tr("Cobertura"), this);
    m_accCobertura->setCheckable(true);
    m_accCobertura->setToolTip(tr("Mancha de zonas con teselas del zoom elegido."));
    connect(m_accCobertura, &QAction::toggled, this, [this](bool on) {
        m_mapa->setCoverageZoom(m_zCobertura->value());
        m_mapa->setCoverageVisible(on);
    });

    // --- Widgets de apoyo (viven en las barras) --------------------------
    m_capasBase = new QComboBox(this);
    for (const BaseLayerInfo &c : m_mapa->availableBaseLayers())
        m_capasBase->addItem(c.displayName, c.id);
    m_capasBase->setCurrentIndex(m_capasBase->findData(m_mapa->baseLayerId()));

    m_zCobertura = new QSpinBox(this);
    m_zCobertura->setRange(0, 22);
    m_zCobertura->setValue(14);
    connect(m_zCobertura, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        if (m_accCobertura->isChecked()) m_mapa->setCoverageZoom(v);
    });

    // Cargar la elevacion (DEM): las mismas dos acciones en el boton «DEM...» de
    // la barra y en los menus Datos y Elevación, para que se encuentren donde se
    // espera (antes solo estaban en el boton y «Datos» parecia leer solo .geo).
    m_accDemCarpeta = new QAction(tr("Carpeta de GeoTIFF o .hgt..."), this);
    connect(m_accDemCarpeta, &QAction::triggered, this, &Ventana::alElegirDemCarpeta);
    m_accDemFichero = new QAction(tr("Fichero GeoTIFF o base de datos .sqlitedb..."), this);
    connect(m_accDemFichero, &QAction::triggered, this, &Ventana::alElegirDemDb);
    m_btnDem = new QPushButton(tr("DEM..."), this);
    {
        QMenu *md = new QMenu(m_btnDem);
        md->addAction(m_accDemCarpeta);
        md->addAction(m_accDemFichero);
        m_btnDem->setMenu(md);
    }

    auto *accGeo = new QAction(tr("Cargar .geo..."), this);
    connect(accGeo, &QAction::triggered, this, &Ventana::cargarGeo);
    // Intercambio con otras herramientas: GeoJSON / KML / GPX.
    auto *accImportar = new QAction(tr("Importar GeoJSON/KML/GPX..."), this);
    connect(accImportar, &QAction::triggered, this, &Ventana::importarVectorial);
    auto *accExportar = new QAction(tr("Exportar capa activa a GeoJSON/KML/GPX..."), this);
    connect(accExportar, &QAction::triggered, this, &Ventana::exportarVectorial);

    m_numObjetivos = new QSpinBox(this);
    m_numObjetivos->setRange(1, 5000);
    m_numObjetivos->setValue(250);
    m_accSimular = new QAction(tr("Simular objetivos"), this);
    m_accSimular->setCheckable(true);
    connect(m_accSimular, &QAction::toggled, this, &Ventana::alternarSimulacion);
    m_traza = new QComboBox(this);
    m_traza->addItem(tr("Sin traza"), 0);
    m_traza->addItem(tr("10"), 10);
    m_traza->addItem(tr("100"), 100);
    m_traza->addItem(tr("500"), 500);
    m_traza->addItem(tr("Toda"), -1);
    m_traza->setCurrentIndex(2);
    m_mapa->setTargetTrailLength(100);
    connect(m_traza, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
        m_mapa->setTargetTrailLength(m_traza->itemData(i).toInt());
    });
    m_simReloj = new QTimer(this);
    m_simReloj->setInterval(100);
    connect(m_simReloj, &QTimer::timeout, this, &Ventana::pasoSimulacion);

    // --- Acciones de elevación (reutilizan los slots de la pestaña) ------
    auto *accPerfil = new QAction(tr("Perfil de ruta"), this);
    connect(accPerfil, &QAction::triggered, this, &Ventana::analizarPerfil);
    auto *accVision = new QAction(tr("Visión A→B"), this);
    connect(accVision, &QAction::triggered, this, &Ventana::analizarVision);
    auto *accViewshed = new QAction(tr("Viewshed (ZVD)"), this);
    connect(accViewshed, &QAction::triggered, this, &Ventana::analizarViewshed);
    auto *accFijarA = new QAction(tr("Fijar punto A en el mapa"), this);
    connect(accFijarA, &QAction::triggered, this, [this] { iniciarPick(Pick::A); });
    auto *accFijarB = new QAction(tr("Fijar punto B en el mapa"), this);
    connect(accFijarB, &QAction::triggered, this, [this] { iniciarPick(Pick::B); });
    auto *accLimpiar = new QAction(tr("Limpiar análisis"), this);
    connect(accLimpiar, &QAction::triggered, this, &Ventana::limpiarAnalisis);

    // --- Selección de zona (bbox o polígono) SIN zoom -------------------
    // Marca un área o un polígono y la librería emite areaSelected /
    // polygonSelected (lo que usa fill_map para elegir qué teselas bajar);
    // aquí se informa de la zona en la barra de estado.
    m_accSelArea = accHerr(tr("Seleccionar área"), MapTool::SelectArea);
    m_accSelPoli = accHerr(tr("Seleccionar polígono (zona)"), MapTool::SelectPolygon);
    m_accSelArea->setStatusTip(
        tr("Marca una zona rectangular (clic en una esquina y clic en la opuesta) "
           "sin hacer zoom: dice qué hay dentro y deja crear con ella un área, "
           "vigilarla o exportarla."));
    m_accSelPoli->setStatusTip(
        tr("Marca una zona de cualquier forma, clic a clic (doble clic, clic "
           "derecho o Intro la cierran; Esc cancela): dice qué hay dentro y deja "
           "crear con ella un área, vigilarla o exportarla."));

    // Exportaciones nuevas: historial de objetivos (GPX con horas) y la
    // vista tal cual se ve (imagen o PDF con título, pie y escala).
    auto *accHistorialGpx = new QAction(tr("Exportar historial de objetivos (GPX)..."), this);
    accHistorialGpx->setStatusTip(
        tr("Guarda la traza completa de cada objetivo, desde su origen, con la hora "
           "de cada punto (se reproduce en QGIS o GPXSee)."));
    connect(accHistorialGpx, &QAction::triggered, this, &Ventana::exportarHistorialGpx);
    auto *accExportarVista = new QAction(tr("Exportar vista a imagen o PDF..."), this);
    accExportarVista->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    accExportarVista->setStatusTip(
        tr("Guarda el mapa tal cual se ve (PNG/JPG o PDF con título, pie y escala)."));
    connect(accExportarVista, &QAction::triggered, this, [this] { exportarVista(); });

    // Mostrar/ocultar los objetivos simulados (capacidad setTargetsVisible).
    m_accVerObjetivos = new QAction(tr("Mostrar objetivos"), this);
    m_accVerObjetivos->setCheckable(true);
    m_accVerObjetivos->setChecked(true);
    connect(m_accVerObjetivos, &QAction::toggled, this,
            [this](bool on) { m_mapa->setTargetsVisible(on); });

    // Alertas de zona (watchZone): vigila / deja de vigilar el polígono
    // seleccionado; las entradas y salidas se avisan en la barra de estado.
    m_accVigilarZona = new QAction(tr("Vigilar zona seleccionada"), this);
    m_accVigilarZona->setStatusTip(
        tr("Avisa cuando un objetivo entra o sale del polígono seleccionado "
           "(otra vez = dejar de vigilar)."));
    connect(m_accVigilarZona, &QAction::triggered, this, &Ventana::alternarVigilancia);

    // Alertas de proximidad (CPA/TCPA) entre objetivos: al marcarla pide la
    // distancia y el horizonte; la librería avisa y dibuja por dónde se cruzan.
    m_accProximidad = new QAction(tr("Alertas de proximidad (CPA)..."), this);
    m_accProximidad->setCheckable(true);
    m_accProximidad->setStatusTip(
        tr("Avisa cuando dos objetivos van a pasar a menos de una distancia "
           "dentro de un tiempo (punto de máximo acercamiento)."));
    connect(m_accProximidad, &QAction::triggered, this, &Ventana::configurarProximidad);

    // Glifos del resto de acciones de barra (los de dibujo ya los puso
    // accHerr). aplicarIconos() los pinta con el color del tema.
    m_accGuardar->setProperty("glifo", QStringLiteral("guardar"));
    m_accAbrir->setProperty("glifo", QStringLiteral("abrir"));
    m_accDeshacer->setProperty("glifo", QStringLiteral("deshacer"));
    m_accRehacer->setProperty("glifo", QStringLiteral("rehacer"));
    m_accBorrar->setProperty("glifo", QStringLiteral("borrar"));
    m_accRejilla->setProperty("glifo", QStringLiteral("rejilla"));
    m_accCobertura->setProperty("glifo", QStringLiteral("cobertura"));
    accGeo->setProperty("glifo", QStringLiteral("geo"));
    m_accSimular->setProperty("glifo", QStringLiteral("simular"));

    // --- Barra de menú ---------------------------------------------------
    QMenuBar *mb = menuBar();
    QMenu *mArchivo = mb->addMenu(tr("&Archivo"));
    mArchivo->addAction(m_accAbrir);
    mArchivo->addAction(m_accGuardar);
    mArchivo->addAction(m_accGuardarComo);
    mArchivo->addSeparator();
    mArchivo->addAction(accSalir);

    QMenu *mVer = mb->addMenu(tr("&Ver"));
    QMenu *mBase = mVer->addMenu(tr("Capa base"));
    auto *gBase = new QActionGroup(this);
    gBase->setExclusive(true);
    for (const BaseLayerInfo &c : m_mapa->availableBaseLayers()) {
        QAction *a = mBase->addAction(c.displayName);
        a->setCheckable(true);
        a->setData(c.id);
        a->setChecked(c.id == m_mapa->baseLayerId());
        gBase->addAction(a);
        connect(a, &QAction::triggered, this, [this, a] {
            const QString id = a->data().toString();
            m_mapa->setBaseLayerId(id);
            if (m_capasBase) m_capasBase->setCurrentIndex(m_capasBase->findData(id));
        });
    }
    mVer->addSeparator();
    mVer->addAction(accAmpliar);
    mVer->addAction(accReducir);
    mVer->addAction(accCuba);
    mVer->addSeparator();
    mVer->addAction(m_accEscala);
    mVer->addAction(m_accEscalaNm);
    mVer->addAction(m_accCuadricula);
    // Cuadrícula UTM en vez de la geográfica (las dos van por la misma acción).
    auto *accCuadriculaUtm = mVer->addAction(tr("Cuadrícula UTM"));
    accCuadriculaUtm->setCheckable(true);
    connect(accCuadriculaUtm, &QAction::toggled, this, [this](bool utm) {
        m_mapa->setGraticuleMode(utm ? GraticuleMode::Utm : GraticuleMode::Geographic);
        if (utm && !m_accCuadricula->isChecked())
            m_accCuadricula->setChecked(true);    // pedir UTM implica verla
    });
    // Formato de la coordenada bajo el cursor (barra de estado).
    QMenu *mFormato = mVer->addMenu(tr("Formato de coordenadas"));
    auto *gFormato = new QActionGroup(this);
    const QList<QPair<QString, CoordinateFormat>> formatos = {
        { tr("Decimal (23.11360, -82.36660)"), CoordinateFormat::Decimal },
        { tr("Grados, minutos, segundos"), CoordinateFormat::DMS },
        { tr("Grados y minutos decimales"), CoordinateFormat::DDM },
        { tr("UTM (17Q 360057 2556751)"), CoordinateFormat::UTM },
        { tr("MGRS (17Q LF 60056 56750)"), CoordinateFormat::MGRS },
    };
    for (const auto &f : formatos) {
        QAction *a = mFormato->addAction(f.first);
        a->setCheckable(true);
        a->setChecked(f.second == m_formatoCoord);
        gFormato->addAction(a);
        const CoordinateFormat fmt = f.second;
        connect(a, &QAction::triggered, this, [this, fmt] { m_formatoCoord = fmt; });
    }
    // Ir a una coordenada escrita en cualquier formato.
    connect(mVer->addAction(tr("Ir a coordenada...")), &QAction::triggered,
            this, &Ventana::irACoordenada);
    mVer->addSeparator();
    // Medidas que se quedan en el mapa: anillos de distancia (VRM) y
    // líneas de marcación (EBL), fijas o colgadas de objetivos.
    auto *accAnillos = mVer->addAction(tr("Anillos de distancia..."));
    accAnillos->setStatusTip(tr("Anillos alrededor de un objetivo o de un punto (los siguen); "
                                "en un lugar vacío se crea antes un punto (radar, observador)."));
    connect(accAnillos, &QAction::triggered, this, [this] { ponerAnillos(); });
    auto *accEbl = mVer->addAction(tr("Marcación (rumbo y distancia)"));
    accEbl->setStatusTip(tr("Línea con rumbo y distancia entre dos elementos: objetivos o puntos "
                            "(el origen es el seleccionado; el destino se pincha)."));
    connect(accEbl, &QAction::triggered, this, &Ventana::ponerMarcacion);
    // Inicio (verde) y fin (rojo) de las líneas: siempre en las rutas GPX
    // (p. ej. un historial importado), y en cualquier línea seleccionada.
    // Filtro de objetivos (por tipo, velocidad o zona): ocultar o atenuar
    // los que no interesan sin borrarlos.
    auto *accFiltro = mVer->addAction(tr("Filtrar objetivos..."));
    accFiltro->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    accFiltro->setStatusTip(tr("Mostrar solo los objetivos de ciertos tipos, velocidades "
                               "o dentro de zonas vigiladas."));
    connect(accFiltro, &QAction::triggered, this, &Ventana::filtrarObjetivos);
    // Agrupar objetivos: con cientos en pantalla, al alejar el zoom se
    // dibujan como círculos con el número; pinchar uno lo amplía.
    auto *accAgrupar = mVer->addAction(tr("Agrupar objetivos al alejar"));
    accAgrupar->setCheckable(true);
    accAgrupar->setStatusTip(tr("Hasta zoom 10, los objetivos amontonados se ven como "
                                "un círculo con su número (clic = ampliar)."));
    connect(accAgrupar, &QAction::toggled, this, [this](bool on) {
        TargetClustering c;
        c.enabled = on;
        c.maxZoom = 10;
        m_mapa->setTargetClustering(c);
    });
    accAgrupar->setChecked(true);
    mVer->addSeparator();
    QMenu *mExtremos = mVer->addMenu(tr("Inicio y fin de rutas"));
    auto *gExtremos = new QActionGroup(this);
    const QList<QPair<QString, RouteEndpoints>> extremos = {
        { tr("Ocultos"), RouteEndpoints::Hidden },
        { tr("Solo la línea seleccionada"), RouteEndpoints::Selected },
        { tr("Rutas GPX y la seleccionada"), RouteEndpoints::Routes },
        { tr("Todas las líneas"), RouteEndpoints::AllLines },
    };
    for (const auto &e : extremos) {
        QAction *a = mExtremos->addAction(e.first);
        a->setCheckable(true);
        a->setChecked(e.second == m_mapa->routeEndpoints());
        gExtremos->addAction(a);
        const RouteEndpoints modo = e.second;
        connect(a, &QAction::triggered, this, [this, modo] { m_mapa->setRouteEndpoints(modo); });
    }
    // Lista de anillos y marcaciones puestos: centrar, quitar uno o todos.
    auto *accMarcas = mVer->addAction(tr("Anillos y marcaciones puestos..."));
    accMarcas->setStatusTip(tr("Ver los anillos y marcaciones del mapa, ir a uno o quitarlos."));
    connect(accMarcas, &QAction::triggered, this, &Ventana::gestionarMarcas);
    mVer->addSeparator();
    mVer->addAction(m_accRejilla);
    mVer->addAction(m_accCobertura);

    // Submenú «Tema»: claro / oscuro, elegible por el usuario y recordado.
    QMenu *mTema = mVer->addMenu(tr("Tema"));
    auto *gTema = new QActionGroup(this);
    gTema->setExclusive(true);
    m_accTemaClaro = mTema->addAction(tr("Claro"));
    m_accTemaOscuro = mTema->addAction(tr("Oscuro"));
    for (QAction *a : {m_accTemaClaro, m_accTemaOscuro}) {
        a->setCheckable(true);
        gTema->addAction(a);
    }
    connect(m_accTemaClaro,  &QAction::triggered, this, [this] { aplicarTema(false); });
    connect(m_accTemaOscuro, &QAction::triggered, this, [this] { aplicarTema(true); });

    m_menuVer = mVer;   // la Fase B añade aquí los toggles de paneles

    QMenu *mDibujo = mb->addMenu(tr("&Dibujo"));
    mDibujo->addAction(m_accNavegar);
    mDibujo->addAction(m_accMedir);
    mDibujo->addAction(m_accMedirArea);
    mDibujo->addSeparator();
    mDibujo->addAction(m_accPunto);
    mDibujo->addAction(m_accLinea);
    mDibujo->addAction(m_accPoligono);
    mDibujo->addSeparator();
    mDibujo->addAction(m_accObjetivo);
    mDibujo->addAction(m_accArea);
    mDibujo->addSeparator();
    mDibujo->addAction(m_accEditar);
    mDibujo->addAction(m_accBorrar);
    mDibujo->addSeparator();
    mDibujo->addAction(m_accDeshacer);
    mDibujo->addAction(m_accRehacer);

    QMenu *mElev = mb->addMenu(tr("&Elevación"));
    mElev->addAction(m_accDemCarpeta);
    mElev->addAction(m_accDemFichero);
    mElev->addSeparator();
    mElev->addAction(accPerfil);
    mElev->addAction(accVision);
    mElev->addAction(accViewshed);
    mElev->addSeparator();
    mElev->addAction(accFijarA);
    mElev->addAction(accFijarB);
    mElev->addSeparator();
    mElev->addAction(accLimpiar);

    QMenu *mDatos = mb->addMenu(tr("&Datos"));
    mDatos->addAction(accGeo);
    QMenu *mDem = mDatos->addMenu(tr("Cargar elevación (DEM)"));
    mDem->addAction(m_accDemCarpeta);
    mDem->addAction(m_accDemFichero);
    mDatos->addAction(accImportar);
    mDatos->addAction(accExportar);
    mDatos->addAction(accExportarVista);
    mDatos->addSeparator();
    mDatos->addSection(tr("Consultar una zona"));
    mDatos->addAction(m_accSelArea);
    mDatos->addAction(m_accSelPoli);
    mDatos->addSeparator();
    mDatos->addAction(m_accSimular);
    mDatos->addAction(m_accVerObjetivos);
    mDatos->addAction(m_accVigilarZona);
    mDatos->addAction(m_accProximidad);
    mDatos->addAction(accHistorialGpx);
    auto *accReproducir = mDatos->addAction(tr("Reproducir historial (GPX)..."));
    accReproducir->setStatusTip(tr("Vuelve a ver cómo se movieron los objetivos de un "
                                   "historial exportado, con su hora."));
    connect(accReproducir, &QAction::triggered, this, &Ventana::abrirReproduccion);
    // Salir del modo reproducción: quita sus objetivos y rutas y cierra la
    // barra. Esc solo mientras se reproduce (fuera de ese modo, Esc es de
    // las herramientas del mapa: cancelar un dibujo, una medida...).
    m_accSalirRep = mDatos->addAction(style()->standardIcon(QStyle::SP_DialogCloseButton),
                                      tr("Salir de la reproducción"));
    m_accSalirRep->setShortcut(QKeySequence(Qt::Key_Escape));
    m_accSalirRep->setShortcutContext(Qt::WindowShortcut);
    m_accSalirRep->setStatusTip(tr("Quita del mapa los objetivos y rutas de la "
                                   "reproducción y cierra su barra."));
    m_accSalirRep->setEnabled(false);
    connect(m_accSalirRep, &QAction::triggered, this, &Ventana::salirReproduccion);

    QMenu *mAyuda = mb->addMenu(tr("A&yuda"));
    connect(mAyuda->addAction(tr("Ratón y teclado...")), &QAction::triggered, this,
            &Ventana::mostrarAyudaRaton);
    connect(mAyuda->addAction(tr("Acerca de...")), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("LibMapaStatic"),
            tr("Demostración de LibMapaStatic: librería de mapas por teselas "
               "para Qt, con entidades, elevación y seguimiento."));
    });

    // --- Barra de herramientas principal (navegación · dibujo · archivo) -
    // Solo iconos (con tooltip): compacta y de aspecto profesional.
    QToolBar *tb = addToolBar(tr("Principal"));
    tb->setObjectName(QStringLiteral("barraPrincipal"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(20, 20));
    tb->addWidget(new QLabel(tr(" Base: ")));
    tb->addWidget(m_capasBase);
    tb->addSeparator();
    tb->addAction(m_accNavegar);
    tb->addAction(m_accMedir);
    tb->addAction(m_accMedirArea);
    tb->addAction(m_accZoomArea);
    tb->addSeparator();
    tb->addAction(m_accPunto);
    tb->addAction(m_accLinea);
    tb->addAction(m_accPoligono);
    tb->addAction(m_accObjetivo);
    tb->addAction(m_accArea);
    tb->addAction(m_accEditar);
    tb->addAction(m_accBorrar);
    tb->addSeparator();
    tb->addAction(m_accDeshacer);
    tb->addAction(m_accRehacer);
    tb->addSeparator();
    tb->addAction(m_accAbrir);
    tb->addAction(m_accGuardar);

    // --- Barra secundaria: terreno y datos (actividad aparte, 2ª fila) ---
    addToolBarBreak();   // fuerza una fila nueva: no se amontona con la de arriba
    QToolBar *tb2 = addToolBar(tr("Terreno y datos"));
    tb2->setObjectName(QStringLiteral("barraDatos"));
    tb2->setMovable(false);
    tb2->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb2->setIconSize(QSize(20, 20));
    tb2->addAction(m_accRejilla);
    tb2->addAction(m_accCobertura);
    tb2->addWidget(new QLabel(tr(" z:")));
    tb2->addWidget(m_zCobertura);
    tb2->addSeparator();
    tb2->addWidget(m_btnDem);
    tb2->addSeparator();
    tb2->addAction(accGeo);
    tb2->addWidget(new QLabel(tr("  Objetivos:")));
    tb2->addWidget(m_numObjetivos);
    tb2->addAction(m_accSimular);
    tb2->addWidget(new QLabel(tr(" Traza:")));
    tb2->addWidget(m_traza);
}

// Aplica el tema (claro u oscuro) a toda la app, marca el menú y lo recuerda
// en QSettings para la próxima vez. Es una hoja de estilo (QSS) global.
void Ventana::aplicarTema(bool oscuro)
{
    m_temaOscuro = oscuro;
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance())) {
        // La PALETA cubre los contenedores planos (páginas de pestaña, scroll,
        // ficha) que la hoja de estilo no alcanza — por eso ya no queda blanco.
        app->setPalette(oscuro ? paletaOscura() : paletaClara());
        app->setStyleSheet(oscuro ? qssOscuro() : qssClaro());
    }
    if (m_accTemaOscuro) m_accTemaOscuro->setChecked(oscuro);
    if (m_accTemaClaro)  m_accTemaClaro->setChecked(!oscuro);
    QSettings().setValue(QStringLiteral("tema/oscuro"), oscuro);
    // Iconos de la barra: se recolorean con el primer plano del tema.
    aplicarIconos(oscuro ? QColor(0xc9, 0xd4, 0xdf) : QColor(0x2a, 0x3b, 0x4c));
    // Los botones de color llevan su propio estilo inline: se repintan para
    // que no queden con el estilo del tema anterior.
    if (m_btnColorLinea)   pintarBotonColor(m_btnColorLinea, m_colorLinea);
    if (m_btnColorRelleno) pintarBotonColor(m_btnColorRelleno, m_colorRelleno);
    // Refresca el árbol para que los fondos de grupo tomen el color del tema.
    if (m_arbol) reconstruirPanel();
}

// Paleta base del tema CLARO (cubre los widgets planos que la QSS no toca).
QPalette Ventana::paletaClara()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xf4, 0xf6, 0xf8));
    p.setColor(QPalette::WindowText, QColor(0x1f, 0x2a, 0x36));
    p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::AlternateBase, QColor(0xee, 0xf2, 0xf5));
    p.setColor(QPalette::Text, QColor(0x1f, 0x2a, 0x36));
    p.setColor(QPalette::Button, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::ButtonText, QColor(0x1f, 0x2a, 0x36));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::ToolTipText, QColor(0x1f, 0x2a, 0x36));
    p.setColor(QPalette::Highlight, QColor(0x15, 0x65, 0xc0));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    return p;
}

// Paleta base del tema OSCURO.
QPalette Ventana::paletaOscura()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x1e, 0x24, 0x2b));
    p.setColor(QPalette::WindowText, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::Base, QColor(0x16, 0x1b, 0x22));
    p.setColor(QPalette::AlternateBase, QColor(0x20, 0x26, 0x2d));
    p.setColor(QPalette::Text, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::Button, QColor(0x26, 0x2d, 0x35));
    p.setColor(QPalette::ButtonText, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::ToolTipBase, QColor(0x26, 0x2d, 0x35));
    p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x81, 0xf7));
    p.setColor(QPalette::HighlightedText, QColor(0x0d, 0x11, 0x17));
    const QColor gris(0x7a, 0x86, 0x92);
    p.setColor(QPalette::Disabled, QPalette::Text, gris);
    p.setColor(QPalette::Disabled, QPalette::WindowText, gris);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, gris);
    return p;
}

// Pinta el icono de cada acción con la propiedad «glifo» en el color \a fg,
// y el del botón DEM. Se llama al cambiar de tema para que casen.
void Ventana::aplicarIconos(const QColor &fg)
{
    for (QAction *a : findChildren<QAction *>()) {
        const QString k = a->property("glifo").toString();
        if (!k.isEmpty())
            a->setIcon(glifo(k, fg));
    }
    if (m_btnDem) {
        m_btnDem->setIcon(glifo(QStringLiteral("dem"), fg));
        m_btnDem->setText(tr(" DEM"));
    }
}

// Hoja de estilo del tema CLARO: fondo claro, acento azul, cabeceras y
// separadores sutiles. Pensada para lucir de producto sin recargar.
QString Ventana::qssClaro()
{
    return QStringLiteral(R"(
        QMainWindow, QDialog { background:#f4f6f8; }
        QWidget { color:#1f2a36; font-size:11px; }
        QToolTip { color:#1f2a36; background-color:#ffffff;
                   border:1px solid #cdd6de; padding:3px 6px; }
        QToolBar { background:#ffffff; border:0; border-bottom:1px solid #d9e0e6;
                   spacing:4px; padding:3px; }
        QToolButton { background:transparent; border:1px solid transparent;
                      border-radius:4px; padding:4px 8px; }
        QToolButton:hover { background:#e9eef3; }
        QToolButton:checked { background:#1565c0; color:#ffffff; }
        QMenuBar { background-color:#ffffff; color:#1f2a36;
                   border-bottom:1px solid #d9e0e6; }
        QMenuBar::item { background:transparent; padding:4px 10px; }
        QMenuBar::item:selected { background:#e9eef3; }
        QMenu { background:#ffffff; border:1px solid #d9e0e6; }
        QMenu::item:selected { background:#1565c0; color:#ffffff; }
        QDockWidget { titlebar-close-icon:none; }
        QDockWidget::title { background:#eef2f5; padding:6px; font-weight:bold; }
        QTabWidget::pane { border:1px solid #d9e0e6; }
        QTabBar::tab { background:#eef2f5; padding:6px 12px; border:1px solid #d9e0e6;
                       border-bottom:0; }
        QTabBar::tab:selected { background:#ffffff; color:#1565c0; font-weight:bold; }
        QTreeWidget, QTableWidget, QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox,
        QTextEdit, QPlainTextEdit {
            background:#ffffff; border:1px solid #cdd6de; border-radius:4px; }
        QHeaderView::section { background:#eef2f5; border:0;
                               border-bottom:1px solid #d9e0e6; padding:4px; }
        QPushButton { background:#ffffff; border:1px solid #cdd6de; border-radius:4px;
                      padding:5px 10px; }
        QPushButton:hover { border-color:#1565c0; }
        QPushButton:pressed { background:#e9eef3; }
        QLabel#fichaTitulo { font-size:13px; font-weight:bold; color:#1565c0;
                             padding:2px 0; }
        QLabel#fichaCapa { color:#5b6b7b; }
        QToolButton#seccionHeader { font-weight:bold; color:#2a3b4c;
                                    border-top:1px solid #e2e8ee; padding:5px 2px; }
        QStatusBar { background:#ffffff; border-top:1px solid #d9e0e6; }
    )");
}

// Hoja de estilo del tema OSCURO: fondo gris/negro, acento azul claro.
// Luce muy «técnico» en demos de sala.
QString Ventana::qssOscuro()
{
    return QStringLiteral(R"(
        QMainWindow, QDialog { background:#1e242b; }
        QWidget { color:#e6edf3; font-size:11px; }
        QToolTip { color:#e6edf3; background-color:#262d35;
                   border:1px solid #12161b; padding:3px 6px; }
        QToolBar { background:#262d35; border:0; border-bottom:1px solid #12161b;
                   spacing:4px; padding:3px; }
        QToolButton { background:transparent; border:1px solid transparent;
                      border-radius:4px; padding:4px 8px; }
        QToolButton:hover { background:#333c46; }
        QToolButton:checked { background:#2f81f7; color:#0d1117; }
        QMenuBar { background-color:#262d35; color:#e6edf3;
                   border-bottom:1px solid #12161b; }
        QMenuBar::item { background:transparent; padding:4px 10px; }
        QMenuBar::item:selected { background:#333c46; }
        QMenu { background:#262d35; border:1px solid #12161b; }
        QMenu::item:selected { background:#2f81f7; color:#0d1117; }
        QDockWidget::title { background:#20262d; padding:6px; font-weight:bold; }
        QTabWidget::pane { border:1px solid #12161b; }
        QTabBar::tab { background:#20262d; padding:6px 12px; border:1px solid #12161b;
                       border-bottom:0; color:#9aa7b4; }
        QTabBar::tab:selected { background:#1e242b; color:#58a6ff; font-weight:bold; }
        QTreeWidget, QTableWidget, QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox,
        QTextEdit, QPlainTextEdit {
            background:#161b22; border:1px solid #30363d; border-radius:4px;
            color:#e6edf3; selection-background-color:#2f81f7; }
        QHeaderView::section { background:#20262d; border:0;
                               border-bottom:1px solid #12161b; padding:4px;
                               color:#9aa7b4; }
        QPushButton { background:#262d35; border:1px solid #30363d; border-radius:4px;
                      padding:5px 10px; color:#e6edf3; }
        QPushButton:hover { border-color:#2f81f7; }
        QPushButton:pressed { background:#1b2128; }
        QLabel#fichaTitulo { font-size:13px; font-weight:bold; color:#58a6ff;
                             padding:2px 0; }
        QLabel#fichaCapa { color:#9aa7b4; }
        QToolButton#seccionHeader { font-weight:bold; color:#c9d4df;
                                    border-top:1px solid #30363d; padding:5px 2px; }
        QStatusBar { background:#262d35; border-top:1px solid #12161b; }
    )");
}
