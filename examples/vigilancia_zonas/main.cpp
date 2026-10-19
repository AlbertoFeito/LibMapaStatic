// vigilancia_zonas - vigilar zonas del mapa y registrar quién entra, quién sale y cuánto se queda.
//
// Octavo ejemplo de la serie. Es la base de cualquier app que vigila áreas: una zona
// prohibida o de seguridad, un fondeadero, un puerto, una zona de operaciones. La
// librería da la geometría (¿este contacto está dentro de esta zona?) y avisa de cada
// entrada y salida; lo que hace de esto una aplicación es lo que se cuelga de ahí:
//   - DIBUJAR zonas con el ratón y darles reglas (MapTool::DrawPolygon, editar con
//     MapTool::EditFeature): avisar al entrar, avisar al salir y avisar si un contacto
//     pasa más de N minutos dentro (la «permanencia»). Las reglas son de la app, no de
//     la librería: se guardan como atributos de la propia zona.
//   - REGISTRAR cada entrada, salida y permanencia excedida, con su hora, en un
//     historial con filtro «solo avisos» y exportación a CSV.
//   - Una tabla de zonas con cuántos hay dentro ahora y cuántas entradas y salidas llevan.
//   - GUARDAR y ABRIR las zonas (GeoJSON): las reglas viajan con el fichero.
//   - Editar la forma de una zona reevalúa quién queda dentro, sin código nuestro: la
//     librería emite las entradas y salidas que provoque el cambio.
//   - Un GLOSARIO en la interfaz con los términos que usa la ventana.
//
//   vigilancia_zonas [paquete de datos]   (por defecto: <exe>/datos)
//   vigilancia_zonas <paquete> --comprobar
//        -> sin ventana: hace avanzar un reloj simulado y comprueba las entradas, salidas y
//           permanencias de las tres zonas de ejemplo, editar una zona, el CSV, guardar y
//           abrir las zonas y que el glosario define lo que se usa.
//   vigilancia_zonas <paquete> --captura=imagen.png
//        -> abre la ventana, la deja correr unos pasos, guarda una imagen y sale.
//
// El «feed» de contactos es un simulador (la clase Simulador). En un producto real se
// sustituye por el decodificador de AIS, radar o ADS-B: basta llamar a Ventana::recibir
// con cada parte (nombre, posición, rumbo, velocidad, hora).

#include <libmapa/MapWidget.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QPair>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace libmapa;

namespace {

// Metros de una milla náutica.
const double kNM = 1852.0;
// Cada segundo real avanza esto de tiempo simulado, para ver una entrada y una salida
// en medio minuto y no en media hora.
const double kEscala = 30.0;
// Capa donde viven las zonas, por encima de las del paquete.
const QString kCapa = QStringLiteral("zonas");
const int kOrdenCapa = 1000;
// Claves de los atributos de la zona donde se guardan sus reglas (viajan en el GeoJSON).
const QString kAtrEntrada = QStringLiteral("avisar_entrada");
const QString kAtrSalida = QStringLiteral("avisar_salida");
const QString kAtrMaxMin = QStringLiteral("max_min");

// Las reglas de aviso de una zona. La librería solo dice «entró» y «salió»: qué cuenta
// como aviso es de la aplicación.
struct Reglas {
    bool entrada = false;        // avisar al entrar
    bool salida = false;         // avisar al salir
    int maxMin = 0;              // avisar si un contacto pasa más de N min dentro (0 = no)
};

// Un hecho del historial: un contacto entra, sale o pasa más tiempo del permitido.
struct Evento {
    QDateTime hora;              // hora (simulada) del hecho, UTC
    QString zona;
    QString contacto;
    QString tipo;                // "Entra", "Sale" o "Permanencia"
    double permanenciaMin = 0.0; // minutos dentro (en Sale y Permanencia)
    bool aviso = false;          // ¿lo marca una regla de la zona?
};

// Un parte recibido del feed: lo que da un decodificador de AIS/radar/ADS-B.
struct Parte {
    QString nombre;
    QGeoCoordinate posicion;
    double rumbo = 0.0;          // grados verdaderos
    double nudos = 0.0;
    QDateTime hora;              // hora del dato, UTC
};

// Una entrada del glosario: el término y su definición.
struct Termino {
    QString nombre;
    QString definicion;
};

// Una columna de una tabla: su texto y la entrada del glosario que la explica.
struct Columna {
    QString texto;
    QString clave;
};

} // namespace

// Cabeceras de la tabla de zonas y el término del glosario de cada una.
static QVector<Columna> columnasZonas()
{
    return {
        { QObject::tr("Zona"), QStringLiteral("Zona vigilada") },
        { QObject::tr("Reglas"), QStringLiteral("Regla de aviso") },
        { QObject::tr("Dentro"), QStringLiteral("Dentro") },
        { QObject::tr("Entradas"), QStringLiteral("Entrada") },
        { QObject::tr("Salidas"), QStringLiteral("Salida") },
    };
}

// Cabeceras de la tabla de eventos y el término del glosario de cada una.
static QVector<Columna> columnasEventos()
{
    return {
        { QObject::tr("Hora"), QStringLiteral("Tiempo simulado") },
        { QObject::tr("Zona"), QStringLiteral("Zona vigilada") },
        { QObject::tr("Contacto"), QStringLiteral("Contacto") },
        { QObject::tr("Evento"), QStringLiteral("Evento") },
        { QObject::tr("Permanencia (min)"), QStringLiteral("Permanencia") },
        { QObject::tr("Aviso"), QStringLiteral("Aviso") },
    };
}

// El glosario de la interfaz: los términos que usa la ventana (cabeceras, reglas, avisos).
// --comprobar verifica que cada columna y cada término de uso tienen aquí su definición.
static QVector<Termino> glosario()
{
    return {
        { QObject::tr("Zona vigilada"),
          QObject::tr("Un polígono del mapa del que se registra quién entra, quién sale y cuánto se "
                      "queda. Se dibuja con el ratón y se le ponen reglas.") },
        { QObject::tr("Contacto"),
          QObject::tr("Cualquier cosa que el sistema sigue: un buque, una aeronave... Llega como una "
                      "posición con su hora.") },
        { QObject::tr("Entrada"),
          QObject::tr("Un contacto que estaba fuera de la zona y ahora está dentro. Si ya estaba dentro "
                      "cuando se empezó a vigilar la zona, cuenta también como entrada.") },
        { QObject::tr("Salida"),
          QObject::tr("Un contacto que estaba dentro de la zona y ahora está fuera. Borrar un contacto "
                      "no es una salida: ha dejado de existir.") },
        { QObject::tr("Dentro"),
          QObject::tr("Los contactos que están ahora mismo en la zona. «Dentro» es lo que se ve dentro "
                      "en el mapa, con los lados rectos.") },
        { QObject::tr("Permanencia"),
          QObject::tr("El tiempo que un contacto lleva dentro de una zona, en minutos. Al salir queda "
                      "anotado; si supera el máximo de la regla, se avisa una sola vez mientras sigue dentro.") },
        { QObject::tr("Regla de aviso"),
          QObject::tr("Lo que marca un aviso en una zona: avisar al entrar, avisar al salir o avisar si la "
                      "permanencia pasa de N minutos. La librería solo dice «entró» y «salió»; las reglas "
                      "son de la aplicación y se guardan como atributos de la zona.") },
        { QObject::tr("Evento"),
          QObject::tr("Un hecho del historial: «Entra», «Sale» o «Permanencia» (la permanencia máxima se ha "
                      "excedido). Se anotan todos, tengan o no aviso.") },
        { QObject::tr("Aviso"),
          QObject::tr("Un evento que una regla de la zona marca como importante (⚠). El filtro «Solo avisos» "
                      "oculta los demás.") },
        { QObject::tr("Dibujar zona"),
          QObject::tr("Herramienta para trazar un polígono clic a clic (doble clic o clic derecho lo cierra). "
                      "Al cerrarlo se pide el nombre y las reglas.") },
        { QObject::tr("Editar zona"),
          QObject::tr("Herramienta para arrastrar los vértices de una zona. Al cambiar su forma se "
                      "reevalúa quién queda dentro, y se anotan las entradas y salidas que provoque.") },
        { QObject::tr("Tiempo simulado"),
          QObject::tr("Esta demo hace pasar 30 s por cada segundo real, para ver una entrada y una salida en "
                      "medio minuto. Un producto real usa la hora de cada parte.") },
        { QObject::tr("CSV"),
          QObject::tr("Fichero de texto con una fila por evento y las columnas separadas por «;», que abre "
                      "cualquier hoja de cálculo. La hora va en UTC.") },
        { QObject::tr("GeoJSON"),
          QObject::tr("Formato de fichero para guardar las zonas con sus reglas y volver a abrirlas.") },
    };
}

// Texto de las reglas de una zona para la tabla: qué avisa, o «Solo registra».
static QString textoReglas(const Reglas &r)
{
    QStringList partes;
    if (r.entrada)
        partes << QObject::tr("al entrar");
    if (r.salida)
        partes << QObject::tr("al salir");
    if (r.maxMin > 0)
        partes << QObject::tr("más de %1 min dentro").arg(r.maxMin);
    return partes.isEmpty() ? QObject::tr("Solo registra") : QObject::tr("Avisa ") + partes.join(QStringLiteral(" · "));
}

// Las reglas guardadas en los atributos de una zona (sin atributos: ninguna regla).
static Reglas reglasDe(const MapFeature &f)
{
    Reglas r;
    r.entrada = f.attributes.value(kAtrEntrada).toBool();
    r.salida = f.attributes.value(kAtrSalida).toBool();
    r.maxMin = f.attributes.value(kAtrMaxMin).toInt();
    return r;
}

// Escribe las reglas en los atributos de la zona y colorea su contorno según la más grave:
// rojo si avisa al entrar, naranja si al salir, azul si por permanencia, gris si solo registra.
static void ponerReglas(MapFeature *f, const Reglas &r)
{
    f->attributes.insert(kAtrEntrada, r.entrada);
    f->attributes.insert(kAtrSalida, r.salida);
    f->attributes.insert(kAtrMaxMin, r.maxMin);
    QColor c(0x75, 0x75, 0x75);
    if (r.entrada)
        c = QColor(0xd3, 0x2f, 0x2f);
    else if (r.salida)
        c = QColor(0xef, 0x6c, 0x00);
    else if (r.maxMin > 0)
        c = QColor(0x15, 0x65, 0xc0);
    f->style.lineColor = c;
    f->style.fillColor = QColor(c.red(), c.green(), c.blue(), 50);
    f->style.lineWidth = 2.5;
}

// Cuadrado de lado 2·semiladoNM centrado en \a centro: la forma de las zonas de ejemplo.
static QVector<QGeoCoordinate> cuadrado(const QGeoCoordinate &centro, double semiladoNM)
{
    QVector<QGeoCoordinate> v;
    for (double az : { 315.0, 45.0, 135.0, 225.0 })
        v << centro.atDistanceAndAzimuth(semiladoNM * kNM * std::sqrt(2.0), az);
    return v;
}

// Simulador del feed: unos contactos con rumbo y velocidad fijos que avanzan en tiempo
// simulado, pensados para cruzar las tres zonas de ejemplo (ver Ventana::crearZonasDeEjemplo).
// En un producto real, esto es el decodificador.
class Simulador
{
public:
    // Contactos de partida alrededor de (23.20, -82.40). Todos con rumbo fijo; Carguero A se
    // detiene a mitad de camino (dentro del fondeadero) para que supere la permanencia, y el
    // Remolcador se detiene tras salir del puerto.
    Simulador()
    {
        const QGeoCoordinate c(23.20, -82.40);
        const QGeoCoordinate puerto = c.atDistanceAndAzimuth(1.0 * kNM, 270.0);
        const QGeoCoordinate fondeadero = c.atDistanceAndAzimuth(4.0 * kNM, 90.0);
        const auto alta = [this](const QString &n, const QGeoCoordinate &pos, double rumbo,
                                 double nudos, double paraDesde = -1.0) {
            Sim s;
            s.parte.nombre = n;
            s.parte.posicion = pos;
            s.parte.rumbo = rumbo;
            s.parte.nudos = nudos;
            s.paraDesde = paraDesde;
            m_contactos.append(s);
        };
        // Sale del puerto hacia el este y se detiene a los 20 min, fuera de todas las zonas
        // (si siguiera, a los 40 min entraría en el fondeadero y ensuciaría el ejemplo).
        alta(QStringLiteral("Remolcador"), puerto, 90.0, 6.0, 1200.0);
        // Baja desde el norte atravesando la zona restringida.
        alta(QStringLiteral("Pesquero 1"), c.atDistanceAndAzimuth(8.0 * kNM, 0.0), 180.0, 6.0);
        // Llega al fondeadero desde el este y se queda parado en su centro (a los 22,5 min).
        alta(QStringLiteral("Carguero A"), c.atDistanceAndAzimuth(7.0 * kNM, 90.0), 270.0, 8.0, 1350.0);
        // Cruza el fondeadero de norte a sur sin pararse.
        alta(QStringLiteral("Pesquero 2"), fondeadero.atDistanceAndAzimuth(3.0 * kNM, 0.0), 180.0, 6.0);
        // Se aleja de todo: no debe dar ningún evento.
        alta(QStringLiteral("Carguero B"), c.atDistanceAndAzimuth(10.0 * kNM, 200.0), 200.0, 9.0);
    }

    // Avanza \a segundos de tiempo simulado y entrega un parte por contacto, con la hora \a hora.
    void avanzar(double segundos, const QDateTime &hora, const std::function<void(const Parte &)> &entregar)
    {
        m_tiempo += segundos;
        for (Sim &s : m_contactos) {
            if (s.paraDesde >= 0.0 && m_tiempo >= s.paraDesde)
                s.parte.nudos = 0.0;
            s.parte.posicion = s.parte.posicion.atDistanceAndAzimuth(
                s.parte.nudos * kNM / 3600.0 * segundos, s.parte.rumbo);
            s.parte.hora = hora;
            entregar(s.parte);
        }
    }

    // Los contactos con su posición inicial, sin avanzar (el primer parte).
    QVector<Parte> iniciales(const QDateTime &hora) const
    {
        QVector<Parte> partes;
        for (const Sim &s : m_contactos) {
            Parte p = s.parte;
            p.hora = hora;
            partes.append(p);
        }
        return partes;
    }

private:
    struct Sim {
        Parte parte;
        double paraDesde = -1.0;     // segundos simulados a partir de los que se detiene
    };
    QVector<Sim> m_contactos;
    double m_tiempo = 0.0;
};

// Ventana: el mapa al centro, la tabla de zonas a la izquierda, el historial abajo y el
// glosario a la derecha.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos, los paneles, las zonas de ejemplo y las
    // señales de entrada y salida, y arranca el reloj de la simulación.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        cfg.autosaveFeatures = false;            // las zonas se guardan a un fichero, a petición
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        resize(1400, 860);
        setWindowTitle(tr("Vigilancia de zonas"));
        m_mapa->setCenter(QGeoCoordinate(23.20, -82.40));
        m_mapa->setZoom(11);
        m_mapa->setScaleBarVisible(true);
        m_mapa->setScaleBarUnits(ScaleUnits::Nautical);
        m_mapa->addFeatureLayer(kCapa, tr("Zonas"), kOrdenCapa);
        m_mapa->setActiveFeatureLayer(kCapa);
        FeatureStyle borrador;
        borrador.lineColor = QColor(0x75, 0x75, 0x75);
        borrador.lineWidth = 2.5;
        borrador.verticesVisible = true;
        m_mapa->setDraftStyle(borrador);

        // El diálogo de reglas por omisión; las pruebas lo sustituyen (ponerPeticionReglas).
        m_pedirReglas = [this](QString *nombre, Reglas *r) { return dialogoReglas(nombre, r); };

        crearPaneles();
        crearMenus();
        conectarSenales();
        crearZonasDeEjemplo();

        // Reloj simulado: arranca ahora y avanza kEscala segundos por segundo real.
        m_hora = QDateTime::currentDateTimeUtc();
        for (const Parte &p : m_sim.iniciales(m_hora))
            recibir(p);
        refrescarZonas();
        connect(&m_reloj, &QTimer::timeout, this, [this] { paso(kEscala); });
        m_reloj.start(1000);

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
    }

    MapWidget *mapa() const { return m_mapa; }
    // Para las pruebas: quita el reloj real y avanza a mano con paso().
    void detener() { m_reloj.stop(); }
    // Sustituye el diálogo de reglas (que bloquea) por una función; devuelve false = cancelar.
    void ponerPeticionReglas(std::function<bool(QString *, Reglas *)> f) { m_pedirReglas = std::move(f); }

    // Anota un parte del feed: da de alta el contacto si es nuevo o actualiza su posición.
    // Las entradas y salidas llegan solas, por las señales de la librería.
    void recibir(const Parte &p)
    {
        const qint64 id = m_contactos.value(p.nombre, -1);
        if (id < 0) {
            MapTarget t;
            t.label = p.nombre;
            t.kind = QStringLiteral("buque");
            t.position = p.posicion;
            t.headingDeg = p.rumbo;
            m_contactos.insert(p.nombre, m_mapa->addTarget(t, p.hora));
        } else {
            m_mapa->updateTarget(id, p.posicion, p.rumbo, p.hora);
        }
    }

    // Un paso del reloj: avanza \a segundos de tiempo simulado, recibe los partes (la
    // librería emite las entradas y salidas), revisa las permanencias y refresca la tabla.
    void paso(double segundos)
    {
        m_hora = m_hora.addMSecs(qint64(segundos * 1000.0));
        m_sim.avanzar(segundos, m_hora, [this](const Parte &p) { recibir(p); });
        revisarPermanencias();
        refrescarZonas();
    }

    // Crea una zona vigilada con \a nombre, \a poligono y \a reglas: la entidad, sus
    // contadores y, al final, watchZone (que emite una entrada por cada contacto que
    // ya esté dentro, así que los contadores tienen que existir antes). Devuelve su id.
    qint64 crearZona(const QString &nombre, const QVector<QGeoCoordinate> &poligono, const Reglas &reglas)
    {
        MapFeature z;
        z.kind = GeometryKind::Polygon;
        z.layerId = kCapa;
        z.type = QStringLiteral("zona");
        z.name = nombre;
        z.geometry = poligono;
        ponerReglas(&z, reglas);
        const qint64 id = m_mapa->addFeature(z);
        if (id < 0)
            return -1;
        m_estadisticas.insert(id, Estadistica());
        m_mapa->watchZone(id);
        return id;
    }

    // Cambia las reglas de la zona \a id y la recolorea. Devuelve false si no existe.
    bool aplicarReglas(qint64 id, const Reglas &reglas)
    {
        auto f = m_mapa->feature(id);
        if (!f)
            return false;
        ponerReglas(&*f, reglas);
        return m_mapa->updateFeature(*f);
    }

    // Una zona acaba de dibujarse con el ratón: pide el nombre y las reglas y empieza a
    // vigilarla; si se cancela el diálogo, la borra. Es lo que hace la señal featureCreated.
    void alCrearZona(qint64 id)
    {
        auto f = m_mapa->feature(id);
        if (!f || f->layerId != kCapa || f->kind != GeometryKind::Polygon)
            return;
        QString nombre = tr("Zona %1").arg(++m_numZonasDibujadas);
        Reglas reglas;
        reglas.entrada = true;
        if (!m_pedirReglas(&nombre, &reglas)) {
            m_mapa->removeFeature(id);
            return;
        }
        f->name = nombre;
        f->type = QStringLiteral("zona");
        ponerReglas(&*f, reglas);
        m_estadisticas.insert(id, Estadistica());
        m_mapa->updateFeature(*f);
        m_mapa->watchZone(id);
        refrescarZonas();
    }

    // Guarda las zonas (con sus reglas) en \a ruta como GeoJSON. false con aviso si falla.
    bool guardarZonas(const QString &ruta)
    {
        QString error;
        if (!m_mapa->exportVectorFile(ruta, kCapa, &error)) {
            statusBar()->showMessage(tr("No se pudo guardar: %1").arg(error), 8000);
            return false;
        }
        statusBar()->showMessage(tr("Zonas guardadas en %1").arg(ruta), 6000);
        return true;
    }

    // Abre las zonas de \a ruta en lugar de las actuales y las vuelve a vigilar. Los contactos
    // que ya estén dentro producen su entrada, como al empezar a vigilar cualquier zona.
    bool abrirZonas(const QString &ruta)
    {
        borrarTodasLasZonas();
        QString error;
        if (m_mapa->importVectorFile(ruta, kCapa, tr("Zonas"), &error) < 0) {
            statusBar()->showMessage(tr("No se pudo abrir: %1").arg(error), 8000);
            return false;
        }
        for (const MapFeature &f : m_mapa->featuresInLayer(kCapa)) {
            if (f.kind != GeometryKind::Polygon)
                continue;
            auto z = f;
            ponerReglas(&z, reglasDe(f));          // recolorea según las reglas leídas
            m_mapa->updateFeature(z);
            m_estadisticas.insert(f.id, Estadistica());
            m_mapa->watchZone(f.id);
        }
        refrescarZonas();
        return true;
    }

    // Quita todas las zonas y sus contadores (no el historial de eventos).
    void borrarTodasLasZonas()
    {
        m_mapa->clearFeatureLayer(kCapa);
        m_estadisticas.clear();
        m_dentroDesde.clear();
        m_avisadaPermanencia.clear();
        refrescarZonas();
    }

    // Escribe el historial en \a ruta como CSV (separado por «;», hora en UTC ISO): una fila
    // por evento, del más antiguo al más reciente. false si no se puede escribir.
    bool exportarEventos(const QString &ruta) const
    {
        QFile f(ruta);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        // Se escribe como bytes UTF-8: QTextStream::setCodec no existe igual en Qt 5 y Qt 6.
        QString texto = QStringLiteral("hora_utc;zona;contacto;evento;permanencia_min;aviso\n");
        for (const Evento &e : m_eventos)
            texto += QStringLiteral("%1;%2;%3;%4;%5;%6\n")
                         .arg(e.hora.toString(Qt::ISODate), e.zona, e.contacto, e.tipo,
                              QString::number(e.permanenciaMin, 'f', 1), QString::number(e.aviso ? 1 : 0));
        return f.write(texto.toUtf8()) >= 0;
    }

    // Datos para las pruebas.
    const QVector<Evento> &eventos() const { return m_eventos; }
    QTableWidget *tablaZonas() const { return m_tablaZonas; }
    QListWidget *listaGlosario() const { return m_listaGlosario; }
    QDateTime hora() const { return m_hora; }
    // Entradas y salidas acumuladas de la zona \a id.
    int entradas(qint64 id) const { return m_estadisticas.value(id).entradas; }
    int salidas(qint64 id) const { return m_estadisticas.value(id).salidas; }
    // Id de la zona llamada \a nombre, o -1.
    qint64 zonaPorNombre(const QString &nombre) const
    {
        for (const MapFeature &f : m_mapa->featuresInLayer(kCapa))
            if (f.name == nombre && f.kind == GeometryKind::Polygon)
                return f.id;
        return -1;
    }

private:
    // Contadores de una zona: cuántas entradas y salidas se han visto desde que se vigila.
    struct Estadistica {
        int entradas = 0;
        int salidas = 0;
    };

    // Nombre de un contacto, leído del propio objetivo del mapa: las señales de zona llegan
    // DENTRO de addTarget/updateTarget, antes de que recibir() apunte el id del recién creado.
    QString nombreContacto(qint64 id) const
    {
        const auto t = m_mapa->target(id);
        return t ? t->label : QString::number(id);
    }

    // Nombre de la zona \a id, o su número si ya no existe.
    QString nombreZona(qint64 id) const
    {
        const auto f = m_mapa->feature(id);
        return f ? f->name : QString::number(id);
    }

    // Un contacto entra en una zona: cuenta la entrada, guarda desde cuándo está dentro (para
    // la permanencia) y la anota en el historial, con aviso si la regla de la zona lo pide.
    void alEntrar(qint64 contacto, qint64 zona)
    {
        const auto f = m_mapa->feature(zona);
        if (!f)
            return;
        m_estadisticas[zona].entradas++;
        m_dentroDesde.insert(qMakePair(contacto, zona), m_hora);
        registrar(zona, contacto, QStringLiteral("Entra"), 0.0, reglasDe(*f).entrada);
    }

    // Un contacto sale de una zona: cuenta la salida, calcula cuánto estuvo dentro y lo anota,
    // con aviso si la regla de la zona lo pide. Si no se vio entrar, la permanencia queda en 0.
    void alSalir(qint64 contacto, qint64 zona)
    {
        const auto f = m_mapa->feature(zona);
        if (!f)
            return;
        const auto clave = qMakePair(contacto, zona);
        const QDateTime desde = m_dentroDesde.take(clave);
        m_avisadaPermanencia.remove(clave);
        m_estadisticas[zona].salidas++;
        const double min = desde.isValid() ? desde.secsTo(m_hora) / 60.0 : 0.0;
        registrar(zona, contacto, QStringLiteral("Sale"), min, reglasDe(*f).salida);
    }

    // Avisa, una sola vez, de los contactos que llevan dentro de su zona más de su máximo.
    void revisarPermanencias()
    {
        for (auto it = m_dentroDesde.constBegin(); it != m_dentroDesde.constEnd(); ++it) {
            const auto f = m_mapa->feature(it.key().second);
            if (!f)
                continue;
            const int maxMin = reglasDe(*f).maxMin;
            const double min = it.value().secsTo(m_hora) / 60.0;
            if (maxMin > 0 && min >= maxMin && !m_avisadaPermanencia.contains(it.key())) {
                m_avisadaPermanencia.insert(it.key());
                registrar(it.key().second, it.key().first, QStringLiteral("Permanencia"), min, true);
            }
        }
    }

    // Añade un hecho al historial (con la hora simulada) y refresca la tabla de eventos.
    void registrar(qint64 zona, qint64 contacto, const QString &tipo, double permanenciaMin, bool aviso)
    {
        Evento e;
        e.hora = m_hora;
        e.zona = nombreZona(zona);
        e.contacto = nombreContacto(contacto);
        e.tipo = tipo;
        e.permanenciaMin = permanenciaMin;
        e.aviso = aviso;
        m_eventos.append(e);
        refrescarEventos();
        if (aviso)
            statusBar()->showMessage(tr("⚠ %1: %2 (%3)").arg(e.zona, e.contacto, tipoLegible(tipo)), 8000);
    }

    // Texto de un tipo de evento para los avisos: «entra», «sale», «permanencia excedida».
    static QString tipoLegible(const QString &tipo)
    {
        if (tipo == QLatin1String("Entra"))
            return tr("entra");
        if (tipo == QLatin1String("Sale"))
            return tr("sale");
        return tr("permanencia excedida");
    }

    // Tres zonas para ver cada regla: un puerto (avisa al salir), una zona restringida
    // (avisa al entrar) y un fondeadero (avisa si alguien pasa de 30 min dentro).
    void crearZonasDeEjemplo()
    {
        const QGeoCoordinate c(23.20, -82.40);
        Reglas puerto;
        puerto.salida = true;
        crearZona(tr("Puerto"), cuadrado(c.atDistanceAndAzimuth(1.0 * kNM, 270.0), 1.0), puerto);
        Reglas restringida;
        restringida.entrada = true;
        crearZona(tr("Zona restringida"), cuadrado(c.atDistanceAndAzimuth(5.0 * kNM, 0.0), 1.5), restringida);
        Reglas fondeadero;
        fondeadero.maxMin = 30;
        crearZona(tr("Fondeadero"), cuadrado(c.atDistanceAndAzimuth(4.0 * kNM, 90.0), 1.0), fondeadero);
    }

    // Diálogo para el nombre y las reglas de una zona. Devuelve false si se cancela.
    bool dialogoReglas(QString *nombre, Reglas *r)
    {
        QDialog d(this);
        d.setWindowTitle(tr("Zona vigilada"));
        auto *form = new QFormLayout(&d);
        auto *campoNombre = new QLineEdit(*nombre, &d);
        auto *entrada = new QCheckBox(tr("Avisar al entrar"), &d);
        entrada->setChecked(r->entrada);
        auto *salida = new QCheckBox(tr("Avisar al salir"), &d);
        salida->setChecked(r->salida);
        auto *maxMin = new QSpinBox(&d);
        maxMin->setRange(0, 24 * 60);
        maxMin->setSuffix(tr(" min"));
        maxMin->setSpecialValueText(tr("sin límite"));
        maxMin->setValue(r->maxMin);
        form->addRow(tr("Nombre:"), campoNombre);
        form->addRow(entrada);
        form->addRow(salida);
        form->addRow(tr("Avisar si permanece más de:"), maxMin);
        auto *botones = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        form->addRow(botones);
        connect(botones, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(botones, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted)
            return false;
        *nombre = campoNombre->text().trimmed().isEmpty() ? *nombre : campoNombre->text().trimmed();
        r->entrada = entrada->isChecked();
        r->salida = salida->isChecked();
        r->maxMin = maxMin->value();
        return true;
    }

    // Reglas de la zona seleccionada en la tabla, con el diálogo.
    void editarReglasSeleccionada()
    {
        const qint64 id = zonaSeleccionada();
        auto f = m_mapa->feature(id);
        if (!f) {
            statusBar()->showMessage(tr("Elige primero una zona de la tabla."), 4000);
            return;
        }
        QString nombre = f->name;
        Reglas r = reglasDe(*f);
        if (!m_pedirReglas(&nombre, &r))
            return;
        f->name = nombre;
        ponerReglas(&*f, r);
        m_mapa->updateFeature(*f);
        refrescarZonas();
    }

    // Id de la zona elegida en la tabla de zonas, o -1.
    qint64 zonaSeleccionada() const
    {
        const auto filas = m_tablaZonas->selectionModel()->selectedRows();
        return filas.isEmpty() ? -1 : m_tablaZonas->item(filas.first().row(), 0)->data(Qt::UserRole).toLongLong();
    }

    // Reconstruye la tabla de zonas con lo que hay dentro ahora y los contadores, conservando
    // la fila elegida.
    void refrescarZonas()
    {
        const qint64 elegida = zonaSeleccionada();
        const QSignalBlocker bloqueo(m_tablaZonas);
        auto zonas = m_mapa->featuresInLayer(kCapa);
        zonas.erase(std::remove_if(zonas.begin(), zonas.end(),
                                   [](const MapFeature &f) { return f.kind != GeometryKind::Polygon; }),
                    zonas.end());
        m_tablaZonas->setRowCount(zonas.size());
        for (int i = 0; i < zonas.size(); ++i) {
            const MapFeature &f = zonas.at(i);
            const int dentro = int(m_mapa->targetsInZone(f.id).size());
            const QStringList celdas = {
                f.name, textoReglas(reglasDe(f)), QString::number(dentro),
                QString::number(m_estadisticas.value(f.id).entradas),
                QString::number(m_estadisticas.value(f.id).salidas),
            };
            for (int j = 0; j < celdas.size(); ++j) {
                auto *item = new QTableWidgetItem(celdas.at(j));
                if (j == 0)
                    item->setData(Qt::UserRole, f.id);
                if (j >= 2)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (j == 2 && dentro > 0)
                    item->setBackground(QColor(0xff, 0xf3, 0xc4));
                m_tablaZonas->setItem(i, j, item);
            }
            if (f.id == elegida)
                m_tablaZonas->selectRow(i);
        }
    }

    // Reconstruye la tabla de eventos (el más reciente arriba), con o sin el filtro «solo avisos».
    void refrescarEventos()
    {
        const bool soloAvisos = m_soloAvisos->isChecked();
        QVector<const Evento *> filas;
        for (int i = m_eventos.size() - 1; i >= 0; --i)
            if (!soloAvisos || m_eventos.at(i).aviso)
                filas.append(&m_eventos.at(i));
        m_tablaEventos->setRowCount(filas.size());
        for (int i = 0; i < filas.size(); ++i) {
            const Evento &e = *filas.at(i);
            const QStringList celdas = {
                e.hora.toString(QStringLiteral("HH:mm:ss")), e.zona, e.contacto, e.tipo,
                e.tipo == QLatin1String("Entra") ? QStringLiteral("—") : QString::number(e.permanenciaMin, 'f', 1),
                e.aviso ? QStringLiteral("⚠") : QString(),
            };
            for (int j = 0; j < celdas.size(); ++j) {
                auto *item = new QTableWidgetItem(celdas.at(j));
                if (j == 4)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (e.aviso)
                    item->setBackground(QColor(0xff, 0xcd, 0xd2));
                m_tablaEventos->setItem(i, j, item);
            }
        }
    }

    // Lleva la vista a la zona \a id, con la caja que contiene sus vértices más un margen.
    void centrarEn(qint64 id)
    {
        const auto f = m_mapa->feature(id);
        if (!f || f->geometry.isEmpty())
            return;
        double norte = -90.0, sur = 90.0, oeste = 180.0, este = -180.0;
        for (const QGeoCoordinate &p : f->geometry) {
            norte = std::max(norte, p.latitude());
            sur = std::min(sur, p.latitude());
            oeste = std::min(oeste, p.longitude());
            este = std::max(este, p.longitude());
        }
        const double mLat = std::max((norte - sur) * 0.4, 0.004);
        const double mLon = std::max((este - oeste) * 0.4, 0.004);
        m_mapa->fitBounds(QGeoCoordinate(norte + mLat, oeste - mLon), QGeoCoordinate(sur - mLat, este + mLon));
    }

    // Paneles: tabla de zonas (izquierda), historial de eventos (abajo) y glosario (derecha).
    void crearPaneles()
    {
        const auto terminos = glosario();

        // --- Tabla de zonas ---
        m_tablaZonas = new QTableWidget(0, columnasZonas().size(), this);
        configurarTabla(m_tablaZonas, columnasZonas(), terminos);
        // El texto largo (las reglas) se lleva el ancho sobrante; los números, lo justo.
        QHeaderView *cab = m_tablaZonas->horizontalHeader();
        cab->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        cab->setSectionResizeMode(1, QHeaderView::Stretch);
        for (int i = 2; i < 5; ++i)
            cab->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        auto *dockZonas = new QDockWidget(tr("Zonas"), this);
        dockZonas->setWidget(m_tablaZonas);
        addDockWidget(Qt::LeftDockWidgetArea, dockZonas);
        dockZonas->setMinimumWidth(520);

        // --- Historial ---
        auto *abajo = new QWidget(this);
        auto *col = new QVBoxLayout(abajo);
        col->setContentsMargins(4, 4, 4, 4);
        m_soloAvisos = new QCheckBox(tr("Solo avisos"), this);
        m_tablaEventos = new QTableWidget(0, columnasEventos().size(), this);
        configurarTabla(m_tablaEventos, columnasEventos(), terminos);
        col->addWidget(m_soloAvisos);
        col->addWidget(m_tablaEventos, 1);
        auto *dockEventos = new QDockWidget(tr("Eventos"), this);
        dockEventos->setWidget(abajo);
        addDockWidget(Qt::BottomDockWidgetArea, dockEventos);
        connect(m_soloAvisos, &QCheckBox::toggled, this, [this](bool) { refrescarEventos(); });

        // --- Glosario ---
        auto *derecha = new QWidget(this);
        auto *colDer = new QVBoxLayout(derecha);
        auto *buscar = new QLineEdit(this);
        buscar->setPlaceholderText(tr("Buscar un término..."));
        buscar->setClearButtonEnabled(true);
        m_listaGlosario = new QListWidget(this);
        auto *definicion = new QTextBrowser(this);
        definicion->setMaximumHeight(160);
        definicion->setPlaceholderText(tr("Elige un término para ver su definición."));
        colDer->addWidget(buscar);
        colDer->addWidget(m_listaGlosario, 1);
        colDer->addWidget(definicion);
        for (const Termino &t : terminos)
            m_listaGlosario->addItem(t.nombre);
        m_dockGlosario = new QDockWidget(tr("Glosario"), this);
        m_dockGlosario->setWidget(derecha);
        addDockWidget(Qt::RightDockWidgetArea, m_dockGlosario);
        connect(m_listaGlosario, &QListWidget::currentTextChanged, this, [definicion](const QString &nombre) {
            for (const Termino &t : glosario())
                if (t.nombre == nombre)
                    definicion->setHtml(QStringLiteral("<b>%1</b><br>%2").arg(t.nombre.toHtmlEscaped(),
                                                                              t.definicion.toHtmlEscaped()));
        });
        connect(buscar, &QLineEdit::textChanged, this, [this](const QString &texto) {
            // Filtra por el nombre o por la definición, sin distinguir mayúsculas.
            const auto todos = glosario();
            for (int i = 0; i < m_listaGlosario->count(); ++i) {
                const Termino &t = todos.at(i);
                const bool ver = texto.isEmpty() || t.nombre.contains(texto, Qt::CaseInsensitive)
                                 || t.definicion.contains(texto, Qt::CaseInsensitive);
                m_listaGlosario->item(i)->setHidden(!ver);
            }
        });
    }

    // Pone las cabeceras de \a tabla y, en cada una, la definición del glosario como ayuda
    // al pasar el ratón. La tabla es de solo lectura y elige filas enteras.
    void configurarTabla(QTableWidget *tabla, const QVector<Columna> &columnas, const QVector<Termino> &terminos)
    {
        QStringList cabeceras;
        for (const Columna &c : columnas)
            cabeceras << c.texto;
        tabla->setHorizontalHeaderLabels(cabeceras);
        for (int i = 0; i < columnas.size(); ++i)
            for (const Termino &t : terminos)
                if (t.nombre == columnas.at(i).clave)
                    tabla->horizontalHeaderItem(i)->setToolTip(t.definicion);
        tabla->setSelectionBehavior(QAbstractItemView::SelectRows);
        tabla->setSelectionMode(QAbstractItemView::SingleSelection);
        tabla->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tabla->verticalHeader()->setVisible(false);
        tabla->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }

    // Menús: Archivo (abrir y guardar zonas, exportar eventos), Zonas (herramientas, reglas,
    // borrar) y Ayuda (glosario, F1).
    void crearMenus()
    {
        QMenu *archivo = menuBar()->addMenu(tr("&Archivo"));
        connect(archivo->addAction(tr("&Abrir zonas...")), &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getOpenFileName(this, tr("Abrir zonas"), QString(),
                                                              tr("Zonas (*.geojson *.json)"));
            if (!ruta.isEmpty())
                abrirZonas(ruta);
        });
        auto *guardar = archivo->addAction(tr("&Guardar zonas..."));
        guardar->setShortcut(QKeySequence::Save);
        connect(guardar, &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getSaveFileName(this, tr("Guardar zonas"), QStringLiteral("zonas.geojson"),
                                                              tr("GeoJSON (*.geojson)"));
            if (!ruta.isEmpty())
                guardarZonas(ruta);
        });
        archivo->addSeparator();
        connect(archivo->addAction(tr("&Exportar eventos (CSV)...")), &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar eventos"), QStringLiteral("eventos.csv"),
                                                              tr("CSV (*.csv)"));
            if (!ruta.isEmpty())
                statusBar()->showMessage(exportarEventos(ruta) ? tr("Eventos exportados a %1").arg(ruta)
                                                               : tr("No se pudo escribir %1").arg(ruta), 6000);
        });

        QMenu *zonas = menuBar()->addMenu(tr("&Zonas"));
        auto *grupo = new QActionGroup(this);
        const auto herramienta = [&](const QString &texto, MapTool t, const QKeySequence &tecla) {
            QAction *a = zonas->addAction(texto);
            a->setCheckable(true);
            a->setActionGroup(grupo);
            a->setShortcut(tecla);
            connect(a, &QAction::triggered, this, [this, t] { m_mapa->setActiveTool(t); });
            return a;
        };
        herramienta(tr("&Navegar"), MapTool::None, QKeySequence(Qt::Key_N))->setChecked(true);
        herramienta(tr("&Dibujar zona (doble clic cierra)"), MapTool::DrawPolygon, QKeySequence(Qt::Key_D));
        herramienta(tr("&Editar zona (arrastrar vértices)"), MapTool::EditFeature, QKeySequence(Qt::Key_E));
        zonas->addSeparator();
        connect(zonas->addAction(tr("&Reglas de la zona elegida...")), &QAction::triggered, this,
                [this] { editarReglasSeleccionada(); });
        connect(zonas->addAction(tr("&Borrar la zona elegida")), &QAction::triggered, this, [this] {
            const qint64 id = zonaSeleccionada();
            if (id >= 0)
                m_mapa->removeFeature(id);
        });

        QMenu *ayuda = menuBar()->addMenu(tr("A&yuda"));
        QAction *glosarioAct = m_dockGlosario->toggleViewAction();
        glosarioAct->setText(tr("&Glosario"));
        glosarioAct->setShortcut(QKeySequence(Qt::Key_F1));
        ayuda->addAction(glosarioAct);
    }

    // Conecta las señales de la librería (entradas, salidas, zonas dibujadas o borradas) y
    // la selección de la tabla de zonas con el mapa.
    void conectarSenales()
    {
        connect(m_mapa, &MapWidget::targetEnteredZone, this,
                [this](qint64 contacto, qint64 zona) { alEntrar(contacto, zona); });
        connect(m_mapa, &MapWidget::targetLeftZone, this,
                [this](qint64 contacto, qint64 zona) { alSalir(contacto, zona); });
        connect(m_mapa, &MapWidget::featureCreated, this, [this](qint64 id) { alCrearZona(id); });
        connect(m_mapa, &MapWidget::featureRemoved, this, [this](qint64 id) {
            // Borrar una zona deja de vigilarla sin emitir salidas: se olvidan sus contadores.
            m_estadisticas.remove(id);
            for (auto it = m_dentroDesde.begin(); it != m_dentroDesde.end();)
                it = it.key().second == id ? m_dentroDesde.erase(it) : std::next(it);
            for (auto it = m_avisadaPermanencia.begin(); it != m_avisadaPermanencia.end();)
                it = it->second == id ? m_avisadaPermanencia.erase(it) : std::next(it);
            refrescarZonas();
        });
        connect(m_mapa, &MapWidget::featureUpdated, this, [this](qint64) { refrescarZonas(); });
        connect(m_mapa, &MapWidget::featureSelected, this, [this](qint64 id) {
            for (int i = 0; i < m_tablaZonas->rowCount(); ++i)
                if (m_tablaZonas->item(i, 0)->data(Qt::UserRole).toLongLong() == id)
                    m_tablaZonas->selectRow(i);
        });
        connect(m_tablaZonas, &QTableWidget::itemSelectionChanged, this, [this] {
            const qint64 id = zonaSeleccionada();
            if (id >= 0) {
                m_mapa->selectFeature(id);
                centrarEn(id);
            }
        });
    }

    MapWidget *m_mapa = nullptr;
    QTableWidget *m_tablaZonas = nullptr;
    QTableWidget *m_tablaEventos = nullptr;
    QCheckBox *m_soloAvisos = nullptr;
    QListWidget *m_listaGlosario = nullptr;
    QDockWidget *m_dockGlosario = nullptr;
    std::function<bool(QString *, Reglas *)> m_pedirReglas;
    QMap<QString, qint64> m_contactos;                    // nombre -> id del objetivo
    QMap<qint64, Estadistica> m_estadisticas;             // por id de zona
    QMap<QPair<qint64, qint64>, QDateTime> m_dentroDesde; // (contacto, zona) -> desde cuándo está dentro
    QSet<QPair<qint64, qint64>> m_avisadaPermanencia;     // pares ya avisados de permanencia
    QVector<Evento> m_eventos;
    Simulador m_sim;
    QDateTime m_hora;                                     // reloj simulado
    QTimer m_reloj;
    int m_numZonasDibujadas = 0;
};

// Evento del historial con ese \a zona, \a contacto y \a tipo (el primero), o nullptr.
static const Evento *buscarEvento(const QVector<Evento> &eventos, const QString &zona,
                                  const QString &contacto, const QString &tipo)
{
    for (const Evento &e : eventos)
        if (e.zona == zona && e.contacto == contacto && e.tipo == tipo)
            return &e;
    return nullptr;
}

// Cuántos eventos del historial cumplen \a zona, \a contacto y \a tipo (vacío = cualquiera).
static int contarEventos(const QVector<Evento> &eventos, const QString &zona = QString(),
                         const QString &contacto = QString(), const QString &tipo = QString())
{
    int n = 0;
    for (const Evento &e : eventos)
        if ((zona.isEmpty() || e.zona == zona) && (contacto.isEmpty() || e.contacto == contacto)
            && (tipo.isEmpty() || e.tipo == tipo))
            ++n;
    return n;
}

// Prueba sin ventana, con reloj simulado a mano: las tres zonas de ejemplo con sus reglas,
// editar una zona, el CSV, guardar y abrir las zonas y el glosario completo.
static int comprobar(Ventana &v)
{
    QTextStream out(stdout);
    bool todoOk = true;
    const auto verificar = [&](const QString &que, bool ok) {
        out << (ok ? "ok    " : "FALLO ") << que << "\n";
        todoOk = todoOk && ok;
    };
    v.detener();
    verificar(QStringLiteral("mapa listo"), v.mapa()->isReady());
    verificar(QStringLiteral("3 zonas de ejemplo, todas vigiladas"),
              v.mapa()->featuresInLayer(kCapa).size() == 3 && v.mapa()->watchedZones().size() == 3);

    // --- Glosario: define las columnas de las dos tablas y los términos de la interfaz ---
    QStringList faltan;
    const auto terminos = glosario();
    const auto definido = [&](const QString &nombre) {
        for (const Termino &t : terminos)
            if (t.nombre == nombre && !t.definicion.isEmpty())
                return true;
        return false;
    };
    for (const Columna &c : columnasZonas() + columnasEventos())
        if (!definido(c.clave))
            faltan << c.clave;
    for (const char *t : { "Evento", "Aviso", "Dibujar zona", "Editar zona", "Regla de aviso", "Permanencia",
                           "Tiempo simulado", "CSV", "GeoJSON" })
        if (!definido(QString::fromUtf8(t)))
            faltan << QString::fromUtf8(t);
    verificar(QStringLiteral("glosario define todo lo que usa la interfaz (%1 términos)%2")
                  .arg(terminos.size())
                  .arg(faltan.isEmpty() ? QString() : QStringLiteral(", faltan: ") + faltan.join(QLatin1String(", "))),
              faltan.isEmpty() && v.listaGlosario()->count() == terminos.size());

    // El texto de las reglas lleva «·» (UTF-8): con QLatin1String saldría «Â·».
    Reglas dos;
    dos.entrada = true;
    dos.salida = true;
    verificar(QStringLiteral("el texto de las reglas separa con «·» (sin caracteres rotos)"),
              textoReglas(dos).contains(QStringLiteral("al entrar · al salir")) && !textoReglas(dos).contains(QChar(0x00c2)));

    // --- Al empezar: el remolcador ya está dentro del puerto (entrada sin aviso) ---
    const qint64 puerto = v.zonaPorNombre(QStringLiteral("Puerto"));
    const qint64 restringida = v.zonaPorNombre(QStringLiteral("Zona restringida"));
    const qint64 fondeadero = v.zonaPorNombre(QStringLiteral("Fondeadero"));
    const Evento *e0 = buscarEvento(v.eventos(), QStringLiteral("Puerto"), QStringLiteral("Remolcador"), QStringLiteral("Entra"));
    verificar(QStringLiteral("al vigilar, el que ya está dentro produce su entrada (Remolcador en el Puerto, sin aviso)"),
              e0 && !e0->aviso && v.mapa()->targetsInZone(puerto).size() == 1);

    // --- 100 pasos de 30 s (50 min simulados) ---
    for (int i = 0; i < 100; ++i)
        v.paso(30.0);
    const auto &ev = v.eventos();
    const Evento *sal = buscarEvento(ev, QStringLiteral("Puerto"), QStringLiteral("Remolcador"), QStringLiteral("Sale"));
    verificar(QStringLiteral("Remolcador sale del Puerto con AVISO tras ~10 min dentro"),
              sal && sal->aviso && std::abs(sal->permanenciaMin - 10.0) < 1.5);
    const Evento *ent = buscarEvento(ev, QStringLiteral("Zona restringida"), QStringLiteral("Pesquero 1"), QStringLiteral("Entra"));
    const Evento *sal1 = buscarEvento(ev, QStringLiteral("Zona restringida"), QStringLiteral("Pesquero 1"), QStringLiteral("Sale"));
    verificar(QStringLiteral("Pesquero 1 entra en la zona restringida con AVISO y sale sin aviso tras ~30 min"),
              ent && ent->aviso && sal1 && !sal1->aviso && std::abs(sal1->permanenciaMin - 30.0) < 1.5);
    const Evento *perm = buscarEvento(ev, QStringLiteral("Fondeadero"), QStringLiteral("Carguero A"), QStringLiteral("Permanencia"));
    verificar(QStringLiteral("Carguero A, parado en el Fondeadero, supera los 30 min: UN aviso de permanencia"),
              perm && perm->aviso && perm->permanenciaMin >= 30.0
              && contarEventos(ev, QStringLiteral("Fondeadero"), QStringLiteral("Carguero A"), QStringLiteral("Permanencia")) == 1);
    const Evento *sal2 = buscarEvento(ev, QStringLiteral("Fondeadero"), QStringLiteral("Pesquero 2"), QStringLiteral("Sale"));
    verificar(QStringLiteral("Pesquero 2 cruza el Fondeadero en ~20 min (menos del máximo): sin aviso de permanencia"),
              sal2 && !sal2->aviso && std::abs(sal2->permanenciaMin - 20.0) < 1.5
              && contarEventos(ev, QStringLiteral("Fondeadero"), QStringLiteral("Pesquero 2"), QStringLiteral("Permanencia")) == 0);
    verificar(QStringLiteral("Carguero B no da ningún evento"), contarEventos(ev, QString(), QStringLiteral("Carguero B")) == 0);
    int avisos = 0;
    for (const Evento &e : ev)
        avisos += e.aviso ? 1 : 0;
    out << "      historial: " << ev.size() << " eventos, " << avisos << " avisos\n";
    for (const Evento &e : ev)
        out << "        " << e.hora.toString(QStringLiteral("HH:mm:ss")) << "  " << e.zona << " / " << e.contacto << " / "
            << e.tipo << "  " << QString::number(e.permanenciaMin, 'f', 1) << " min" << (e.aviso ? "  ⚠" : "") << "\n";
    verificar(QStringLiteral("8 eventos y 3 avisos en total"), ev.size() == 8 && avisos == 3);
    verificar(QStringLiteral("contadores: Puerto 1/1, Restringida 1/1, Fondeadero 2/1 (entradas/salidas)"),
              v.entradas(puerto) == 1 && v.salidas(puerto) == 1 && v.entradas(restringida) == 1
              && v.salidas(restringida) == 1 && v.entradas(fondeadero) == 2 && v.salidas(fondeadero) == 1);
    verificar(QStringLiteral("dentro ahora: solo Carguero A, en el Fondeadero"),
              v.mapa()->targetsInZone(fondeadero).size() == 1 && v.mapa()->targetsInZone(puerto).isEmpty()
              && v.mapa()->targetsInZone(restringida).isEmpty());

    // --- Editar la forma de una zona reevalúa quién queda dentro ---
    auto fz = v.mapa()->feature(fondeadero);
    const int antes = ev.size();
    fz->geometry = cuadrado(QGeoCoordinate(24.5, -81.0), 0.5);    // lejos de todo
    v.mapa()->updateFeature(*fz);
    const Evento *salEdit = buscarEvento(v.eventos(), QStringLiteral("Fondeadero"), QStringLiteral("Carguero A"), QStringLiteral("Sale"));
    verificar(QStringLiteral("mover el Fondeadero hace que Carguero A salga (evento anotado, ~35 min dentro)"),
              v.eventos().size() == antes + 1 && salEdit && std::abs(salEdit->permanenciaMin - 35.0) < 1.5
              && v.mapa()->targetsInZone(fondeadero).isEmpty());
    fz->geometry = cuadrado(QGeoCoordinate(23.20, -82.40).atDistanceAndAzimuth(4.0 * kNM, 90.0), 1.0);
    v.mapa()->updateFeature(*fz);                                   // lo devuelve a su sitio
    verificar(QStringLiteral("devolver el Fondeadero a su sitio vuelve a meter a Carguero A (una entrada más)"),
              v.entradas(fondeadero) == 3 && v.mapa()->targetsInZone(fondeadero).size() == 1);

    // --- Cambiar reglas ---
    Reglas nueva;
    nueva.entrada = true;
    nueva.maxMin = 10;
    v.aplicarReglas(restringida, nueva);
    const auto fr = v.mapa()->feature(restringida);
    verificar(QStringLiteral("cambiar las reglas de una zona las guarda en sus atributos y la recolorea"),
              fr && reglasDe(*fr).entrada && reglasDe(*fr).maxMin == 10 && !reglasDe(*fr).salida
              && fr->style.lineColor == QColor(0xd3, 0x2f, 0x2f));
    Reglas original;
    original.entrada = true;
    v.aplicarReglas(restringida, original);

    // --- Dibujar una zona: lo que hace la señal featureCreated ---
    v.ponerPeticionReglas([](QString *nombre, Reglas *r) {
        *nombre = QStringLiteral("Zona nueva");
        r->entrada = false;
        r->salida = true;
        return true;
    });
    MapFeature dibujada;
    dibujada.kind = GeometryKind::Polygon;
    dibujada.layerId = kCapa;
    dibujada.geometry = cuadrado(QGeoCoordinate(22.0, -83.0), 0.5);
    const qint64 idNueva = v.mapa()->addFeature(dibujada);
    v.alCrearZona(idNueva);
    const auto fn = v.mapa()->feature(idNueva);
    verificar(QStringLiteral("una zona recién dibujada pide nombre y reglas y queda vigilada"),
              fn && fn->name == QLatin1String("Zona nueva") && reglasDe(*fn).salida && v.mapa()->isZoneWatched(idNueva));
    v.ponerPeticionReglas([](QString *, Reglas *) { return false; });
    const int zonasAntes = int(v.mapa()->featuresInLayer(kCapa).size());
    MapFeature cancelada = dibujada;
    const qint64 idCancelada = v.mapa()->addFeature(cancelada);
    v.alCrearZona(idCancelada);
    verificar(QStringLiteral("cancelar el diálogo borra la zona recién dibujada"),
              !v.mapa()->feature(idCancelada) && int(v.mapa()->featuresInLayer(kCapa).size()) == zonasAntes);
    v.mapa()->removeFeature(idNueva);
    verificar(QStringLiteral("borrar una zona la deja de vigilar y limpia su fila"),
              !v.mapa()->isZoneWatched(idNueva) && v.mapa()->watchedZones().size() == 3
              && v.tablaZonas()->rowCount() == 3);

    // --- CSV ---
    QTemporaryDir dir;
    const QString csv = dir.filePath(QStringLiteral("eventos.csv"));
    QStringList lineas;
    if (v.exportarEventos(csv)) {
        QFile f(csv);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            lineas = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    bool columnasOk = !lineas.isEmpty();
    bool filaRemolcador = false;
    for (const QString &l : lineas) {
        columnasOk = columnasOk && l.split(QLatin1Char(';')).size() == 6;
        const auto c = l.split(QLatin1Char(';'));
        if (c.size() == 6 && c.at(1) == QLatin1String("Puerto") && c.at(2) == QLatin1String("Remolcador")
            && c.at(3) == QLatin1String("Sale") && std::abs(c.at(4).toDouble() - 10.0) < 1.5 && c.at(5) == QLatin1String("1")
            && QDateTime::fromString(c.at(0), Qt::ISODate).isValid())
            filaRemolcador = true;
    }
    verificar(QStringLiteral("CSV: cabecera + una fila por evento, 6 columnas, y la fila del Remolcador completa"),
              lineas.size() == v.eventos().size() + 1 && columnasOk && filaRemolcador
              && lineas.first() == QLatin1String("hora_utc;zona;contacto;evento;permanencia_min;aviso"));

    // --- Guardar y abrir las zonas: las reglas viajan con el fichero ---
    const QString geojson = dir.filePath(QStringLiteral("zonas.geojson"));
    const bool guardado = v.guardarZonas(geojson);
    v.borrarTodasLasZonas();
    verificar(QStringLiteral("borrar todas las zonas las deja sin vigilar"),
              guardado && v.mapa()->featuresInLayer(kCapa).isEmpty() && v.mapa()->watchedZones().isEmpty());
    const bool abierto = v.abrirZonas(geojson);
    const qint64 puerto2 = v.zonaPorNombre(QStringLiteral("Puerto"));
    const qint64 restr2 = v.zonaPorNombre(QStringLiteral("Zona restringida"));
    const qint64 fond2 = v.zonaPorNombre(QStringLiteral("Fondeadero"));
    const auto a = puerto2 >= 0 ? v.mapa()->feature(puerto2) : std::nullopt;
    const auto b = restr2 >= 0 ? v.mapa()->feature(restr2) : std::nullopt;
    const auto c = fond2 >= 0 ? v.mapa()->feature(fond2) : std::nullopt;
    verificar(QStringLiteral("abrir las zonas guardadas: 3 zonas con sus nombres y reglas, otra vez vigiladas"),
              abierto && a && b && c && v.mapa()->watchedZones().size() == 3
              && reglasDe(*a).salida && !reglasDe(*a).entrada && reglasDe(*b).entrada && !reglasDe(*b).salida
              && reglasDe(*c).maxMin == 30 && !reglasDe(*c).entrada && !reglasDe(*c).salida
              && a->style.lineColor == QColor(0xef, 0x6c, 0x00));
    verificar(QStringLiteral("al volver a vigilar, Carguero A (parado en el Fondeadero) vuelve a constar dentro"),
              v.mapa()->targetsInZone(fond2).size() == 1 && v.entradas(fond2) == 1);

    out << (todoOk ? "TODO BIEN\n" : "HAY FALLOS\n");
    return todoOk ? 0 : 1;
}

// Arranca la ventana (o, con --comprobar, la prueba sin ventana; con --captura=, la deja
// correr unos pasos y guarda una imagen).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vigilancia_zonas"));

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

    // --captura=imagen.png: unos pasos de simulación, espera a las teselas, guarda la ventana y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(strlen("--captura=")));
            ventana.detener();
            for (int i = 0; i < 70; ++i)
                ventana.paso(kEscala);
            QTimer::singleShot(2500, &app, [&ventana, &app, destino] {
                ventana.grab().save(destino);
                app.quit();
            });
        }
    }
    return app.exec();
}
