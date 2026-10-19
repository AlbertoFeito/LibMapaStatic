// monitor_trafico - monitor de tráfico (buques y aeronaves) con libmapa.
//
// Séptimo ejemplo de la serie. Es el esqueleto de cualquier app que sigue
// contactos en tiempo real: un decodificador de AIS, de radar o de ADS-B que
// alimenta el mapa, y una persona que quiere saber QUÉ hay, QUÉ se acerca y QUÉ
// se ha dejado de oír. Se diferencia de app_seguimiento (que enseña el API de
// objetivos con tres buques) en lo que añade una app de verdad:
//   - Una TABLA de contactos viva, ordenada por distancia, con rumbo, velocidad,
//     CPA y TCPA respecto al propio, y su estado (Activo, Alerta, Perdido).
//   - Selección enlazada: elegir una fila selecciona el contacto en el mapa y
//     dibuja la marcación hasta él; pinchar un contacto en el mapa elige su fila.
//   - Contactos PERDIDOS: sin datos durante un tiempo se atenúan; pasado otro
//     tiempo se borran. Es lo que hay que hacer con cualquier feed real.
//   - Alertas de proximidad y de zona en un registro con hora.
//   - Filtro por tipo, agrupación al alejar y un GLOSARIO en la propia interfaz
//     con todos los términos que usa la ventana (también como ayuda al pasar el
//     ratón por las cabeceras de la tabla).
//
//   monitor_trafico [paquete de datos]   (por defecto: <exe>/datos)
//   monitor_trafico <paquete> --comprobar
//        -> sin ventana: hace avanzar un reloj simulado y comprueba la tabla, el
//           CPA de un cruce frontal, la alerta, el contacto perdido y su borrado,
//           el filtro, la selección y que el glosario define todo lo que se usa.
//   monitor_trafico <paquete> --captura=imagen.png
//        -> abre la ventana, la deja correr unos pasos, guarda una imagen y sale.
//
// El «feed» de aquí es un simulador (la clase Simulador). En un producto real se
// sustituye por el decodificador: lo único que hace falta es llamar a
// Ventana::recibir con cada parte (nombre, tipo, posición, rumbo, velocidad, hora).

#include <libmapa/MapWidget.h>
#include <libmapa/Proximity.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableWidget>
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
// Cada segundo real avanza esto de tiempo simulado, para que el cruce de dos
// buques se vea en medio minuto y no en media hora.
const double kEscala = 30.0;
// Segundos (simulados) sin datos para dar un contacto por perdido, y para borrarlo.
const double kPerdidoSeg = 90.0;
const double kBorrarSeg = 300.0;
// Nombre del contacto propio: desde él se miden las distancias y los CPA.
const QString kPropio = QStringLiteral("Propio");

// Un parte recibido del feed: lo que da un decodificador de AIS/radar/ADS-B.
struct Parte {
    QString nombre;
    QString tipo;                 // "propio", "buque", "pesquero", "aeronave"
    QGeoCoordinate posicion;
    double rumbo = 0.0;           // grados verdaderos (COG)
    double nudos = 0.0;           // velocidad sobre el fondo (SOG)
    QDateTime hora;               // hora del dato, UTC
};

// Lo que la ventana recuerda de cada contacto, además de lo que guarda el mapa.
struct Contacto {
    qint64 id = -1;
    QString nombre;
    QString tipo;
    double rumbo = 0.0;
    double nudos = 0.0;
    QDateTime ultima;             // hora del último parte
    QString estado;               // "Activo", "Alerta" o "Perdido"
};

// Una entrada del glosario: el término y su definición.
struct Termino {
    QString nombre;
    QString definicion;
};

// Una columna de la tabla: su texto y la entrada del glosario que la explica.
struct Columna {
    QString texto;
    QString clave;
};

} // namespace

// Cabeceras de la tabla de contactos y el término del glosario de cada una.
static QVector<Columna> columnas()
{
    return {
        { QObject::tr("Contacto"), QStringLiteral("Contacto") },
        { QObject::tr("Tipo"), QStringLiteral("Tipo") },
        { QObject::tr("Rumbo"), QStringLiteral("Rumbo (COG)") },
        { QObject::tr("Vel. (kn)"), QStringLiteral("Velocidad (SOG)") },
        { QObject::tr("Dist. (NM)"), QStringLiteral("Distancia") },
        { QObject::tr("CPA (NM)"), QStringLiteral("CPA") },
        { QObject::tr("TCPA (min)"), QStringLiteral("TCPA") },
        { QObject::tr("Estado"), QStringLiteral("Estado") },
    };
}

// El glosario de la interfaz: todos los términos que usa la ventana (cabeceras,
// opciones, avisos). Una prueba (--comprobar) verifica que cada columna y cada
// término de uso tienen aquí su definición, para que el glosario no se quede atrás.
static QVector<Termino> glosario()
{
    return {
        { QObject::tr("Contacto"),
          QObject::tr("Cualquier cosa que el sistema sigue: un buque, una aeronave... Llega como una "
                      "posición con su hora y en el mapa se dibuja como un objetivo, con su estela.") },
        { QObject::tr("Propio"),
          QObject::tr("El contacto desde el que se mide todo: las distancias, el CPA y la marcación. "
                      "Aquí es el buque propio, y lleva los anillos de distancia.") },
        { QObject::tr("Tipo"),
          QObject::tr("Clase del contacto: buque, pesquero o aeronave. La librería no la interpreta; "
                      "la aplicación la usa para filtrar y para elegir el símbolo.") },
        { QObject::tr("Rumbo (COG)"),
          QObject::tr("Dirección en la que se mueve, en grados verdaderos (0° = norte, sentido "
                      "horario). En AIS es el COG, «course over ground». No es hacia dónde apunta la "
                      "proa (heading).") },
        { QObject::tr("Velocidad (SOG)"),
          QObject::tr("Rapidez sobre el fondo, en nudos. En AIS es el SOG, «speed over ground».") },
        { QObject::tr("Distancia"),
          QObject::tr("Distancia ortodrómica (la más corta sobre la Tierra) del propio al contacto, "
                      "ahora, en millas náuticas.") },
        { QObject::tr("CPA"),
          QObject::tr("«Closest Point of Approach»: la menor distancia a la que pasarán el propio y el "
                      "contacto si los dos mantienen rumbo y velocidad. Un CPA pequeño es un riesgo "
                      "de colisión.") },
        { QObject::tr("TCPA"),
          QObject::tr("«Time to CPA»: los minutos que faltan para el CPA. Si es negativo, el momento "
                      "de máximo acercamiento ya pasó y se están alejando.") },
        { QObject::tr("Estado"),
          QObject::tr("Activo: llegan datos. Alerta: forma parte de una alerta de proximidad. "
                      "Perdido: lleva más de 90 s sin datos.") },
        { QObject::tr("Contacto perdido"),
          QObject::tr("Un contacto del que dejaron de llegar partes. Se atenúa en el mapa y se borra "
                      "tras 5 min sin datos. Su CPA no se calcula: ya no se sabe por dónde va.") },
        { QObject::tr("Alerta de proximidad"),
          QObject::tr("Aviso cuando dos contactos pasarán a menos de 0,5 NM uno del otro en los "
                      "próximos 15 min (CPA y TCPA). Se apunta en el registro con su hora.") },
        { QObject::tr("Zona vigilada"),
          QObject::tr("Polígono del mapa que avisa cuando un contacto entra o sale de él.") },
        { QObject::tr("Marcación"),
          QObject::tr("Línea del propio al contacto seleccionado, con el rumbo verdadero y la "
                      "distancia (en inglés, EBL: «electronic bearing line»).") },
        { QObject::tr("Anillos de distancia"),
          QObject::tr("Circunferencias alrededor del propio, una por milla náutica, para estimar "
                      "distancias de un vistazo (en inglés, VRM: «variable range marker»).") },
        { QObject::tr("Estela"),
          QObject::tr("La línea que dejan las últimas posiciones de un contacto: enseña por dónde "
                      "ha venido.") },
        { QObject::tr("Atenuar"),
          QObject::tr("Dibujar tenue, sin rótulo ni estela, lo que no cumple el filtro (o está "
                      "perdido), en vez de ocultarlo.") },
        { QObject::tr("Agrupar"),
          QObject::tr("Al alejar el mapa, los contactos cercanos se funden en un círculo con su "
                      "número, para que no se amontonen los iconos.") },
        { QObject::tr("NM"),
          QObject::tr("Milla náutica: 1852 metros. Una milla náutica por hora es un nudo.") },
        { QObject::tr("kn"),
          QObject::tr("Nudo: una milla náutica por hora (unos 0,514 m/s).") },
        { QObject::tr("Tiempo simulado"),
          QObject::tr("Esta demo hace pasar 30 s por cada segundo real, para que un cruce se vea "
                      "en medio minuto. Un producto real usa la hora de cada parte.") },
    };
}

// Simulador del feed: unos contactos con rumbo y velocidad fijos que avanzan en
// tiempo simulado. Uno de ellos (Pesquero 2) se queda sin señal a los 150 s, para
// ver cómo se pierde y se borra. En un producto real, esto es el decodificador.
class Simulador
{
public:
    // Contactos de partida alrededor de (23.20, -82.40), el propio hacia el norte.
    Simulador()
    {
        const QGeoCoordinate c(23.20, -82.40);
        const auto alta = [this, &c](const QString &n, const QString &t, double distNM, double az,
                                     double rumbo, double nudos, double silencio = -1.0) {
            Sim s;
            s.parte.nombre = n;
            s.parte.tipo = t;
            s.parte.posicion = c.atDistanceAndAzimuth(distNM * kNM, az);
            s.parte.rumbo = rumbo;
            s.parte.nudos = nudos;
            s.silencioDesde = silencio;
            m_contactos.append(s);
        };
        alta(kPropio, QStringLiteral("propio"), 0.0, 0.0, 0.0, 10.0);
        // Frontal: 5 NM al norte, rumbo sur. Cruce de proa con el propio: CPA ~ 0.
        alta(QStringLiteral("Frontal"), QStringLiteral("buque"), 5.0, 0.0, 180.0, 10.0);
        alta(QStringLiteral("Carguero A"), QStringLiteral("buque"), 8.0, 70.0, 250.0, 12.0);
        alta(QStringLiteral("Carguero B"), QStringLiteral("buque"), 10.0, 200.0, 20.0, 9.0);
        alta(QStringLiteral("Pesquero 1"), QStringLiteral("pesquero"), 4.0, 300.0, 90.0, 5.0);
        alta(QStringLiteral("Pesquero 2"), QStringLiteral("pesquero"), 6.0, 120.0, 45.0, 4.0, 150.0);
        alta(QStringLiteral("Helicóptero"), QStringLiteral("aeronave"), 12.0, 20.0, 200.0, 60.0);
        alta(QStringLiteral("Remolcador"), QStringLiteral("buque"), 3.0, 250.0, 100.0, 6.0);
    }

    // Avanza \a segundos de tiempo simulado y entrega un parte por contacto que
    // siga con señal, con la hora \a hora. Los que se quedan sin señal siguen
    // moviéndose pero ya no informan.
    void avanzar(double segundos, const QDateTime &hora, const std::function<void(const Parte &)> &entregar)
    {
        m_tiempo += segundos;
        for (Sim &s : m_contactos) {
            s.parte.posicion = s.parte.posicion.atDistanceAndAzimuth(
                knotsToMetersPerSecond(s.parte.nudos) * segundos, s.parte.rumbo);
            s.parte.hora = hora;
            if (s.silencioDesde >= 0.0 && m_tiempo >= s.silencioDesde)
                continue;
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
        double silencioDesde = -1.0;   // segundos simulados a partir de los que no informa
    };
    QVector<Sim> m_contactos;
    double m_tiempo = 0.0;
};

// Ventana: el mapa al centro, la tabla de contactos abajo, el glosario a la
// derecha y los filtros y el registro de alertas a la izquierda.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos, los paneles, las alertas y la zona,
    // y arranca el reloj de la simulación.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        resize(1380, 860);
        setWindowTitle(tr("Monitor de tráfico"));
        const QGeoCoordinate centro(23.20, -82.40);
        m_mapa->setCenter(centro);
        m_mapa->setZoom(11);
        m_mapa->setScaleBarVisible(true);
        m_mapa->setScaleBarUnits(ScaleUnits::Nautical);

        crearPaneles();
        crearAvisos(centro);

        // Reloj simulado: arranca ahora y avanza kEscala segundos por segundo real.
        m_hora = QDateTime::currentDateTimeUtc();
        for (const Parte &p : m_sim.iniciales(m_hora))
            recibir(p);
        refrescar();
        connect(&m_reloj, &QTimer::timeout, this, [this] { paso(kEscala); });
        m_reloj.start(1000);

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
    }

    MapWidget *mapa() const { return m_mapa; }
    // Para las pruebas: quita el reloj real y avanza a mano con paso().
    void detener() { m_reloj.stop(); }

    // Anota un parte del feed: da de alta el contacto si es nuevo o actualiza su
    // posición, y le pone la velocidad y el rumbo que da el feed (mandan sobre la
    // estima por la traza en las alertas de proximidad).
    void recibir(const Parte &p)
    {
        Contacto &c = m_contactos[p.nombre];
        if (c.id < 0) {
            MapTarget t;
            t.label = p.nombre;
            t.kind = p.tipo;
            t.position = p.posicion;
            t.headingDeg = p.rumbo;
            t.color = colorDe(p.tipo);
            t.attributes.insert(QStringLiteral("estado"), QStringLiteral("Activo"));
            c.id = m_mapa->addTarget(t, p.hora);
            c.nombre = p.nombre;
            m_porId.insert(c.id, p.nombre);
            if (p.nombre == kPropio)
                crearAnillos(c.id);
        } else {
            m_mapa->updateTarget(c.id, p.posicion, p.rumbo, p.hora);
        }
        m_mapa->setTargetVelocity(c.id, knotsToMetersPerSecond(p.nudos), p.rumbo);
        c.tipo = p.tipo;
        c.rumbo = p.rumbo;
        c.nudos = p.nudos;
        c.ultima = p.hora;
    }

    // Un paso del reloj: avanza \a segundos de tiempo simulado, recibe los partes,
    // revisa los contactos perdidos y refresca la tabla.
    void paso(double segundos)
    {
        m_hora = m_hora.addMSecs(qint64(segundos * 1000.0));
        m_sim.avanzar(segundos, m_hora, [this](const Parte &p) { recibir(p); });
        revisar();
        refrescar();
    }

    // Marca como perdidos los contactos sin datos desde hace kPerdidoSeg y borra los
    // que llevan kBorrarSeg. El propio nunca se pierde. Reaplica el filtro si algo cambió.
    void revisar()
    {
        bool cambio = false;
        QStringList aBorrar;
        for (auto it = m_contactos.begin(); it != m_contactos.end(); ++it) {
            Contacto &c = it.value();
            if (c.nombre == kPropio)
                continue;
            const double edad = c.ultima.secsTo(m_hora);
            if (edad > kBorrarSeg) {
                aBorrar << c.nombre;
            } else if (edad > kPerdidoSeg && c.estado != QLatin1String("Perdido")) {
                c.estado = QStringLiteral("Perdido");
                m_mapa->setTargetAttribute(c.id, QStringLiteral("estado"), QStringLiteral("Perdido"));
                registrar(tr("%1: sin datos, contacto perdido").arg(c.nombre));
                cambio = true;
            }
        }
        for (const QString &nombre : aBorrar) {
            const Contacto c = m_contactos.take(nombre);
            m_porId.remove(c.id);
            if (m_seleccion == nombre)
                seleccionar(QString());
            m_mapa->removeTarget(c.id);
            registrar(tr("%1: borrado tras %2 min sin datos").arg(nombre).arg(qRound(kBorrarSeg / 60.0)));
            cambio = true;
        }
        if (cambio)
            aplicarFiltro();
    }

    // Reconstruye la tabla (ordenada por distancia al propio) con el estado actual,
    // conservando la fila elegida, y actualiza el contador de la barra de estado.
    void refrescar()
    {
        QSet<qint64> enAlerta;
        for (const ProximityAlert &a : m_mapa->proximityAlerts())
            enAlerta << a.targetA << a.targetB;

        struct Fila {
            Contacto c;
            double dist = 0.0, cpa = 0.0, tcpa = 0.0;
            bool hayCpa = false;
        };
        QVector<Fila> filas;
        const qint64 propio = m_contactos.value(kPropio).id;
        int activos = 0, perdidos = 0, alertas = 0;
        for (Contacto &c : m_contactos) {
            if (c.estado != QLatin1String("Perdido"))
                c.estado = enAlerta.contains(c.id) && c.nombre != kPropio ? QStringLiteral("Alerta")
                                                                           : QStringLiteral("Activo");
            Fila f;
            f.c = c;
            if (propio >= 0 && c.id != propio) {
                const CpaResult r = m_mapa->targetCpa(propio, c.id);
                f.dist = r.valid ? r.currentDistanceMeters / kNM : 0.0;
                f.hayCpa = r.valid && c.estado != QLatin1String("Perdido");
                f.cpa = r.distanceMeters / kNM;
                f.tcpa = r.timeSeconds / 60.0;
            }
            filas.append(f);
            if (c.nombre == kPropio)
                continue;
            if (c.estado == QLatin1String("Perdido"))
                ++perdidos;
            else
                ++activos;
            if (c.estado == QLatin1String("Alerta"))
                ++alertas;
        }
        std::sort(filas.begin(), filas.end(), [](const Fila &a, const Fila &b) {
            if ((a.c.nombre == kPropio) != (b.c.nombre == kPropio))
                return a.c.nombre == kPropio;     // el propio, siempre arriba
            return a.dist < b.dist;
        });

        const QSignalBlocker bloqueo(m_tabla);
        m_tabla->setRowCount(filas.size());
        int filaElegida = -1;
        for (int i = 0; i < filas.size(); ++i) {
            const Fila &f = filas.at(i);
            const bool propioFila = f.c.nombre == kPropio;
            const bool perdido = f.c.estado == QLatin1String("Perdido");
            const QStringList celdas = {
                f.c.nombre,
                f.c.tipo,
                QStringLiteral("%1°").arg(f.c.rumbo, 3, 'f', 0, QLatin1Char('0')),
                QString::number(f.c.nudos, 'f', 1),
                propioFila ? QStringLiteral("—") : QString::number(f.dist, 'f', 2),
                f.hayCpa ? QString::number(f.cpa, 'f', 2) : QStringLiteral("—"),
                f.hayCpa ? QString::number(f.tcpa, 'f', 1) : QStringLiteral("—"),
                f.c.estado,
            };
            for (int j = 0; j < celdas.size(); ++j) {
                auto *item = new QTableWidgetItem(celdas.at(j));
                if (j >= 2 && j <= 6)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (perdido)
                    item->setForeground(Qt::gray);
                else if (f.c.estado == QLatin1String("Alerta"))
                    item->setBackground(QColor(0xff, 0xcd, 0xd2));
                m_tabla->setItem(i, j, item);
            }
            if (f.c.nombre == m_seleccion)
                filaElegida = i;
        }
        if (filaElegida >= 0)
            m_tabla->selectRow(filaElegida);
        else
            m_tabla->clearSelection();
        m_contador->setText(tr("Contactos: %1 activos · %2 perdidos · %3 en alerta")
                                .arg(activos).arg(perdidos).arg(alertas));
    }

    // Elige el contacto \a nombre (vacío = ninguno): lo selecciona en el mapa y
    // dibuja la marcación del propio hasta él, quitando la anterior.
    void seleccionar(const QString &nombre)
    {
        m_seleccion = nombre;
        if (m_marcacion >= 0) {
            m_mapa->removeRangeMark(m_marcacion);
            m_marcacion = -1;
        }
        const Contacto c = m_contactos.value(nombre);
        if (c.id < 0) {
            m_mapa->setSelectedTarget(-1);
            return;
        }
        m_mapa->setSelectedTarget(c.id);
        const Contacto propio = m_contactos.value(kPropio);
        if (nombre != kPropio && propio.id >= 0 && c.estado != QLatin1String("Perdido")) {
            BearingLine linea;
            linea.fromTarget = propio.id;
            linea.toTarget = c.id;
            linea.color = QColor(0x15, 0x65, 0xc0);
            m_marcacion = m_mapa->addBearingLine(linea);
        }
    }

    // Aplica el filtro de la interfaz: tipo elegido y, siempre, atenuar los
    // perdidos (el propio pasa siempre). Se llama al cambiar la elección o el estado.
    void aplicarFiltro()
    {
        TargetFilter f;
        const QString tipo = m_tipo->currentData().toString();
        if (!tipo.isEmpty())
            f.kinds << tipo << QStringLiteral("propio");
        f.predicate = [](const MapTarget &t) {
            return t.attributes.value(QStringLiteral("estado")).toString() != QLatin1String("Perdido");
        };
        f.dim = true;
        m_mapa->setTargetFilter(f);
    }

    // Apunta una línea en el registro de avisos, con la hora simulada.
    void registrar(const QString &texto)
    {
        m_registro->insertItem(0, QStringLiteral("%1  %2").arg(m_hora.toString(QStringLiteral("HH:mm:ss")), texto));
        while (m_registro->count() > 200)
            delete m_registro->takeItem(m_registro->count() - 1);
        ++m_avisos;
    }

    // Nombre del contacto \a id, leído del propio objetivo del mapa. No sirve m_porId:
    // las señales de zona y de proximidad llegan DENTRO de addTarget/updateTarget,
    // antes de que recibir() haya apuntado el id de un contacto recién creado.
    QString nombreDe(qint64 id) const
    {
        const auto t = m_mapa->target(id);
        return t ? t->label : m_porId.value(id);
    }

    // Datos para las pruebas.
    QStringList avisos() const
    {
        QStringList l;
        for (int i = 0; i < m_registro->count(); ++i)
            l << m_registro->item(i)->text();
        return l;
    }
    int numContactos() const { return m_contactos.size(); }
    int numAvisos() const { return m_avisos; }
    QTableWidget *tabla() const { return m_tabla; }
    QComboBox *tipoCombo() const { return m_tipo; }
    QString seleccion() const { return m_seleccion; }
    const QMap<QString, Contacto> &contactos() const { return m_contactos; }
    QListWidget *listaGlosario() const { return m_listaGlosario; }

private:
    // Color del símbolo y la estela de un contacto según su tipo.
    static QColor colorDe(const QString &tipo)
    {
        if (tipo == QLatin1String("propio"))
            return QColor(0x2e, 0x7d, 0x32);
        if (tipo == QLatin1String("aeronave"))
            return QColor(0x6a, 0x1b, 0x9a);
        if (tipo == QLatin1String("pesquero"))
            return QColor(0x00, 0x83, 0x8f);
        return QColor(0xff, 0x8f, 0x00);
    }

    // Anillos de distancia (uno por milla náutica) alrededor del propio.
    void crearAnillos(qint64 propio)
    {
        RangeRings anillos;
        anillos.targetId = propio;
        anillos.spacingMeters = kNM;
        anillos.count = 6;
        anillos.spokeEveryDeg = 30;
        m_mapa->addRangeRings(anillos);
    }

    // Alertas: de proximidad (0,5 NM en 15 min) y de zona (un polígono vigilado al
    // norte), con su aviso en el registro. Y agrupar al alejar, si se marca.
    void crearAvisos(const QGeoCoordinate &centro)
    {
        m_mapa->setProximityAlert(0.5 * kNM, 15 * 60.0);
        connect(m_mapa, &MapWidget::proximityAlertRaised, this,
                [this](qint64 a, qint64 b, double cpa, double tcpa) {
            registrar(tr("⚠ Proximidad: %1 y %2, CPA %3 NM en %4 min")
                .arg(nombreDe(a), nombreDe(b)).arg(cpa / kNM, 0, 'f', 2)
                .arg(qRound(tcpa / 60.0)));
        });
        connect(m_mapa, &MapWidget::proximityAlertCleared, this, [this](qint64 a, qint64 b) {
            registrar(tr("Fin de alerta: %1 y %2").arg(nombreDe(a), nombreDe(b)));
        });

        MapFeature zona;
        zona.kind = GeometryKind::Polygon;
        zona.name = tr("Zona restringida");
        for (double az : { 0.0, 90.0, 180.0, 270.0 })
            zona.geometry.append(centro.atDistanceAndAzimuth(2.0 * kNM, az + 45.0)
                                     .atDistanceAndAzimuth(8.0 * kNM, 70.0));
        m_mapa->watchZone(m_mapa->addFeature(zona));
        connect(m_mapa, &MapWidget::targetEnteredZone, this, [this](qint64 t, qint64) {
            registrar(tr("Zona restringida: entra %1").arg(nombreDe(t)));
        });
        connect(m_mapa, &MapWidget::targetLeftZone, this, [this](qint64 t, qint64) {
            registrar(tr("Zona restringida: sale %1").arg(nombreDe(t)));
        });

        TargetClustering grupos;
        grupos.maxZoom = 9;
        m_grupos = grupos;
    }

    // Paneles: tabla de contactos (abajo), filtros y registro (izquierda) y
    // glosario (derecha, también con F1 desde el menú Ayuda).
    void crearPaneles()
    {
        // --- Tabla ---
        m_tabla = new QTableWidget(0, columnas().size(), this);
        const auto terminos = glosario();
        QStringList cabeceras;
        for (const Columna &c : columnas())
            cabeceras << c.texto;
        m_tabla->setHorizontalHeaderLabels(cabeceras);
        for (int i = 0; i < columnas().size(); ++i) {
            // Al pasar el ratón por la cabecera, la definición del glosario.
            for (const Termino &t : terminos)
                if (t.nombre == columnas().at(i).clave)
                    m_tabla->horizontalHeaderItem(i)->setToolTip(t.definicion);
        }
        m_tabla->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tabla->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tabla->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tabla->verticalHeader()->setVisible(false);
        m_tabla->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);   // el ancho, repartido
        auto *dockTabla = new QDockWidget(tr("Contactos"), this);
        dockTabla->setWidget(m_tabla);
        addDockWidget(Qt::BottomDockWidgetArea, dockTabla);
        connect(m_tabla, &QTableWidget::itemSelectionChanged, this, [this] {
            const auto filas = m_tabla->selectionModel()->selectedRows();
            if (!filas.isEmpty())
                seleccionar(m_tabla->item(filas.first().row(), 0)->text());
        });
        connect(m_mapa, &MapWidget::targetClicked, this, [this](qint64 id, const QGeoCoordinate &) {
            seleccionar(nombreDe(id));
            refrescar();
        });

        // --- Filtros y registro ---
        auto *izquierda = new QWidget(this);
        auto *col = new QVBoxLayout(izquierda);
        col->addWidget(new QLabel(tr("Tipo de contacto:"), this));
        m_tipo = new QComboBox(this);
        m_tipo->addItem(tr("Todos"), QString());
        m_tipo->addItem(tr("Buques"), QStringLiteral("buque"));
        m_tipo->addItem(tr("Pesqueros"), QStringLiteral("pesquero"));
        m_tipo->addItem(tr("Aeronaves"), QStringLiteral("aeronave"));
        col->addWidget(m_tipo);
        auto *agrupar = new QCheckBox(tr("Agrupar al alejar"), this);
        col->addWidget(agrupar);
        col->addWidget(new QLabel(tr("Avisos (más reciente arriba):"), this));
        m_registro = new QListWidget(this);
        m_registro->setWordWrap(true);
        col->addWidget(m_registro, 1);
        m_contador = new QLabel(this);
        statusBar()->addPermanentWidget(m_contador);
        auto *dockIzq = new QDockWidget(tr("Vista"), this);
        dockIzq->setWidget(izquierda);
        addDockWidget(Qt::LeftDockWidgetArea, dockIzq);
        connect(m_tipo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { aplicarFiltro(); });
        connect(agrupar, &QCheckBox::toggled, this, [this](bool on) {
            TargetClustering g = m_grupos;
            g.enabled = on;
            m_mapa->setTargetClustering(g);
        });

        // --- Glosario ---
        auto *derecha = new QWidget(this);
        auto *colDer = new QVBoxLayout(derecha);
        auto *buscar = new QLineEdit(this);
        buscar->setPlaceholderText(tr("Buscar un término..."));
        buscar->setClearButtonEnabled(true);
        m_listaGlosario = new QListWidget(this);
        auto *definicion = new QTextBrowser(this);
        definicion->setMaximumHeight(150);
        definicion->setPlaceholderText(tr("Elige un término para ver su definición."));
        colDer->addWidget(buscar);
        colDer->addWidget(m_listaGlosario, 1);
        colDer->addWidget(definicion);
        for (const Termino &t : terminos)
            m_listaGlosario->addItem(t.nombre);
        auto *dockGlosario = new QDockWidget(tr("Glosario"), this);
        dockGlosario->setWidget(derecha);
        addDockWidget(Qt::RightDockWidgetArea, dockGlosario);
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

        // --- Menú Ayuda ---
        QMenu *ayuda = menuBar()->addMenu(tr("A&yuda"));
        QAction *glosarioAct = dockGlosario->toggleViewAction();
        glosarioAct->setText(tr("&Glosario"));
        glosarioAct->setShortcut(QKeySequence(Qt::Key_F1));
        ayuda->addAction(glosarioAct);
    }

    MapWidget *m_mapa = nullptr;
    QTableWidget *m_tabla = nullptr;
    QComboBox *m_tipo = nullptr;
    QListWidget *m_registro = nullptr;
    QListWidget *m_listaGlosario = nullptr;
    QLabel *m_contador = nullptr;
    QMap<QString, Contacto> m_contactos;   // por nombre
    QMap<qint64, QString> m_porId;
    QString m_seleccion;
    qint64 m_marcacion = -1;               // la marcación propio -> seleccionado
    TargetClustering m_grupos;
    Simulador m_sim;
    QDateTime m_hora;                      // reloj simulado
    QTimer m_reloj;
    int m_avisos = 0;
};

// Texto de la columna \a col de la fila \a fila de la tabla, o vacío.
static QString celda(QTableWidget *t, int fila, int col)
{
    return t->item(fila, col) ? t->item(fila, col)->text() : QString();
}

// Fila de la tabla cuyo contacto se llama \a nombre, o -1.
static int filaDe(QTableWidget *t, const QString &nombre)
{
    for (int i = 0; i < t->rowCount(); ++i)
        if (celda(t, i, 0) == nombre)
            return i;
    return -1;
}

// Prueba sin ventana, con reloj simulado a mano: cruce frontal (CPA ~ 0), alerta,
// contacto perdido y borrado, filtro, selección y glosario completo.
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
    verificar(QStringLiteral("8 contactos de partida (propio incluido)"), v.numContactos() == 8);

    // --- Glosario: define todas las columnas y los términos de la interfaz ----
    QStringList faltan;
    const auto terminos = glosario();
    const auto definido = [&](const QString &nombre) {
        for (const Termino &t : terminos)
            if (t.nombre == nombre && !t.definicion.isEmpty())
                return true;
        return false;
    };
    for (const Columna &c : columnas())
        if (!definido(c.clave))
            faltan << c.clave;
    // Términos que además usan los avisos, filtros y opciones de la ventana.
    for (const char *t : { "Propio", "Contacto perdido", "Alerta de proximidad", "Zona vigilada",
                           "Marcación", "Anillos de distancia", "Estela", "Atenuar", "Agrupar",
                           "NM", "kn", "Tiempo simulado" })
        if (!definido(QString::fromUtf8(t)))
            faltan << QString::fromUtf8(t);
    verificar(QStringLiteral("glosario define todo lo que usa la interfaz (%1 términos)%2")
                  .arg(terminos.size()).arg(faltan.isEmpty() ? QString() : QStringLiteral(", faltan: ") + faltan.join(QLatin1String(", "))),
              faltan.isEmpty() && v.listaGlosario()->count() == terminos.size());

    // Los avisos de zona llegan dentro de addTarget: deben llevar igualmente el nombre.
    bool avisosConNombre = true;
    int avisosZona = 0;
    for (const QString &a : v.avisos())
        if (a.contains(QLatin1String("Zona restringida:"))) {
            ++avisosZona;
            avisosConNombre = avisosConNombre && !a.trimmed().endsWith(QLatin1String("entra"))
                              && !a.trimmed().endsWith(QLatin1String("sale"));
        }
    out << "      avisos de zona al arrancar: " << avisosZona << "\n";
    verificar(QStringLiteral("los avisos de zona dicen QUÉ contacto entra"), avisosZona > 0 && avisosConNombre);

    // --- Un paso de 30 s: tabla, orden y cruce frontal ------------------------
    v.paso(30.0);
    QTableWidget *t = v.tabla();
    verificar(QStringLiteral("la tabla tiene 8 filas y el propio va arriba"),
              t->rowCount() == 8 && celda(t, 0, 0) == kPropio);
    bool ordenada = true;
    for (int i = 2; i < t->rowCount(); ++i)
        ordenada = ordenada && celda(t, i, 4).toDouble() >= celda(t, i - 1, 4).toDouble();
    verificar(QStringLiteral("el resto, ordenado por distancia al propio"), ordenada);

    const int f = filaDe(t, QStringLiteral("Frontal"));
    const double cpa = f >= 0 ? celda(t, f, 5).toDouble() : 99.0;
    const double tcpa = f >= 0 ? celda(t, f, 6).toDouble() : 99.0;
    out << "      Frontal: dist " << celda(t, f, 4) << " NM, CPA " << celda(t, f, 5) << " NM, TCPA "
        << celda(t, f, 6) << " min, " << celda(t, f, 7) << "\n";
    // 5 NM de proa a 10 + 10 kn: 15 min; han pasado 30 s, quedan ~14,5 min.
    verificar(QStringLiteral("Frontal: CPA < 0,1 NM y TCPA ~ 14,5 min"),
              cpa < 0.1 && tcpa > 13.5 && tcpa < 15.5);
    verificar(QStringLiteral("Frontal está en Alerta y se avisó en el registro"),
              celda(t, f, 7) == QLatin1String("Alerta") && v.numAvisos() > 0);

    // --- Selección: fila -> mapa + marcación (solo una) -----------------------
    t->selectRow(f);
    verificar(QStringLiteral("elegir la fila selecciona el contacto en el mapa"),
              v.seleccion() == QLatin1String("Frontal")
              && v.mapa()->selectedTarget() == v.contactos().value(QStringLiteral("Frontal")).id
              && v.mapa()->rangeMarks().size() == 2 /* anillos + marcación */);
    t->selectRow(filaDe(t, QStringLiteral("Carguero A")));
    verificar(QStringLiteral("elegir otra fila reemplaza la marcación (no se acumulan)"),
              v.seleccion() == QLatin1String("Carguero A") && v.mapa()->rangeMarks().size() == 2);

    // --- Filtro por tipo ------------------------------------------------------
    v.tipoCombo()->setCurrentIndex(v.tipoCombo()->findData(QStringLiteral("aeronave")));
    const int fuera = v.mapa()->filteredOutTargets().size();
    out << "      atenuados por el filtro: " << fuera << "\n";
    verificar(QStringLiteral("filtro «aeronave»: 6 buques y pesqueros atenuados, propio y helicóptero no"),
              fuera == 6);
    v.tipoCombo()->setCurrentIndex(0);

    // --- Contacto perdido y borrado -------------------------------------------
    // Pesquero 2 deja de informar a los 150 s simulados (ya van 30): a los 90 s sin
    // datos pasa a Perdido, y a los 300 s se borra.
    for (int i = 0; i < 6; ++i)
        v.paso(30.0);                       // t = 210 s: último parte a 120 s -> 90 s
    verificar(QStringLiteral("a los 90 s sin datos, Pesquero 2 sigue Activo (umbral exclusivo)"),
              v.contactos().value(QStringLiteral("Pesquero 2")).estado != QLatin1String("Perdido"));
    v.paso(30.0);                           // t = 240 s: 120 s sin datos
    const int fp = filaDe(t, QStringLiteral("Pesquero 2"));
    verificar(QStringLiteral("pasados 90 s sin datos, Pesquero 2 está Perdido y sin CPA"),
              fp >= 0 && celda(t, fp, 7) == QLatin1String("Perdido") && celda(t, fp, 5) == QStringLiteral("—"));
    verificar(QStringLiteral("el filtro lo atenúa (predicado sobre su estado)"),
              v.mapa()->filteredOutTargets().contains(v.contactos().value(QStringLiteral("Pesquero 2")).id));
    for (int i = 0; i < 7; ++i)
        v.paso(30.0);                       // t = 450 s: 330 s sin datos (el umbral es «más de» 300 s)
    verificar(QStringLiteral("pasados 5 min sin datos, Pesquero 2 se borra (7 contactos)"),
              filaDe(t, QStringLiteral("Pesquero 2")) < 0 && v.numContactos() == 7);
    verificar(QStringLiteral("el propio y los demás siguen activos"),
              v.contactos().value(kPropio).estado == QLatin1String("Activo")
              && v.contactos().value(QStringLiteral("Carguero B")).estado != QLatin1String("Perdido"));

    out << (todoOk ? "TODO BIEN\n" : "HAY FALLOS\n");
    return todoOk ? 0 : 1;
}

// Arranca la ventana (o, con --comprobar, la prueba sin ventana; con --captura=,
// la deja correr unos pasos y guarda una imagen).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("monitor_trafico"));

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

    // --captura=imagen.png: unos pasos de simulación, espera a las teselas,
    // guarda la ventana y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(strlen("--captura=")));
            ventana.detener();
            for (int i = 0; i < 12; ++i)
                ventana.paso(kEscala);
            QTimer::singleShot(2500, &app, [&ventana, &app, destino] {
                ventana.grab().save(destino);
                app.quit();
            });
        }
    }
    return app.exec();
}
