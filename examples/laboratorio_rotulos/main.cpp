// laboratorio_rotulos - banco de pruebas de los rotulos y de la escala polar de libmapa.
//
// Sirve para ver y tocar, con el raton, lo que añade la fase 1 de la convergencia con el
// fork extendido. Un panel a la derecha cambia en vivo:
//   - Rotulos de ENTIDADES: el lado donde cuelga el nombre (LabelAnchor, 10 posiciones) y
//     su tamano de fuente (FeatureStyle::labelFontSize), sobre seis aerodromos de ejemplo.
//   - FORMULARIO de objetivos: tres objetivos con su formulario de tres lineas y colores por
//     segmento (MapTarget::labelContent), con la colocacion clasica o COLGADA de la esquina
//     (TargetLabelPlacement), fuente monoespaciada, ancho en caracteres, columnas por
//     segmento, desplazamiento por defecto y color de seleccion. El formulario se arrastra
//     con el raton; soltarlo fuera del mapa cancela y la senal targetLabelMoved se ve abajo.
//   - ESCALA POLAR: anillos cada 10 km (hasta 300 o 600 km) y radiales cada 10 grados en
//     UN solo RangeRings, con el enfasis en gris oscuro de uno de cada cinco anillos y de
//     un radial de cada tres.
//
//   laboratorio_rotulos [paquete de datos]   (por defecto: <exe>/datos)
//   laboratorio_rotulos <paquete> --comprobar
//        -> sin ventana: monta lo anterior y comprueba posiciones y tamanos de los rotulos.
//   laboratorio_rotulos <paquete> --captura=imagen.png
//        -> abre la ventana con el formulario colgado y la escala polar, guarda una imagen y sale.

#include <libmapa/MapFeature.h>
#include <libmapa/MapTarget.h>
#include <libmapa/MapTypes.h>
#include <libmapa/MapWidget.h>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

using namespace libmapa;

namespace {

// Nombre y valor de cada posicion del rotulo, en el orden del combo.
struct Posicion {
    const char *nombre;
    LabelAnchor ancla;
};
const Posicion kPosiciones[] = {
    {"Auto (como siempre)", LabelAnchor::Auto}, {"Encima", LabelAnchor::Top},
    {"Debajo", LabelAnchor::Bottom},            {"Izquierda", LabelAnchor::Left},
    {"Derecha", LabelAnchor::Right},            {"Arriba izquierda", LabelAnchor::TopLeft},
    {"Arriba derecha", LabelAnchor::TopRight},  {"Abajo izquierda", LabelAnchor::BottomLeft},
    {"Abajo derecha", LabelAnchor::BottomRight}, {"Centrado", LabelAnchor::Center},
};

const QGeoCoordinate kCentro(23.05, -82.35);   // La Habana y alrededores

// Seis aerodromos de ejemplo (nombre y posicion aproximada).
struct Aerodromo {
    const char *nombre;
    double lat;
    double lon;
};
const Aerodromo kAerodromos[] = {
    {"San Antonio", 22.87, -82.51}, {"Baracoa", 23.07, -82.58}, {"Playa", 23.10, -82.42},
    {"Santa Fe", 23.08, -82.52},    {"Jaimanitas", 23.08, -82.50}, {"Bejucal", 22.95, -82.35},
};

// Una linea del formulario de un objetivo con sus segmentos (texto y color).
TargetLabelLine linea(std::initializer_list<TargetLabelSegment> segmentos)
{
    TargetLabelLine l;
    for (const TargetLabelSegment &s : segmentos)
        l.segments.append(s);
    return l;
}

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
        resize(1180, 760);
        m_mapa->setCenter(kCentro);
        m_mapa->setZoom(11);

        creaAerodromos();
        creaObjetivos();
        auto *cuerpo = new QWidget(this);
        auto *caja = new QHBoxLayout(cuerpo);
        caja->setContentsMargins(0, 0, 0, 0);
        caja->addWidget(m_mapa, 1);
        caja->addWidget(creaPanel());
        setCentralWidget(cuerpo);

        connect(m_mapa, &MapWidget::targetLabelMoved, this,
                [this](qint64 id, const QPointF &offset) {
                    statusBar()->showMessage(
                        tr("Formulario del objetivo %1 soltado en offset (%2, %3)")
                            .arg(id).arg(offset.x()).arg(offset.y()));
                });
        aplicaFormato();
        escalaPolar();
    }

    MapWidget *mapa() const { return m_mapa; }
    qint64 primerAerodromo() const { return m_aerodromos.value(0, -1); }
    QVector<qint64> objetivos() const { return m_objetivos; }
    qint64 anillos() const { return m_anillos; }

    // Cambia la posicion del rotulo de todos los aerodromos y repinta.
    void ponAncla(LabelAnchor ancla)
    {
        for (qint64 id : m_aerodromos) {
            auto f = m_mapa->feature(id);
            if (!f)
                continue;
            f->style.labelAnchor = ancla;
            m_mapa->updateFeature(*f);
        }
    }

    // Cambia el tamano de fuente del rotulo de todos los aerodromos (0 = el del mapa).
    void ponTamano(int puntos)
    {
        for (qint64 id : m_aerodromos) {
            auto f = m_mapa->feature(id);
            if (!f)
                continue;
            f->style.labelFontSize = puntos;
            m_mapa->updateFeature(*f);
        }
    }

    // Aplica al mapa el formato de los formularios segun los controles del panel.
    void aplicaFormato()
    {
        TargetLabelFormat fmt;
        fmt.placement = m_colgado->isChecked() ? TargetLabelPlacement::Hanging
                                               : TargetLabelPlacement::Classic;
        fmt.monospace = m_mono->isChecked();
        fmt.maxChars = m_maxChars->value();
        if (m_columnas->isChecked())
            fmt.segmentChars = {8, 8};
        m_mapa->setTargetLabelFormat(fmt);
        m_mapa->setDefaultTargetLabelOffset(QPointF(m_offX->value(), m_offY->value()));
    }

    // Dibuja la escala polar a 300 o 600 km, con o sin enfasis, sustituyendo la anterior.
    void escalaPolar()
    {
        if (m_anillos >= 0)
            m_mapa->removeRangeMark(m_anillos);
        m_anillos = -1;
        if (!m_polar->isChecked())
            return;
        RangeRings r;
        r.center = kCentro;
        r.spacingMeters = 10000.0;
        r.count = m_escala->currentIndex() == 0 ? 30 : 60;     // 300 km o 600 km
        r.spokeEveryDeg = 10;
        r.color = QColor(150, 150, 150);
        r.labels = false;
        if (m_enfasis->isChecked()) {
            r.majorRingEvery = 5;                // 50, 100, 150... km
            r.majorSpokeEveryDeg = 30;
            r.majorColor = QColor(60, 60, 60);
        }
        m_anillos = m_mapa->addRangeRings(r);
        m_mapa->setZoom(m_escala->currentIndex() == 0 ? 7 : 6);
    }

private:
    // Seis aerodromos con nombre, en una capa propia, para probar el LabelAnchor.
    void creaAerodromos()
    {
        m_mapa->addFeatureLayer(QStringLiteral("aerodromos"), tr("Aerodromos"), 50);
        for (const Aerodromo &a : kAerodromos) {
            MapFeature f;
            f.kind = GeometryKind::Point;
            f.layerId = QStringLiteral("aerodromos");
            f.type = QStringLiteral("aerodromo");
            f.name = QString::fromUtf8(a.nombre);
            f.geometry = {QGeoCoordinate(a.lat, a.lon)};
            f.style.lineColor = QColor(0x15, 0x65, 0xc0);
            m_aerodromos.append(m_mapa->addFeature(f));
        }
    }

    // Tres objetivos con formulario de tres lineas: identificacion y estado en la
    // primera (cada una de su color), y datos de dominio en las otras dos.
    void creaObjetivos()
    {
        struct Dato {
            double lat, lon, rumbo;
            const char *id, *estado, *l2, *l3;
        };
        const Dato datos[] = {
            {23.30, -82.20, 80.0, "E046", "M1M2", "+42  I   01655", "_S_A     1.2.3.4"},
            {23.20, -82.60, 160.0, "E051", "EMER", "+38  I   00980", "_S_B     5.6.7.8"},
            {22.85, -82.10, 300.0, "E077", "DET", "+35  I   00720", "_S_C     9.0.1.2"},
        };
        const QColor colorEstado[] = {QColor(0, 120, 0), QColor(200, 0, 0), QColor(200, 120, 0)};
        int i = 0;
        for (const Dato &d : datos) {
            MapTarget t;
            t.position = QGeoCoordinate(d.lat, d.lon);
            t.headingDeg = d.rumbo;
            t.kind = QStringLiteral("aeronave");
            t.label = QString::fromUtf8(d.id);       // lo que se veria sin formulario por segmentos
            t.labelContent = {
                linea({{QString::fromUtf8(d.id), QColor(0, 70, 200)},
                       {QString::fromUtf8(d.estado), colorEstado[i]}}),
                linea({{QString::fromUtf8(d.l2), QColor()}}),
                linea({{QString::fromUtf8(d.l3), QColor()}}),
            };
            m_objetivos.append(m_mapa->addTarget(t));
            ++i;
        }
    }

    // Construye el panel de la derecha: tres grupos de controles y la ayuda.
    QWidget *creaPanel()
    {
        auto *panel = new QWidget(this);
        panel->setFixedWidth(330);
        auto *col = new QVBoxLayout(panel);

        // --- Rotulos de entidades ---------------------------------------------
        auto *g1 = new QGroupBox(tr("Rotulos de entidades"), panel);
        auto *f1 = new QFormLayout(g1);
        auto *posicion = new QComboBox(g1);
        for (const Posicion &p : kPosiciones)
            posicion->addItem(tr(p.nombre));
        auto *tamano = new QSpinBox(g1);
        tamano->setRange(0, 40);
        tamano->setSpecialValueText(tr("0 (el del mapa)"));
        f1->addRow(tr("Lado del anclaje"), posicion);
        f1->addRow(tr("Fuente (pt)"), tamano);
        connect(posicion, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int i) { ponAncla(kPosiciones[i].ancla); });
        connect(tamano, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) { ponTamano(v); });
        col->addWidget(g1);

        // --- Formulario de los objetivos --------------------------------------
        auto *g2 = new QGroupBox(tr("Formulario de objetivos (arrastrable)"), panel);
        auto *f2 = new QFormLayout(g2);
        m_colgado = new QCheckBox(tr("Colgado de la esquina mas cercana"), g2);
        m_mono = new QCheckBox(tr("Fuente monoespaciada"), g2);
        m_columnas = new QCheckBox(tr("Columnas 8 + 8 en la primera linea"), g2);
        m_maxChars = new QSpinBox(g2);
        m_maxChars->setRange(0, 40);
        m_maxChars->setSpecialValueText(tr("0 (sin limite)"));
        m_offX = new QSpinBox(g2);
        m_offY = new QSpinBox(g2);
        for (QSpinBox *s : {m_offX, m_offY})
            s->setRange(-300, 300);
        m_offX->setValue(-31);
        m_offY->setValue(-31);
        auto *color = new QPushButton(tr("Color de seleccion..."), g2);
        auto *restablecer = new QPushButton(tr("Quitar offsets propios"), g2);
        f2->addRow(m_colgado);
        f2->addRow(m_mono);
        f2->addRow(m_columnas);
        f2->addRow(tr("Ancho (caracteres)"), m_maxChars);
        f2->addRow(tr("Offset por defecto X"), m_offX);
        f2->addRow(tr("Offset por defecto Y"), m_offY);
        f2->addRow(color);
        f2->addRow(restablecer);
        for (QCheckBox *c : {m_colgado, m_mono, m_columnas})
            connect(c, &QCheckBox::toggled, this, [this] { aplicaFormato(); });
        for (QSpinBox *s : {m_maxChars, m_offX, m_offY})
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this] { aplicaFormato(); });
        connect(color, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_mapa->targetLabelSelectionColor(), this);
            if (c.isValid())
                m_mapa->setTargetLabelSelectionColor(c);
        });
        connect(restablecer, &QPushButton::clicked, this, [this] {
            for (qint64 id : m_objetivos)
                m_mapa->clearTargetLabelOffset(id);
        });
        col->addWidget(g2);

        // --- Escala polar ------------------------------------------------------
        auto *g3 = new QGroupBox(tr("Escala polar"), panel);
        auto *f3 = new QFormLayout(g3);
        m_polar = new QCheckBox(tr("Mostrar anillos y radiales"), g3);
        m_polar->setChecked(true);
        m_escala = new QComboBox(g3);
        m_escala->addItems({tr("300 km (30 anillos)"), tr("600 km (60 anillos)")});
        m_enfasis = new QCheckBox(tr("Enfasis: 1 de cada 5 anillos y 1 de cada 3 radiales"), g3);
        m_enfasis->setChecked(true);
        f3->addRow(m_polar);
        f3->addRow(tr("Alcance"), m_escala);
        f3->addRow(m_enfasis);
        for (QCheckBox *c : {m_polar, m_enfasis})
            connect(c, &QCheckBox::toggled, this, [this] { escalaPolar(); });
        connect(m_escala, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this] { escalaPolar(); });
        col->addWidget(g3);

        auto *ayuda = new QLabel(
            tr("Arrastra un formulario con el raton: queda fijado como propio. Soltarlo "
               "fuera del mapa cancela el arrastre. La senal targetLabelMoved se ve abajo."),
            panel);
        ayuda->setWordWrap(true);
        col->addWidget(ayuda);
        col->addStretch(1);
        return panel;
    }

    MapWidget *m_mapa = nullptr;
    QVector<qint64> m_aerodromos;
    QVector<qint64> m_objetivos;
    qint64 m_anillos = -1;
    QCheckBox *m_colgado = nullptr;
    QCheckBox *m_mono = nullptr;
    QCheckBox *m_columnas = nullptr;
    QSpinBox *m_maxChars = nullptr;
    QSpinBox *m_offX = nullptr;
    QSpinBox *m_offY = nullptr;
    QCheckBox *m_polar = nullptr;
    QComboBox *m_escala = nullptr;
    QCheckBox *m_enfasis = nullptr;
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

// Comprueba sin ventana lo que el panel permite tocar: la posicion y el tamano del rotulo
// de una entidad, el formulario colgado y su ancho fijo, el offset por defecto y los 60
// anillos de la escala de 600 km. Sale con 0 si todo esta bien.
int comprobar(Ventana &v)
{
    int fallos = 0;
    MapWidget *m = v.mapa();
    if (!m->isReady()) {
        QTextStream(stderr) << "FALLO: el mapa no esta listo: " << m->lastError() << "\n";
        return 1;
    }
    // Con la ventana oculta la capa de objetivos no llega a pintarse: se muestra (con
    // QT_QPA_PLATFORM=offscreen no abre nada de verdad) y el diseno da el tamano al mapa.
    v.show();
    m->setCenter(kCentro);
    m->setZoom(10);          // el paquete mas pequeno solo llega a zoom 2: no se exige
    // El objetivo seleccionado siempre muestra su rotulo (salta el declutter, que a poco
    // zoom dibuja uno solo por celda de pantalla), asi que se comprueba ese.
    m->setSelectedTarget(v.objetivos().value(0, -1));
    esperaRepintado(m);

    // 1. Entidades: Encima deja la caja sobre el punto; mas fuente la hace mas grande.
    const qint64 id = v.primerAerodromo();
    const QRect clasico = m->featureLabelRect(id);
    exige(clasico.isValid(), "el aerodromo no tiene rotulo", &fallos);
    v.ponAncla(LabelAnchor::Top);
    const QRect encima = m->featureLabelRect(id);
    exige(qAbs(encima.width() - clasico.width()) <= 1 && qAbs(encima.height() - clasico.height()) <= 1,
          "el ancla no debe cambiar el tamano", &fallos);
    exige(encima.bottom() < clasico.bottom(), "Top deberia dejar la caja sobre el punto", &fallos);
    v.ponTamano(24);
    exige(m->featureLabelRect(id).height() > encima.height(),
          "la fuente 24 deberia crecer la caja", &fallos);
    exige(m->feature(id)->style.labelFontSize == 24, "el tamano de fuente no se guardo", &fallos);

    // 2. Formulario: el contenido por segmentos gana a 'label' (tres lineas de alto).
    esperaRepintado(m);
    const qint64 t0 = v.objetivos().value(0, -1);
    const QRect form = m->targetLabelRect(t0);
    exige(form.isValid(), "el formulario del objetivo no se dibujo", &fallos);
    if (form.isValid())
        exige(form.height() >= 3 * 10, "el formulario deberia tener tres lineas", &fallos);

    // 3. El offset por defecto lo ven los objetivos que no fijaron el suyo; el propio manda.
    const QPoint antes = form.topLeft();
    m->setDefaultTargetLabelOffset(QPointF(-31.0, -31.0));
    esperaRepintado(m);
    const QPoint conDefecto = m->targetLabelRect(t0).topLeft();
    m->setDefaultTargetLabelOffset(QPointF(20.0, 30.0));
    esperaRepintado(m);
    exige(m->targetLabelRect(t0).topLeft() == conDefecto + QPoint(51, 61),
          "el offset por defecto no se aplico", &fallos);
    m->setTargetLabelOffset(t0, QPointF(-40.0, 0.0));
    esperaRepintado(m);
    exige(m->targetLabelRect(t0).topLeft() != conDefecto + QPoint(51, 61),
          "el offset propio deberia mandar sobre el de por defecto", &fallos);
    exige(m->clearTargetLabelOffset(t0), "no se pudo quitar el offset propio", &fallos);
    Q_UNUSED(antes);

    // 4. Colgado y ancho fijo: con 16 columnas todos los formularios miden lo mismo.
    TargetLabelFormat fmt;
    fmt.placement = TargetLabelPlacement::Hanging;
    fmt.monospace = true;
    fmt.maxChars = 16;
    fmt.segmentChars = {8, 8};
    m->setTargetLabelFormat(fmt);
    m->setDefaultTargetLabelOffset(QPointF(-60.0, -40.0));    // arriba-izquierda
    esperaRepintado(m);
    const QRect colgada = m->targetLabelRect(t0);
    exige(colgada.isValid(), "no se dibujo el formulario colgado", &fallos);
    for (qint64 otro : v.objetivos()) {
        const QRect r = m->targetLabelRect(otro);
        if (r.isValid())
            exige(qAbs(r.width() - colgada.width()) <= 1,
                  "con 16 columnas fijas todos los formularios miden lo mismo", &fallos);
    }

    // 5. Escala: por defecto 300 km (30 anillos); se amplia a 600 km sin que el tope lo recorte.
    const auto r = m->rangeRings(v.anillos());
    exige(r.has_value(), "no hay escala polar", &fallos);
    if (r) {
        exige(r->count == 30, "por defecto la escala es de 300 km (30 anillos)", &fallos);
        RangeRings largo = *r;
        largo.count = 60;
        exige(m->updateRangeRings(v.anillos(), largo), "no se pudo ampliar a 600 km", &fallos);
        exige(m->rangeRings(v.anillos())->count == 60, "el tope recorto los 60 anillos", &fallos);
        exige(m->rangeRings(v.anillos())->majorRingEvery == 5, "falta el enfasis de anillos", &fallos);
    }

    if (fallos == 0)
        QTextStream(stdout) << "laboratorio_rotulos --comprobar: todo correcto\n";
    return fallos == 0 ? 0 : 1;
}

} // namespace

// Arranque: carga el paquete de datos (por defecto <exe>/datos) y abre la ventana, o
// comprueba sin ventana (--comprobar) o guarda una imagen (--captura=).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("laboratorio_rotulos"));

    QStringList posicionales;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith(QLatin1String("--")))
            posicionales << a;
    const QString datos = posicionales.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/datos")
        : posicionales.first();

    Ventana ventana(datos);
    if (app.arguments().contains(QStringLiteral("--comprobar")))
        return comprobar(ventana);
    ventana.show();

    // --captura=imagen.png: formulario colgado, escala de 600 km, y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(strlen("--captura=")));
            TargetLabelFormat fmt;
            fmt.placement = TargetLabelPlacement::Hanging;
            fmt.monospace = true;
            fmt.maxChars = 16;
            fmt.segmentChars = {8, 8};
            ventana.mapa()->setTargetLabelFormat(fmt);
            ventana.mapa()->setDefaultTargetLabelOffset(QPointF(-31.0, -31.0));
            ventana.ponAncla(LabelAnchor::Top);
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
