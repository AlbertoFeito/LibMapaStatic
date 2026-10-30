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
#include <cmath>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QHash>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
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

// Estados de un objetivo y el color de su segundo segmento (informacion operativa: el color
// dice si esta detectado, en seguimiento o en emergencia).
struct Estado {
    const char *nombre;
    QRgb color;
};
const Estado kEstados[] = {
    {"DET", 0xffef6c00}, {"M1M2", 0xff2e7d32}, {"EMER", 0xffc62828}, {"SIN", 0xff757575},
};

// Color asociado a un estado (gris si no se conoce).
QColor colorEstado(const QString &estado)
{
    for (const Estado &e : kEstados)
        if (estado == QLatin1String(e.nombre))
            return QColor::fromRgb(e.color);
    return QColor(0x75, 0x75, 0x75);
}

// Ajustes de los anillos de UN origen (cada punto tiene los suyos).
struct AjustesAnillos {
    bool mostrar = true;
    double separacion = 10.0;      // en las unidades elegidas (km o NM)
    int anillos = 10;
    int radiales = 30;             // grados entre radiales; 0 = sin radiales
    bool rotulos = true;
    bool enfasis = false;
    int cadaAnillos = 5;
    int cadaGrados = 30;
    bool radialesAzules = false;
    int paleta = 0;                // 0 = rojo, 1 = grises del Anexo C
};

// Un ORIGEN: un punto del mapa con sus anillos y sus sectores. Los anillos y los sectores se
// miden siempre desde un punto; puede haber varios puntos y cada uno lleva los suyos.
struct Origen {
    qint64 punto = -1;             // la entidad punto
    QString nombre;
    QGeoCoordinate pos;
    qint64 anillos = -1;           // el RangeRings dibujado, o -1
    AjustesAnillos aj;
    QVector<qint64> sectores;      // entidades Circle / Arc con arcSector
    QSet<qint64> ocultos;          // sectores que el usuario oculto
};

// Pide al usuario el radio y, si no es un circulo entero, los azimuts de un sector. Devuelve
// false si cancela.
bool pideSector(QWidget *padre, bool circulo, const QString &titulo, double *radioKm, int *ini,
                int *fin)
{
    QDialog d(padre);
    d.setWindowTitle(titulo);
    auto *f = new QFormLayout(&d);
    auto *radio = new QDoubleSpinBox(&d);
    radio->setRange(0.1, 2000.0);
    radio->setSuffix(QObject::tr(" km"));
    radio->setValue(*radioKm);
    auto *a = new QSpinBox(&d);
    auto *b = new QSpinBox(&d);
    for (QSpinBox *sp : { a, b }) {
        sp->setRange(0, 359);
        sp->setSuffix(QStringLiteral("°"));
    }
    a->setValue(*ini);
    b->setValue(*fin);
    f->addRow(QObject::tr("Radio"), radio);
    if (!circulo) {
        f->addRow(QObject::tr("Desde (azimut)"), a);
        f->addRow(QObject::tr("Hasta (azimut)"), b);
    }
    auto *bt = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bt);
    QObject::connect(bt, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bt, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return false;
    *radioKm = radio->value();
    *ini = a->value();
    *fin = b->value();
    return true;
}

// Una linea del formulario de un objetivo con sus segmentos (texto y color).
TargetLabelLine linea(std::initializer_list<TargetLabelSegment> segmentos)
{
    TargetLabelLine l;
    for (const TargetLabelSegment &s : segmentos)
        l.segments.append(s);
    return l;
}

// Texto con la ayuda del panel: para que sirve cada pestana y que hay que comprobar en ella.
QString textoAyuda()
{
    return QObject::tr(
        "<b>Punto de origen</b> · los anillos y los sectores se miden siempre desde un punto"
        "<ul><li>Elige el punto arriba (o tócalo en el mapa). Cada punto tiene <i>sus</i> anillos y "
        "<i>sus</i> sectores; se ven todos a la vez; «solo el elegido» oculta los de los demás.</li>"
        "<li>Agrega más puntos en la pestaña Puntos y ponles anillos o sectores propios.</li>"
        "<li><b>Clic derecho:</b> sobre un punto, sus anillos y sus sectores; sobre un sector, "
        "cambiarlo, ocultarlo o quitarlo; sobre un objetivo, su estado; sobre un lugar vacío, "
        "agregar un punto o un objetivo allí.</li></ul>"
        "<b>Anillos</b> · medir distancia y marcación desde un punto"
        "<ul><li>Pasa el ratón por un anillo: «Desde el centro» debe dar su rótulo (10 km, "
        "20 km...); compáralo con la barra de escala.</li>"
        "<li>Pasa el ratón por un radial: la marcación debe ser la de su rótulo.</li>"
        "<li>«Elegir centro» y un clic mueve los anillos; Esc o clic derecho cancela.</li>"
        "<li>«Escala de 300 km» y «de 600 km» ponen 30 o 60 anillos de 10 km y radiales cada "
        "10° en un solo objeto, con uno de cada cinco anillos y uno de cada tres radiales más "
        "oscuro. Prueba también los grises del Anexo C y los radiales de otro color.</li>"
        "<li>En millas náuticas cambian a la vez rótulos, barra y lectura del ratón.</li></ul>"
        "<b>Sectores</b> · señalar un área donde concentrarse"
        "<ul><li>Cada sector es una cuña rellena entre el centro, los dos radios y el arco "
        "(el de 330° a 30° cruza el norte). La casilla lo oculta sin borrarlo; «Cambiar» le pone "
        "otro radio o azimut; «Eliminar» quita solo ese.</li>"
        "<li>La lista dice el área y cuántos objetivos caen dentro; con «Simular movimiento» "
        "(pestaña Objetivos) el recuento cambia solo.</li>"
        "<li>Siguen al centro de los anillos. El círculo lleva su rótulo en el borde norte.</li></ul>"
        "<b>Puntos</b> · muchos elementos con un mismo icono, cada nombre en su lado"
        "<ul><li>Busca un lugar y agrégalo como punto suelto o como una <i>parte</i> de la entidad "
        "«Sitios» (una sola entidad con nombre y color por parte); también con un clic.</li>"
        "<li>Elige uno de la lista (o tócalo en el mapa) y cambia el lado de su rótulo, la fuente "
        "y el color según su estado (Activo, Inactivo, Alarma). El panel inferior dice qué parte "
        "tocaste.</li></ul>"
        "<b>Objetivos</b> · leer varios a la vez"
        "<ul><li>El color del segundo segmento es el estado (DET, M1M2, EMER): cámbialo y el "
        "rótulo cambia sin perder la traza. «Agregar 3 objetivos pegados» muestra cómo se pisan "
        "los formularios; arrástralos (soltar fuera del mapa cancela).</li>"
        "<li>Colgado de la esquina, fuente monoespaciada, ancho, columnas 8 + 8, offset por "
        "defecto y color de selección son del aspecto común.</li></ul>"
        "<b>Capas</b> · filtrar y borrar en bloque"
        "<ul><li>Cada capa con sus tipos y cuántas entidades hay. Oculta un tipo, muestra solo "
        "uno («solo los aeródromos internos») o bórralo de una vez.</li></ul>");
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
        m_soloElegido = new QCheckBox(tr("Ver solo el punto elegido (ocultar los anillos y sectores de los demás)"), this);
        m_soloElegido->setChecked(false);       // por defecto se ven todos a la vez
        // El mapa recarga lo que se dejo guardado en otras sesiones (y en otros ejemplos, como los
        // «aerodromo» de laboratorio_rotulos): se parte limpio, los puntos los agrega el usuario.
        quitaPuntos();
        m_mapa->addFeatureLayer(QStringLiteral("puntos"), tr("Puntos"), 50);
        m_timerSim.setInterval(1000);
        connect(&m_timerSim, &QTimer::timeout, this, [this] { muevePasoObjetivos(); });
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
            if (m_modoClic == 1) {                // el clic agrega un punto
                m_modoClic = 0;
                m_colocar->setText(tr("Agregar un punto con un clic en el mapa"));
                agrega(p, tr("Punto %1").arg(m_puntos.size() + 1), false);
                return;
            }
            if (m_modoClic == 2) {                // el clic agrega un objetivo
                m_modoClic = 0;
                m_agregaObjetivo->setText(tr("Agregar un objetivo con un clic en el mapa"));
                agregaObjetivo(p, QString(), 90.0, QStringLiteral("DET"));
                return;
            }
            m_elegir->setText(tr("Mover el origen elegido (clic en el mapa)"));
            ponCentro(p);
            statusBar()->showMessage(tr("Origen movido a %1")
                                         .arg(formatCoordinate(p, CoordinateFormat::DMS)), 6000);
        });
        connect(m_mapa, &MapWidget::pickCancelled, this, [this] {
            m_modoClic = 0;
            m_colocar->setText(tr("Agregar un punto con un clic en el mapa"));
            m_agregaObjetivo->setText(tr("Agregar un objetivo con un clic en el mapa"));
            m_elegir->setText(tr("Mover el origen elegido (clic en el mapa)"));
            statusBar()->showMessage(tr("Acción cancelada"), 4000);
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
        connect(m_mapa, &MapWidget::contextMenuRequested, this,
                [this](const MapHit &hit, const QPoint &global) { menuContextual(hit, global); });
        connect(m_mapa, &MapWidget::targetClicked, this, [this](qint64 id, const QGeoCoordinate &) {
            seleccionaObjetivo(id);
        });
        connect(m_soloElegido, &QCheckBox::toggled, this, [this] { aplicaVisibilidad(); });
        aplicaFormato();
        // El primer origen: La Habana, con anillos y los cuatro sectores de ejemplo.
        agregaOrigen(kHabana, tr("Centro"));
        aplicaAnillos();
        restableceSectores();
        statusBar()->showMessage(tr("Capa base: %1").arg(m_mapa->baseLayerId()));
    }

    MapWidget *mapa() const { return m_mapa; }
    qint64 anillos() const { return m_cur >= 0 ? m_origenes[m_cur].anillos : -1; }
    int numOrigenes() const { return int(m_origenes.size()); }
    int origenActual() const { return m_cur; }
    qint64 anillosDeOrigen(int o) const { return m_origenes.value(o).anillos; }
    qint64 sectorDeOrigen(int o, int i) const { return m_origenes.value(o).sectores.value(i, -1); }
    qint64 puntoDeOrigen(int i) const { return m_origenes.value(i).punto; }
    QString lecturaRaton() const { return m_desdeCentro ? m_desdeCentro->text() : QString(); }
    QCheckBox *casillaMostrar() const { return m_mostrar; }
    QCheckBox *casillaSolo() const { return m_soloElegido; }
    QComboBox *unidades() const { return m_unidades; }
    qint64 primerPunto() const { return m_puntos.value(0, -1); }
    QVector<qint64> objetivos() const { return m_objetivos; }
    qint64 sitios() const { return m_sitios; }
    // Cambia de pestana del panel (la usa --captura=... --pestana=N).
    void ponPestana(int i) { if (m_pestanas) m_pestanas->setCurrentIndex(i); }
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
        seleccionaElemento(hit.featureId, hit.subIndex);
        for (int i = 0; i < m_origenes.size(); ++i)       // tocar un punto (o un sector suyo) lo elige
            if (m_origenes[i].punto == hit.featureId || m_origenes[i].sectores.contains(hit.featureId)) {
                seleccionaOrigen(i);
                break;
            }
    }

public:
    // Crea un circulo (\a sector falso y \a fin ignorado) o un sector alrededor del centro y lo
    // anade a la lista; devuelve su id. Un sector es una cuna RELLENA (area a vigilar).
    qint64 creaSector(bool circulo, double radioKm, double ini, double fin, const QColor &c,
                      const QString &nombre)
    {
        MapFeature f;
        f.kind = circulo ? GeometryKind::Circle : GeometryKind::Arc;
        f.layerId = QStringLiteral("sectores");
        f.type = QStringLiteral("sector");
        f.name = nombre;
        f.geometry = { m_centro };
        f.radiusMeters = radioKm * 1000.0;
        f.arcStartDeg = ini;
        f.arcEndDeg = fin;
        f.arcSector = !circulo;
        f.style.lineColor = c;
        f.style.lineWidth = 2.0;
        f.style.fillColor = QColor(c.red(), c.green(), c.blue(), 70);
        f.visible = !m_soloElegido->isChecked() || m_cur >= 0;
        const qint64 id = m_mapa->addFeature(f);
        sectoresActuales().append(id);
        return id;
    }

    // Deja los cuatro de ejemplo: un circulo de 50 km y tres sectores (de 0 a 90 grados, de 120
    // a 200 y uno que cruza el norte, de 330 a 30). Borra antes los que hubiera.
    void restableceSectores()
    {
        eliminaTodosSectores();
        creaSector(true, 50, 0, 0, QColor(0x15, 0x65, 0xc0), nombreEn(tr("Círculo 50 km")));
        creaSector(false, 100, 0, 90, QColor(0x2e, 0x7d, 0x32), nombreEn(tr("Sector 0–90")));
        creaSector(false, 150, 120, 200, QColor(0xef, 0x6c, 0x00), nombreEn(tr("Sector 120–200")));
        creaSector(false, 200, 330, 30, QColor(0x8e, 0x24, 0xaa), nombreEn(tr("Sector 330–30")));
        actualizaSectores();
    }

    // Quita todos los sectores (por tipo, incluidos los que dejara guardados otra sesion).
    void eliminaTodosSectores()
    {
        for (qint64 id : sectoresActuales())
            m_mapa->removeFeature(id);
        sectoresActuales().clear();
        if (m_cur >= 0)
            m_origenes[m_cur].ocultos.clear();
    }

    // Objetivos que caen dentro del sector \a id (su nombre o id), para analizarlo por separado.
    QStringList objetivosDentro(qint64 id) const
    {
        QStringList dentro;
        const auto f = m_mapa->feature(id);
        if (!f)
            return dentro;
        for (const MapTarget &t : m_mapa->targets())
            if (f->roundContains(t.position))
                dentro << (t.label.isEmpty() ? QString::number(t.id) : t.label);
        return dentro;
    }

    // Vuelve a escribir la lista de sectores: nombre, tamano y objetivos dentro de cada uno, con
    // su casilla de visible. Conserva el sector elegido.
    void actualizaSectores()
    {
        if (!m_listaSectores)
            return;
        const int fila = m_listaSectores->currentRow();
        const QSignalBlocker b(m_listaSectores);
        m_listaSectores->clear();
        for (qint64 id : sectoresActuales()) {
            const auto f = m_mapa->feature(id);
            if (!f)
                continue;
            const double barrido = f->kind == GeometryKind::Circle
                ? 360.0 : MapFeature::arcSweepDeg(f->arcStartDeg, f->arcEndDeg);
            const double areaKm2 = M_PI * f->radiusMeters * f->radiusMeters * barrido / 360.0 / 1e6;
            auto *it = new QListWidgetItem(
                tr("%1 · %2 km · %3 km² · %4 objetivo(s)")
                    .arg(f->name).arg(f->radiusMeters / 1000.0, 0, 'f', 0)
                    .arg(areaKm2, 0, 'f', 0).arg(objetivosDentro(id).size()),
                m_listaSectores);
            it->setData(Qt::UserRole, id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(m_cur >= 0 && m_origenes[m_cur].ocultos.contains(id) ? Qt::Unchecked
                                                                                    : Qt::Checked);
        }
        if (m_grupoSectores)
            m_grupoSectores->setTitle(m_cur >= 0 ? tr("Sectores de «%1»").arg(m_origenes[m_cur].nombre)
                                                 : tr("Sectores (elige o agrega un punto)"));
        m_listaSectores->setCurrentRow(qBound(-1, fila, m_listaSectores->count() - 1));
        actualizaConteos();
    }

    // Id del sector elegido en la lista, o -1.
    qint64 sectorElegido() const
    {
        const auto *it = m_listaSectores ? m_listaSectores->currentItem() : nullptr;
        return it ? it->data(Qt::UserRole).toLongLong() : -1;
    }

public:
    // Cuantos sectores hay, el id del i-esimo y operaciones por sector (las usa --comprobar y
    // el panel): mostrar/ocultar, cambiar y eliminar uno sin tocar los demas.
    int numSectores() const { return int(sectoresActuales().size()); }
    qint64 sector(int i) const { return sectoresActuales().value(i, -1); }
    void ponSectorVisible(qint64 id, bool visible)
    {
        auto f = m_mapa->feature(id);
        if (!f)
            return;
        if (m_cur >= 0) {                      // lo que el usuario oculto se recuerda por origen
            if (visible)
                m_origenes[m_cur].ocultos.remove(id);
            else
                m_origenes[m_cur].ocultos.insert(id);
        }
        f->visible = visible && (!m_soloElegido->isChecked() || m_cur >= 0);
        m_mapa->updateFeature(*f);
        actualizaSectores();
    }
    void cambiaSector(qint64 id, double radioKm, double ini, double fin)
    {
        auto f = m_mapa->feature(id);
        if (!f)
            return;
        f->radiusMeters = radioKm * 1000.0;
        f->arcStartDeg = ini;
        f->arcEndDeg = fin;
        m_mapa->updateFeature(*f);
        actualizaSectores();
    }
    void eliminaSector(qint64 id)
    {
        m_mapa->removeFeature(id);
        sectoresActuales().removeAll(id);
        if (m_cur >= 0)
            m_origenes[m_cur].ocultos.remove(id);
        actualizaSectores();
    }
    int dentroDe(qint64 id) const { return int(objetivosDentro(id).size()); }

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

    // Mueve el origen elegido a \a p (lo que hace «Mover el origen» con un clic): su punto, sus
    // anillos y sus sectores se van con el.
    void ponCentro(const QGeoCoordinate &p)
    {
        if (m_cur < 0)
            return;
        Origen &o = m_origenes[m_cur];
        o.pos = p;
        m_centro = p;
        m_lblCentro->setText(tr("%1 · %2").arg(o.nombre, formatCoordinate(p, CoordinateFormat::DMS)));
        if (auto f = m_mapa->feature(o.punto)) {
            f->geometry = { p };
            m_mapa->updateFeature(*f);
        }
        for (qint64 id : o.sectores)               // los sectores siguen al origen
            if (auto f = m_mapa->feature(id)) {
                f->geometry = { p };
                m_mapa->updateFeature(*f);
            }
        dibujaAnillos(o, m_cur);
        actualizaSectores();
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

    // Ajustes de anillos que dicen los controles del panel (los del punto elegido).
    AjustesAnillos leeControles() const
    {
        AjustesAnillos a;
        a.mostrar = m_mostrar->isChecked();
        a.separacion = m_separacion->value();
        a.anillos = m_numero->value();
        a.radiales = m_radiales->currentData().toInt();
        a.rotulos = m_rotulos->isChecked();
        a.enfasis = m_enfasis->isChecked();
        a.cadaAnillos = m_cadaAnillos->value();
        a.cadaGrados = m_cadaGrados->value();
        a.radialesAzules = m_colorRadiales->isChecked();
        a.paleta = m_paleta->currentIndex();
        return a;
    }

    // Pone en los controles del panel los ajustes de anillos de un origen, sin disparar nada.
    void cargaControles(const AjustesAnillos &a)
    {
        const QSignalBlocker b1(m_mostrar), b2(m_separacion), b3(m_numero), b4(m_radiales),
            b5(m_rotulos), b6(m_enfasis), b7(m_cadaAnillos), b8(m_cadaGrados),
            b9(m_colorRadiales), b10(m_paleta);
        m_mostrar->setChecked(a.mostrar);
        m_separacion->setValue(a.separacion);
        m_numero->setValue(a.anillos);
        m_radiales->setCurrentIndex(qMax(0, m_radiales->findData(a.radiales)));
        m_rotulos->setChecked(a.rotulos);
        m_enfasis->setChecked(a.enfasis);
        m_cadaAnillos->setValue(a.cadaAnillos);
        m_cadaGrados->setValue(a.cadaGrados);
        m_colorRadiales->setChecked(a.radialesAzules);
        m_paleta->setCurrentIndex(a.paleta);
    }

    // Dibuja (o quita) los anillos de un origen con SUS ajustes. Con «solo el punto elegido» los
    // de los demas puntos no se ven aunque tengan los suyos activados.
    void dibujaAnillos(Origen &o, int indice)
    {
        if (o.anillos >= 0)
            m_mapa->removeRangeMark(o.anillos);
        o.anillos = -1;
        if (!o.aj.mostrar || (m_soloElegido->isChecked() && indice != m_cur))
            return;
        RangeRings r;
        r.center = o.pos;
        r.spacingMeters = o.aj.separacion * (nauticas() ? kMetrosPorMilla : 1000.0);
        r.count = o.aj.anillos;
        r.spokeEveryDeg = o.aj.radiales;
        r.labels = o.aj.rotulos;
        const bool grises = o.aj.paleta == 1;
        r.color = grises ? QColor(180, 180, 180)             // gris claro (Anexo C)
                         : QColor(0xc6, 0x28, 0x28);         // rojo: se lee sobre la costa y el mar
        if (o.aj.enfasis) {
            r.majorRingEvery = o.aj.cadaAnillos;
            r.majorSpokeEveryDeg = o.aj.cadaGrados;
            r.majorColor = grises ? QColor(80, 80, 80) : QColor(0x5d, 0x0e, 0x0e);
        }
        if (o.aj.radialesAzules)
            r.spokeColor = QColor(0x15, 0x65, 0xc0);         // radiales en azul
        o.anillos = m_mapa->addRangeRings(r);
    }

    // Aplica los controles del panel al punto elegido y repinta los anillos de todos (las
    // unidades son comunes). Ajustes de mapa: barra de escala y cuadricula.
    void aplicaAnillos()
    {
        m_mapa->setScaleBarUnits(nauticas() ? ScaleUnits::Nautical : ScaleUnits::Metric);
        m_mapa->setScaleBarVisible(m_barra->isChecked());
        m_mapa->setGraticuleVisible(m_cuadricula->isChecked());
        if (m_cur >= 0)
            m_origenes[m_cur].aj = leeControles();
        for (int i = 0; i < m_origenes.size(); ++i)
            dibujaAnillos(m_origenes[i], i);
    }

    // Muestra u oculta los anillos y los sectores de cada origen segun cual esta elegido (con
    // «solo el elegido») y lo que el usuario oculto a mano.
    void aplicaVisibilidad()
    {
        for (int i = 0; i < m_origenes.size(); ++i) {
            Origen &o = m_origenes[i];
            dibujaAnillos(o, i);
            for (qint64 id : o.sectores)
                if (auto f = m_mapa->feature(id)) {
                    const bool ver = !o.ocultos.contains(id) && (!m_soloElegido->isChecked() || i == m_cur);
                    if (f->visible != ver) {
                        f->visible = ver;
                        m_mapa->updateFeature(*f);
                    }
                }
        }
    }

    // Agrega un origen (un punto con sus anillos y sectores) y lo elige. Devuelve su indice.
    int agregaOrigen(const QGeoCoordinate &p, const QString &nombre)
    {
        MapFeature f;
        f.kind = GeometryKind::Point;
        f.layerId = QStringLiteral("puntos");
        f.type = QStringLiteral("punto");
        f.name = nombre;
        f.geometry = { p };
        f.style.lineColor = QColor(0x15, 0x65, 0xc0);
        Origen o;
        o.punto = m_mapa->addFeature(f);
        o.nombre = nombre;
        o.pos = p;
        m_puntos.append(o.punto);
        m_origenes.append(o);
        actualizaOrigenes();
        seleccionaOrigen(int(m_origenes.size()) - 1);
        return int(m_origenes.size()) - 1;
    }

public:
    // Elige el origen \a i: sus ajustes pasan a los controles y sus anillos y sectores a la vista.
    void seleccionaOrigen(int i)
    {
        if (i < 0 || i >= m_origenes.size() || i == m_cur) {
            if (i == m_cur)
                return;
            m_cur = -1;
        } else {
            m_cur = i;
            m_centro = m_origenes[i].pos;
            cargaControles(m_origenes[i].aj);
            m_lblCentro->setText(tr("%1 · %2").arg(m_origenes[i].nombre,
                                                    formatCoordinate(m_centro, CoordinateFormat::DMS)));
            statusBar()->showMessage(tr("Punto elegido: «%1». Sus anillos y sus sectores son los de las pestañas Anillos y Sectores.")
                                         .arg(m_origenes[i].nombre), 8000);
        }
        if (m_cmbOrigen) {
            const QSignalBlocker b(m_cmbOrigen);
            m_cmbOrigen->setCurrentIndex(m_cur);
        }
        aplicaVisibilidad();
        actualizaSectores();
    }

private:
    // Rellena la lista desplegable de origenes.
    void actualizaOrigenes()
    {
        if (!m_cmbOrigen)
            return;
        const QSignalBlocker b(m_cmbOrigen);
        m_cmbOrigen->clear();
        for (const Origen &o : m_origenes)
            m_cmbOrigen->addItem(o.nombre);
        m_cmbOrigen->setCurrentIndex(m_cur);
    }

    // Nombre de un sector con el del punto al que pertenece («Centro · Sector 0–90»).
    QString nombreEn(const QString &base) const
    {
        return m_cur >= 0 ? QStringLiteral("%1 · %2").arg(m_origenes[m_cur].nombre, base) : base;
    }

    // Sectores del origen elegido (una lista vacia si no hay ninguno).
    QVector<qint64> &sectoresActuales()
    {
        static QVector<qint64> vacio;
        vacio.clear();
        return m_cur >= 0 ? m_origenes[m_cur].sectores : vacio;
    }
    const QVector<qint64> &sectoresActuales() const
    {
        static const QVector<qint64> vacio;
        return m_cur >= 0 ? m_origenes[m_cur].sectores : vacio;
    }

    // Quita todos los puntos con nombre que haya: los que agrega este ejemplo («punto», «sitio»)
    // y los «aerodromo» que dejo guardados otro ejemplo.
    void quitaPuntos()
    {
        for (const char *t : { "punto", "sitio", "aerodromo", "sector" })
            m_mapa->clearFeaturesOfType(QString::fromLatin1(t));
        for (const Origen &o : m_origenes)
            if (o.anillos >= 0)
                m_mapa->removeRangeMark(o.anillos);
        m_origenes.clear();
        m_cur = -1;
        m_puntos.clear();
        m_sitios = -1;
        actualizaOrigenes();
        actualizaSectores();
    }

    // Agrega un lugar al mapa: como punto suelto (una entidad por punto) o como una parte nueva
    // de la entidad «Sitios» (una sola entidad con tantas partes como lugares, cada una con su
    // nombre y un color propio de una paleta; se crea al agregar la primera).
    void agregaImpl(const QGeoCoordinate &p, const QString &nombre, bool comoParte)
    {
        if (!comoParte) {
            agregaOrigen(p, nombre);         // un punto suelto es un origen: lleva anillos y sectores
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

    // Vuelve a escribir lo que depende de lo que hay en el mapa: el arbol de capas y tipos con
    // sus cuentas y la lista de lo agregado.
    void actualizaConteos()
    {
        actualizaArbol();
        actualizaElementos();
    }

    // Reconstruye el arbol de la pestana «Capas»: cada capa con su casilla de visible y, debajo,
    // sus tipos con cuantas entidades hay y su casilla (ocultar un tipo lo oculta en todas las
    // capas). Los tipos vacios (sin nombre) se cuentan pero no se pueden ocultar.
    void actualizaArbol()
    {
        if (!m_arbol)
            return;
        const QSignalBlocker b(m_arbol);
        m_arbol->clear();
        for (const LayerInfo &capa : m_mapa->featureLayers()) {
            auto *it = new QTreeWidgetItem(m_arbol);
            it->setText(0, capa.displayName.isEmpty() ? capa.id : capa.displayName);
            it->setText(1, QString::number(capa.featureCount));
            it->setData(0, Qt::UserRole, capa.id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, capa.visible ? Qt::Checked : Qt::Unchecked);
            const QMap<QString, int> tipos = m_mapa->featureTypeCounts(capa.id);
            for (auto t = tipos.constBegin(); t != tipos.constEnd(); ++t) {
                auto *h = new QTreeWidgetItem(it);
                h->setText(0, t.key().isEmpty() ? tr("(sin tipo)") : t.key());
                h->setText(1, QString::number(t.value()));
                h->setData(0, Qt::UserRole, capa.id);
                h->setData(0, Qt::UserRole + 1, t.key());
                if (!t.key().isEmpty()) {
                    h->setFlags(h->flags() | Qt::ItemIsUserCheckable);
                    h->setCheckState(0, m_mapa->isFeatureTypeVisible(t.key()) ? Qt::Checked
                                                                               : Qt::Unchecked);
                }
            }
            it->setExpanded(!tipos.isEmpty() && capa.featureCount < 100);
        }
    }

    // Rellena la lista de lo agregado: cada punto suelto y cada parte de «Sitios».
    void actualizaElementos()
    {
        if (!m_listaElementos)
            return;
        const QPair<qint64, int> previo = elementoElegido();
        const QSignalBlocker b(m_listaElementos);
        m_listaElementos->clear();
        const auto anade = [this](const QString &texto, qint64 id, int parte) {
            auto *it = new QListWidgetItem(texto, m_listaElementos);
            it->setData(Qt::UserRole, id);
            it->setData(Qt::UserRole + 1, parte);
        };
        for (qint64 id : m_puntos)
            if (const auto f = m_mapa->feature(id))
                anade(tr("Punto · %1").arg(f->name), id, -1);
        if (const auto s = m_mapa->feature(m_sitios))
            for (int i = 0; i < s->parts.size(); ++i)
                anade(tr("Sitios · %1").arg(s->partInfo(i) ? s->partInfo(i)->name : QString()),
                      m_sitios, i);
        for (int i = 0; i < m_listaElementos->count(); ++i)
            if (m_listaElementos->item(i)->data(Qt::UserRole).toLongLong() == previo.first &&
                m_listaElementos->item(i)->data(Qt::UserRole + 1).toInt() == previo.second)
                m_listaElementos->setCurrentRow(i);
    }

    // (id de la entidad, parte o -1) del elemento elegido en la lista, o (-1, -1).
    QPair<qint64, int> elementoElegido() const
    {
        const auto *it = m_listaElementos ? m_listaElementos->currentItem() : nullptr;
        return it ? qMakePair(it->data(Qt::UserRole).toLongLong(), it->data(Qt::UserRole + 1).toInt())
                  : qMakePair(qint64(-1), -1);
    }

    // Elige en la lista el elemento (entidad, parte) que se acaba de tocar en el mapa.
    void seleccionaElemento(qint64 id, int parte)
    {
        if (!m_listaElementos)
            return;
        for (int i = 0; i < m_listaElementos->count(); ++i)
            if (m_listaElementos->item(i)->data(Qt::UserRole).toLongLong() == id &&
                m_listaElementos->item(i)->data(Qt::UserRole + 1).toInt() == parte) {
                m_listaElementos->setCurrentRow(i);
                return;
            }
    }

    // Carga en los controles el lado del rotulo y la fuente del elemento elegido.
    void cargaElementoElegido()
    {
        const auto [id, parte] = elementoElegido();
        const auto f = m_mapa->feature(id);
        if (!f)
            return;
        const LabelAnchor a = parte >= 0 && f->partInfo(parte) ? f->partInfo(parte)->labelAnchor
                                                                : f->style.labelAnchor;
        int idx = 0;
        for (int i = 0; i < int(sizeof(kPosiciones) / sizeof(kPosiciones[0])); ++i)
            if (kPosiciones[i].ancla == a)
                idx = i;
        const QSignalBlocker b1(m_elAncla), b2(m_elFuente);
        m_elAncla->setCurrentIndex(idx);
        m_elFuente->setValue(f->style.labelFontSize);
    }

    // Aplica al elemento elegido (o a todos, si se pidio) el lado y la fuente de los controles.
    void aplicaElementoElegido()
    {
        const LabelAnchor ancla = kPosiciones[qMax(0, m_elAncla->currentIndex())].ancla;
        const int fuente = m_elFuente->value();
        if (m_elATodos->isChecked()) {
            ponAncla(ancla);
            ponTamano(fuente);
            if (auto s = m_mapa->feature(m_sitios)) {
                for (PartInfo &pi : s->partsInfo)
                    pi.labelAnchor = ancla;
                s->style.labelFontSize = fuente;
                m_mapa->updateFeature(*s);
            }
            return;
        }
        const auto [id, parte] = elementoElegido();
        auto f = m_mapa->feature(id);
        if (!f)
            return;
        if (parte >= 0 && parte < f->partsInfo.size())
            f->partsInfo[parte].labelAnchor = ancla;
        else
            f->style.labelAnchor = ancla;
        f->style.labelFontSize = fuente;           // la fuente es de toda la entidad
        m_mapa->updateFeature(*f);
    }

public:
    // Cambia el color de un elemento: el de la parte (colorOverride) o el del punto suelto.
    void ponColor(qint64 id, int parte, const QColor &color)
    {
        auto f = m_mapa->feature(id);
        if (!f)
            return;
        if (parte >= 0 && parte < f->partsInfo.size())
            f->partsInfo[parte].colorOverride = color;
        else
            f->style.lineColor = color;
        m_mapa->updateFeature(*f);
    }

private:
    // Pone el color (el estado) al elemento elegido en la lista.
    void ponColorElegido(const QColor &color)
    {
        const auto [id, parte] = elementoElegido();
        ponColor(id, parte, color);
    }

    // Quita el elemento elegido: un punto suelto, o una parte de «Sitios» (si era la ultima,
    // la entidad entera).
    void quitaElementoElegido()
    {
        const auto [id, parte] = elementoElegido();
        auto f = m_mapa->feature(id);
        if (!f)
            return;
        if (parte >= 0 && id == m_sitios && parte < f->parts.size()) {
            f->parts.removeAt(parte);
            if (parte < f->partsInfo.size())
                f->partsInfo.removeAt(parte);
            if (f->parts.isEmpty()) {
                m_mapa->removeFeature(id);
                m_sitios = -1;
            } else {
                f->geometry = f->parts.first();
                m_mapa->updateFeature(*f);
            }
        } else {
            for (int i = 0; i < m_origenes.size(); ++i)
                if (m_origenes[i].punto == id) {          // con su punto se van sus anillos y sectores
                    if (m_origenes[i].anillos >= 0)
                        m_mapa->removeRangeMark(m_origenes[i].anillos);
                    for (qint64 sid : m_origenes[i].sectores)
                        m_mapa->removeFeature(sid);
                    m_origenes.removeAt(i);
                    m_cur = -1;
                    break;
                }
            m_mapa->removeFeature(id);
            m_puntos.removeAll(id);
            actualizaOrigenes();
            seleccionaOrigen(m_origenes.isEmpty() ? -1 : 0);
        }
        actualizaConteos();
    }

    // Pone el rotulo por segmentos de un objetivo a partir de sus datos: identificacion y estado
    // (cada uno de su color) en la primera linea y los datos de dominio en las otras dos.
    void ponContenido(qint64 id)
    {
        const DatosObj d = m_datosObj.value(id);
        QVector<TargetLabelLine> lineas;
        lineas << linea({ { d.ident, QColor(0, 70, 200) }, { d.estado, colorEstado(d.estado) } });
        lineas << linea({ { d.l2, QColor() } });
        lineas << linea({ { d.l3, QColor() } });
        m_mapa->setTargetLabelContent(id, lineas);
        m_mapa->setTargetLabel(id, d.ident);
    }

public:
    // Agrega un objetivo en \a p con su rumbo y estado; sin identificacion se le da una. Es el
    // mismo que usa el clic en el mapa. Devuelve su id.
    qint64 agregaObjetivo(const QGeoCoordinate &p, const QString &ident, double rumbo,
                          const QString &estado)
    {
        MapTarget t;
        t.position = p;
        t.headingDeg = rumbo;
        t.kind = QStringLiteral("aeronave");
        const qint64 id = m_mapa->addTarget(t);
        DatosObj d;
        d.ident = ident.isEmpty() ? QStringLiteral("E%1").arg(100 + m_objetivos.size()) : ident;
        d.estado = estado;
        d.l2 = QStringLiteral("+%1  I   0%2").arg(30 + m_objetivos.size()).arg(1000 + 37 * m_objetivos.size());
        d.l3 = QStringLiteral("_S_%1     1.2.3.%2").arg(QChar(char('A' + int(m_objetivos.size() % 26)))).arg(m_objetivos.size());
        m_datosObj.insert(id, d);
        m_objetivos.append(id);
        ponContenido(id);
        actualizaObjetivos();
        return id;
    }

    // Cambia el estado de un objetivo (y con el, el color de su segundo segmento).
    void cambiaEstadoObjetivo(qint64 id, const QString &estado)
    {
        if (!m_datosObj.contains(id))
            return;
        m_datosObj[id].estado = estado;
        ponContenido(id);
        actualizaObjetivos();
    }

private:
    // Tres objetivos de ejemplo con formulario de tres lineas.
    void creaObjetivos()
    {
        agregaObjetivo(QGeoCoordinate(23.40, -82.00), QStringLiteral("E046"), 80.0, QStringLiteral("M1M2"));
        agregaObjetivo(QGeoCoordinate(23.40, -82.80), QStringLiteral("E051"), 160.0, QStringLiteral("EMER"));
        agregaObjetivo(QGeoCoordinate(22.80, -82.00), QStringLiteral("E077"), 300.0, QStringLiteral("DET"));
    }

    // Tres objetivos casi en el mismo sitio, para ver que los formularios se pisan y arrastrarlos.
    void agregaTresPegados()
    {
        const QGeoCoordinate c = m_mapa->center();
        for (int i = 0; i < 3; ++i)
            agregaObjetivo(c.atDistanceAndAzimuth(600.0 * i, 45.0), QString(), 90.0 + 40.0 * i,
                           QString::fromUtf8(kEstados[i % 3].nombre));
    }

    // Rellena la lista de objetivos: identificacion y estado.
    void actualizaObjetivos()
    {
        if (!m_listaObjetivos)
            return;
        const int fila = m_listaObjetivos->currentRow();
        const QSignalBlocker b(m_listaObjetivos);
        m_listaObjetivos->clear();
        for (qint64 id : m_objetivos) {
            const DatosObj d = m_datosObj.value(id);
            auto *it = new QListWidgetItem(QStringLiteral("%1 · %2").arg(d.ident, d.estado),
                                           m_listaObjetivos);
            it->setData(Qt::UserRole, id);
            it->setForeground(colorEstado(d.estado));
        }
        m_listaObjetivos->setCurrentRow(qBound(-1, fila, m_listaObjetivos->count() - 1));
    }

    // Id del objetivo elegido en la lista, o -1.
    qint64 objetivoElegido() const
    {
        const auto *it = m_listaObjetivos ? m_listaObjetivos->currentItem() : nullptr;
        return it ? it->data(Qt::UserRole).toLongLong() : -1;
    }

    // Elige en la lista (y en el mapa) el objetivo \a id.
    void seleccionaObjetivo(qint64 id)
    {
        if (!m_listaObjetivos)
            return;
        for (int i = 0; i < m_listaObjetivos->count(); ++i)
            if (m_listaObjetivos->item(i)->data(Qt::UserRole).toLongLong() == id)
                m_listaObjetivos->setCurrentRow(i);
    }

    // Carga en los campos los datos del objetivo elegido y lo selecciona en el mapa.
    void cargaObjetivoElegido()
    {
        const qint64 id = objetivoElegido();
        if (id < 0)
            return;
        const DatosObj d = m_datosObj.value(id);
        m_obId->setText(d.ident);
        m_obEstado->setCurrentText(d.estado);
        m_obL2->setText(d.l2);
        m_obL3->setText(d.l3);
        m_mapa->setSelectedTarget(id);
    }

    // Aplica al objetivo elegido lo escrito en los campos: su rotulo cambia de texto y de color.
    void aplicaObjetivoElegido()
    {
        const qint64 id = objetivoElegido();
        if (id < 0)
            return;
        DatosObj &d = m_datosObj[id];
        d.ident = m_obId->text();
        d.estado = m_obEstado->currentText();
        d.l2 = m_obL2->text();
        d.l3 = m_obL3->text();
        ponContenido(id);
        actualizaObjetivos();
    }

    // Quita el objetivo elegido.
    void quitaObjetivoElegido()
    {
        const qint64 id = objetivoElegido();
        if (id < 0)
            return;
        m_mapa->removeTarget(id);
        m_objetivos.removeAll(id);
        m_datosObj.remove(id);
        actualizaObjetivos();
    }

    // Un paso de la simulacion: cada objetivo avanza unos kilometros por su rumbo.
    void muevePasoObjetivos()
    {
        for (qint64 id : m_objetivos)
            if (const auto t = m_mapa->target(id))
                m_mapa->updateTarget(id, t->position.atDistanceAndAzimuth(3000.0, t->headingDeg));
        actualizaSectores();                       // los objetivos dentro de cada sector cambian
    }


    // Grupo del panel para los sectores: lista con casilla de visible, valores para agregar o
    // cambiar uno, y botones para eliminar uno o todos. Cada sector va por separado.
    QGroupBox *crearGrupoSectores()
    {
        auto *gz = new QGroupBox(tr("Sectores (áreas a vigilar)"));
        m_grupoSectores = gz;
        auto *vz = new QVBoxLayout(gz);
        m_listaSectores = new QListWidget(gz);
        m_listaSectores->setMaximumHeight(130);
        auto *fz = new QFormLayout;
        auto *radio = new QDoubleSpinBox(gz);
        radio->setRange(0.1, 2000.0);
        radio->setValue(100.0);
        radio->setSuffix(tr(" km"));
        auto *ini = new QSpinBox(gz);
        auto *fin = new QSpinBox(gz);
        for (QSpinBox *sp : { ini, fin }) {
            sp->setRange(0, 359);
            sp->setSuffix(QStringLiteral("°"));
        }
        fin->setValue(90);
        auto *esCirculo = new QCheckBox(tr("Círculo entero (sin inicio ni fin)"), gz);
        fz->addRow(tr("Radio"), radio);
        fz->addRow(tr("Desde (azimut)"), ini);
        fz->addRow(tr("Hasta (azimut)"), fin);
        fz->addRow(esCirculo);
        auto *agregaSec = new QPushButton(tr("Agregar sector con estos valores"), gz);
        auto *cambiaSec = new QPushButton(tr("Cambiar el sector elegido"), gz);
        auto *quitaSec = new QPushButton(tr("Eliminar el sector elegido"), gz);
        auto *restSec = new QPushButton(tr("Restablecer los sectores de ejemplo"), gz);
        auto *borraSec = new QPushButton(tr("Eliminar todos los sectores"), gz);
        vz->addWidget(new QLabel(tr("La casilla de cada uno lo muestra u oculta:"), gz));
        vz->addWidget(m_listaSectores);
        vz->addLayout(fz);
        for (QPushButton *b : { agregaSec, cambiaSec, quitaSec, restSec, borraSec })
            vz->addWidget(b);
        connect(m_listaSectores, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
            const qint64 id = it->data(Qt::UserRole).toLongLong();
            const bool ver = it->checkState() == Qt::Checked;
            if (m_cur >= 0 && m_origenes[m_cur].ocultos.contains(id) == ver)
                ponSectorVisible(id, ver);
        });
        connect(m_listaSectores, &QListWidget::currentRowChanged, this,
                [this, radio, ini, fin, esCirculo](int) {
                    const qint64 id = sectorElegido();
                    const auto f = m_mapa->feature(id);
                    if (!f)
                        return;
                    radio->setValue(f->radiusMeters / 1000.0);
                    ini->setValue(int(f->arcStartDeg));
                    fin->setValue(int(f->arcEndDeg));
                    esCirculo->setChecked(f->kind == GeometryKind::Circle);
                    const QStringList dentro = objetivosDentro(id);
                    statusBar()->showMessage(
                        tr("Dentro de «%1»: %2").arg(f->name,
                            dentro.isEmpty() ? tr("ningún objetivo")
                                             : dentro.join(QStringLiteral(", "))), 8000);
                });
        connect(agregaSec, &QPushButton::clicked, this, [this, radio, ini, fin, esCirculo] {
            static const QColor paleta[] = { QColor(0xc6, 0x28, 0x28), QColor(0x00, 0x83, 0x8f),
                                             QColor(0x6a, 0x1b, 0x9a), QColor(0x55, 0x8b, 0x2f) };
            const bool c = esCirculo->isChecked();
            creaSector(c, radio->value(), ini->value(), fin->value(),
                       paleta[sectoresActuales().size() % 4],
                       nombreEn(c ? tr("Círculo %1 km").arg(radio->value(), 0, 'f', 0)
                                  : tr("Sector %1–%2").arg(ini->value()).arg(fin->value())));
            actualizaSectores();
        });
        connect(cambiaSec, &QPushButton::clicked, this, [this, radio, ini, fin] {
            const qint64 id = sectorElegido();
            auto f = m_mapa->feature(id);
            if (!f)
                return;
            f->name = nombreEn(f->kind == GeometryKind::Arc
                ? tr("Sector %1–%2").arg(ini->value()).arg(fin->value())
                : tr("Círculo %1 km").arg(radio->value(), 0, 'f', 0));
            m_mapa->updateFeature(*f);
            cambiaSector(id, radio->value(), ini->value(), fin->value());
        });
        connect(quitaSec, &QPushButton::clicked, this, [this] {
            if (sectorElegido() >= 0)
                eliminaSector(sectorElegido());
        });
        connect(restSec, &QPushButton::clicked, this, [this] { restableceSectores(); });
        connect(borraSec, &QPushButton::clicked, this, [this] {
            eliminaTodosSectores();
            actualizaSectores();
        });
        return gz;
    }

    // Menu del clic derecho: segun lo que haya bajo el cursor ofrece lo de ese elemento. Un PUNTO
    // ofrece sus anillos y sus sectores; un sector, cambiarlo, ocultarlo o quitarlo; un objetivo,
    // su estado; un lugar vacio, agregar un punto o un objetivo alli.
    void menuContextual(const MapHit &hit, const QPoint &global)
    {
        QMenu menu(this);
        int origen = -1;
        qint64 sector = -1;
        for (int i = 0; i < m_origenes.size() && origen < 0; ++i) {
            if (m_origenes[i].punto == hit.featureId)
                origen = i;
            else if (m_origenes[i].sectores.contains(hit.featureId)) {
                origen = i;
                sector = hit.featureId;
            }
        }
        if (hit.targetId >= 0)
            menuObjetivo(&menu, hit.targetId);
        else if (sector >= 0)
            menuSector(&menu, origen, sector);
        else if (origen >= 0)
            menuPunto(&menu, origen);
        else
            menuLugar(&menu, hit.coordinate);
        menu.exec(global);
    }

    // Cabecera de un menu: una accion desactivada en negrita que dice sobre que se actua.
    static void cabecera(QMenu *menu, const QString &texto)
    {
        QAction *a = menu->addAction(texto);
        QFont f = a->font();
        f.setBold(true);
        a->setFont(f);
        a->setEnabled(false);
    }

    // Menu de un punto (origen): elegirlo, sus anillos, sus sectores, moverlo y quitarlo.
    void menuPunto(QMenu *menu, int origen)
    {
        const Origen &o = m_origenes[origen];
        cabecera(menu, tr("Punto «%1»").arg(o.nombre));
        connect(menu->addAction(tr("Elegir este punto (sus anillos y sectores en las pestañas)")),
                &QAction::triggered, this, [this, origen] { seleccionaOrigen(origen); });
        menu->addSeparator();

        QMenu *ani = menu->addMenu(tr("Anillos de este punto"));
        QAction *ver = ani->addAction(tr("Mostrar los anillos"));
        ver->setCheckable(true);
        ver->setChecked(o.aj.mostrar);
        connect(ver, &QAction::toggled, this, [this, origen](bool on) {
            seleccionaOrigen(origen);
            m_mostrar->setChecked(on);
        });
        connect(ani->addAction(tr("Escala de 300 km (30 anillos)")), &QAction::triggered, this,
                [this, origen] { seleccionaOrigen(origen); escala(300); });
        connect(ani->addAction(tr("Escala de 600 km (60 anillos)")), &QAction::triggered, this,
                [this, origen] { seleccionaOrigen(origen); escala(600); });
        connect(ani->addAction(tr("Cambiar sus ajustes (pestaña Anillos)...")), &QAction::triggered,
                this, [this, origen] {
                    seleccionaOrigen(origen);
                    m_pestanas->setCurrentIndex(0);
                });

        QMenu *sec = menu->addMenu(tr("Sectores de este punto (%1)").arg(o.sectores.size()));
        connect(sec->addAction(tr("Agregar un sector...")), &QAction::triggered, this,
                [this, origen] { agregaSectorPedido(origen, false); });
        connect(sec->addAction(tr("Agregar un círculo...")), &QAction::triggered, this,
                [this, origen] { agregaSectorPedido(origen, true); });
        for (qint64 id : o.sectores)
            if (const auto f = m_mapa->feature(id))
                menuSector(sec->addMenu(f->name), origen, id, false);
        if (!o.sectores.isEmpty()) {
            sec->addSeparator();
            connect(sec->addAction(tr("Quitar todos los sectores de este punto")),
                    &QAction::triggered, this, [this, origen] {
                        seleccionaOrigen(origen);
                        eliminaTodosSectores();
                        actualizaSectores();
                    });
        }
        menu->addSeparator();
        connect(menu->addAction(tr("Mover este punto (clic en el mapa)")), &QAction::triggered, this,
                [this, origen] {
                    seleccionaOrigen(origen);
                    m_modoClic = 0;
                    m_mapa->setActiveTool(MapTool::PickPoint);
                    m_elegir->setText(tr("Haz clic en el mapa... (Esc cancela)"));
                });
        connect(menu->addAction(tr("Quitar el punto (con sus anillos y sectores)")),
                &QAction::triggered, this, [this, origen] {
                    seleccionaElemento(m_origenes[origen].punto, -1);
                    quitaElementoElegido();
                });
    }

    // Opciones de un sector de un punto: elegir su punto, mostrarlo u ocultarlo, cambiarlo,
    // quitarlo y cuantos objetivos tiene dentro. Con \a cabeza se pone el titulo.
    void menuSector(QMenu *menu, int origen, qint64 id, bool cabeza = true)
    {
        const auto f = m_mapa->feature(id);
        if (!f)
            return;
        if (cabeza)
            cabecera(menu, tr("Sector «%1»").arg(f->name));
        const QStringList dentro = objetivosDentro(id);
        menu->addAction(dentro.isEmpty() ? tr("Ningún objetivo dentro")
                                         : tr("Dentro: %1").arg(dentro.join(QStringLiteral(", "))))
            ->setEnabled(false);
        if (cabeza)
            connect(menu->addAction(tr("Elegir su punto «%1»").arg(m_origenes[origen].nombre)),
                    &QAction::triggered, this, [this, origen] { seleccionaOrigen(origen); });
        QAction *ver = menu->addAction(tr("Mostrar"));
        ver->setCheckable(true);
        ver->setChecked(!m_origenes[origen].ocultos.contains(id));
        connect(ver, &QAction::toggled, this, [this, origen, id](bool on) {
            seleccionaOrigen(origen);
            ponSectorVisible(id, on);
        });
        connect(menu->addAction(tr("Cambiar...")), &QAction::triggered, this,
                [this, origen, id] { cambiaSectorPedido(origen, id); });
        connect(menu->addAction(tr("Eliminar")), &QAction::triggered, this, [this, origen, id] {
            seleccionaOrigen(origen);
            eliminaSector(id);
        });
    }

    // Pide radio y azimuts y agrega un sector (o un circulo) al punto \a origen.
    void agregaSectorPedido(int origen, bool circulo)
    {
        double radio = 100.0;
        int ini = 0, fin = 90;
        if (!pideSector(this, circulo, circulo ? tr("Nuevo círculo") : tr("Nuevo sector"), &radio,
                        &ini, &fin))
            return;
        seleccionaOrigen(origen);
        static const QColor paleta[] = { QColor(0xc6, 0x28, 0x28), QColor(0x00, 0x83, 0x8f),
                                         QColor(0x6a, 0x1b, 0x9a), QColor(0x55, 0x8b, 0x2f) };
        creaSector(circulo, radio, ini, fin, paleta[sectoresActuales().size() % 4],
                   nombreEn(circulo ? tr("Círculo %1 km").arg(radio, 0, 'f', 0)
                                    : tr("Sector %1–%2").arg(ini).arg(fin)));
        actualizaSectores();
    }

    // Pide otro radio y otros azimuts para el sector \a id del punto \a origen y se los pone.
    void cambiaSectorPedido(int origen, qint64 id)
    {
        const auto f = m_mapa->feature(id);
        if (!f)
            return;
        const bool circulo = f->kind == GeometryKind::Circle;
        double radio = f->radiusMeters / 1000.0;
        int ini = int(f->arcStartDeg), fin = int(f->arcEndDeg);
        if (!pideSector(this, circulo, tr("Cambiar «%1»").arg(f->name), &radio, &ini, &fin))
            return;
        seleccionaOrigen(origen);
        auto g = *f;
        g.name = nombreEn(circulo ? tr("Círculo %1 km").arg(radio, 0, 'f', 0)
                                  : tr("Sector %1–%2").arg(ini).arg(fin));
        m_mapa->updateFeature(g);
        cambiaSector(id, radio, ini, fin);
    }

    // Menu de un objetivo: su estado (cambia el color del rotulo), elegirlo y quitarlo.
    void menuObjetivo(QMenu *menu, qint64 id)
    {
        const DatosObj d = m_datosObj.value(id);
        cabecera(menu, tr("Objetivo %1 (%2)").arg(d.ident, d.estado));
        connect(menu->addAction(tr("Elegirlo en la pestaña Objetivos")), &QAction::triggered, this,
                [this, id] {
                    seleccionaObjetivo(id);
                    m_pestanas->setCurrentIndex(3);
                });
        QMenu *est = menu->addMenu(tr("Estado"));
        for (const Estado &e : kEstados) {
            const QString nombre = QString::fromUtf8(e.nombre);
            QAction *a = est->addAction(nombre);
            a->setCheckable(true);
            a->setChecked(nombre == d.estado);
            connect(a, &QAction::triggered, this, [this, id, nombre] { cambiaEstadoObjetivo(id, nombre); });
        }
        connect(menu->addAction(tr("Quitar el objetivo")), &QAction::triggered, this, [this, id] {
            seleccionaObjetivo(id);
            quitaObjetivoElegido();
        });
    }

    // Menu de un lugar vacio del mapa: agregar alli un punto (con anillos y sectores propios) o
    // un objetivo, o llevar alli el origen elegido.
    void menuLugar(QMenu *menu, const QGeoCoordinate &p)
    {
        cabecera(menu, formatCoordinate(p, CoordinateFormat::DMS));
        connect(menu->addAction(tr("Agregar un punto aquí (con sus anillos y sectores)")),
                &QAction::triggered, this,
                [this, p] { agrega(p, tr("Punto %1").arg(m_puntos.size() + 1), false); });
        connect(menu->addAction(tr("Agregar un objetivo aquí")), &QAction::triggered, this,
                [this, p] { agregaObjetivo(p, QString(), 90.0, QStringLiteral("DET")); });
        if (m_cur >= 0)
            connect(menu->addAction(tr("Mover aquí el punto elegido «%1»").arg(m_origenes[m_cur].nombre)),
                    &QAction::triggered, this, [this, p] { ponCentro(p); });
    }

    // Envuelve el contenido de una pestana en una zona con desplazamiento, con una linea al
    // principio que dice PARA QUE SIRVE (el objetivo operativo de lo que hay en ella).
    QWidget *pestana(const QString &objetivo, const QList<QWidget *> &grupos)
    {
        auto *interior = new QWidget;
        auto *col = new QVBoxLayout(interior);
        auto *obj = new QLabel(QStringLiteral("<b>%1</b>").arg(objetivo.toHtmlEscaped()), interior);
        obj->setWordWrap(true);
        obj->setStyleSheet(QStringLiteral("background:#e8f1f8;padding:6px;border-radius:4px;"));
        col->addWidget(obj);
        for (QWidget *g : grupos)
            col->addWidget(g);
        col->addStretch(1);
        auto *scroll = new QScrollArea;
        scroll->setWidget(interior);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        return scroll;
    }

    // Pestana «Anillos»: el grafico polar azimut-distancia (escala, radiales, enfasis, colores).
    QWidget *pestanaAnillos()
    {
        auto *g0 = new QGroupBox(tr("Mapa"));
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

        auto *g1 = new QGroupBox(tr("Anillos del punto elegido"));
        auto *f1 = new QFormLayout(g1);
        m_mostrar = new QCheckBox(tr("Mostrar los anillos de este punto"), g1);
        m_mostrar->setChecked(true);
        m_lblCentro = new QLabel(formatCoordinate(kHabana, CoordinateFormat::DMS), g1);
        m_lblCentro->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_elegir = new QPushButton(tr("Mover el origen elegido (clic en el mapa)"), g1);
        auto *habana = new QPushButton(tr("Origen elegido en La Habana"), g1);
        auto *ir = new QPushButton(tr("Llevar el mapa al origen elegido"), g1);
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
        f1->addRow(tr("Origen"), m_lblCentro);
        f1->addRow(m_elegir);
        f1->addRow(habana);
        f1->addRow(ir);
        f1->addRow(tr("Unidades"), m_unidades);
        f1->addRow(tr("Separación"), m_separacion);
        f1->addRow(tr("Anillos"), m_numero);
        f1->addRow(tr("Radiales"), m_radiales);
        f1->addRow(m_rotulos);

        auto *gp = new QGroupBox(tr("Escala polar, énfasis y colores"));
        auto *fp = new QFormLayout(gp);
        auto *e300 = new QPushButton(tr("Escala de 300 km (30 anillos)"), gp);
        auto *e600 = new QPushButton(tr("Escala de 600 km (60 anillos)"), gp);
        m_enfasis = new QCheckBox(tr("Énfasis: anillo y radial «mayor» más oscuro"), gp);
        m_cadaAnillos = new QSpinBox(gp);
        m_cadaAnillos->setRange(1, 50);
        m_cadaAnillos->setValue(5);
        m_cadaGrados = new QSpinBox(gp);
        m_cadaGrados->setRange(5, 180);
        m_cadaGrados->setValue(30);
        m_colorRadiales = new QCheckBox(tr("Radiales de otro color (azul)"), gp);
        m_paleta = new QComboBox(gp);
        m_paleta->addItems({ tr("Rojo (se lee sobre costa y mar)"),
                             tr("Grises: claro y oscuro (como el Anexo C)") });
        fp->addRow(e300);
        fp->addRow(e600);
        fp->addRow(m_enfasis);
        fp->addRow(tr("Un anillo de cada"), m_cadaAnillos);
        fp->addRow(tr("Un radial cada (°)"), m_cadaGrados);
        fp->addRow(m_colorRadiales);
        fp->addRow(tr("Colores"), m_paleta);
        connect(e300, &QPushButton::clicked, this, [this] { escala(300); });
        connect(e600, &QPushButton::clicked, this, [this] { escala(600); });
        connect(m_elegir, &QPushButton::clicked, this, [this] {
            m_modoClic = 0;
            m_mapa->setActiveTool(MapTool::PickPoint);
            m_elegir->setText(tr("Haz clic en el mapa... (Esc cancela)"));
        });
        connect(habana, &QPushButton::clicked, this, [this] { ponCentro(kHabana); });
        connect(ir, &QPushButton::clicked, this, [this] { m_mapa->setCenter(m_centro); });
        // Las conexiones de los ajustes, al final: aplicaAnillos lee todos los controles.
        for (QCheckBox *c : { m_mostrar, m_rotulos, m_barra, m_cuadricula, m_enfasis,
                              m_colorRadiales })
            connect(c, &QCheckBox::toggled, this, [this] { aplicaAnillos(); });
        for (QSpinBox *sp : { m_cadaAnillos, m_cadaGrados })
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this] { aplicaAnillos(); });
        for (QComboBox *c : { m_unidades, m_radiales, m_paleta })
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this] { aplicaAnillos(); });
        connect(m_separacion, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this] { aplicaAnillos(); });
        connect(m_numero, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this] { aplicaAnillos(); });
        return pestana(tr("Medir distancia y marcación desde un punto: anillos cada N km y "
                          "radiales cada N°, con uno de cada varios más oscuro para leer rápido."),
                       { g0, g1, gp });
    }

    // Pestana «Sectores»: areas a vigilar, una por una.
    QWidget *pestanaSectores()
    {
        return pestana(tr("Señalar al operador un área donde concentrarse (círculo o cuña), "
                          "mostrarla, cambiarla o quitarla sin tocar las demás, y saber qué "
                          "objetivos hay dentro."), { crearGrupoSectores() });
    }

    // Pestana «Puntos»: buscar lugares, agregarlos y ajustar cada uno (rotulo, fuente, color).
    QWidget *pestanaPuntos()
    {
        auto *gb = new QGroupBox(tr("1. Agregar (buscador de lugares)"));
        auto *vb = new QVBoxLayout(gb);
        auto *caja = new QLineEdit(gb);
        caja->setPlaceholderText(tr("Nombre de un lugar y Enter"));
        m_resultados = new QListWidget(gb);
        m_resultados->setMaximumHeight(120);
        auto *aPunto = new QPushButton(tr("Agregar como punto suelto"), gb);
        auto *aParte = new QPushButton(tr("Agregar como parte de «Sitios» (una entidad)"), gb);
        m_colocar = new QPushButton(tr("Agregar un punto con un clic en el mapa"), gb);
        vb->addWidget(caja);
        vb->addWidget(m_resultados);
        vb->addWidget(aPunto);
        vb->addWidget(aParte);
        vb->addWidget(m_colocar);
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
        connect(m_colocar, &QPushButton::clicked, this, [this] {
            m_modoClic = 1;
            m_mapa->setActiveTool(MapTool::PickPoint);
            m_colocar->setText(tr("Haz clic donde va el punto... (Esc cancela)"));
        });

        auto *ge = new QGroupBox(tr("2. Lo agregado (elige uno para ajustarlo)"));
        auto *ve = new QVBoxLayout(ge);
        m_listaElementos = new QListWidget(ge);
        m_listaElementos->setMaximumHeight(130);
        auto *fe = new QFormLayout;
        m_elAncla = new QComboBox(ge);
        for (const Posicion &po : kPosiciones)
            m_elAncla->addItem(tr(po.nombre));
        m_elFuente = new QSpinBox(ge);
        m_elFuente->setRange(0, 40);
        m_elFuente->setSpecialValueText(tr("0 (el del mapa)"));
        m_elATodos = new QCheckBox(tr("Aplicar el lado y la fuente a TODOS"), ge);
        fe->addRow(tr("Lado del rótulo"), m_elAncla);
        fe->addRow(tr("Fuente (pt)"), m_elFuente);
        fe->addRow(m_elATodos);
        auto *estados = new QHBoxLayout;
        const struct { const char *t; QRgb c; } est[] = {
            {"Activo", 0xff2e7d32}, {"Inactivo", 0xff757575}, {"Alarma", 0xffc62828}, {"Normal", 0xff1565c0}};
        for (const auto &e : est) {
            auto *b = new QPushButton(tr(e.t), ge);
            const QColor col = QColor::fromRgb(e.c);
            b->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;").arg(col.name()));
            connect(b, &QPushButton::clicked, this, [this, col] { ponColorElegido(col); });
            estados->addWidget(b);
        }
        auto *quitaEl = new QPushButton(tr("Quitar el elegido"), ge);
        auto *quitaTodo = new QPushButton(tr("Quitar todos los puntos y sitios"), ge);
        ve->addWidget(m_listaElementos);
        ve->addLayout(fe);
        ve->addWidget(new QLabel(tr("Color (estado) del elegido:"), ge));
        ve->addLayout(estados);
        ve->addWidget(quitaEl);
        ve->addWidget(quitaTodo);
        connect(m_listaElementos, &QListWidget::currentRowChanged, this,
                [this](int) { cargaElementoElegido(); });
        connect(m_elAncla, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { aplicaElementoElegido(); });
        connect(m_elFuente, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { aplicaElementoElegido(); });
        connect(quitaEl, &QPushButton::clicked, this, [this] { quitaElementoElegido(); });
        connect(quitaTodo, &QPushButton::clicked, this, [this] {
            quitaPuntos();
            actualizaConteos();
        });
        return pestana(tr("Tener muchos elementos con el mismo icono (una entidad con partes) o "
                          "sueltos, elegir el lado de cada nombre para que no se pisen y "
                          "cambiar el color según su estado."), { gb, ge });
    }

    // Pestana «Objetivos»: formularios arrastrables, estado por colores y prueba de solape.
    QWidget *pestanaObjetivos()
    {
        auto *gl = new QGroupBox(tr("1. Objetivos"));
        auto *vl = new QVBoxLayout(gl);
        m_listaObjetivos = new QListWidget(gl);
        m_listaObjetivos->setMaximumHeight(110);
        auto *fo = new QFormLayout;
        m_obId = new QLineEdit(gl);
        m_obEstado = new QComboBox(gl);
        for (const auto &e : kEstados)
            m_obEstado->addItem(QString::fromUtf8(e.nombre));
        m_obL2 = new QLineEdit(gl);
        m_obL3 = new QLineEdit(gl);
        fo->addRow(tr("Identificación"), m_obId);
        fo->addRow(tr("Estado (su color)"), m_obEstado);
        fo->addRow(tr("Línea 2"), m_obL2);
        fo->addRow(tr("Línea 3"), m_obL3);
        auto *aplicaOb = new QPushButton(tr("Cambiar el rótulo del elegido"), gl);
        auto *agregaOb = new QPushButton(tr("Agregar un objetivo con un clic en el mapa"), gl);
        auto *tres = new QPushButton(tr("Agregar 3 objetivos pegados (probar solapes)"), gl);
        auto *quitaOb = new QPushButton(tr("Quitar el elegido"), gl);
        m_simula = new QCheckBox(tr("Simular movimiento (se desplazan por su rumbo)"), gl);
        vl->addWidget(m_listaObjetivos);
        vl->addLayout(fo);
        vl->addWidget(aplicaOb);
        vl->addWidget(agregaOb);
        vl->addWidget(tres);
        vl->addWidget(quitaOb);
        vl->addWidget(m_simula);
        m_agregaObjetivo = agregaOb;
        connect(m_listaObjetivos, &QListWidget::currentRowChanged, this,
                [this](int) { cargaObjetivoElegido(); });
        connect(aplicaOb, &QPushButton::clicked, this, [this] { aplicaObjetivoElegido(); });
        connect(agregaOb, &QPushButton::clicked, this, [this] {
            m_modoClic = 2;
            m_mapa->setActiveTool(MapTool::PickPoint);
            m_agregaObjetivo->setText(tr("Haz clic donde va el objetivo... (Esc cancela)"));
        });
        connect(tres, &QPushButton::clicked, this, [this] { agregaTresPegados(); });
        connect(quitaOb, &QPushButton::clicked, this, [this] { quitaObjetivoElegido(); });
        connect(m_simula, &QCheckBox::toggled, this,
                [this](bool on) { on ? m_timerSim.start() : m_timerSim.stop(); });

        auto *gf = new QGroupBox(tr("2. Aspecto de los formularios (arrastrables)"));
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
        auto *quitar = new QPushButton(tr("Quitar offsets propios (vuelven al de por defecto)"), gf);
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
        actualizaObjetivos();
        return pestana(tr("Leer varios objetivos a la vez: el color de cada segmento es información "
                          "(identificación, estado) y el operador arrastra cada formulario para "
                          "que no se pisen."), { gl, gf });
    }

    // Pestana «Capas»: que hay en el mapa, por capa y por tipo, para filtrar y borrar en bloque.
    QWidget *pestanaCapas()
    {
        auto *g = new QGroupBox(tr("Capas y tipos (con cuántas entidades hay)"));
        auto *v = new QVBoxLayout(g);
        m_arbol = new QTreeWidget(g);
        m_arbol->setHeaderLabels({ tr("Capa / tipo"), tr("Nº") });
        m_arbol->setColumnWidth(0, 250);
        m_arbol->setMinimumHeight(260);
        auto *soloEste = new QPushButton(tr("Mostrar SOLO el tipo elegido"), g);
        auto *todo = new QPushButton(tr("Mostrar todo"), g);
        auto *borra = new QPushButton(tr("Borrar el tipo elegido (en esa capa)"), g);
        auto *act = new QPushButton(tr("Actualizar"), g);
        v->addWidget(new QLabel(tr("La casilla de una capa la muestra u oculta; la de un tipo "
                                   "oculta ese tipo en todas las capas."), g));
        v->addWidget(m_arbol);
        v->addWidget(soloEste);
        v->addWidget(todo);
        v->addWidget(borra);
        v->addWidget(act);
        connect(m_arbol, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int) {
            const QString capa = it->data(0, Qt::UserRole).toString();
            const QString tipo = it->data(0, Qt::UserRole + 1).toString();
            const bool ver = it->checkState(0) == Qt::Checked;
            if (tipo.isEmpty() && !capa.isEmpty())
                m_mapa->setFeatureLayerVisible(capa, ver);
            else if (!tipo.isEmpty())
                m_mapa->setFeatureTypeVisible(tipo, ver);
        });
        connect(act, &QPushButton::clicked, this, [this] { actualizaConteos(); });
        connect(todo, &QPushButton::clicked, this, [this] {
            for (const QString &t : m_mapa->hiddenFeatureTypes())
                m_mapa->setFeatureTypeVisible(t, true);
            actualizaConteos();
        });
        connect(soloEste, &QPushButton::clicked, this, [this] {
            const auto *it = m_arbol->currentItem();
            const QString elegido = it ? it->data(0, Qt::UserRole + 1).toString() : QString();
            if (elegido.isEmpty())
                return;
            for (const QString &t : m_mapa->featureTypeCounts().keys())
                m_mapa->setFeatureTypeVisible(t, t == elegido);
            actualizaConteos();
        });
        connect(borra, &QPushButton::clicked, this, [this] {
            const auto *it = m_arbol->currentItem();
            if (!it || it->data(0, Qt::UserRole + 1).toString().isEmpty())
                return;
            m_mapa->clearFeaturesOfType(it->data(0, Qt::UserRole + 1).toString(),
                                        it->data(0, Qt::UserRole).toString());
            actualizaConteos();
        });
        actualizaConteos();
        return pestana(tr("Mostrar solo lo que importa («solo los aeródromos internos»), ocultar "
                          "una categoría o borrarla de una vez, y saber cuántas hay."), { g });
    }

    // Pestana «Ayuda»: que ver y que probar, pestana por pestana.
    QWidget *pestanaAyuda()
    {
        auto *g = new QGroupBox(tr("Qué ver y qué probar"));
        auto *v = new QVBoxLayout(g);
        auto *ayuda = new QLabel(textoAyuda(), g);
        ayuda->setWordWrap(true);
        ayuda->setTextFormat(Qt::RichText);
        v->addWidget(ayuda);
        return pestana(tr("Guía de lo que hace cada pestaña y cómo comprobarlo."), { g });
    }

    // Construye el panel de la derecha: pestanas (una por tema) y, siempre a la vista debajo,
    // la lectura del raton y de la ultima entidad tocada.
    QWidget *creaPanel()
    {
        auto *tabs = new QTabWidget;
        m_pestanas = tabs;
        tabs->setUsesScrollButtons(true);
        tabs->addTab(pestanaAnillos(), tr("Anillos"));
        tabs->addTab(pestanaSectores(), tr("Sectores"));
        tabs->addTab(pestanaPuntos(), tr("Puntos"));
        tabs->addTab(pestanaObjetivos(), tr("Objetivos"));
        tabs->addTab(pestanaCapas(), tr("Capas"));
        tabs->addTab(pestanaAyuda(), tr("Ayuda"));

        auto *g2 = new QGroupBox(tr("Ratón y último clic"));
        auto *f2 = new QFormLayout(g2);
        m_lblDecimal = new QLabel(QStringLiteral("—"), g2);
        m_lblDms = new QLabel(QStringLiteral("—"), g2);
        m_lblUtm = new QLabel(QStringLiteral("—"), g2);
        m_desdeCentro = new QLabel(QStringLiteral("—"), g2);
        m_desdeCentro->setWordWrap(true);
        QFont negrita = m_desdeCentro->font();
        negrita.setBold(true);
        m_desdeCentro->setFont(negrita);
        m_lblElemento = new QLabel(QStringLiteral("—"), g2);
        m_lblElemento->setWordWrap(true);
        f2->addRow(tr("Decimal"), m_lblDecimal);
        f2->addRow(tr("GMS"), m_lblDms);
        f2->addRow(tr("UTM"), m_lblUtm);
        f2->addRow(tr("Desde el origen"), m_desdeCentro);
        f2->addRow(tr("Entidad tocada"), m_lblElemento);

        // Barra de origen: los anillos y los sectores se miden desde UN punto; aqui se elige cual.
        auto *go = new QGroupBox(tr("Punto de origen (anillos y sectores)"));
        auto *vo = new QVBoxLayout(go);
        m_cmbOrigen = new QComboBox(go);
        vo->addWidget(m_cmbOrigen);
        vo->addWidget(m_soloElegido);
        auto *tip = new QLabel(tr("Toca un punto en el mapa para elegirlo, o agrega puntos en la "
                                  "pestaña Puntos. Cada punto tiene sus anillos y sus sectores."), go);
        tip->setWordWrap(true);
        vo->addWidget(tip);
        connect(m_cmbOrigen, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int i) { seleccionaOrigen(i); });

        auto *panel = new QWidget(this);
        auto *col = new QVBoxLayout(panel);
        col->setContentsMargins(0, 0, 0, 0);
        col->addWidget(go);
        col->addWidget(tabs, 1);
        col->addWidget(g2);
        panel->setFixedWidth(440);
        return panel;
    }

    struct DatosObj {
        QString ident, estado, l2, l3;
    };
    MapWidget *m_mapa = nullptr;
    QVector<Origen> m_origenes;
    int m_cur = -1;                 // origen elegido (indice en m_origenes), o -1
    QComboBox *m_cmbOrigen = nullptr;
    QCheckBox *m_soloElegido = nullptr;
    QGroupBox *m_grupoSectores = nullptr;
    QHash<qint64, DatosObj> m_datosObj;
    QTimer m_timerSim;
    QTreeWidget *m_arbol = nullptr;
    QTabWidget *m_pestanas = nullptr;
    QListWidget *m_listaElementos = nullptr;
    QComboBox *m_elAncla = nullptr;
    QSpinBox *m_elFuente = nullptr;
    QCheckBox *m_elATodos = nullptr;
    QListWidget *m_listaObjetivos = nullptr;
    QLineEdit *m_obId = nullptr;
    QComboBox *m_obEstado = nullptr;
    QLineEdit *m_obL2 = nullptr;
    QLineEdit *m_obL3 = nullptr;
    QCheckBox *m_simula = nullptr;
    QPushButton *m_agregaObjetivo = nullptr;
    QComboBox *m_paleta = nullptr;
    int m_modoClic = 0;             // 0 = ninguno; 1 = agregar punto; 2 = agregar objetivo
    QGeoCoordinate m_centro;
    QCheckBox *m_mostrar = nullptr;
    QCheckBox *m_rotulos = nullptr;
    QCheckBox *m_barra = nullptr;
    QCheckBox *m_cuadricula = nullptr;
    QLabel *m_lblCentro = nullptr;
    QPushButton *m_elegir = nullptr;
    QPushButton *m_colocar = nullptr;
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
    QListWidget *m_listaSectores = nullptr;
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
    exige(v.numSectores() == 4 && m->featureTypeCounts().value(QStringLiteral("sector")) == 4,
          "deberia haber cuatro sectores al arrancar", &fallos);
    // Por separado: ocultar uno no toca los demas ni lo borra; cambiarlo cambia solo ese;
    // eliminar uno deja los otros tres; restablecer vuelve a cuatro.
    const qint64 s1 = v.sector(1), s2 = v.sector(2);
    v.ponSectorVisible(s1, false);
    exige(!m->feature(s1)->visible && m->feature(s2)->visible && v.numSectores() == 4,
          "ocultar un sector no debe afectar a los demas ni borrarlo", &fallos);
    v.ponSectorVisible(s1, true);
    exige(m->feature(s1)->visible, "no se volvio a mostrar el sector", &fallos);
    v.cambiaSector(s1, 120.0, 10.0, 80.0);
    exige(qAbs(m->feature(s1)->radiusMeters - 120000.0) < 1.0 && m->feature(s1)->arcEndDeg == 80.0 &&
              qAbs(m->feature(s2)->radiusMeters - 150000.0) < 1.0,
          "cambiar un sector debe tocar solo ese", &fallos);
    v.eliminaSector(s2);
    exige(v.numSectores() == 3 && !m->feature(s2).has_value() &&
              m->featureTypeCounts().value(QStringLiteral("sector")) == 3,
          "eliminar un sector deja los otros", &fallos);
    exige(v.dentroDe(v.sector(0)) >= 0, "no se pudo analizar el sector", &fallos);
    v.restableceSectores();
    exige(v.numSectores() == 4 && m->featureTypeCounts().value(QStringLiteral("sector")) == 4,
          "restablecer no dejo los cuatro", &fallos);
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
        v.eliminaTodosSectores();
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

    // 11b. Varios puntos, cada uno con SUS anillos y SUS sectores: un segundo origen no toca los del
    //      primero; elegir uno u otro cambia lo que se ve y lo que se edita.
    v.restableceSectores();                      // el paso 11 los quito para ver el sitio
    const int origen0 = v.origenActual();
    const QGeoCoordinate pos0 = m->feature(v.puntoDeOrigen(origen0))->geometry.first();
    exige(v.numOrigenes() == 1 && v.anillos() >= 0, "al arrancar debe haber un origen con anillos", &fallos);
    v.agrega(QGeoCoordinate(22.40, -83.70), QStringLiteral("Punto B"), false);
    exige(v.numOrigenes() == 2 && v.origenActual() == 1, "agregar un punto debe crear y elegir su origen", &fallos);
    exige(v.numSectores() == 0 && m->featureTypeCounts().value(QStringLiteral("sector")) == 4,
          "el origen nuevo no debe heredar los sectores del otro", &fallos);
    const auto rb = m->rangeRings(v.anillos());
    exige(rb && rb->center.distanceTo(QGeoCoordinate(22.40, -83.70)) < 1.0,
          "los anillos del punto B deben estar en el punto B", &fallos);
    // Por defecto se ven los de todos los puntos A LA VEZ; con «solo el elegido», solo los suyos.
    exige(m->rangeRings(v.anillosDeOrigen(origen0)).has_value() && m->rangeRings(v.anillos()).has_value(),
          "por defecto deben verse los anillos de los dos puntos a la vez", &fallos);
    v.casillaSolo()->setChecked(true);
    exige(!m->rangeRings(v.anillosDeOrigen(origen0)).has_value() && v.anillos() >= 0,
          "con «solo el elegido» los anillos del otro punto no se ven", &fallos);
    v.casillaSolo()->setChecked(false);
    v.creaSector(false, 80, 10, 100, QColor(0xc6, 0x28, 0x28), QStringLiteral("B · Sector 10–100"));
    exige(v.numSectores() == 1 && m->featureTypeCounts().value(QStringLiteral("sector")) == 5,
          "el sector va al punto elegido", &fallos);
    exige(m->feature(v.sectorDeOrigen(origen0, 0))->visible && m->feature(v.sector(0))->visible,
          "los sectores de los dos puntos deben verse a la vez", &fallos);
    v.seleccionaOrigen(origen0);
    exige(v.numSectores() == 4 && v.anillos() >= 0 &&
              m->rangeRings(v.anillos())->center.distanceTo(pos0) < 1.0,
          "al elegir el primer punto vuelven sus anillos y sus 4 sectores", &fallos);

    // 12. Objetivos: agregar uno, cambiar su estado (el color del segmento cambia, la traza no
    //     se pierde) y quitarlo.
    const int antesObj = v.objetivos().size();
    const qint64 nuevoObj = v.agregaObjetivo(kHabana.atDistanceAndAzimuth(20000.0, 10.0),
                                              QString(), 45.0, QStringLiteral("DET"));
    exige(v.objetivos().size() == antesObj + 1 && m->target(nuevoObj).has_value(),
          "no se agrego el objetivo", &fallos);
    m->updateTarget(nuevoObj, kHabana.atDistanceAndAzimuth(21000.0, 10.0));
    v.cambiaEstadoObjetivo(nuevoObj, QStringLiteral("EMER"));
    const auto ob = m->target(nuevoObj);
    exige(ob && ob->labelContent.size() == 3 && ob->labelContent.first().segments.size() == 2 &&
              ob->labelContent.first().segments.at(1).text == QStringLiteral("EMER") &&
              ob->labelContent.first().segments.at(1).color == QColor(0xc6, 0x28, 0x28),
          "el estado no cambio el color del rotulo", &fallos);

    // 13. Un elemento de «Sitios» cambia de color por si solo, sin tocar las otras partes.
    v.ponColor(sitios, 0, QColor(0x15, 0x65, 0xc0));
    const auto sit = m->feature(sitios);
    exige(sit && sit->partInfo(0)->colorOverride == QColor(0x15, 0x65, 0xc0) &&
              sit->partInfo(1)->colorOverride != QColor(0x15, 0x65, 0xc0),
          "el color de una parte debe cambiar solo esa", &fallos);

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

    // --captura=imagen.png [--pestana=N]: la ventana con la escala de 600 km, el formulario colgado y el raton
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
                for (const QString &b : app.arguments())
                    if (b.startsWith(QLatin1String("--pestana=")))
                        ventana.ponPestana(b.mid(10).toInt());
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
