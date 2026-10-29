// laboratorio_anillos - banco de pruebas de los ANILLOS DE DISTANCIA y los RADIALES de libmapa.
//
// Sirve para comprobar a ojo, con el raton, que la escala de los anillos es correcta:
//   - Arranca con el mapa de costas («costas», Cuba_Vector.sqlitedb) si el paquete lo trae,
//     SIN nada hibrido encima: el overlay vectorial del mapa.json, si lo hay, se quita.
//     Las demas capas base se pueden elegir en el panel.
//   - Anillos con radiales en un punto: se muestran y se ocultan, y el centro se cambia con
//     un clic en el mapa (herramienta PickPoint).
//   - Separacion, numero de anillos, radiales, rotulos (distancia de cada anillo y marcacion
//     de cada radial), unidades (km o NM), barra de escala y cuadricula, en vivo.
//   - Coordenadas del raton (decimal, grados-minutos-segundos y UTM) y, sobre todo, su
//     DISTANCIA y MARCACION desde el centro de los anillos: al pasar el raton por un anillo
//     la distancia tiene que coincidir con su rotulo, y la marcacion con el radial.
//   - ESCALA POLAR de 300 o 600 km en UN solo RangeRings (hasta 100 anillos; los radiales no
//     cuentan), con enfasis en uno de cada N anillos y de cada N grados y otro color de radiales.
//   - ROTULOS de entidades: el lado donde cuelga el nombre (LabelAnchor) y su fuente, sobre
//     los puntos que agregues con el buscador de lugares.
//   - FORMULARIO de objetivos arrastrable: tres objetivos con formulario de tres lineas y
//     colores por segmento, colocacion colgada de la esquina, fuente monoespaciada, ancho y
//     columnas, offset por defecto, color de seleccion y aviso al soltarlo (soltar fuera del
//     mapa cancela).
//   - A la derecha, que hay que ver y que probar.
//
//   laboratorio_anillos [paquete de datos]   (por defecto: <exe>/datos)
//   laboratorio_anillos <paquete> --comprobar
//        -> sin ventana: monta lo anterior y comprueba anillos, centro, unidades y lecturas.
//   laboratorio_anillos <paquete> --captura=imagen.png
//        -> abre la ventana, guarda una imagen y sale.

#include <libmapa/Coordinates.h>
#include <libmapa/MapFeature.h>
#include <libmapa/MapTarget.h>
#include <libmapa/MapTypes.h>
#include <libmapa/MapWidget.h>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

using namespace libmapa;

namespace {

const QGeoCoordinate kHabana(23.1136, -82.3666);   // centro inicial de los anillos
const QString kCapaInicial = QStringLiteral("costas");
constexpr double kMetrosPorMilla = 1852.0;

// Radiales que se pueden elegir: grados entre radiales (0 = sin radiales).
const int kRadiales[] = { 0, 10, 15, 30, 45, 90 };

// Nombre y valor de cada posicion del rotulo de una entidad, en el orden del combo.
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

// Una linea del formulario de un objetivo con sus segmentos (texto y color).
TargetLabelLine linea(std::initializer_list<TargetLabelSegment> segmentos)
{
    TargetLabelLine l;
    for (const TargetLabelSegment &s : segmentos)
        l.segments.append(s);
    return l;
}

// Texto con la ayuda del panel: que se ve y que hay que comprobar, por pasos.
QString textoAyuda()
{
    return QObject::tr(
        "<b>Qué ver</b><ul>"
        "<li>El mapa de <b>costas</b>, solo, sin nada híbrido encima.</li>"
        "<li>Anillos discontinuos (el último continuo) alrededor de un cuadrado con cruz: "
        "el <b>centro</b>.</li>"
        "<li>Cada anillo rotulado con su <b>distancia</b> arriba, y cada radial con su "
        "<b>marcación</b> en el extremo (el de 000° no se rotula: lo tapa el rótulo del "
        "anillo).</li>"
        "<li>La <b>barra de escala</b> abajo, en las mismas unidades.</li></ul>"
        "<b>Qué probar</b><ol>"
        "<li><b>Escala:</b> pasa el ratón por un anillo. «Desde el centro» tiene que dar "
        "su rótulo (10 km, 20 km...). Compáralo también con la barra de escala.</li>"
        "<li><b>Radiales:</b> pasa el ratón por un radial. La marcación tiene que ser la de "
        "su rótulo (030°, 060°...).</li>"
        "<li><b>Centro:</b> pulsa «Elegir centro» y haz clic en otro sitio (un cabo, una "
        "ciudad). Los anillos se mueven allí; clic derecho o Esc cancela.</li>"
        "<li><b>Mostrar/ocultar:</b> la casilla quita y vuelve a poner los anillos con "
        "los mismos ajustes.</li>"
        "<li><b>Unidades:</b> pasa a millas náuticas. Los rótulos, la barra y la lectura del "
        "ratón cambian juntos; la separación se lee en NM.</li>"
        "<li><b>Forma:</b> los anillos son círculos reales sobre el terreno; en el mapa "
        "(Mercator) se estiran un poco hacia el norte, y más cuanto más grandes.</li>"
        "<li><b>Cuadrícula:</b> enciéndela y comprueba que las coordenadas del ratón "
        "coinciden con las líneas.</li>"
        "<li><b>Escala polar:</b> «Escala de 600 km» pone 60 anillos de 10 km y radiales cada "
        "10° en un solo objeto, con el gris oscuro de un anillo de cada cinco y un radial de "
        "cada tres. «Escala de 300 km» hace lo mismo con 30 anillos.</li>"
        "<li><b>Rótulos de entidades:</b> agrega puntos con el buscador y cambia el lado del "
        "anclaje y la fuente: los nombres se recolocan y cambian de tamaño.</li>"
        "<li><b>Formulario de objetivos:</b> arrastra uno con el ratón (queda fijado y la "
        "barra de estado dice dónde lo soltaste); soltarlo <i>fuera del mapa</i> cancela. "
        "Con «Colgado de la esquina» la caja cuelga del punto por la esquina cercana al "
        "objetivo; prueba también la fuente monoespaciada, el ancho, las columnas 8 + 8, el "
        "offset por defecto y el color de selección.</li>"
        "<li><b>Sectores:</b> el círculo de 50 km y los tres sectores (el de 330° a 30° cruza "
        "el norte) se mueven con el centro. Oculta cada tipo, bórralos por tipo y mira cómo "
        "cambia el recuento.</li>"
        "<li><b>Partes:</b> agrega varios lugares «como parte de Sitios» y toca uno: el panel "
        "dice qué parte de la entidad es. Todos son <i>una</i> entidad con nombre y color por "
        "parte.</li></ol>");
}

} // namespace

// Ventana con el mapa y el panel de pruebas a la derecha.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos, con la capa de costas y sin hibrido, el
    // panel y los anillos iniciales. Sin \a avisar (--comprobar) un paquete que no carga
    // no abre ningun dialogo: lo dice comprobar() por la consola.
    explicit Ventana(const QString &datos, bool avisar = true)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        if (!m_mapa->isReady()) {
            if (avisar)
                QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
            return;
        }
        resize(1280, 820);
        m_mapa->clearVectorOverlay();             // solo el mapa: nada hibrido encima
        for (const BaseLayerInfo &c : m_mapa->availableBaseLayers())
            if (c.id == kCapaInicial)
                m_mapa->setBaseLayerId(kCapaInicial);
        m_mapa->setCenter(kHabana);
        m_mapa->setZoom(9);
        m_centro = kHabana;
        // El mapa recarga lo que se dejo guardado en otras sesiones (y en otros ejemplos, como los
        // «aerodromo» de laboratorio_rotulos): se parte limpio, los puntos los agrega el usuario.
        quitaPuntos();
        m_mapa->addFeatureLayer(QStringLiteral("puntos"), tr("Puntos"), 50);
        creaObjetivos();

        auto *cuerpo = new QWidget(this);
        auto *caja = new QHBoxLayout(cuerpo);
        caja->setContentsMargins(0, 0, 0, 0);
        caja->addWidget(m_mapa, 1);
        caja->addWidget(creaPanel());
        setCentralWidget(cuerpo);

        connect(m_mapa, &MapWidget::mouseMoved, this,
                [this](const QGeoCoordinate &p) { leeRaton(p); });
        connect(m_mapa, &MapWidget::pointPicked, this, [this](const QGeoCoordinate &p) {
            m_mapa->setActiveTool(MapTool::None);
            if (m_colocando) {                    // el clic agrega un punto
                m_colocando = false;
                m_colocar->setText(tr("Agregar un punto con un clic en el mapa"));
                agrega(p, tr("Punto %1").arg(m_puntos.size() + 1), false);
                return;
            }
            m_elegir->setText(tr("Elegir centro (clic en el mapa)"));
            ponCentro(p);
            statusBar()->showMessage(tr("Centro de los anillos en %1")
                                         .arg(formatCoordinate(p, CoordinateFormat::DMS)), 6000);
        });
        connect(m_mapa, &MapWidget::pickCancelled, this, [this] {
            m_colocando = false;
            m_colocar->setText(tr("Agregar un punto con un clic en el mapa"));
            m_elegir->setText(tr("Elegir centro (clic en el mapa)"));
            statusBar()->showMessage(tr("Elección del centro cancelada"), 4000);
        });
        connect(m_mapa, &MapWidget::targetLabelMoved, this,
                [this](qint64 id, const QPointF &offset) {
                    statusBar()->showMessage(
                        tr("Formulario del objetivo %1 soltado en offset (%2, %3)")
                            .arg(id).arg(offset.x()).arg(offset.y()), 8000);
                });
        // Tocar una entidad dice que parte de ella es (subIndex) y cuantas tiene.
        connect(m_mapa, &MapWidget::elementClicked, this,
                [this](const MapHit &hit, const QPoint &) { leeElemento(hit); });
        aplicaFormato();
        aplicaAnillos();
        aplicaSectores();
        statusBar()->showMessage(tr("Capa base: %1").arg(m_mapa->baseLayerId()));
    }

    MapWidget *mapa() const { return m_mapa; }
    qint64 anillos() const { return m_anillos; }
    QString lecturaRaton() const { return m_desdeCentro ? m_desdeCentro->text() : QString(); }
    QCheckBox *casillaMostrar() const { return m_mostrar; }
    QComboBox *unidades() const { return m_unidades; }
    qint64 primerPunto() const { return m_puntos.value(0, -1); }
    QVector<qint64> objetivos() const { return m_objetivos; }
    qint64 sitios() const { return m_sitios; }
    QCheckBox *casillaSectores() const { return m_sectores; }
    QString lecturaElemento() const { return m_lblElemento ? m_lblElemento->text() : QString(); }

    // Dice que hay bajo el clic: la entidad, y si tiene varias partes, CUAL (subIndex).
    void leeElemento(const MapHit &hit)
    {
        QString txt = tr("nada");
        if (hit.featureId >= 0) {
            const auto f = m_mapa->feature(hit.featureId);
            txt = f ? f->name : QString::number(hit.featureId);
            if (f && hit.subIndex >= 0) {
                const PartInfo *pi = f->partInfo(hit.subIndex);
                txt += tr(" · parte %1 de %2").arg(hit.subIndex + 1).arg(f->parts.size());
                if (pi && !pi->name.isEmpty())
                    txt += QStringLiteral(" (%1)").arg(pi->name);
            }
        }
        m_lblElemento->setText(txt);
    }

    // Pone los sectores y el circulo alrededor del centro (o los quita): un circulo de 50 km
    // y tres arcos (de 0 a 90 grados, de 120 a 200 y uno que cruza el norte, de 330 a 30), en
    // un solo lote (una sola senal de repintado y un solo paso de deshacer).
    void aplicaSectores()
    {
        m_mapa->clearFeaturesOfType(QStringLiteral("sector"));
        actualizaConteos();
        if (!m_sectores->isChecked())
            return;
        const auto nuevo = [&](GeometryKind k, double radio, double ini, double fin,
                               const QColor &c, const QString &nombre) {
            MapFeature f;
            f.kind = k;
            f.layerId = QStringLiteral("sectores");
            f.type = QStringLiteral("sector");
            f.name = nombre;
            f.geometry = { m_centro };
            f.radiusMeters = radio;
            f.arcStartDeg = ini;
            f.arcEndDeg = fin;
            f.style.lineColor = c;
            f.style.lineWidth = 2.0;
            f.style.fillColor = QColor(c.red(), c.green(), c.blue(), 40);
            return f;
        };
        QVector<MapFeature> lote;
        lote.append(nuevo(GeometryKind::Circle, 50000.0, 0, 0, QColor(0x15, 0x65, 0xc0),
                          tr("Círculo 50 km")));
        lote.append(nuevo(GeometryKind::Arc, 100000.0, 0, 90, QColor(0x2e, 0x7d, 0x32),
                          tr("Sector 0–90")));
        lote.append(nuevo(GeometryKind::Arc, 150000.0, 120, 200, QColor(0xef, 0x6c, 0x00),
                          tr("Sector 120–200")));
        lote.append(nuevo(GeometryKind::Arc, 200000.0, 330, 30, QColor(0x8e, 0x24, 0xaa),
                          tr("Sector 330–30")));
        m_mapa->addFeatures(lote);
        actualizaConteos();
    }

    // Cambia el lado del anclaje del rotulo de todos los puntos y repinta.
    void ponAncla(LabelAnchor ancla)
    {
        for (qint64 id : m_puntos) {
            auto f = m_mapa->feature(id);
            if (!f)
                continue;
            f->style.labelAnchor = ancla;
            m_mapa->updateFeature(*f);
        }
    }

    // Cambia el tamano de fuente del rotulo de todos los puntos (0 = el del mapa).
    void ponTamano(int puntos)
    {
        for (qint64 id : m_puntos) {
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

    // Pone la escala polar de \a km kilometros: anillos cada 10 km, radiales cada 10 grados
    // y enfasis en uno de cada cinco anillos y de cada tres radiales, todo en UN RangeRings.
    // La separacion se fija en kilometros (aunque se estuviera en millas) y se aleja el
    // mapa para que quepa.
    void escala(int km)
    {
        {
            const QSignalBlocker b1(m_unidades), b2(m_separacion), b3(m_numero),
                b4(m_radiales), b5(m_enfasis), b6(m_mostrar);
            m_mostrar->setChecked(true);
            m_unidades->setCurrentIndex(0);
            m_separacion->setValue(10.0);
            m_numero->setValue(km / 10);
            m_radiales->setCurrentIndex(m_radiales->findData(10));
            m_enfasis->setChecked(true);
        }
        aplicaAnillos();
        m_mapa->setCenter(m_centro);
        m_mapa->setZoom(km >= 600 ? 6 : 7);
    }

    // Mueve el centro de los anillos a \a p (lo que hace el clic con «Elegir centro»).
    void ponCentro(const QGeoCoordinate &p)
    {
        m_centro = p;
        m_lblCentro->setText(formatCoordinate(p, CoordinateFormat::DMS));
        aplicaAnillos();
        aplicaSectores();
    }

    // Rellena la lectura del raton: coordenadas en tres formatos y distancia y
    // marcacion desde el centro de los anillos, con el anillo mas cercano.
    void leeRaton(const QGeoCoordinate &p)
    {
        if (!p.isValid())
            return;
        m_lblDecimal->setText(formatCoordinate(p, CoordinateFormat::Decimal));
        m_lblDms->setText(formatCoordinate(p, CoordinateFormat::DMS));
        m_lblUtm->setText(formatCoordinate(p, CoordinateFormat::UTM));
        const double metros = m_centro.distanceTo(p);
        const int rumbo = qRound(m_centro.azimuthTo(p)) % 360;
        const double paso = separacionMetros();
        const int anillo = qRound(metros / paso);
        m_desdeCentro->setText(tr("%1 · %2°  (anillo %3 a %4)")
                                   .arg(distancia(metros))
                                   .arg(QString::number(rumbo).rightJustified(3, QLatin1Char('0')))
                                   .arg(anillo)
                                   .arg(distancia(anillo * paso)));
    }

private:
    // ¿Se mide en millas nauticas? (si no, en km).
    bool nauticas() const { return m_unidades->currentIndex() == 1; }

    // Separacion entre anillos en metros, leida del panel en sus unidades.
    double separacionMetros() const
    {
        return m_separacion->value() * (nauticas() ? kMetrosPorMilla : 1000.0);
    }

    // Distancia en las unidades elegidas, con un decimal.
    QString distancia(double metros) const
    {
        return nauticas() ? tr("%1 NM").arg(metros / kMetrosPorMilla, 0, 'f', 1)
                          : tr("%1 km").arg(metros / 1000.0, 0, 'f', 1);
    }

    // Vuelve a poner los anillos con los ajustes del panel, o los quita si estan
    // ocultos. Los ajustes viven en el panel, asi que al mostrarlos de nuevo salen igual.
    void aplicaAnillos()
    {
        if (m_anillos >= 0)
            m_mapa->removeRangeMark(m_anillos);
        m_anillos = -1;
        m_mapa->setScaleBarUnits(nauticas() ? ScaleUnits::Nautical : ScaleUnits::Metric);
        m_mapa->setScaleBarVisible(m_barra->isChecked());
        m_mapa->setGraticuleVisible(m_cuadricula->isChecked());
        if (!m_mostrar->isChecked())
            return;
        RangeRings r;
        r.center = m_centro;
        r.spacingMeters = separacionMetros();
        r.count = m_numero->value();
        r.spokeEveryDeg = m_radiales->currentData().toInt();
        r.labels = m_rotulos->isChecked();
        r.color = QColor(0xc6, 0x28, 0x28);       // rojo: se lee sobre la costa y el mar
        r.name = tr("Centro");
        if (m_enfasis->isChecked()) {
            r.majorRingEvery = m_cadaAnillos->value();
            r.majorSpokeEveryDeg = m_cadaGrados->value();
            r.majorColor = QColor(0x5d, 0x0e, 0x0e);     // rojo oscuro
        }
        if (m_colorRadiales->isChecked())
            r.spokeColor = QColor(0x15, 0x65, 0xc0);     // radiales en azul
        m_anillos = m_mapa->addRangeRings(r);
    }

    // Quita todos los puntos con nombre que haya: los que agrega este ejemplo («punto», «sitio»)
    // y los «aerodromo» que dejo guardados otro ejemplo.
    void quitaPuntos()
    {
        for (const char *t : { "punto", "sitio", "aerodromo" })
            m_mapa->clearFeaturesOfType(QString::fromLatin1(t));
        m_puntos.clear();
        m_sitios = -1;
    }

    // Agrega un lugar al mapa: como punto suelto (una entidad por punto) o como una parte nueva
    // de la entidad «Sitios» (una sola entidad con tantas partes como lugares, cada una con su
    // nombre y un color propio de una paleta; se crea al agregar la primera).
    void agregaImpl(const QGeoCoordinate &p, const QString &nombre, bool comoParte)
    {
        if (!comoParte) {
            MapFeature f;
            f.kind = GeometryKind::Point;
            f.layerId = QStringLiteral("puntos");
            f.type = QStringLiteral("punto");
            f.name = nombre;
            f.geometry = { p };
            f.style.lineColor = QColor(0x15, 0x65, 0xc0);
            m_puntos.append(m_mapa->addFeature(f));
        } else {
            static const QColor paleta[] = { QColor(0xc6, 0x28, 0x28), QColor(0x2e, 0x7d, 0x32),
                                             QColor(), QColor(0xef, 0x6c, 0x00) };
            auto f = m_mapa->feature(m_sitios);
            const bool nuevo = !f;
            if (nuevo) {
                f = MapFeature();
                f->kind = GeometryKind::Point;
                f->layerId = QStringLiteral("puntos");
                f->type = QStringLiteral("sitio");
                f->name = tr("Sitios");
                f->style.lineColor = QColor(0x6a, 0x1b, 0x9a);
            }
            PartInfo pi;
            pi.name = nombre;
            pi.colorOverride = paleta[f->parts.size() % 4];
            f->parts.append({ p });
            f->partsInfo.append(pi);
            f->geometry = f->parts.first();
            if (nuevo)
                m_sitios = m_mapa->addFeature(*f);
            else
                m_mapa->updateFeature(*f);
        }
        actualizaConteos();
        statusBar()->showMessage(tr("«%1» agregado en %2").arg(nombre, formatCoordinate(p, CoordinateFormat::DMS)), 6000);
    }

public:
    // Lo mismo que agregaImpl, para quien llama desde fuera de la ventana (--comprobar).
    void agrega(const QGeoCoordinate &p, const QString &nombre, bool comoParte)
    {
        agregaImpl(p, nombre, comoParte);
    }

private:
    // Busca \a texto en el indice de lugares del paquete y llena la lista de resultados.
    void busca(const QString &texto)
    {
        m_resultados->clear();
        if (!m_mapa->hasPlaces()) {
            statusBar()->showMessage(tr("El paquete no trae índice de lugares: usa el clic en el mapa"), 8000);
            return;
        }
        m_lugares = m_mapa->searchPlaces(texto.trimmed(), 30);
        for (const Place &l : m_lugares)
            m_resultados->addItem(QStringLiteral("%1 · %2 · %3").arg(l.name, l.typeLabel(),
                                      formatCoordinate(l.coordinate, CoordinateFormat::Decimal)));
        if (m_lugares.isEmpty())
            statusBar()->showMessage(tr("Sin resultados para «%1»").arg(texto), 5000);
    }

    // Escribe en el panel cuantas entidades hay de cada tipo (y cuales estan ocultas).
    void actualizaConteos()
    {
        if (!m_lblConteos)
            return;
        QStringList l;
        const QMap<QString, int> c = m_mapa->featureTypeCounts();
        for (auto it = c.constBegin(); it != c.constEnd(); ++it)
            l << QStringLiteral("%1: %2").arg(it.key()).arg(it.value());
        m_lblConteos->setText(l.isEmpty() ? tr("(ninguna)") : l.join(QStringLiteral("  ·  ")));
    }

    // Tres objetivos con formulario de tres lineas: identificacion y estado (cada uno de su
    // color) en la primera, y datos de dominio en las otras dos.
    void creaObjetivos()
    {
        struct Dato {
            double lat, lon, rumbo;
            const char *id, *estado, *l2, *l3;
        };
        const Dato datos[] = {
            { 23.40, -82.00, 80.0, "E046", "M1M2", "+42  I   01655", "_S_A     1.2.3.4" },
            { 23.40, -82.80, 160.0, "E051", "EMER", "+38  I   00980", "_S_B     5.6.7.8" },
            { 22.80, -82.00, 300.0, "E077", "DET", "+35  I   00720", "_S_C     9.0.1.2" },
        };
        const QColor colorEstado[] = { QColor(0, 120, 0), QColor(200, 0, 0), QColor(200, 120, 0) };
        int i = 0;
        for (const Dato &d : datos) {
            MapTarget t;
            t.position = QGeoCoordinate(d.lat, d.lon);
            t.headingDeg = d.rumbo;
            t.kind = QStringLiteral("aeronave");
            t.label = QString::fromUtf8(d.id);       // lo que se veria sin formulario por segmentos
            t.labelContent = {
                linea({ { QString::fromUtf8(d.id), QColor(0, 70, 200) },
                        { QString::fromUtf8(d.estado), colorEstado[i] } }),
                linea({ { QString::fromUtf8(d.l2), QColor() } }),
                linea({ { QString::fromUtf8(d.l3), QColor() } }),
            };
            m_objetivos.append(m_mapa->addTarget(t));
            ++i;
        }
    }

    // Construye el panel de la derecha: mapa, anillos, raton y la ayuda, con barra de
    // desplazamiento por si la ventana es baja.
    QWidget *creaPanel()
    {
        auto *panel = new QWidget;
        auto *col = new QVBoxLayout(panel);

        // --- Mapa ----------------------------------------------------------------
        auto *g0 = new QGroupBox(tr("Mapa"), panel);
        auto *f0 = new QFormLayout(g0);
        auto *base = new QComboBox(g0);
        for (const BaseLayerInfo &c : m_mapa->availableBaseLayers())
            base->addItem(c.displayName.isEmpty() ? c.id : c.displayName, c.id);
        base->setCurrentIndex(qMax(0, base->findData(m_mapa->baseLayerId())));
        connect(base, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, base](int i) { m_mapa->setBaseLayerId(base->itemData(i).toString()); });
        m_barra = new QCheckBox(tr("Barra de escala"), g0);
        m_barra->setChecked(true);
        m_cuadricula = new QCheckBox(tr("Cuadrícula de coordenadas"), g0);
        f0->addRow(tr("Capa base"), base);
        f0->addRow(m_barra);
        f0->addRow(m_cuadricula);
        col->addWidget(g0);

        // --- Anillos ---------------------------------------------------------------
        auto *g1 = new QGroupBox(tr("Anillos y radiales"), panel);
        auto *f1 = new QFormLayout(g1);
        m_mostrar = new QCheckBox(tr("Mostrar anillos"), g1);
        m_mostrar->setChecked(true);
        m_lblCentro = new QLabel(formatCoordinate(kHabana, CoordinateFormat::DMS), g1);
        m_lblCentro->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_elegir = new QPushButton(tr("Elegir centro (clic en el mapa)"), g1);
        auto *habana = new QPushButton(tr("Centro en La Habana"), g1);
        auto *ir = new QPushButton(tr("Llevar el mapa al centro"), g1);
        m_unidades = new QComboBox(g1);
        m_unidades->addItems({ tr("Kilómetros"), tr("Millas náuticas") });
        m_separacion = new QDoubleSpinBox(g1);
        m_separacion->setRange(0.5, 100.0);
        m_separacion->setDecimals(1);
        m_separacion->setValue(10.0);
        m_numero = new QSpinBox(g1);
        m_numero->setRange(1, 100);
        m_numero->setValue(10);
        m_radiales = new QComboBox(g1);
        for (int g : kRadiales)
            m_radiales->addItem(g == 0 ? tr("Sin radiales") : tr("Cada %1°").arg(g), g);
        m_radiales->setCurrentIndex(m_radiales->findData(30));
        m_rotulos = new QCheckBox(tr("Rotular distancias y marcaciones"), g1);
        m_rotulos->setChecked(true);
        f1->addRow(m_mostrar);
        f1->addRow(tr("Centro"), m_lblCentro);
        f1->addRow(m_elegir);
        m_colocar = new QPushButton(tr("Agregar un punto con un clic en el mapa"), g1);
        f1->addRow(m_colocar);
        connect(m_colocar, &QPushButton::clicked, this, [this] {
            m_colocando = true;
            m_mapa->setActiveTool(MapTool::PickPoint);
            m_colocar->setText(tr("Haz clic donde va el punto... (Esc cancela)"));
        });
        f1->addRow(habana);
        f1->addRow(ir);
        f1->addRow(tr("Unidades"), m_unidades);
        f1->addRow(tr("Separación"), m_separacion);
        f1->addRow(tr("Anillos"), m_numero);
        f1->addRow(tr("Radiales"), m_radiales);
        f1->addRow(m_rotulos);
        col->addWidget(g1);

        // --- Escala polar -------------------------------------------------------------
        auto *gp = new QGroupBox(tr("Escala polar y énfasis"), panel);
        auto *fp = new QFormLayout(gp);
        auto *e300 = new QPushButton(tr("Escala de 300 km (30 anillos)"), gp);
        auto *e600 = new QPushButton(tr("Escala de 600 km (60 anillos)"), gp);
        m_enfasis = new QCheckBox(tr("Énfasis: anillo y radial «mayor» en rojo oscuro"), gp);
        m_cadaAnillos = new QSpinBox(gp);
        m_cadaAnillos->setRange(1, 50);
        m_cadaAnillos->setValue(5);
        m_cadaGrados = new QSpinBox(gp);
        m_cadaGrados->setRange(5, 180);
        m_cadaGrados->setValue(30);
        m_colorRadiales = new QCheckBox(tr("Radiales de otro color (azul)"), gp);
        fp->addRow(e300);
        fp->addRow(e600);
        fp->addRow(m_enfasis);
        fp->addRow(tr("Un anillo de cada"), m_cadaAnillos);
        fp->addRow(tr("Un radial cada (°)"), m_cadaGrados);
        fp->addRow(m_colorRadiales);
        col->addWidget(gp);

        // --- Rotulos de entidades ------------------------------------------------------
        auto *gr = new QGroupBox(tr("Rótulos de los puntos agregados"), panel);
        auto *fr = new QFormLayout(gr);
        auto *posicion = new QComboBox(gr);
        for (const Posicion &po : kPosiciones)
            posicion->addItem(tr(po.nombre));
        auto *tamano = new QSpinBox(gr);
        tamano->setRange(0, 40);
        tamano->setSpecialValueText(tr("0 (el del mapa)"));
        fr->addRow(tr("Lado del anclaje"), posicion);
        fr->addRow(tr("Fuente (pt)"), tamano);
        connect(posicion, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int i) { ponAncla(kPosiciones[i].ancla); });
        connect(tamano, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) { ponTamano(v); });
        col->addWidget(gr);

        // --- Buscador de lugares: los puntos los agrega el usuario -------------------------
        auto *gb = new QGroupBox(tr("Agregar puntos (buscador de lugares)"), panel);
        auto *vb = new QVBoxLayout(gb);
        auto *caja = new QLineEdit(gb);
        caja->setPlaceholderText(tr("Nombre de un lugar y Enter"));
        m_resultados = new QListWidget(gb);
        m_resultados->setMaximumHeight(150);
        auto *aPunto = new QPushButton(tr("Agregar como punto"), gb);
        auto *aParte = new QPushButton(tr("Agregar como parte de «Sitios» (una entidad)"), gb);
        auto *quitaTodo = new QPushButton(tr("Quitar todos los puntos y sitios"), gb);
        vb->addWidget(caja);
        vb->addWidget(m_resultados);
        vb->addWidget(aPunto);
        vb->addWidget(aParte);
        vb->addWidget(quitaTodo);
        connect(caja, &QLineEdit::returnPressed, this, [this, caja] { busca(caja->text()); });
        const auto agregaElegido = [this](bool comoParte) {
            const int i = m_resultados->currentRow();
            if (i >= 0 && i < m_lugares.size())
                agregaImpl(m_lugares.at(i).coordinate, m_lugares.at(i).name, comoParte);
        };
        connect(aPunto, &QPushButton::clicked, this, [agregaElegido] { agregaElegido(false); });
        connect(aParte, &QPushButton::clicked, this, [agregaElegido] { agregaElegido(true); });
        connect(m_resultados, &QListWidget::itemDoubleClicked, this,
                [agregaElegido](QListWidgetItem *) { agregaElegido(false); });
        connect(quitaTodo, &QPushButton::clicked, this, [this] {
            quitaPuntos();
            actualizaConteos();
        });
        col->addWidget(gb);

        // --- Circulos, sectores y entidades por tipo -------------------------------------
        auto *gs = new QGroupBox(tr("Sectores, tipos y partes"), panel);
        auto *fs = new QFormLayout(gs);
        m_sectores = new QCheckBox(tr("Círculo y sectores alrededor del centro"), gs);
        m_sectores->setChecked(true);
        m_lblConteos = new QLabel(gs);
        m_lblConteos->setWordWrap(true);
        auto *tSector = new QCheckBox(tr("Ver sectores"), gs);
        auto *tSitio = new QCheckBox(tr("Ver sitios (una entidad, tres partes)"), gs);
        auto *tPunto = new QCheckBox(tr("Ver puntos agregados"), gs);
        for (QCheckBox *c : { tSector, tSitio, tPunto })
            c->setChecked(true);
        auto *borraSect = new QPushButton(tr("Borrar los sectores (por tipo)"), gs);
        m_lblElemento = new QLabel(QStringLiteral("—"), gs);
        m_lblElemento->setWordWrap(true);
        fs->addRow(m_sectores);
        fs->addRow(tr("Entidades por tipo"), m_lblConteos);
        fs->addRow(tSector);
        fs->addRow(tSitio);
        fs->addRow(tPunto);
        fs->addRow(borraSect);
        fs->addRow(tr("Último clic en entidad"), m_lblElemento);
        connect(m_sectores, &QCheckBox::toggled, this, [this] { aplicaSectores(); });
        connect(tSector, &QCheckBox::toggled, this,
                [this](bool v) { m_mapa->setFeatureTypeVisible(QStringLiteral("sector"), v); });
        connect(tSitio, &QCheckBox::toggled, this,
                [this](bool v) { m_mapa->setFeatureTypeVisible(QStringLiteral("sitio"), v); });
        connect(tPunto, &QCheckBox::toggled, this,
                [this](bool v) { m_mapa->setFeatureTypeVisible(QStringLiteral("punto"), v); });
        connect(borraSect, &QPushButton::clicked, this, [this] {
            const QSignalBlocker b(m_sectores);
            m_sectores->setChecked(false);
            aplicaSectores();
        });
        col->addWidget(gs);
        actualizaConteos();

        // --- Formulario de los objetivos -------------------------------------------------
        auto *gf = new QGroupBox(tr("Formulario de objetivos (arrastrable)"), panel);
        auto *ff = new QFormLayout(gf);
        m_colgado = new QCheckBox(tr("Colgado de la esquina más cercana"), gf);
        m_mono = new QCheckBox(tr("Fuente monoespaciada"), gf);
        m_columnas = new QCheckBox(tr("Columnas 8 + 8 en la primera línea"), gf);
        m_maxChars = new QSpinBox(gf);
        m_maxChars->setRange(0, 40);
        m_maxChars->setSpecialValueText(tr("0 (sin límite)"));
        m_offX = new QSpinBox(gf);
        m_offY = new QSpinBox(gf);
        for (QSpinBox *sp : { m_offX, m_offY })
            sp->setRange(-300, 300);
        m_offX->setValue(-31);
        m_offY->setValue(-31);
        auto *colorSel = new QPushButton(tr("Color de selección..."), gf);
        auto *quitar = new QPushButton(tr("Quitar offsets propios"), gf);
        ff->addRow(m_colgado);
        ff->addRow(m_mono);
        ff->addRow(m_columnas);
        ff->addRow(tr("Ancho (caracteres)"), m_maxChars);
        ff->addRow(tr("Offset por defecto X"), m_offX);
        ff->addRow(tr("Offset por defecto Y"), m_offY);
        ff->addRow(colorSel);
        ff->addRow(quitar);
        for (QCheckBox *c : { m_colgado, m_mono, m_columnas })
            connect(c, &QCheckBox::toggled, this, [this] { aplicaFormato(); });
        for (QSpinBox *sp : { m_maxChars, m_offX, m_offY })
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this] { aplicaFormato(); });
        connect(colorSel, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_mapa->targetLabelSelectionColor(), this);
            if (c.isValid())
                m_mapa->setTargetLabelSelectionColor(c);
        });
        connect(quitar, &QPushButton::clicked, this, [this] {
            for (qint64 id : m_objetivos)
                m_mapa->clearTargetLabelOffset(id);
        });
        col->addWidget(gf);
        connect(e300, &QPushButton::clicked, this, [this] { escala(300); });
        connect(e600, &QPushButton::clicked, this, [this] { escala(600); });

        // --- Raton -------------------------------------------------------------------
        auto *g2 = new QGroupBox(tr("Ratón"), panel);
        auto *f2 = new QFormLayout(g2);
        m_lblDecimal = new QLabel(QStringLiteral("—"), g2);
        m_lblDms = new QLabel(QStringLiteral("—"), g2);
        m_lblUtm = new QLabel(QStringLiteral("—"), g2);
        m_desdeCentro = new QLabel(QStringLiteral("—"), g2);
        m_desdeCentro->setWordWrap(true);
        QFont negrita = m_desdeCentro->font();
        negrita.setBold(true);
        m_desdeCentro->setFont(negrita);
        f2->addRow(tr("Decimal"), m_lblDecimal);
        f2->addRow(tr("GMS"), m_lblDms);
        f2->addRow(tr("UTM"), m_lblUtm);
        f2->addRow(tr("Desde el centro"), m_desdeCentro);
        col->addWidget(g2);

        // --- Que ver y que probar ------------------------------------------------------
        auto *g3 = new QGroupBox(tr("Qué ver y qué probar"), panel);
        auto *v3 = new QVBoxLayout(g3);
        auto *ayuda = new QLabel(textoAyuda(), g3);
        ayuda->setWordWrap(true);
        ayuda->setTextFormat(Qt::RichText);
        v3->addWidget(ayuda);
        col->addWidget(g3);
        col->addStretch(1);

        // Las conexiones, al final: aplicaAnillos lee todos los controles.
        for (QCheckBox *c : { m_mostrar, m_rotulos, m_barra, m_cuadricula, m_enfasis,
                              m_colorRadiales })
            connect(c, &QCheckBox::toggled, this, [this] { aplicaAnillos(); });
        for (QSpinBox *sp : { m_cadaAnillos, m_cadaGrados })
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this] { aplicaAnillos(); });
        for (QComboBox *c : { m_unidades, m_radiales })
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this] { aplicaAnillos(); });
        connect(m_separacion, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this] { aplicaAnillos(); });
        connect(m_numero, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this] { aplicaAnillos(); });
        connect(m_elegir, &QPushButton::clicked, this, [this] {
            m_mapa->setActiveTool(MapTool::PickPoint);
            m_elegir->setText(tr("Haz clic en el mapa... (Esc cancela)"));
        });
        connect(habana, &QPushButton::clicked, this, [this] { ponCentro(kHabana); });
        connect(ir, &QPushButton::clicked, this, [this] { m_mapa->setCenter(m_centro); });

        auto *scroll = new QScrollArea(this);
        scroll->setWidget(panel);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFixedWidth(400);
        return scroll;
    }

    MapWidget *m_mapa = nullptr;
    qint64 m_anillos = -1;
    QGeoCoordinate m_centro;
    QCheckBox *m_mostrar = nullptr;
    QCheckBox *m_rotulos = nullptr;
    QCheckBox *m_barra = nullptr;
    QCheckBox *m_cuadricula = nullptr;
    QLabel *m_lblCentro = nullptr;
    QPushButton *m_elegir = nullptr;
    QPushButton *m_colocar = nullptr;
    bool m_colocando = false;       // el proximo clic agrega un punto
    QListWidget *m_resultados = nullptr;
    QVector<Place> m_lugares;       // lo que muestra la lista de resultados
    QComboBox *m_unidades = nullptr;
    QDoubleSpinBox *m_separacion = nullptr;
    QSpinBox *m_numero = nullptr;
    QComboBox *m_radiales = nullptr;
    QLabel *m_lblDecimal = nullptr;
    QLabel *m_lblDms = nullptr;
    QLabel *m_lblUtm = nullptr;
    QLabel *m_desdeCentro = nullptr;
    QCheckBox *m_enfasis = nullptr;
    QSpinBox *m_cadaAnillos = nullptr;
    QSpinBox *m_cadaGrados = nullptr;
    QCheckBox *m_colorRadiales = nullptr;
    QVector<qint64> m_puntos;
    qint64 m_sitios = -1;
    QCheckBox *m_sectores = nullptr;
    QLabel *m_lblConteos = nullptr;
    QLabel *m_lblElemento = nullptr;
    QVector<qint64> m_objetivos;
    QCheckBox *m_colgado = nullptr;
    QCheckBox *m_mono = nullptr;
    QCheckBox *m_columnas = nullptr;
    QSpinBox *m_maxChars = nullptr;
    QSpinBox *m_offX = nullptr;
    QSpinBox *m_offY = nullptr;
};

namespace {

// Deja que la aplicacion procese eventos unos milisegundos (los objetivos se repintan con un
// temporizador) y fuerza un repintado completo con grab().
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

// Comprueba sin ventana lo que el panel permite tocar: capa inicial y sin hibrido,
// anillos con sus ajustes, cambio de centro, ocultar y mostrar, unidades y la lectura
// de distancia y marcacion del raton. Sale con 0 si todo esta bien.
int comprobar(Ventana &v)
{
    int fallos = 0;
    MapWidget *m = v.mapa();
    if (!m->isReady()) {
        QTextStream(stderr) << "FALLO: el mapa no esta listo: " << m->lastError() << "\n";
        return 1;
    }
    // 1. Solo el mapa: sin overlay hibrido, y la capa de costas si el paquete la tiene.
    exige(!m->hasVectorOverlay(), "no deberia haber nada hibrido encima", &fallos);
    bool hayCostas = false;
    for (const BaseLayerInfo &c : m->availableBaseLayers())
        hayCostas = hayCostas || c.id == kCapaInicial;
    if (hayCostas)
        exige(m->baseLayerId() == kCapaInicial, "deberia arrancar con la capa de costas", &fallos);

    // 2. Anillos iniciales: 10 cada 10 km con radiales cada 30 grados y rotulados.
    auto r = m->rangeRings(v.anillos());
    exige(r.has_value(), "no hay anillos al arrancar", &fallos);
    if (r) {
        exige(r->count == 10 && qAbs(r->spacingMeters - 10000.0) < 0.5,
              "deberian ser 10 anillos cada 10 km", &fallos);
        exige(r->spokeEveryDeg == 30 && r->labels, "faltan los radiales o los rotulos", &fallos);
    }

    // 3. Otro centro (lo que hace el clic con «Elegir centro»).
    const QGeoCoordinate otro(22.40, -83.70);
    v.ponCentro(otro);
    r = m->rangeRings(v.anillos());
    exige(r && r->center.distanceTo(otro) < 1.0, "los anillos no se movieron al nuevo centro", &fallos);

    // 4. La lectura del raton: un punto a 25 km al 045 del centro, cerca del anillo 2 o 3.
    v.leeRaton(otro.atDistanceAndAzimuth(25000.0, 45.0));
    exige(v.lecturaRaton().startsWith(QStringLiteral("25.0 km · 045°")),
          "la distancia o la marcacion del raton no cuadran", &fallos);
    // A 32 km, el anillo mas cercano es el 3.
    v.leeRaton(otro.atDistanceAndAzimuth(32000.0, 200.0));
    exige(v.lecturaRaton().startsWith(QStringLiteral("32.0 km · 200°  (anillo 3 a 30.0 km)")),
          "el anillo mas cercano no cuadra", &fallos);

    // 5. Ocultar y volver a mostrar: se quitan y vuelven igual, en el mismo centro.
    v.casillaMostrar()->setChecked(false);
    exige(!m->rangeRings(v.anillos()).has_value(), "ocultar no quito los anillos", &fallos);
    v.casillaMostrar()->setChecked(true);
    r = m->rangeRings(v.anillos());
    exige(r && r->center.distanceTo(otro) < 1.0 && r->count == 10,
          "al mostrarlos de nuevo no salen igual", &fallos);

    // 6. Millas nauticas: la separacion de 10 pasa a 10 NM y la barra de escala tambien.
    v.unidades()->setCurrentIndex(1);
    r = m->rangeRings(v.anillos());
    exige(r && qAbs(r->spacingMeters - 18520.0) < 0.5, "10 NM deberian ser 18 520 m", &fallos);
    exige(m->scaleBarUnits() == ScaleUnits::Nautical, "la barra no paso a NM", &fallos);
    v.leeRaton(otro.atDistanceAndAzimuth(20.0 * kMetrosPorMilla, 90.0));
    exige(v.lecturaRaton().startsWith(QStringLiteral("20.0 NM · 090°  (anillo 2 a 20.0 NM)")),
          "la lectura en NM no cuadra", &fallos);

    // 7. Escala polar de 600 km: 60 anillos y radiales cada 10 grados en UN solo RangeRings,
    //    con enfasis (el tope de anillos ya no recorta a 50) y, aunque se estuviera en NM,
    //    la separacion vuelve a 10 km.
    v.escala(600);
    r = m->rangeRings(v.anillos());
    exige(r.has_value(), "no hay anillos tras la escala de 600 km", &fallos);
    if (r) {
        exige(r->count == 60 && qAbs(r->spacingMeters - 10000.0) < 0.5,
              "la escala de 600 km deberia ser 60 anillos de 10 km", &fallos);
        exige(r->spokeEveryDeg == 10, "los radiales deberian ser cada 10 grados", &fallos);
        exige(r->majorRingEvery == 5 && r->majorSpokeEveryDeg == 30 && r->majorColor.isValid(),
              "falta el enfasis de la escala polar", &fallos);
    }
    v.escala(300);
    r = m->rangeRings(v.anillos());
    exige(r && r->count == 30, "la escala de 300 km deberia ser 30 anillos", &fallos);

    // 8. Rotulos de entidades: el lado del anclaje y la fuente cambian la caja en pixeles.
    v.agrega(kHabana, QStringLiteral("Punto de prueba"), false);
    const qint64 id = v.primerPunto();
    const QRect clasico = m->featureLabelRect(id);
    exige(clasico.isValid(), "el punto de ejemplo no tiene rotulo", &fallos);
    v.ponAncla(LabelAnchor::Top);
    const QRect encima = m->featureLabelRect(id);
    exige(qAbs(encima.width() - clasico.width()) <= 1 && qAbs(encima.height() - clasico.height()) <= 1,
          "el ancla no debe cambiar el tamano", &fallos);
    exige(encima.bottom() < clasico.bottom(), "Top deberia dejar la caja sobre el punto", &fallos);
    v.ponTamano(24);
    exige(m->featureLabelRect(id).height() > encima.height(),
          "la fuente 24 deberia crecer la caja", &fallos);
    exige(m->feature(id)->style.labelFontSize == 24, "el tamano de fuente no se guardo", &fallos);

    // 9. Formulario de objetivos. Con la ventana oculta la capa de objetivos no llega a
    //    pintarse: se muestra (con QT_QPA_PLATFORM=offscreen no abre nada de verdad). El
    //    objetivo seleccionado siempre muestra su rotulo (salta el declutter, que a poco zoom
    //    dibuja uno solo por celda de pantalla), asi que se comprueba ese.
    v.show();
    esperaRepintado(m);
    const qint64 t0 = v.objetivos().value(0, -1);
    m->setSelectedTarget(t0);
    esperaRepintado(m);
    const QRect form = m->targetLabelRect(t0);
    exige(form.isValid(), "el formulario del objetivo no se dibujo", &fallos);
    if (form.isValid())
        exige(form.height() >= 3 * 10, "el formulario deberia tener tres lineas", &fallos);

    // El offset por defecto lo ven los objetivos que no fijaron el suyo; el propio manda.
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

    // Colgado y ancho fijo: con 16 columnas todos los formularios miden lo mismo.
    TargetLabelFormat fmt;
    fmt.placement = TargetLabelPlacement::Hanging;
    fmt.monospace = true;
    fmt.maxChars = 16;
    fmt.segmentChars = { 8, 8 };
    m->setTargetLabelFormat(fmt);
    m->setDefaultTargetLabelOffset(QPointF(-60.0, -40.0));    // arriba-izquierda
    esperaRepintado(m);
    const QRect colgada = m->targetLabelRect(t0);
    exige(colgada.isValid(), "no se dibujo el formulario colgado", &fallos);
    for (qint64 otroId : v.objetivos()) {
        const QRect rr = m->targetLabelRect(otroId);
        if (rr.isValid())
            exige(qAbs(rr.width() - colgada.width()) <= 1,
                  "con 16 columnas fijas todos los formularios miden lo mismo", &fallos);
    }

    // 10. Circulo y sectores: un lote de cuatro entidades geodesicas alrededor del centro; se
    //     quitan por tipo y vuelven al marcar la casilla.
    v.agrega(QGeoCoordinate(23.0379, -82.3923), QStringLiteral("Sitio Alfa"), true);
    v.agrega(QGeoCoordinate(22.9592, -82.4092), QStringLiteral("Sitio Bravo"), true);
    v.agrega(QGeoCoordinate(23.0321, -82.5166), QStringLiteral("Sitio Charly"), true);
    const qint64 sitios = v.sitios();
    exige(m->featureTypeCounts().value(QStringLiteral("sector")) == 4,
          "deberia haber cuatro sectores al arrancar", &fallos);
    v.casillaSectores()->setChecked(false);
    exige(m->featureTypeCounts().value(QStringLiteral("sector")) == 0,
          "borrar por tipo no quito los sectores", &fallos);
    v.casillaSectores()->setChecked(true);
    exige(m->featureTypeCounts().value(QStringLiteral("sector")) == 4,
          "los sectores no volvieron", &fallos);
    m->setFeatureTypeVisible(QStringLiteral("sector"), false);
    exige(!m->isFeatureTypeVisible(QStringLiteral("sector")) &&
              m->featureTypeCounts().value(QStringLiteral("sector")) == 4,
          "ocultar un tipo no debe borrarlo", &fallos);
    m->setFeatureTypeVisible(QStringLiteral("sector"), true);

    // 11. Una entidad con tres partes: cada punto devuelve la misma entidad y su parte.
    const auto sitio = m->feature(sitios);
    exige(sitio && sitio->parts.size() == 3 && sitio->partsInfo.size() == 3,
          "los sitios deberian ser una entidad de tres partes", &fallos);
    if (sitio && sitio->parts.size() == 3) {
        // Sin los sectores (su relleno taparia el punto). Con el paquete minimo el zoom no pasa
        // de 2 y el centro no se puede llevar al sitio, asi que se rastrea la vista con
        // hitTest hasta dar con la entidad; la separacion exacta de partes la prueba tst_mapwidget.
        v.casillaSectores()->setChecked(false);
        m->setCenter(sitio->parts.at(1).first());
        esperaRepintado(m);
        MapHit hit;
        for (int y = 0; y < m->height() && hit.featureId != sitios; y += 3)
            for (int x = 0; x < m->width() && hit.featureId != sitios; x += 3)
                hit = m->hitTest(QPoint(x, y));
        exige(hit.featureId == sitios && hit.subIndex >= 0 && hit.subIndex < 3,
              "el clic en un sitio deberia dar la entidad y una de sus tres partes", &fallos);
        v.leeElemento(hit);
        exige(v.lecturaElemento().contains(QStringLiteral("Sitio ")),
              "la lectura no nombra la parte", &fallos);
        exige(m->featurePartLabelRect(sitios, 0).isValid() || m->featurePartLabelRect(sitios, 1).isValid(),
              "las partes con nombre deberian tener rotulo propio", &fallos);
    }

    if (fallos == 0)
        QTextStream(stdout) << "laboratorio_anillos --comprobar: todo correcto\n";
    return fallos == 0 ? 0 : 1;
}

} // namespace

// Arranque: carga el paquete de datos (por defecto <exe>/datos) y abre la ventana, o
// comprueba sin ventana (--comprobar).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("laboratorio_anillos"));

    QStringList posicionales;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith(QLatin1String("--")))
            posicionales << a;
    const QString datos = posicionales.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/datos")
        : posicionales.first();

    const bool soloComprobar = app.arguments().contains(QStringLiteral("--comprobar"));
    Ventana ventana(datos, !soloComprobar);
    if (soloComprobar)
        return comprobar(ventana);
    ventana.show();

    // --captura=imagen.png: la ventana con la escala de 600 km, el formulario colgado y el raton
    // «puesto» sobre el tercer anillo al 060, y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(QStringLiteral("--captura=").size()));
            QTimer::singleShot(2500, &app, [&ventana, &app, destino] {
                TargetLabelFormat fmt;
                fmt.placement = TargetLabelPlacement::Hanging;
                fmt.monospace = true;
                fmt.maxChars = 16;
                fmt.segmentChars = { 8, 8 };
                ventana.mapa()->setTargetLabelFormat(fmt);
                ventana.mapa()->setDefaultTargetLabelOffset(QPointF(-31.0, -31.0));
                ventana.ponAncla(LabelAnchor::Top);
                ventana.escala(600);
                ventana.leeRaton(kHabana.atDistanceAndAzimuth(30000.0, 60.0));
                ventana.grab().save(destino);
                app.quit();
            });
            break;
        }
    }
    return app.exec();
}
