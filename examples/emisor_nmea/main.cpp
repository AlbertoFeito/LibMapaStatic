// emisor_nmea - simula un receptor de NMEA 0183 y AIS y lo emite por UDP, con libtelemetria.
//
// Décimo ejemplo de la serie y, a diferencia de los demás, SIN mapa: solo usa libtelemetria
// (el decodificador de NMEA/AIS) y Qt Network. Es la pareja de `cliente_telemetria`: lo que este
// escucha, esto lo emite. Sirve para probar un cliente sin hardware, para generar capturas
// reproducibles y como ejemplo de cómo se CODIFICA con la biblioteca (formatRmc, encodeAis...).
//
// El escenario es una mañana frente a La Habana, con cinco barcos:
//   - «Propio» (el que lleva el receptor): 10 nudos al este. Manda RMC, GGA, VTG y HDT cada segundo
//     y su propio informe AIS (!AIVDO).
//   - HABANA STAR, carguero de clase A (tipos 1 y 5): viene de frente por el mismo carril, para que
//     se cruce con el propio.
//   - PESCADOR 7, pesquero de clase B (tipos 18 y 24, partes A y B), casi parado al norte.
//   - TANKER SUR, petrolero de clase A al sur, rumbo noreste.
//   - REMOLCADOR 3, de clase A, que se queda sin emitir a los 90 s: para ver qué hace el cliente con
//     un contacto perdido.
// Y, de vez en cuando, ruido: una frase con la suma de comprobación estropeada, una GSV que ningún
// cliente decodifica y una frase cortada. Un cliente decente cuenta todo eso y sigue.
//
//   emisor_nmea [--host=127.0.0.1] [--puerto=10110] [--escala=1] [--duracion=0] [--agrupar]
//   emisor_nmea --salida=captura.nmea [--duracion=120]
//        -> en vez de la red, escribe la captura en un fichero (para `cliente_telemetria --reproducir=`).
//   emisor_nmea --comprobar
//        -> sin red de verdad: simula 120 s, decodifica lo emitido con la propia biblioteca y comprueba
//           barcos, nombres, posiciones, ruido y fragmentos; y manda un datagrama UDP por localhost.
//
// --escala=N hace que cada segundo real avance N segundos simulados (los barcos se mueven más rápido).
// --duracion=S termina a los S segundos simulados (0 = hasta que se cierre). --agrupar manda todas las
// frases de un segundo en un solo datagrama, separadas por saltos de línea, como hacen muchos
// multiplexores; por omisión va una frase por datagrama.

#include <telemetria/Decoder.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QMap>
#include <QNetworkDatagram>
#include <QTextStream>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>
#include <cmath>

using namespace telemetria;

namespace {

// Un barco simulado. Se mueve en línea recta a su rumbo y velocidad.
struct Barco {
    quint32 mmsi = 0;
    QString nombre;
    QString indicativo;
    int tipoBarco = 0;
    bool claseB = false;
    bool propio = false;
    double lat = 0.0, lon = 0.0;
    double nudos = 0.0, rumbo = 0.0;
    int proaM = 0, popaM = 0, babor = 0, estribor = 0;
    double silencioDesde = -1.0;        // segundos simulados a partir de los que deja de emitir (-1: nunca)
    int primeraEmision = 0;             // desfase (s) para que no emitan todos a la vez
};

// El escenario de la mañana frente a La Habana.
QVector<Barco> escenario()
{
    QVector<Barco> v;
    {
        Barco b;
        b.mmsi = 323000100;
        b.nombre = QStringLiteral("PROPIO");
        b.indicativo = QStringLiteral("CO0001");
        b.tipoBarco = 37;
        b.propio = true;
        b.lat = 23.20;
        b.lon = -82.60;
        b.nudos = 10.0;
        b.rumbo = 90.0;
        v.append(b);
    }
    {
        Barco b;
        b.mmsi = 323000101;
        b.nombre = QStringLiteral("HABANA STAR");
        b.indicativo = QStringLiteral("COHS01");
        b.tipoBarco = 70;
        b.lat = 23.20;
        b.lon = -82.35;
        b.nudos = 12.0;
        b.rumbo = 270.0;
        b.proaM = 150;
        b.popaM = 30;
        b.babor = 12;
        b.estribor = 13;
        b.primeraEmision = 1;
        v.append(b);
    }
    {
        Barco b;
        b.mmsi = 323000103;
        b.nombre = QStringLiteral("PESCADOR 7");
        b.indicativo = QStringLiteral("COPE07");
        b.tipoBarco = 30;
        b.claseB = true;
        b.lat = 23.32;
        b.lon = -82.50;
        b.nudos = 3.5;
        b.rumbo = 10.0;
        b.proaM = 9;
        b.popaM = 3;
        b.babor = 2;
        b.estribor = 2;
        b.primeraEmision = 2;
        v.append(b);
    }
    {
        Barco b;
        b.mmsi = 351000104;
        b.nombre = QStringLiteral("TANKER SUR");
        b.indicativo = QStringLiteral("3FTS04");
        b.tipoBarco = 80;
        b.lat = 23.05;
        b.lon = -82.55;
        b.nudos = 9.0;
        b.rumbo = 45.0;
        b.proaM = 180;
        b.popaM = 40;
        b.babor = 16;
        b.estribor = 16;
        b.primeraEmision = 3;
        v.append(b);
    }
    {
        Barco b;
        b.mmsi = 323000105;
        b.nombre = QStringLiteral("REMOLCADOR 3");
        b.indicativo = QStringLiteral("COTG03");
        b.tipoBarco = 52;
        b.lat = 23.14;
        b.lon = -82.45;
        b.nudos = 6.0;
        b.rumbo = 180.0;
        b.proaM = 20;
        b.popaM = 8;
        b.babor = 5;
        b.estribor = 5;
        b.silencioDesde = 90.0;
        b.primeraEmision = 4;
        v.append(b);
    }
    return v;
}

// Quién emite qué y cuándo: el escenario, su reloj simulado, y las frases que salen en cada segundo.
class Simulador
{
public:
    explicit Simulador(const QDateTime &inicio) : m_barcos(escenario()), m_hora(inicio) {}

    const QVector<Barco> &barcos() const { return m_barcos; }
    int segundos() const { return m_t; }
    QDateTime hora() const { return m_hora; }
    int ruidoSumaMala() const { return m_sumaMala; }
    int ruidoGsv() const { return m_gsv; }
    int ruidoCortadas() const { return m_cortadas; }

    // Avanza un segundo simulado y devuelve las frases (sin salto de línea) que se emiten en él.
    QList<QByteArray> avanzar()
    {
        ++m_t;
        m_hora = m_hora.addSecs(1);
        QList<QByteArray> frases;
        for (Barco &b : m_barcos) {
            mover(&b);
            if (b.silencioDesde >= 0.0 && m_t >= int(b.silencioDesde))
                continue;
            if (b.propio)
                emitirPropio(b, &frases);
            else
                emitirAjeno(b, &frases);
        }
        emitirRuido(&frases);
        return frases;
    }

private:
    // Un segundo de movimiento en línea recta (una milla náutica son 1/60 de grado de latitud).
    static void mover(Barco *b)
    {
        const double rad = M_PI / 180.0;
        const double millas = b->nudos / 3600.0;
        b->lat += millas * std::cos(b->rumbo * rad) / 60.0;
        b->lon += millas * std::sin(b->rumbo * rad) / (60.0 * std::cos(b->lat * rad));
    }

    // Un informe de posición AIS del barco.
    static AisMessage posicionAis(const Barco &b, int segundoUtc)
    {
        AisMessage m;
        m.type = b.claseB ? 18 : 1;
        m.mmsi = b.mmsi;
        m.lat = b.lat;
        m.lon = b.lon;
        m.sogKn = b.nudos;
        m.cogDeg = b.rumbo;
        m.headingDeg = int(std::lround(b.rumbo)) % 360;
        m.navStatus = b.nudos < 0.5 ? 1 : 0;
        m.second = segundoUtc;
        m.classB = b.claseB;
        return m;
    }

    // Los datos estáticos del barco: el tipo 5 (clase A) o las dos partes del 24 (clase B).
    static QList<AisMessage> estaticosAis(const Barco &b)
    {
        QList<AisMessage> l;
        if (!b.claseB) {
            AisMessage m;
            m.type = 5;
            m.mmsi = b.mmsi;
            m.imo = 9000000 + b.mmsi % 1000;
            m.callSign = b.indicativo;
            m.name = b.nombre;
            m.shipType = b.tipoBarco;
            m.toBowM = b.proaM;
            m.toSternM = b.popaM;
            m.toPortM = b.babor;
            m.toStarboardM = b.estribor;
            m.draughtM = 6.5;
            m.destination = QStringLiteral("LA HABANA");
            m.etaMonth = 11;
            m.etaDay = 3;
            m.etaHour = 14;
            m.etaMinute = 30;
            l.append(m);
        } else {
            AisMessage a;
            a.type = 24;
            a.partNo = 0;
            a.mmsi = b.mmsi;
            a.name = b.nombre;
            AisMessage p = a;
            p.partNo = 1;
            p.shipType = b.tipoBarco;
            p.callSign = b.indicativo;
            p.toBowM = b.proaM;
            p.toSternM = b.popaM;
            p.toPortM = b.babor;
            p.toStarboardM = b.estribor;
            l.append(a);
            l.append(p);
        }
        return l;
    }

    // Las frases NMEA del propio barco, cada segundo, y su informe AIS (!AIVDO) cada 10 s.
    void emitirPropio(const Barco &b, QList<QByteArray> *frases)
    {
        Rmc r;
        r.valid = true;
        r.time = m_hora.time();
        r.utc = m_hora;
        r.lat = b.lat;
        r.lon = b.lon;
        r.sogKn = b.nudos;
        r.cogDeg = b.rumbo;
        r.magVarDeg = 8.0;
        frases->append(formatRmc(r));
        Gga g;
        g.time = m_hora.time();
        g.lat = b.lat;
        g.lon = b.lon;
        g.quality = 1;
        g.satellites = 9;
        g.hdop = 0.9;
        g.altitudeM = 4.0;
        frases->append(formatGga(g));
        Vtg v;
        v.cogTrueDeg = b.rumbo;
        v.cogMagDeg = std::fmod(b.rumbo + 352.0, 360.0);
        v.sogKn = b.nudos;
        v.sogKmh = b.nudos * 1.852;
        frases->append(formatVtg(v));
        Hdt h;
        h.headingDeg = b.rumbo;
        frases->append(formatHdt(h));
        if (m_t % 10 == 0)
            frases->append(encodeAis(posicionAis(b, m_hora.time().second()), 'A', 1, true).first());
    }

    // El informe de un barco ajeno: posición cada 5 s (clase A) o 15 s (clase B), estáticos cada 30 s.
    void emitirAjeno(const Barco &b, QList<QByteArray> *frases)
    {
        const char canal = b.claseB ? 'B' : 'A';
        const int periodo = b.claseB ? 15 : 5;
        if ((m_t + b.primeraEmision) % periodo == 0)
            frases->append(encodeAis(posicionAis(b, m_hora.time().second()), canal).first());
        if ((m_t + b.primeraEmision) % 30 == 0) {
            // Un tipo 5 son dos frases (las dos salen en el mismo segundo); el tipo 24, una por parte.
            for (const AisMessage &m : estaticosAis(b))
                for (const QByteArray &f : encodeAis(m, canal, m_t % 10))
                    frases->append(f);
        }
    }

    // El ruido: una suma estropeada cada 20 s, una GSV cada 40 s y una frase cortada cada 60 s.
    void emitirRuido(QList<QByteArray> *frases)
    {
        if (m_t % 20 == 7) {
            QByteArray mala = formatVtg(Vtg{ 90.0, 98.0, 10.0, 18.5 });
            mala[mala.size() - 1] = mala.at(mala.size() - 1) == '0' ? '1' : '0';
            frases->append(mala);
            ++m_sumaMala;
        }
        if (m_t % 40 == 11) {
            frases->append(formatSentence("GPGSV", { "3", "1", "11", "03", "03", "111", "00", "04", "15", "270", "00",
                                                     "06", "01", "010", "00", "13", "06", "292", "00" }));
            ++m_gsv;
        }
        if (m_t % 60 == 31) {
            Rmc r;
            r.valid = true;
            r.time = m_hora.time();
            r.utc = m_hora;
            r.lat = 23.0;
            r.lon = -82.0;
            frases->append(formatRmc(r).left(25));              // se corta a medias: sin suma ni final
            ++m_cortadas;
        }
    }

    QVector<Barco> m_barcos;
    QDateTime m_hora;
    int m_t = 0;
    int m_sumaMala = 0, m_gsv = 0, m_cortadas = 0;
};

// Manda la captura por UDP a \a host:\a puerto o, si hay fichero abierto, la guarda en él.
struct Salida {
    QUdpSocket socket;
    QHostAddress host = QHostAddress::LocalHost;
    quint16 puerto = 10110;
    QFile fichero;
    bool agrupar = false;
    quint64 datagramas = 0;
    quint64 lineas = 0;

    // Entrega las frases de un segundo.
    void enviar(const QList<QByteArray> &frases)
    {
        lineas += quint64(frases.size());
        if (fichero.isOpen()) {
            for (const QByteArray &f : frases)
                fichero.write(f + "\r\n");
            return;
        }
        if (agrupar && !frases.isEmpty()) {
            QByteArray todo;
            for (const QByteArray &f : frases)
                todo += f + "\r\n";
            socket.writeDatagram(todo, host, puerto);
            ++datagramas;
            return;
        }
        for (const QByteArray &f : frases) {
            socket.writeDatagram(f + "\r\n", host, puerto);
            ++datagramas;
        }
    }
};

// El valor de una opción «--nombre=valor», o \a porDefecto.
QString opcion(const QStringList &args, const QString &nombre, const QString &porDefecto = QString())
{
    const QString prefijo = QStringLiteral("--") + nombre + QLatin1Char('=');
    for (const QString &a : args)
        if (a.startsWith(prefijo))
            return a.mid(prefijo.size());
    return porDefecto;
}

// La prueba sin pantalla: simula 120 s, decodifica lo emitido y lo compara con lo que se simuló.
int comprobar()
{
    QTextStream out(stdout);
    bool todoOk = true;
    const auto verificar = [&](const QString &que, bool ok) {
        out << (ok ? "ok    " : "FALLO ") << que << "\n";
        todoOk = todoOk && ok;
    };

    const QDateTime inicio(QDate(2026, 10, 19), QTime(12, 0, 0), Qt::UTC);
    Simulador sim(inicio);
    QList<QByteArray> captura;
    for (int s = 0; s < 120; ++s)
        captura += sim.avanzar();
    out << "      " << captura.size() << " lineas emitidas en 120 s simulados\n";

    Decoder d;
    int rmc = 0, gga = 0, vtg = 0, hdt = 0, propios = 0;
    QMap<quint32, AisMessage> ultimaPosicion;
    QMap<quint32, AisMessage> estatico;
    QMap<quint32, int> informes;
    Rmc ultimoRmc;
    for (const QByteArray &f : captura) {
        for (const Event &e : d.feedLine(f)) {
            switch (e.kind) {
            case Event::Kind::Rmc: ++rmc; ultimoRmc = e.rmc; break;
            case Event::Kind::Gga: ++gga; break;
            case Event::Kind::Vtg: ++vtg; break;
            case Event::Kind::Hdt: ++hdt; break;
            case Event::Kind::Ais:
                if (e.ownShip) {
                    ++propios;
                } else if (e.ais.hasPosition()) {
                    ultimaPosicion[e.ais.mmsi] = e.ais;
                    ++informes[e.ais.mmsi];
                } else if (!e.ais.name.isEmpty() || e.ais.shipType != 0) {
                    AisMessage &s = estatico[e.ais.mmsi];
                    if (!e.ais.name.isEmpty())
                        s.name = e.ais.name;
                    if (e.ais.shipType != 0)
                        s.shipType = e.ais.shipType;
                    if (!e.ais.callSign.isEmpty())
                        s.callSign = e.ais.callSign;
                    if (e.ais.lengthM() > 0) {
                        s.toBowM = e.ais.toBowM;
                        s.toSternM = e.ais.toSternM;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    const DecoderStats &st = d.stats();
    out << "      RMC " << rmc << ", GGA " << gga << ", VTG " << vtg << ", HDT " << hdt << ", AIVDO " << propios
        << "; suma mala " << st.badChecksum << ", sin suma/cortadas " << st.malformed << ", ignoradas " << st.ignored
        << ", fragmentos perdidos " << st.aisLost << "\n";
    verificar(QStringLiteral("el propio manda RMC, GGA, VTG y HDT cada segundo (120 de cada) y 12 informes !AIVDO"),
              rmc == 120 && gga == 120 && vtg == 120 && hdt == 120 && propios == 12);
    verificar(QStringLiteral("el ruido que se inyecta es lo que el decodificador cuenta: %1 sumas malas, %2 cortadas, %3 GSV")
                  .arg(sim.ruidoSumaMala()).arg(sim.ruidoCortadas()).arg(sim.ruidoGsv()),
              st.badChecksum == quint64(sim.ruidoSumaMala()) && st.malformed == quint64(sim.ruidoCortadas())
              && st.ignored == quint64(sim.ruidoGsv()));
    verificar(QStringLiteral("ningun fragmento AIS se pierde: los tipos 5 llegan de dos en dos"), st.aisLost == 0);

    // Los cuatro ajenos aparecen con su MMSI, y su ultima posicion coincide con la simulada.
    const QVector<Barco> &barcos = sim.barcos();
    bool posOk = true, nombresOk = true, tiposOk = true;
    for (const Barco &b : barcos) {
        if (b.propio)
            continue;
        if (!ultimaPosicion.contains(b.mmsi)) {
            posOk = false;
            continue;
        }
        const AisMessage &m = ultimaPosicion[b.mmsi];
        // Cada barco se movio hasta su ultimo informe (hasta 15 s antes del final): 15 s a 12 kn son 0,05 NM.
        const double dLat = std::abs(m.lat - b.lat) * 60.0;           // en millas
        const double dLon = std::abs(m.lon - b.lon) * 60.0 * std::cos(b.lat * M_PI / 180.0);
        posOk = posOk && std::hypot(dLat, dLon) < 0.08;
        nombresOk = nombresOk && estatico.value(b.mmsi).name == b.nombre
                    && estatico.value(b.mmsi).callSign == b.indicativo;
        tiposOk = tiposOk && estatico.value(b.mmsi).shipType == b.tipoBarco
                  && estatico.value(b.mmsi).lengthM() == b.proaM + b.popaM;
    }
    verificar(QStringLiteral("los 4 barcos ajenos se reciben, con su posicion a menos de 0,08 NM de la simulada"),
              posOk && ultimaPosicion.size() == 4);
    verificar(QStringLiteral("sus nombres e indicativos llegan por los tipos 5 (dos frases) y 24 (partes A y B)"), nombresOk);
    verificar(QStringLiteral("sus tipos de barco y esloras tambien"), tiposOk);
    verificar(QStringLiteral("el pesquero es de clase B (tipo 18) y el carguero de clase A"),
              ultimaPosicion.value(323000103).classB && !ultimaPosicion.value(323000101).classB);
    // El remolcador deja de emitir a los 90 s: su ultimo informe es de antes, y los demas siguen.
    const int informesRemolcador = informes.value(323000105);
    verificar(QStringLiteral("el remolcador se calla a los 90 s: da ~18 informes (uno cada 5 s) y no 24 como los demas (%1)")
                  .arg(informesRemolcador),
              informesRemolcador >= 16 && informesRemolcador <= 19 && informes.value(323000101) >= 23);
    // El propio, al final, esta donde la simulacion dice.
    const Barco &yo = barcos.first();
    verificar(QStringLiteral("la ultima RMC del propio coincide con su posicion simulada (a menos de 1/10000 de minuto)"),
              std::abs(ultimoRmc.lat - yo.lat) < 1.0 / 600000.0 && std::abs(ultimoRmc.lon - yo.lon) < 1.0 / 600000.0
              && ultimoRmc.utc == inicio.addSecs(120));
    // Los dos barcos van de frente por el mismo carril: rumbos opuestos y la misma latitud.
    const Barco &carguero = barcos.at(1);
    verificar(QStringLiteral("el carguero viene de frente por el mismo carril que el propio (rumbos opuestos, misma latitud)"),
              std::abs(std::fmod(carguero.rumbo - yo.rumbo + 360.0, 360.0) - 180.0) < 1e-9
              && std::abs(carguero.lat - yo.lat) < 1e-3);

    // La salida por UDP: un datagrama a localhost llega entero.
    QUdpSocket receptor;
    const bool escucha = receptor.bind(QHostAddress::LocalHost, 0);
    verificar(QStringLiteral("se puede abrir un socket UDP en localhost"), escucha);
    if (escucha) {
        Salida salida;
        salida.puerto = receptor.localPort();
        salida.enviar({ captura.first(), captura.at(1) });
        QList<QByteArray> recibidos;
        for (int i = 0; i < 40 && recibidos.size() < 2; ++i)
            if (receptor.waitForReadyRead(50))
                while (receptor.hasPendingDatagrams())
                    recibidos << receptor.receiveDatagram().data();
        verificar(QStringLiteral("el emisor manda una frase por datagrama y llegan tal cual"),
                  recibidos.size() == 2 && recibidos.at(0) == captura.at(0) + "\r\n"
                  && recibidos.at(1) == captura.at(1) + "\r\n" && salida.datagramas == 2);
        salida.agrupar = true;
        salida.enviar({ captura.first(), captura.at(1) });
        QByteArray agrupado;
        for (int i = 0; i < 40 && agrupado.isEmpty(); ++i)
            if (receptor.waitForReadyRead(50))
                agrupado = receptor.receiveDatagram().data();
        verificar(QStringLiteral("con --agrupar todas las frases de un segundo van en un solo datagrama"),
                  agrupado == captura.at(0) + "\r\n" + captura.at(1) + "\r\n" && salida.datagramas == 3);
    }

    out << (todoOk ? "TODO BIEN\n" : "HAY FALLOS\n");
    return todoOk ? 0 : 1;
}

} // namespace

// Arranca el emisor: por UDP, a un fichero, o la prueba con --comprobar.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("emisor_nmea"));
    const QStringList args = app.arguments().mid(1);
    if (args.contains(QStringLiteral("--comprobar")))
        return comprobar();

    Salida salida;
    salida.host = QHostAddress(opcion(args, QStringLiteral("host"), QStringLiteral("127.0.0.1")));
    salida.puerto = quint16(opcion(args, QStringLiteral("puerto"), QStringLiteral("10110")).toUInt());
    salida.agrupar = args.contains(QStringLiteral("--agrupar"));
    const double escala = std::max(0.1, opcion(args, QStringLiteral("escala"), QStringLiteral("1")).toDouble());
    const int duracion = opcion(args, QStringLiteral("duracion"), QStringLiteral("0")).toInt();
    const QString destino = opcion(args, QStringLiteral("salida"));
    QTextStream err(stderr);
    if (salida.host.isNull()) {
        err << "--host no es una direccion IP valida\n";
        return 2;
    }
    if (!destino.isEmpty()) {
        salida.fichero.setFileName(destino);
        if (!salida.fichero.open(QIODevice::WriteOnly)) {
            err << "No se puede escribir " << destino << "\n";
            return 2;
        }
    }

    Simulador sim(QDateTime::currentDateTimeUtc());
    if (!destino.isEmpty()) {
        // A un fichero no se espera: se genera la duracion pedida (o 120 s) de golpe.
        for (int s = 0; s < (duracion > 0 ? duracion : 120); ++s)
            salida.enviar(sim.avanzar());
        salida.fichero.close();
        QTextStream(stdout) << salida.lineas << " lineas en " << destino << "\n";
        return 0;
    }
    QTextStream(stdout) << "Emitiendo NMEA/AIS a " << salida.host.toString() << ":" << salida.puerto
                        << " (escala x" << escala << "). Ctrl+C para parar.\n";
    QTimer reloj;
    QObject::connect(&reloj, &QTimer::timeout, &app, [&] {
        for (int i = 0; i < int(std::lround(escala)); ++i)
            salida.enviar(sim.avanzar());
        if (duracion > 0 && sim.segundos() >= duracion)
            app.quit();
    });
    reloj.start(1000);
    return app.exec();
}
