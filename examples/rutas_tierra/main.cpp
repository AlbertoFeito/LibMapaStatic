// rutas_tierra - probar rutas por tierra (por la red vial) sin conexión, con libmapa.
//
// Octavo ejemplo de la serie. Es la base de un planificador de rutas offline y sirve
// para ver cómo se comporta el cálculo con caminos de tierra. Enseña:
//   - Calcular la ruta más rápida en coche entre dos puntos (MapWidget::findRoute): la red
//     vial se lee de las teselas del MBTiles vectorial al vuelo, sin preparar nada.
//   - Poner el origen A y el destino B con dos clics, escribiéndolos (parseCoordinate) o
//     ARRASTRANDO los marcadores: la ruta se recalcula al soltar.
//   - Pintar la ruta por tramos de un mismo tipo de vía (RouteResult::legs): el asfalto en
//     azul y los caminos de tierra en marrón y discontinuos, para ver de un vistazo cuánto
//     de la ruta es tierra.
//   - El reparto por tipo de vía (kilómetros y %), el tiempo estimado y lo que hubo que
//     andar del punto elegido a la vía (RouteResult::byClass, snapFromM, snapToM).
//   - Guardar la ruta a GPX, KML o GeoJSON (exportVectorFile).
//
//   rutas_tierra [paquete de datos] [--demo]   (por defecto: <exe>/datos; si no existe, la demo)
//   rutas_tierra --comprobar
//        -> sin ventana: arma la red de demostración, calcula tres rutas conocidas, comprueba
//           distancias, tramos, tipos de vía y la exportación (sale con 0 si todo está bien).
//
// El paquete de datos necesita un MBTiles VECTORIAL (pbf, p. ej. el de OpenMapTiles) como capa
// base o como "vectorOverlay". La demo trae el suyo: un pueblo al oeste, otro al este, un camino
// de tierra entre los dos, un ramal a una finca y una carretera de asfalto dando la vuelta.
//
// El tiempo es una ESTIMACIÓN por la velocidad media de cada tipo de vía (un camino de tierra,
// 15 km/h): las teselas de OpenMapTiles no traen giros prohibidos ni límites de velocidad reales.

#include <libmapa/Coordinates.h>
#include <libmapa/MapWidget.h>
#include <libmapa/Places.h>

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVariant>

#include <cmath>
#include <memory>

using namespace libmapa;

namespace {

// Capas del mapa: la ruta debajo y los marcadores A y B encima.
const QString kCapaRuta = QStringLiteral("ruta");
const QString kCapaPuntos = QStringLiteral("puntos");
const QString kCapaAlcance = QStringLiteral("alcance");

// Nombre en español de un tipo de vía de OpenMapTiles (el tipo tal cual si no lo conoce).
QString nombreVia(const QString &tipo)
{
    static const QHash<QString, QString> nombres = {
        { QStringLiteral("motorway"), QObject::tr("autopista") },
        { QStringLiteral("trunk"), QObject::tr("vía troncal") },
        { QStringLiteral("primary"), QObject::tr("carretera principal") },
        { QStringLiteral("secondary"), QObject::tr("carretera secundaria") },
        { QStringLiteral("tertiary"), QObject::tr("carretera terciaria") },
        { QStringLiteral("minor"), QObject::tr("calle") },
        { QStringLiteral("service"), QObject::tr("vía de servicio") },
        { QStringLiteral("track"), QObject::tr("camino de tierra") },
    };
    return nombres.value(tipo, tipo);
}

// ¿Es de tierra? En OpenMapTiles, "track" son las pistas y caminos rurales (de tierra casi siempre).
bool esTierra(const QString &tipo)
{
    return tipo == QLatin1String("track") || tipo == QLatin1String("path");
}

// Distancia legible: metros por debajo de 1 km, kilómetros con dos decimales por encima.
QString textoDistancia(double metros)
{
    return metros < 1000.0 ? QObject::tr("%1 m").arg(qRound(metros))
                           : QObject::tr("%1 km").arg(metros / 1000.0, 0, 'f', 2);
}

// Tiempo legible: segundos si es menos de un minuto, minutos y, desde la hora, horas y minutos.
QString textoDuracion(double segundos)
{
    if (segundos < 60.0)
        return QObject::tr("%1 s").arg(qRound(segundos));
    const int min = qRound(segundos / 60.0);
    return min >= 60 ? QObject::tr("%1 h %2 min").arg(min / 60).arg(min % 60) : QObject::tr("%1 min").arg(min);
}

// ---------------------------------------------------------------------------------------
// Red de demostración: un MBTiles vectorial con UNA tesela z14 y una capa `transportation`.
// Es lo mínimo que lee libmapa (el MVT de OpenMapTiles), escrito a mano para no depender
// de ningún dato: así el ejemplo se prueba en cualquier PC.
// ---------------------------------------------------------------------------------------

// Varint de protobuf (7 bits por byte, el bit alto dice «sigue»).
void varint(QByteArray &b, quint64 v)
{
    while (v >= 0x80) {
        b.append(char((v & 0x7f) | 0x80));
        v >>= 7;
    }
    b.append(char(v));
}

// Campo de protobuf con longitud (texto o submensaje).
void campoBytes(QByteArray &b, int campo, const QByteArray &datos)
{
    varint(b, quint64((campo << 3) | 2));
    varint(b, quint64(datos.size()));
    b.append(datos);
}

// Campo de protobuf entero.
void campoEntero(QByteArray &b, int campo, quint64 v)
{
    varint(b, quint64(campo << 3));
    varint(b, v);
}

// Entero con signo en zigzag, como pide la geometría del MVT.
quint64 zigzag(int v)
{
    return quint64((v << 1) ^ (v >> 31));
}

// Una vía de la demo: tipo, vértices en unidades de la tesela (0..4096) y sentido único (1 = como se dibuja).
struct ViaDemo
{
    QString tipo;
    QVector<QPoint> puntos;
    int sentido = 0;
};

// La red de la demo. Dos pueblos, uno al oeste (300, 1500) y otro al este (3800, 1500), unidos por un
// camino de tierra casi recto (unos 1,9 km). Por el sur los une una calle que da la vuelta (unos 4,6 km),
// con una carretera principal que sube desde ella hasta el camino. Y un ramal de tierra sin salida,
// al norte, lleva a una finca (2048, 600).
QVector<ViaDemo> redDemo()
{
    return {
        { QStringLiteral("track"), { { 300, 1500 }, { 1200, 1450 }, { 2048, 1500 }, { 2900, 1550 }, { 3800, 1500 } }, 0 },
        { QStringLiteral("track"), { { 2048, 1500 }, { 2048, 1050 }, { 2048, 600 } }, 0 },
        { QStringLiteral("minor"), { { 300, 1500 }, { 300, 2700 }, { 300, 3900 } }, 0 },
        { QStringLiteral("minor"), { { 300, 3900 }, { 2048, 3900 }, { 3800, 3900 } }, 0 },
        { QStringLiteral("minor"), { { 3800, 3900 }, { 3800, 2700 }, { 3800, 1500 } }, 0 },
        { QStringLiteral("primary"), { { 2048, 3900 }, { 2048, 2700 }, { 2048, 1500 } }, 0 },
    };
}

// La tesela z14 de la demo, con la geografía de las coordenadas de abajo (cerca de Cienfuegos).
struct TeselaDemo
{
    int z = 14, x = 0, y = 0;

    // Calcula la tesela que contiene (22.0, -80.0).
    TeselaDemo()
    {
        const double pi = 3.14159265358979323846;
        const double n = double(1 << z);
        x = int(std::floor((-80.0 + 180.0) / 360.0 * n));
        y = int(std::floor((1.0 - std::asinh(std::tan(22.0 * pi / 180.0)) / pi) / 2.0 * n));
    }

    // Coordenada geográfica de un punto de la tesela, en unidades de 0 a 4096.
    QGeoCoordinate punto(double ux, double uy) const
    {
        const double pi = 3.14159265358979323846;
        const double n = double(1 << z);
        return QGeoCoordinate(std::atan(std::sinh(pi * (1.0 - 2.0 * (y + uy / 4096.0) / n))) * 180.0 / pi,
                              (x + ux / 4096.0) / n * 360.0 - 180.0);
    }
};

// Los bytes del MVT de la demo: una capa `transportation` con las vías de redDemo().
QByteArray teselaMvt()
{
    const QVector<ViaDemo> vias = redDemo();
    const QStringList claves{ QStringLiteral("class"), QStringLiteral("oneway") };
    QStringList valores;
    const auto indiceValor = [&valores](const QString &v) {
        int i = int(valores.indexOf(v));
        if (i < 0) {
            valores << v;
            i = int(valores.size()) - 1;
        }
        return i;
    };

    QByteArray capa;
    campoEntero(capa, 15, 2);
    campoBytes(capa, 1, QByteArray("transportation"));
    for (const ViaDemo &v : vias) {
        // Geometría: MoveTo al primer vértice y LineTo con los demás, en desplazamientos zigzag.
        QByteArray geom;
        varint(geom, 1 | (1 << 3));
        varint(geom, zigzag(v.puntos[0].x()));
        varint(geom, zigzag(v.puntos[0].y()));
        varint(geom, 2 | (quint64(v.puntos.size() - 1) << 3));
        for (int i = 1; i < v.puntos.size(); ++i) {
            varint(geom, zigzag(v.puntos[i].x() - v.puntos[i - 1].x()));
            varint(geom, zigzag(v.puntos[i].y() - v.puntos[i - 1].y()));
        }
        QByteArray etiquetas;
        varint(etiquetas, 0);
        varint(etiquetas, quint64(indiceValor(v.tipo)));
        if (v.sentido != 0) {
            varint(etiquetas, 1);
            varint(etiquetas, quint64(indiceValor(QString::number(v.sentido))));
        }
        QByteArray entidad;
        campoBytes(entidad, 2, etiquetas);
        campoEntero(entidad, 3, 2);                         // LineString
        campoBytes(entidad, 4, geom);
        campoBytes(capa, 2, entidad);
    }
    for (const QString &k : claves)
        campoBytes(capa, 3, k.toUtf8());
    for (const QString &v : valores) {
        QByteArray valor;
        campoBytes(valor, 1, v.toUtf8());
        campoBytes(capa, 4, valor);
    }
    campoEntero(capa, 5, 4096);
    QByteArray tesela;
    campoBytes(tesela, 3, capa);
    return tesela;
}

// Escribe en \a carpeta el paquete de demostración: demo.mbtiles (una tesela vectorial) y su
// mapa.json. \return false si no se pudo (el motivo en \a error).
bool escribirDemo(const QString &carpeta, QString *error)
{
    const TeselaDemo t;
    const QString ruta = carpeta + QStringLiteral("/demo.mbtiles");
    const QString conexion = QStringLiteral("rutas_tierra_demo");
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conexion);
        db.setDatabaseName(ruta);
        if (!db.open()) {
            *error = QObject::tr("No se pudo crear %1").arg(ruta);
            return false;
        }
        QSqlQuery q(db);
        ok = q.exec(QStringLiteral("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, "
                                   "tile_row INTEGER, tile_data BLOB)"))
             && q.exec(QStringLiteral("CREATE TABLE metadata (name TEXT, value TEXT)"))
             && q.exec(QStringLiteral("INSERT INTO metadata VALUES ('format', 'pbf'), ('minzoom', '14'), "
                                      "('maxzoom', '14'), ('name', 'Caminos de demostración')"));
        q.prepare(QStringLiteral("INSERT INTO tiles VALUES (:z, :x, :y, :d)"));
        q.bindValue(QStringLiteral(":z"), t.z);
        q.bindValue(QStringLiteral(":x"), t.x);
        q.bindValue(QStringLiteral(":y"), (1 << t.z) - 1 - t.y);      // MBTiles guarda las filas en TMS
        q.bindValue(QStringLiteral(":d"), teselaMvt());
        ok = ok && q.exec();
        db.close();
    }
    QSqlDatabase::removeDatabase(conexion);
    if (!ok) {
        *error = QObject::tr("No se pudo escribir la tesela de demostración");
        return false;
    }

    const QGeoCoordinate no = t.punto(0, 0), se = t.punto(4096, 4096), centro = t.punto(2048, 2048);
    const QJsonObject manifiesto{
        { QStringLiteral("format"), QStringLiteral("libmapa-package") },
        { QStringLiteral("version"), 2 },
        { QStringLiteral("package"), QJsonObject{
              { QStringLiteral("id"), QStringLiteral("rutas-demo") },
              { QStringLiteral("name"), QStringLiteral("Demo de rutas por tierra") },
              { QStringLiteral("bounds"), QJsonObject{ { QStringLiteral("north"), no.latitude() },
                                                      { QStringLiteral("west"), no.longitude() },
                                                      { QStringLiteral("south"), se.latitude() },
                                                      { QStringLiteral("east"), se.longitude() } } } } },
        { QStringLiteral("start"), QJsonObject{ { QStringLiteral("layer"), QStringLiteral("demo") },
                                               { QStringLiteral("center"), QJsonArray{ centro.latitude(), centro.longitude() } },
                                               { QStringLiteral("zoom"), 14 } } },
        { QStringLiteral("datasets"), QJsonArray{ QJsonObject{
              { QStringLiteral("id"), QStringLiteral("demo") },
              { QStringLiteral("displayName"), QStringLiteral("Caminos de demostración") },
              { QStringLiteral("filePath"), QStringLiteral("demo.mbtiles") } } } },
    };
    QFile f(carpeta + QStringLiteral("/mapa.json"));
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("No se pudo escribir mapa.json");
        return false;
    }
    f.write(QJsonDocument(manifiesto).toJson());
    return true;
}

} // namespace

// Ventana: a la izquierda los puntos A y B, el resultado y el reparto por tipo de vía; a la
// derecha, el mapa con la ruta pintada por tramos.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos (o la demo) y el panel de la ruta al lado.
    Ventana(const QString &datos, bool demo) : m_demo(demo)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        m_mapa->setScaleBarVisible(true);
        m_mapa->addFeatureLayer(kCapaAlcance, tr("Alcance de B"), 900);
        m_mapa->addFeatureLayer(kCapaRuta, tr("Ruta"), 1000);
        m_mapa->addFeatureLayer(kCapaPuntos, tr("Puntos"), 1100);
        m_mapa->setFeatureLayerTransient(kCapaPuntos);

        // --- Panel ----------------------------------------------------------
        auto *intro = new QLabel(tr("Clic en el mapa: <b>A</b> (origen) y <b>B</b> (destino). Arrastra los "
                                    "marcadores para recalcular. Los tramos <b>marrones discontinuos</b> son "
                                    "caminos de tierra."), this);
        intro->setWordWrap(true);
        m_a = new QLineEdit(this);
        m_a->setPlaceholderText(tr("A: lat, lon (o UTM, MGRS…)"));
        m_b = new QLineEdit(this);
        m_b->setPlaceholderText(tr("B: lat, lon (o UTM, MGRS…)"));
        auto *calcularBtn = new QPushButton(tr("Calcular"), this);
        auto *invertir = new QPushButton(tr("Invertir"), this);
        auto *limpiar = new QPushButton(tr("Limpiar"), this);
        m_ejemplos = new QComboBox(this);
        m_ejemplos->addItem(tr("Rutas de ejemplo de la demo…"));
        m_ejemplos->addItem(tr("Pueblo oeste → pueblo este (¿tierra o asfalto?)"));
        m_ejemplos->addItem(tr("Cruce de la carretera → finca (asfalto y tierra)"));
        m_ejemplos->addItem(tr("Pueblo oeste → finca (todo de tierra)"));
        m_ejemplos->setEnabled(m_demo);
        m_resumen = new QLabel(tr("Sin ruta."), this);
        m_resumen->setWordWrap(true);
        m_resumen->setTextFormat(Qt::RichText);
        m_tabla = new QTableWidget(0, 3, this);
        m_tabla->setHorizontalHeaderLabels({ tr("Tipo de vía"), tr("km"), tr("%") });
        m_tabla->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_tabla->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tabla->setSelectionMode(QAbstractItemView::NoSelection);
        m_tabla->verticalHeader()->hide();
        m_alcance = new QLabel(this);
        m_alcance->setWordWrap(true);
        m_alcance->setTextFormat(Qt::RichText);
        m_aviso = new QLabel(this);
        m_aviso->setWordWrap(true);
        m_aviso->setTextFormat(Qt::RichText);
        m_exportar = new QPushButton(tr("Guardar la ruta (GPX/KML/GeoJSON)…"), this);
        m_exportar->setEnabled(false);

        auto *panel = new QWidget(this);
        panel->setFixedWidth(360);
        auto *col = new QVBoxLayout(panel);
        col->addWidget(intro);
        col->addWidget(m_a);
        col->addWidget(m_b);
        auto *botones = new QHBoxLayout;
        botones->addWidget(calcularBtn);
        botones->addWidget(invertir);
        botones->addWidget(limpiar);
        col->addLayout(botones);
        col->addWidget(m_ejemplos);
        col->addWidget(m_alcance);
        col->addWidget(m_resumen);
        col->addWidget(m_tabla, 1);
        col->addWidget(m_aviso);
        col->addWidget(m_exportar);

        auto *centro = new QWidget(this);
        auto *fila = new QHBoxLayout(centro);
        fila->setContentsMargins(0, 0, 0, 0);
        fila->addWidget(panel);
        fila->addWidget(m_mapa, 1);
        setCentralWidget(centro);
        resize(1250, 780);
        setWindowTitle(tr("Rutas por tierra"));

        // --- Conexiones -----------------------------------------------------
        connect(calcularBtn, &QPushButton::clicked, this, [this] { desdeTexto(); });
        connect(m_a, &QLineEdit::returnPressed, this, [this] { desdeTexto(); });
        connect(m_b, &QLineEdit::returnPressed, this, [this] { desdeTexto(); });
        connect(invertir, &QPushButton::clicked, this, [this] { invertirPuntos(); });
        connect(limpiar, &QPushButton::clicked, this, [this] { limpiarTodo(); });
        connect(m_exportar, &QPushButton::clicked, this, [this] { guardar(); });
        connect(m_ejemplos, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { ejemplo(i); });
        connect(m_mapa, &MapWidget::clicked, this, [this](const QGeoCoordinate &c, Qt::MouseButton b) {
            // Un clic sobre un marcador (para arrastrarlo) no pone un punto nuevo.
            if (b == Qt::LeftButton && m_mapa->hitTest(m_mapa->mapFromGlobal(QCursor::pos())).isEmpty())
                clicEnMapa(c);
        });
        connect(m_mapa, &MapWidget::featureDragFinished, this, [this](qint64 id) { marcadorSoltado(id); });

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
        else if (!m_mapa->canSearchPointsOfInterest())
            statusBar()->showMessage(tr("Este paquete no tiene un mapa vectorial (MBTiles pbf): no hay red vial."));
        else if (m_demo)
            statusBar()->showMessage(tr("Red de demostración. Prueba las rutas de ejemplo del desplegable."), 10000);
    }

    MapWidget *mapa() const { return m_mapa; }

    // Coordenada de un punto de la red de demostración (unidades de tesela, 0..4096).
    static QGeoCoordinate puntoDemo(double ux, double uy) { return TeselaDemo().punto(ux, uy); }

    // Calcula la ruta de \a a a \a b: la pinta por tramos, rellena el panel y la encuadra. Con
    // \a progreso muestra una barra que se puede cancelar. \return el resultado (con su error si no hay ruta).
    RouteResult calcular(const QGeoCoordinate &a, const QGeoCoordinate &b, bool progreso)
    {
        m_mapa->clearFeatureLayer(kCapaRuta);
        m_exportar->setEnabled(false);
        QElapsedTimer reloj;
        reloj.start();

        std::unique_ptr<QProgressDialog> barra;
        if (progreso) {
            barra = std::make_unique<QProgressDialog>(tr("Leyendo la red vial…"), tr("Cancelar"), 0, 100, this);
            barra->setWindowModality(Qt::WindowModal);
            barra->setMinimumDuration(400);                 // las rutas cortas no parpadean
        }
        const RouteResult r = m_mapa->findRoute(a, b, [&barra](int hechas, int total) {
            if (!barra)
                return true;
            barra->setMaximum(qMax(1, total));
            barra->setValue(hechas);
            QApplication::processEvents();
            return !barra->wasCanceled();
        });
        barra.reset();

        if (!r.ok()) {
            m_resumen->setText(tr("<span style='color:#b00020'><b>Sin ruta.</b> %1</span>").arg(r.error.toHtmlEscaped()));
            m_tabla->setRowCount(0);
            m_aviso->clear();
            return r;
        }
        pintar(r, a, b);
        resumir(r, reloj.elapsed());
        m_exportar->setEnabled(true);
        return r;
    }

    // Cuántos trazos hay pintados en la capa de la ruta (para la prueba sin ventana).
    int tramosPintados() const { return int(m_mapa->featuresInLayer(kCapaRuta).size()); }

    // Guarda la última ruta en \a ruta (el formato sale de la extensión). \return false si falló.
    bool exportar(const QString &ruta, QString *error) { return m_mapa->exportVectorFile(ruta, kCapaRuta, error); }

private:
    // Pinta la ruta: un trazo por tramo de un mismo tipo de vía (asfalto azul, tierra marrón y discontinuo)
    // y, finos y punteados, lo que se anda del punto elegido a la vía.
    void pintar(const RouteResult &r, const QGeoCoordinate &a, const QGeoCoordinate &b)
    {
        for (const RouteLeg &leg : r.legs) {
            MapFeature f;
            f.kind = GeometryKind::Polyline;
            f.layerId = kCapaRuta;
            f.type = leg.type;
            f.name = tr("%1 · %2").arg(nombreVia(leg.type), textoDistancia(leg.lengthM));
            f.geometry = leg.path;
            f.selectable = false;
            f.style.labelVisible = false;
            f.style.lineWidth = 6.0;
            if (esTierra(leg.type)) {
                f.style.lineColor = QColor(0x8d, 0x55, 0x24);
                f.style.lineStyle = Qt::DashLine;
            } else {
                f.style.lineColor = QColor(0x15, 0x65, 0xc0, 220);
            }
            f.attributes.insert(QStringLiteral("tipo"), leg.type);
            f.attributes.insert(QStringLiteral("tierra"), esTierra(leg.type));
            f.attributes.insert(QStringLiteral("longitud_m"), qRound(leg.lengthM));
            m_mapa->addFeature(f);
        }
        // Enganche: del punto elegido a la vía, y de la vía al destino. Se anda, no se conduce.
        const auto enlace = [this](const QGeoCoordinate &desde, const QGeoCoordinate &hasta) {
            if (desde.distanceTo(hasta) < 1.0)
                return;
            MapFeature f;
            f.kind = GeometryKind::Polyline;
            f.layerId = kCapaRuta;
            f.type = QStringLiteral("enganche");
            f.name = tr("A pie hasta la vía");
            f.geometry = { desde, hasta };
            f.selectable = false;
            f.style.labelVisible = false;
            f.style.lineWidth = 2.0;
            f.style.lineStyle = Qt::DotLine;
            f.style.lineColor = QColor(0x60, 0x60, 0x60);
            m_mapa->addFeature(f);
        };
        if (!r.legs.isEmpty()) {
            enlace(a, r.legs.first().path.first());
            enlace(r.legs.last().path.last(), b);
        }

        // Encuadra la ruta entera, con un poco de aire.
        double s = 90, n = -90, w = 180, e = -180;
        for (const QGeoCoordinate &c : r.path) {
            s = qMin(s, c.latitude());  n = qMax(n, c.latitude());
            w = qMin(w, c.longitude()); e = qMax(e, c.longitude());
        }
        const double aireLat = qMax(0.0004, (n - s) * 0.12), aireLon = qMax(0.0004, (e - w) * 0.12);
        m_mapa->fitBounds(QGeoCoordinate(n + aireLat, w - aireLon), QGeoCoordinate(s - aireLat, e + aireLon));
    }

    // Rellena el panel: distancia, tiempo, enganche, tabla por tipo de vía y el aviso de tierra.
    void resumir(const RouteResult &r, qint64 ms)
    {
        double tierra = 0.0;
        for (const auto &c : r.byClass)
            if (esTierra(c.first))
                tierra += c.second;
        const double enRuta = r.distanceM - r.snapFromM - r.snapToM;       // lo conducido
        m_resumen->setText(tr("<b>%1</b> en coche (estimado) · <b>%2</b><br>Enganche: %3 al origen, %4 al destino<br>"
                              "<small>%5 teselas, %6 nodos, %7 ms</small>")
                               .arg(textoDuracion(r.durationS), textoDistancia(r.distanceM),
                                    textoDistancia(r.snapFromM), textoDistancia(r.snapToM))
                               .arg(r.tiles).arg(r.nodes).arg(ms));

        m_tabla->setRowCount(int(r.byClass.size()));
        for (int i = 0; i < r.byClass.size(); ++i) {
            const auto &c = r.byClass[i];
            const double pct = enRuta > 0.0 ? c.second / enRuta * 100.0 : 0.0;
            const QStringList celdas{ nombreVia(c.first), QString::number(c.second / 1000.0, 'f', 2),
                                      QString::number(pct, 'f', 0) };
            for (int k = 0; k < 3; ++k) {
                auto *item = new QTableWidgetItem(celdas[k]);
                if (k > 0)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (esTierra(c.first))
                    item->setBackground(QColor(0xef, 0xdc, 0xc8));
                m_tabla->setItem(i, k, item);
            }
        }
        if (tierra > 0.0) {
            m_aviso->setText(tr("<span style='color:#8d5524'><b>⚠ %1 por caminos de tierra (%2 %)</b>: con un coche bajo "
                                "o con lluvia puede no pasar.</span>")
                                 .arg(textoDistancia(tierra)).arg(enRuta > 0.0 ? qRound(tierra / enRuta * 100.0) : 0));
        } else {
            m_aviso->setText(tr("<span style='color:#2e7d32'>Toda la ruta va por vías asfaltadas o de servicio.</span>"));
        }
    }

    // Un clic en el mapa: el primero pone A, el segundo B (y calcula); el tercero empieza otra ruta.
    void clicEnMapa(const QGeoCoordinate &c)
    {
        if (!m_aPuesto || m_bPuesto) {
            m_mapa->clearFeatureLayer(kCapaRuta);
            fijar(true, c);
            m_bPuesto = false;
            quitarMarcador(m_idB);
            m_b->clear();
            m_resumen->setText(tr("Origen fijado. Clic para poner el destino."));
            m_tabla->setRowCount(0);
            m_aviso->clear();
            m_exportar->setEnabled(false);
        } else {
            fijar(false, c);
            calcular(m_puntoA, m_puntoB, true);
        }
    }

    // Pone A o B en \a c: guarda el punto, escribe el campo y coloca (o mueve) el marcador.
    void fijar(bool esA, const QGeoCoordinate &c)
    {
        (esA ? m_puntoA : m_puntoB) = c;
        (esA ? m_aPuesto : m_bPuesto) = true;
        (esA ? m_a : m_b)->setText(formatCoordinate(c, CoordinateFormat::Decimal));
        qint64 &id = esA ? m_idA : m_idB;
        quitarMarcador(id);
        MapFeature f;
        f.kind = GeometryKind::Point;
        f.layerId = kCapaPuntos;
        f.name = esA ? QStringLiteral("A") : QStringLiteral("B");
        f.geometry = { c };
        f.draggable = true;                                  // arrastrar el marcador recalcula la ruta
        f.style.pointRadiusPx = 10.0;
        f.style.lineColor = esA ? QColor(0x2e, 0x7d, 0x32) : QColor(0xc6, 0x28, 0x28);
        f.style.fillColor = f.style.lineColor;
        id = m_mapa->addFeature(f);
        if (esA)
            pintarAlcance(c);
    }

    // Dibuja alrededor de \a a el contorno de hasta dónde puede estar B: más allá, findRoute dice «ruta
    // demasiado larga» porque habría que leer más de kMaxRouteTiles teselas. No es un círculo: la zona
    // que se lee es un rectángulo de teselas, y en línea recta llega más lejos que en diagonal.
    void pintarAlcance(const QGeoCoordinate &a)
    {
        m_mapa->clearFeatureLayer(kCapaAlcance);
        m_alcance->clear();
        const QVector<QGeoCoordinate> borde = m_mapa->routeReach(a, 72);
        if (borde.isEmpty())
            return;
        MapFeature f;
        f.kind = GeometryKind::Polygon;
        f.layerId = kCapaAlcance;
        f.name = tr("Alcance de B");
        f.geometry = borde;
        f.selectable = false;
        f.style.labelVisible = false;
        f.style.lineWidth = 2.0;
        f.style.lineStyle = Qt::DashLine;
        f.style.lineColor = QColor(0x2e, 0x7d, 0x32);
        f.style.fillColor = QColor(0x2e, 0x7d, 0x32, 28);
        m_mapa->addFeature(f);

        double minM = 1e18, maxM = 0.0;
        for (const QGeoCoordinate &c : borde) {
            const double d = a.distanceTo(c);
            minM = qMin(minM, d);
            maxM = qMax(maxM, d);
        }
        m_alcance->setText(tr("<span style='color:#2e7d32'><b>B debe caer dentro del contorno verde</b>: a unos "
                              "%1 de A en diagonal y hasta %2 en línea recta (más lejos, la ruta pide leer "
                              "demasiadas teselas).</span>")
                               .arg(textoDistancia(minM), textoDistancia(maxM)));
    }

    // Quita un marcador si existe.
    void quitarMarcador(qint64 &id)
    {
        if (id >= 0)
            m_mapa->removeFeature(id);
        id = -1;
    }

    // Lo escrito en los campos A y B (en cualquier formato de coordenada) pasa a ser la ruta.
    void desdeTexto()
    {
        bool okA = false, okB = false;
        const QGeoCoordinate a = parseCoordinate(m_a->text(), &okA);
        const QGeoCoordinate b = parseCoordinate(m_b->text(), &okB);
        if (!okA || !okB) {
            m_resumen->setText(tr("<span style='color:#b00020'>Escribe A y B como coordenadas "
                                  "(decimal, grados y minutos, UTM o MGRS).</span>"));
            return;
        }
        fijar(true, a);
        fijar(false, b);
        calcular(a, b, true);
    }

    // Cambia A por B y vuelve a calcular.
    void invertirPuntos()
    {
        if (!m_aPuesto || !m_bPuesto)
            return;
        const QGeoCoordinate a = m_puntoB, b = m_puntoA;
        fijar(true, a);
        fijar(false, b);
        calcular(a, b, true);
    }

    // Quita puntos y ruta.
    void limpiarTodo()
    {
        m_mapa->clearFeatureLayer(kCapaRuta);
        m_mapa->clearFeatureLayer(kCapaAlcance);
        m_alcance->clear();
        quitarMarcador(m_idA);
        quitarMarcador(m_idB);
        m_aPuesto = m_bPuesto = false;
        m_a->clear();
        m_b->clear();
        m_resumen->setText(tr("Sin ruta."));
        m_tabla->setRowCount(0);
        m_aviso->clear();
        m_exportar->setEnabled(false);
    }

    // Se soltó un marcador arrastrado: su nueva posición es el punto y se recalcula.
    void marcadorSoltado(qint64 id)
    {
        if (id != m_idA && id != m_idB)
            return;
        const auto f = m_mapa->feature(id);
        if (!f)
            return;
        const bool esA = id == m_idA;
        (esA ? m_puntoA : m_puntoB) = f->position();
        (esA ? m_a : m_b)->setText(formatCoordinate(f->position(), CoordinateFormat::Decimal));
        if (esA)
            pintarAlcance(f->position());
        if (m_aPuesto && m_bPuesto)
            calcular(m_puntoA, m_puntoB, true);
    }

    // Una ruta de ejemplo de la demo (el número es la posición del desplegable).
    void ejemplo(int i)
    {
        QGeoCoordinate a, b;
        switch (i) {
        case 1: a = puntoDemo(300, 1500);  b = puntoDemo(3800, 1500); break;   // pueblo oeste -> pueblo este
        case 2: a = puntoDemo(2048, 3900); b = puntoDemo(2048, 600);  break;   // cruce de la carretera -> finca
        case 3: a = puntoDemo(300, 1500);  b = puntoDemo(2048, 600);  break;   // pueblo oeste -> finca
        default: return;
        }
        fijar(true, a);
        fijar(false, b);
        calcular(a, b, true);
        m_ejemplos->setCurrentIndex(0);
    }

    // Guarda la última ruta en el fichero que se elija (GPX, KML o GeoJSON).
    void guardar()
    {
        const QString ruta = QFileDialog::getSaveFileName(this, tr("Guardar la ruta"),
            QDir::homePath() + QStringLiteral("/ruta.gpx"), tr("GPX (*.gpx);;KML (*.kml);;GeoJSON (*.geojson)"));
        if (ruta.isEmpty())
            return;
        QString error;
        if (exportar(ruta, &error))
            statusBar()->showMessage(tr("Ruta guardada en %1").arg(ruta), 8000);
        else
            QMessageBox::warning(this, tr("Guardar la ruta"), error);
    }

    MapWidget *m_mapa = nullptr;
    bool m_demo = false;
    QLineEdit *m_a = nullptr, *m_b = nullptr;
    QComboBox *m_ejemplos = nullptr;
    QLabel *m_resumen = nullptr;
    QLabel *m_alcance = nullptr;
    QLabel *m_aviso = nullptr;
    QTableWidget *m_tabla = nullptr;
    QPushButton *m_exportar = nullptr;
    QGeoCoordinate m_puntoA, m_puntoB;
    bool m_aPuesto = false, m_bPuesto = false;
    qint64 m_idA = -1, m_idB = -1;
};

// Prueba sin ventana, contra la red de demostración: tres rutas de longitud conocida (el camino de
// tierra entre los pueblos, el ramal a la finca desde la carretera y desde el pueblo), con sus
// tramos por tipo de vía y la exportación a GPX. Sale con 0 si todo está bien.
static int comprobar(Ventana &ventana)
{
    QTextStream out(stdout);
    bool todoBien = ventana.mapa()->isReady() && ventana.mapa()->canSearchPointsOfInterest();
    out << "listo: " << (ventana.mapa()->isReady() ? "si" : "no") << ", mapa vectorial: "
        << (ventana.mapa()->canSearchPointsOfInterest() ? "si" : "no") << "\n";

    // Suma de lo recorrido por un tipo de vía, en metros.
    const auto metros = [](const RouteResult &r, const QString &tipo) {
        double m = 0.0;
        for (const auto &c : r.byClass)
            if (c.first == tipo)
                m += c.second;
        return m;
    };
    // ¿Los tramos van seguidos (cada uno empieza donde acabó el anterior)?
    const auto seguidos = [](const RouteResult &r) {
        for (int i = 1; i < r.legs.size(); ++i)
            if (r.legs[i].path.first().distanceTo(r.legs[i - 1].path.last()) > 0.5)
                return false;
        return !r.legs.isEmpty();
    };
    // Una comprobación con su línea de resultado.
    const auto verificar = [&](const QString &que, bool ok, const QString &detalle) {
        out << (ok ? "  ok  " : "  MAL ") << que << ": " << detalle << "\n";
        todoBien = todoBien && ok;
    };

    // 1) Pueblo oeste -> pueblo este: el camino de tierra (~1,9 km a 15 km/h) gana a dar la vuelta por
    //    asfalto (~4,6 km a 30 km/h), así que casi todo es de tierra.
    const QGeoCoordinate oeste = Ventana::puntoDemo(300, 1500), este = Ventana::puntoDemo(3800, 1500);
    const QGeoCoordinate cruce = Ventana::puntoDemo(2048, 3900), finca = Ventana::puntoDemo(2048, 600);
    const RouteResult r1 = ventana.calcular(oeste, este, false);
    out << "pueblo oeste -> pueblo este: " << (r1.ok() ? textoDistancia(r1.distanceM) : r1.error) << "\n";
    verificar(QStringLiteral("ruta calculada"), r1.ok(), r1.ok() ? textoDuracion(r1.durationS) : r1.error);
    if (r1.ok()) {
        verificar(QStringLiteral("distancia ~1,9 km"), r1.distanceM > 1700.0 && r1.distanceM < 2200.0,
                  textoDistancia(r1.distanceM));
        verificar(QStringLiteral("todo de tierra"), metros(r1, QStringLiteral("track")) > 0.95 * r1.distanceM,
                  textoDistancia(metros(r1, QStringLiteral("track"))));
        verificar(QStringLiteral("un solo tramo, seguido"), r1.legs.size() == 1 && seguidos(r1), QString::number(r1.legs.size()));
    }

    // 2) Cruce de la carretera -> finca: asfalto primero (principal) y tierra al final (el ramal).
    const RouteResult r2 = ventana.calcular(cruce, finca, false);
    out << "cruce -> finca: " << (r2.ok() ? textoDistancia(r2.distanceM) : r2.error) << "\n";
    verificar(QStringLiteral("ruta calculada"), r2.ok(), r2.ok() ? textoDuracion(r2.durationS) : r2.error);
    if (r2.ok()) {
        verificar(QStringLiteral("carretera + tierra"),
                  metros(r2, QStringLiteral("primary")) > 1000.0 && metros(r2, QStringLiteral("track")) > 400.0,
                  QStringLiteral("principal %1, tierra %2").arg(textoDistancia(metros(r2, QStringLiteral("primary"))),
                                                               textoDistancia(metros(r2, QStringLiteral("track")))));
        verificar(QStringLiteral("dos tramos, principal y tierra"),
                  r2.legs.size() == 2 && r2.legs[0].type == QLatin1String("primary")
                      && r2.legs[1].type == QLatin1String("track") && seguidos(r2),
                  QString::number(r2.legs.size()));
    }

    // 3) Pueblo oeste -> finca: solo por tierra (la finca está al final de un ramal sin salida).
    const RouteResult r3 = ventana.calcular(oeste, finca, false);
    out << "pueblo oeste -> finca: " << (r3.ok() ? textoDistancia(r3.distanceM) : r3.error) << "\n";
    verificar(QStringLiteral("solo tierra"), r3.ok() && metros(r3, QStringLiteral("track")) > 0.95 * r3.distanceM,
              r3.ok() ? textoDistancia(metros(r3, QStringLiteral("track"))) : r3.error);

    // 4) La vuelta mide lo mismo (todo es de doble sentido).
    const RouteResult r4 = ventana.calcular(finca, cruce, false);
    verificar(QStringLiteral("la vuelta mide lo mismo"),
              r2.ok() && r4.ok() && std::fabs(r4.distanceM - r2.distanceM) < 5.0,
              r4.ok() ? textoDistancia(r4.distanceM) : r4.error);

    // 5) Guardar la última ruta (finca -> cruce) a GPX y comprobar que el fichero tiene su trazado.
    QTemporaryDir carpeta;
    const QString gpx = carpeta.path() + QStringLiteral("/ruta.gpx");
    QString error;
    const bool guardada = carpeta.isValid() && ventana.exportar(gpx, &error);
    QFile f(gpx);
    const bool contenido = guardada && f.open(QIODevice::ReadOnly) && f.readAll().contains("<trkpt");
    verificar(QStringLiteral("exportar a GPX"), guardada && contenido,
              guardada ? QStringLiteral("%1 bytes").arg(f.size()) : error);
    verificar(QStringLiteral("la ruta está pintada por tramos"), ventana.tramosPintados() >= 1,
              QString::number(ventana.tramosPintados()));

    // El contorno de alcance de B: 72 rumbos; un destino al 90 % del borde no da «demasiado larga» y
    // uno al 120 % sí. En ambos casos el destino es el mismo rumbo visto desde A.
    const QVector<QGeoCoordinate> borde = ventana.mapa()->routeReach(oeste, 72);
    bool alcanceBien = borde.size() == 72;
    QString detalleAlcance = QString::number(borde.size());
    for (int i = 0; alcanceBien && i < borde.size(); i += 9) {
        const double d = oeste.distanceTo(borde[i]), rumbo = oeste.azimuthTo(borde[i]);
        const RouteResult dentro = ventana.mapa()->findRoute(oeste, oeste.atDistanceAndAzimuth(d * 0.9, rumbo));
        const RouteResult fuera = ventana.mapa()->findRoute(oeste, oeste.atDistanceAndAzimuth(d * 1.2, rumbo));
        alcanceBien = d > 10000.0 && !dentro.error.contains(QLatin1String("demasiado larga"))
                      && fuera.error.contains(QLatin1String("demasiado larga"));
        detalleAlcance = QStringLiteral("rumbo %1: %2 m, dentro «%3», fuera «%4»")
                             .arg(rumbo, 0, 'f', 0).arg(d, 0, 'f', 0).arg(dentro.error, fuera.error);
    }
    verificar(QStringLiteral("contorno de alcance de B"), alcanceBien, detalleAlcance);

    out << (todoBien ? "TODO BIEN\n" : "HAY FALLOS\n");
    return todoBien ? 0 : 1;
}

// Arranca la ventana (o, con --comprobar, la prueba sin ventana). Sin paquete de datos, usa la demo.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rutas_tierra"));

    QStringList posicionales;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith(QLatin1String("--")))
            posicionales << a;
    const bool comprobando = app.arguments().contains(QStringLiteral("--comprobar"));

    // Paquete real si lo hay y no se pidió la demo; si no, la demo en una carpeta temporal.
    QString datos = posicionales.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/datos")
                                           : posicionales.first();
    const bool usarDemo = comprobando || app.arguments().contains(QStringLiteral("--demo"))
                          || !QFile::exists(datos + QStringLiteral("/mapa.json"));
    QTemporaryDir carpetaDemo;
    if (usarDemo) {
        QString error;
        if (!carpetaDemo.isValid() || !escribirDemo(carpetaDemo.path(), &error)) {
            QTextStream(stderr) << "No se pudo preparar la demostración: " << error << "\n";
            return 1;
        }
        datos = carpetaDemo.path();
    }

    Ventana ventana(datos, usarDemo);
    if (comprobando)
        return comprobar(ventana);
    ventana.show();
    return app.exec();
}
