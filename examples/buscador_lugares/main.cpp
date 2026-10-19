// buscador_lugares - buscar sitios por nombre o por coordenada, sin internet.
//
// Quinto ejemplo de la serie. Es la base de un callejero offline o de una utilidad
// de coordenadas. Enseña:
//   - Buscar mientras se escribe en el índice de lugares del paquete (searchPlaces):
//     sin tildes ni mayúsculas, ciudades antes que aldeas, sin conexión.
//   - Ir al lugar con el zoom que le corresponde (goToPlace) y marcarlo con un pin.
//   - Lo escrito también vale como coordenada, en cualquier formato (parseCoordinate):
//     decimal, grados y minutos, UTM o MGRS.
//   - La coordenada bajo el cursor en el formato elegido (formatCoordinate), y un
//     clic la copia al portapapeles.
//
//   buscador_lugares [paquete de datos]   (por defecto: <exe>/datos)
//   buscador_lugares <paquete> --comprobar
//        -> sin ventana: busca un nombre, lee una coordenada MGRS y comprueba que
//           el formato de ida y vuelta coincide (sale con 0 si todo está bien).
//
// El índice es el «places» del mapa.json (un lugares.sqlitedb que genera
// herramientas/osm_lugares_a_sqlitedb.py). Sin él, solo funcionan las coordenadas.

#include <libmapa/Coordinates.h>
#include <libmapa/MapWidget.h>
#include <libmapa/Places.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QShortcut>
#include <QStatusBar>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

using namespace libmapa;

namespace {

// Capa del pin de la búsqueda, por encima de las del paquete y temporal (no se guarda).
const QString kCapa = QStringLiteral("busqueda");
const int kOrdenCapa = 1000;

// Un resultado de la lista: un lugar del índice o una coordenada escrita por la persona.
struct Resultado {
    bool esCoordenada = false;
    Place lugar;
    QGeoCoordinate coordenada;
};

} // namespace

// Ventana: a la izquierda el cuadro de búsqueda, la lista de resultados y la
// coordenada bajo el cursor; a la derecha, el mapa.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos y el panel de búsqueda al lado.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        m_mapa->setScaleBarVisible(true);
        m_mapa->addFeatureLayer(kCapa, tr("Búsqueda"), kOrdenCapa);
        m_mapa->setFeatureLayerTransient(kCapa);

        // --- Panel de búsqueda ---------------------------------------------
        m_texto = new QLineEdit(this);
        m_texto->setPlaceholderText(m_mapa->hasPlaces()
            ? tr("Lugar o coordenada (Ctrl+K)")
            : tr("Coordenada (este paquete no trae lugares)"));
        m_texto->setClearButtonEnabled(true);
        m_lista = new QListWidget(this);
        m_formato = new QComboBox(this);
        m_formato->addItem(tr("Decimal"), int(CoordinateFormat::Decimal));
        m_formato->addItem(tr("Grados, min, seg"), int(CoordinateFormat::DMS));
        m_formato->addItem(tr("Grados y minutos"), int(CoordinateFormat::DDM));
        m_formato->addItem(QStringLiteral("UTM"), int(CoordinateFormat::UTM));
        m_formato->addItem(QStringLiteral("MGRS"), int(CoordinateFormat::MGRS));
        m_coordenada = new QLabel(tr("Mueve el ratón sobre el mapa"), this);
        m_coordenada->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *ayuda = new QLabel(tr("Un clic en el mapa copia la coordenada."), this);
        ayuda->setWordWrap(true);

        auto *panel = new QWidget(this);
        panel->setFixedWidth(320);
        auto *col = new QVBoxLayout(panel);
        col->addWidget(m_texto);
        col->addWidget(m_lista, 1);
        col->addWidget(new QLabel(tr("Formato de coordenadas:"), this));
        col->addWidget(m_formato);
        col->addWidget(m_coordenada);
        col->addWidget(ayuda);

        auto *centro = new QWidget(this);
        auto *fila = new QHBoxLayout(centro);
        fila->setContentsMargins(0, 0, 0, 0);
        fila->addWidget(panel);
        fila->addWidget(m_mapa, 1);
        setCentralWidget(centro);
        resize(1200, 760);
        setWindowTitle(tr("Buscador de lugares"));

        // --- Conexiones -----------------------------------------------------
        connect(m_texto, &QLineEdit::textChanged, this, [this](const QString &t) { buscar(t); });
        connect(m_texto, &QLineEdit::returnPressed, this, [this] {
            if (m_lista->count() > 0)
                ir(m_lista->item(0));            // Intro = el primer resultado
        });
        connect(m_lista, &QListWidget::itemActivated, this, [this](QListWidgetItem *i) { ir(i); });
        connect(m_lista, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) { ir(i); });
        connect(m_mapa, &MapWidget::mouseMoved, this, [this](const QGeoCoordinate &c) {
            m_ultima = c;
            mostrarCoordenada();
        });
        connect(m_formato, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { mostrarCoordenada(); });
        connect(m_mapa, &MapWidget::clicked, this, [this](const QGeoCoordinate &c, Qt::MouseButton b) {
            if (b == Qt::LeftButton)
                copiar(c);
        });
        auto *atajo = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
        connect(atajo, &QShortcut::activated, this, [this] {
            m_texto->setFocus();
            m_texto->selectAll();
        });

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
        else if (!m_mapa->hasPlaces())
            statusBar()->showMessage(tr("Sin índice de lugares: añade \"places\" al mapa.json (ver README)."));
    }

    MapWidget *mapa() const { return m_mapa; }

private:
    // Rellena la lista con lo que encaja con \a texto: primero, si se lee como
    // coordenada, esa coordenada; después, los lugares del índice.
    void buscar(const QString &texto)
    {
        m_lista->clear();
        m_resultados.clear();
        if (texto.trimmed().isEmpty())
            return;

        bool ok = false;
        const QGeoCoordinate c = parseCoordinate(texto, &ok);
        if (ok && c.isValid()) {
            Resultado r;
            r.esCoordenada = true;
            r.coordenada = c;
            anadir(r, tr("📍 Coordenada: %1").arg(formatCoordinate(c, CoordinateFormat::Decimal)));
        }
        for (const Place &p : m_mapa->searchPlaces(texto, 12)) {
            Resultado r;
            r.lugar = p;
            const QString pob = p.population > 0
                ? tr(", %1 hab.").arg(QLocale().toString(p.population)) : QString();
            anadir(r, tr("%1 — %2%3").arg(p.name, p.typeLabel(), pob));
        }
    }

    // Añade un resultado a la lista, con su texto.
    void anadir(const Resultado &r, const QString &texto)
    {
        auto *item = new QListWidgetItem(texto, m_lista);
        item->setData(Qt::UserRole, int(m_resultados.size()));
        m_resultados.append(r);
    }

    // Va al resultado elegido y lo marca con un pin.
    void ir(QListWidgetItem *item)
    {
        const int i = item ? item->data(Qt::UserRole).toInt() : -1;
        if (i < 0 || i >= m_resultados.size())
            return;
        const Resultado &r = m_resultados.at(i);
        if (r.esCoordenada) {
            m_mapa->setCenter(r.coordenada);
            m_mapa->setZoom(14);
            marcar(r.coordenada, formatCoordinate(r.coordenada, CoordinateFormat::Decimal));
        } else {
            m_mapa->goToPlace(r.lugar);          // centra con el zoom que le toca
            marcar(r.lugar.coordinate, r.lugar.name);
        }
    }

    // Pone el pin en \a donde con \a nombre, quitando el de la búsqueda anterior.
    void marcar(const QGeoCoordinate &donde, const QString &nombre)
    {
        if (m_pin >= 0)
            m_mapa->removeFeature(m_pin);
        MapFeature pin;
        pin.kind = GeometryKind::Point;
        pin.layerId = kCapa;
        pin.name = nombre;
        pin.geometry = { donde };
        pin.selectable = false;                  // es una marca, no se edita
        pin.style.pointRadiusPx = 7.0;
        pin.style.lineColor = QColor(0xd3, 0x2f, 0x2f);
        pin.style.fillColor = QColor(0xd3, 0x2f, 0x2f, 200);
        m_pin = m_mapa->addFeature(pin);
        statusBar()->showMessage(tr("%1: %2").arg(nombre, formatCoordinate(donde, formatoActual())), 8000);
    }

    // Formato elegido en el desplegable.
    CoordinateFormat formatoActual() const
    {
        return static_cast<CoordinateFormat>(m_formato->currentData().toInt());
    }

    // Muestra la última coordenada bajo el cursor en el formato elegido.
    void mostrarCoordenada()
    {
        if (m_ultima.isValid())
            m_coordenada->setText(formatCoordinate(m_ultima, formatoActual()));
    }

    // Copia \a c al portapapeles en el formato elegido, y lo dice.
    void copiar(const QGeoCoordinate &c)
    {
        const QString texto = formatCoordinate(c, formatoActual());
        QGuiApplication::clipboard()->setText(texto);
        statusBar()->showMessage(tr("Copiado: %1").arg(texto), 5000);
    }

    MapWidget *m_mapa = nullptr;
    QLineEdit *m_texto = nullptr;
    QListWidget *m_lista = nullptr;
    QComboBox *m_formato = nullptr;
    QLabel *m_coordenada = nullptr;
    QVector<Resultado> m_resultados;
    QGeoCoordinate m_ultima;
    qint64 m_pin = -1;
};

// Prueba sin ventana: busca un nombre en el índice, lee una coordenada MGRS y
// comprueba que formatear y volver a leer da el mismo punto. MGRS con 5 cifras
// tiene 1 m de rejilla y trunca, así que la ida y vuelta puede perder hasta ~1,4 m.
static int comprobar(Ventana &ventana)
{
    QTextStream out(stdout);
    MapWidget *mapa = ventana.mapa();
    out << "listo: " << (mapa->isReady() ? "si" : "no") << "\n";
    out << "indice de lugares: " << (mapa->hasPlaces() ? "si" : "no") << "\n";

    bool ok = false;
    const QGeoCoordinate c = parseCoordinate(QStringLiteral("17Q LF 60056 56750"), &ok);
    const QString mgrs = formatCoordinate(c, CoordinateFormat::MGRS);
    bool ok2 = false;
    const QGeoCoordinate vuelta = parseCoordinate(mgrs, &ok2);
    const double dif = c.distanceTo(vuelta);
    const bool coordOk = ok && ok2 && dif < 2.0;
    out << "MGRS: " << mgrs << " -> " << formatCoordinate(c, CoordinateFormat::Decimal)
        << (coordOk ? " (ida y vuelta ok, " : " (NO coincide, ")
        << QString::number(dif, 'f', 2) << " m de diferencia)\n";

    bool lugarOk = true;
    if (mapa->hasPlaces()) {
        // «guantanamo» sin tilde debe encontrar «Guantánamo», la ciudad antes que la aldea.
        const auto res = mapa->searchPlaces(QStringLiteral("guantanamo"), 5);
        out << "buscar 'guantanamo': " << res.size() << " resultados";
        if (!res.isEmpty())
            out << ", el primero: " << res.first().name << " (" << res.first().typeLabel() << ")";
        out << "\n";
        lugarOk = !res.isEmpty();
    }
    return (mapa->isReady() && coordOk && lugarOk) ? 0 : 1;
}

// Arranca la ventana (o, con --comprobar, la prueba sin ventana).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("buscador_lugares"));

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
    return app.exec();
}
