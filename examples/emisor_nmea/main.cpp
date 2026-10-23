// emisor_nmea - simula un receptor de NMEA 0183 y AIS y lo emite por UDP, con libtelemetria.
//
// Décimo ejemplo de la serie y, a diferencia de los demás, SIN mapa: solo usa libtelemetria
// (el decodificador de NMEA/AIS) y Qt Network. Es la pareja de `cliente_telemetria`: lo que este
// escucha, esto lo emite. Sirve para probar un cliente sin hardware, para generar capturas
// reproducibles y como ejemplo de cómo se CODIFICA con la biblioteca (formatRmc, encodeAis...).
//
// El escenario es una mañana frente a La Habana, con cinco barcos y tres estaciones fijas:
//   - «Propio» (el que lleva el receptor): 10 nudos al este. Manda RMC, GGA, VTG y HDT cada segundo
//     y su propio informe AIS (!AIVDO).
//   - HABANA STAR, carguero de clase A (tipos 1 y 5): viene de frente por el mismo carril, para que
//     se cruce con el propio.
//   - PESCADOR 7, pesquero de clase B (tipos 18 y 24, partes A y B), casi parado al norte.
//   - TANKER SUR, petrolero de clase A al sur, rumbo noreste.
//   - REMOLCADOR 3, de clase A, que se queda sin emitir a los 90 s: para ver qué hace el cliente con
//     un contacto perdido.
//   - FARO MORRO y BOYA CANAL, dos ayudas a la navegación (tipo 21), y la estación base del puerto
//     (tipo 4, fecha y hora UTC, y un aviso de seguridad, tipo 14, cada minuto). No se mueven.
//   - Un catálogo con un ejemplo de cada tipo AIS que falta (2, 3, 6 a 13, 15 a 17, 19, 20, 22, 23, 25 a 27, y una
//     ayuda virtual): con los de arriba, salen los 27 tipos de ITU-R M.1371. Uno por segundo, cada minuto.
// Y, de vez en cuando, ruido: una frase con la suma de comprobación estropeada, una GSV que ningún
// cliente decodifica y una frase cortada. Un cliente decente cuenta todo eso y sigue.
//
//   emisor_nmea [--host=127.0.0.1] [--puerto=10110] [--escala=1] [--duracion=0] [--agrupar]
//   emisor_nmea --tcp=10110 [--escala=1] [--duracion=0]
//        -> en vez de UDP, hace de SERVIDOR TCP en ese puerto y manda las frases a los clientes que se conecten
//           (como un receptor AIS de red o AIS-catcher): para probar el modo TCP de cliente_telemetria.
//   emisor_nmea --salida=captura.nmea [--duracion=120]
//        -> en vez de la red, escribe la captura en un fichero (para `cliente_telemetria --reproducir=`).
//   emisor_nmea --comprobar
//        -> sin red de verdad: simula 120 s, decodifica lo emitido con la propia biblioteca y comprueba
//           barcos, nombres, posiciones, ruido, fragmentos y el catálogo de tipos AIS; y manda un datagrama UDP por localhost.
//
// Un puerto serie no se emite desde aquí: con un par de puertos virtuales (com0com en Windows, `socat` en Linux) se
// puentea el servidor TCP o un fichero --salida al extremo que lee el cliente.
//
// --escala=N hace que cada segundo real avance N segundos simulados (los barcos se mueven más rápido).
// --duracion=S termina a los S segundos simulados (0 = hasta que se cierre). --agrupar manda todas las
// frases de un segundo en un solo datagrama, separadas por saltos de línea, como hacen muchos
// multiplexores; por omisión va una frase por datagrama.

#include <telemetria/Decoder.h>

#if !defined(TELEMETRIA_API_LEVEL) || TELEMETRIA_API_LEVEL < 3
#  error "La libtelemetria instalada es anterior a este ejemplo: reinstalala (qmake/libmapa/libmapa.pro o qmake/libtelemetria/telemetria.pro y make install; con CMake, cmake --install)."
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QtNetwork/QHostAddress>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>
#include <QtNetwork/QUdpSocket>

#include <algorithm>
#include <cmath>
#include <functional>

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

// Segundos entre los informes de una ayuda a la navegación. Una real emite cada 3 min; aquí cada 30 s para que
// la demostración las muestre pronto.
const int kPeriodoAyudaSeg = 30;
// El aviso de seguridad (tipo 14) que repite la estación base cada minuto.
const char kAvisoSeguridad[] = "TRAFICO PESADO EN LA ENTRADA DEL PUERTO";

// Una estación fija: una ayuda a la navegación (tipo 21) o la estación base del puerto (tipo 4, más su aviso, tipo 14).
struct Fija {
    quint32 mmsi = 0;
    QString nombre;
    int tipoMensaje = 21;               // 21 = ayuda a la navegación, 4 = estación base
    int tipoAyuda = 0;                  // solo en el 21: el código de la tabla de M.1371 (6 = luz con sector...)
    double lat = 0.0, lon = 0.0;
    int primeraEmision = 0;             // desfase (s) para que no emitan todas a la vez
};

// Las estaciones fijas del escenario: el faro del Morro, una boya del canal y la estación base de La Habana.
QVector<Fija> escenarioFijas()
{
    QVector<Fija> v;
    {
        Fija f;
        f.mmsi = 993230001;             // 99MIDxxxx: una ayuda a la navegación
        f.nombre = QStringLiteral("FARO MORRO");
        f.tipoAyuda = 6;
        f.lat = 23.1667;
        f.lon = -82.3567;
        f.primeraEmision = 5;
        v.append(f);
    }
    {
        Fija f;
        f.mmsi = 993230002;
        f.nombre = QStringLiteral("BOYA CANAL");
        f.tipoAyuda = 24;               // flotante de babor
        f.lat = 23.1500;
        f.lon = -82.3400;
        f.primeraEmision = 12;
        v.append(f);
    }
    {
        Fija f;
        f.mmsi = 3230001;               // 00MIDxxxx: una estación base
        f.nombre = QStringLiteral("BASE LA HABANA");
        f.tipoMensaje = 4;
        f.lat = 23.1450;
        f.lon = -82.3500;
        v.append(f);
    }
    return v;
}

// El primer MMSI del catálogo: cada ejemplo usa kMmsiCatalogo + su posición en la lista, para reconocerlos al decodificar.
const quint32 kMmsiCatalogo = 970000001;

// Un ejemplo del catálogo: un mensaje AIS de un tipo (o variante de un tipo) que ni los barcos ni las estaciones
// fijas emiten, y la comprobación de que, decodificado, conserva lo que lleva. Entre los barcos (1, 5, 18, 24) y las
// estaciones fijas (4, 14, 21) salen ya los otros siete tipos de los 27 de ITU-R M.1371 (el 28 es reservado).
struct EjemploAis {
    QString etiqueta;
    AisMessage mensaje;
    std::function<bool(const AisMessage &)> correcto;
};

// ¿\a a y \a b coinciden dentro de la resolución más basta de AIS (1/10 de minuto, que usan los tipos 22, 23 y 27)?
static bool cerca(double a, double b)
{
    return std::abs(a - b) < 1.0 / 600.0;
}

// El catálogo: un ejemplo de cada tipo AIS que falta, con sus variantes (direccionado y difundido, con y sin
// estructura DAC/FID, área y destinos). Salen uno por segundo, cada minuto; sirve para probar que un cliente
// los entiende todos, o los ignora sin romperse. Las direcciones apuntan a barcos del escenario.
QVector<EjemploAis> catalogoAis()
{
    QVector<EjemploAis> v;
    const auto nuevo = [&v](const QString &etiqueta, int tipo) -> AisMessage & {
        EjemploAis e;
        e.etiqueta = etiqueta;
        e.mensaje.type = tipo;
        e.mensaje.mmsi = kMmsiCatalogo + quint32(v.size());
        v.append(e);
        return v.last().mensaje;
    };
    const auto comprobacion = [&v](std::function<bool(const AisMessage &)> f) { v.last().correcto = std::move(f); };

    // Tipos 2 y 3: el mismo informe de posición de clase A que el 1, con otra forma de programar la emisión.
    for (int tipo = 2; tipo <= 3; ++tipo) {
        AisMessage &m = nuevo(QStringLiteral("posicion de clase A, tipo %1").arg(tipo), tipo);
        m.lat = 23.25 + 0.01 * tipo;
        m.lon = -82.70;
        m.sogKn = 8.0;
        m.cogDeg = 60.0;
        m.headingDeg = 60;
        m.navStatus = 0;
        m.second = 20;
        const double lat = m.lat;
        comprobacion([tipo, lat](const AisMessage &d) {
            return d.type == tipo && cerca(d.lat, lat) && d.sogKn == 8.0 && d.cogDeg == 60.0 && d.headingDeg == 60;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("binario direccionado (DAC 200, FID 10)"), 6);
        m.destMmsi = 323000101;
        m.sequence = 1;
        m.retransmit = true;
        m.dac = 200;
        m.fid = 10;
        m.binaryData = QByteArray::fromHex("deadbeef");
        m.binaryBits = 32;
        comprobacion([](const AisMessage &d) {
            return d.type == 6 && d.destMmsi == 323000101 && d.sequence == 1 && d.retransmit && d.dac == 200
                   && d.fid == 10 && d.binaryBits == 32 && d.binaryData.toHex() == "deadbeef";
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("acuse de recibo de un binario, a dos estaciones"), 7);
        m.ackMmsi = { 323000101, 323000103 };
        m.ackSequence = { 1, 2 };
        comprobacion([](const AisMessage &d) {
            return d.type == 7 && d.ackMmsi == QVector<quint32>({ 323000101, 323000103 }) && d.ackSequence == QVector<int>({ 1, 2 });
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("binario difundido (DAC 1, FID 31)"), 8);
        m.dac = 1;
        m.fid = 31;
        m.binaryData = QByteArray::fromHex("0102030405");
        m.binaryBits = 40;
        comprobacion([](const AisMessage &d) {
            return d.type == 8 && d.dac == 1 && d.fid == 31 && d.binaryBits == 40 && d.binaryData.toHex() == "0102030405";
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("aeronave de busqueda y rescate (SAR)"), 9);
        m.lat = 23.10;
        m.lon = -82.45;
        m.altitudeM = 1500.0;
        m.sogKn = 120.0;                            // en nudos enteros: el tipo 9 no usa décimas
        m.cogDeg = 271.0;
        m.second = 25;
        m.accuracy = true;
        comprobacion([](const AisMessage &d) {
            return d.type == 9 && cerca(d.lat, 23.10) && cerca(d.lon, -82.45) && d.altitudeM == 1500.0
                   && d.sogKn == 120.0 && d.cogDeg == 271.0 && d.headingDeg == -1;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("peticion de fecha y hora UTC"), 10);
        m.destMmsi = 323000101;
        comprobacion([](const AisMessage &d) { return d.type == 10 && d.destMmsi == 323000101; });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("respuesta de fecha y hora UTC de una estacion movil"), 11);
        m.lat = 23.18;
        m.lon = -82.55;
        m.accuracy = true;
        m.epfsType = 1;
        comprobacion([](const AisMessage &d) {
            return d.type == 11 && d.utcYear == 2026 && d.utcMonth == 10 && d.utcDay == 19 && cerca(d.lat, 23.18)
                   && d.epfsType == 1;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("texto de seguridad direccionado"), 12);
        m.destMmsi = 323000101;
        m.sequence = 2;
        m.text = QStringLiteral("ENTRE POR EL CANAL NORTE");
        comprobacion([](const AisMessage &d) {
            return d.type == 12 && d.destMmsi == 323000101 && d.sequence == 2 && d.text == QLatin1String("ENTRE POR EL CANAL NORTE");
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("acuse de recibo de un texto de seguridad"), 13);
        m.ackMmsi = { 477553000 };
        m.ackSequence = { 3 };
        comprobacion([](const AisMessage &d) {
            return d.type == 13 && d.ackMmsi == QVector<quint32>({ 477553000 }) && d.ackSequence == QVector<int>({ 3 });
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("interrogacion a dos estaciones"), 15);
        AisMessage::Interrogation a;
        a.mmsi = 323000101;
        a.type1 = 5;
        a.offset1 = 300;
        a.type2 = 24;
        a.offset2 = 600;
        AisMessage::Interrogation b;
        b.mmsi = 351000104;
        b.type1 = 19;
        b.offset1 = 900;
        m.interrogations = { a, b };
        comprobacion([](const AisMessage &d) {
            return d.type == 15 && d.interrogations.size() == 2 && d.interrogations.at(0).mmsi == 323000101
                   && d.interrogations.at(0).type1 == 5 && d.interrogations.at(0).type2 == 24
                   && d.interrogations.at(0).offset2 == 600 && d.interrogations.at(1).mmsi == 351000104
                   && d.interrogations.at(1).type1 == 19 && d.interrogations.at(1).type2 == -1;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("asignacion de modo a dos estaciones"), 16);
        AisMessage::Assignment a;
        a.mmsi = 323000101;
        a.offset = 100;
        a.increment = 5;
        AisMessage::Assignment b;
        b.mmsi = 351000104;
        b.offset = 200;
        b.increment = 10;
        m.assignments = { a, b };
        comprobacion([](const AisMessage &d) {
            return d.type == 16 && d.assignments.size() == 2 && d.assignments.at(0).mmsi == 323000101
                   && d.assignments.at(0).offset == 100 && d.assignments.at(1).increment == 10;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("correcciones DGNSS de una estacion de referencia"), 17);
        m.dgnssLat = 23.10;
        m.dgnssLon = -82.40;
        m.binaryData = QByteArray::fromHex("aabbcc");
        m.binaryBits = 24;
        comprobacion([](const AisMessage &d) {
            // La posición es la de la estación de referencia: no cuenta como la del emisor.
            return d.type == 17 && !d.hasPosition() && cerca(d.dgnssLat, 23.10) && cerca(d.dgnssLon, -82.40)
                   && d.binaryBits == 24 && d.binaryData.toHex() == "aabbcc";
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("posicion de clase B ampliada, con nombre y tipo"), 19);
        m.classB = true;
        m.lat = 23.28;
        m.lon = -82.62;
        m.sogKn = 5.5;
        m.cogDeg = 120.0;
        m.headingDeg = 120;
        m.second = 40;
        m.name = QStringLiteral("YATE LUNA");
        m.shipType = 37;
        m.toBowM = 10;
        m.toSternM = 4;
        m.toPortM = 2;
        m.toStarboardM = 2;
        m.epfsType = 1;
        comprobacion([](const AisMessage &d) {
            return d.type == 19 && d.name == QLatin1String("YATE LUNA") && d.shipType == 37 && d.lengthM() == 14
                   && cerca(d.lat, 23.28) && d.epfsType == 1;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("gestion del enlace de datos: dos reservas"), 20);
        AisMessage::Reservation a;
        a.offset = 120;
        a.slotCount = 2;
        a.timeout = 5;
        a.increment = 17;
        AisMessage::Reservation b;
        b.offset = 300;
        b.slotCount = 1;
        b.timeout = 3;
        b.increment = 9;
        m.reservations = { a, b };
        comprobacion([](const AisMessage &d) {
            return d.type == 20 && d.reservations.size() == 2 && d.reservations.at(0).offset == 120
                   && d.reservations.at(0).slotCount == 2 && d.reservations.at(0).timeout == 5
                   && d.reservations.at(0).increment == 17 && d.reservations.at(1).increment == 9;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("ayuda virtual con nombre largo (20 caracteres mas la extension)"), 21);
        m.atonType = 19;
        m.name = QStringLiteral("BOYA VIRTUAL CANAL NORTE");
        m.lat = 23.1800;
        m.lon = -82.3300;
        m.accuracy = true;
        m.epfsType = 1;
        m.second = 30;
        m.virtualAton = true;
        comprobacion([](const AisMessage &d) {
            return d.type == 21 && d.name == QLatin1String("BOYA VIRTUAL CANAL NORTE") && d.atonType == 19
                   && d.virtualAton && !d.offPosition && cerca(d.lat, 23.18);
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("gestion de canales para un area"), 22);
        m.channelA = 2087;
        m.channelB = 2088;
        m.txRxMode = 1;
        m.neLat = 23.5;
        m.neLon = -82.0;
        m.swLat = 22.5;
        m.swLon = -83.0;
        m.zoneSize = 3;
        comprobacion([](const AisMessage &d) {
            return d.type == 22 && !d.addressed && d.channelA == 2087 && d.channelB == 2088 && d.txRxMode == 1
                   && cerca(d.neLat, 23.5) && cerca(d.swLon, -83.0) && d.zoneSize == 3;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("gestion de canales direccionada a dos estaciones"), 22);
        m.channelA = 2087;
        m.channelB = 2088;
        m.addressed = true;
        m.destMmsi = 323000101;
        m.destMmsi2 = 351000104;
        m.txRxMode = 2;
        m.lowPower = true;
        m.zoneSize = 2;
        comprobacion([](const AisMessage &d) {
            return d.type == 22 && d.addressed && d.destMmsi == 323000101 && d.destMmsi2 == 351000104
                   && d.txRxMode == 2 && d.lowPower && d.zoneSize == 2;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("asignacion a un grupo de estaciones"), 23);
        m.neLat = 23.5;
        m.neLon = -82.0;
        m.swLat = 22.5;
        m.swLon = -83.0;
        m.stationType = 0;
        m.shipType = 70;
        m.txRxMode = 2;
        m.reportInterval = 6;
        m.quietTime = 3;
        comprobacion([](const AisMessage &d) {
            return d.type == 23 && cerca(d.neLat, 23.5) && cerca(d.swLon, -83.0) && d.shipType == 70
                   && d.txRxMode == 2 && d.reportInterval == 6 && d.quietTime == 3;
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("binario de un intervalo, direccionado y estructurado"), 25);
        m.addressed = true;
        m.structured = true;
        m.destMmsi = 323000101;
        m.dac = 200;
        m.fid = 10;
        m.binaryData = QByteArray::fromHex("112233");
        m.binaryBits = 24;
        comprobacion([](const AisMessage &d) {
            return d.type == 25 && d.addressed && d.structured && d.destMmsi == 323000101 && d.dac == 200
                   && d.fid == 10 && d.binaryData.toHex() == "112233";
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("binario de un intervalo, difundido y sin estructura"), 25);
        m.binaryData = QByteArray::fromHex("ffeeddcc");
        m.binaryBits = 32;
        comprobacion([](const AisMessage &d) {
            return d.type == 25 && !d.addressed && !d.structured && d.dac == -1 && d.binaryBits == 32
                   && d.binaryData.toHex() == "ffeeddcc";
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("binario de varios intervalos, estructurado"), 26);
        m.structured = true;
        m.dac = 1;
        m.fid = 31;
        m.binaryData = QByteArray::fromHex("090807");
        m.binaryBits = 24;
        comprobacion([](const AisMessage &d) {
            return d.type == 26 && d.structured && d.dac == 1 && d.fid == 31 && d.binaryBits == 24
                   && d.binaryData.toHex() == "090807";
        });
    }
    {
        AisMessage &m = nuevo(QStringLiteral("posicion de largo alcance (por satelite)"), 27);
        m.lat = 23.12;
        m.lon = -82.45;
        m.sogKn = 14.0;                             // nudos y grados enteros
        m.cogDeg = 271.0;
        m.navStatus = 0;
        m.accuracy = true;
        m.gnss = true;
        comprobacion([](const AisMessage &d) {
            return d.type == 27 && cerca(d.lat, 23.12) && cerca(d.lon, -82.45) && d.sogKn == 14.0
                   && d.cogDeg == 271.0 && d.navStatus == 0 && d.gnss;
        });
    }
    return v;
}

// Quién emite qué y cuándo: el escenario, su reloj simulado, y las frases que salen en cada segundo.
class Simulador
{
public:
    explicit Simulador(const QDateTime &inicio) : m_barcos(escenario()), m_fijas(escenarioFijas()), m_catalogo(catalogoAis()), m_hora(inicio) {}

    const QVector<Barco> &barcos() const { return m_barcos; }
    const QVector<Fija> &fijas() const { return m_fijas; }
    const QVector<EjemploAis> &catalogo() const { return m_catalogo; }
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
        for (const Fija &f : m_fijas)
            emitirFija(f, &frases);
        emitirCatalogo(&frases);
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

    // El informe de una estación fija. Una ayuda manda su tipo 21 (nombre, tipo de ayuda y posición) cada
    // kPeriodoAyudaSeg; la estación base manda su fecha y hora UTC (tipo 4) cada 10 s y un aviso de seguridad
    // (tipo 14, que ocupa dos frases) cada minuto.
    void emitirFija(const Fija &f, QList<QByteArray> *frases)
    {
        const int t = m_t + f.primeraEmision;
        AisMessage m;
        m.mmsi = f.mmsi;
        m.lat = f.lat;
        m.lon = f.lon;
        m.accuracy = true;
        m.epfsType = 1;                                 // GPS
        if (f.tipoMensaje == 21) {
            if (t % kPeriodoAyudaSeg != 0)
                return;
            m.type = 21;
            m.name = f.nombre;
            m.atonType = f.tipoAyuda;
            m.second = m_hora.time().second();
        } else {
            if (t % 60 == 0) {
                AisMessage aviso;
                aviso.type = 14;
                aviso.mmsi = f.mmsi;
                aviso.text = QString::fromLatin1(kAvisoSeguridad);
                for (const QByteArray &fr : encodeAis(aviso, 'A', m_t % 10))
                    frases->append(fr);
            }
            if (t % 10 != 0)
                return;
            m.type = 4;
            m.utcYear = m_hora.date().year();
            m.utcMonth = m_hora.date().month();
            m.utcDay = m_hora.date().day();
            m.utcHour = m_hora.time().hour();
            m.utcMinute = m_hora.time().minute();
            m.utcSecond = m_hora.time().second();
        }
        for (const QByteArray &fr : encodeAis(m, 'A', m_t % 10))
            frases->append(fr);
    }

    // El catálogo de tipos AIS: un ejemplo por segundo, cada minuto (el ejemplo i sale cuando el segundo del minuto es
    // i + 1, así nunca coincide con el aviso de la estación base, que sale en el segundo 0). Va por el canal B para que
    // sus identificadores de secuencia no se mezclen con los tipos 5 de los barcos de clase A. La respuesta de hora UTC
    // (tipo 11) lleva la hora del reloj simulado.
    void emitirCatalogo(QList<QByteArray> *frases)
    {
        const int i = m_t % 60 - 1;
        if (i < 0 || i >= m_catalogo.size())
            return;
        AisMessage m = m_catalogo.at(i).mensaje;
        if (m.type == 11) {
            m.utcYear = m_hora.date().year();
            m.utcMonth = m_hora.date().month();
            m.utcDay = m_hora.date().day();
            m.utcHour = m_hora.time().hour();
            m.utcMinute = m_hora.time().minute();
            m.utcSecond = m_hora.time().second();
        }
        for (const QByteArray &fr : encodeAis(m, 'B', m_t % 10))
            frases->append(fr);
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
    QVector<Fija> m_fijas;
    QVector<EjemploAis> m_catalogo;
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
    // Modo servidor TCP: las frases van a los clientes conectados, un flujo de líneas (como un receptor de red).
    QTcpServer servidor;
    QList<QTcpSocket *> clientes;

    // Se pone a escuchar como servidor TCP en \a puerto (0 = uno libre) y acepta a quien se conecte. Falso si no puede.
    bool servirTcp(quint16 puerto)
    {
        if (!servidor.listen(QHostAddress(QHostAddress::AnyIPv4), puerto))
            return false;
        QObject::connect(&servidor, &QTcpServer::newConnection, &servidor, [this] {
            while (QTcpSocket *c = servidor.nextPendingConnection()) {
                clientes.append(c);
                QObject::connect(c, &QTcpSocket::disconnected, c, [this, c] {
                    clientes.removeAll(c);
                    c->deleteLater();
                });
            }
        });
        return true;
    }

    // Entrega las frases de un segundo.
    void enviar(const QList<QByteArray> &frases)
    {
        lineas += quint64(frases.size());
        if (servidor.isListening()) {
            QByteArray todo;
            for (const QByteArray &f : frases)
                todo += f + "\r\n";
            for (QTcpSocket *c : clientes)
                c->write(todo);
            return;
        }
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

    const QDateTime inicio = utcDateTime(QDate(2026, 10, 19), QTime(12, 0, 0));
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
    QMap<quint32, AisMessage> ultimaAyuda;              // tipo 21, por MMSI
    QMap<quint32, int> informesAyuda;
    AisMessage ultimaBase;                              // tipo 4
    int informesBase = 0;
    QStringList avisos;                                 // tipo 14
    QMap<quint32, AisMessage> delCatalogo;              // el último de cada ejemplo del catálogo, por MMSI
    QMap<quint32, int> informesCatalogo;
    QSet<int> tiposVistos;                              // todos los tipos AIS que llegaron
    Rmc ultimoRmc;
    for (const QByteArray &f : captura) {
        for (const Event &e : d.feedLine(f)) {
            switch (e.kind) {
            case Event::Kind::Rmc: ++rmc; ultimoRmc = e.rmc; break;
            case Event::Kind::Gga: ++gga; break;
            case Event::Kind::Vtg: ++vtg; break;
            case Event::Kind::Hdt: ++hdt; break;
            case Event::Kind::Ais:
                tiposVistos.insert(e.ais.type);
                if (e.ownShip) {
                    ++propios;
                } else if (e.ais.mmsi >= kMmsiCatalogo && e.ais.mmsi < kMmsiCatalogo + 100) {
                    delCatalogo[e.ais.mmsi] = e.ais;
                    ++informesCatalogo[e.ais.mmsi];
                } else if (e.ais.type == 21) {
                    ultimaAyuda[e.ais.mmsi] = e.ais;
                    ++informesAyuda[e.ais.mmsi];
                } else if (e.ais.type == 4) {
                    ultimaBase = e.ais;
                    ++informesBase;
                } else if (e.ais.type == 14) {
                    avisos << e.ais.text;
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
    // Las estaciones fijas (tipos 21, 4 y 14) llegan aparte de los barcos: no cuentan como contactos con rumbo.
    bool ayudasOk = true;
    int nAyudas = 0;
    for (const Fija &f : sim.fijas()) {
        if (f.tipoMensaje != 21)
            continue;
        ++nAyudas;
        const AisMessage &a = ultimaAyuda.value(f.mmsi);
        ayudasOk = ayudasOk && a.name == f.nombre && a.atonType == f.tipoAyuda && informesAyuda.value(f.mmsi) == 4
                   && std::abs(a.lat - f.lat) < 2e-6 && std::abs(a.lon - f.lon) < 2e-6;
    }
    verificar(QStringLiteral("las 2 ayudas a la navegacion (tipo 21) llegan con nombre, tipo de ayuda y posicion, 4 informes cada una"),
              nAyudas == 2 && ayudasOk && ultimaAyuda.size() == 2);
    verificar(QStringLiteral("la estacion base manda 12 fechas y horas UTC (tipo 4) y la ultima es la del reloj simulado"),
              informesBase == 12 && ultimaBase.utcYear == 2026 && ultimaBase.utcMonth == 10 && ultimaBase.utcDay == 19
              && ultimaBase.utcHour == 12 && ultimaBase.utcMinute == 2 && ultimaBase.utcSecond == 0);
    verificar(QStringLiteral("y dos avisos de seguridad (tipo 14, en dos frases) con el texto entero"),
              avisos.size() == 2 && avisos.at(0) == QString::fromLatin1(kAvisoSeguridad) && avisos.at(1) == avisos.at(0));
    // El catálogo: cada ejemplo sale dos veces en 120 s y, decodificado, conserva lo que lleva.
    const QVector<EjemploAis> &catalogo = sim.catalogo();
    QStringList fallosCatalogo;
    for (const EjemploAis &ej : catalogo) {
        const quint32 mmsi = ej.mensaje.mmsi;
        if (informesCatalogo.value(mmsi) != 2 || !ej.correcto(delCatalogo.value(mmsi)))
            fallosCatalogo << ej.etiqueta;
    }
    verificar(QStringLiteral("los %1 ejemplos del catalogo llegan dos veces cada uno y conservan sus campos%2")
                  .arg(catalogo.size()).arg(fallosCatalogo.isEmpty() ? QString() : QStringLiteral(" (fallan: ") + fallosCatalogo.join(QStringLiteral("; ")) + QLatin1Char(')')),
              fallosCatalogo.isEmpty() && delCatalogo.size() == catalogo.size());
    // Con los barcos y las estaciones fijas, salen los 27 tipos de M.1371 (y ninguno más).
    QStringList faltan;
    for (int tipo = 1; tipo <= 27; ++tipo)
        if (!tiposVistos.contains(tipo))
            faltan << QString::number(tipo);
    verificar(QStringLiteral("salen los 27 tipos AIS de M.1371%1")
                  .arg(faltan.isEmpty() ? QString() : QStringLiteral(" (faltan: ") + faltan.join(QLatin1Char(',')) + QLatin1Char(')')),
              faltan.isEmpty() && tiposVistos.size() == 27);
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
    const bool escucha = receptor.bind(QHostAddress(QHostAddress::LocalHost), quint16(0));   // explícito: con Qt 5, (enum, 0) es ambiguo con bind(quint16, BindMode)
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

    // El servidor TCP: un cliente que se conecta recibe las frases como un flujo de líneas (las que se emiten, en orden).
    {
        Salida tcp;
        const bool sirve = tcp.servirTcp(0);
        verificar(QStringLiteral("el emisor puede hacer de servidor TCP en un puerto libre"), sirve);
        if (sirve) {
            QTcpSocket cliente;
            cliente.connectToHost(QHostAddress::LocalHost, tcp.servidor.serverPort());
            QElapsedTimer reloj;
            reloj.start();
            while (tcp.clientes.isEmpty() && reloj.elapsed() < 3000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            verificar(QStringLiteral("acepta la conexión de un cliente"), tcp.clientes.size() == 1);
            tcp.enviar({ captura.at(0), captura.at(1) });
            tcp.enviar({ captura.at(2) });
            QByteArray recibido;
            const QByteArray esperado = captura.at(0) + "\r\n" + captura.at(1) + "\r\n" + captura.at(2) + "\r\n";
            reloj.restart();
            while (recibido.size() < esperado.size() && reloj.elapsed() < 3000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                recibido += cliente.readAll();
            }
            verificar(QStringLiteral("el cliente recibe las tres frases, en orden y con su salto de línea"), recibido == esperado);
            cliente.disconnectFromHost();
            reloj.restart();
            while (!tcp.clientes.isEmpty() && reloj.elapsed() < 3000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            verificar(QStringLiteral("al desconectarse el cliente, el servidor lo olvida y sigue emitiendo sin error"),
                      tcp.clientes.isEmpty() && (tcp.enviar({ captura.at(0) }), true));
        }
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
    const QString tcp = opcion(args, QStringLiteral("tcp"));
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

    if (!tcp.isEmpty() && !salida.servirTcp(quint16(tcp.toUInt()))) {
        err << "No se puede escuchar el puerto TCP " << tcp << ": " << salida.servidor.errorString() << "\n";
        return 2;
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
    if (!tcp.isEmpty())
        QTextStream(stdout) << "Sirviendo NMEA/AIS por TCP en el puerto " << salida.servidor.serverPort()
                            << " (escala x" << escala << "). Ctrl+C para parar.\n";
    else
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
