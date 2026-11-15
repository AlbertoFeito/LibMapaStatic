// cambio_pantalla - banco de pruebas del cambio de pantalla de objetivos y de lo que lo acompana.
//
// Sirve para ver y tocar, con el raton, lo que se anadio para una app que muestra varias
// «informaciones» de objetivos, una cada vez, sobre la misma capa. Un panel a la derecha:
//   - PANTALLAS: dos informaciones simuladas (A, ids 100..; B, ids 200..) que se mueven las
//     DOS en un almacen propio de la app, se vea la que se vea. Al cambiar se hace
//     beginTargetLoad() -> clearTargets(conservar) -> addTarget(objetivo, historia) ->
//     endTargetLoad(). Tres casillas quitan cada pieza para ver que arregla: la carga
//     silenciosa (sin ella, las alarmas de zona se repiten al volver), conservar los sectores
//     (sin ello, el sector del objetivo 100 desaparece para siempre) y devolver la historia
//     (sin ella, la trayectoria empieza de cero).
//   - VARIAS ENTIDADES bajo el clic: dos zonas solapadas y una linea que las cruza. El clic
//     derecho abre UN menu con un submenu por entidad (MapHit::features).
//   - SELECCIONAR SIN RESALTAR: una red de lineas con MapFeature::highlightWhenSelected.
//   - CAJA DE LOS ROTULOS: fondo, borde y halo de los rotulos de entidades (FeatureStyle) y,
//     si se pide, de los anillos y la cuadricula (MapWidget::setLabelBoxStyle).
//
//   cambio_pantalla [paquete de datos]   (por defecto: <exe>/datos)
//   cambio_pantalla <paquete> --comprobar
//        -> sin ventana: hace el cambio de pantalla de ida y vuelta y comprueba lo anterior.
//   cambio_pantalla <paquete> --captura=imagen.png
//        -> abre la ventana, guarda una imagen y sale.

#include <libmapa/MapFeature.h>
#include <libmapa/MapTarget.h>
#include <libmapa/MapTypes.h>
#include <libmapa/MapWidget.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>

using namespace libmapa;

namespace {

const QGeoCoordinate kCentro(23.05, -82.35);   // La Habana y alrededores
const int kPorPantalla = 10;                   // objetivos de cada informacion
const qint64 kIdA = 100;                       // primer id de la pantalla A
const qint64 kIdB = 200;                       // primer id de la pantalla B

// Un objetivo tal como lo guarda la APP: lo que se pinta, su trayectoria y como se mueve.
struct Guardado {
    MapTarget objetivo;
    QVector<TargetTrackPoint> historia;
    double velocidad = 10.0;       // m/s
};

// Colores que ofrecen los combos del panel de rotulos.
struct ColorNombrado {
    const char *nombre;
    QColor color;
};
const ColorNombrado kColores[] = {
    {"Blanco", QColor(255, 255, 255)}, {"Negro", QColor(0, 0, 0)},
    {"Amarillo", QColor(255, 235, 59)}, {"Azul", QColor(13, 71, 161)},
    {"Rojo", QColor(198, 40, 40)},
};

} // namespace

// Ventana con el mapa y el panel de pruebas. Todo esta en el constructor y en unas pocas
// funciones pequenas para leerlo de arriba abajo.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos, los datos de ejemplo y el panel.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        if (!m_mapa->isReady()) {
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
            return;
        }
        resize(1280, 800);
        m_mapa->setCenter(kCentro);
        m_mapa->setZoom(10);
        m_reloj = QDateTime::currentDateTimeUtc();

        creaZona();
        creaSolapadas();
        creaRed();
        creaFuentes();
        creaPantallas();

        auto *cuerpo = new QWidget(this);
        auto *caja = new QHBoxLayout(cuerpo);
        caja->setContentsMargins(0, 0, 0, 0);
        caja->addWidget(m_mapa, 1);
        caja->addWidget(creaPanel());
        setCentralWidget(cuerpo);

        conectaSenales();
        // La primera carga NO es silenciosa: asi se ve la alarma de los que nacen dentro
        // de la zona, y despues se compara con los cambios de pantalla.
        m_silenciosa->setChecked(false);
        muestra('A');
        m_silenciosa->setChecked(true);
        creaColgados();
        aplicaRotulos();

        // El tiempo simulado avanza 30 s por segundo real: las trayectorias crecen a la vista.
        connect(&m_tic, &QTimer::timeout, this, [this] { avanza(30); });
        m_tic.start(1000);
    }

    MapWidget *mapa() const { return m_mapa; }
    int alarmas() const { return m_alarmas; }
    qint64 sector() const { return m_sector; }
    qint64 anillos() const { return m_anillos; }
    qint64 zonaGrande() const { return m_zonaGrande; }
    qint64 zonaChica() const { return m_zonaChica; }
    qint64 linea() const { return m_linea; }
    QGeoCoordinate cruce() const { return m_cruce; }
    QVector<qint64> red() const { return m_red; }
    QVector<qint64> fuentes() const { return m_fuentes; }
    const QVector<Guardado> &guardados(char pantalla) const { return pantalla == 'A' ? m_a : m_b; }
    void paraElReloj() { m_tic.stop(); }

    // Opciones del cambio de pantalla (las casillas del panel), para --comprobar.
    void ponOpciones(bool silenciosa, bool conservar, bool historia)
    {
        m_silenciosa->setChecked(silenciosa);
        m_conservar->setChecked(conservar);
        m_historia->setChecked(historia);
    }

    // Mueve TODOS los objetivos de las dos pantallas \a segundos de tiempo simulado y anota
    // el punto en la historia que guarda la app. Solo los de la pantalla visible van al mapa.
    void avanza(int segundos)
    {
        m_reloj = m_reloj.addSecs(segundos);
        for (QVector<Guardado> *pantalla : {&m_a, &m_b}) {
            const bool visible = (pantalla == &m_a) == (m_visible == 'A');
            for (Guardado &g : *pantalla) {
                MapTarget &t = g.objetivo;
                // Vuelta suave alrededor del centro: no se van del mapa.
                t.headingDeg = std::fmod(t.headingDeg + 4.0, 360.0);
                t.position = t.position.atDistanceAndAzimuth(g.velocidad * segundos, t.headingDeg);
                g.historia.append({t.position, m_reloj});
                if (visible)
                    m_mapa->updateTarget(t.id, t.position, t.headingDeg, m_reloj);
            }
        }
    }

    // Cambia de pantalla: guarda nada (la app ya lo tiene todo en su almacen), limpia y carga
    // la otra informacion, segun las casillas del panel.
    void muestra(char pantalla)
    {
        const bool silenciosa = m_silenciosa->isChecked();
        const bool conservar = m_conservar->isChecked();
        const bool conHistoria = m_historia->isChecked();
        const int antes = m_alarmas;

        if (silenciosa)
            m_mapa->beginTargetLoad();
        m_mapa->clearTargets(conservar);
        m_visible = pantalla;
        for (const Guardado &g : guardados(pantalla)) {
            if (conHistoria)
                m_mapa->addTarget(g.objetivo, g.historia);      // con su trayectoria y sus horas
            else
                m_mapa->addTarget(g.objetivo, m_reloj);         // como siempre: empieza de cero
        }
        if (silenciosa)
            m_mapa->endTargetLoad();

        m_titulo->setText(tr("Pantalla visible: <b>%1</b>").arg(QChar::fromLatin1(pantalla)));
        anota(tr("Pantalla %1: %2 objetivos, %3 alarmas nuevas%4")
                  .arg(QChar::fromLatin1(pantalla)).arg(m_mapa->targetCount())
                  .arg(m_alarmas - antes)
                  .arg(m_mapa->feature(m_sector).has_value() || m_sector < 0
                           ? QString() : tr(" — el sector se ha perdido")));
    }

    // Aplica a los rotulos de las fuentes el fondo, el borde y el halo del panel; y, si se
    // pide, tambien a los rotulos que no son de una entidad (anillos y cuadricula).
    void aplicaRotulos()
    {
        QColor fondo = kColores[m_fondoColor->currentIndex()].color;
        fondo.setAlpha(m_fondoAlfa->value());
        const QColor borde = m_borde->isChecked() ? kColores[m_bordeColor->currentIndex()].color
                                                  : QColor();
        QColor halo;
        if (m_halo->isChecked()) {
            halo = kColores[m_haloColor->currentIndex()].color;
            halo.setAlpha(m_haloAlfa->value());
        }
        for (const qint64 id : m_fuentes) {
            auto f = m_mapa->feature(id);
            if (!f)
                continue;
            f->style.labelBackground = fondo;
            f->style.labelBorderColor = borde;
            f->style.labelBorderWidth = m_bordeGrosor->value();
            f->style.labelHaloColor = halo;
            f->style.labelHaloWidth = m_haloGrosor->value();
            m_mapa->updateFeature(*f);
        }
        LabelBoxStyle global;                   // por defecto: cada capa, su caja de siempre
        if (m_global->isChecked()) {
            global.background = fondo;
            global.borderColor = borde;
            global.borderWidth = m_bordeGrosor->value();
            global.haloColor = halo;
            global.haloWidth = m_haloGrosor->value();
        }
        m_mapa->setLabelBoxStyle(global);
    }

    // Pone o quita el resaltado al seleccionar en todas las lineas de la red.
    void resaltaRed(bool on)
    {
        for (const qint64 id : m_red) {
            auto f = m_mapa->feature(id);
            if (!f)
                continue;
            f->highlightWhenSelected = on;
            m_mapa->updateFeature(*f);
        }
    }

private:
    // Zona vigilada alrededor del centro: los objetivos que caen dentro dan la alarma.
    void creaZona()
    {
        MapFeature z;
        z.kind = GeometryKind::Polygon;
        z.type = QStringLiteral("zona_vigilada");
        z.name = tr("Zona vigilada");
        z.style.lineColor = QColor(198, 40, 40);
        z.style.fillColor = QColor(198, 40, 40, 35);
        z.geometry = { QGeoCoordinate(22.93, -82.50), QGeoCoordinate(23.17, -82.50),
                       QGeoCoordinate(23.17, -82.20), QGeoCoordinate(22.93, -82.20) };
        m_zona = m_mapa->addFeature(z);
        m_mapa->watchZone(m_zona);
    }

    // Dos zonas solapadas y, en una capa mas alta, una linea que las cruza: el punto de
    // cruce tiene tres entidades debajo.
    void creaSolapadas()
    {
        m_cruce = QGeoCoordinate(kCentro.latitude(), kCentro.longitude() + 0.38);
        const auto cuadro = [this](double medio, const QString &nombre, const QColor &c) {
            MapFeature z;
            z.kind = GeometryKind::Polygon;
            z.type = QStringLiteral("zona");
            z.name = nombre;
            z.style.lineColor = c;
            z.style.fillColor = QColor(c.red(), c.green(), c.blue(), 40);
            z.style.labelAnchorVertex = 0;      // el rotulo, en una esquina: no tapa el cruce
            const double lat = m_cruce.latitude(), lon = m_cruce.longitude();
            z.geometry = { QGeoCoordinate(lat - medio, lon - medio),
                           QGeoCoordinate(lat + medio, lon - medio),
                           QGeoCoordinate(lat + medio, lon + medio),
                           QGeoCoordinate(lat - medio, lon + medio) };
            return m_mapa->addFeature(z);
        };
        m_zonaGrande = cuadro(0.11, tr("Zona grande"), QColor(21, 101, 192));
        m_zonaChica = cuadro(0.05, tr("Zona chica"), QColor(46, 125, 50));

        m_mapa->addFeatureLayer(QStringLiteral("lineas"), tr("Lineas"), 10);
        MapFeature l;
        l.kind = GeometryKind::Polyline;
        l.layerId = QStringLiteral("lineas");
        l.type = QStringLiteral("linea");
        l.name = tr("Linea");
        l.style.lineColor = QColor(230, 81, 0);
        l.style.lineWidth = 3.0;
        l.style.labelAnchorVertex = 0;
        l.geometry = { QGeoCoordinate(m_cruce.latitude(), m_cruce.longitude() - 0.16),
                       QGeoCoordinate(m_cruce.latitude(), m_cruce.longitude() + 0.16) };
        m_linea = m_mapa->addFeature(l);
    }

    // Una red de lineas dibujada como entidades, al oeste: no debe «encenderse» al tocarla.
    void creaRed()
    {
        const double lat0 = kCentro.latitude() - 0.15, lon0 = kCentro.longitude() - 0.75;
        const auto linea = [this](const QGeoCoordinate &a, const QGeoCoordinate &b) {
            MapFeature f;
            f.kind = GeometryKind::Polyline;
            f.type = QStringLiteral("red");
            f.style.lineColor = QColor(55, 71, 79);
            f.style.lineWidth = 1.5;
            f.geometry = { a, b };
            f.highlightWhenSelected = false;
            m_red.append(m_mapa->addFeature(f));
        };
        for (int i = 0; i < 4; ++i) {
            linea(QGeoCoordinate(lat0 + 0.1 * i, lon0), QGeoCoordinate(lat0 + 0.1 * i, lon0 + 0.3));
            linea(QGeoCoordinate(lat0, lon0 + 0.1 * i), QGeoCoordinate(lat0 + 0.3, lon0 + 0.1 * i));
        }
    }

    // Tres «fuentes» con su numero debajo: los rotulos sobre los que actua el panel.
    void creaFuentes()
    {
        for (int i = 0; i < 3; ++i) {
            MapFeature p;
            p.kind = GeometryKind::Point;
            p.type = QStringLiteral("fuente");
            p.name = tr("FUENTE %1").arg(i + 1);
            p.style.lineColor = QColor(106, 27, 154);
            p.style.pointRadiusPx = 7.0;
            p.style.labelAnchor = LabelAnchor::Bottom;
            p.geometry = { QGeoCoordinate(kCentro.latitude() + 0.33,
                                          kCentro.longitude() - 0.25 + 0.25 * i) };
            m_fuentes.append(m_mapa->addFeature(p));
        }
    }

    // Las dos informaciones que guarda la app: A en un corro cerca del centro (varios dentro
    // de la zona vigilada), B mas al sur.
    void creaPantallas()
    {
        for (int i = 0; i < kPorPantalla; ++i) {
            Guardado a;
            a.objetivo.id = kIdA + i;
            a.objetivo.kind = QStringLiteral("A");
            a.objetivo.label = QStringLiteral("A-%1").arg(i);
            a.objetivo.color = QColor(0xff, 0x8f, 0x00);
            a.objetivo.headingDeg = 36.0 * i;
            a.objetivo.position = kCentro.atDistanceAndAzimuth(4000.0 + 2500.0 * i, 36.0 * i);
            a.velocidad = 8.0 + i;
            a.historia.append({a.objetivo.position, m_reloj});
            m_a.append(a);

            Guardado b;
            b.objetivo.id = kIdB + i;
            b.objetivo.kind = QStringLiteral("B");
            b.objetivo.label = QStringLiteral("B-%1").arg(i);
            b.objetivo.color = QColor(0x00, 0x83, 0x8f);
            b.objetivo.headingDeg = 180.0 + 30.0 * i;
            b.objetivo.position = QGeoCoordinate(kCentro.latitude() - 0.05 - 0.02 * i,
                                                 kCentro.longitude() - 0.18 + 0.04 * i);
            b.velocidad = 14.0 - i;
            b.historia.append({b.objetivo.position, m_reloj});
            m_b.append(b);
        }
    }

    // Lo que cuelga del primer objetivo de A: un sector (entidad con dueno) y unos anillos.
    void creaColgados()
    {
        MapFeature s;
        s.kind = GeometryKind::Arc;
        s.type = QStringLiteral("sector");
        s.name = tr("Sector de A-0");
        s.geometry = { QGeoCoordinate(0.0, 0.0) };      // el centro lo pone el dueno
        s.radiusMeters = 9000.0;
        s.arcStartDeg = 20.0;
        s.arcEndDeg = 110.0;
        s.arcSector = true;
        s.originTargetId = kIdA;
        s.style.lineColor = QColor(123, 31, 162);
        s.style.fillColor = QColor(123, 31, 162, 50);
        m_sector = m_mapa->addFeature(s);

        RangeRings r;
        r.targetId = kIdA;
        r.spacingMeters = 3000.0;
        r.count = 3;
        m_anillos = m_mapa->addRangeRings(r);
    }

    // Panel de la derecha: un grupo por cada cosa que se prueba y el registro de eventos.
    QWidget *creaPanel()
    {
        auto *panel = new QWidget;
        auto *col = new QVBoxLayout(panel);

        // --- 1. Pantallas ----------------------------------------------------------
        auto *g1 = new QGroupBox(tr("1. Pantallas de objetivos"));
        auto *l1 = new QVBoxLayout(g1);
        m_titulo = new QLabel;
        l1->addWidget(m_titulo);
        auto *botones = new QHBoxLayout;
        auto *a = new QPushButton(tr("Pantalla A"));
        auto *b = new QPushButton(tr("Pantalla B"));
        botones->addWidget(a);
        botones->addWidget(b);
        l1->addLayout(botones);
        connect(a, &QPushButton::clicked, this, [this] { muestra('A'); });
        connect(b, &QPushButton::clicked, this, [this] { muestra('B'); });
        m_silenciosa = new QCheckBox(tr("Carga silenciosa (sin alarmas al cargar)"));
        m_conservar = new QCheckBox(tr("Conservar sectores y círculos con dueño"));
        m_historia = new QCheckBox(tr("Devolver la historia (con hora)"));
        for (QCheckBox *c : {m_silenciosa, m_conservar, m_historia}) {
            c->setChecked(true);
            l1->addWidget(c);
        }
        m_contador = new QLabel(tr("Alarmas de zona: 0"));
        l1->addWidget(m_contador);
        auto *resaltar = new QPushButton(tr("Resaltar un objetivo de esta pantalla"));
        connect(resaltar, &QPushButton::clicked, this, [this] {
            m_mapa->setSelectedTarget(guardados(m_visible).first().objetivo.id);
        });
        l1->addWidget(resaltar);
        col->addWidget(g1);

        // --- 2 y 3. Clic y resaltado ----------------------------------------------
        auto *g2 = new QGroupBox(tr("2 y 3. Clic sobre entidades"));
        auto *l2 = new QVBoxLayout(g2);
        auto *ayuda = new QLabel(tr("Clic derecho donde la línea naranja cruza las dos zonas "
                                    "(al este): un menú con las tres. La red está al oeste."));
        ayuda->setWordWrap(true);
        l2->addWidget(ayuda);
        auto *red = new QCheckBox(tr("Resaltar la red al seleccionarla"));
        connect(red, &QCheckBox::toggled, this, [this](bool on) { resaltaRed(on); });
        l2->addWidget(red);
        col->addWidget(g2);

        // --- 4. Rotulos ------------------------------------------------------------
        auto *g4 = new QGroupBox(tr("4. Caja de los rótulos (las FUENTES, al norte)"));
        auto *f4 = new QFormLayout(g4);
        const auto combo = [](int inicial) {
            auto *c = new QComboBox;
            for (const ColorNombrado &k : kColores)
                c->addItem(QString::fromUtf8(k.nombre));
            c->setCurrentIndex(inicial);
            return c;
        };
        const auto numero = [](int min, int max, int valor) {
            auto *s = new QSpinBox;
            s->setRange(min, max);
            s->setValue(valor);
            return s;
        };
        m_fondoColor = combo(0);
        m_fondoAlfa = numero(0, 255, 190);
        f4->addRow(tr("Fondo"), m_fondoColor);
        f4->addRow(tr("Opacidad del fondo (0 = sin caja)"), m_fondoAlfa);
        m_borde = new QCheckBox(tr("Borde"));
        m_bordeColor = combo(1);
        m_bordeGrosor = numero(1, 6, 1);
        f4->addRow(m_borde, m_bordeColor);
        f4->addRow(tr("Grosor del borde (px)"), m_bordeGrosor);
        m_halo = new QCheckBox(tr("Halo"));
        m_haloColor = combo(0);
        m_haloAlfa = numero(0, 255, 220);
        m_haloGrosor = numero(1, 6, 2);
        f4->addRow(m_halo, m_haloColor);
        f4->addRow(tr("Opacidad del halo"), m_haloAlfa);
        f4->addRow(tr("Grosor del halo (px)"), m_haloGrosor);
        m_global = new QCheckBox(tr("También anillos y cuadrícula"));
        f4->addRow(m_global);
        auto *cuadricula = new QCheckBox(tr("Mostrar la cuadrícula"));
        connect(cuadricula, &QCheckBox::toggled, m_mapa, &MapWidget::setGraticuleVisible);
        f4->addRow(cuadricula);
        for (QComboBox *c : {m_fondoColor, m_bordeColor, m_haloColor})
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this] { aplicaRotulos(); });
        for (QSpinBox *s : {m_fondoAlfa, m_bordeGrosor, m_haloAlfa, m_haloGrosor})
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this] { aplicaRotulos(); });
        for (QCheckBox *c : {m_borde, m_halo, m_global})
            connect(c, &QCheckBox::toggled, this, [this] { aplicaRotulos(); });
        col->addWidget(g4);

        // --- Registro --------------------------------------------------------------
        m_registro = new QPlainTextEdit;
        m_registro->setReadOnly(true);
        m_registro->setMaximumBlockCount(200);
        col->addWidget(new QLabel(tr("Registro de eventos")));
        col->addWidget(m_registro, 1);

        auto *zona = new QScrollArea;
        zona->setWidget(panel);
        zona->setWidgetResizable(true);
        zona->setFixedWidth(360);
        return zona;
    }

    // Escucha lo que se quiere ver en el registro: alarmas, seleccion, clics y el menu.
    void conectaSenales()
    {
        connect(m_mapa, &MapWidget::targetEnteredZone, this, [this](qint64 objetivo, qint64) {
            ++m_alarmas;
            m_contador->setText(tr("Alarmas de zona: %1").arg(m_alarmas));
            anota(tr("ALARMA: el objetivo %1 entra en la zona").arg(objetivo));
        });
        connect(m_mapa, &MapWidget::targetLeftZone, this, [this](qint64 objetivo, qint64) {
            anota(tr("El objetivo %1 sale de la zona").arg(objetivo));
        });
        connect(m_mapa, &MapWidget::targetSelectionChanged, this, [this](qint64 id) {
            anota(id >= 0 ? tr("Objetivo resaltado: %1").arg(id) : tr("Sin objetivo resaltado"));
        });
        connect(m_mapa, &MapWidget::featureSelected, this, [this](qint64 id) {
            if (id >= 0)
                anota(tr("featureSelected: %1").arg(nombreDe(id)));
        });
        connect(m_mapa, &MapWidget::elementClicked, this, [this](const MapHit &hit, const QPoint &) {
            if (!hit.features.isEmpty())
                anota(tr("Clic: %1 entidad(es) debajo: %2")
                          .arg(hit.features.size()).arg(listaDe(hit)));
        });
        // UNA señal por clic derecho, con todas las entidades: un submenu por cada una.
        connect(m_mapa, &MapWidget::contextMenuRequested, this,
                [this](const MapHit &hit, const QPoint &global) {
            anota(tr("Menú: %1 entidad(es) debajo: %2").arg(hit.features.size()).arg(listaDe(hit)));
            if (hit.features.isEmpty())
                return;
            QMenu menu;
            for (const MapHitFeature &e : hit.features) {
                QMenu *sub = menu.addMenu(nombreDe(e.id));
                sub->addAction(tr("Seleccionar"), this, [this, e] { m_mapa->selectFeature(e.id); });
                sub->addAction(tr("Decir cuál es"), this,
                               [this, e] { anota(tr("Elegida en el menú: %1").arg(nombreDe(e.id))); });
            }
            menu.exec(global);
        });
    }

    // Nombre legible de una entidad para el registro y el menu.
    QString nombreDe(qint64 id) const
    {
        const auto f = m_mapa->feature(id);
        if (!f)
            return QString::number(id);
        return f->name.isEmpty() ? QStringLiteral("%1 #%2").arg(f->type).arg(id) : f->name;
    }

    // Los nombres de las entidades de un MapHit, de arriba abajo.
    QString listaDe(const MapHit &hit) const
    {
        QStringList nombres;
        for (const MapHitFeature &e : hit.features)
            nombres << nombreDe(e.id);
        return nombres.join(QStringLiteral(" > "));
    }

    // Anade una linea al registro del panel.
    void anota(const QString &texto)
    {
        if (m_registro)
            m_registro->appendPlainText(texto);
    }

    MapWidget *m_mapa = nullptr;
    QTimer m_tic;
    QDateTime m_reloj;                 // tiempo simulado
    QVector<Guardado> m_a, m_b;        // el almacen de la app: las dos informaciones
    char m_visible = 'A';
    int m_alarmas = 0;
    qint64 m_zona = -1, m_sector = -1, m_anillos = -1;
    qint64 m_zonaGrande = -1, m_zonaChica = -1, m_linea = -1;
    QGeoCoordinate m_cruce;
    QVector<qint64> m_red, m_fuentes;

    QLabel *m_titulo = nullptr;
    QLabel *m_contador = nullptr;
    QCheckBox *m_silenciosa = nullptr;
    QCheckBox *m_conservar = nullptr;
    QCheckBox *m_historia = nullptr;
    QComboBox *m_fondoColor = nullptr;
    QSpinBox *m_fondoAlfa = nullptr;
    QCheckBox *m_borde = nullptr;
    QComboBox *m_bordeColor = nullptr;
    QSpinBox *m_bordeGrosor = nullptr;
    QCheckBox *m_halo = nullptr;
    QComboBox *m_haloColor = nullptr;
    QSpinBox *m_haloAlfa = nullptr;
    QSpinBox *m_haloGrosor = nullptr;
    QCheckBox *m_global = nullptr;
    QPlainTextEdit *m_registro = nullptr;
};

namespace {

// Deja que la aplicacion procese eventos unos milisegundos (los objetivos se repintan
// con un temporizador) y fuerza un repintado completo con grab().
void esperaRepintado(MapWidget *mapa)
{
    QElapsedTimer reloj;
    reloj.start();
    while (reloj.elapsed() < 150)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    mapa->grab();
}

// Anota un fallo y devuelve false; se usa para encadenar las comprobaciones.
bool exige(bool condicion, const char *mensaje, int *fallos)
{
    if (!condicion) {
        QTextStream(stderr) << "FALLO: " << mensaje << "\n";
        ++*fallos;
    }
    return condicion;
}

// Pixel del mapa donde cae una coordenada. El API publica el camino contrario (hitTest da
// la coordenada de un pixel), asi que se interpola entre dos esquinas en la proyeccion del
// mapa (longitud lineal, latitud en Mercator). Vale aunque la vista no haya podido
// centrarse donde se pidio, que es lo que pasa con un paquete de muy poco zoom.
QPoint pixelDe(MapWidget *m, const QGeoCoordinate &c)
{
    const auto mercator = [](double lat) {
        const double pi = 3.14159265358979323846;
        return std::log(std::tan(pi / 4.0 + lat * pi / 360.0));
    };
    const QPoint a(m->width() / 4, m->height() / 4);
    const QPoint b(3 * m->width() / 4, 3 * m->height() / 4);
    const QGeoCoordinate ca = m->hitTest(a).coordinate;
    const QGeoCoordinate cb = m->hitTest(b).coordinate;
    const double fx = (c.longitude() - ca.longitude()) / (cb.longitude() - ca.longitude());
    const double fy = (mercator(c.latitude()) - mercator(ca.latitude()))
                      / (mercator(cb.latitude()) - mercator(ca.latitude()));
    return QPoint(qRound(a.x() + fx * (b.x() - a.x())), qRound(a.y() + fy * (b.y() - a.y())));
}

// Comprueba sin ventana lo que el panel permite tocar: el cambio de pantalla de ida y
// vuelta (historia, alarmas, sector, anillos, resaltado), las tres entidades bajo el cruce,
// la seleccion sin resaltado y la caja de los rotulos. Sale con 0 si todo esta bien.
int comprobar(Ventana &v)
{
    int fallos = 0;
    MapWidget *m = v.mapa();
    if (!m->isReady()) {
        QTextStream(stderr) << "FALLO: el mapa no esta listo: " << m->lastError() << "\n";
        return 1;
    }
    v.paraElReloj();                // el tiempo lo mueve la comprobacion, no el temporizador
    v.show();                       // con QT_QPA_PLATFORM=offscreen no abre nada de verdad
    m->setCenter(kCentro);
    m->setZoom(10);
    esperaRepintado(m);

    // --- 1. Cambio de pantalla ---------------------------------------------------
    for (int i = 0; i < 5; ++i)
        v.avanza(30);
    exige(m->targetCount() == kPorPantalla, "la pantalla A no tiene sus objetivos", &fallos);
    exige(m->feature(v.sector()).has_value(), "falta el sector del objetivo 100", &fallos);
    exige(v.alarmas() > 0, "ningun objetivo de A dio la alarma al entrar en la zona", &fallos);
    const int puntosA = int(m->targetHistory(kIdA).size());
    const QDateTime origenA = m->targetOrigin(kIdA).time;
    exige(puntosA == 6, "la historia de A-0 deberia tener 6 puntos antes de cambiar", &fallos);

    // A la pantalla B, con todo puesto: silenciosa, conservando y con historia.
    v.ponOpciones(true, true, true);
    m->setSelectedTarget(kIdA);
    int alarmas = v.alarmas();
    v.muestra('B');
    exige(m->selectedTarget() == -1, "el resaltado sobrevivio al cambio de pantalla", &fallos);
    exige(m->target(kIdB).has_value() && !m->target(kIdA).has_value(),
          "tras cambiar deberian estar los objetivos de B y no los de A", &fallos);
    exige(m->feature(v.sector()).has_value(), "el sector deberia quedar suspendido, no borrado",
          &fallos);
    exige(v.alarmas() == alarmas, "la carga silenciosa dio alarmas", &fallos);

    // Mientras se ve B, A sigue moviendose en el almacen de la app.
    for (int i = 0; i < 4; ++i)
        v.avanza(30);

    // Vuelta a A: la historia llega entera (la que se vio y la que paso oculta).
    alarmas = v.alarmas();
    v.muestra('A');
    const QVector<TargetTrackPoint> vuelta = m->targetHistory(kIdA);
    exige(vuelta.size() == puntosA + 4, "la historia de A-0 no volvio entera", &fallos);
    exige(!vuelta.isEmpty() && vuelta.first().time == origenA && m->targetOrigin(kIdA).time == origenA,
          "el origen de A-0 no conserva su hora", &fallos);
    exige(v.alarmas() == alarmas, "volver a A repitio las alarmas", &fallos);
    const auto sector = m->feature(v.sector());
    const auto a0 = m->target(kIdA);
    exige(sector.has_value() && a0.has_value()
              && sector->geometry.first().distanceTo(a0->position) < 1.0,
          "el sector no reaparecio sobre su objetivo", &fallos);
    exige(m->rangeRings(v.anillos()).has_value(), "los anillos del objetivo se perdieron", &fallos);

    // Sin carga silenciosa, lo mismo SI repite las alarmas: es lo que arregla.
    v.ponOpciones(false, true, true);
    alarmas = v.alarmas();
    v.muestra('B');
    v.muestra('A');
    exige(v.alarmas() > alarmas, "sin carga silenciosa deberian repetirse las alarmas", &fallos);

    // Sin historia, la trayectoria empieza de cero; sin conservar, el sector se pierde.
    v.ponOpciones(true, false, false);
    v.muestra('B');
    v.muestra('A');
    exige(m->targetHistory(kIdA).size() == 1, "sin historia deberia empezar de cero", &fallos);
    exige(!m->feature(v.sector()).has_value(), "sin conservar, el sector deberia borrarse", &fallos);

    // --- 2. Varias entidades bajo el cursor --------------------------------------
    m->setCenter(v.cruce());
    esperaRepintado(m);
    const QPoint centro = pixelDe(m, v.cruce());
    const MapHit hit = m->hitTest(centro);
    // La linea, de la capa mas alta, va siempre la primera; y las dos zonas estan debajo.
    const auto posicion = [&hit](qint64 id) {
        for (int i = 0; i < hit.features.size(); ++i)
            if (hit.features.at(i).id == id)
                return i;
        return -1;
    };
    exige(posicion(v.linea()) == 0 && hit.featureId == v.linea(),
          "la primera deberia ser la linea (capa mas alta)", &fallos);
    exige(posicion(v.zonaChica()) > 0 && posicion(v.zonaGrande()) > 0,
          "bajo el cruce deberian salir tambien las dos zonas", &fallos);
    // El orden exacto solo se exige con zoom de verdad: en un paquete que no pasa de zoom 2
    // todo el ejemplo cabe en unos pixeles y bajo el cursor cae casi todo.
    if (m->zoom() >= 8) {
        exige(hit.features.size() == 3, "en el cruce deberian salir tres entidades", &fallos);
        exige(posicion(v.zonaChica()) == 1 && posicion(v.zonaGrande()) == 2,
              "debajo de la linea, la zona chica y luego la grande", &fallos);
    }
    exige(m->featuresAt(centro) == hit.features, "featuresAt no coincide con hitTest", &fallos);

    // --- 3. Seleccionar sin resaltar ----------------------------------------------
    const qint64 red = v.red().value(0, -1);
    exige(m->feature(red).has_value() && !m->feature(red)->highlightWhenSelected,
          "la red deberia nacer sin resaltado", &fallos);
    m->selectFeature(red);
    exige(m->selectedFeature() == red, "una entidad sin resaltado tambien se selecciona", &fallos);
    v.resaltaRed(true);
    exige(m->feature(red)->highlightWhenSelected, "no se pudo poner el resaltado a la red", &fallos);
    m->clearSelection();

    // --- 4. Caja de los rotulos ---------------------------------------------------
    const qint64 fuente = v.fuentes().value(0, -1);
    m->setCenter(m->feature(fuente)->position());
    esperaRepintado(m);
    const QRect caja = m->featureLabelRect(fuente);
    exige(caja.isValid(), "la fuente de ejemplo no tiene rotulo", &fallos);
    MapFeature f = *m->feature(fuente);
    exige(f.style.labelBackground == QColor(255, 255, 255, 190),
          "por defecto, el fondo es el blanco de siempre", &fallos);
    f.style.labelBackground = QColor(0, 0, 0, 0);
    f.style.labelHaloColor = QColor(255, 255, 255, 220);
    exige(m->updateFeature(f), "no se pudo cambiar el estilo del rotulo", &fallos);
    esperaRepintado(m);
    exige(m->featureLabelRect(fuente) == caja, "quitar la caja movio la caja del raton", &fallos);
    // Sin caja el rotulo se sigue agarrando. Con poco zoom las tres fuentes quedan a unos
    // pixeles y sus rotulos se solapan (y segun la fuente tipografica de cada Qt manda uno u
    // otro), asi que solo se exige que salga alguna fuente; la exacta, con zoom de verdad.
    const qint64 agarrado = m->featureLabelAt(caja.center());
    exige(v.fuentes().contains(agarrado), "sin caja, el rotulo ya no se agarra", &fallos);
    if (m->zoom() >= 8)
        exige(agarrado == fuente, "sin caja, se agarra el rotulo de otra fuente", &fallos);
    LabelBoxStyle global;
    global.background = QColor(13, 71, 161, 255);
    m->setLabelBoxStyle(global);
    exige(m->labelBoxStyle().background == QColor(13, 71, 161, 255),
          "setLabelBoxStyle no guarda el estilo", &fallos);

    if (fallos == 0)
        QTextStream(stdout) << "cambio_pantalla --comprobar: todo correcto\n";
    return fallos == 0 ? 0 : 1;
}

} // namespace

// Arranque: carga el paquete de datos (por defecto <exe>/datos) y abre la ventana, o
// comprueba sin ventana (--comprobar) o guarda una imagen (--captura=).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cambio_pantalla"));

    QStringList posicionales;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith(QLatin1String("--")))
            posicionales << a;
    const QString datos = posicionales.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/datos")
        : posicionales.first();

    Ventana ventana(datos);
    if (!ventana.mapa()->isReady())
        return 1;
    if (app.arguments().contains(QStringLiteral("--comprobar")))
        return comprobar(ventana);
    ventana.show();

    // --captura=imagen.png: unos pasos de simulacion, guarda la ventana y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(strlen("--captura=")));
            for (int i = 0; i < 20; ++i)
                ventana.avanza(30);
            ventana.mapa()->setSelectedTarget(kIdA);
            QTimer::singleShot(2500, &app, [&ventana, &app, destino] {
                esperaRepintado(ventana.mapa());
                ventana.grab().save(destino);
                app.quit();
            });
            return app.exec();
        }
    }
    return app.exec();
}
