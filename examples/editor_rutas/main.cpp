// editor_rutas - dibujar, editar y guardar rutas con libmapa.
//
// Cuarto ejemplo de la serie (el visor_gpx solo lee). Enseña lo que necesita un
// editor de recorridos o de planes:
//   - Dibujar una ruta clic a clic (MapTool::DrawPolyline) sobre una capa propia
//     que va por encima de las del paquete (setActiveFeatureLayer, setDraftStyle).
//   - Editarla: arrastrar vértices, insertar uno en un lado, borrar (MapTool::EditFeature).
//   - Deshacer y rehacer (undo / redo) y borrar la ruta seleccionada.
//   - Guardado EXPLÍCITO a GPX, KML o GeoJSON (exportVectorFile) y abrir uno
//     (importVectorFile); aviso de cambios sin guardar. Con autosaveFeatures = false
//     nada se escribe solo en la base de entidades del paquete.
//   - Longitud de la ruta seleccionada, en vivo mientras se edita.
//
//   editor_rutas [paquete de datos] [fichero.gpx|.kml|.geojson]   (paquete por defecto: <exe>/datos)
//   editor_rutas <paquete> --comprobar
//        -> sin ventana: crea una ruta por código, la guarda a GPX, la vuelve a leer y
//           comprueba que es la misma (sale con 0 si coincide).
//
// Nota: la librería escribe las líneas a GPX como <trk> (recorrido), no como <rte>.

#include <libmapa/Coordinates.h>
#include <libmapa/MapWidget.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextStream>

using namespace libmapa;

namespace {

// Capa donde viven las rutas, por encima de las del paquete (provincias, curvas...):
// si quedara debajo, un clic dentro de una provincia la seleccionaría a ella.
const QString kCapa = QStringLiteral("rutas");
const int kOrdenCapa = 1000;

// Extensión (con punto) elegida en un filtro del diálogo de guardar, o ".gpx".
QString extensionDeFiltro(const QString &filtro)
{
    for (const char *ext : { ".kml", ".geojson", ".gpx" })
        if (filtro.contains(QLatin1String(ext), Qt::CaseInsensitive))
            return QLatin1String(ext);
    return QStringLiteral(".gpx");
}

} // namespace

// Ventana con el mapa y los menús de archivo, dibujo y edición. Todo está en el
// constructor y los métodos de debajo para leerlo de arriba abajo.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos (sin guardado automático) y los menús.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        cfg.autosaveFeatures = false;          // el guardado lo decide el usuario
        m_mapa = new MapWidget(cfg, this);
        m_mapa->setScaleBarVisible(true);
        setCentralWidget(m_mapa);
        resize(1100, 760);

        // La capa de rutas y cómo se dibujan las nuevas.
        m_mapa->addFeatureLayer(kCapa, tr("Rutas"), kOrdenCapa);
        m_mapa->setActiveFeatureLayer(kCapa);
        FeatureStyle estilo;
        estilo.lineColor = QColor(0x15, 0x65, 0xc0);
        estilo.lineWidth = 3.0;
        estilo.verticesVisible = true;
        m_mapa->setDraftStyle(estilo);

        crearMenus();

        // Cambios: marcar «sin guardar», refrescar la longitud y pedir nombre al crear.
        connect(m_mapa, &MapWidget::featureAdded, this, [this](qint64) { cambio(); });
        connect(m_mapa, &MapWidget::featureUpdated, this, [this](qint64) { cambio(); });
        connect(m_mapa, &MapWidget::featureRemoved, this, [this](qint64) { cambio(); });
        connect(m_mapa, &MapWidget::featureSelected, this, [this](qint64) { mostrarLongitud(); });
        connect(m_mapa, &MapWidget::featureCreated, this, [this](qint64 id) { pedirNombre(id); });

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
        actualizarTitulo();
    }

    // Abre un GPX/KML/GeoJSON como las rutas a editar (sustituye las actuales).
    bool abrir(const QString &ruta)
    {
        QString error;
        m_cargando = true;
        m_mapa->clearFeatureLayer(kCapa);
        const qint64 primero = m_mapa->importVectorFile(ruta, kCapa, tr("Rutas"), &error);
        m_cargando = false;
        if (primero < 0) {
            statusBar()->showMessage(tr("No se pudo abrir: %1").arg(error));
            return false;
        }
        m_mapa->clearUndoHistory();             // deshacer no debe «des-abrir» el fichero
        m_fichero = ruta;
        m_modificado = false;
        encuadrar();
        actualizarTitulo();
        statusBar()->showMessage(tr("%1: %2 rutas").arg(QFileInfo(ruta).fileName()).arg(numRutas()));
        return true;
    }

    // Guarda las rutas en \a ruta (formato según su extensión). false con aviso si falla.
    bool guardarEn(const QString &ruta)
    {
        QString error;
        if (!m_mapa->exportVectorFile(ruta, kCapa, &error)) {
            QMessageBox::warning(this, tr("Guardar"), error);
            return false;
        }
        m_fichero = ruta;
        m_modificado = false;
        actualizarTitulo();
        statusBar()->showMessage(tr("Guardado en %1").arg(ruta), 6000);
        return true;
    }

    // Número de rutas (líneas) de la capa.
    int numRutas() const { return int(m_mapa->featuresInLayer(kCapa).size()); }
    MapWidget *mapa() const { return m_mapa; }

protected:
    // Al cerrar con cambios sin guardar, pregunta qué hacer.
    void closeEvent(QCloseEvent *e) override
    {
        if (!m_modificado || numRutas() == 0) {
            e->accept();
            return;
        }
        const auto r = QMessageBox::question(this, tr("Cambios sin guardar"),
            tr("Hay rutas sin guardar. ¿Guardarlas antes de salir?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel || (r == QMessageBox::Save && !guardar()))
            e->ignore();
        else
            e->accept();
    }

private:
    // Menús: Archivo (abrir / guardar), Ruta (herramientas) y Edición (deshacer, borrar).
    void crearMenus()
    {
        QMenu *archivo = menuBar()->addMenu(tr("&Archivo"));
        connect(archivo->addAction(tr("&Abrir...")), &QAction::triggered, this, [this] {
            if (!confirmarDescarte())
                return;
            const QString ruta = QFileDialog::getOpenFileName(
                this, tr("Abrir rutas"), QString(), tr("Rutas (*.gpx *.kml *.geojson *.json)"));
            if (!ruta.isEmpty())
                abrir(ruta);
        });
        auto *guardarAct = archivo->addAction(tr("&Guardar"));
        guardarAct->setShortcut(QKeySequence::Save);
        connect(guardarAct, &QAction::triggered, this, [this] { guardar(); });
        connect(archivo->addAction(tr("Guardar &como...")), &QAction::triggered, this,
                [this] { guardarComo(); });

        // Herramientas: exclusivas entre sí.
        QMenu *ruta = menuBar()->addMenu(tr("&Ruta"));
        auto *grupo = new QActionGroup(this);
        const auto herramienta = [&](const QString &texto, MapTool t, const QKeySequence &tecla) {
            QAction *a = ruta->addAction(texto);
            a->setCheckable(true);
            a->setActionGroup(grupo);
            a->setShortcut(tecla);
            connect(a, &QAction::triggered, this, [this, t] { m_mapa->setActiveTool(t); });
            return a;
        };
        herramienta(tr("&Navegar"), MapTool::None, QKeySequence(Qt::Key_N))->setChecked(true);
        herramienta(tr("&Dibujar ruta (doble clic cierra)"), MapTool::DrawPolyline, QKeySequence(Qt::Key_D));
        herramienta(tr("&Editar vértices"), MapTool::EditFeature, QKeySequence(Qt::Key_E));

        QMenu *edicion = menuBar()->addMenu(tr("&Edición"));
        auto *deshacer = edicion->addAction(tr("&Deshacer"));
        deshacer->setShortcut(QKeySequence::Undo);
        connect(deshacer, &QAction::triggered, this, [this] { m_mapa->undo(); });
        auto *rehacer = edicion->addAction(tr("&Rehacer"));
        rehacer->setShortcut(QKeySequence::Redo);
        connect(rehacer, &QAction::triggered, this, [this] { m_mapa->redo(); });
        connect(edicion->addAction(tr("&Renombrar la seleccionada...")), &QAction::triggered, this,
                [this] { pedirNombre(m_mapa->selectedFeature()); });
        connect(edicion->addAction(tr("&Borrar la seleccionada")), &QAction::triggered, this, [this] {
            if (m_mapa->selectedFeature() >= 0)
                m_mapa->removeFeature(m_mapa->selectedFeature());
        });
    }

    // Algo cambió en el mapa: sin guardar, y longitud al día. Se ignora al cargar.
    void cambio()
    {
        if (m_cargando)
            return;
        m_modificado = true;
        actualizarTitulo();
        mostrarLongitud();
    }

    // Pregunta el nombre de la ruta \a id y se lo pone (si se acepta).
    void pedirNombre(qint64 id)
    {
        auto f = id >= 0 ? m_mapa->feature(id) : std::nullopt;
        if (!f)
            return;
        bool ok = false;
        const QString nombre = QInputDialog::getText(this, tr("Nombre de la ruta"), tr("Nombre:"),
                                                     QLineEdit::Normal, f->name, &ok);
        if (ok) {
            f->name = nombre;
            m_mapa->updateFeature(*f);
        }
    }

    // Longitud y vértices de la ruta seleccionada, en la barra de estado.
    void mostrarLongitud()
    {
        const auto f = m_mapa->selectedFeature() >= 0 ? m_mapa->feature(m_mapa->selectedFeature())
                                                      : std::nullopt;
        if (!f || f->kind != GeometryKind::Polyline || f->outlines().isEmpty())
            return;
        const auto camino = f->outlines().first();
        statusBar()->showMessage(tr("%1: %2 km, %3 vértices")
            .arg(f->name.isEmpty() ? tr("Ruta") : f->name)
            .arg(pathLength(camino) / 1000.0, 0, 'f', 2).arg(camino.size()));
    }

    // Guardar: en el fichero actual, o pregunta uno si aún no hay.
    bool guardar() { return m_fichero.isEmpty() ? guardarComo() : guardarEn(m_fichero); }

    // Guardar como: el formato lo da la extensión (si no hay, la del filtro elegido).
    bool guardarComo()
    {
        QString filtro;
        QString ruta = QFileDialog::getSaveFileName(
            this, tr("Guardar rutas"), m_fichero,
            tr("GPX (*.gpx);;KML (*.kml);;GeoJSON (*.geojson)"), &filtro);
        if (ruta.isEmpty())
            return false;
        if (QFileInfo(ruta).suffix().isEmpty())
            ruta += extensionDeFiltro(filtro);
        return guardarEn(ruta);
    }

    // Antes de reemplazar las rutas: ¿hay cambios sin guardar y se descartan?
    bool confirmarDescarte()
    {
        if (!m_modificado || numRutas() == 0)
            return true;
        return QMessageBox::question(this, tr("Cambios sin guardar"),
            tr("Se perderán las rutas sin guardar. ¿Continuar?")) == QMessageBox::Yes;
    }

    // Encuadra el mapa sobre todas las rutas abiertas.
    void encuadrar()
    {
        double norte = -90, sur = 90, este = -180, oeste = 180;
        bool hay = false;
        for (const MapFeature &f : m_mapa->featuresInLayer(kCapa))
            for (const auto &parte : f.outlines())
                for (const QGeoCoordinate &c : parte) {
                    norte = std::max(norte, c.latitude());
                    sur = std::min(sur, c.latitude());
                    este = std::max(este, c.longitude());
                    oeste = std::min(oeste, c.longitude());
                    hay = true;
                }
        if (hay && (norte - sur > 1e-5 || este - oeste > 1e-5))
            m_mapa->fitBounds(QGeoCoordinate(norte, oeste), QGeoCoordinate(sur, este));
    }

    // Título con el fichero y un asterisco si hay cambios sin guardar.
    void actualizarTitulo()
    {
        setWindowTitle(tr("Editor de rutas - %1%2")
            .arg(m_fichero.isEmpty() ? tr("(sin título)") : QFileInfo(m_fichero).fileName(),
                 m_modificado ? QStringLiteral(" *") : QString()));
    }

    MapWidget *m_mapa = nullptr;
    QString m_fichero;
    bool m_modificado = false;
    bool m_cargando = false;
};

// Prueba sin ventana: crea una ruta por código, la guarda a GPX, la vuelve a leer en
// otra capa y comprueba que tiene los mismos puntos y la misma longitud.
static int comprobar(Ventana &ventana)
{
    QTextStream out(stdout);
    MapWidget *mapa = ventana.mapa();
    if (!mapa->isReady()) {
        out << "listo: no\n";
        return 1;
    }
    MapFeature ruta;
    ruta.kind = GeometryKind::Polyline;
    ruta.layerId = kCapa;
    ruta.name = QStringLiteral("Ruta de prueba");
    ruta.geometry = { QGeoCoordinate(23.1300, -82.3600), QGeoCoordinate(23.1400, -82.3500),
                      QGeoCoordinate(23.1500, -82.3550) };
    if (mapa->addFeature(ruta) < 0) {
        out << "no se pudo crear la ruta\n";
        return 1;
    }
    QTemporaryDir dir;
    const QString fichero = dir.filePath(QStringLiteral("prueba.gpx"));
    if (!ventana.guardarEn(fichero))
        return 1;
    mapa->addFeatureLayer(QStringLiteral("releida"), QStringLiteral("Releída"), 1);
    QString error;
    if (mapa->importVectorFile(fichero, QStringLiteral("releida"), QString(), &error) < 0) {
        out << "no se pudo releer: " << error << "\n";
        return 1;
    }
    const auto leidas = mapa->featuresInLayer(QStringLiteral("releida"));
    if (leidas.size() != 1 || leidas.first().outlines().isEmpty()) {
        out << "la ruta releída no coincide\n";
        return 1;
    }
    const auto original = ruta.outlines().first();
    const auto vuelta = leidas.first().outlines().first();
    const double dif = std::abs(pathLength(original) - pathLength(vuelta));
    out << "ruta creada: " << original.size() << " puntos, "
        << QString::number(pathLength(original) / 1000.0, 'f', 2) << " km\n";
    out << "guardada y releída: " << vuelta.size() << " puntos, "
        << QString::number(pathLength(vuelta) / 1000.0, 'f', 2) << " km, nombre «"
        << leidas.first().name << "»\n";
    return (vuelta.size() == original.size() && dif < 1.0
            && leidas.first().name == ruta.name) ? 0 : 1;
}

// ¿Parece un fichero de rutas por su extensión?
static bool esFicheroDeRutas(const QString &arg)
{
    for (const char *ext : { ".gpx", ".kml", ".geojson", ".json" })
        if (arg.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
            return true;
    return false;
}

// Arranca la ventana (o, con --comprobar, la prueba de guardar y releer).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("editor_rutas"));

    QStringList posicionales;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith(QLatin1String("--")))
            posicionales << a;

    // Un solo argumento con extensión de ruta = el fichero (paquete por defecto).
    QString datos = QCoreApplication::applicationDirPath() + QStringLiteral("/datos");
    QString fichero;
    if (posicionales.size() == 1 && esFicheroDeRutas(posicionales.first())) {
        fichero = posicionales.first();
    } else {
        if (!posicionales.isEmpty())
            datos = posicionales.first();
        if (posicionales.size() > 1)
            fichero = posicionales.at(1);
    }

    Ventana ventana(datos);
    if (app.arguments().contains(QStringLiteral("--comprobar")))
        return comprobar(ventana);
    if (!fichero.isEmpty())
        ventana.abrir(fichero);
    ventana.show();
    return app.exec();
}
