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
#include "libmapa/TrackPlayer.h"

#include "qcustomplot.h"      // ya compilado dentro de libmapa_widget; aquí solo se usa

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
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
#include <QMenuBar>
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
#include <QSet>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QPalette>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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
constexpr int RolBase = Qt::UserRole + 2;    //!< id de capa base (grupo «Base»)

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


// Sección plegable: una cabecera con flecha (QToolButton) que muestra/oculta su
// contenido. No necesita Q_OBJECT (no define señales propias; connect es estático).
// Se usa en la ficha contextual de la entidad para Atributos/Coordenadas/Estilo.
class SeccionPlegable : public QWidget
{
public:
    explicit SeccionPlegable(const QString &titulo, QWidget *parent = nullptr,
                             bool abierta = false)
        : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);

        m_boton = new QToolButton(this);
        m_boton->setText(titulo);
        m_boton->setCheckable(true);
        m_boton->setChecked(abierta);
        m_boton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_boton->setArrowType(abierta ? Qt::DownArrow : Qt::RightArrow);
        m_boton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_boton->setObjectName(QStringLiteral("seccionHeader"));
        v->addWidget(m_boton);

        m_cont = new QWidget(this);
        m_cont->setVisible(abierta);
        v->addWidget(m_cont);

        QObject::connect(m_boton, &QToolButton::toggled, m_cont,
                         [this](bool on) {
                             m_cont->setVisible(on);
                             m_boton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                         });
    }

    //! Widget donde colocar el contenido (se le pone un layout desde fuera).
    QWidget *contenido() const { return m_cont; }

private:
    QToolButton *m_boton;
    QWidget *m_cont;
};

// Dibuja en código un icono de herramienta (glifo lineal) del color \a c, para
// que se recoloree según el tema (claro/oscuro). Estilo coherente: trazo fino,
// 20×20, sin relleno salvo donde ayuda a leerlo. \a k es la clave de la acción.
inline QIcon glifo(const QString &k, const QColor &c)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(c, 1.7);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (k == QLatin1String("navegar")) {               // flecha de cursor
        QPolygonF f({{5, 3}, {5, 16}, {8.5, 12.5}, {11, 17}, {13, 16}, {10.5, 11.5}, {15, 11}});
        p.setBrush(c); p.drawPolygon(f);
    } else if (k == QLatin1String("medir")) {          // regla con marcas
        p.drawRect(QRectF(3, 7, 14, 6));
        for (double x = 5; x < 17; x += 3)
            p.drawLine(QPointF(x, 7), QPointF(x, 10));
    } else if (k == QLatin1String("zoom")) {           // lupa con +
        p.drawEllipse(QPointF(8.5, 8.5), 5, 5);
        p.drawLine(QPointF(12.5, 12.5), QPointF(17, 17));
        p.drawLine(QPointF(8.5, 6), QPointF(8.5, 11));
        p.drawLine(QPointF(6, 8.5), QPointF(11, 8.5));
    } else if (k == QLatin1String("punto")) {          // punto con anillo
        p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        p.setBrush(c); p.drawEllipse(QPointF(10, 10), 2.2, 2.2);
    } else if (k == QLatin1String("linea")) {          // polilínea + vértices
        p.drawPolyline(QPolygonF({{3.5, 15}, {9, 6}, {16.5, 13}}));
        p.setBrush(c);
        for (const QPointF &v : {QPointF(3.5, 15), QPointF(9, 6), QPointF(16.5, 13)})
            p.drawEllipse(v, 1.7, 1.7);
    } else if (k == QLatin1String("poligono")) {       // pentágono + vértices
        QPolygonF pg({{10, 3}, {17, 8.5}, {14, 17}, {6, 17}, {3, 8.5}});
        p.drawPolygon(pg);
        p.setBrush(c);
        for (const QPointF &v : pg) p.drawEllipse(v, 1.5, 1.5);
    } else if (k == QLatin1String("objetivo")) {       // cruz de mira
        p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        p.drawLine(QPointF(10, 1.5), QPointF(10, 5));
        p.drawLine(QPointF(10, 15), QPointF(10, 18.5));
        p.drawLine(QPointF(1.5, 10), QPointF(5, 10));
        p.drawLine(QPointF(15, 10), QPointF(18.5, 10));
        p.setBrush(c); p.drawEllipse(QPointF(10, 10), 1.3, 1.3);
    } else if (k == QLatin1String("area")) {           // pentágono relleno suave
        QPolygonF pg({{10, 3}, {17, 8.5}, {14, 17}, {6, 17}, {3, 8.5}});
        QColor relleno = c; relleno.setAlpha(45);
        p.setBrush(relleno); p.drawPolygon(pg);
        p.setBrush(c); p.drawEllipse(QPointF(10, 3), 2.2, 2.2);   // 1er vértice
    } else if (k == QLatin1String("editar")) {         // lápiz
        QPolygonF cuerpo({{12.5, 3.5}, {16.5, 7.5}, {7.5, 16.5}, {3.5, 12.5}});
        p.drawPolygon(cuerpo);
        p.drawLine(QPointF(11, 5), QPointF(15, 9));     // virola
        p.setBrush(c);                                  // punta de grafito
        p.drawPolygon(QPolygonF({{3.5, 12.5}, {7.5, 16.5}, {2.8, 17.2}}));
    } else if (k == QLatin1String("deshacer") || k == QLatin1String("rehacer")) {
        const bool re = (k == QLatin1String("rehacer"));
        QPainterPath path;                              // flecha curva de retorno
        if (re) { path.moveTo(5, 8); path.cubicTo(7, 4.5, 13, 4.5, 15, 8.5); }
        else    { path.moveTo(15, 8); path.cubicTo(13, 4.5, 7, 4.5, 5, 8.5); }
        p.drawPath(path);
        p.setBrush(c);
        if (re) p.drawPolygon(QPolygonF({{15, 8.5}, {11.5, 9}, {14, 12}}));
        else    p.drawPolygon(QPolygonF({{5, 8.5}, {8.5, 9}, {6, 12}}));
    } else if (k == QLatin1String("borrar")) {         // papelera
        p.drawLine(QPointF(4, 6), QPointF(16, 6));
        p.drawRect(QRectF(5.5, 6, 9, 11));
        p.drawRect(QRectF(8, 3.5, 4, 2.5));
        for (double x = 8; x <= 12; x += 2) p.drawLine(QPointF(x, 8.5), QPointF(x, 14.5));
    } else if (k == QLatin1String("deshacer") || k == QLatin1String("rehacer")) {
        const bool re = (k == QLatin1String("rehacer"));
        QRectF arc(4, 5, 12, 10);
        p.drawArc(arc, 30 * 16, (re ? -200 : 200) * 16);
        const QPointF tip = re ? QPointF(15.5, 7.5) : QPointF(4.5, 7.5);
        const double dx = re ? -1.0 : 1.0;
        p.setBrush(c);
        p.drawPolygon(QPolygonF({tip, {tip.x() + 3.5 * dx, tip.y() - 1},
                                 {tip.x() + 1.5 * dx, tip.y() + 3}}));
    } else if (k == QLatin1String("guardar")) {        // disquete
        QPolygonF cuerpo({{3.5, 3.5}, {14, 3.5}, {16.5, 6}, {16.5, 16.5}, {3.5, 16.5}});
        p.drawPolygon(cuerpo);
        p.drawRect(QRectF(6.5, 3.5, 5, 3.5));
        p.drawRect(QRectF(6, 10.5, 8, 6));
    } else if (k == QLatin1String("abrir")) {          // carpeta
        p.drawPolyline(QPolygonF({{3, 15.5}, {3, 6}, {8, 6}, {9.5, 8}, {17, 8}, {17, 15.5}, {3, 15.5}}));
    } else if (k == QLatin1String("rejilla")) {        // cuadrícula
        p.drawRect(QRectF(4, 4, 12, 12));
        p.drawLine(QPointF(10, 4), QPointF(10, 16));
        p.drawLine(QPointF(4, 10), QPointF(16, 10));
    } else if (k == QLatin1String("cobertura")) {      // mancha / tablero
        QColor relleno = c; relleno.setAlpha(55);
        p.setBrush(relleno);
        p.drawRoundedRect(QRectF(4, 5, 12, 10), 3, 3);
    } else if (k == QLatin1String("geo")) {            // capas apiladas
        p.drawPolygon(QPolygonF({{10, 3.5}, {17, 7}, {10, 10.5}, {3, 7}}));
        p.drawPolyline(QPolygonF({{3, 11}, {10, 14.5}, {17, 11}}));
    } else if (k == QLatin1String("simular")) {        // play
        p.setBrush(c); p.drawPolygon(QPolygonF({{6, 4}, {16, 10}, {6, 16}}));
    } else if (k == QLatin1String("dem")) {            // montaña
        p.drawPolyline(QPolygonF({{3, 16}, {8, 7}, {11, 12}, {13.5, 8.5}, {17, 16}}));
    } else {
        p.drawEllipse(QPointF(10, 10), 5, 5);          // genérico
    }
    p.end();
    return QIcon(pm);
}

} // namespace

class Ventana : public QMainWindow
{
    Q_OBJECT

    //! Punto que se está capturando del mapa (pestaña Elevación).
    enum class Pick { Ninguno, A, B, Anillos, OrigenEbl, DestinoEbl };

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
        // GUARDADO EXPLÍCITO: se apaga el autoguardado. Esto es CLAVE con un
        // paquete: si no, al dejar featuresDbFile vacío la librería usaría el
        // fichero de entidades del propio paquete y guardaría/ recargaría solo.
        // Con autosaveFeatures=false no se toca ese fichero; --features (abajo)
        // se carga a mano y «Guardar» escribe solo cuando el usuario lo pide.
        cfg.autosaveFeatures = false;
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

private:
    // ==================================================== barras ==========
    //! Crea una acción de herramienta (grupo exclusivo de dibujo/navegación).
    QAction *accHerr(const QString &texto, MapTool tool,
                     const QString &atajo = QString(),
                     const QString &claveIcono = QString())
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

    //! Barra de menú + una barra de herramientas principal + una secundaria de
    //! datos/terreno. Todo a partir de acciones compartidas (un solo origen).
    void construirMenuYBarra()
    {
        m_grupo = new QActionGroup(this);
        m_grupo->setExclusive(true);

        // --- Acciones de navegación y dibujo (grupo exclusivo) ---------------
        m_accNavegar  = accHerr(tr("Navegar"), MapTool::None, QString(), QStringLiteral("navegar"));
        m_accNavegar->setChecked(true);
        m_accMedir    = accHerr(tr("Medir"), MapTool::Measure, QString(), QStringLiteral("medir"));
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
        for (QAction *a : {m_accNavegar, m_accMedir, m_accZoomArea, m_accPunto, m_accLinea,
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

        m_btnDem = new QPushButton(tr("DEM..."), this);
        {
            QMenu *md = new QMenu(m_btnDem);
            connect(md->addAction(tr("Carpeta de .hgt...")), &QAction::triggered,
                    this, &Ventana::alElegirDemCarpeta);
            connect(md->addAction(tr("Base de datos .sqlitedb...")), &QAction::triggered,
                    this, &Ventana::alElegirDemDb);
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
        accAnillos->setStatusTip(tr("Anillos alrededor del objetivo seleccionado (lo siguen) "
                                    "o de un punto que pinches."));
        connect(accAnillos, &QAction::triggered, this, &Ventana::ponerAnillos);
        auto *accEbl = mVer->addAction(tr("Marcación (rumbo y distancia)"));
        accEbl->setStatusTip(tr("Línea con rumbo y distancia desde el objetivo seleccionado "
                                "(o un punto) hasta otro objetivo o punto que pinches."));
        connect(accEbl, &QAction::triggered, this, &Ventana::ponerMarcacion);
        // Inicio (verde) y fin (rojo) de las líneas: siempre en las rutas GPX
        // (p. ej. un historial importado), y en cualquier línea seleccionada.
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
        connect(mVer->addAction(tr("Quitar anillos y marcaciones")), &QAction::triggered,
                this, [this] {
                    m_mapa->clearRangeMarks();
                    statusBar()->showMessage(tr("Anillos y marcaciones quitados"), 3000);
                });
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

    // ==================================================== temas ===========
    //! Aplica el tema (claro u oscuro) a toda la app, marca el menú y lo recuerda
    //! en QSettings para la próxima vez. Es una hoja de estilo (QSS) global.
    void aplicarTema(bool oscuro)
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

    //! Paleta base del tema CLARO (cubre los widgets planos que la QSS no toca).
    static QPalette paletaClara()
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

    //! Paleta base del tema OSCURO.
    static QPalette paletaOscura()
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

    //! Pinta el icono de cada acción con la propiedad «glifo» en el color \a fg,
    //! y el del botón DEM. Se llama al cambiar de tema para que casen.
    void aplicarIconos(const QColor &fg)
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

    //! Hoja de estilo del tema CLARO: fondo claro, acento azul, cabeceras y
    //! separadores sutiles. Pensada para lucir de producto sin recargar.
    static QString qssClaro()
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

    //! Hoja de estilo del tema OSCURO: fondo gris/negro, acento azul claro.
    //! Luce muy «técnico» en demos de sala.
    static QString qssOscuro()
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

    //! Borra las capas de resultados de elevación y restablece los pines A/B.
    //! (Era una lambda del botón «Limpiar»; ahora es un slot reutilizable.)
    void limpiarAnalisis()
    {
        m_mapa->removeFeatureLayer(kCapaVision);
        m_mapa->removeFeatureLayer(kCapaViewshed);
        m_mapa->removeFeatureLayer(kCapaPicos);
        m_picoIds.clear(); m_picoBase.clear(); m_picoHover = -1;
        if (m_resultado) m_resultado->clear();
        m_ultimo = Analisis::Ninguno;
        marcarPuntosActivos();
        statusBar()->showMessage(tr("Análisis de elevación limpiado"), 3000);
    }

    //! Al iniciar, los overlays del paquete arrancan OCULTOS (no cargan el
    //! sistema). El operador los enciende desde el panel de capas.
    void ocultarOverlaysAlIniciar()
    {
        // Los overlays del paquete se cargan como capas NO editables; las capas
        // de usuario sí lo son. Ocultamos las no editables (los overlays).
        for (const LayerInfo &capa : m_mapa->featureLayers())
            if (!capa.editable)
                m_mapa->setFeatureLayerVisible(capa.id, false);
    }


    //! Repone el borrador por defecto (sin tipo de dominio, sin tope de
    //! vértices, estilo por defecto). Lo usa volverANavegar al desarmar el alta.
    void reponerBorrador()
    {
        m_mapa->setDraftType(QString());
        m_mapa->setDraftMaxVertices(-1);
        m_mapa->setDraftStyle(FeatureStyle());
    }

    //! Herramientas genéricas (Punto/Línea/Polígono): abre el diálogo de alta
    //! con el estilo que haya configurado el usuario en el panel.
    void nuevaEntidadGenerica()
    {
        configurarNuevaEntidad(QString(), estiloActual(), -1, tr("Entidad"));
    }

    //! Modo "Objetivo": punto con type "objetivo" y etiqueta visible; su rótulo
    //! (y el punto) se arrastran. Pasa por el diálogo de alta.
    void modoObjetivo()
    {
        FeatureStyle s;
        s.labelVisible = true;
        s.pointRadiusPx = 7.0;
        s.lineColor = QColor(0xff, 0x8f, 0x00);
        s.fillColor = QColor(0xff, 0x8f, 0x00, 160);
        configurarNuevaEntidad(QStringLiteral("objetivo"), s, -1, tr("Objetivo"));
    }

    //! Modo "Área objetivo": polígono de <=10 vértices (type "objetivo_area")
    //! con la etiqueta colgada de su PRIMER punto (labelAnchorVertex=0).
    void modoAreaObjetivo()
    {
        FeatureStyle s;
        s.labelVisible = true;
        s.labelAnchorVertex = 0;
        s.lineColor = QColor(0xc6, 0x28, 0x28);
        s.fillColor = QColor(0xc6, 0x28, 0x28, 40);
        s.lineWidth = 2.0;
        configurarNuevaEntidad(QStringLiteral("objetivo_area"), s, 10, tr("Área"));
    }

    //! Vuelve a la herramienta Navegar y desarma el alta de entidad en curso.
    void volverANavegar()
    {
        m_esperandoDibujo = false;
        m_nombrePendiente.clear();
        reponerBorrador();
        if (m_accNavegar)
            m_accNavegar->setChecked(true);
        m_mapa->setActiveTool(MapTool::None);
    }

    //! Flujo de alta de una entidad: pregunta nombre/capa/colores y, al aceptar,
    //! configura el borrador y deja que el usuario dibuje; al terminar el trazo
    //! (señal featureAdded → alEntidadCreada) se le pone el nombre y se vuelve a
    //! Navegar. Se DIFIERE con un singleShot(0) para que el diálogo salga después
    //! de que el grupo de acciones fije la herramienta de dibujo; así, al
    //! cancelar, volverANavegar no queda pisado por esa activación automática.
    void configurarNuevaEntidad(const QString &tipo, FeatureStyle base,
                                int maxVert, const QString &nombreSug)
    {
        QTimer::singleShot(0, this, [this, tipo, base, maxVert, nombreSug]() mutable {
            QString nombre, capa;
            QColor cl = base.lineColor, cr = base.fillColor;
            double ancho = base.lineWidth > 0 ? base.lineWidth : 1.5;
            if (!dialogoNuevaEntidad(nombreSug, &nombre, &capa, &cl, &cr, &ancho)) {
                volverANavegar();
                return;
            }
            if (capa.isEmpty())
                capa = m_mapa->activeFeatureLayer();
            // addFeatureLayer no-op si la capa ya existe; la crea si es nueva.
            m_mapa->addFeatureLayer(capa, capa,
                static_cast<int>(m_mapa->featureLayers().size()) * 10);
            m_mapa->setActiveFeatureLayer(capa);

            base.lineColor = cl;
            base.fillColor = cr;
            base.lineWidth = ancho;
            m_mapa->setDraftType(tipo);
            m_mapa->setDraftStyle(base);
            m_mapa->setDraftMaxVertices(maxVert);

            m_nombrePendiente = nombre;
            m_esperandoDibujo = true;
            statusBar()->showMessage(
                tr("Dibuja la entidad en el mapa; al terminar vuelve a Navegar."),
                6000);
        });
    }

    //! Diálogo modal de alta: nombre, capa (editable: se puede teclear una
    //! nueva) y colores de borde/relleno + ancho. Devuelve true si se aceptó.
    bool dialogoNuevaEntidad(const QString &nombreSug, QString *nombre,
                             QString *capa, QColor *linea, QColor *relleno,
                             double *ancho)
    {
        QDialog d(this);
        d.setWindowTitle(tr("Nueva entidad"));
        auto *form = new QFormLayout(&d);

        auto *edNombre = new QLineEdit(nombreSug, &d);
        form->addRow(tr("Nombre:"), edNombre);

        auto *cbCapa = new QComboBox(&d);
        cbCapa->setEditable(true);           // permite teclear una capa nueva
        for (const LayerInfo &c : m_mapa->featureLayers())
            if (c.editable)
                cbCapa->addItem(c.displayName.isEmpty() ? c.id : c.displayName, c.id);
        const QString activa = m_mapa->activeFeatureLayer();
        if (const int i = cbCapa->findData(activa); i >= 0)
            cbCapa->setCurrentIndex(i);
        else if (!activa.isEmpty())
            cbCapa->setCurrentText(activa);
        form->addRow(tr("Capa:"), cbCapa);

        QColor cl = *linea, cr = *relleno;
        auto *btnL = new QPushButton(&d);
        auto *btnR = new QPushButton(&d);
        pintarBotonColor(btnL, cl);
        pintarBotonColor(btnR, cr);
        connect(btnL, &QPushButton::clicked, &d, [this, &d, &cl, btnL] {
            const QColor c = QColorDialog::getColor(cl, &d, tr("Color de borde"),
                                                    QColorDialog::ShowAlphaChannel);
            if (c.isValid()) { cl = c; pintarBotonColor(btnL, c); }
        });
        connect(btnR, &QPushButton::clicked, &d, [this, &d, &cr, btnR] {
            const QColor c = QColorDialog::getColor(cr, &d, tr("Color de relleno"),
                                                    QColorDialog::ShowAlphaChannel);
            if (c.isValid()) { cr = c; pintarBotonColor(btnR, c); }
        });
        form->addRow(tr("Color de borde:"), btnL);
        form->addRow(tr("Color de relleno:"), btnR);

        auto *spAncho = new QDoubleSpinBox(&d);
        spAncho->setRange(0.5, 12.0);
        spAncho->setSingleStep(0.5);
        spAncho->setValue(*ancho);
        form->addRow(tr("Ancho de línea:"), spAncho);

        auto *bb = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);

        if (d.exec() != QDialog::Accepted)
            return false;

        *nombre = edNombre->text().trimmed();
        // Si el texto de la capa coincide con un ítem, usa su id (data); si el
        // usuario tecleó algo nuevo, ese texto ES el id de la capa nueva.
        // OJO: en un combo editable currentData()/currentIndex() NO cambian al
        // teclear texto nuevo (siguen en la capa preseleccionada), así que se
        // resuelve por TEXTO: si coincide con un ítem, se usa su id (data); si
        // el usuario tecleó algo nuevo, ese texto ES el id de la capa nueva.
        const QString texto = cbCapa->currentText().trimmed();
        const int i = cbCapa->findText(texto);
        *capa = (i >= 0) ? cbCapa->itemData(i).toString() : texto;
        *linea = cl;
        *relleno = cr;
        *ancho = spAncho->value();
        return true;
    }

    //! Llega por featureAdded: si hay un alta en curso, pone el nombre elegido a
    //! la entidad recién dibujada, la deja seleccionada y vuelve a Navegar.
    void alEntidadCreada(qint64 id)
    {
        if (!m_esperandoDibujo)
            return;
        m_esperandoDibujo = false;
        if (!m_nombrePendiente.isEmpty()) {
            if (auto f = m_mapa->feature(id)) {
                f->name = m_nombrePendiente;
                m_mapa->updateFeature(*f);
            }
        }
        m_nombrePendiente.clear();
        marcarSucio();               // entidad nueva: hay algo que guardar
        volverANavegar();
        m_mapa->selectFeature(id);   // deja ver sus datos en el panel
    }

    //! Si la entidad seleccionada es un área objetivo, lista sus coordenadas en
    //! la barra de estado (demuestra el acceso a los puntos vía feature()).
    void mostrarCoordsArea(qint64 id)
    {
        const auto f = m_mapa->feature(id);
        if (!f || f->type != QLatin1String("objetivo_area"))
            return;
        QStringList pts;
        for (const QGeoCoordinate &c : f->geometry)
            pts << QStringLiteral("%1,%2")
                       .arg(c.latitude(), 0, 'f', 5).arg(c.longitude(), 0, 'f', 5);
        statusBar()->showMessage(
            tr("Área (%1 pts): %2").arg(f->geometry.size()).arg(pts.join(QStringLiteral("  "))),
            8000);
    }

    // ==================================================== panel ===========
    void construirPanel()
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
        // Mismo slot que el menú «Elevación → Limpiar análisis» (sin duplicar).
        connect(bLimpiar, &QPushButton::clicked, this, &Ventana::limpiarAnalisis);
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

        // El arrastre de los pines A/B ya NO se hace aquí: lo gestiona la librería
        // (entidad marcada draggable + señales featureDrag*, ver construirPanel).

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
                QToolTip::showText(QCursor::pos(), tip, m_mapa);   // global, Qt5/Qt6
            else
                QToolTip::hideText();
        }
        return QMainWindow::eventFilter(obj, ev);
    }

    // Vuelca una coordenada (arrastre de un pin A/B) en sus campos lat/lon, que
    // redibujan el pin y la lectura al vuelo. La coordenada la da la librería por
    // la señal featureDragMovedTo.
    void moverPinCoord(Pick cual, const QGeoCoordinate &g)
    {
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
        if (m_picking == Pick::Anillos || m_picking == Pick::OrigenEbl
            || m_picking == Pick::DestinoEbl) {
            puntoDeMedida(p);
            return;
        }
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
            f.selectable = true;
            f.draggable = true;         // la librería lo arrastra con «Navegar»
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
        }

        // --- Capas vectoriales: overlays (no editables) vs entidades ----------
        // featureLayers() viene por zOrder ascendente; se invierte para que
        // arriba en el panel sea arriba en el mapa.
        QTreeWidgetItem *gOver = nuevoGrupo(tr("Overlays (solo lectura)"));
        QTreeWidgetItem *gEnt  = nuevoGrupo(tr("Entidades (editables)"));
        auto capas = m_mapa->featureLayers();
        std::reverse(capas.begin(), capas.end());

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

    //! Crea una cabecera de grupo del árbol de capas (no seleccionable ni con
    //! casilla; solo organiza visualmente las tres categorías).
    QTreeWidgetItem *nuevoGrupo(const QString &titulo)
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

    void visibilidadCambiada(QTreeWidgetItem *item, int col)
    {
        if (col != 0)
            return;

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

    //! Marca en el árbol la base activa (cuando cambia desde la barra o el menú,
    //! que no reconstruyen el panel). Solo toca las casillas del grupo «Base».
    void sincronizarBaseEnArbol(const QString &id)
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

    void resaltarCapaActiva()
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

    // ==================================================== borrado =========
    void borrarSeleccion()
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
        marcarSucio();
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
        marcarSucio();
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
        marcarSucio();
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
            marcarSucio();
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
        marcarSucio();
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
        marcarSucio();
    }

    //! Vuelca la entidad seleccionada a los campos del panel.
    void mostrarPropiedades(qint64 id)
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

    //! ¿La capa de esta entidad es editable? (Las de usuario sí; las de overlays
    //! del paquete, no.) Decide si se pueden tocar las coordenadas.
    bool capaEditable(const QString &layerId) const
    {
        for (const LayerInfo &c : m_mapa->featureLayers())
            if (c.id == layerId)
                return c.editable;
        return false;
    }

    //! Vuelca los vértices de la entidad seleccionada a la tabla de coordenadas.
    //! Reutiliza el guardia m_actualizandoPropiedades para no reescribir al
    //! rellenar. Si la entidad no es editable, la tabla queda de solo lectura.
    void mostrarCoords(qint64 id)
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

    //! Crea una celda con una coordenada formateada a 6 decimales.
    static QTableWidgetItem *celdaCoord(double v)
    {
        return new QTableWidgetItem(QString::number(v, 'f', 6));
    }

    //! Una celda de lat/lon cambió: mueve ese vértice (moveVertex).
    void coordEditada(int fila, int /*col*/)
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

    //! Inserta un vértice tras el seleccionado, en el punto medio hacia el
    //! siguiente (en polígono cierra el anillo; en línea usa el vecino que haya).
    void anadirVertice()
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

    //! Quita el vértice seleccionado (la librería rechaza bajar del mínimo:
    //! 2 en línea, 3 en polígono).
    void quitarVertice()
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
        if (m_mapa->saveFeaturesTo(m_archivoActual)) {
            m_sucio = false;
            actualizarTitulo();
            statusBar()->showMessage(tr("Guardadas %1 entidades en %2")
                .arg(m_mapa->featureCount()).arg(m_archivoActual), 6000);
        } else {
            QMessageBox::warning(this, tr("Guardar"),
                tr("No se pudo guardar en:\n%1").arg(m_archivoActual));
        }
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
            m_sucio = false;
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
        // Cargar sustituye TODO. Con guardado explícito, primero se ofrece
        // guardar lo pendiente (y se puede cancelar la apertura).
        if (!confirmarDescartar())
            return;

        const QString ruta = QFileDialog::getOpenFileName(this,
            tr("Abrir entidades"), dirTrabajo(),
            tr("Base de datos SQLite (*.db)"));
        if (ruta.isEmpty())
            return;

        if (m_mapa->loadFeaturesFrom(ruta)) {
            m_archivoActual = ruta;
            m_ultimoDir = QFileInfo(ruta).absolutePath();
            m_sucio = false;
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
        // Un asterisco delante marca que hay cambios sin guardar (explícito).
        const QString marca = m_sucio ? QStringLiteral("*") : QString();
        const DataPackageInfo paquete = m_mapa ? m_mapa->packageInfo() : DataPackageInfo();
        const QString datos = paquete.isValid()
            ? tr("  -  datos: %1 %2").arg(paquete.name, paquete.dataVersion) : QString();
        setWindowTitle(tr("libmapa - demostracion  -  %1%2%3").arg(marca, f, datos));
    }

    //! Marca el documento como modificado (guardado explícito): refresca el
    //! título con el asterisco. Lo llaman las ediciones de entidades de usuario;
    //! las capas temporales (análisis, A/B) NO lo llaman, por eso no ensucian.
    void marcarSucio()
    {
        if (m_sucio)
            return;
        m_sucio = true;
        actualizarTitulo();
    }

    //! Pregunta si guardar los cambios pendientes. Devuelve false solo si el
    //! usuario cancela (para abortar el cierre o la apertura de otro fichero).
    bool confirmarDescartar()
    {
        if (!m_sucio)
            return true;
        const auto r = QMessageBox::question(this, tr("Cambios sin guardar"),
            tr("Hay entidades sin guardar. ¿Quieres guardarlas?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (r == QMessageBox::Cancel)
            return false;
        if (r == QMessageBox::Save) {
            guardar();
            return !m_sucio;      // si el guardado se canceló/falló, no sigas
        }
        return true;              // Descartar
    }

    //! Al cerrar la ventana, ofrece guardar lo pendiente (guardado explícito).
    void closeEvent(QCloseEvent *e) override
    {
        if (confirmarDescartar())
            e->accept();
        else
            e->ignore();
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
        marcarSucio();               // el .geo entra como entidad guardable
        statusBar()->showMessage(tr("Cargado %1 como una entidad en la capa '%2'")
            .arg(QFileInfo(ruta).fileName(), id), 5000);
    }

    // Pide una coordenada (decimal, GMS, UTM o MGRS), centra el mapa en ella y
    // la muestra en todos los formatos, como comprobación cruzada.
    void irACoordenada()
    {
        bool aceptado = false;
        const QString txt = QInputDialog::getText(this, tr("Ir a coordenada"),
            tr("Coordenada (p. ej. 23.1136, -82.3666 · 17Q LF 60056 56750 · "
               "17Q 360057 2556751 · 23°06'49\"N 82°22'00\"W):"),
            QLineEdit::Normal, QString(), &aceptado);
        if (!aceptado || txt.trimmed().isEmpty())
            return;
        bool ok = false;
        const QGeoCoordinate c = parseCoordinate(txt, &ok);
        if (!ok) {
            QMessageBox::warning(this, tr("Ir a coordenada"),
                                 tr("No se reconoce «%1» como coordenada.").arg(txt));
            return;
        }
        m_mapa->setCenter(c);
        statusBar()->showMessage(tr("%1  ·  %2  ·  %3")
            .arg(formatCoordinate(c, CoordinateFormat::DMS),
                 formatCoordinate(c, CoordinateFormat::UTM),
                 formatCoordinate(c, CoordinateFormat::MGRS)), 15000);
    }

    // Importa un GeoJSON/KML/GPX como capa nueva (id = nombre del fichero). Cada
    // entidad del fichero entra con su nombre, atributos y estilo.
    void importarVectorial()
    {
        const QString ruta = QFileDialog::getOpenFileName(this,
            tr("Importar entidades"), dirTrabajo(),
            tr("Vectorial (*.geojson *.json *.kml *.gpx);;Todos (*)"));
        if (ruta.isEmpty())
            return;
        const QString id = QFileInfo(ruta).completeBaseName().toLower();
        QString error;
        const qint64 fid = m_mapa->importVectorFile(ruta, id,
            QFileInfo(ruta).completeBaseName(), &error);
        if (fid < 0) {
            QMessageBox::warning(this, tr("Importar"), tr("No se pudo importar:\n%1").arg(error));
            return;
        }
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        m_mapa->setActiveFeatureLayer(id);
        marcarSucio();
        statusBar()->showMessage(tr("Importadas %1 entidades en la capa '%2'")
            .arg(m_mapa->featuresInLayer(id).size()).arg(id), 5000);
    }

    // Exporta la capa activa; el formato lo decide la extension elegida.
    void exportarVectorial()
    {
        const QString capa = m_mapa->activeFeatureLayer();
        const QString ruta = QFileDialog::getSaveFileName(this,
            tr("Exportar capa '%1'").arg(capa), dirTrabajo() + QLatin1Char('/') + capa + QStringLiteral(".geojson"),
            tr("GeoJSON (*.geojson);;KML (*.kml);;GPX (*.gpx)"));
        if (ruta.isEmpty())
            return;
        QString error;
        if (!m_mapa->exportVectorFile(ruta, capa, &error)) {
            QMessageBox::warning(this, tr("Exportar"), tr("No se pudo exportar:\n%1").arg(error));
            return;
        }
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        statusBar()->showMessage(tr("Capa '%1' exportada a %2")
            .arg(capa, QFileInfo(ruta).fileName()), 5000);
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
        // Variantes de ALERTA: objetivo dentro de una zona vigilada.
        const QColor alerta(0xff, 0x17, 0x44);
        m_icoBuqueZona = iconoBuque(alerta);
        m_icoAereoZona = iconoAereo(alerta);
        m_icoUavZona   = iconoUav(alerta);

        m_mapa->setTargetSymbolProvider([this](const MapTarget &t) {
            TargetSymbol s;                         // icono nulo => galon
            // Dentro de una zona vigilada: icono rojo y algo mayor (la alerta
            // se VE en el mapa, no solo en la barra de estado).
            const bool enZona = !m_mapa->zonesOfTarget(t.id).isEmpty();
            if (t.kind == QLatin1String("buque"))
                s.icon = enZona ? m_icoBuqueZona : m_icoBuque;
            else if (t.kind == QLatin1String("aeronave"))
                s.icon = enZona ? m_icoAereoZona : m_icoAereo;
            else if (t.kind == QLatin1String("uav"))
                s.icon = enZona ? m_icoUavZona
                       : t.attributes.value(QStringLiteral("bateria")).toInt() < 20
                             ? m_icoUavBajo : m_icoUav;
            s.scale = enZona ? 1.3 : 1.0;
            s.rotateWithHeading = true;
            return s;
        });

        // Con miles de objetivos se dejan de rotular/trazar al amontonarse.
        m_mapa->setTargetDetailBudget(200, 600);

        // Clic sobre un objetivo -> la libreria lo resalta y emite targetClicked;
        // aqui mostramos sus datos de dominio (sus attributes) en la barra de
        // estado. Es el cierre del ciclo ver -> seleccionar -> consultar.
        // Alertas de zona: la librería avisa al cruzar el borde de una zona vigilada.
        connect(m_mapa, &MapWidget::targetEnteredZone, this, [this](qint64 obj, qint64 zona) {
            statusBar()->showMessage(textoAlertaZona(obj, false, zona, true), 6000);
            actualizarIndicadorAlertas();
        });
        connect(m_mapa, &MapWidget::targetLeftZone, this, [this](qint64 obj, qint64 zona) {
            statusBar()->showMessage(textoAlertaZona(obj, false, zona, false), 6000);
            actualizarIndicadorAlertas();
        });
        // Alertas de proximidad: aviso en la barra y recuento en el indicador.
        connect(m_mapa, &MapWidget::proximityAlertRaised, this,
                [this](qint64 a, qint64 b, double cpa, double tcpa) {
                    statusBar()->showMessage(textoProximidad(a, b, cpa, tcpa), 8000);
                    actualizarIndicadorAlertas();
                });
        connect(m_mapa, &MapWidget::proximityAlertCleared, this,
                [this](qint64, qint64) { actualizarIndicadorAlertas(); });
        // Los «Objetivo» que coloca el operador son ENTIDADES punto: su propia señal.
        connect(m_mapa, &MapWidget::featureEnteredZone, this, [this](qint64 ent, qint64 zona) {
            statusBar()->showMessage(textoAlertaZona(ent, true, zona, true), 8000);
            actualizarIndicadorAlertas();
        });
        connect(m_mapa, &MapWidget::featureLeftZone, this, [this](qint64 ent, qint64 zona) {
            statusBar()->showMessage(textoAlertaZona(ent, true, zona, false), 8000);
            actualizarIndicadorAlertas();
        });

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
                    // Origen: donde y cuando se vio por primera vez (la
                    // librería guarda el historial entero y, al estar
                    // seleccionado, lo dibuja desde ese punto).
                    const TargetTrackPoint o = m_mapa->targetOrigin(id);
                    const QString origen = o.isValid()
                        ? tr("   origen %1 a las %2 (%3 puntos)")
                              .arg(formatCoordinate(o.position, m_formatoCoord),
                                   o.time.toLocalTime().toString(QStringLiteral("HH:mm:ss")))
                              .arg(m_mapa->targetHistory(id).size())
                        : QString();
                    statusBar()->showMessage(
                        tr("Objetivo %1 [%2]  %3%4")
                            .arg(id).arg(t->kind, datos.join(QStringLiteral("   ")), origen),
                        12000);
                });
    }

    // Respuesta a «Seleccionar área» / «Seleccionar polígono»: informa de la
    // zona marcada (tamaño, extensión y qué objetivos y entidades hay dentro)
    // y ofrece lo que se puede hacer con ella. Al cerrar, quita la marca y
    // vuelve a Navegar.
    void zonaSeleccionada(const QVector<QGeoCoordinate> &zona, bool rectangulo)
    {
        double s = 90, n = -90, w = 180, e = -180;
        for (const QGeoCoordinate &c : zona) {
            s = qMin(s, c.latitude());  n = qMax(n, c.latitude());
            w = qMin(w, c.longitude()); e = qMax(e, c.longitude());
        }
        const QGeoCoordinate nw(n, w), se(s, e);
        const double m2 = polygonArea(zona);
        const QString superficie = m2 >= 1e6
            ? tr("%1 km²").arg(m2 / 1e6, 0, 'f', m2 >= 1e8 ? 0 : 2)
            : tr("%1 ha").arg(m2 / 1e4, 0, 'f', 2);
        const double per = pathLength(zona, true);
        const QString perimetro = per >= 1000 ? tr("%1 km").arg(per / 1000, 0, 'f', 2)
                                              : tr("%1 m").arg(per, 0, 'f', 0);

        // Qué hay dentro: nombres de los primeros objetivos y entidades.
        const QVector<qint64> objs = m_mapa->targetsInArea(zona);
        const QVector<qint64> ents = m_mapa->featuresInArea(zona);
        const auto lista = [](const QStringList &l) {
            return l.size() <= 5 ? l.join(QStringLiteral(", "))
                                 : QStringList(l.mid(0, 5)).join(QStringLiteral(", "))
                                       + QStringLiteral(", …");
        };
        QStringList nomObjs, nomEnts;
        for (qint64 id : objs) {
            const auto t = m_mapa->target(id);
            nomObjs << (t && !t->label.isEmpty() ? t->label.section(QLatin1Char('\n'), 0, 0)
                                                 : tr("objetivo %1").arg(id));
        }
        for (qint64 id : ents) {
            const auto f = m_mapa->feature(id);
            nomEnts << (f && !f->name.isEmpty() ? f->name.section(QLatin1Char('\n'), 0, 0)
                                                : tr("entidad %1").arg(id));
        }

        QString txt = tr("<b>%1</b><br>Superficie: %2 · perímetro: %3<br>"
                         "NO: %4<br>SE: %5<br><br>")
            .arg(rectangulo ? tr("Área rectangular")
                            : tr("Polígono de %1 vértices").arg(zona.size()),
                 superficie, perimetro,
                 formatCoordinate(nw, m_formatoCoord).toHtmlEscaped(),
                 formatCoordinate(se, m_formatoCoord).toHtmlEscaped());
        txt += tr("Objetivos dentro: <b>%1</b>").arg(objs.size());
        if (!nomObjs.isEmpty())
            txt += QStringLiteral(" (") + lista(nomObjs).toHtmlEscaped() + QStringLiteral(")");
        txt += tr("<br>Entidades dentro: <b>%1</b>").arg(ents.size());
        if (!nomEnts.isEmpty())
            txt += QStringLiteral(" (") + lista(nomEnts).toHtmlEscaped() + QStringLiteral(")");

        QMessageBox box(QMessageBox::Information, tr("Zona seleccionada"), txt,
                        QMessageBox::NoButton, this);
        box.setTextFormat(Qt::RichText);
        box.setInformativeText(
            tr("La zona marcada es temporal: no se guarda. Para conservarla, "
               "conviértela en un área de la capa activa."));
        QPushButton *bCrear = box.addButton(tr("Crear área"), QMessageBox::ActionRole);
        QPushButton *bVigilar = box.addButton(tr("Crear y vigilar"), QMessageBox::ActionRole);
        QPushButton *bExportar = box.addButton(tr("Exportar zona..."), QMessageBox::ActionRole);
        box.addButton(tr("Cerrar"), QMessageBox::RejectRole);
        bCrear->setToolTip(tr("Crea un polígono con esta forma en la capa activa."));
        bVigilar->setToolTip(tr("Crea el polígono y avisa cuando un objetivo entre o salga."));
        bExportar->setToolTip(tr("Encuadra la zona y la guarda como imagen o PDF."));
        box.exec();

        m_mapa->clearAreaSelection();
        volverANavegar();

        const QAbstractButton *pulsado = box.clickedButton();
        if (pulsado == bExportar) {
            m_mapa->fitBounds(nw, se);
            exportarVista(rectangulo ? tr("Área seleccionada") : tr("Zona seleccionada"));
            return;
        }
        if (pulsado != bCrear && pulsado != bVigilar)
            return;

        MapFeature f;
        f.kind = GeometryKind::Polygon;
        f.type = QStringLiteral("zona");
        f.name = tr("Zona %1").arg(++m_zonasCreadas);
        f.layerId = m_mapa->activeFeatureLayer();
        f.geometry = zona;
        f.style.lineColor = QColor(0xff, 0x6d, 0x00);
        f.style.lineWidth = 2.0;
        f.style.fillColor = QColor(0xff, 0x6d, 0x00, 40);
        const qint64 id = m_mapa->addFeature(f);
        if (id < 0) {
            statusBar()->showMessage(tr("No se pudo crear la zona"), 4000);
            return;
        }
        m_mapa->selectFeature(id);
        if (pulsado == bVigilar) {
            alternarVigilancia();                 // vigila la recién seleccionada
        } else {
            statusBar()->showMessage(tr("«%1» creada en la capa '%2'")
                                         .arg(f.name, f.layerId), 5000);
        }
    }

    // Exporta la vista actual a imagen o PDF. Pide el fichero y un título
    // opcional; el pie lleva extensión, fecha, escala (PDF) y la atribución
    // de la base, como exige el proveedor de la imagen.
    void exportarVista(const QString &tituloSugerido = QString())
    {
        QString filtro;
        const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar vista"),
            dirTrabajo() + QStringLiteral("/mapa.pdf"),
            tr("PDF (*.pdf);;Imagen PNG (*.png);;Imagen JPEG (*.jpg)"), &filtro);
        if (ruta.isEmpty())
            return;
        bool ok = false;
        const QString titulo = QInputDialog::getText(this, tr("Exportar vista"),
            tr("Título (opcional):"), QLineEdit::Normal, tituloSugerido, &ok);
        if (!ok)
            return;
        MapExportOptions o;
        o.title = titulo;
        o.attribution = tr("Base: %1").arg(m_capasBase ? m_capasBase->currentText() : QString());
        o.imageScale = 2.0;                       // imagen nítida para informes
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool bien = m_mapa->exportView(ruta, o, &error);
        QApplication::restoreOverrideCursor();
        if (!bien) {
            QMessageBox::warning(this, tr("Exportar vista"), tr("No se pudo exportar:\n%1").arg(error));
            return;
        }
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        statusBar()->showMessage(tr("Vista exportada a %1").arg(QFileInfo(ruta).fileName()), 5000);
    }

    // Abre un historial GPX y lo reproduce con la barra de reproducción. La
    // simulación se para: mezclar objetivos simulados y reproducidos confunde.
    void abrirReproduccion()
    {
        const QString ruta = QFileDialog::getOpenFileName(this, tr("Reproducir historial"),
            dirTrabajo(), tr("GPX (*.gpx)"));
        if (ruta.isEmpty())
            return;
        if (m_accSimular && m_accSimular->isChecked())
            m_accSimular->setChecked(false);
        if (!m_player) {
            m_player = new TrackPlayer(m_mapa, this);
            crearBarraReproduccion();
        }
        QString error;
        if (!m_player->loadFile(ruta, &error)) {
            QMessageBox::warning(this, tr("Reproducir historial"),
                                 tr("No se pudo cargar:\n%1").arg(error));
            return;
        }
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        m_player->setRoutesVisible(m_repRutas->isChecked());
        m_barraRep->show();
        m_accSalirRep->setEnabled(true);
        m_modoRep->show();
        statusBar()->showMessage(tr("%1 pistas de %2 a %3. Pulsa ▶ para reproducir; "
                                    "Esc o «Salir de la reproducción» para terminar.")
            .arg(m_player->tracks().size())
            .arg(m_player->startTime().toLocalTime().toString(QStringLiteral("dd/MM HH:mm:ss")),
                 m_player->endTime().toLocalTime().toString(QStringLiteral("dd/MM HH:mm:ss"))),
            8000);
    }

    // Barra de reproducción (abajo): reproducir/pausa, parar, deslizador de
    // tiempo (mil pasos sobre el rango del fichero), hora actual, velocidad,
    // rutas completas y cerrar. El deslizador manda al reproductor y el
    // reproductor al deslizador (con las señales bloqueadas, para no rebotar).
    void crearBarraReproduccion()
    {
        m_barraRep = new QToolBar(tr("Reproducción"), this);
        m_barraRep->setObjectName(QStringLiteral("barraReproduccion"));
        m_barraRep->setMovable(false);
        // Que no se pueda ocultar desde el menú contextual de las barras: se
        // quedaría la reproducción activa sin controles. Se sale con su botón.
        m_barraRep->toggleViewAction()->setEnabled(false);
        addToolBar(Qt::BottomToolBarArea, m_barraRep);

        auto *play = m_barraRep->addAction(style()->standardIcon(QStyle::SP_MediaPlay), tr("Reproducir"));
        play->setCheckable(true);
        connect(play, &QAction::triggered, this,
                [this](bool on) { on ? m_player->play() : m_player->pause(); });
        connect(m_player, &TrackPlayer::playingChanged, this, [this, play](bool on) {
            QSignalBlocker b(play);
            play->setChecked(on);
            play->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        });
        auto *parar = m_barraRep->addAction(style()->standardIcon(QStyle::SP_MediaStop), tr("Al principio"));
        connect(parar, &QAction::triggered, m_player, &TrackPlayer::stop);

        m_repSlider = new QSlider(Qt::Horizontal, m_barraRep);
        m_repSlider->setRange(0, 1000);
        m_repSlider->setMinimumWidth(300);
        m_barraRep->addWidget(m_repSlider);
        connect(m_repSlider, &QSlider::valueChanged, this, [this](int v) {
            const qint64 total = m_player->startTime().msecsTo(m_player->endTime());
            m_player->seek(m_player->startTime().addMSecs(total * v / 1000));
        });

        m_repHora = new QLabel(m_barraRep);
        m_repHora->setMinimumWidth(170);
        m_repHora->setAlignment(Qt::AlignCenter);
        m_barraRep->addWidget(m_repHora);
        connect(m_player, &TrackPlayer::timeChanged, this, [this](const QDateTime &t) {
            m_repHora->setText(t.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")));
            const qint64 total = m_player->startTime().msecsTo(m_player->endTime());
            QSignalBlocker b(m_repSlider);
            m_repSlider->setValue(total > 0 ? int(m_player->startTime().msecsTo(t) * 1000 / total) : 0);
        });

        auto *vel = new QComboBox(m_barraRep);
        for (int f : { 1, 10, 60, 300, 1800 })
            vel->addItem(QStringLiteral("×%1").arg(f), f);
        vel->setCurrentIndex(2);                       // ×60: un minuto por segundo
        vel->setToolTip(tr("Velocidad respecto al tiempo real"));
        m_barraRep->addWidget(vel);
        connect(vel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, vel](int) { m_player->setSpeed(vel->currentData().toDouble()); });

        m_repRutas = new QCheckBox(tr("Rutas"), m_barraRep);
        m_repRutas->setChecked(true);
        m_repRutas->setToolTip(tr("Recorrido completo de cada pista, con su inicio y su fin"));
        m_barraRep->addWidget(m_repRutas);
        connect(m_repRutas, &QCheckBox::toggled, m_player, &TrackPlayer::setRoutesVisible);

        m_barraRep->addSeparator();
        auto *salir = new QToolButton(m_barraRep);
        salir->setDefaultAction(m_accSalirRep);
        salir->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_barraRep->addWidget(salir);
    }

    // Sale del modo reproducción: el reproductor quita sus objetivos y sus
    // rutas (el mapa queda como antes de abrir el historial), se cierra la
    // barra y se apaga el indicador. Sin reproducción activa no hace nada.
    void salirReproduccion()
    {
        if (!m_player || !m_player->isLoaded())
            return;
        m_player->clear();
        m_barraRep->hide();
        m_modoRep->hide();
        m_accSalirRep->setEnabled(false);
        statusBar()->showMessage(tr("Reproducción cerrada"), 4000);
    }

    // Exporta el historial completo de todos los objetivos a GPX con horas.
    void exportarHistorialGpx()
    {
        if (m_mapa->targets().isEmpty()) {
            statusBar()->showMessage(tr("No hay objetivos (Datos → Simular objetivos)"), 4000);
            return;
        }
        const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar historial de objetivos"),
            dirTrabajo() + QStringLiteral("/historial_objetivos.gpx"), tr("GPX (*.gpx)"));
        if (ruta.isEmpty())
            return;
        QString error;
        if (!m_mapa->exportTargetHistory(ruta, {}, &error)) {
            QMessageBox::warning(this, tr("Exportar historial"), tr("No se pudo exportar:\n%1").arg(error));
            return;
        }
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        statusBar()->showMessage(tr("Historial de %1 objetivos exportado a %2")
            .arg(m_mapa->targets().size()).arg(QFileInfo(ruta).fileName()), 5000);
    }

    // Activa o apaga las alertas de proximidad. Al activarlas pide distancia (en
    // millas náuticas) y horizonte (minutos); se recuerdan para la próxima vez.
    void configurarProximidad(bool activar)
    {
        if (!activar) {
            m_mapa->setProximityAlert(0.0);
            actualizarIndicadorAlertas();
            statusBar()->showMessage(tr("Alertas de proximidad apagadas"), 4000);
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Alertas de proximidad (CPA/TCPA)"));
        auto *form = new QFormLayout(&dlg);
        auto *distancia = new QDoubleSpinBox(&dlg);
        distancia->setRange(0.05, 50.0);
        distancia->setDecimals(2);
        distancia->setSingleStep(0.25);
        distancia->setSuffix(tr(" NM"));
        distancia->setValue(m_cpaMillas);
        auto *horizonte = new QSpinBox(&dlg);
        horizonte->setRange(1, 240);
        horizonte->setSuffix(tr(" min"));
        horizonte->setValue(m_cpaMinutos);
        auto *explica = new QLabel(tr("Avisar si dos objetivos van a pasar a menos de una "
                                      "distancia dentro de un tiempo, suponiendo que "
                                      "mantienen rumbo y velocidad."), &dlg);
        explica->setWordWrap(true);
        form->addRow(explica);
        form->addRow(tr("Distancia (CPA):"), distancia);
        form->addRow(tr("Horizonte (TCPA):"), horizonte);
        auto *botones = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(botones, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(botones, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(botones);
        if (dlg.exec() != QDialog::Accepted) {
            m_accProximidad->setChecked(m_mapa->proximityAlertDistance() > 0);
            return;
        }
        m_cpaMillas = distancia->value();
        m_cpaMinutos = horizonte->value();
        m_mapa->setProximityAlert(m_cpaMillas * 1852.0, m_cpaMinutos * 60.0);
        actualizarIndicadorAlertas();
        statusBar()->showMessage(
            tr("Alertas de proximidad: menos de %1 NM en %2 min — %3 pares en alerta")
                .arg(m_cpaMillas, 0, 'f', 2).arg(m_cpaMinutos)
                .arg(m_mapa->proximityAlerts().size()), 6000);
    }

    // Texto de una alerta de proximidad: los dos objetivos (primera línea de su
    // etiqueta), a qué distancia pasarán y en cuánto tiempo.
    QString textoProximidad(qint64 a, qint64 b, double cpa, double tcpa) const
    {
        const auto nombre = [this](qint64 id) {
            const auto t = m_mapa->target(id);
            return t && !t->label.isEmpty() ? t->label.section(QLatin1Char('\n'), 0, 0)
                                            : tr("Objetivo %1").arg(id);
        };
        if (tcpa < 0)
            return tr("⚠ Proximidad: %1 y %2 ya están a menos de %3 NM")
                .arg(nombre(a), nombre(b)).arg(m_cpaMillas, 0, 'f', 2);
        return tr("⚠ CPA: %1 y %2 pasarán a %3 NM dentro de %4 min")
            .arg(nombre(a), nombre(b)).arg(cpa / 1852.0, 0, 'f', 2)
            .arg(qRound(tcpa / 60.0));
    }

    // Pide separación, número de anillos y radiales, y los pone alrededor del
    // objetivo seleccionado (lo siguen al moverse) o, si no hay, del punto que
    // se pinche a continuación.
    void ponerAnillos()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Anillos de distancia"));
        auto *form = new QFormLayout(&dlg);
        auto *sep = new QDoubleSpinBox(&dlg);
        sep->setRange(0.05, 100.0);
        sep->setDecimals(2);
        sep->setSuffix(tr(" NM"));
        sep->setValue(m_anillos.spacingMeters / 1852.0);
        auto *num = new QSpinBox(&dlg);
        num->setRange(1, 20);
        num->setValue(m_anillos.count);
        auto *radiales = new QCheckBox(tr("Radiales cada 30°"), &dlg);
        radiales->setChecked(m_anillos.spokeEveryDeg > 0);
        const qint64 sel = m_mapa->selectedTarget();
        auto *donde = new QLabel(sel >= 0 ? tr("Centro: el objetivo seleccionado (lo siguen).")
                                          : tr("Centro: pincha en el mapa tras aceptar."), &dlg);
        donde->setWordWrap(true);
        form->addRow(donde);
        form->addRow(tr("Separación:"), sep);
        form->addRow(tr("Anillos:"), num);
        form->addRow(radiales);
        auto *botones = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(botones, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(botones, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(botones);
        if (dlg.exec() != QDialog::Accepted)
            return;
        m_anillos.spacingMeters = sep->value() * 1852.0;
        m_anillos.count = num->value();
        m_anillos.spokeEveryDeg = radiales->isChecked() ? 30 : 0;
        if (sel >= 0) {
            RangeRings r = m_anillos;
            r.targetId = sel;
            m_mapa->addRangeRings(r);
            statusBar()->showMessage(tr("Anillos alrededor del objetivo %1").arg(sel), 4000);
            return;
        }
        iniciarPickMedida(Pick::Anillos, tr("Pincha el centro de los anillos…"));
    }

    // Marcación: desde el objetivo seleccionado (o un punto que se pinche) hasta
    // otro objetivo o punto que se pinche. Si el destino cae sobre un objetivo,
    // la línea cuelga de él y se actualiza al moverse.
    void ponerMarcacion()
    {
        m_ebl = BearingLine();
        const qint64 sel = m_mapa->selectedTarget();
        if (sel >= 0) {
            m_ebl.fromTarget = sel;
            iniciarPickMedida(Pick::DestinoEbl,
                tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
        } else {
            iniciarPickMedida(Pick::OrigenEbl,
                tr("Pincha el origen de la marcación (un objetivo o un punto)…"));
        }
    }

    // Arma la herramienta de elegir punto para una medida, con su aviso.
    void iniciarPickMedida(Pick cual, const QString &aviso)
    {
        m_picking = cual;
        m_mapa->setActiveTool(MapTool::PickPoint);
        statusBar()->showMessage(aviso, 10000);
    }

    // Objetivo a menos de ~14 px del punto (con la resolución actual), o -1.
    qint64 objetivoCerca(const QGeoCoordinate &p) const
    {
        const double tolerancia = 14.0 * std::max(1.0, m_mapa->metersPerPixel());
        qint64 mejor = -1;
        double dMejor = tolerancia;
        for (const MapTarget &t : m_mapa->targets()) {
            const double d = t.position.distanceTo(p);
            if (d <= dMejor) {
                dMejor = d;
                mejor = t.id;
            }
        }
        return mejor;
    }

    // Punto pinchado para anillos o marcación.
    void puntoDeMedida(const QGeoCoordinate &p)
    {
        const Pick cual = m_picking;
        m_picking = Pick::Ninguno;
        m_mapa->setActiveTool(MapTool::None);
        if (m_accNavegar)
            m_accNavegar->setChecked(true);
        const qint64 obj = objetivoCerca(p);
        if (cual == Pick::Anillos) {
            RangeRings r = m_anillos;
            r.targetId = obj;
            r.center = p;
            m_mapa->addRangeRings(r);
            statusBar()->showMessage(obj >= 0 ? tr("Anillos alrededor del objetivo %1").arg(obj)
                                              : tr("Anillos puestos"), 4000);
        } else if (cual == Pick::OrigenEbl) {
            m_ebl.fromTarget = obj;
            m_ebl.from = p;
            iniciarPickMedida(Pick::DestinoEbl,
                tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
        } else {
            m_ebl.toTarget = obj;
            m_ebl.to = p;
            const qint64 id = m_mapa->addBearingLine(m_ebl);
            double rumbo = 0.0, dist = 0.0;
            if (m_mapa->bearingLineMeasure(id, &rumbo, &dist))
                statusBar()->showMessage(tr("Marcación %1° · %2 NM")
                    .arg(qRound(rumbo), 3, 10, QLatin1Char('0'))
                    .arg(dist / 1852.0, 0, 'f', 2), 6000);
        }
    }

    // Vigila (o deja de vigilar) el polígono seleccionado como zona de alerta.
    // Al empezar, la librería ya avisa de los objetivos que estén dentro.
    void alternarVigilancia()
    {
        const qint64 id = m_mapa->selectedFeature();
        const auto f = id >= 0 ? m_mapa->feature(id) : std::nullopt;
        if (!f || f->kind != GeometryKind::Polygon) {
            statusBar()->showMessage(tr("Selecciona primero un polígono para vigilarlo"), 4000);
            return;
        }
        const QString nombre = f->name.isEmpty() ? tr("zona %1").arg(id) : f->name;
        if (m_mapa->isZoneWatched(id)) {
            m_mapa->unwatchZone(id);
            m_noVigilar.insert(id);           // no volver a vigilarla sola
            actualizarIndicadorAlertas();
            statusBar()->showMessage(tr("Se deja de vigilar «%1»").arg(nombre), 4000);
            return;
        }
        m_noVigilar.remove(id);
        m_mapa->watchZone(id);
        actualizarIndicadorAlertas();
        statusBar()->showMessage(
            tr("Vigilando «%1»: %2 objetivos dentro")
                .arg(nombre).arg(m_mapa->targetsInZone(id).size()), 5000);
    }

    // Texto de una alerta de zona para la barra de estado: qué objetivo (primera
    // línea de su etiqueta), qué zona y cuántos quedan dentro.
    // \a esEntidad: el que cruza es una entidad punto (un «Objetivo» colocado)
    // y no un objetivo móvil. El recuento suma ambos.
    QString textoAlertaZona(qint64 quienId, bool esEntidad, qint64 zona, bool entra) const
    {
        QString quien;
        if (esEntidad) {
            const auto e = m_mapa->feature(quienId);
            quien = e && !e->name.isEmpty() ? e->name.section(QLatin1Char('\n'), 0, 0)
                                            : tr("Entidad %1").arg(quienId);
        } else {
            const auto t = m_mapa->target(quienId);
            quien = t && !t->label.isEmpty() ? t->label.section(QLatin1Char('\n'), 0, 0)
                                             : tr("Objetivo %1").arg(quienId);
        }
        const auto f = m_mapa->feature(zona);
        const QString donde = f && !f->name.isEmpty()
            ? f->name.section(QLatin1Char('\n'), 0, 0) : tr("zona %1").arg(zona);
        const int dentro = int(m_mapa->targetsInZone(zona).size()
                               + m_mapa->featuresInZone(zona).size());
        return (entra ? tr("⚠ %1 ENTRA en «%2» (%3 dentro)")
                      : tr("%1 sale de «%2» (%3 dentro)"))
            .arg(quien, donde).arg(dentro);
    }

    // Indicador fijo de la barra de estado: cuántos objetivos (móviles o
    // colocados) hay ahora dentro de alguna zona vigilada. Rojo si hay alguno.
    void actualizarIndicadorAlertas()
    {
        if (!m_alertas)
            return;
        QSet<qint64> moviles, puntos;
        for (qint64 z : m_mapa->watchedZones()) {
            for (qint64 t : m_mapa->targetsInZone(z))
                moviles.insert(t);
            for (qint64 p : m_mapa->featuresInZone(z))
                puntos.insert(p);
        }
        const int n = int(moviles.size() + puntos.size());
        const int zonas = int(m_mapa->watchedZones().size());
        const int cpa = int(m_mapa->proximityAlerts().size());
        QStringList partes;
        if (zonas > 0)
            partes << (n == 0 ? tr("Zonas vigiladas: %1").arg(zonas) : tr("⚠ %1 en zonas").arg(n));
        if (m_mapa->proximityAlertDistance() > 0)
            partes << (cpa == 0 ? tr("CPA vigilado") : tr("⚠ %1 CPA").arg(cpa));
        m_alertas->setText(partes.join(QStringLiteral(" · ")));
        m_alertas->setStyleSheet(n + cpa > 0 ? QStringLiteral("QLabel { color:#fff; background:#d50032;"
                                                              " font-weight:bold; padding:0 6px; }")
                                             : QString());
    }

    // Vigila automáticamente toda «Área objetivo» (al crearla, cargarla o
    // deshacer), salvo las que el usuario desactivó a mano.
    void vigilarAreasObjetivo()
    {
        bool alguna = false;
        for (const MapFeature &f : m_mapa->featuresOfType(QStringLiteral("objetivo_area"))) {
            if (f.kind != GeometryKind::Polygon || m_noVigilar.contains(f.id)
                || m_mapa->isZoneWatched(f.id))
                continue;
            alguna = m_mapa->watchZone(f.id) || alguna;
        }
        if (alguna)
            actualizarIndicadorAlertas();
    }

    void alternarSimulacion(bool on)
    {
        if (on) {
            salirReproduccion();          // en vivo y reproducción no se mezclan
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
        m_simHora = QDateTime::currentDateTimeUtc();
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

            const qint64 id = m_mapa->addTarget(t, m_simHora);
            m_simIds.append(id);

            const double rad = qDegreesToRadians(rumbo);
            // Componentes: x = variacion de longitud, y = variacion de latitud.
            m_simVel.append(QPointF(std::sin(rad) * velGrados,
                                    std::cos(rad) * velGrados));
        }
        statusBar()->showMessage(tr("Simulando %1 objetivos").arg(n), 3000);
    }

    // Un paso de la simulación. Es un «time-lapse»: cada paso (100 ms reales)
    // son kSegundosPorPaso de tiempo simulado, y esa hora se pasa a la
    // librería. Así la velocidad que estima por la traza (para el CPA/TCPA) es
    // la que se ve: 0.002–0.006° por paso son 7–22 m/s, y no kilómetros por
    // segundo de reloj de pared.
    void pasoSimulacion()
    {
        auto *r = QRandomGenerator::global();
        m_simHora = m_simHora.addSecs(kSegundosPorPaso);
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
            m_mapa->updateTarget(m_simIds[i], QGeoCoordinate(lat, lon), rumbo, m_simHora);
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
    QAction *m_accMedir = nullptr;
    QAction *m_accZoomArea = nullptr;
    QAction *m_accPunto = nullptr;
    QAction *m_accLinea = nullptr;
    QAction *m_accPoligono = nullptr;
    QAction *m_accObjetivo = nullptr;
    QAction *m_accArea = nullptr;
    QAction *m_accEditar = nullptr;
    QAction *m_accGuardar = nullptr;
    QAction *m_accGuardarComo = nullptr;
    QAction *m_accAbrir = nullptr;
    QAction *m_accRejilla = nullptr;
    QAction *m_accCobertura = nullptr;
    QAction *m_accEscala = nullptr;        //!< escala gráfica
    QAction *m_accEscalaNm = nullptr;      //!< escala en millas náuticas
    QAction *m_accCuadricula = nullptr;    //!< cuadrícula de coordenadas
    CoordinateFormat m_formatoCoord = CoordinateFormat::Decimal;   //!< coordenada bajo el cursor
    QMenu *m_menuVer = nullptr;
    QAction *m_accTemaClaro = nullptr;
    QAction *m_accTemaOscuro = nullptr;
    bool m_temaOscuro = false;

    QDockWidget *m_dock = nullptr;   //!< panel lateral «Capas y entidades»
    QWidget *m_ficha = nullptr;      //!< ficha contextual (oculta sin selección)
    QLabel *m_lblFicha = nullptr;    //!< título de la ficha (nombre · tipo)
    QLabel *m_lblCapaEntidad = nullptr; //!< capa de la entidad (solo lectura)
    QTreeWidget *m_arbol = nullptr;
    QLineEdit *m_campoNombre = nullptr;
    QLineEdit *m_campoTipo = nullptr;
    QLineEdit *m_campoDescripcion = nullptr;
    QTableWidget *m_tablaAtributos = nullptr;
    QTableWidget *m_tablaCoords = nullptr;   //!< lat/lon editables de la entidad
    QPushButton *m_btnAddVert = nullptr;
    QPushButton *m_btnDelVert = nullptr;
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
    bool m_sucio = false;        //!< hay entidades sin guardar (guardado explícito)
    QString m_ultimoDir;

    // Alta de entidad en curso (diálogo → dibuja → Navegar): nombre a aplicar a
    // la siguiente entidad dibujada y si hay un alta armada.
    QString m_nombrePendiente;
    bool m_esperandoDibujo = false;

    // Simulacion de objetivos moviles.
    QSpinBox *m_numObjetivos = nullptr;
    QComboBox *m_traza = nullptr;
    QAction *m_accSimular = nullptr;
    QAction *m_accVerObjetivos = nullptr;   //!< mostrar/ocultar objetivos
    QAction *m_accVigilarZona = nullptr;    //!< alertas de zona del polígono seleccionado
    QAction *m_accSelArea = nullptr;        //!< seleccionar área (bbox)
    QAction *m_accSelPoli = nullptr;        //!< seleccionar polígono (zona)
    int m_zonasCreadas = 0;                 //!< para nombrar «Zona N» desde una selección
    QAction *m_accProximidad = nullptr;     //!< alertas CPA/TCPA entre objetivos
    double m_cpaMillas = 1.0;               //!< distancia de la alerta CPA (NM)
    int m_cpaMinutos = 10;                  //!< horizonte de la alerta CPA (min)
    RangeRings m_anillos;                   //!< últimos ajustes de anillos (1 NM x 5)
    BearingLine m_ebl;                      //!< marcación en curso (origen ya fijado)
    TrackPlayer *m_player = nullptr;        //!< reproducción de historiales (al abrir uno)
    QToolBar *m_barraRep = nullptr;         //!< barra de reproducción (abajo)
    QSlider *m_repSlider = nullptr;         //!< posición en el tiempo
    QLabel *m_repHora = nullptr;            //!< hora de la reproducción
    QCheckBox *m_repRutas = nullptr;        //!< mostrar rutas completas
    QAction *m_accSalirRep = nullptr;       //!< salir del modo reproducción (Esc)
    QPushButton *m_modoRep = nullptr;       //!< indicador "▶ Reproducción" (clic = salir)
    QTimer *m_simReloj = nullptr;
    QVector<qint64> m_simIds;
    QVector<QPointF> m_simVel;      //!< x = dLon, y = dLat por paso.
    QDateTime m_simHora;            //!< Reloj simulado (avanza kSegundosPorPaso por paso).
    static constexpr int kSegundosPorPaso = 30;
    // Juego de iconos de la app para la simbologia de objetivos (Fase 2).
    QPixmap m_icoBuque, m_icoAereo, m_icoUav, m_icoUavBajo;
    QPixmap m_icoBuqueZona, m_icoAereoZona, m_icoUavZona;   //!< dentro de zona vigilada
    QLabel *m_alertas = nullptr;            //!< indicador fijo «⚠ N en zonas»
    QSet<qint64> m_noVigilar;               //!< áreas objetivo que el usuario desactivó
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
    // Para QSettings (recordar el tema elegido entre sesiones).
    QCoreApplication::setOrganizationName(QStringLiteral("LibMapaStatic"));
    QCoreApplication::setApplicationName(QStringLiteral("demo"));

    // Primer positional: la carpeta de un paquete de datos (o su mapa.json), o un
    // datasets.json. Opcionales: --dem <carpeta> / --dem-db <fichero>
    // (elevacion) y --features <fichero> (entidades: se CARGA al arrancar y es el
    // fichero de trabajo para «Guardar»; el guardado es EXPLÍCITO, no automático).
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
