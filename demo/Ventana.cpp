// Constructor, conexión de señales del mapa y barra de estado.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

using namespace libmapa;

// \a origen es un datasets.json o, si \a esPaquete, la carpeta de un
// paquete de datos (mapa.json), que ya trae elevacion, capas fijas y
// entidades: entonces basta con esa linea.
Ventana::Ventana(const QString &origen, bool esPaquete,
                 const QString &demDir,
                 const QString &demDb,
                 const QString &featuresDb)
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
    // GUARDADO EXPLÍCITO: se apaga el autoguardado. Esto es CLAVE con un
    // paquete: si no, al dejar featuresDbFile vacío la librería usaría el
    // fichero de entidades del propio paquete y guardaría/ recargaría solo.
    // Con autosaveFeatures=false no se toca ese fichero; --features (abajo)
    // se carga a mano y «Guardar» escribe solo cuando el usuario lo pide.
    cfg.autosaveFeatures = false;
    cfg.cacheMiB = 192;

    m_mapa = new MapWidget(cfg, this);
    // Hay DEM solo si la libreria pudo cargar algo (con --dem, --dem-db o el
    // paquete): una carpeta vacia o una ruta equivocada no cuenta.
    m_demActivo = !m_mapa->elevationDescription().isEmpty();
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

    construirMenuYBarra();
    construirPanel();
    conectarSenales();
    aplicarEstiloAlTrazo();
    prepararSeguimiento();

    // Guardado explícito: si viene --features, se CARGA (si existe) y se
    // recuerda como fichero de trabajo; a partir de ahí «Guardar» escribe
    // ahí, pero nada se guarda solo. loadFeaturesFrom reinyecta los overlays
    // del paquete (los deja visibles), así que la carga va ANTES de ocultar.
    if (!featuresDb.isEmpty()) {
        if (QFileInfo::exists(featuresDb))
            m_mapa->loadFeaturesFrom(featuresDb);
        m_archivoActual = featuresDb;
        m_sucio = false;
        actualizarTitulo();
    }

    reconstruirPanel();
    ocultarOverlaysAlIniciar();   // los overlays arrancan ocultos (no cargan)

    // Tema elegido por el usuario (recordado en QSettings; claro por defecto).
    aplicarTema(QSettings().value(QStringLiteral("tema/oscuro"), false).toBool());

    // Asi deberia hacerlo una app real: el mapa arranca con lo que funcione
    // y avisa de lo que no (fichero que falta, imagenes sin plugin...).
    const QStringList problemas = m_mapa->dataWarnings();
    if (!problemas.isEmpty())
        QMessageBox::warning(this, tr("Datos del mapa"),
            tr("El paquete de datos tiene problemas; el mapa usara lo que "
               "funcione:\n\n%1").arg(problemas.join(QLatin1Char('\n'))));
}

// Conecta las señales del mapa con el panel, la barra de estado y los indicadores.
void Ventana::conectarSenales()
{
    // Clic derecho: menú según lo que haya debajo (objetivo, entidad...).
    connect(m_mapa, &MapWidget::contextMenuRequested, this, &Ventana::menuContextual);
    // Clic izquierdo sobre un elemento: tarjeta con sus datos y acciones rápidas.
    connect(m_mapa, &MapWidget::elementClicked, this, &Ventana::mostrarTarjeta);
    // Esc o clic derecho durante una captura de punto: se cancela.
    connect(m_mapa, &MapWidget::pickCancelled, this, &Ventana::cancelarCaptura);
    // Lo que el ratón o el teclado rechazan (capa bloqueada, vértices mínimos...).
    connect(m_mapa, &MapWidget::interactionBlocked, this,
            [this](const QString &motivo) { statusBar()->showMessage(motivo, 6000); });
    // Los anillos y las marcaciones de un punto se van con él (es un solo elemento).
    connect(m_mapa, &MapWidget::featureRemoved, this, [this](qint64 id) { quitarMarcasDe(-1, id); });

    connect(m_capasBase, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
                m_mapa->setBaseLayerId(m_capasBase->itemData(i).toString());
            });

    connect(m_mapa, &MapWidget::mouseMoved, this,
            [this](const QGeoCoordinate &p) {
                // En el formato elegido en Ver → Formato de coordenadas.
                const QString txt = formatCoordinate(p, m_formatoCoord);
                m_coords->setText(QStringLiteral("  %1  ").arg(
                    txt.isEmpty() ? tr("(fuera de UTM)") : txt));
                mostrarCota(p);
            });

    connect(m_mapa, &MapWidget::zoomChanged, this, &Ventana::actualizarEstado);
    connect(m_mapa, &MapWidget::baseLayerChanged, this, &Ventana::actualizarEstado);
    // Si la base cambia desde la barra o el menú, refleja la marca en el árbol.
    connect(m_mapa, &MapWidget::baseLayerChanged, this, &Ventana::sincronizarBaseEnArbol);
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

    // Medir superficie: la medida en vivo ya la rotula la librería; al
    // cerrar se deja el polígono medido (otro clic empieza otra medida) y
    // se repite el resultado en la barra de estado.
    connect(m_mapa, &MapWidget::areaMeasured, this, [this](const AreaMeasurement &m) {
        const ScaleUnits u = m_mapa->scaleBarUnits();
        const double nm2 = 1852.0 * 1852.0;
        const QString sup = u == ScaleUnits::Nautical
            ? tr("%1 NM²").arg(m.areaSquareMeters / nm2, 0, 'f', 3)
            : (m.areaSquareMeters >= 1e6 ? tr("%1 km²").arg(m.areaSquareMeters / 1e6, 0, 'f', 3)
                                         : tr("%1 ha").arg(m.areaSquareMeters / 1e4, 0, 'f', 2));
        const QString per = u == ScaleUnits::Nautical
            ? tr("%1 NM").arg(m.perimeterMeters / 1852.0, 0, 'f', 2)
            : tr("%1 km").arg(m.perimeterMeters / 1000.0, 0, 'f', 2);
        statusBar()->showMessage(tr("Superficie %1 · perímetro %2 · %3 vértices "
                                    "(clic para medir otra)")
                                     .arg(sup, per).arg(m.polygon.size()), 15000);
    });

    connect(m_mapa, &MapWidget::errorOccurred, this,
            [this](const QString &e) {
                statusBar()->showMessage(tr("Error: %1").arg(e), 8000);
            });

    // Selección de zona: la librería deja el recuadro/contorno visible y
    // emite la geometría. Antes aquí se volvía a Navegar en el acto, y eso
    // BORRABA la marca: parecía que la herramienta no hacía nada. Ahora la
    // marca sigue a la vista mientras se muestra qué hay dentro y qué se
    // puede hacer con la zona (fuera del evento de ratón, con un
    // singleShot, para que el mapa se repinte con la marca antes).
    connect(m_mapa, &MapWidget::areaSelected, this,
            [this](const QGeoCoordinate &nw, const QGeoCoordinate &se) {
                const QVector<QGeoCoordinate> rect = {
                    nw, QGeoCoordinate(nw.latitude(), se.longitude()),
                    se, QGeoCoordinate(se.latitude(), nw.longitude()) };
                QTimer::singleShot(0, this, [this, rect] { zonaSeleccionada(rect, true); });
            });
    connect(m_mapa, &MapWidget::polygonSelected, this,
            [this](const QVector<QGeoCoordinate> &pol) {
                QTimer::singleShot(0, this, [this, pol] { zonaSeleccionada(pol, false); });
            });

    connect(m_grupo, &QActionGroup::triggered, this, [this](QAction *a) {
        const auto tool = static_cast<MapTool>(a->data().toInt());
        m_mapa->setActiveTool(tool);
        // Si se cambia a una herramienta que no dibuja, se desarma el alta
        // en curso (p. ej. el usuario pulsa Navegar sin llegar a dibujar).
        if (tool != MapTool::DrawPoint && tool != MapTool::DrawPolyline
            && tool != MapTool::DrawPolygon) {
            m_esperandoDibujo = false;
            m_nombrePendiente.clear();
        }
    });

    // --- El panel se reconstruye SOLO desde las senales del modelo. ---
    connect(m_mapa, &MapWidget::featureAdded, this, &Ventana::alEntidadCreada);
    connect(m_mapa, &MapWidget::featureAdded, this, &Ventana::pedirReconstruir);
    connect(m_mapa, &MapWidget::featureRemoved, this, &Ventana::pedirReconstruir);
    connect(m_mapa, &MapWidget::featureUpdated, this, &Ventana::pedirReconstruir);
    connect(m_mapa, &MapWidget::featureLayersChanged, this, &Ventana::pedirReconstruir);
    // Toda «Área objetivo» queda vigilada sola (creada, cargada o restaurada).
    connect(m_mapa, &MapWidget::featureLayersChanged, this, &Ventana::vigilarAreasObjetivo);
    connect(m_mapa, &MapWidget::featureRemoved, this, [this](qint64) {
        actualizarIndicadorAlertas();
    });

    connect(m_mapa, &MapWidget::featureSelected, this, &Ventana::seleccionEnMapa);

    connect(m_mapa, &MapWidget::featureCreated, this, [this](qint64 id) {
        auto f = m_mapa->feature(id);
        // A un objetivo/área recién creado se le pone un nombre por defecto
        // (3 líneas en el objetivo) para que su etiqueta se vea al momento.
        if (f && f->name.isEmpty()) {
            if (f->type == QLatin1String("objetivo")) {
                f->name = tr("Objetivo %1\nrumbo —\nestado —").arg(id);
                m_mapa->updateFeature(*f);
            } else if (f->type == QLatin1String("objetivo_area")) {
                f->name = tr("Área %1").arg(id);
                m_mapa->updateFeature(*f);
            }
        }
        m_mapa->selectFeature(id);
        statusBar()->showMessage(
            tr("Creada entidad %1 con %2 vertices en la capa '%3'")
                .arg(id).arg(f ? f->geometry.size() : 0)
                .arg(f ? f->layerId : QString()), 5000);
    });

    connect(m_mapa, &MapWidget::drawingCancelled, this,
            [this] { statusBar()->showMessage(tr("Trazado cancelado"), 3000); });

    // Arrastre de los pines A/B por la LIBRERÍA: al empezar se resuelve si es
    // A o B; en cada movimiento se vuelca la coordenada en los campos; al
    // soltar se recalcula el análisis. (Antes esto lo hacía un eventFilter.)
    connect(m_mapa, &MapWidget::featureDragStarted, this, [this](qint64 id) {
        const auto f = m_mapa->feature(id);
        if (f && f->type == QStringLiteral("punto_ab"))
            m_dragPick = (f->name == tr("B")) ? Pick::B : Pick::A;
    });
    connect(m_mapa, &MapWidget::featureDragMovedTo, this,
            [this](qint64, const QGeoCoordinate &g) {
                if (m_dragPick != Pick::Ninguno)
                    moverPinCoord(m_dragPick, g);
            });
    connect(m_mapa, &MapWidget::featureDragFinished, this, [this](qint64) {
        if (m_dragPick != Pick::Ninguno) {
            m_dragPick = Pick::Ninguno;
            reejecutar();            // recalcula el análisis con el punto movido
        } else {
            marcarSucio();           // un punto del usuario (radar...) movido: hay que guardar
        }
    });

    m_coords = new QLabel(this);
    m_cota = new QLabel(this);
    m_cota->setMinimumWidth(80);
    m_cota->setToolTip(tr("Altura del terreno bajo el cursor (necesita DEM)."));
    m_info = new QLabel(this);
    statusBar()->addPermanentWidget(m_coords);
    statusBar()->addPermanentWidget(m_cota);
    statusBar()->addPermanentWidget(m_info);
    m_alertas = new QLabel(this);
    m_alertas->setToolTip(tr("Objetivos dentro de zonas vigiladas (las «Área objetivo» "
                             "se vigilan solas; cualquier polígono, con Datos → "
                             "Vigilar zona seleccionada)."));
    statusBar()->addPermanentWidget(m_alertas);
    // Indicador del MODO reproducción: se ve mientras dura, y un clic sale.
    m_modoRep = new QPushButton(tr("▶ Reproducción · Esc para salir"), this);
    m_modoRep->setFlat(true);
    m_modoRep->setCursor(Qt::PointingHandCursor);
    m_modoRep->setToolTip(tr("Estás viendo un historial grabado, no objetivos en vivo. "
                             "Pulsa aquí o Esc para salir."));
    m_modoRep->setStyleSheet(QStringLiteral("QPushButton { color:#fff; background:#1565c0;"
                                            " font-weight:bold; padding:0 8px; border:none; }"));
    m_modoRep->hide();
    connect(m_modoRep, &QPushButton::clicked, this, &Ventana::salirReproduccion);
    statusBar()->addPermanentWidget(m_modoRep);
    // Indicador del filtro de objetivos (solo mientras hay uno).
    m_filtroLbl = new QLabel(this);
    m_filtroLbl->setStyleSheet(QStringLiteral("QLabel { color:#fff; background:#00796b;"
                                              " font-weight:bold; padding:0 6px; }"));
    m_filtroLbl->setToolTip(tr("Objetivos que pasan el filtro (Ver → Filtrar objetivos)."));
    m_filtroLbl->hide();
    statusBar()->addPermanentWidget(m_filtroLbl);
}

// Muestra la cota del terreno bajo el cursor (o "—" si no hay DEM/dato).
void Ventana::mostrarCota(const QGeoCoordinate &p)
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

// Elige una CARPETA de ficheros `.hgt` o GeoTIFF como origen de elevacion (en
// caliente); el formato lo reconoce la libreria por lo que haya dentro.
void Ventana::alElegirDemCarpeta()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Carpeta de ficheros de elevacion (.hgt o GeoTIFF)"));
    if (dir.isEmpty())
        return;
    m_mapa->setElevationDir(dir);
    avisarElevacion();
}

// Elige una BASE DE DATOS `.sqlitedb` o un GeoTIFF de elevacion como origen (en
// caliente).
void Ventana::alElegirDemDb()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Base de datos o GeoTIFF de elevacion"), QString(),
        tr("Elevacion (*.sqlitedb *.db *.tif *.tiff);;Base de datos (*.sqlitedb *.db);;"
           "GeoTIFF (*.tif *.tiff);;Todos (*)"));
    if (file.isEmpty())
        return;
    m_mapa->setElevationDb(file);
    avisarElevacion();
}

// Dice que quedo cargado tras elegir una elevacion: en la barra de estado lo que
// se leyo (p. ej. «42 GeoTIFF, lat 19.0..24.0…») y, si algo fallo -ficheros
// ilegibles, carpeta sin datos-, un aviso con el motivo. Antes la demo decia
// «Elevacion: <ruta>» aunque no hubiera cargado nada, y los errores solo iban al
// registro de Qt Creator.
void Ventana::avisarElevacion()
{
    const QString desc = m_mapa->elevationDescription();
    const QStringList avisos = m_mapa->elevationWarnings();
    m_demActivo = !desc.isEmpty();
    if (!desc.isEmpty())
        statusBar()->showMessage(tr("Elevación: %1").arg(desc), 10000);
    if (!avisos.isEmpty()) {
        const QString cabecera = desc.isEmpty()
            ? tr("No se pudo cargar la elevación:")
            : tr("Se cargó la elevación, pero algunos ficheros no se pudieron usar:");
        QMessageBox::warning(this, tr("Elevación"), cabecera + QLatin1String("\n\n")
                                                        + avisos.join(QLatin1Char('\n')));
    }
}

// Habilita deshacer/rehacer/borrar y pone el resumen de la barra de estado.
void Ventana::actualizarEstado()
{
    if (m_accDeshacer) {
        m_accDeshacer->setEnabled(m_mapa->canUndo());
        m_accRehacer->setEnabled(m_mapa->canRedo());
        m_accBorrar->setEnabled(m_mapa->selectedFeature() >= 0);
    }
    if (!m_info)
        return;
    m_info->setText(
        QStringLiteral("  %1  |  zoom %2 de %3  |  %4 % propias  |  %5 entidades  |  %6 objetivos  ")
        .arg(m_mapa->baseLayerId())
        .arg(m_mapa->zoom())
        .arg(m_mapa->maxZoom())
        .arg(m_mapa->exactCoverage() * 100.0, 0, 'f', 0)
        .arg(m_mapa->featureCount())
        .arg(m_mapa->targetCount()));
    if (m_filtroLbl) {
        const int total = m_mapa->targetCount();
        const int fuera = int(m_mapa->filteredOutTargets().size());
        m_filtroLbl->setVisible(m_mapa->hasTargetFilter());
        m_filtroLbl->setText(tr("Filtro: %1 de %2").arg(total - fuera).arg(total));
    }
}
