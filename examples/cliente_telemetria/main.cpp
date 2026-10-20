// cliente_telemetria - un receptor de NMEA 0183 y AIS por UDP, con libmapa y libtelemetria.
//
// Décimo ejemplo de la serie. Es lo que sustituye al «feed» simulado de monitor_trafico en un
// producto real: escucha un puerto UDP (o reproduce una captura), decodifica las frases con
// libtelemetria y pone en el mapa el barco propio y los contactos AIS. Lo que enseña:
//   - Tres FUENTES de datos, con el mismo decodificador: un puerto UDP (QUdpSocket; cada datagrama trae líneas
//     completas), una conexión TCP como cliente (QTcpSocket; un flujo, con reconexión automática: es lo que dan
//     AIS-catcher, gpsd o un conversor serie-red) y un PUERTO SERIE (QSerialPort; el receptor conectado por
//     cable, a 4800 o 38400 baudios). El serie solo está si el kit trae el módulo Qt Serial Port.
//   - Qué hacer con lo que sale: RMC/GGA/VTG/HDT mueven el BARCO PROPIO; los mensajes AIS de los
//     demás (posición, nombre, tipo, tamaño) van llegando por separado y se FUSIONAN en un solo
//     contacto por MMSI; un mensaje de dos frases puede llegar en dos datagramas.
//   - Poner los contactos en el mapa (addTarget/updateTarget) con su rumbo, velocidad y traza.
//   - Un contacto del que se deja de oír se atenúa (PERDIDO) y, pasado un tiempo, se borra.
//   - La CALIDAD DEL ENLACE: frases por segundo, sumas de comprobación malas, frases mal formadas,
//     tipos ignorados, fragmentos perdidos. Un cliente real debe poder decir si el problema es el
//     cable, la configuración del receptor o su propio código.
//   - Reproducir una CAPTURA (`.nmea`) igual que si llegara en vivo, y un escenario de demostración.
//   - Un GLOSARIO en la interfaz con los términos de la ventana.
//
//   cliente_telemetria [paquete de datos] [--puerto=10110 | --tcp=host:puerto | --serie=COM3[,38400]]
//                      [--reproducir=captura.nmea]
//   cliente_telemetria <paquete> --comprobar
//        -> sin ventana: alimenta el cliente con tramas conocidas (entre ellas un mensaje AIS publicado
//           de referencia), por llamada directa, por un socket UDP real, por una conexión TCP real (con corte y
//           reconexión), por un puerto serie (un pseudoterminal, en Linux) y desde un fichero, y comprueba
//           el barco propio, la tabla, los contactos, la fusión de datos estáticos, los contadores del
//           enlace, el contacto perdido y su borrado, y que el glosario define lo que se usa.
//   cliente_telemetria <paquete> --captura=imagen.png [--espera=3]
//        -> abre la ventana, guarda una imagen a los 3 s (o a los que diga --espera) y sale. Con el escenario
//           de demostración; si se da --puerto, --tcp, --serie o --reproducir, con lo que llegue por ahí.
//
// La pareja para probarlo sin hardware es `emisor_nmea` (otro ejemplo): lo que él emite, esto lo escucha.

#include <libmapa/Coordinates.h>
#include <libmapa/MapWidget.h>
#include <libmapa/Proximity.h>
#include <telemetria/Decoder.h>

#if !defined(TELEMETRIA_API_LEVEL) || TELEMETRIA_API_LEVEL < 2
#  error "La libtelemetria instalada es anterior a este ejemplo: reinstalala (qmake/libmapa/libmapa.pro o qmake/libtelemetria/telemetria.pro y make install; con CMake, cmake --install)."
#endif

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QtNetwork/QUdpSocket>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef TELEMETRIA_CON_SERIE
#  include <QtSerialPort/QSerialPort>
#  include <QtSerialPort/QSerialPortInfo>
#endif
#ifdef Q_OS_UNIX
#  include <fcntl.h>
#  include <stdlib.h>
#  include <unistd.h>
#endif

using namespace libmapa;
using telemetria::AisMessage;
using telemetria::Event;

namespace {

// Metros de una milla náutica.
const double kNM = 1852.0;
// Segundos sin datos para dar un contacto por perdido, y para borrarlo. Un AIS de clase A en
// movimiento informa cada 2-10 s y uno fondeado cada 3 min: 60 s es prudente para el tráfico en marcha.
const double kPerdidoSeg = 60.0;
const double kBorrarSeg = 300.0;
// Puerto UDP habitual de NMEA 0183 sobre red.
const int kPuertoPorOmision = 10110;
// Segundos entre intentos de reconexión de una conexión TCP o de un puerto serie que se ha caído.
const int kReconexionMs = 3000;
// Velocidades habituales: NMEA 0183 clásico va a 4800 baudios y AIS a 38400.
const int kBaudiosPorOmision = 38400;

// El barco que lleva el receptor, a partir de RMC, GGA, VTG y HDT.
struct Propio {
    bool hayPosicion = false;
    QGeoCoordinate posicion;
    double sogKn = telemetria::notAvailable();
    double cogDeg = telemetria::notAvailable();
    double rumboDeg = telemetria::notAvailable();   // la proa (HDT)
    bool fijado = false;                            // estado A de la RMC
    int calidad = 0;                                // GGA: 0 sin fijar, 1 GPS, 2 DGPS...
    int satelites = -1;
    double hdop = telemetria::notAvailable();
    QDateTime ultima;
    qint64 id = -1;
};

// Un contacto AIS: lo que se sabe de un MMSI, fusionando los mensajes de posición y los estáticos.
struct Contacto {
    quint32 mmsi = 0;
    qint64 id = -1;                                 // el objetivo del mapa (-1 hasta que haya posición)
    QString nombre;
    QString indicativo;
    QString destino;
    int tipoBarco = 0;
    int esloraM = 0;
    bool claseB = false;
    QGeoCoordinate posicion;
    double sogKn = telemetria::notAvailable();
    double cogDeg = telemetria::notAvailable();
    int rumboDeg = -1;
    int navegacion = -1;
    QDateTime ultima;
    int informes = 0;
    bool perdido = false;
};

// Una entrada del glosario y una columna de la tabla.
struct Termino {
    QString nombre;
    QString definicion;
};
struct Columna {
    QString texto;
    QString clave;     // el término del glosario que la define (para la ayuda de la cabecera)
};

} // namespace

// Las columnas de la tabla de contactos, cada una con el término que la define.
static QVector<Columna> columnas()
{
    return {
        { QObject::tr("MMSI"), QObject::tr("MMSI") },
        { QObject::tr("Nombre"), QObject::tr("Nombre") },
        { QObject::tr("Tipo"), QObject::tr("Tipo de barco") },
        { QObject::tr("Clase"), QObject::tr("Clase A / Clase B") },
        { QObject::tr("SOG (kn)"), QObject::tr("SOG") },
        { QObject::tr("COG (°)"), QObject::tr("COG") },
        { QObject::tr("Distancia (NM)"), QObject::tr("Distancia") },
        { QObject::tr("Marcación (°)"), QObject::tr("Marcación") },
        { QObject::tr("Navegación"), QObject::tr("Estado de navegación") },
        { QObject::tr("Edad (s)"), QObject::tr("Edad") },
        { QObject::tr("Estado"), QObject::tr("Contacto perdido") },
    };
}

// El glosario de la interfaz: los términos que usa la ventana. --comprobar verifica que cada uno de los
// que salen en pantalla (terminosEnPantalla) tiene aquí su definición.
static QVector<Termino> glosario()
{
    return {
        { QObject::tr("NMEA 0183"),
          QObject::tr("El protocolo de texto con que los instrumentos de un barco (GPS, rumbo, AIS...) se hablan. "
                      "Cada dato es una «frase» en una línea, que empieza por $ o ! y acaba en *XX.") },
        { QObject::tr("UDP"),
          QObject::tr("Forma de mandar datos por red sin establecer conexión: cada datagrama sale y puede llegar, o no. "
                      "Muchos receptores y multiplexores NMEA lo emiten a un puerto (por omisión, el 10110).") },
        { QObject::tr("Puerto"),
          QObject::tr("El número (1-65535) por el que este programa escucha (UDP) o al que se conecta (TCP). Debe ser el "
                      "del receptor. Solo un programa a la vez puede escuchar un puerto UDP.") },
        { QObject::tr("TCP"),
          QObject::tr("Forma de comunicar por red con una conexión que se mantiene abierta y entrega los datos en orden, "
                      "como un flujo continuo (una frase puede llegar partida). Aquí el programa es el CLIENTE: se conecta "
                      "a un servidor (un receptor AIS por red, AIS-catcher, gpsd...) y, si la conexión se corta, vuelve a "
                      "intentarlo cada 3 s.") },
        { QObject::tr("Puerto serie"),
          QObject::tr("El cable por el que muchos receptores GPS y AIS entregan las frases (COM3 en Windows, /dev/ttyUSB0 "
                      "en Linux). Solo un programa a la vez puede abrirlo. Si se desenchufa, el programa lo reabre cuando "
                      "vuelve. Necesita que el kit de Qt traiga el módulo Qt Serial Port.") },
        { QObject::tr("Baudios"),
          QObject::tr("La velocidad del puerto serie, en símbolos por segundo. Debe ser la del receptor: 4800 en NMEA 0183 "
                      "clásico (GPS) y 38400 en AIS. Con la velocidad equivocada llegan frases rotas: sube «Suma mala».") },
        { QObject::tr("Frase"),
          QObject::tr("Una línea NMEA: `$GPRMC,...*6A`. Lleva un tipo (RMC, GGA, VDM...) y campos separados por comas.") },
        { QObject::tr("Suma de comprobación"),
          QObject::tr("Las dos cifras tras el * al final de una frase: el XOR de sus caracteres. Si no cuadra, la "
                      "frase se corrompió por el camino y se descarta (se cuenta en «Suma mala»).") },
        { QObject::tr("RMC"),
          QObject::tr("Frase de un GPS con la hora, la posición, la velocidad y el rumbo sobre el fondo. Es la que "
                      "mueve el barco propio. Con estado V el receptor avisa de que no tiene fijado.") },
        { QObject::tr("GGA, VTG y HDT"),
          QObject::tr("GGA: calidad del fijado, satélites y HDOP. VTG: rumbo y velocidad sobre el fondo. HDT: rumbo "
                      "de la proa (hacia dónde apunta el barco, que no es lo mismo que hacia dónde se mueve).") },
        { QObject::tr("Calidad del fijado"),
          QObject::tr("Cuánto se puede fiar de la posición del GPS: 0 = sin fijar, 1 = GPS, 2 = GPS diferencial (más "
                      "preciso). El HDOP resume la geometría de los satélites: cuanto más bajo, mejor.") },
        { QObject::tr("AIS"),
          QObject::tr("Sistema de Identificación Automática: los barcos emiten por radio VHF quiénes son, dónde están "
                      "y adónde van. Los mensajes llegan en frases !AIVDM (de otros) y !AIVDO (del propio barco).") },
        { QObject::tr("Mensaje de varias frases"),
          QObject::tr("Algunos mensajes AIS (el de datos estáticos, tipo 5) no caben en una frase y salen en dos o "
                      "más con el mismo identificador de secuencia. Hasta que no llegan todas, no hay mensaje. Si "
                      "falta una, se descarta y se cuenta en «Fragmentos perdidos».") },
        { QObject::tr("MMSI"),
          QObject::tr("Número de 9 cifras que identifica la radio de un barco (las tres primeras dicen el país). Es la "
                      "clave de cada contacto: todos los mensajes con el mismo MMSI son del mismo barco.") },
        { QObject::tr("Nombre"),
          QObject::tr("El nombre del barco. No viaja con la posición: llega en los mensajes estáticos, cada pocos "
                      "minutos. Hasta entonces el contacto se muestra solo con su MMSI.") },
        { QObject::tr("Tipo de barco"),
          QObject::tr("La categoría que declara el barco: carguero, petrolero, pesquero, pasaje, remolcador, recreo...") },
        { QObject::tr("Clase A / Clase B"),
          QObject::tr("Clase A: buques obligados a llevar AIS (informan cada 2-10 s en movimiento). Clase B: "
                      "embarcaciones menores y de recreo (cada 30 s o más, con menos potencia).") },
        { QObject::tr("SOG"),
          QObject::tr("Velocidad sobre el fondo (Speed Over Ground), en nudos: la que lleva realmente el barco "
                      "respecto a la tierra, con corriente y viento incluidos.") },
        { QObject::tr("COG"),
          QObject::tr("Rumbo sobre el fondo (Course Over Ground), en grados verdaderos: hacia dónde se mueve el "
                      "barco, que con corriente puede no ser hacia donde apunta.") },
        { QObject::tr("Distancia"),
          QObject::tr("Millas náuticas (1852 m) desde el barco propio hasta el contacto.") },
        { QObject::tr("Marcación"),
          QObject::tr("La dirección en que está el contacto visto desde el barco propio, en grados verdaderos "
                      "(0° norte, 90° este).") },
        { QObject::tr("Estado de navegación"),
          QObject::tr("Lo que declara hacer el barco: navegando a motor, fondeado, amarrado, pescando, sin gobierno... "
                      "Solo lo dan los de clase A.") },
        { QObject::tr("Edad"),
          QObject::tr("Segundos desde el último mensaje de ese contacto.") },
        { QObject::tr("Contacto perdido"),
          QObject::tr("Un contacto del que no se oye nada en 60 s: se atenúa en el mapa y la tabla. Pasados 5 minutos "
                      "se borra. Si vuelve a emitir, vuelve a estar activo.") },
        { QObject::tr("Frases por segundo"),
          QObject::tr("Cuántas líneas por segundo llegan al decodificador, en promedio de los últimos segundos. Si "
                      "baja a 0, el enlace se ha cortado.") },
        { QObject::tr("Captura"),
          QObject::tr("Un fichero de texto con frases NMEA grabadas. Se reproduce como si llegaran en vivo, útil para "
                      "analizar un problema o para probar sin receptor.") },
    };
}

// Los términos que la ventana muestra tal cual (títulos, etiquetas y cabeceras de columna): cada uno
// debe estar definido en el glosario.
static QStringList terminosEnPantalla()
{
    QStringList l = {
        QObject::tr("NMEA 0183"), QObject::tr("UDP"), QObject::tr("TCP"), QObject::tr("Puerto serie"),
        QObject::tr("Baudios"), QObject::tr("Puerto"), QObject::tr("Frase"),
        QObject::tr("Suma de comprobación"), QObject::tr("RMC"), QObject::tr("GGA, VTG y HDT"),
        QObject::tr("Calidad del fijado"), QObject::tr("AIS"), QObject::tr("Mensaje de varias frases"),
        QObject::tr("Frases por segundo"), QObject::tr("Captura"),
    };
    for (const Columna &c : columnas())
        l << c.clave;
    return l;
}

// El escenario de demostración como líneas NMEA: el barco propio y tres contactos AIS (un carguero de
// clase A, un pesquero de clase B y un petrolero), con sus posiciones y sus datos estáticos. Se codifica
// con libtelemetria, igual que lo hace emisor_nmea.
static QList<QByteArray> escenarioDemostracion(const QDateTime &hora)
{
    QList<QByteArray> l;
    telemetria::Rmc r;
    r.valid = true;
    r.time = hora.time();
    r.utc = hora;
    r.lat = 23.20;
    r.lon = -82.60;
    r.sogKn = 10.0;
    r.cogDeg = 90.0;
    l << telemetria::formatRmc(r);
    telemetria::Gga g;
    g.time = hora.time();
    g.lat = r.lat;
    g.lon = r.lon;
    g.quality = 1;
    g.satellites = 9;
    g.hdop = 0.9;
    l << telemetria::formatGga(g);
    telemetria::Hdt h;
    h.headingDeg = 90.0;
    l << telemetria::formatHdt(h);

    struct Dato {
        quint32 mmsi;
        const char *nombre;
        const char *indicativo;
        int tipo;
        bool claseB;
        double lat, lon, nudos, rumbo;
        int proa, popa;
    };
    const Dato datos[] = {
        { 323000101, "HABANA STAR", "COHS01", 70, false, 23.20, -82.35, 12.0, 270.0, 150, 30 },
        { 323000103, "PESCADOR 7", "COPE07", 30, true, 23.32, -82.50, 3.5, 10.0, 9, 3 },
        { 351000104, "TANKER SUR", "3FTS04", 80, false, 23.05, -82.55, 9.0, 45.0, 180, 40 },
    };
    for (const Dato &d : datos) {
        AisMessage p;
        p.type = d.claseB ? 18 : 1;
        p.mmsi = d.mmsi;
        p.lat = d.lat;
        p.lon = d.lon;
        p.sogKn = d.nudos;
        p.cogDeg = d.rumbo;
        p.headingDeg = int(d.rumbo) % 360;
        p.navStatus = 0;
        p.second = hora.time().second();
        l << telemetria::encodeAis(p, d.claseB ? 'B' : 'A');
        if (!d.claseB) {
            AisMessage s;
            s.type = 5;
            s.mmsi = d.mmsi;
            s.callSign = QString::fromLatin1(d.indicativo);
            s.name = QString::fromLatin1(d.nombre);
            s.shipType = d.tipo;
            s.toBowM = d.proa;
            s.toSternM = d.popa;
            s.toPortM = 12;
            s.toStarboardM = 13;
            s.destination = QStringLiteral("LA HABANA");
            l << telemetria::encodeAis(s, 'A', 3);
        } else {
            AisMessage a;
            a.type = 24;
            a.partNo = 0;
            a.mmsi = d.mmsi;
            a.name = QString::fromLatin1(d.nombre);
            AisMessage b = a;
            b.partNo = 1;
            b.shipType = d.tipo;
            b.callSign = QString::fromLatin1(d.indicativo);
            b.toBowM = d.proa;
            b.toSternM = d.popa;
            b.toPortM = 2;
            b.toStarboardM = 2;
            l << telemetria::encodeAis(a, 'B') << telemetria::encodeAis(b, 'B');
        }
    }
    return l;
}

// Ventana: el mapa al centro; a la izquierda el enlace y el barco propio; abajo la tabla de contactos y el
// registro; a la derecha el glosario.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos, los paneles y el reloj de revisión, y empieza a escuchar
    // en \a puerto (0 = un puerto libre cualquiera; -1 = no escuchar todavía).
    explicit Ventana(const QString &datos, int puerto)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        cfg.autosaveFeatures = false;
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        resize(1400, 860);
        setWindowTitle(tr("Cliente de telemetría (NMEA 0183 / AIS)"));
        m_mapa->setCenter(QGeoCoordinate(23.20, -82.45));
        m_mapa->setZoom(10);
        m_mapa->setScaleBarVisible(true);
        m_mapa->setScaleBarUnits(ScaleUnits::Nautical);

        crearPaneles(puerto > 0 ? puerto : kPuertoPorOmision);
        crearMenus();
        connect(&m_socket, &QUdpSocket::readyRead, this, [this] { leerSocket(); });
        conectarFuentes();
        connect(m_mapa, &MapWidget::centerChanged, this,
                [this](const QGeoCoordinate &) { alCambiarCentro(QApplication::mouseButtons() != Qt::NoButton); });
        connect(m_seguir, &QCheckBox::toggled, this, [this](bool on) {
            if (on && m_propio.hayPosicion) {
                m_centrando = true;
                m_mapa->setCenter(m_propio.posicion);
                m_centrando = false;
            }
        });
        connect(&m_reloj, &QTimer::timeout, this, [this] { tic(); });
        m_reloj.start(1000);
        if (puerto >= 0)
            escuchar(quint16(puerto));
        actualizarEnlace();

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
    }

    MapWidget *mapa() const { return m_mapa; }
    // Para las pruebas: quita el reloj real y se avanza a mano con tic().
    void detenerReloj() { m_reloj.stop(); }

    // De dónde llegan los datos: nada, un puerto UDP, una conexión TCP (como cliente) o un puerto serie.
    enum class Fuente { Ninguna, Udp, Tcp, Serie };

    // ¿Esta compilación incluye el módulo Qt Serial Port (el puerto serie)?
    static bool tieneSerie()
    {
#ifdef TELEMETRIA_CON_SERIE
        return true;
#else
        return false;
#endif
    }

    // Empieza a escuchar el puerto UDP \a puerto (0 = uno libre). Falso, y lo dice en la barra de estado,
    // si no se puede (por ejemplo, otro programa lo tiene).
    bool escuchar(quint16 puerto)
    {
        parar();
        if (!m_socket.bind(QHostAddress(QHostAddress::AnyIPv4), puerto, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            statusBar()->showMessage(tr("No se puede escuchar el puerto %1: %2").arg(puerto).arg(m_socket.errorString()), 8000);
            registrar(tr("Error: no se puede escuchar el puerto %1 (%2)").arg(puerto).arg(m_socket.errorString()));
            actualizarEnlace();
            return false;
        }
        m_fuenteActiva = Fuente::Udp;
        if (puerto > 0)
            m_puerto->setValue(puerto);
        m_fuente->setCurrentIndex(0);
        registrar(tr("Escuchando en UDP %1").arg(m_socket.localPort()));
        actualizarEnlace();
        return true;
    }

    // Se conecta como CLIENTE TCP a \a host:\a puerto. La conexión es asíncrona: devuelve si se pudo EMPEZAR (los datos de
    // entrada no vacíos) y el estado real sale en el panel y en el registro. Si el servidor no está o la conexión
    // se corta, lo reintenta cada kReconexionMs hasta que se pare.
    bool conectarTcp(const QString &host, quint16 puerto)
    {
        parar();
        if (host.trimmed().isEmpty() || puerto == 0) {
            statusBar()->showMessage(tr("Para conectar por TCP hace falta un servidor y un puerto."), 6000);
            return false;
        }
        m_fuenteActiva = Fuente::Tcp;
        m_tcpHost = host.trimmed();
        m_tcpPuerto = puerto;
        m_intentos = 0;
        m_host->setText(m_tcpHost);
        m_puertoTcp->setValue(puerto);
        m_fuente->setCurrentIndex(1);
        registrar(tr("Conectando por TCP a %1:%2...").arg(m_tcpHost).arg(m_tcpPuerto));
        m_tcp.connectToHost(m_tcpHost, m_tcpPuerto);
        actualizarEnlace();
        return true;
    }

    // Abre el puerto serie \a nombre (COM3, /dev/ttyUSB0...) a \a baudios, 8N1, solo lectura. Falso si esta compilación no
    // trae Qt Serial Port o no se da el nombre; si el puerto no se puede abrir (no existe, lo tiene otro programa) lo
    // dice y lo reintenta cada kReconexionMs: así también se recupera cuando se desenchufa y se vuelve a enchufar.
    bool abrirSerie(const QString &nombre, int baudios)
    {
        parar();
#ifdef TELEMETRIA_CON_SERIE
        if (nombre.trimmed().isEmpty()) {
            statusBar()->showMessage(tr("Para abrir un puerto serie hace falta su nombre (COM3, /dev/ttyUSB0...)."), 6000);
            return false;
        }
        m_fuenteActiva = Fuente::Serie;
        m_serieNombre = nombre.trimmed();
        m_baudios = baudios > 0 ? baudios : kBaudiosPorOmision;
        m_intentos = 0;
        m_serie->setEditText(m_serieNombre);
        m_baudiosCombo->setCurrentText(QString::number(m_baudios));
        m_fuente->setCurrentIndex(2);
        return abrirSerieAhora();
#else
        Q_UNUSED(nombre)
        Q_UNUSED(baudios)
        statusBar()->showMessage(tr("Esta compilación no incluye Qt Serial Port: no hay puerto serie."), 8000);
        registrar(tr("Error: esta compilación no incluye Qt Serial Port (QT += serialport)"));
        return false;
#endif
    }

    // Deja de recibir, sea la fuente que sea. Una frase a medias de la fuente anterior se entrega como mal formada
    // (si no, se pegaría a la primera de la siguiente).
    void parar()
    {
        const bool habia = m_fuenteActiva != Fuente::Ninguna;
        m_fuenteActiva = Fuente::Ninguna;           // antes de cerrar: las señales de cierre no deben reconectar
        m_reconectar.stop();
        if (m_socket.state() == QAbstractSocket::BoundState)
            m_socket.close();
        if (m_tcp.state() != QAbstractSocket::UnconnectedState)
            m_tcp.abort();
#ifdef TELEMETRIA_CON_SERIE
        if (m_puertoSerie.isOpen())
            m_puertoSerie.close();
#endif
        procesar(m_decodificador.feedDatagram(QByteArray()));
        if (habia)
            registrar(tr("Enlace detenido"));
        actualizarEnlace();
    }

    Fuente fuenteActiva() const { return m_fuenteActiva; }
    // ¿Hay una conexión TCP establecida?
    bool tcpConectado() const { return m_tcp.state() == QAbstractSocket::ConnectedState; }
    // Intentos de reconexión sin éxito desde la última vez que hubo enlace.
    int intentosReconexion() const { return m_intentos; }

    // El puerto UDP en el que se escucha (0 si la fuente no es UDP o no se escucha).
    quint16 puertoLocal() const
    {
        return m_fuenteActiva == Fuente::Udp && m_socket.state() == QAbstractSocket::BoundState ? m_socket.localPort()
                                                                                                : quint16(0);
    }

    // Lo que llega por el socket: cada datagrama, a la entrada.
    void leerSocket()
    {
        while (m_socket.hasPendingDatagrams())
            recibirDatagrama(m_socket.receiveDatagram().data());
    }

    // Un datagrama UDP: lo que trae son líneas completas, aunque la última no acabe en salto de línea.
    void recibirDatagrama(const QByteArray &datos)
    {
        ++m_datagramas;
        procesar(m_decodificador.feedDatagram(datos));
    }

    // Un trozo de un flujo (TCP, puerto serie): no trae líneas completas, y la última espera al siguiente trozo.
    void recibirFlujo(const QByteArray &datos)
    {
        ++m_datagramas;
        procesar(m_decodificador.feed(datos));
    }

    // Reproduce una captura NMEA como si llegara en vivo: unas 40 líneas cada 50 ms. Falso si no se puede leer.
    bool reproducir(const QString &fichero)
    {
        QFile f(fichero);
        if (!f.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(tr("No se puede leer %1").arg(fichero), 6000);
            return false;
        }
        m_captura = f.readAll().split('\n');
        m_posCaptura = 0;
        registrar(tr("Reproduciendo la captura %1 (%2 líneas)").arg(QFileInfo(fichero).fileName()).arg(m_captura.size()));
        m_reproductor.disconnect();
        connect(&m_reproductor, &QTimer::timeout, this, [this] {
            for (int i = 0; i < 40 && m_posCaptura < m_captura.size(); ++i, ++m_posCaptura)
                recibirDatagrama(m_captura.at(m_posCaptura) + "\n");
            if (m_posCaptura >= m_captura.size()) {
                m_reproductor.stop();
                registrar(tr("Fin de la captura"));
            }
        });
        m_reproductor.start(50);
        return true;
    }

    // ¿Sigue reproduciendo una captura?
    bool reproduciendo() const { return m_reproductor.isActive(); }

    // Carga el escenario de demostración: el barco propio y tres contactos, como si llegaran por UDP.
    void cargarDemostracion()
    {
        for (const QByteArray &l : escenarioDemostracion(ahora()))
            recibirDatagrama(l + "\r\n");
        tic();
    }

    // Lo que sale del decodificador, al barco propio, a los contactos o al registro.
    void procesar(const QVector<Event> &eventos)
    {
        for (const Event &e : eventos) {
            m_ultimoDato = ahora();
            switch (e.kind) {
            case Event::Kind::Rmc:
                alRmc(e.rmc);
                break;
            case Event::Kind::Gga:
                m_propio.calidad = e.gga.quality;
                m_propio.satelites = e.gga.satellites;
                m_propio.hdop = e.gga.hdop;
                break;
            case Event::Kind::Vtg:
                if (std::isfinite(e.vtg.sogKn))
                    m_propio.sogKn = e.vtg.sogKn;
                if (std::isfinite(e.vtg.cogTrueDeg))
                    m_propio.cogDeg = e.vtg.cogTrueDeg;
                break;
            case Event::Kind::Hdt:
                m_propio.rumboDeg = e.hdt.headingDeg;
                break;
            case Event::Kind::Ais:
                if (e.ownShip)
                    alAisPropio(e.ais);
                else
                    alAjeno(e.ais);
                break;
            case Event::Kind::Bad:
                registrar(tr("Frase descartada (%1): %2").arg(e.detail, QString::fromLatin1(e.raw.left(60))));
                break;
            case Event::Kind::Ignored:
                if (m_verboso->isChecked())
                    registrar(tr("Frase ignorada (%1)").arg(e.detail));
                break;
            }
            if (m_verboso->isChecked() && e.kind != Event::Kind::Bad && e.kind != Event::Kind::Ignored)
                registrar(QString::fromLatin1(e.raw));
        }
        if (!eventos.isEmpty()) {
            actualizarPropioEnMapa();
            actualizarEnlace();
        }
    }

    // Un paso del reloj de revisión: marca los contactos perdidos, borra los muy viejos y refresca la tabla y el enlace.
    void tic()
    {
        revisar();
        refrescar();
        const quint64 lineas = m_decodificador.stats().lines;
        m_tasa.append(double(lineas - m_lineasAntes));
        m_lineasAntes = lineas;
        while (m_tasa.size() > 5)
            m_tasa.removeFirst();
        actualizarEnlace();
    }

    // Marca como perdidos los contactos sin datos desde hace kPerdidoSeg y borra los que llevan kBorrarSeg.
    void revisar()
    {
        bool cambio = false;
        QList<quint32> aBorrar;
        for (auto it = m_contactos.begin(); it != m_contactos.end(); ++it) {
            Contacto &c = it.value();
            const double edad = double(c.ultima.secsTo(ahora()));
            if (edad > kBorrarSeg) {
                aBorrar << c.mmsi;
            } else if (edad > kPerdidoSeg && !c.perdido) {
                c.perdido = true;
                if (c.id >= 0)
                    m_mapa->setTargetAttribute(c.id, QStringLiteral("estado"), QStringLiteral("Perdido"));
                registrar(tr("%1: sin datos, contacto perdido").arg(rotulo(c)));
                cambio = true;
            }
        }
        for (const quint32 mmsi : aBorrar) {
            const Contacto c = m_contactos.take(mmsi);
            if (c.id >= 0)
                m_mapa->removeTarget(c.id);
            registrar(tr("%1: borrado tras %2 min sin datos").arg(rotulo(c)).arg(qRound(kBorrarSeg / 60.0)));
            cambio = true;
        }
        if (cambio)
            aplicarFiltro();
    }

    // Reconstruye la tabla (ordenada por distancia al propio) con el estado actual.
    void refrescar()
    {
        struct Fila {
            Contacto c;
            double distanciaNM = -1.0;
            double marcacion = 0.0;
        };
        QVector<Fila> filas;
        for (const Contacto &c : m_contactos) {
            Fila f;
            f.c = c;
            if (m_propio.hayPosicion && c.posicion.isValid()) {
                f.distanciaNM = m_propio.posicion.distanceTo(c.posicion) / kNM;
                f.marcacion = m_propio.posicion.azimuthTo(c.posicion);
            }
            filas.append(f);
        }
        std::sort(filas.begin(), filas.end(), [](const Fila &a, const Fila &b) {
            if ((a.distanciaNM < 0.0) != (b.distanciaNM < 0.0))
                return a.distanciaNM >= 0.0;            // los que no tienen posición, al final
            return a.distanciaNM < b.distanciaNM;
        });
        const QSignalBlocker bloqueo(m_tabla);
        m_tabla->setRowCount(int(filas.size()));
        int activos = 0, perdidos = 0;
        const auto sino = [](double v, int dec) {
            return std::isfinite(v) ? QString::number(v, 'f', dec) : QStringLiteral("—");
        };
        for (int i = 0; i < filas.size(); ++i) {
            const Fila &f = filas.at(i);
            const Contacto &c = f.c;
            (c.perdido ? perdidos : activos) += 1;
            const QStringList celdas = {
                QString::number(c.mmsi),
                c.nombre.isEmpty() ? QStringLiteral("—") : c.nombre,
                c.tipoBarco > 0 ? telemetria::aisShipTypeText(c.tipoBarco) : QStringLiteral("—"),
                c.claseB ? QStringLiteral("B") : QStringLiteral("A"),
                sino(c.sogKn, 1),
                sino(c.cogDeg, 0),
                f.distanciaNM >= 0.0 ? QString::number(f.distanciaNM, 'f', 2) : QStringLiteral("—"),
                f.distanciaNM >= 0.0 ? QString::number(f.marcacion, 'f', 0) : QStringLiteral("—"),
                c.navegacion >= 0 ? telemetria::aisNavStatusText(c.navegacion) : QStringLiteral("—"),
                QString::number(c.ultima.secsTo(ahora())),
                c.perdido ? tr("Perdido") : tr("Activo"),
            };
            for (int j = 0; j < celdas.size(); ++j) {
                auto *item = new QTableWidgetItem(celdas.at(j));
                if (j == 4 || j == 5 || j == 6 || j == 7 || j == 9)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (c.perdido)
                    item->setForeground(Qt::gray);
                m_tabla->setItem(i, j, item);
            }
        }
        m_contador->setText(tr("Contactos AIS: %1 activos · %2 perdidos").arg(activos).arg(perdidos));
    }

    // Anota una línea en el registro, con la hora; el registro guarda las últimas 500.
    void registrar(const QString &texto)
    {
        if (!m_registro)
            return;
        m_registro->insertItem(0, QStringLiteral("%1  %2").arg(ahora().toString(QStringLiteral("HH:mm:ss")), texto));
        while (m_registro->count() > 500)
            delete m_registro->takeItem(m_registro->count() - 1);
    }

    // El mapa cambió de centro. Si fue porque la persona lo arrastra con el ratón (\a arrastrando) y no porque lo recentre el
    // seguimiento, deja de seguir al barco propio: quien mueve el mapa quiere verlo donde lo pone. Volver a marcar la casilla
    // lo recentra y sigue de nuevo.
    void alCambiarCentro(bool arrastrando)
    {
        if (!arrastrando || m_centrando || !m_seguir->isChecked())
            return;
        m_seguir->setChecked(false);
        statusBar()->showMessage(tr("Has movido el mapa: ya no se centra en tu barco (marca «Centrar el mapa en mi barco» para seguirlo)."), 8000);
    }

    // Pone el reloj a una hora fija (para las pruebas: así se simula el paso del tiempo sin esperar).
    void fijarReloj(const QDateTime &hora) { m_relojFalso = hora; }
    // La hora de la que dependen las edades: la del reloj fijado o, si no, la actual.
    QDateTime ahora() const { return m_relojFalso.isValid() ? m_relojFalso : QDateTime::currentDateTimeUtc(); }

    // Datos para las pruebas.
    const Propio &propio() const { return m_propio; }
    const QMap<quint32, Contacto> &contactos() const { return m_contactos; }
    const telemetria::DecoderStats &estadisticas() const { return m_decodificador.stats(); }
    quint64 datagramas() const { return m_datagramas; }
    QTableWidget *tabla() const { return m_tabla; }
    QCheckBox *casillaSeguir() const { return m_seguir; }
    QListWidget *listaGlosario() const { return m_listaGlosario; }
    QString textoPropio() const { return m_textoPropio->text(); }
    QString textoEstadoEnlace() const { return m_estado->text(); }
    QString textoBotonEnlace() const { return m_botonEscuchar->text(); }
    int indiceFuente() const { return m_fuente->currentIndex(); }
    QString textoEstadisticas() const { return m_textoEstadisticas->text(); }
    QStringList registro() const
    {
        QStringList l;
        for (int i = 0; i < m_registro->count(); ++i)
            l << m_registro->item(i)->text();
        return l;
    }

private:
    // Cablea los sockets de TCP y el puerto serie: los datos van al decodificador y la pérdida del enlace, al reintento.
    void conectarFuentes()
    {
        m_reconectar.setSingleShot(true);
        connect(&m_reconectar, &QTimer::timeout, this, [this] { reintentar(); });
        connect(&m_tcp, &QTcpSocket::connected, this, [this] {
            m_intentos = 0;
            registrar(tr("Conectado por TCP a %1:%2").arg(m_tcpHost).arg(m_tcpPuerto));
            actualizarEnlace();
        });
        connect(&m_tcp, &QTcpSocket::readyRead, this, [this] { recibirFlujo(m_tcp.readAll()); });
        // stateChanged en vez de las señales de error: sirve igual en Qt 5.14, 5.15 y 6, y cubre el servidor que no
        // está, el que cierra y la red que cae (todo acaba en «desconectado»).
        connect(&m_tcp, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState estado) {
            if (estado == QAbstractSocket::UnconnectedState && m_fuenteActiva == Fuente::Tcp)
                alPerderFuente(tr("TCP %1:%2").arg(m_tcpHost).arg(m_tcpPuerto), m_tcp.errorString());
            else
                actualizarEnlace();
        });
#ifdef TELEMETRIA_CON_SERIE
        connect(&m_puertoSerie, &QSerialPort::readyRead, this, [this] { recibirFlujo(m_puertoSerie.readAll()); });
        connect(&m_puertoSerie, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
            // Sobre todo ResourceError: el dispositivo desapareció (se desenchufó el cable USB).
            if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError || m_fuenteActiva != Fuente::Serie
                || !m_puertoSerie.isOpen())
                return;
            const QString motivo = m_puertoSerie.errorString();
            m_puertoSerie.close();
            alPerderFuente(tr("Puerto serie %1").arg(m_serieNombre), motivo);
        });
#endif
    }

    // El enlace de TCP o serie no está (no se pudo abrir o se cayó): entrega lo que hubiera a medias, lo anota (el primer
    // intento y luego uno de cada diez, para no llenar el registro) y programa el siguiente intento.
    void alPerderFuente(const QString &quien, const QString &motivo)
    {
        procesar(m_decodificador.feedDatagram(QByteArray()));
        if (m_intentos % 10 == 0)
            registrar(tr("%1: sin enlace (%2). Reintento cada %3 s").arg(quien, motivo).arg(kReconexionMs / 1000));
        ++m_intentos;
        m_reconectar.start(kReconexionMs);
        actualizarEnlace();
    }

    // Un intento de reconexión: vuelve a conectar el TCP o a abrir el puerto serie, si la fuente sigue siendo esa.
    void reintentar()
    {
        if (m_fuenteActiva == Fuente::Tcp && m_tcp.state() == QAbstractSocket::UnconnectedState)
            m_tcp.connectToHost(m_tcpHost, m_tcpPuerto);
#ifdef TELEMETRIA_CON_SERIE
        else if (m_fuenteActiva == Fuente::Serie && !m_puertoSerie.isOpen())
            abrirSerieAhora();
#endif
    }

#ifdef TELEMETRIA_CON_SERIE
    // Abre m_serieNombre a m_baudios (8N1, solo lectura). Si no puede, lo deja para el reintento.
    bool abrirSerieAhora()
    {
        m_puertoSerie.setPortName(m_serieNombre);
        m_puertoSerie.setBaudRate(m_baudios);
        m_puertoSerie.setDataBits(QSerialPort::Data8);
        m_puertoSerie.setParity(QSerialPort::NoParity);
        m_puertoSerie.setStopBits(QSerialPort::OneStop);
        m_puertoSerie.setFlowControl(QSerialPort::NoFlowControl);
        if (!m_puertoSerie.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(tr("No se puede abrir %1: %2").arg(m_serieNombre, m_puertoSerie.errorString()), 8000);
            alPerderFuente(tr("Puerto serie %1").arg(m_serieNombre), m_puertoSerie.errorString());
            return false;
        }
        m_intentos = 0;
        registrar(tr("Puerto serie %1 abierto a %2 baudios").arg(m_serieNombre).arg(m_baudios));
        actualizarEnlace();
        return true;
    }

    // Rellena la lista de puertos serie con los que ve el sistema (se puede escribir otro nombre a mano).
    void refrescarPuertosSerie()
    {
        const QString actual = m_serie->currentText();
        m_serie->clear();
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
            m_serie->addItem(info.portName());
        m_serie->setEditText(actual);
    }
#endif

    // El texto de estado de la fuente activa.
    QString textoFuente() const
    {
        switch (m_fuenteActiva) {
        case Fuente::Udp:
            return m_socket.state() == QAbstractSocket::BoundState ? tr("Escuchando en UDP %1").arg(m_socket.localPort())
                                                                   : tr("Parado");
        case Fuente::Tcp:
            if (m_tcp.state() == QAbstractSocket::ConnectedState)
                return tr("Conectado por TCP a %1:%2").arg(m_tcpHost).arg(m_tcpPuerto);
            if (m_reconectar.isActive())
                return tr("Sin conexión con %1:%2: reintentando cada %3 s (%4 intentos)")
                    .arg(m_tcpHost).arg(m_tcpPuerto).arg(kReconexionMs / 1000).arg(m_intentos);
            return tr("Conectando a %1:%2...").arg(m_tcpHost).arg(m_tcpPuerto);
        case Fuente::Serie:
#ifdef TELEMETRIA_CON_SERIE
            if (m_puertoSerie.isOpen())
                return tr("Puerto serie %1 abierto a %2 baudios").arg(m_serieNombre).arg(m_baudios);
#endif
            return tr("Puerto serie %1 no disponible: reintentando cada %2 s (%3 intentos)")
                .arg(m_serieNombre).arg(kReconexionMs / 1000).arg(m_intentos);
        case Fuente::Ninguna:
            break;
        }
        return tr("Parado");
    }

    // Cómo se llama un contacto en los avisos: su nombre o, si aún no se sabe, su MMSI.
    static QString rotulo(const Contacto &c)
    {
        return c.nombre.isEmpty() ? QString::number(c.mmsi) : c.nombre;
    }

    // Color del símbolo y la traza: verde el propio, naranja la clase A y turquesa la clase B.
    static QColor colorDe(bool propio, bool claseB)
    {
        if (propio)
            return QColor(0x2e, 0x7d, 0x32);
        return claseB ? QColor(0x00, 0x83, 0x8f) : QColor(0xff, 0x8f, 0x00);
    }

    // Una RMC: mueve el barco propio si el receptor tiene fijado; si no, solo lo anota.
    void alRmc(const telemetria::Rmc &r)
    {
        m_propio.fijado = r.valid;
        if (!r.valid || !std::isfinite(r.lat) || !std::isfinite(r.lon))
            return;
        m_propio.posicion = QGeoCoordinate(r.lat, r.lon);
        m_propio.hayPosicion = true;
        m_propio.ultima = ahora();
        if (std::isfinite(r.sogKn))
            m_propio.sogKn = r.sogKn;
        if (std::isfinite(r.cogDeg))
            m_propio.cogDeg = r.cogDeg;
    }

    // El informe AIS del propio barco (!AIVDO): sirve de posición si no hay RMC con fijado fresca (un AIS sin GPS).
    void alAisPropio(const AisMessage &m)
    {
        if (!m.hasPosition() || (m_propio.fijado && m_propio.ultima.secsTo(ahora()) < 10))
            return;                                     // la RMC manda mientras esté fresca
        m_propio.posicion = QGeoCoordinate(m.lat, m.lon);
        m_propio.hayPosicion = true;
        m_propio.ultima = ahora();
        if (std::isfinite(m.sogKn))
            m_propio.sogKn = m.sogKn;
        if (std::isfinite(m.cogDeg))
            m_propio.cogDeg = m.cogDeg;
        if (m.headingDeg >= 0)
            m_propio.rumboDeg = m.headingDeg;
    }

    // Un mensaje AIS de otro barco: se funde en su contacto (el MMSI es la clave) y, si trae posición, mueve
    // su objetivo en el mapa. Los mensajes estáticos (nombre, tipo, tamaño) pueden llegar antes o después.
    void alAjeno(const AisMessage &m)
    {
        Contacto &c = m_contactos[m.mmsi];
        c.mmsi = m.mmsi;
        c.ultima = ahora();
        ++c.informes;
        if (c.perdido) {
            c.perdido = false;
            if (c.id >= 0)
                m_mapa->setTargetAttribute(c.id, QStringLiteral("estado"), QStringLiteral("Activo"));
            registrar(tr("%1: vuelve a emitir").arg(rotulo(c)));
        }
        if (m.type == 18 || m.type == 19 || m.type == 24)
            c.claseB = true;
        else if (m.type >= 1 && m.type <= 5)
            c.claseB = false;
        // Los datos estáticos (los que no vienen vacíos) se funden con lo que ya se sabía.
        const QString nombreAntes = c.nombre;
        if (!m.name.isEmpty())
            c.nombre = m.name;
        if (!m.callSign.isEmpty())
            c.indicativo = m.callSign;
        if (!m.destination.isEmpty())
            c.destino = m.destination;
        if (m.shipType > 0)
            c.tipoBarco = m.shipType;
        if (m.lengthM() > 0)
            c.esloraM = m.lengthM();
        if (m.hasPosition()) {
            c.posicion = QGeoCoordinate(m.lat, m.lon);
            c.sogKn = m.sogKn;
            c.cogDeg = m.cogDeg;
            c.rumboDeg = m.headingDeg;
            if (m.navStatus >= 0)
                c.navegacion = m.navStatus;
            ponerEnMapa(&c);
        } else if (c.id >= 0 && c.nombre != nombreAntes) {
            m_mapa->setTargetLabel(c.id, rotulo(c));     // el nombre llegó después de la posición
        }
    }

    // Pone el contacto en el mapa (lo crea la primera vez que tiene posición) con su rumbo y velocidad.
    void ponerEnMapa(Contacto *c)
    {
        const double rumbo = c->rumboDeg >= 0 ? double(c->rumboDeg) : (std::isfinite(c->cogDeg) ? c->cogDeg : 0.0);
        if (c->id < 0) {
            MapTarget t;
            t.label = rotulo(*c);
            t.kind = c->claseB ? QStringLiteral("claseB") : QStringLiteral("claseA");
            t.position = c->posicion;
            t.headingDeg = rumbo;
            t.color = colorDe(false, c->claseB);
            t.attributes.insert(QStringLiteral("mmsi"), c->mmsi);
            t.attributes.insert(QStringLiteral("estado"), QStringLiteral("Activo"));
            c->id = m_mapa->addTarget(t, ahora());
        } else {
            m_mapa->updateTarget(c->id, c->posicion, rumbo, ahora());
            m_mapa->setTargetLabel(c->id, rotulo(*c));
        }
        if (std::isfinite(c->sogKn))
            m_mapa->setTargetVelocity(c->id, knotsToMetersPerSecond(c->sogKn), std::isfinite(c->cogDeg) ? c->cogDeg : rumbo);
    }

    // Pinta el barco propio en el mapa (lo crea la primera vez) y, si se sigue, centra el mapa en él.
    void actualizarPropioEnMapa()
    {
        if (!m_propio.hayPosicion)
            return;
        const double rumbo = std::isfinite(m_propio.rumboDeg) ? m_propio.rumboDeg
                             : std::isfinite(m_propio.cogDeg) ? m_propio.cogDeg : 0.0;
        if (m_propio.id < 0) {
            MapTarget t;
            t.label = tr("Propio");
            t.kind = QStringLiteral("propio");
            t.position = m_propio.posicion;
            t.headingDeg = rumbo;
            t.color = colorDe(true, false);
            t.attributes.insert(QStringLiteral("estado"), QStringLiteral("Activo"));
            m_propio.id = m_mapa->addTarget(t, ahora());
            aplicarFiltro();
        } else {
            m_mapa->updateTarget(m_propio.id, m_propio.posicion, rumbo, ahora());
        }
        if (std::isfinite(m_propio.sogKn) && std::isfinite(m_propio.cogDeg))
            m_mapa->setTargetVelocity(m_propio.id, knotsToMetersPerSecond(m_propio.sogKn), m_propio.cogDeg);
        if (m_seguir->isChecked())
            seguirPropio();
    }

    // Mantiene el barco propio a la vista SIN mover el mapa a cada dato: solo lo recentra cuando el barco sale de la
    // zona central (el 35 % del medio ancho de la ventana). Centrar a cada RMC (a 1 Hz, o a 10 Hz con --escala) hace
    // que el mapa tiemble sin parar y no deje moverlo con el ratón.
    void seguirPropio()
    {
        const double mitadMetros = 0.5 * std::min(m_mapa->width(), m_mapa->height()) * m_mapa->metersPerPixel();
        if (m_mapa->center().distanceTo(m_propio.posicion) <= 0.35 * mitadMetros)
            return;
        m_centrando = true;
        m_mapa->setCenter(m_propio.posicion);
        m_centrando = false;
    }

    // Atenúa en el mapa los contactos perdidos (el propio pasa siempre).
    void aplicarFiltro()
    {
        TargetFilter f;
        f.predicate = [](const MapTarget &t) {
            return t.attributes.value(QStringLiteral("estado")).toString() != QLatin1String("Perdido");
        };
        f.dim = true;
        m_mapa->setTargetFilter(f);
    }

    // Pone los textos del panel de enlace y del barco propio con el estado actual.
    void actualizarEnlace()
    {
        if (!m_estado)
            return;
        m_estado->setText(textoFuente());
        static const QString verbos[] = { QObject::tr("Escuchar"), QObject::tr("Conectar"), QObject::tr("Abrir") };
        m_botonEscuchar->setText(m_fuenteActiva != Fuente::Ninguna ? tr("Parar")
                                                                   : verbos[qBound(0, m_fuente->currentIndex(), 2)]);
        const telemetria::DecoderStats &s = m_decodificador.stats();
        double tasa = 0.0;
        for (const double v : m_tasa)
            tasa += v;
        tasa = m_tasa.isEmpty() ? 0.0 : tasa / double(m_tasa.size());
        m_ultimo->setText(m_ultimoDato.isValid() ? tr("Último dato hace %1 s").arg(m_ultimoDato.secsTo(ahora()))
                                                 : tr("Sin datos todavía"));
        m_textoEstadisticas->setText(
            tr("Frases por segundo: %1\nRecepciones: %2 · líneas: %3\nDecodificadas: %4\nSuma mala: %5 · mal formadas: %6\n"
               "Ignoradas: %7\nAIS completos: %8 (no soportados: %9)\nFragmentos perdidos: %10")
                .arg(tasa, 0, 'f', 1).arg(m_datagramas).arg(s.lines).arg(s.decoded).arg(s.badChecksum).arg(s.malformed)
                .arg(s.ignored).arg(s.aisMessages).arg(s.aisUnsupported).arg(s.aisLost));

        QString propio;
        if (!m_propio.hayPosicion) {
            propio = m_propio.fijado ? tr("Sin posición todavía.") : tr("Sin fijado (esperando una RMC con estado A).");
        } else {
            const auto num = [](double v, int dec, const QString &u) {
                return std::isfinite(v) ? QStringLiteral("%1 %2").arg(v, 0, 'f', dec).arg(u) : QStringLiteral("—");
            };
            propio = tr("%1\nSOG %2 · COG %3\nRumbo (proa) %4\nFijado: calidad %5, %6 satélites, HDOP %7\nÚltima posición hace %8 s")
                         .arg(formatCoordinate(m_propio.posicion, CoordinateFormat::Decimal), num(m_propio.sogKn, 1, tr("kn")),
                              num(m_propio.cogDeg, 0, QStringLiteral("°")), num(m_propio.rumboDeg, 0, QStringLiteral("°")))
                         .arg(m_propio.calidad)
                         .arg(m_propio.satelites >= 0 ? QString::number(m_propio.satelites) : QStringLiteral("—"),
                              std::isfinite(m_propio.hdop) ? QString::number(m_propio.hdop, 'f', 1) : QStringLiteral("—"))
                         .arg(m_propio.ultima.secsTo(ahora()));
        }
        m_textoPropio->setText(propio);
    }

    // Menús: Archivo (captura, demostración, salir) y Ayuda (glosario, F1).
    void crearMenus()
    {
        QMenu *archivo = menuBar()->addMenu(tr("&Archivo"));
        connect(archivo->addAction(tr("Reproducir una &captura NMEA...")), &QAction::triggered, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Captura NMEA"), QString(),
                                                           tr("Capturas NMEA (*.nmea *.txt *.log);;Todos (*)"));
            if (!f.isEmpty())
                reproducir(f);
        });
        connect(archivo->addAction(tr("Cargar el escenario de &demostración")), &QAction::triggered, this,
                [this] { cargarDemostracion(); });
        archivo->addSeparator();
        connect(archivo->addAction(tr("&Salir")), &QAction::triggered, this, &QWidget::close);

        QMenu *ayuda = menuBar()->addMenu(tr("A&yuda"));
        QAction *glosarioAct = m_dockGlosario->toggleViewAction();
        glosarioAct->setText(tr("&Glosario"));
        glosarioAct->setShortcut(QKeySequence(Qt::Key_F1));
        ayuda->addAction(glosarioAct);
    }

    // Paneles: enlace y barco propio (izquierda), contactos y registro (abajo) y glosario (derecha).
    void crearPaneles(int puerto)
    {
        const auto terminos = glosario();

        // --- Enlace y barco propio ---
        auto *izquierda = new QWidget(this);
        auto *col = new QVBoxLayout(izquierda);
        m_fuente = new QComboBox(this);
        m_fuente->addItem(tr("UDP (escuchar un puerto)"), int(Fuente::Udp));
        m_fuente->addItem(tr("TCP (conectar a un servidor)"), int(Fuente::Tcp));
        m_fuente->addItem(tr("Puerto serie"), int(Fuente::Serie));
        m_fuente->setToolTip(tr("De dónde llegan las frases: %1\n\n%2\n\n%3")
                                 .arg(termino(QObject::tr("UDP")), termino(QObject::tr("TCP")), termino(QObject::tr("Puerto serie"))));
        col->addWidget(new QLabel(tr("Fuente de datos:"), this));
        col->addWidget(m_fuente);
        m_campos = new QStackedWidget(this);
        {   // UDP: el puerto en el que se escucha.
            auto *pagina = new QWidget(this);
            auto *f = new QHBoxLayout(pagina);
            f->setContentsMargins(0, 0, 0, 0);
            m_puerto = new QSpinBox(this);
            m_puerto->setRange(1, 65535);
            m_puerto->setValue(puerto);
            m_puerto->setToolTip(termino(QObject::tr("Puerto")));
            f->addWidget(new QLabel(tr("Puerto UDP:"), this));
            f->addWidget(m_puerto);
            m_campos->addWidget(pagina);
        }
        {   // TCP: el servidor y su puerto.
            auto *pagina = new QWidget(this);
            auto *f = new QHBoxLayout(pagina);
            f->setContentsMargins(0, 0, 0, 0);
            m_host = new QLineEdit(QStringLiteral("127.0.0.1"), this);
            m_host->setToolTip(termino(QObject::tr("TCP")));
            m_puertoTcp = new QSpinBox(this);
            m_puertoTcp->setRange(1, 65535);
            m_puertoTcp->setValue(kPuertoPorOmision);
            m_puertoTcp->setToolTip(termino(QObject::tr("Puerto")));
            f->addWidget(new QLabel(tr("Servidor:"), this));
            f->addWidget(m_host, 1);
            f->addWidget(m_puertoTcp);
            m_campos->addWidget(pagina);
        }
        {   // Puerto serie: el nombre (con los que ve el sistema) y los baudios.
            auto *pagina = new QWidget(this);
            auto *f = new QHBoxLayout(pagina);
            f->setContentsMargins(0, 0, 0, 0);
            m_serie = new QComboBox(this);
            m_serie->setEditable(true);
            m_serie->setToolTip(termino(QObject::tr("Puerto serie")));
            m_baudiosCombo = new QComboBox(this);
            for (const int b : { 4800, 9600, 19200, 38400, 57600, 115200 })
                m_baudiosCombo->addItem(QString::number(b));
            m_baudiosCombo->setCurrentText(QString::number(kBaudiosPorOmision));
            m_baudiosCombo->setToolTip(termino(QObject::tr("Baudios")));
            f->addWidget(new QLabel(tr("Puerto:"), this));
            f->addWidget(m_serie, 1);
            f->addWidget(m_baudiosCombo);
            f->addWidget(new QLabel(tr("baudios"), this));
            if (!tieneSerie()) {
                pagina->setEnabled(false);
                pagina->setToolTip(tr("Esta compilación no incluye el módulo Qt Serial Port (QT += serialport)."));
            }
            m_campos->addWidget(pagina);
        }
        col->addWidget(m_campos);
        m_botonEscuchar = new QPushButton(tr("Escuchar"), this);
        col->addWidget(m_botonEscuchar);
        m_estado = new QLabel(this);
        m_ultimo = new QLabel(this);
        m_textoEstadisticas = new QLabel(this);
        m_textoEstadisticas->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_textoEstadisticas->setToolTip(termino(QObject::tr("Suma de comprobación")));
        col->addWidget(m_estado);
        col->addWidget(m_ultimo);
        col->addWidget(m_textoEstadisticas);
        col->addWidget(new QLabel(tr("<b>Barco propio</b>"), this));
        m_textoPropio = new QLabel(this);
        m_textoPropio->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_textoPropio->setWordWrap(true);
        col->addWidget(m_textoPropio);
        m_seguir = new QCheckBox(tr("Centrar el mapa en mi barco"), this);
        m_seguir->setToolTip(tr("Mantiene el barco a la vista: recentra el mapa solo cuando sale de la zona central. "
                                "Si arrastras el mapa, se desmarca sola."));
        m_verboso = new QCheckBox(tr("Anotar todas las frases en el registro"), this);
        col->addWidget(m_seguir);
        col->addWidget(m_verboso);
        col->addStretch(1);
        auto *dockIzq = new QDockWidget(tr("Enlace"), this);
        dockIzq->setWidget(izquierda);
        addDockWidget(Qt::LeftDockWidgetArea, dockIzq);
        connect(m_fuente, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
            m_campos->setCurrentIndex(i);
#ifdef TELEMETRIA_CON_SERIE
            if (i == 2)
                refrescarPuertosSerie();
#endif
            actualizarEnlace();
        });
        connect(m_botonEscuchar, &QPushButton::clicked, this, [this] {
            if (m_fuenteActiva != Fuente::Ninguna) {
                parar();
                return;
            }
            switch (m_fuente->currentIndex()) {
            case 0: escuchar(quint16(m_puerto->value())); break;
            case 1: conectarTcp(m_host->text(), quint16(m_puertoTcp->value())); break;
            default: abrirSerie(m_serie->currentText(), m_baudiosCombo->currentText().toInt()); break;
            }
        });

        // --- Tabla de contactos y registro, en pestañas ---
        m_tabla = new QTableWidget(0, int(columnas().size()), this);
        QStringList cabeceras;
        for (const Columna &c : columnas())
            cabeceras << c.texto;
        m_tabla->setHorizontalHeaderLabels(cabeceras);
        for (int i = 0; i < columnas().size(); ++i)
            m_tabla->horizontalHeaderItem(i)->setToolTip(termino(columnas().at(i).clave));   // la ayuda de cada cabecera
        m_tabla->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tabla->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tabla->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tabla->verticalHeader()->setVisible(false);
        m_tabla->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_registro = new QListWidget(this);
        m_registro->setWordWrap(true);
        m_contador = new QLabel(this);
        statusBar()->addPermanentWidget(m_contador);
        auto *pestanas = new QTabWidget(this);
        pestanas->addTab(m_tabla, tr("Contactos AIS"));
        pestanas->addTab(m_registro, tr("Registro"));
        auto *dockTabla = new QDockWidget(tr("Contactos"), this);
        dockTabla->setWidget(pestanas);
        addDockWidget(Qt::BottomDockWidgetArea, dockTabla);
        connect(m_tabla, &QTableWidget::itemSelectionChanged, this, [this] {
            const auto filas = m_tabla->selectionModel()->selectedRows();
            if (filas.isEmpty())
                return;
            const Contacto c = m_contactos.value(m_tabla->item(filas.first().row(), 0)->text().toUInt());
            if (c.id >= 0)
                m_mapa->setSelectedTarget(c.id);
        });

        // --- Glosario ---
        auto *derecha = new QWidget(this);
        auto *colDer = new QVBoxLayout(derecha);
        auto *buscar = new QLineEdit(this);
        buscar->setPlaceholderText(tr("Buscar un término..."));
        buscar->setClearButtonEnabled(true);
        m_listaGlosario = new QListWidget(this);
        auto *definicion = new QTextBrowser(this);
        definicion->setMaximumHeight(170);
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

    // La definición del glosario del término \a nombre (vacía si no está).
    static QString termino(const QString &nombre)
    {
        for (const Termino &t : glosario())
            if (t.nombre == nombre)
                return t.definicion;
        return QString();
    }

    MapWidget *m_mapa = nullptr;
    QUdpSocket m_socket;
    QTcpSocket m_tcp;
    QTimer m_reconectar;                      // el siguiente intento de TCP o de puerto serie
    Fuente m_fuenteActiva = Fuente::Ninguna;
    QString m_tcpHost;
    quint16 m_tcpPuerto = 0;
    QString m_serieNombre;
    int m_baudios = kBaudiosPorOmision;
    int m_intentos = 0;
#ifdef TELEMETRIA_CON_SERIE
    QSerialPort m_puertoSerie;
#endif
    telemetria::Decoder m_decodificador;
    QTimer m_reloj;
    QTimer m_reproductor;
    QList<QByteArray> m_captura;
    int m_posCaptura = 0;
    QDateTime m_relojFalso;
    QDateTime m_ultimoDato;
    quint64 m_datagramas = 0;
    quint64 m_lineasAntes = 0;
    QList<double> m_tasa;                     // líneas por segundo de los últimos tics
    Propio m_propio;
    QMap<quint32, Contacto> m_contactos;      // por MMSI
    QSpinBox *m_puerto = nullptr;
    QComboBox *m_fuente = nullptr;
    QStackedWidget *m_campos = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_puertoTcp = nullptr;
    QComboBox *m_serie = nullptr;
    QComboBox *m_baudiosCombo = nullptr;
    QPushButton *m_botonEscuchar = nullptr;
    QLabel *m_estado = nullptr;
    QLabel *m_ultimo = nullptr;
    QLabel *m_textoEstadisticas = nullptr;
    QLabel *m_textoPropio = nullptr;
    QLabel *m_contador = nullptr;
    QCheckBox *m_seguir = nullptr;
    QCheckBox *m_verboso = nullptr;
    bool m_centrando = false;                 // el centro lo está moviendo el seguimiento, no la persona
    QTableWidget *m_tabla = nullptr;
    QListWidget *m_registro = nullptr;
    QListWidget *m_listaGlosario = nullptr;
    QDockWidget *m_dockGlosario = nullptr;
};

// Texto de la columna \a col de la fila \a fila de la tabla, o vacío.
static QString celda(QTableWidget *t, int fila, int col)
{
    return t->item(fila, col) ? t->item(fila, col)->text() : QString();
}

// Fila de la tabla cuyo MMSI es \a mmsi, o -1.
static int filaDe(QTableWidget *t, quint32 mmsi)
{
    for (int i = 0; i < t->rowCount(); ++i)
        if (celda(t, i, 0) == QString::number(mmsi))
            return i;
    return -1;
}

// Cuántos objetivos hay en el mapa con \a clase (MapTarget::kind).
static int objetivosDe(MapWidget *mapa, const QString &clase)
{
    int n = 0;
    for (const MapTarget &t : mapa->targets())
        n += t.kind == clase ? 1 : 0;
    return n;
}

// El objetivo del mapa con la etiqueta \a etiqueta (o uno vacío).
static MapTarget objetivoConEtiqueta(MapWidget *mapa, const QString &etiqueta)
{
    for (const MapTarget &t : mapa->targets())
        if (t.label == etiqueta)
            return t;
    return MapTarget();
}

// Espera hasta \a ms milisegundos a que \a listo sea cierta, atendiendo los eventos de Qt (los sockets).
template <typename F>
static bool esperar(int ms, F listo)
{
    QElapsedTimer reloj;
    reloj.start();
    while (!listo() && reloj.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return listo();
}

// Prueba sin ventana: barco propio, contactos, fusión de datos estáticos, ruido, mensaje publicado de
// referencia, contacto perdido y borrado, UDP real, una captura y el glosario.
static int comprobar(Ventana &v)
{
    QTextStream out(stdout);
    bool todoOk = true;
    const auto verificar = [&](const QString &que, bool ok) {
        out << (ok ? "ok    " : "FALLO ") << que << "\n";
        todoOk = todoOk && ok;
    };
    verificar(QStringLiteral("mapa listo"), v.mapa()->isReady());
    v.detenerReloj();
    const QDateTime t0 = telemetria::utcDateTime(QDate(2026, 10, 19), QTime(12, 0, 0));
    v.fijarReloj(t0);

    // --- Sin datos: no se inventa nada ---
    v.tic();
    verificar(QStringLiteral("sin datos no hay barco propio ni contactos y la ventana lo dice"),
              !v.propio().hayPosicion && v.contactos().isEmpty() && v.textoPropio().contains(QStringLiteral("Sin"))
              && v.tabla()->rowCount() == 0);

    // --- Escucha de un puerto UDP libre ---
    verificar(QStringLiteral("se puede escuchar un puerto UDP libre"), v.escuchar(0) && v.puertoLocal() > 0);

    // --- El escenario de demostración, un datagrama por línea ---
    for (const QByteArray &l : escenarioDemostracion(t0))
        v.recibirDatagrama(l + "\r\n");
    v.tic();
    const Propio &yo = v.propio();
    out << "      propio: " << yo.posicion.latitude() << ", " << yo.posicion.longitude() << "; " << v.contactos().size()
        << " contactos; " << v.datagramas() << " datagramas\n";
    verificar(QStringLiteral("la RMC pone el barco propio en 23,20 N 82,60 O, a 10 kn y rumbo 90°"),
              yo.hayPosicion && std::abs(yo.posicion.latitude() - 23.20) < 1e-5 && std::abs(yo.posicion.longitude() + 82.60) < 1e-5
              && yo.sogKn == 10.0 && yo.cogDeg == 90.0 && yo.fijado);
    verificar(QStringLiteral("la GGA da calidad 1, 9 satélites y HDOP 0,9; la HDT, el rumbo de la proa"),
              yo.calidad == 1 && yo.satelites == 9 && yo.hdop == 0.9 && yo.rumboDeg == 90.0);
    verificar(QStringLiteral("el panel del barco propio lo muestra"),
              v.textoPropio().contains(QStringLiteral("23.2")) && v.textoPropio().contains(QStringLiteral("9 satélites")));
    verificar(QStringLiteral("el barco propio está en el mapa (verde, rumbo 90°)"),
              objetivosDe(v.mapa(), QStringLiteral("propio")) == 1
              && objetivoConEtiqueta(v.mapa(), QStringLiteral("Propio")).headingDeg == 90.0);
    verificar(QStringLiteral("hay 3 contactos AIS, dos de clase A y uno de clase B, y los 3 están en el mapa"),
              v.contactos().size() == 3 && objetivosDe(v.mapa(), QStringLiteral("claseA")) == 2
              && objetivosDe(v.mapa(), QStringLiteral("claseB")) == 1);
    const Contacto carguero = v.contactos().value(323000101);
    const Contacto pesquero = v.contactos().value(323000103);
    verificar(QStringLiteral("los estáticos se funden con la posición: el carguero (tipo 5, dos frases) tiene nombre, tipo, eslora y destino"),
              carguero.nombre == QStringLiteral("HABANA STAR") && carguero.tipoBarco == 70 && carguero.esloraM == 180
              && carguero.destino == QStringLiteral("LA HABANA") && !carguero.claseB && carguero.rumboDeg == 270);
    verificar(QStringLiteral("y el pesquero (tipo 18 más tipo 24, partes A y B) también: nombre, tipo, eslora y clase B"),
              pesquero.nombre == QStringLiteral("PESCADOR 7") && pesquero.tipoBarco == 30 && pesquero.esloraM == 12
              && pesquero.claseB && pesquero.indicativo == QStringLiteral("COPE07"));
    verificar(QStringLiteral("el objetivo del mapa lleva el nombre como etiqueta y la posición del informe"),
              objetivoConEtiqueta(v.mapa(), QStringLiteral("HABANA STAR")).position.distanceTo(QGeoCoordinate(23.20, -82.35)) < 5.0);

    // La tabla va ordenada por distancia al propio. Desde 23,20/-82,60: pesquero ~9,1 NM, petrolero ~9,4 NM, carguero ~13,8 NM.
    const double dPesquero = std::hypot(0.12 * 60.0, 0.10 * 60.0 * std::cos(23.2 * M_PI / 180.0));
    out << "      tabla:";
    for (int i = 0; i < v.tabla()->rowCount(); ++i)
        out << " | " << celda(v.tabla(), i, 1) << " " << celda(v.tabla(), i, 6) << " NM, " << celda(v.tabla(), i, 7) << "°";
    out << "\n";
    verificar(QStringLiteral("la tabla tiene 3 filas ordenadas por distancia: PESCADOR 7, TANKER SUR, HABANA STAR"),
              v.tabla()->rowCount() == 3 && celda(v.tabla(), 0, 1) == QStringLiteral("PESCADOR 7")
              && celda(v.tabla(), 1, 1) == QStringLiteral("TANKER SUR") && celda(v.tabla(), 2, 1) == QStringLiteral("HABANA STAR"));
    verificar(QStringLiteral("la distancia al pesquero es la del cálculo a mano (%1 NM) y su marcación ~37°").arg(dPesquero, 0, 'f', 2),
              std::abs(celda(v.tabla(), 0, 6).toDouble() - dPesquero) < 0.1 && std::abs(celda(v.tabla(), 0, 7).toDouble() - 37.0) < 2.0);
    verificar(QStringLiteral("la tabla muestra tipo, clase y estado de navegación en español"),
              celda(v.tabla(), 2, 2) == QStringLiteral("Carguero") && celda(v.tabla(), 2, 3) == QStringLiteral("A")
              && celda(v.tabla(), 0, 3) == QStringLiteral("B") && celda(v.tabla(), 0, 2) == QStringLiteral("Pesquero")
              && celda(v.tabla(), 2, 8) == QStringLiteral("Navegando a motor") && celda(v.tabla(), 2, 10) == QStringLiteral("Activo"));
    verificar(QStringLiteral("cada cabecera de la tabla tiene su ayuda del glosario"),
              v.tabla()->horizontalHeaderItem(6)->toolTip().contains(QStringLiteral("1852")));
    verificar(QStringLiteral("por omisión el mapa NO sigue al barco: tras recibir su posición (a 15 km) sigue en el centro inicial"),
              !v.casillaSeguir()->isChecked()
              && (v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(QGeoCoordinate(23.20, -82.45)) < 200.0));
    v.casillaSeguir()->setChecked(true);
    verificar(QStringLiteral("marcar «Centrar el mapa en mi barco» lo centra en él (cuando el paquete tiene zoom suficiente)"),
              v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(yo.posicion) < 200.0);
    // El seguimiento no mueve el mapa a cada dato (eso lo hacía temblar y no dejaba moverlo): un barco que se mueve poco dentro
    // de la zona central no lo toca; uno que sale de ella, lo recentra.
    const QGeoCoordinate centroAntes = v.mapa()->center();
    telemetria::Rmc poco;
    poco.valid = true;
    poco.time = t0.time();
    poco.utc = t0;
    poco.lat = 23.20;
    poco.lon = -82.599;                                       // ~100 m al este
    poco.sogKn = 10.0;
    poco.cogDeg = 90.0;
    v.recibirDatagrama(telemetria::formatRmc(poco) + "\r\n");
    verificar(QStringLiteral("un barco que se mueve ~100 m no mueve el mapa"),
              v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(centroAntes) < 1.0);
    poco.lon = -82.20;                                        // ~40 km: fuera de la ventana
    v.recibirDatagrama(telemetria::formatRmc(poco) + "\r\n");
    verificar(QStringLiteral("uno que sale de la zona central recentra el mapa en él"),
              v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(QGeoCoordinate(23.20, -82.20)) < 200.0);
    v.alCambiarCentro(false);
    verificar(QStringLiteral("un cambio de centro sin ratón (zoom, programa) no desactiva el seguimiento"),
              v.casillaSeguir()->isChecked());
    v.alCambiarCentro(true);
    verificar(QStringLiteral("arrastrar el mapa con el ratón desactiva el seguimiento (y deja moverlo)"),
              !v.casillaSeguir()->isChecked());
    poco.lon = -82.10;
    v.recibirDatagrama(telemetria::formatRmc(poco) + "\r\n");
    verificar(QStringLiteral("con el seguimiento desactivado el mapa se queda donde la persona lo puso"),
              v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(QGeoCoordinate(23.20, -82.20)) < 200.0);
    v.casillaSeguir()->setChecked(true);
    verificar(QStringLiteral("volver a marcar la casilla recentra el mapa en el barco"),
              v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(QGeoCoordinate(23.20, -82.10)) < 200.0);
    // Se devuelve el barco a su sitio de antes para el resto de la prueba.
    poco.lon = -82.60;
    v.recibirDatagrama(telemetria::formatRmc(poco) + "\r\n");

    // --- El nombre llega ANTES que la posición, y un mensaje de dos frases en dos datagramas ---
    AisMessage estatico;
    estatico.type = 5;
    estatico.mmsi = 323000199;
    estatico.name = QStringLiteral("LLEGA ANTES");
    estatico.shipType = 60;
    const QList<QByteArray> frases = telemetria::encodeAis(estatico, 'A', 5);
    v.recibirDatagrama(frases.at(0) + "\r\n");
    verificar(QStringLiteral("con solo la primera de las dos frases aún no hay mensaje ni contacto"),
              !v.contactos().contains(323000199) && v.estadisticas().aisLost == 0);
    v.recibirDatagrama(frases.at(1) + "\r\n");
    verificar(QStringLiteral("con la segunda (otro datagrama) hay contacto con su nombre, pero sin posición ni objetivo en el mapa"),
              v.contactos().value(323000199).nombre == QStringLiteral("LLEGA ANTES") && v.contactos().value(323000199).id < 0);
    AisMessage pos;
    pos.type = 3;
    pos.mmsi = 323000199;
    pos.lat = 23.40;
    pos.lon = -82.70;
    pos.sogKn = 8.0;
    pos.cogDeg = 120.0;
    pos.navStatus = 0;
    v.recibirDatagrama(telemetria::encodeAis(pos).first() + "\r\n");
    verificar(QStringLiteral("al llegar su posición aparece en el mapa ya con el nombre y la clase A del tipo 3"),
              v.contactos().value(323000199).id >= 0 && !v.contactos().value(323000199).claseB
              && !objetivoConEtiqueta(v.mapa(), QStringLiteral("LLEGA ANTES")).label.isEmpty());

    // --- Un informe sin posición (181°/91°: «no disponible») no crea objetivo ---
    AisMessage sinPosicion;
    sinPosicion.type = 1;
    sinPosicion.mmsi = 999999999;
    v.recibirDatagrama(telemetria::encodeAis(sinPosicion).first() + "\r\n");
    v.tic();
    verificar(QStringLiteral("un informe con la posición «no disponible» da contacto sin objetivo, al final de la tabla con «—»"),
              v.contactos().contains(999999999) && v.contactos().value(999999999).id < 0
              && celda(v.tabla(), filaDe(v.tabla(), 999999999), 6) == QStringLiteral("—")
              && filaDe(v.tabla(), 999999999) == v.tabla()->rowCount() - 1);

    // --- El mensaje AIS publicado en la documentación de gpsd, tal cual ---
    v.recibirDatagrama("!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C\r\n");
    const Contacto publicado = v.contactos().value(477553000);
    verificar(QStringLiteral("el mensaje publicado (MMSI 477553000) llega a 47,582833 N 122,345833 O, rumbo 51°, proa 181°, amarrado"),
              publicado.id >= 0 && std::abs(publicado.posicion.latitude() - 47.582833) < 1e-5
              && std::abs(publicado.posicion.longitude() + 122.345833) < 1e-5 && publicado.cogDeg == 51.0
              && publicado.rumboDeg == 181 && publicado.navegacion == 5);

    // --- Ruido: se cuenta y se anota, y el flujo sigue ---
    const quint64 malas = v.estadisticas().badChecksum;
    const quint64 ignoradas = v.estadisticas().ignored;
    const quint64 malFormadas = v.estadisticas().malformed;
    v.recibirDatagrama("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*49\r\n");                       // la suma publicada es 48
    v.recibirDatagrama("$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\r\n");
    v.recibirDatagrama("$GPRMC,120000.00,A,2312.");                                              // cortada, sin suma
    verificar(QStringLiteral("una suma mala, una GSV (tipo ignorado) y una frase cortada se cuentan cada una en lo suyo"),
              v.estadisticas().badChecksum == malas + 1 && v.estadisticas().ignored == ignoradas + 1
              && v.estadisticas().malformed == malFormadas + 1);
    verificar(QStringLiteral("la suma mala y la frase cortada se anotan en el registro; la GSV, no (a menos que se pida todo)"),
              v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("suma de comprobacion incorrecta"))
              && v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("sin suma"))
              && !v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("GPGSV")));
    verificar(QStringLiteral("los contadores salen en el panel del enlace"),
              v.textoEstadisticas().contains(QStringLiteral("Suma mala: %1").arg(malas + 1)));
    const QVector<MapTarget> antesRuido = v.mapa()->targets();
    v.recibirDatagrama("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*49\r\n");
    verificar(QStringLiteral("una suma mala no mueve nada (el VTG corrupto no cambia el barco propio)"),
              v.propio().cogDeg == 90.0 && v.mapa()->targets().size() == antesRuido.size());

    // --- Contacto perdido, que reaparece y que se borra ---
    const int antesDeEnvejecer = int(v.contactos().size());
    v.fijarReloj(t0.addSecs(61));
    v.tic();
    verificar(QStringLiteral("a los 61 s sin datos todos los contactos pasan a Perdido, en la tabla, en el mapa y en el registro"),
              celda(v.tabla(), filaDe(v.tabla(), 323000101), 10) == QStringLiteral("Perdido")
              && v.mapa()->targetAttribute(v.contactos().value(323000101).id, QStringLiteral("estado")).toString() == QStringLiteral("Perdido")
              && v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("HABANA STAR: sin datos, contacto perdido")));
    verificar(QStringLiteral("el barco propio no es un contacto: no está en la tabla ni se pierde"),
              v.contactos().size() == antesDeEnvejecer && filaDe(v.tabla(), 323000100) < 0
              && v.mapa()->targetAttribute(v.propio().id, QStringLiteral("estado")).toString() == QStringLiteral("Activo"));
    v.fijarReloj(t0.addSecs(62));
    AisMessage vuelve;
    vuelve.type = 18;
    vuelve.mmsi = 323000103;
    vuelve.lat = 23.325;
    vuelve.lon = -82.50;
    vuelve.sogKn = 3.5;
    vuelve.cogDeg = 10.0;
    v.recibirDatagrama(telemetria::encodeAis(vuelve, 'B').first() + "\r\n");
    v.tic();
    verificar(QStringLiteral("el pesquero vuelve a emitir: Activo otra vez (y con su nombre de antes); el carguero sigue Perdido"),
              celda(v.tabla(), filaDe(v.tabla(), 323000103), 10) == QStringLiteral("Activo")
              && celda(v.tabla(), filaDe(v.tabla(), 323000103), 1) == QStringLiteral("PESCADOR 7")
              && celda(v.tabla(), filaDe(v.tabla(), 323000101), 10) == QStringLiteral("Perdido"));
    v.fijarReloj(t0.addSecs(330));
    v.tic();
    verificar(QStringLiteral("a los 330 s se borran los contactos sin datos (más de 300 s) y queda solo el pesquero, perdido otra vez"),
              v.contactos().size() == 1 && v.contactos().contains(323000103) && v.contactos().value(323000103).perdido
              && objetivosDe(v.mapa(), QStringLiteral("claseA")) == 0 && objetivosDe(v.mapa(), QStringLiteral("claseB")) == 1
              && v.tabla()->rowCount() == 1);

    // --- UDP de verdad: un datagrama a localhost llega por el socket ---
    const quint64 antesUdp = v.datagramas();
    QUdpSocket emisor;
    telemetria::Rmc r;
    r.valid = true;
    r.time = QTime(12, 5, 30);
    r.utc = telemetria::utcDateTime(t0.date(), r.time);
    r.lat = 23.25;
    r.lon = -82.55;
    r.sogKn = 11.0;
    r.cogDeg = 45.0;
    emisor.writeDatagram(telemetria::formatRmc(r) + "\r\n", QHostAddress::LocalHost, v.puertoLocal());
    const bool llego = esperar(3000, [&] { return std::abs(v.propio().posicion.latitude() - 23.25) < 1e-5; });
    verificar(QStringLiteral("un datagrama UDP mandado a localhost mueve el barco propio (se recibe por el socket, no por llamada)"),
              llego && v.datagramas() == antesUdp + 1 && v.propio().sogKn == 11.0 && v.propio().cogDeg == 45.0);

    // --- TCP: el cliente se conecta a un servidor, recibe un FLUJO (frases partidas, varias en un bloque) y se reconecta ---
    QTcpServer servidor;
    verificar(QStringLiteral("se abre un servidor TCP de prueba en localhost"),
              servidor.listen(QHostAddress(QHostAddress::LocalHost), 0));
    verificar(QStringLiteral("sin servidor o sin puerto no se intenta conectar"),
              !v.conectarTcp(QString(), 5) && !v.conectarTcp(QStringLiteral("127.0.0.1"), 0)
              && v.fuenteActiva() == Ventana::Fuente::Ninguna);
    verificar(QStringLiteral("conectarTcp empieza la conexión: la fuente es TCP, el selector y el botón lo reflejan"),
              v.conectarTcp(QStringLiteral("127.0.0.1"), servidor.serverPort()) && v.fuenteActiva() == Ventana::Fuente::Tcp
              && v.indiceFuente() == 1 && v.textoBotonEnlace() == QStringLiteral("Parar"));
    const bool aceptada = esperar(3000, [&] { return servidor.hasPendingConnections(); });
    QTcpSocket *conexion = aceptada ? servidor.nextPendingConnection() : nullptr;
    verificar(QStringLiteral("el servidor acepta la conexión y el cliente se da por conectado (y lo dice en el panel)"),
              aceptada && esperar(3000, [&] { return v.tcpConectado(); })
              && v.textoEstadoEnlace().contains(QStringLiteral("Conectado por TCP")));
    telemetria::Rmc rt;
    rt.valid = true;
    rt.time = QTime(12, 6, 0);
    rt.utc = telemetria::utcDateTime(t0.date(), rt.time);
    rt.lat = 23.30;
    rt.lon = -82.40;
    rt.sogKn = 7.0;
    rt.cogDeg = 30.0;
    const QByteArray fraseTcp = telemetria::formatRmc(rt) + "\r\n";
    if (conexion) {
        conexion->write(fraseTcp.left(20));
        conexion->flush();
    }
    esperar(300, [] { return false; });                      // deja que llegue (y se vea que no hace nada)
    verificar(QStringLiteral("la mitad de una frase no mueve nada: en un flujo la línea espera al resto"),
              std::abs(v.propio().posicion.latitude() - 23.30) > 1e-3);
    if (conexion) {
        conexion->write(fraseTcp.mid(20));
        conexion->flush();
    }
    verificar(QStringLiteral("al llegar el resto del flujo se completa la frase y mueve el barco propio"),
              esperar(3000, [&] { return std::abs(v.propio().posicion.latitude() - 23.30) < 1e-5; }) && v.propio().sogKn == 7.0);
    AisMessage porTcp;
    porTcp.type = 5;
    porTcp.mmsi = 323000177;
    porTcp.name = QStringLiteral("POR TCP");
    porTcp.shipType = 80;
    AisMessage posTcp;
    posTcp.type = 1;
    posTcp.mmsi = 323000177;
    posTcp.lat = 23.40;
    posTcp.lon = -82.30;
    posTcp.sogKn = 5.0;
    posTcp.cogDeg = 90.0;
    QByteArray bloque;
    for (const QByteArray &f : telemetria::encodeAis(porTcp, 'A', 4))
        bloque += f + "\r\n";
    bloque += telemetria::encodeAis(posTcp).first() + "\r\n";
    if (conexion) {
        conexion->write(bloque);
        conexion->flush();
    }
    verificar(QStringLiteral("varias frases en un solo bloque (un tipo 5 de dos frases y un tipo 1) dan un contacto con nombre y posición"),
              esperar(3000, [&] { return v.contactos().value(323000177).id >= 0; })
              && v.contactos().value(323000177).nombre == QStringLiteral("POR TCP") && v.contactos().value(323000177).tipoBarco == 80);
    if (conexion)
        conexion->disconnectFromHost();
    verificar(QStringLiteral("si el servidor corta, el cliente lo nota, lo anota y programa el reintento"),
              esperar(3000, [&] { return !v.tcpConectado() && v.intentosReconexion() >= 1; })
              && v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("sin enlace"))
              && v.textoEstadoEnlace().contains(QStringLiteral("reintentando")));
    const bool vuelve2 = esperar(8000, [&] { return servidor.hasPendingConnections(); });
    QTcpSocket *conexion2 = vuelve2 ? servidor.nextPendingConnection() : nullptr;
    verificar(QStringLiteral("a los ~3 s se reconecta solo: el servidor ve una conexión nueva y el cliente lo da por conectado"),
              vuelve2 && esperar(3000, [&] { return v.tcpConectado(); }) && v.intentosReconexion() == 0);
    rt.lat = 23.31;
    rt.time = QTime(12, 7, 0);
    if (conexion2) {
        conexion2->write(telemetria::formatRmc(rt) + "\r\n");
        conexion2->flush();
    }
    verificar(QStringLiteral("y por la conexión nueva sigue llegando todo"),
              esperar(3000, [&] { return std::abs(v.propio().posicion.latitude() - 23.31) < 1e-5; }));
    v.parar();
    verificar(QStringLiteral("parar corta la conexión y NO reconecta (ni el servidor ve otra tras 3,5 s)"),
              v.fuenteActiva() == Ventana::Fuente::Ninguna && !v.tcpConectado() && !esperar(3500, [&] { return servidor.hasPendingConnections(); })
              && v.textoBotonEnlace() == QStringLiteral("Conectar"));
    // Un servidor que no existe: lo intenta, lo dice una vez y sigue reintentando hasta que se pare.
    servidor.close();
    verificar(QStringLiteral("con el servidor caído el cliente lo anota y reintenta (sin llenar el registro)"),
              v.conectarTcp(QStringLiteral("127.0.0.1"), 9) && esperar(3000, [&] { return v.intentosReconexion() >= 1; })
              && v.textoEstadoEnlace().contains(QStringLiteral("127.0.0.1:9")));
    v.parar();

#ifdef TELEMETRIA_CON_SERIE
#  ifdef Q_OS_UNIX
    // --- Puerto serie: un pseudoterminal hace de cable (un extremo lo escribe el programa de prueba; el otro lo abre el cliente) ---
    const int maestro = ::posix_openpt(O_RDWR | O_NOCTTY);
    const bool ptyOk = maestro >= 0 && ::grantpt(maestro) == 0 && ::unlockpt(maestro) == 0;
    const QString esclavo = ptyOk ? QString::fromLocal8Bit(::ptsname(maestro)) : QString();
    verificar(QStringLiteral("se crea un pseudoterminal que hace de puerto serie (%1)").arg(esclavo), ptyOk && !esclavo.isEmpty());
    if (ptyOk) {
        verificar(QStringLiteral("abrirSerie abre el puerto a 38400 baudios y lo dice en el panel"),
                  v.abrirSerie(esclavo, 38400) && v.fuenteActiva() == Ventana::Fuente::Serie && v.indiceFuente() == 2
                  && v.textoEstadoEnlace().contains(QStringLiteral("38400")));
        rt.lat = 23.32;
        rt.time = QTime(12, 8, 0);
        const QByteArray fraseSerie = telemetria::formatRmc(rt) + "\r\n";
        const ssize_t mitad = ::write(maestro, fraseSerie.constData(), size_t(fraseSerie.size() / 2));
        esperar(300, [] { return false; });
        verificar(QStringLiteral("por el puerto serie también: media frase espera, y el resto la completa y mueve el barco"),
                  mitad > 0 && std::abs(v.propio().posicion.latitude() - 23.32) > 1e-3
                  && ::write(maestro, fraseSerie.constData() + fraseSerie.size() / 2, size_t(fraseSerie.size() - fraseSerie.size() / 2)) > 0
                  && esperar(3000, [&] { return std::abs(v.propio().posicion.latitude() - 23.32) < 1e-5; }));
        v.parar();
        verificar(QStringLiteral("parar cierra el puerto serie"), v.fuenteActiva() == Ventana::Fuente::Ninguna
                  && v.textoBotonEnlace() == QStringLiteral("Abrir"));
        ::close(maestro);
    }
    verificar(QStringLiteral("un puerto serie que no existe no se abre, se anota y se reintenta"),
              !v.abrirSerie(QStringLiteral("/dev/no_existe_telemetria"), 4800)
              && v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("no_existe_telemetria"))
              && v.intentosReconexion() >= 1);
    v.parar();
#  endif
    verificar(QStringLiteral("sin nombre de puerto no se abre"), !v.abrirSerie(QString(), 4800));
#else
    verificar(QStringLiteral("sin Qt Serial Port, abrirSerie dice que no está en esta compilación y no abre nada"),
              !v.abrirSerie(QStringLiteral("COM3"), 4800) && !Ventana::tieneSerie()
              && v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("Qt Serial Port")));
#endif

    // --- Una captura en fichero, reproducida como si llegara en vivo ---
    QTemporaryDir dir;
    QFile captura(dir.filePath(QStringLiteral("captura.nmea")));
    const bool escrita = dir.isValid() && captura.open(QIODevice::WriteOnly);
    if (escrita) {
        for (const QByteArray &l : escenarioDemostracion(t0.addSecs(330)))
            captura.write(l + "\r\n");
        captura.close();
    }
    v.fijarReloj(t0.addSecs(330));
    verificar(QStringLiteral("se escribe una captura de ejemplo"), escrita);
    verificar(QStringLiteral("se reproduce: el carguero y el petrolero se suman al pesquero y al de TCP (4 contactos) y el pesquero vuelve a estar Activo"),
              v.reproducir(captura.fileName()) && esperar(5000, [&] { return !v.reproduciendo(); }) && v.contactos().size() == 4
              && !v.contactos().value(323000103).perdido && v.contactos().value(323000101).nombre == QStringLiteral("HABANA STAR"));
    verificar(QStringLiteral("la captura se anota en el registro"),
              v.registro().join(QLatin1Char('\n')).contains(QStringLiteral("captura.nmea")));
    verificar(QStringLiteral("un fichero que no existe no se reproduce"), !v.reproducir(dir.filePath(QStringLiteral("no_existe.nmea"))));

    // --- Parar la escucha ---
    v.parar();
    verificar(QStringLiteral("parar deja de escuchar"), v.puertoLocal() == 0);

    // --- Glosario ---
    QStringList faltan;
    const auto terminos = glosario();
    for (const QString &t : terminosEnPantalla()) {
        bool definido = false;
        for (const Termino &g : terminos)
            definido = definido || (g.nombre == t && !g.definicion.isEmpty());
        if (!definido)
            faltan << t;
    }
    verificar(QStringLiteral("glosario define todo lo que la interfaz muestra (%1 términos)%2")
                  .arg(terminos.size())
                  .arg(faltan.isEmpty() ? QString() : QStringLiteral(", faltan: ") + faltan.join(QLatin1String(", "))),
              faltan.isEmpty() && v.listaGlosario()->count() == terminos.size());

    out << (todoOk ? "TODO BIEN\n" : "HAY FALLOS\n");
    return todoOk ? 0 : 1;
}

// Arranca la ventana (o, con --comprobar, la prueba sin ventana; con --captura=, la deja con el escenario
// de demostración y guarda una imagen).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cliente_telemetria"));

    QStringList posicionales;
    int puerto = kPuertoPorOmision;
    QString reproducirFichero, capturaDestino;
    bool puertoExplicito = false;
    QString tcpArg, serieArg;
    int espera = 3;
    for (const QString &a : app.arguments().mid(1)) {
        if (a.startsWith(QLatin1String("--puerto="))) {
            puerto = a.mid(int(strlen("--puerto="))).toInt();
            puertoExplicito = true;
        } else if (a.startsWith(QLatin1String("--tcp="))) {
            tcpArg = a.mid(int(strlen("--tcp=")));
        } else if (a.startsWith(QLatin1String("--serie="))) {
            serieArg = a.mid(int(strlen("--serie=")));
        } else if (a.startsWith(QLatin1String("--espera="))) {
            espera = std::max(1, a.mid(int(strlen("--espera="))).toInt());
        } else if (a.startsWith(QLatin1String("--reproducir="))) {
            reproducirFichero = a.mid(int(strlen("--reproducir=")));
        } else if (a.startsWith(QLatin1String("--captura="))) {
            capturaDestino = a.mid(int(strlen("--captura=")));
        } else if (!a.startsWith(QLatin1String("--"))) {
            posicionales << a;
        }
    }
    const QString datos = posicionales.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/datos")
        : posicionales.first();
    const bool probando = app.arguments().contains(QStringLiteral("--comprobar"));

    // Para las pruebas no se escucha de entrada (la prueba abre su propio puerto libre), ni para una captura
    // con el escenario de demostración. Una captura con --puerto sí escucha, para fotografiar un enlace real.
    const bool otraFuente = !tcpArg.isEmpty() || !serieArg.isEmpty();
    const bool demostracion = !capturaDestino.isEmpty() && !puertoExplicito && !otraFuente && reproducirFichero.isEmpty();
    Ventana ventana(datos, probando || demostracion || otraFuente ? -1 : puerto);
    if (probando)
        return comprobar(ventana);
    ventana.show();
    if (!tcpArg.isEmpty()) {
        // host:puerto; el puerto es lo que sigue al último «:» (así vale también un nombre con ":" como una IPv6 entre corchetes).
        const int dosPuntos = int(tcpArg.lastIndexOf(QLatin1Char(':')));
        const QString host = dosPuntos > 0 ? tcpArg.left(dosPuntos) : tcpArg;
        const int puertoTcp = dosPuntos > 0 ? tcpArg.mid(dosPuntos + 1).toInt() : kPuertoPorOmision;
        ventana.conectarTcp(host, quint16(puertoTcp));
    }
    if (!serieArg.isEmpty()) {
        const QStringList partes = serieArg.split(QLatin1Char(','));
        ventana.abrirSerie(partes.value(0), partes.size() > 1 ? partes.at(1).toInt() : kBaudiosPorOmision);
    }
    if (!reproducirFichero.isEmpty())
        ventana.reproducir(reproducirFichero);
    if (!capturaDestino.isEmpty()) {
        if (demostracion)
            ventana.cargarDemostracion();
        QTimer::singleShot(espera * 1000, &app, [&ventana, &app, capturaDestino] {
            ventana.grab().save(capturaDestino);
            app.quit();
        });
    }
    return app.exec();
}
