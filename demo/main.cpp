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
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
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
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
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

// --- Perfil de elevacion: dibujo del corte del terreno --------------------
// Pinta un ElevationProfile (distancia en X, cota en Y) como un corte relleno.
// Las muestras sin dato (NaN) dejan hueco. Es SOLO de la app: la libreria da los
// numeros, el dibujo lo pone quien la usa.
class PerfilWidget : public QWidget
{
public:
    explicit PerfilWidget(const ElevationProfile &p, QWidget *parent = nullptr)
        : QWidget(parent), m_p(p)
    {
        setMinimumSize(560, 240);
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, QColor(0xf7, 0xf7, 0xf7));
        setPalette(pal);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing, true);
        g.fillRect(rect(), palette().window());
        const QRectF area = QRectF(rect()).adjusted(56, 14, -16, -28);

        if (m_p.samples.size() < 2 || std::isnan(m_p.maxElevation)) {
            g.setPen(Qt::darkGray);
            g.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("Sin datos de elevacion en la ruta"));
            return;
        }

        const double dMax = m_p.totalDistanceM > 0.0 ? m_p.totalDistanceM : 1.0;
        double yMin = m_p.minElevation, yMax = m_p.maxElevation;
        if (yMax - yMin < 1.0) { yMax += 0.5; yMin -= 0.5; }   // rango minimo
        const double margen = (yMax - yMin) * 0.1;
        yMin -= margen; yMax += margen;

        auto px = [&](double d) { return area.left() + area.width() * (d / dMax); };
        auto py = [&](double e) {
            return area.bottom() - area.height() * ((e - yMin) / (yMax - yMin));
        };

        // Ejes y rotulos (cota arriba/abajo, distancia a la derecha).
        g.setPen(QColor(0xaa, 0xaa, 0xaa));
        g.drawLine(area.topLeft(), area.bottomLeft());
        g.drawLine(area.bottomLeft(), area.bottomRight());
        g.setPen(QColor(0x55, 0x55, 0x55));
        g.drawText(QRectF(0, area.top() - 7, 50, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 m").arg(m_p.maxElevation, 0, 'f', 0));
        g.drawText(QRectF(0, area.bottom() - 7, 50, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 m").arg(m_p.minElevation, 0, 'f', 0));
        g.drawText(QRectF(area.right() - 90, area.bottom() + 8, 90, 16),
                   Qt::AlignRight,
                   QStringLiteral("%1 km").arg(dMax / 1000.0, 0, 'f', 1));
        g.drawText(QRectF(area.left(), area.bottom() + 8, 60, 16),
                   Qt::AlignLeft, QStringLiteral("0"));

        // Corte: un poligono relleno por cada tramo continuo (cortado en los NaN).
        const QColor relleno(0x2e, 0x7d, 0x32, 110);
        const QColor linea(0x1b, 0x5e, 0x20);
        QPolygonF tramo;
        auto cerrar = [&] {
            if (tramo.size() >= 2) {
                QPolygonF poly = tramo;
                poly << QPointF(poly.last().x(), area.bottom())
                     << QPointF(poly.first().x(), area.bottom());
                g.setPen(Qt::NoPen);
                g.setBrush(relleno);
                g.drawPolygon(poly);
                g.setBrush(Qt::NoBrush);
                g.setPen(QPen(linea, 1.6));
                g.drawPolyline(tramo);
            }
            tramo.clear();
        };
        for (const ElevationSample &s : m_p.samples) {
            if (std::isnan(s.elevation)) { cerrar(); continue; }
            tramo << QPointF(px(s.distanceM), py(s.elevation));
        }
        cerrar();
    }

private:
    ElevationProfile m_p;
};

} // namespace

class Ventana : public QMainWindow
{
    Q_OBJECT

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
        construirBarraElevacion();
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
    // Barra para PROBAR los tres calculos nuevos sobre el DEM activo: perfil de
    // una ruta, linea de vision entre dos puntos y viewshed 360. La libreria
    // devuelve los datos; aqui se dibujan (dialogo del perfil; linea de vision y
    // poligono de visibilidad como entidades sobre el mapa).
    void construirBarraElevacion()
    {
        auto *barra = addToolBar(tr("Elevacion"));
        barra->setObjectName(QStringLiteral("barraElevacion"));
        barra->setMovable(false);

        barra->addWidget(new QLabel(tr("  Alt1:")));
        m_altA = new QDoubleSpinBox(this);
        m_altA->setRange(0.0, 20000.0);
        m_altA->setValue(10.0);
        m_altA->setSuffix(tr(" m"));
        m_altA->setToolTip(tr("Visión: antena en A.  Viewshed: altura del observador."));
        barra->addWidget(m_altA);

        barra->addWidget(new QLabel(tr(" Alt2:")));
        m_altB = new QDoubleSpinBox(this);
        m_altB->setRange(0.0, 20000.0);
        m_altB->setValue(10.0);
        m_altB->setSuffix(tr(" m"));
        m_altB->setToolTip(tr("Visión: antena en B.  Viewshed: altura del objetivo."));
        barra->addWidget(m_altB);

        barra->addWidget(new QLabel(tr(" Alcance:")));
        m_alcanceKm = new QDoubleSpinBox(this);
        m_alcanceKm->setRange(1.0, 300.0);
        m_alcanceKm->setValue(40.0);
        m_alcanceKm->setSuffix(tr(" km"));
        m_alcanceKm->setToolTip(tr("Alcance (radio) de los rayos del viewshed."));
        barra->addWidget(m_alcanceKm);

        barra->addSeparator();
        auto *perfil = barra->addAction(tr("Perfil"));
        perfil->setToolTip(tr("Perfil del terreno a lo largo de la LÍNEA o polígono "
                              "seleccionado (dibuja una línea y selecciónala)."));
        connect(perfil, &QAction::triggered, this, &Ventana::analizarPerfil);

        auto *vision = barra->addAction(tr("Visión A→B"));
        vision->setToolTip(tr("Línea de visión entre el primer y el último vértice "
                              "de la línea seleccionada, con Alt1/Alt2 de antena."));
        connect(vision, &QAction::triggered, this, &Ventana::analizarVision);

        auto *viewshed = barra->addAction(tr("Viewshed"));
        viewshed->setToolTip(tr("Zona de visibilidad 360° desde el vértice de la "
                                "entidad seleccionada (o el centro del mapa)."));
        connect(viewshed, &QAction::triggered, this, &Ventana::analizarViewshed);

        auto *limpiar = barra->addAction(tr("Limpiar análisis"));
        limpiar->setToolTip(tr("Quita las capas de visión y viewshed."));
        connect(limpiar, &QAction::triggered, this, [this] {
            m_mapa->removeFeatureLayer(kCapaVision);
            m_mapa->removeFeatureLayer(kCapaViewshed);
            statusBar()->showMessage(tr("Análisis de elevación limpiado"), 3000);
        });
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

    // Entidad seleccionada (o vacía si no hay ninguna).
    std::optional<MapFeature> entidadSeleccionada() const
    {
        const qint64 id = m_mapa->selectedFeature();
        return id >= 0 ? m_mapa->feature(id) : std::nullopt;
    }

    // (Re)crea vacía una capa donde volcar un resultado de análisis.
    void prepararCapa(const QString &id, const QString &nombre, int z)
    {
        if (!m_mapa->addFeatureLayer(id, nombre, z))
            m_mapa->clearFeatureLayer(id);
    }

    // Perfil del terreno a lo largo de la ruta seleccionada -> diálogo con el corte.
    void analizarPerfil()
    {
        if (!exigirDem())
            return;
        const auto f = entidadSeleccionada();
        if (!f || f->geometry.size() < 2) {
            statusBar()->showMessage(
                tr("Selecciona una LÍNEA o polígono (≥2 vértices) para su perfil."),
                5000);
            return;
        }
        const ElevationProfile p = m_mapa->elevationProfile(f->geometry);
        if (!p.isValid() || std::isnan(p.maxElevation)) {
            statusBar()->showMessage(
                tr("La ruta no tiene cota en el DEM activo (fuera de cobertura)."),
                5000);
            return;
        }

        QDialog dlg(this);
        dlg.setWindowTitle(tr("Perfil del terreno — %1")
                               .arg(f->name.isEmpty() ? tr("ruta") : f->name));
        auto *lay = new QVBoxLayout(&dlg);
        lay->addWidget(new QLabel(
            tr("Distancia %1 km   ·   mín %2 m   máx %3 m   ·   "
               "subida +%4 m   bajada −%5 m")
                .arg(p.totalDistanceM / 1000.0, 0, 'f', 2)
                .arg(p.minElevation, 0, 'f', 0)
                .arg(p.maxElevation, 0, 'f', 0)
                .arg(p.gain, 0, 'f', 0)
                .arg(p.loss, 0, 'f', 0), &dlg));
        lay->addWidget(new PerfilWidget(p, &dlg), 1);
        auto *botones = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        connect(botones, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(botones);
        dlg.resize(660, 340);
        dlg.exec();
    }

    // Línea de visión entre el 1º y el último vértice de la línea seleccionada.
    void analizarVision()
    {
        if (!exigirDem())
            return;
        const auto f = entidadSeleccionada();
        if (!f || f->geometry.size() < 2) {
            statusBar()->showMessage(
                tr("Selecciona una LÍNEA (se usan su 1º y último vértice como A y B)."),
                5000);
            return;
        }
        const QGeoCoordinate a = f->geometry.first();
        const QGeoCoordinate b = f->geometry.last();
        const LineOfSightResult v =
            m_mapa->lineOfSight(a, b, m_altA->value(), m_altB->value());
        if (!v.isValid()) {
            statusBar()->showMessage(
                tr("No se pudo calcular la visión (falta cota en A o B)."), 5000);
            return;
        }

        prepararCapa(kCapaVision, tr("Análisis: visión"), 50);
        const QColor color = v.clear ? QColor(0x2e, 0x7d, 0x32)
                                     : QColor(0xc6, 0x28, 0x28);
        MapFeature linea;
        linea.layerId = kCapaVision;
        linea.kind = GeometryKind::Polyline;
        linea.type = QStringLiteral("linea_vision");
        linea.name = v.clear ? tr("Visión directa") : tr("Bloqueada");
        linea.geometry = { a, b };
        linea.style.lineColor = color;
        linea.style.lineWidth = 3.0;
        linea.selectable = false;
        m_mapa->addFeature(linea);

        if (!v.clear && v.blockPosition.isValid()) {
            MapFeature corte;
            corte.layerId = kCapaVision;
            corte.kind = GeometryKind::Point;
            corte.type = QStringLiteral("obstaculo");
            corte.name = tr("Obstáculo");
            corte.geometry = { v.blockPosition };
            corte.style.lineColor = QColor(0xc6, 0x28, 0x28);
            corte.style.pointRadiusPx = 7.0;
            corte.selectable = false;
            m_mapa->addFeature(corte);
        }

        statusBar()->showMessage(
            v.clear
                ? tr("Visión DIRECTA · %1 km · holgura mínima %2 m")
                      .arg(v.totalDistanceM / 1000.0, 0, 'f', 2)
                      .arg(v.clearanceM, 0, 'f', 0)
                : tr("BLOQUEADA a %1 km · faltan %2 m de altura en el obstáculo")
                      .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                      .arg(-v.clearanceM, 0, 'f', 0),
            8000);
    }

    // Viewshed 360° desde el vértice de la entidad seleccionada (o el centro del
    // mapa): dibuja el polígono de la zona visible para un objetivo a Alt2.
    void analizarViewshed()
    {
        if (!exigirDem())
            return;
        const auto f = entidadSeleccionada();
        const QGeoCoordinate origen =
            (f && !f->geometry.isEmpty()) ? f->geometry.first() : m_mapa->center();

        ViewshedParams vp;
        vp.observerHeight = m_altA->value();
        vp.targetHeight = m_altB->value();
        vp.maxRangeM = m_alcanceKm->value() * 1000.0;

        QElapsedTimer reloj;
        reloj.start();
        const Viewshed vs = m_mapa->viewshed(origen, vp);
        const qint64 ms = reloj.elapsed();
        if (!vs.isValid()) {
            statusBar()->showMessage(
                tr("No hay cota en el origen del viewshed (fuera de cobertura)."),
                5000);
            return;
        }

        // Zona de visibilidad REAL: una cuña por cada TRAMO visible de cada rayo,
        // todas como partes de UNA sola entidad (polígono multiparte). Así los
        // huecos (vaguadas ocultas tras una loma) se ven como entrantes y la zona
        // concuerda, azimut a azimut, con la Visión A→B. No es la estrella de
        // "alcance contiguo" de antes, que se cortaba en el primer obstáculo.
        const double half = (vp.azimuthStepDeg > 0.0 ? vp.azimuthStepDeg : 1.0) / 2.0;
        QVector<QVector<QGeoCoordinate>> cunas;
        int tramos = 0;
        double maxKm = 0.0;
        for (const ViewshedRay &r : vs.rays) {
            for (const VisibleRange &vr : r.visibleRanges) {
                if (vr.endM - vr.startM < 1.0)
                    continue;
                const double d0 = qMax(vr.startM, 1.0);
                cunas.append({
                    origen.atDistanceAndAzimuth(d0,      r.azimuthDeg - half),
                    origen.atDistanceAndAzimuth(vr.endM, r.azimuthDeg - half),
                    origen.atDistanceAndAzimuth(vr.endM, r.azimuthDeg + half),
                    origen.atDistanceAndAzimuth(d0,      r.azimuthDeg + half)
                });
                ++tramos;
                maxKm = qMax(maxKm, vr.endM / 1000.0);
            }
        }

        prepararCapa(kCapaViewshed, tr("Análisis: viewshed"), 40);
        if (!cunas.isEmpty()) {
            MapFeature zona;
            zona.layerId = kCapaViewshed;
            zona.kind = GeometryKind::Polygon;
            zona.type = QStringLiteral("zona_visibilidad");
            zona.name = tr("Visibilidad a %1 m").arg(vp.targetHeight, 0, 'f', 0);
            zona.parts = cunas;              // multiparte: las cuñas, con sus huecos
            zona.geometry = cunas.first();
            zona.style.lineColor = QColor(0x15, 0x65, 0xc0, 0);   // sin borde por cuña
            zona.style.fillColor = QColor(0x42, 0xa5, 0xf5, 70);
            zona.style.labelVisible = false;
            zona.selectable = false;
            m_mapa->addFeature(zona);
        }

        MapFeature centro;
        centro.layerId = kCapaViewshed;
        centro.kind = GeometryKind::Point;
        centro.type = QStringLiteral("observador");
        centro.name = tr("Observador");
        centro.geometry = { origen };
        centro.style.lineColor = QColor(0x0d, 0x47, 0xa1);
        centro.selectable = false;
        m_mapa->addFeature(centro);

        statusBar()->showMessage(
            tr("Viewshed: %1 rayos · obs %2 m, obj %3 m · %4 tramos visibles · "
               "máx %5 km · %6 ms")
                .arg(vs.rays.size())
                .arg(vp.observerHeight, 0, 'f', 0)
                .arg(vp.targetHeight, 0, 'f', 0)
                .arg(tramos)
                .arg(maxKm, 0, 'f', 1)
                .arg(ms),
            9000);
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

    // Analisis de elevacion (barra "Elevacion").
    QDoubleSpinBox *m_altA = nullptr;       //!< antena A / altura del observador
    QDoubleSpinBox *m_altB = nullptr;       //!< antena B / altura del objetivo
    QDoubleSpinBox *m_alcanceKm = nullptr;  //!< alcance del viewshed
    const QString kCapaVision = QStringLiteral("elev_vision");
    const QString kCapaViewshed = QStringLiteral("elev_viewshed");

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
