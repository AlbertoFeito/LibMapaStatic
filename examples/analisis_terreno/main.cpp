// analisis_terreno - ¿se ven dos puntos? ¿qué se ve desde uno? Análisis de terreno con libmapa.
//
// Noveno ejemplo de la serie. Es la base de cualquier app que razona sobre el relieve: un
// radioenlace o una antena, un puesto de vigilancia, una torre de observación, una ruta que
// quiere saber qué cota va a recorrer. La librería lee el modelo de elevación (DEM) y hace
// las cuentas de visibilidad con la curvatura de la Tierra; lo que enseña este ejemplo es
// cómo ponerlo en manos de una persona:
//   - Dos puntos ARRASTRABLES (A, el observador, y B, el objetivo) con la altura de su antena:
//     al moverlos se recalcula, en vivo, si hay LÍNEA DE VISTA (lineOfSight) y con qué holgura.
//   - El PERFIL de elevación entre A y B (elevationProfile) dibujado con la recta de la visual y
//     el punto que bloquea, con la curvatura incluida: se ve POR QUÉ no se ve.
//   - «¿Qué altura necesita la antena de B?» — una bisección sobre lineOfSight.
//   - La ZONA VISIBLE desde A (viewshed 360°) dibujada sobre el mapa, con su superficie y su
//     alcance mayor y menor, y una barra de progreso que se puede cancelar.
//   - Los ÁNGULOS DE CIERRE: a lo largo del perfil A–B (lo más alto que tapa el cielo hasta cada distancia,
//     y si B queda por encima) y en los 360° hasta el alcance (ViewshedRay::horizonDeg).
//   - Los 10 PICOS más altos en un radio alrededor de A, con distancia, azimut, ángulo y si se ven.
//   - Las coordenadas de A y B se pueden ESCRIBIR (parseCoordinate: decimal, sexagesimal, UTM, MGRS).
//   - «Mar = 0»: el análisis usa 0 m donde no hay cota (voidElevation = 0), con la curvatura de la Tierra.
//   - El relieve sombreado (setHillshadeVisible) y la cota bajo el cursor (elevationAt).
//   - Sin elevación en el paquete, la app lo dice y ofrece un TERRENO DE DEMOSTRACIÓN.
//   - Un GLOSARIO en la interfaz con los términos que usa la ventana.
//
//   analisis_terreno [paquete de datos]   (por defecto: <exe>/datos)
//   analisis_terreno <paquete> --comprobar
//        -> sin ventana: crea un terreno sintético con una loma de altura conocida y comprueba la
//           línea de vista (bloqueada, y clara con otra antena), el perfil, la altura mínima de
//           antena, la zona visible, un punto fuera de los datos y que el glosario define lo que se usa.
//   analisis_terreno <paquete> --captura=imagen.png
//        -> abre la ventana con el terreno de demostración, calcula la zona visible, guarda una imagen y sale.
//
// La elevación es la del paquete (su «elevation» del mapa.json) o, en la demostración, un `.hgt`
// sintético en una carpeta temporal.

#include <libmapa/Coordinates.h>
#include <libmapa/Elevation.h>
#include <libmapa/MapWidget.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QScrollArea>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace libmapa;

namespace {

// Capa de los dos puntos de análisis y capa donde se dibuja la zona visible. Ambas temporales:
// no se guardan en la base de entidades del paquete.
const QString kCapaPuntos = QStringLiteral("puntos");
const QString kCapaZona = QStringLiteral("visibilidad");
const QString kCapaPicos = QStringLiteral("picos");
const int kOrdenCapa = 1000;
// Refracción estándar (radio efectivo 4/3 del de la Tierra) y radio medio, como en la librería.
const double kK = 4.0 / 3.0;
const double kRadioTierra = 6371000.0;
// Centro del terreno de demostración: la cresta de la loma sintética.
const QGeoCoordinate kCentroDemo(23.50, -82.50);

// El resultado del análisis A–B: la visual, el perfil y las cotas de los extremos.
struct Analisis {
    LineOfSightResult los;
    ElevationProfile perfil;
    double cotaA = std::numeric_limits<double>::quiet_NaN();
    double cotaB = std::numeric_limits<double>::quiet_NaN();
    double distanciaM = 0.0;
    bool hayElevacion = false;
};

// Lo que dice la zona visible desde A: cuánto del círculo se ve, cuánto se esconde y cuánto no tiene dato,
// lo más lejos que se ve y dónde está la zona ciega más cercana (el terreno que se esconde primero, p. ej.
// la ladera bajo el borde de una cumbre).
struct ZonaVisible {
    bool valida = false;
    int rayos = 0;
    int tramos = 0;               // tramos visibles, sumados todos los rayos
    int tramosOcultos = 0;
    double areaKm2 = 0.0;         // suma de lo que se ve, tramo a tramo
    double porcentaje = 0.0;      // de la superficie del círculo de alcance
    double porcentajeOculto = 0.0;
    double porcentajeSinDato = 0.0;   // lo que ni se ve ni se sabe que se esconde (sin cota)
    double alcanceMaxM = 0.0;     // el punto visible más lejano
    double azimutMaxDeg = 0.0;
    double zonaCiegaM = -1.0;     // distancia a la que antes empieza a no verse algo (-1: no hay zona ciega)
    double azimutCiegaDeg = 0.0;
    // Ángulo de cierre en los 360°: por azimut, la elevación (grados) del horizonte hasta el alcance.
    QVector<QPointF> cierres;     // (azimut, ángulo de cierre)
    double cierreMaxDeg = 0.0;    // el horizonte más alto y dónde está
    double cierreMaxAzimutDeg = 0.0;
    double cierreMinDeg = 0.0;    // el más bajo (la dirección más despejada)
    double cierreMinAzimutDeg = 0.0;
};

// Un pico cerca de A: su cota, dónde está respecto a A y con qué ángulo se ve desde A.
struct Pico {
    QGeoCoordinate posicion;
    double cota = 0.0;
    double distanciaM = 0.0;
    double azimutDeg = 0.0;
    double anguloDeg = 0.0;       // elevación del pico visto desde el ojo de A (con la curvatura)
    bool visible = false;         // ¿hay línea de vista de A a la cima?
};

// El ángulo de cierre a lo largo del perfil A–B, visto desde el ojo de A: por cada muestra, la elevación
// del terreno (grados) y el máximo acumulado hasta ahí (el ángulo de cierre), más el ángulo con que se ve B.
struct AngulosPerfil {
    bool valido = false;
    QVector<QPointF> terreno;     // (distancia m, ángulo del terreno)
    QVector<QPointF> cierre;      // (distancia m, ángulo de cierre acumulado)
    double anguloB = 0.0;         // elevación del ojo de B visto desde A
    double cierreAntesB = 0.0;    // el ángulo de cierre justo antes de B
};

// Una entrada del glosario: el término y su definición.
struct Termino {
    QString nombre;
    QString definicion;
};

} // namespace

// El glosario de la interfaz: los términos que usa la ventana. --comprobar verifica que cada uno
// de los que salen en pantalla (terminosEnPantalla) tiene aquí su definición.
static QVector<Termino> glosario()
{
    return {
        { QObject::tr("Observador (A)"),
          QObject::tr("El punto desde el que se mira: una torre, una antena, un puesto. Se arrastra por el "
                      "mapa; la altura de su antena se pone aparte.") },
        { QObject::tr("Objetivo (B)"),
          QObject::tr("El punto que se quiere ver, o con el que se quiere enlazar. También se arrastra.") },
        { QObject::tr("Altura de antena"),
          QObject::tr("Metros sobre el terreno a los que está el ojo, la antena o el objetivo. Una antena más alta "
                      "ve por encima de más relieve.") },
        { QObject::tr("Cota"),
          QObject::tr("Altura del terreno sobre el nivel del mar, en metros, en un lugar. Sale del modelo de "
                      "elevación.") },
        { QObject::tr("Modelo de elevación (DEM)"),
          QObject::tr("Un mapa de cotas: la altura del terreno en una malla de puntos. Sin él no se puede "
                      "calcular ninguna visibilidad.") },
        { QObject::tr("Línea de vista"),
          QObject::tr("Si desde A se ve B en línea recta, sin que el terreno se interponga. Cuenta las alturas de "
                      "las dos antenas y la curvatura de la Tierra.") },
        { QObject::tr("Holgura"),
          QObject::tr("Cuánto por encima del terreno pasa la visual en su peor punto, en metros. Positiva: hay "
                      "margen. Negativa: es lo que le falta a la visual para pasar.") },
        { QObject::tr("Perfil de elevación"),
          QObject::tr("El corte del terreno a lo largo de la recta de A a B: la cota en cada lugar del trayecto. "
                      "El dibujo incluye la curvatura, así que la visual se ve recta.") },
        { QObject::tr("Ángulo de cierre"),
          QObject::tr("Visto desde A, el ángulo de elevación (en grados sobre el horizonte) de lo más alto que "
                      "tapa el cielo hasta una distancia: el máximo, a lo largo del trayecto, de la elevación del "
                      "terreno. Un objetivo se ve si queda por encima de ese ángulo. En el perfil se dibuja hacia "
                      "B; en los 360° se dibuja por azimut, hasta el alcance.") },
        { QObject::tr("Pico"),
          QObject::tr("Una cumbre del terreno: una cota más alta que todas las de su entorno inmediato. La lista "
                      "da los diez más altos dentro del radio elegido alrededor de A, separados entre sí al menos "
                      "una décima del radio (si no, saldrían diez puntos de la misma cresta).") },
        { QObject::tr("Nivel del mar (mar = 0)"),
          QObject::tr("Con esta opción, donde no hay cota (el mar) o la cota es negativa (el fondo marino) el "
                      "análisis usa 0 m: el agua es una superficie lisa a nivel del mar y no tapa ni esconde nada "
                      "en la costa. Sin ella, esos lugares quedan «sin dato».") },
        { QObject::tr("Curvatura y refracción"),
          QObject::tr("La Tierra se abomba entre A y B y la atmósfera curva un poco los rayos hacia abajo. Se "
                      "calcula con un radio efectivo de 4/3 del de la Tierra (k = 4/3), que es la refracción "
                      "estándar.") },
        { QObject::tr("Zona visible"),
          QObject::tr("Lo que se ve desde A de un objetivo a la altura elegida, tramo a tramo en cada dirección "
                      "(viewshed de 360 rayos). Azul = visible, amarillo = oculto; lo que no tiene dato queda sin "
                      "pintar. Un marcador señala el punto visible más lejano.") },
        { QObject::tr("Zona ciega"),
          QObject::tr("Terreno cercano que no se ve desde A porque queda por debajo del horizonte del observador: "
                      "por ejemplo, la ladera justo debajo del borde de una cumbre redondeada. Más lejos puede "
                      "volver a verse (las tierras bajas).") },
        { QObject::tr("Alcance"),
          QObject::tr("La distancia máxima que se analiza desde A en cada dirección. Un alcance mayor tarda más.") },
        { QObject::tr("Azimut"),
          QObject::tr("La dirección en grados verdaderos: 0° norte, 90° este, 180° sur, 270° oeste.") },
        { QObject::tr("Relieve sombreado"),
          QObject::tr("Pintar el terreno como si le diera el sol desde una dirección, para ver las lomas y los "
                      "valles. Se calcula en vivo del modelo de elevación.") },
        { QObject::tr("Terreno de demostración"),
          QObject::tr("Un relieve inventado (una loma de 300 m, un pico redondeado de 900 m y un cerro, en una "
                      "llanura suave) para probar la aplicación sin datos reales.") },
    };
}

// Los términos que la ventana muestra tal cual (títulos de grupos, etiquetas y botones): cada uno
// debe estar definido en el glosario.
static QStringList terminosEnPantalla()
{
    return {
        QObject::tr("Observador (A)"), QObject::tr("Objetivo (B)"), QObject::tr("Altura de antena"),
        QObject::tr("Cota"), QObject::tr("Modelo de elevación (DEM)"), QObject::tr("Línea de vista"),
        QObject::tr("Ángulo de cierre"), QObject::tr("Pico"), QObject::tr("Nivel del mar (mar = 0)"),
        QObject::tr("Holgura"), QObject::tr("Perfil de elevación"), QObject::tr("Curvatura y refracción"),
        QObject::tr("Zona visible"), QObject::tr("Zona ciega"), QObject::tr("Alcance"), QObject::tr("Azimut"),
        QObject::tr("Relieve sombreado"), QObject::tr("Terreno de demostración"),
    };
}

// Escribe en \a dir el tile `N23W083.hgt` del terreno de demostración (SRTM3, 1201 x 1201 enteros
// de 16 bits, big-endian, el norte arriba): una llanura que sube 20 m por grado hacia el norte, una
// loma alargada de 300 m de norte a sur en lon -82.5 y un cerro de 120 m al suroeste. Devuelve false
// si no se puede escribir.
static bool crearTerrenoDemostracion(const QString &dir)
{
    const int lado = 1201;
    QByteArray bytes;
    bytes.resize(lado * lado * 2);
    char *p = bytes.data();
    for (int fila = 0; fila < lado; ++fila) {
        const double lat = 24.0 - fila / 1200.0;
        for (int col = 0; col < lado; ++col) {
            const double lon = -83.0 + col / 1200.0;
            const double loma = 300.0 * std::exp(-0.5 * std::pow((lon + 82.5) / 0.02, 2.0));
            const double cerro = 120.0 * std::exp(-0.5 * (std::pow((lon + 82.75) / 0.03, 2.0)
                                                          + std::pow((lat - 23.3) / 0.03, 2.0)));
            const double pico = 880.0 * std::exp(-0.5 * (std::pow((lon + 82.25) / 0.02, 2.0)
                                                         + std::pow((lat - 23.75) / 0.02, 2.0)));
            const double cota = 10.0 + 20.0 * (lat - 23.0) + loma + cerro + pico;
            qToBigEndian<qint16>(qint16(std::lround(cota)), p + (qint64(fila) * lado + col) * 2);
        }
    }
    QFile f(dir + QStringLiteral("/N23W083.hgt"));
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// Calcula el ángulo de cierre a lo largo del perfil, visto desde el ojo de A: la elevación de cada muestra
// del terreno sobre el horizontal de A, bajando la curvatura d²/(2·k·R) (la misma convención que la visual),
// y el máximo acumulado. El ángulo de cierre justo antes de B se compara con el ángulo del ojo de B: si este
// es mayor, B se ve; es la misma pregunta que lineOfSight, vista como un ángulo en vez de como una altura.
static AngulosPerfil angulosPerfil(const Analisis &a, double alturaA, double alturaB)
{
    AngulosPerfil r;
    if (!a.perfil.isValid() || !std::isfinite(a.cotaA) || !std::isfinite(a.cotaB))
        return r;
    const double D = std::max(a.perfil.totalDistanceM, 1.0);
    const double ojoA = a.cotaA + alturaA;
    const auto angulo = [&](double d, double z) {
        return std::atan2(z - ojoA - d * d / (2.0 * kK * kRadioTierra), d) * 180.0 / M_PI;
    };
    double maximo = -90.0;
    for (const ElevationSample &s : a.perfil.samples) {
        if (s.distanceM <= 0.0 || s.distanceM >= D - 1.0 || !std::isfinite(s.elevation))
            continue;                                   // ni A mismo, ni B, ni los huecos sin cota
        const double ang = angulo(s.distanceM, s.elevation);
        maximo = std::max(maximo, ang);
        r.terreno.append(QPointF(s.distanceM, ang));
        r.cierre.append(QPointF(s.distanceM, maximo));
    }
    r.cierreAntesB = maximo;
    r.anguloB = angulo(D, a.cotaB + alturaB);
    r.valido = !r.terreno.isEmpty();
    return r;
}

// Elige el rango vertical (grados) de la gráfica de ángulos. Sin zoom, abarca el cierre y B (sin contar el primer
// 3 % del trayecto: pegado a A el terreno cae a decenas de grados y aplastaría todo). Con zoom automático se centra
// en lo que decide si B se ve, la diferencia entre el ángulo de B y el cierre justo antes de B: la ventana mide
// ±4 veces esa diferencia (mínimo ±0,5°) alrededor de los dos valores, de modo que 0,15° de margen se ve
// como una separación clara en vez de una línea pegada. Lo que cae fuera (la subida inicial del cierre) se recorta.
static void ejeAngulos(const AngulosPerfil &ang, double D, bool zoom, double *aMin, double *aMax)
{
    // El rango completo: el cierre y B, y el terreno salvo el primer 3 % del trayecto.
    *aMin = std::min(ang.anguloB, ang.cierre.last().y());
    *aMax = std::max(ang.anguloB, ang.cierre.last().y());
    for (int i = 0; i < ang.terreno.size(); ++i) {
        if (ang.terreno[i].x() < 0.03 * D)
            continue;
        *aMin = std::min({ *aMin, ang.terreno[i].y(), ang.cierre[i].y() });
        *aMax = std::max({ *aMax, ang.terreno[i].y(), ang.cierre[i].y() });
    }
    const double margen = std::max(0.3, 0.08 * (*aMax - *aMin));
    *aMin -= margen;
    *aMax += margen;
    if (!zoom)
        return;
    // La ventana del zoom. Si no es más estrecha que el rango completo (B muy por debajo del cierre, p. ej.), no
    // aporta nada y se queda el completo.
    const double hueco = std::abs(ang.anguloB - ang.cierreAntesB);
    const double h = std::max(0.5, 4.0 * hueco);
    const double zMin = std::min(ang.anguloB, ang.cierreAntesB) - h;
    const double zMax = std::max(ang.anguloB, ang.cierreAntesB) + h;
    if (zMax - zMin < *aMax - *aMin) {
        *aMin = zMin;
        *aMax = zMax;
    }
}

// Dibuja el perfil de elevación entre A y B con la recta de la visual encima, y debajo, con el mismo eje
// de distancias, el ÁNGULO DE CIERRE visto desde A. El terreno se dibuja «efectivo»: su cota más el
// abombamiento de la Tierra a esa distancia, d·(D−d)/(2·k·R), de modo que la visual es una recta y lo que
// la corta es justo lo que bloquea. El punto crítico (donde bloquea, o el de menor holgura) se marca con un
// círculo. En la gráfica de ángulos, B se ve si su ángulo queda por encima del ángulo de cierre.
class PerfilWidget : public QWidget
{
public:
    // Color del relleno del terreno (opaco, para poder contarlo en las pruebas).
    static QColor colorTerreno() { return QColor(141, 110, 99); }
    // Color del trazo del ángulo de cierre (también para las pruebas).
    static QColor colorCierre() { return QColor(13, 71, 161); }

    explicit PerfilWidget(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(300); }

    // Zoom automático de la gráfica de ángulos (por omisión sí); un doble clic encima lo alterna.
    void setZoomAngulos(bool on) { m_zoomAngulos = on; update(); }
    bool zoomAngulos() const { return m_zoomAngulos; }

    // Cambia lo que se dibuja y repinta. Sin perfil, se muestra \a mensaje (por qué falta).
    void setDatos(const Analisis &a, double alturaA, double alturaB, const QString &mensaje)
    {
        m_a = a;
        m_alturaA = alturaA;
        m_alturaB = alturaB;
        m_mensaje = mensaje;
        update();
    }

protected:
    // Un doble clic alterna el zoom automático de los ángulos.
    void mouseDoubleClickEvent(QMouseEvent *) override { setZoomAngulos(!m_zoomAngulos); }

    // Dibuja el terreno efectivo, la visual, las antenas y el punto crítico, y debajo la gráfica de ángulos.
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().base());
        const ElevationProfile &perfil = m_a.perfil;
        if (!perfil.isValid() || !std::isfinite(m_a.cotaA) || !std::isfinite(m_a.cotaB)) {
            p.setPen(palette().color(QPalette::Mid));
            p.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap,
                       m_mensaje.isEmpty() ? tr("Sin perfil: mueve A o B sobre una zona con elevación") : m_mensaje);
            return;
        }
        const double D = std::max(perfil.totalDistanceM, 1.0);
        const auto abombamiento = [D](double d) { return d * (D - d) / (2.0 * kK * kRadioTierra); };
        const double ojoA = m_a.cotaA + m_alturaA;
        const double ojoB = m_a.cotaB + m_alturaB;
        double minimo = std::min(ojoA, ojoB), maximo = std::max(ojoA, ojoB);
        for (const ElevationSample &s : perfil.samples) {
            if (!std::isfinite(s.elevation))
                continue;
            const double e = s.elevation + abombamiento(s.distanceM);
            minimo = std::min(minimo, e);
            maximo = std::max(maximo, e);
        }
        const double rango = std::max(maximo - minimo, 1.0);
        // Dos gráficas con el mismo eje de distancias: el perfil (56 % de la altura útil) y los ángulos.
        const double util = height() - 26.0 - 40.0 - 22.0;
        const QRectF zona(58.0, 26.0, width() - 72.0, util * 0.56);
        const QRectF zonaAng(58.0, zona.bottom() + 40.0, width() - 72.0, util * 0.44);
        const auto aPantalla = [&](double d, double cota) {
            return QPointF(zona.left() + zona.width() * d / D,
                           zona.bottom() - zona.height() * (cota - minimo) / rango);
        };

        QPainterPath curva;
        bool primero = true;
        for (const ElevationSample &s : perfil.samples) {
            if (!std::isfinite(s.elevation))
                continue;
            const QPointF q = aPantalla(s.distanceM, s.elevation + abombamiento(s.distanceM));
            if (primero)
                curva.moveTo(q);
            else
                curva.lineTo(q);
            primero = false;
        }
        QPainterPath area = curva;
        area.lineTo(zona.right(), zona.bottom());
        area.lineTo(zona.left(), zona.bottom());
        area.closeSubpath();
        p.fillPath(area, colorTerreno());
        p.setPen(QPen(QColor(93, 64, 55), 1.4));
        p.drawPath(curva);

        // La visual: azul si hay vista, roja si el terreno la bloquea.
        const bool hayVista = m_a.los.isValid() && m_a.los.clear;
        const QColor color = hayVista ? QColor(21, 101, 192) : QColor(198, 40, 40);
        p.setPen(QPen(color, 2.0));
        p.drawLine(aPantalla(0.0, ojoA), aPantalla(D, ojoB));
        // Las antenas: un trazo vertical del terreno al ojo, y un punto en el ojo.
        p.setPen(QPen(color, 3.0));
        p.drawLine(aPantalla(0.0, m_a.cotaA), aPantalla(0.0, ojoA));
        p.drawLine(aPantalla(D, m_a.cotaB), aPantalla(D, ojoB));
        p.setBrush(color);
        p.drawEllipse(aPantalla(0.0, ojoA), 4.0, 4.0);
        p.drawEllipse(aPantalla(D, ojoB), 4.0, 4.0);
        // El punto crítico: donde bloquea (rojo) o donde hay menos holgura (verde).
        if (m_a.los.isValid()) {
            const double d = std::clamp(m_a.los.blockDistanceM, 0.0, D);
            const double cota = std::isfinite(m_a.los.clearanceM)
                ? ojoA + (ojoB - ojoA) * d / D - m_a.los.clearanceM : ojoA;
            p.setPen(QPen(hayVista ? QColor(46, 125, 50) : QColor(198, 40, 40), 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(aPantalla(d, cota), 7.0, 7.0);
        }
        p.setPen(palette().color(QPalette::Mid));
        p.drawText(QRectF(zona.left(), 4, zona.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("Terreno más la curvatura de la Tierra (k = 4/3): las cotas del eje son «efectivas»"));
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, zona.top() - 8, 54, 14), Qt::AlignRight, tr("%1 m").arg(qRound(maximo)));
        p.drawText(QRectF(0, zona.bottom() - 8, 54, 14), Qt::AlignRight, tr("%1 m").arg(qRound(minimo)));
        p.drawText(QRectF(zona.left(), zona.bottom() + 3, 40, 14), Qt::AlignLeft, QStringLiteral("A"));
        p.drawText(QRectF(zona.right() - 40, zona.bottom() + 3, 40, 14), Qt::AlignRight, QStringLiteral("B"));

        // --- Gráfica secundaria: el ángulo de cierre visto desde A ---
        const AngulosPerfil ang = angulosPerfil(m_a, m_alturaA, m_alturaB);
        if (!ang.valido)
            return;
        double aMin = 0.0, aMax = 0.0;
        ejeAngulos(ang, D, m_zoomAngulos, &aMin, &aMax);
        const auto aPantallaAng = [&](double d, double grados) {
            return QPointF(zonaAng.left() + zonaAng.width() * d / D,
                           zonaAng.bottom() - zonaAng.height() * (grados - aMin) / (aMax - aMin));
        };
        p.setPen(palette().color(QPalette::Mid));
        p.setBrush(Qt::NoBrush);
        p.drawRect(zonaAng);
        if (aMin < 0.0 && aMax > 0.0) {                  // el horizonte matemático, el 0°
            p.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DashLine));
            p.drawLine(aPantallaAng(0.0, 0.0), aPantallaAng(D, 0.0));
        }
        QPainterPath trazoTerreno, trazoCierre;
        for (int i = 0; i < ang.terreno.size(); ++i) {
            const QPointF t = aPantallaAng(ang.terreno[i].x(), ang.terreno[i].y());
            const QPointF c = aPantallaAng(ang.cierre[i].x(), ang.cierre[i].y());
            if (i == 0) {
                trazoTerreno.moveTo(t);
                trazoCierre.moveTo(c);
            } else {
                trazoTerreno.lineTo(t);
                trazoCierre.lineTo(c);
            }
        }
        p.save();
        p.setClipRect(zonaAng);
        p.setPen(QPen(QColor(161, 136, 127), 1.0));
        p.drawPath(trazoTerreno);
        p.setPen(QPen(colorCierre(), 2.2));
        p.drawPath(trazoCierre);
        p.restore();
        // B: su ángulo desde A. Por encima del cierre = se ve (azul); por debajo = lo tapa el terreno (rojo).
        const bool porEncima = ang.anguloB >= ang.cierreAntesB;
        const QColor colorB = porEncima ? QColor(21, 101, 192) : QColor(198, 40, 40);
        p.setPen(QPen(colorB, 1.2, Qt::DashLine));
        p.drawLine(aPantallaAng(0.0, ang.anguloB), aPantallaAng(D, ang.anguloB));
        p.setPen(QPen(colorB, 2.0));
        p.setBrush(colorB);
        p.drawEllipse(aPantallaAng(D, ang.anguloB), 4.5, 4.5);
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, zonaAng.top() - 7, 54, 14), Qt::AlignRight,
                   QStringLiteral("%1°").arg(aMax, 0, 'f', 1));
        p.drawText(QRectF(0, zonaAng.bottom() - 7, 54, 14), Qt::AlignRight,
                   QStringLiteral("%1°").arg(aMin, 0, 'f', 1));
        p.drawText(QRectF(zonaAng.left(), zonaAng.bottom() + 3, 40, 14), Qt::AlignLeft, QStringLiteral("A"));
        p.drawText(QRectF(zonaAng.right() - 40, zonaAng.bottom() + 3, 40, 14), Qt::AlignRight, QStringLiteral("B"));
        p.drawText(QRectF(zonaAng.left(), zonaAng.bottom() + 3, zonaAng.width(), 14), Qt::AlignHCenter,
                   tr("%1 km").arg(D / 1000.0, 0, 'f', 2));
        p.setPen(palette().color(QPalette::Mid));
        p.drawText(QRectF(zonaAng.left(), zonaAng.top() - 20, zonaAng.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(tr("Ángulo de cierre desde A (azul) · B a %1° · cierre antes de B: %2° · %3")
                                                .arg(ang.anguloB, 0, 'f', 2).arg(ang.cierreAntesB, 0, 'f', 2)
                                                .arg(m_zoomAngulos ? tr("zoom en B (2 clics: todo)")
                                                                   : tr("todo (2 clics: zoom en B)")),
                                            Qt::ElideRight, int(zonaAng.width())));
    }

private:
    Analisis m_a;
    double m_alturaA = 0.0;
    double m_alturaB = 0.0;
    bool m_zoomAngulos = true;
    QString m_mensaje;
};

// Escribe en \a dir el tile `N23W083.hgt` de una prueba de picos: nivel del mar (0 m) en todo el tile y 12
// conos de 1,5 km de radio en la base (de punta afilada, que se ve desde lejos) de alturas 400, 450 ... 950 m, en un anillo de 6 km de radio alrededor de
// \a centro, uno cada 30° de azimut (el de 400 m al norte, subiendo en sentido horario). Devuelve false si no
// se puede escribir. Sirve para saber de antemano cuáles son los 10 picos más altos y dónde.
static bool crearTerrenoPicos(const QString &dir, const QGeoCoordinate &centro)
{
    const int lado = 1201;
    QVector<QGeoCoordinate> cerros;
    for (int k = 0; k < 12; ++k)
        cerros.append(centro.atDistanceAndAzimuth(6000.0, 30.0 * k));
    QByteArray bytes;
    bytes.resize(lado * lado * 2);
    char *p = bytes.data();
    for (int fila = 0; fila < lado; ++fila) {
        const double lat = 24.0 - fila / 1200.0;
        for (int col = 0; col < lado; ++col) {
            const double lon = -83.0 + col / 1200.0;
            double cota = 0.0;
            for (int k = 0; k < cerros.size(); ++k) {
                const double dx = (lon - cerros[k].longitude()) * 111320.0 * std::cos(lat * M_PI / 180.0);
                const double dy = (lat - cerros[k].latitude()) * 110574.0;
                cota += std::max(0.0, (400.0 + 50.0 * k) * (1.0 - std::hypot(dx, dy) / 1500.0));
            }
            qToBigEndian<qint16>(qint16(std::lround(cota)), p + (qint64(fila) * lado + col) * 2);
        }
    }
    QFile f(dir + QStringLiteral("/N23W083.hgt"));
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// Dibuja los ÁNGULOS DE CIERRE en los 360°: en el eje horizontal el azimut (0° norte … 360°) y en el vertical la
// elevación del horizonte visto desde A hasta el alcance elegido (viewshed: ViewshedRay::horizonDeg). Una
// dirección con un ángulo alto está cerrada por una montaña; con uno bajo o negativo, despejada. Una línea
// vertical marca el azimut hacia B.
class CierresWidget : public QWidget
{
public:
    // Color del relleno bajo la curva (opaco, para poder contarlo en las pruebas).
    static QColor colorRelleno() { return QColor(144, 164, 174); }

    explicit CierresWidget(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(300); }

    // Pone los ángulos de cierre (azimut, grados) hasta \a alcanceKm y el azimut hacia B (NaN: sin marca).
    // Sin datos se muestra \a mensaje.
    void setDatos(const QVector<QPointF> &cierres, double alcanceKm, double azimutB, const QString &mensaje)
    {
        m_cierres = cierres;
        m_alcanceKm = alcanceKm;
        m_azimutB = azimutB;
        m_mensaje = mensaje;
        update();
    }

    // ¿Hay ángulos que dibujar?
    bool hayDatos() const { return !m_cierres.isEmpty(); }

protected:
    // Dibuja la silueta del horizonte (relleno y trazo), el cero, la rejilla de azimuts y las marcas del
    // máximo, del mínimo y de B.
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().base());
        if (m_cierres.isEmpty()) {
            p.setPen(palette().color(QPalette::Mid));
            p.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap, m_mensaje);
            return;
        }
        double aMin = 0.0, aMax = 0.0;                   // el 0° siempre dentro del eje
        QPointF pMax = m_cierres.first(), pMin = m_cierres.first();
        for (const QPointF &q : m_cierres) {
            if (q.y() > pMax.y())
                pMax = q;
            if (q.y() < pMin.y())
                pMin = q;
            aMin = std::min(aMin, q.y());
            aMax = std::max(aMax, q.y());
        }
        const double margen = std::max(0.5, 0.1 * (aMax - aMin));
        aMin -= margen;
        aMax += margen;
        const QRectF zona = rect().adjusted(58, 26, -14, -22);
        const auto aPantalla = [&](double azimut, double grados) {
            return QPointF(zona.left() + zona.width() * azimut / 360.0,
                           zona.bottom() - zona.height() * (grados - aMin) / (aMax - aMin));
        };
        // Rejilla de azimuts cada 45°, con los puntos cardinales.
        const char *rotulos[] = { "N", "NE", "E", "SE", "S", "SO", "O", "NO", "N" };
        p.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
        for (int k = 0; k <= 8; ++k) {
            const QPointF x = aPantalla(45.0 * k, aMin);
            p.drawLine(QPointF(x.x(), zona.top()), QPointF(x.x(), zona.bottom()));
        }
        p.setPen(palette().color(QPalette::Text));
        for (int k = 0; k <= 8; ++k) {
            const double x = aPantalla(45.0 * k, aMin).x();
            p.drawText(QRectF(x - 30.0, zona.bottom() + 3.0, 60.0, 14.0), Qt::AlignHCenter,
                       QStringLiteral("%1° %2").arg(45 * k).arg(QLatin1String(rotulos[k])));
        }
        // La silueta: relleno hasta el borde de abajo y trazo.
        QPainterPath curva;
        for (int i = 0; i < m_cierres.size(); ++i) {
            const QPointF q = aPantalla(m_cierres[i].x(), m_cierres[i].y());
            if (i == 0)
                curva.moveTo(q);
            else
                curva.lineTo(q);
        }
        QPainterPath area = curva;
        area.lineTo(aPantalla(m_cierres.last().x(), aMin));
        area.lineTo(aPantalla(m_cierres.first().x(), aMin));
        area.closeSubpath();
        p.fillPath(area, colorRelleno());
        p.setPen(QPen(PerfilWidget::colorCierre(), 1.8));
        p.drawPath(curva);
        // El 0° (el horizonte matemático).
        p.setPen(QPen(palette().color(QPalette::Text), 1.0, Qt::DashLine));
        p.drawLine(aPantalla(0.0, 0.0), aPantalla(360.0, 0.0));
        // El azimut hacia B.
        if (std::isfinite(m_azimutB)) {
            p.setPen(QPen(QColor(21, 101, 192), 1.6));
            p.drawLine(aPantalla(m_azimutB, aMin), aPantalla(m_azimutB, aMax));
            p.drawText(aPantalla(m_azimutB, aMax) + QPointF(4.0, 12.0), QStringLiteral("B"));
        }
        // El máximo (la dirección más cerrada) y el mínimo (la más despejada).
        p.setPen(QPen(QColor(198, 40, 40), 2.0));
        p.setBrush(QColor(198, 40, 40));
        p.drawEllipse(aPantalla(pMax.x(), pMax.y()), 4.0, 4.0);
        p.setBrush(QColor(46, 125, 50));
        p.setPen(QPen(QColor(46, 125, 50), 2.0));
        p.drawEllipse(aPantalla(pMin.x(), pMin.y()), 4.0, 4.0);
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, zona.top() - 8, 54, 14), Qt::AlignRight, QStringLiteral("%1°").arg(aMax, 0, 'f', 1));
        p.drawText(QRectF(0, zona.bottom() - 8, 54, 14), Qt::AlignRight, QStringLiteral("%1°").arg(aMin, 0, 'f', 1));
        p.setPen(palette().color(QPalette::Mid));
        p.drawText(QRectF(zona.left(), 4, zona.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(tr("Cierre en los 360° hasta %1 km · máx. %2° (az %3°, rojo) · mín. %4° (az %5°, verde)")
                                                .arg(m_alcanceKm, 0, 'f', 0).arg(pMax.y(), 0, 'f', 1).arg(qRound(pMax.x()))
                                                .arg(pMin.y(), 0, 'f', 1).arg(qRound(pMin.x())),
                                            Qt::ElideRight, int(zona.width())));
    }

private:
    QVector<QPointF> m_cierres;
    double m_alcanceKm = 0.0;
    double m_azimutB = std::numeric_limits<double>::quiet_NaN();
    QString m_mensaje;
};

// Ventana: el mapa al centro; a la izquierda los dos puntos, la visual y la zona visible; abajo el
// perfil; a la derecha el glosario.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos, los paneles, los dos puntos y las conexiones.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        cfg.autosaveFeatures = false;
        m_mapa = new MapWidget(cfg, this);
        setCentralWidget(m_mapa);
        resize(1400, 860);
        setWindowTitle(tr("Análisis de terreno"));
        m_mapa->setScaleBarVisible(true);
        m_mapa->addFeatureLayer(kCapaZona, tr("Zona visible"), kOrdenCapa - 1);
        m_mapa->setFeatureLayerTransient(kCapaZona);
        m_mapa->addFeatureLayer(kCapaPicos, tr("Picos"), kOrdenCapa - 1);
        m_mapa->setFeatureLayerTransient(kCapaPicos);
        m_mapa->addFeatureLayer(kCapaPuntos, tr("Puntos"), kOrdenCapa);
        m_mapa->setFeatureLayerTransient(kCapaPuntos);

        crearPaneles();
        crearMenus();
        conectarSenales();
        crearPuntos(m_mapa->center());
        mostrarCoordenadas();
        borrarPicos();
        actualizarOrigenElevacion();
        analizar();

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
    }

    MapWidget *mapa() const { return m_mapa; }
    PerfilWidget *perfil() const { return m_perfil; }

    // ¿Hay un modelo de elevación utilizable (el del paquete o el de demostración)?
    bool tieneElevacion() const { return !m_mapa->elevationDescription().isEmpty(); }

    // Usa el terreno de demostración como origen de elevación: lo escribe en una carpeta temporal,
    // centra el mapa en su cresta y pone A y B a 12 km a cada lado de ella. false si no se puede escribir.
    bool usarTerrenoDemostracion()
    {
        if (!m_demoDir.isValid() || !crearTerrenoDemostracion(m_demoDir.path())) {
            statusBar()->showMessage(tr("No se pudo crear el terreno de demostración."), 6000);
            return false;
        }
        m_mapa->setElevationDir(m_demoDir.path());
        m_mapa->setCenter(kCentroDemo);
        m_mapa->setZoom(10);
        borrarZonaVisible();
        fijarPunto(true, kCentroDemo.atDistanceAndAzimuth(12000.0, 270.0));
        fijarPunto(false, kCentroDemo.atDistanceAndAzimuth(12000.0, 90.0));
        actualizarOrigenElevacion();
        return true;
    }

    // Pone el punto A (observador) o B (objetivo) en \a posicion, mueve su marca y recalcula.
    void fijarPunto(bool esA, const QGeoCoordinate &posicion)
    {
        (esA ? m_a : m_b) = posicion;
        if (auto f = m_mapa->feature(esA ? m_idA : m_idB)) {
            f->geometry = { posicion };
            m_mapa->updateFeature(*f);
        }
        if (esA)
            borrarPicos();                  // los picos eran de la A anterior
        mostrarCoordenadas();
        analizar();
    }

    // Lee la coordenada escrita en la caja de A o de B (decimal, sexagesimal, UTM o MGRS, lo que entienda
    // parseCoordinate) y pone ahí el punto, centrando el mapa. Si no se entiende, marca la caja en rojo y
    // lo dice en la barra de estado sin tocar el punto. Devuelve si se aplicó.
    bool aplicarTexto(bool esA)
    {
        QLineEdit *caja = esA ? m_editA : m_editB;
        bool ok = false;
        const QGeoCoordinate pos = parseCoordinate(caja->text().trimmed(), &ok);
        if (!ok || !pos.isValid()) {
            caja->setStyleSheet(QStringLiteral("QLineEdit { background: #ffcdd2; }"));
            statusBar()->showMessage(tr("No entiendo la coordenada de %1: prueba «23.1136, -82.3666», "
                                        "«23°06'48\"N 82°21'59\"O», UTM o MGRS.")
                                         .arg(esA ? QStringLiteral("A") : QStringLiteral("B")), 6000);
            return false;
        }
        caja->setStyleSheet(QString());
        statusBar()->clearMessage();
        if ((esA ? m_a : m_b).distanceTo(pos) > 1.0) {
            fijarPunto(esA, pos);
            m_mapa->setCenter(pos);
        }
        return true;
    }

    // Escribe en las cajas las coordenadas actuales de A y B (sin disparar su edición) y quita el rojo.
    void mostrarCoordenadas()
    {
        for (const bool esA : { true, false }) {
            QLineEdit *caja = esA ? m_editA : m_editB;
            const QSignalBlocker bloqueo(caja);
            caja->setText(formatCoordinate(esA ? m_a : m_b, CoordinateFormat::Decimal));
            caja->setStyleSheet(QString());
        }
    }

    // El suelo del análisis: 0 si «mar = 0» está marcado (los huecos y las cotas negativas valen 0 m, el nivel
    // del mar), o NaN para usar la cota cruda. Se pasa como voidElevation a todos los cálculos de la librería.
    double sueloMar() const
    {
        return m_mar->isChecked() ? 0.0 : std::numeric_limits<double>::quiet_NaN();
    }

    // La cota en \a pos con la misma regla que el análisis: con «mar = 0», sin dato o bajo cero es 0.
    // NaN si no hay modelo de elevación.
    double cotaEn(const QGeoCoordinate &pos) const
    {
        if (!tieneElevacion())
            return std::numeric_limits<double>::quiet_NaN();
        const double c = m_mapa->elevationAt(pos);
        if (!m_mar->isChecked())
            return c;
        return std::isfinite(c) ? std::max(c, 0.0) : 0.0;
    }

    // Elige el alcance (km) y la altura del objetivo (m) de la zona visible, sin calcularla.
    void ponerZona(double alcanceKm, double alturaObjetivo)
    {
        m_alcance->setValue(alcanceKm);
        m_alturaObjetivo->setValue(alturaObjetivo);
    }

    // Las alturas de las antenas de A y B (metros sobre el terreno), y recalcula.
    void ponerAlturas(double alturaA, double alturaB)
    {
        m_alturaA->setValue(alturaA);
        m_alturaB->setValue(alturaB);
        analizar();
    }

    // Mueve A o B porque se está arrastrando su marca con el ratón (señal de la librería) y recalcula.
    void alArrastrar(qint64 id, const QGeoCoordinate &posicion)
    {
        if (id == m_idA) {
            m_a = posicion;
            borrarPicos();
        } else if (id == m_idB) {
            m_b = posicion;
        } else {
            return;
        }
        mostrarCoordenadas();
        analizar();
    }

    // Calcula la visual y el perfil entre A y B con las alturas actuales, y actualiza los textos y el
    // dibujo. Es lo que se llama al mover un punto o cambiar una altura.
    void analizar()
    {
        Analisis a;
        a.hayElevacion = tieneElevacion();
        a.distanciaM = m_a.distanceTo(m_b);
        QString mensaje;
        if (!a.hayElevacion) {
            mensaje = tr("Este paquete no trae elevación. Prueba el «Terreno de demostración» (menú Terreno).");
        } else {
            const double paso = std::max(30.0, a.distanciaM / 1500.0);
            ElevationProfileParams pp;
            pp.stepMeters = paso;
            pp.voidElevation = sueloMar();
            a.perfil = m_mapa->elevationProfile({ m_a, m_b }, pp);
            LineOfSightParams lp;
            lp.stepMeters = paso;
            lp.voidElevation = sueloMar();
            a.los = m_mapa->lineOfSight(m_a, m_b, m_alturaA->value(), m_alturaB->value(), lp);
            a.cotaA = cotaEn(m_a);
            a.cotaB = cotaEn(m_b);
            if (!a.los.isValid())
                mensaje = tr("Sin cota en A, en B o en parte del trayecto: no se puede decidir. Mueve los puntos "
                             "a una zona con modelo de elevación.");
        }
        m_ultimo = a;

        m_coordenadas->setText(tr("Cota de A: %1  ·  cota de B: %2\nDistancia: %3 km  ·  azimut de A a B: %4°")
            .arg(textoCota(a.cotaA), textoCota(a.cotaB))
            .arg(a.distanciaM / 1000.0, 0, 'f', 2).arg(qRound(m_a.azimuthTo(m_b))));
        refrescarCierres();
        m_textoVisual->setText(textoVisual(a));
        m_perfil->setDatos(a, m_alturaA->value(), m_alturaB->value(), mensaje);
    }

    // La menor altura de antena de B (metros sobre el terreno, redondeada hacia arriba al metro) con la
    // que A ve B, manteniendo la altura de A, o -1 si ni con 5000 m se vería. Es una bisección sobre
    // lineOfSight: la visibilidad solo mejora al subir la antena.
    double alturaMinimaB() const
    {
        const auto ve = [this](double alturaB) {
            LineOfSightParams lp;
            lp.voidElevation = sueloMar();
            const LineOfSightResult r = m_mapa->lineOfSight(m_a, m_b, m_alturaA->value(), alturaB, lp);
            return r.isValid() && r.clear;
        };
        if (!tieneElevacion() || !ve(5000.0))
            return -1.0;
        double bajo = 0.0, alto = 5000.0;
        if (ve(bajo))
            return 0.0;
        while (alto - bajo > 0.5) {
            const double medio = 0.5 * (bajo + alto);
            (ve(medio) ? alto : bajo) = medio;
        }
        return std::ceil(alto);
    }

    // Calcula la zona visible desde A (viewshed 360°) con el alcance y la altura de objetivo elegidos,
    // la dibuja sobre el mapa y devuelve sus datos. Muestra una barra de progreso que se puede cancelar;
    // si se cancela o no hay cota en A, devuelve una zona no válida y no dibuja nada.
    ZonaVisible calcularZonaVisible()
    {
        ZonaVisible z;
        borrarZonaVisible();
        if (!tieneElevacion()) {
            m_textoZona->setText(tr("Sin modelo de elevación no hay zona visible."));
            return z;
        }
        ViewshedParams vp;
        vp.maxRangeM = m_alcance->value() * 1000.0;
        vp.observerHeight = m_alturaA->value();
        vp.targetHeight = m_alturaObjetivo->value();
        vp.stepMeters = std::max(30.0, vp.maxRangeM / 700.0);
        vp.voidElevation = sueloMar();
        m_cancelar = false;
        m_progreso->setValue(0);
        m_progreso->setVisible(true);
        m_botonCancelar->setVisible(true);
        const Viewshed vs = m_mapa->viewshed(m_a, vp, [this](int hechos, int total) {
            m_progreso->setMaximum(total);
            m_progreso->setValue(hechos);
            QApplication::processEvents();          // deja pulsar «Cancelar» mientras calcula
            return !m_cancelar;
        });
        m_progreso->setVisible(false);
        m_botonCancelar->setVisible(false);
        if (!vs.isValid()) {
            m_textoZona->setText(m_cancelar ? tr("Cálculo cancelado.")
                                            : tr("No hay cota en A: mueve el observador a una zona con elevación."));
            return z;
        }

        // Zona de visibilidad REAL, como en la demo: una cuña (trozo de corona circular) por cada TRAMO de cada
        // rayo, de donde empieza a verse (o a esconderse) a donde acaba, en la franja de azimut del rayo (± medio
        // paso). Todas las cuñas de un mismo tipo son partes de UNA entidad: AZUL = visible, AMARILLO = oculto;
        // lo que no tiene dato se deja sin pintar. Se pinta TODO lo visible, también lo que reaparece tras una
        // loma o una cima.
        const double mediaFranja = 0.5 * 360.0 / double(vs.rays.size());
        const auto cuñasDe = [&](const QVector<VisibleRange> &tramos, double azimut, double *areaM2) {
            QVector<QVector<QGeoCoordinate>> cuñas;
            for (const VisibleRange &t : tramos) {
                if (t.endM - t.startM < 1.0)
                    continue;                           // un tramo de menos de un metro no se ve ni importa
                const double d0 = std::max(t.startM, 1.0);
                const QVector<QGeoCoordinate> cuña = {
                    m_a.atDistanceAndAzimuth(d0, azimut - mediaFranja),
                    m_a.atDistanceAndAzimuth(t.endM, azimut - mediaFranja),
                    m_a.atDistanceAndAzimuth(t.endM, azimut + mediaFranja),
                    m_a.atDistanceAndAzimuth(d0, azimut + mediaFranja),
                };
                *areaM2 += polygonArea(cuña);
                cuñas.append(cuña);
            }
            return cuñas;
        };
        QVector<QVector<QGeoCoordinate>> cuñasVisibles, cuñasOcultas;
        double areaVisibleM2 = 0.0, areaOcultaM2 = 0.0;
        z.alcanceMaxM = -1.0;
        z.cierreMaxDeg = -90.0;
        z.cierreMinDeg = 90.0;
        for (const ViewshedRay &r : vs.rays) {
            z.cierres.append(QPointF(r.azimuthDeg, r.horizonDeg));
            if (r.horizonDeg > z.cierreMaxDeg) {
                z.cierreMaxDeg = r.horizonDeg;
                z.cierreMaxAzimutDeg = r.azimuthDeg;
            }
            if (r.horizonDeg < z.cierreMinDeg) {
                z.cierreMinDeg = r.horizonDeg;
                z.cierreMinAzimutDeg = r.azimuthDeg;
            }
            cuñasVisibles += cuñasDe(r.visibleRanges, r.azimuthDeg, &areaVisibleM2);
            cuñasOcultas += cuñasDe(r.hiddenRanges, r.azimuthDeg, &areaOcultaM2);
            for (const VisibleRange &t : r.visibleRanges) {
                if (t.endM > z.alcanceMaxM) {
                    z.alcanceMaxM = t.endM;
                    z.azimutMaxDeg = r.azimuthDeg;
                }
            }
            // La zona ciega más cercana: donde acaba el primer tramo, si acaba antes del alcance.
            const double fin = r.visibleRanges.isEmpty() ? 0.0 : r.visibleRanges.first().endM;
            const bool ciega = r.visibleRanges.isEmpty() || fin < vp.maxRangeM - 2.0 * vp.stepMeters;
            if (ciega && (z.zonaCiegaM < 0.0 || fin < z.zonaCiegaM)) {
                z.zonaCiegaM = fin;
                z.azimutCiegaDeg = r.azimuthDeg;
            }
        }
        const double circuloKm2 = M_PI * std::pow(vp.maxRangeM / 1000.0, 2.0);
        z.rayos = int(vs.rays.size());
        z.tramos = int(cuñasVisibles.size());
        z.tramosOcultos = int(cuñasOcultas.size());
        z.areaKm2 = areaVisibleM2 / 1.0e6;
        z.porcentaje = 100.0 * z.areaKm2 / circuloKm2;
        z.porcentajeOculto = 100.0 * areaOcultaM2 / 1.0e6 / circuloKm2;
        z.porcentajeSinDato = std::max(0.0, 100.0 - z.porcentaje - z.porcentajeOculto);
        z.valida = true;

        const auto pintarCuñas = [this](const QVector<QVector<QGeoCoordinate>> &cuñas, const QString &tipo,
                                        const QString &nombre, const QColor &relleno) {
            if (cuñas.isEmpty())
                return;
            MapFeature zona;
            zona.layerId = kCapaZona;
            zona.kind = GeometryKind::Polygon;
            zona.type = tipo;
            zona.name = nombre;
            zona.parts = cuñas;                         // una sola entidad con todas las cuñas
            zona.geometry = cuñas.first();
            zona.style.lineColor = QColor(0, 0, 0, 0);  // sin borde por cuña: solo el relleno
            zona.style.fillColor = relleno;
            zona.style.labelVisible = false;
            zona.selectable = false;
            m_mapa->addFeature(zona);
        };
        // Lo oculto primero (debajo) y lo visible encima, como en la demo.
        pintarCuñas(cuñasOcultas, QStringLiteral("zona_oculta"), tr("Oculto"), QColor(0xff, 0xc1, 0x07, 90));
        pintarCuñas(cuñasVisibles, QStringLiteral("zona_visibilidad"), tr("Visible a %1 m").arg(vp.targetHeight, 0, 'f', 0),
                    QColor(0x42, 0xa5, 0xf5, 80));

        // La máxima visibilidad: el punto visible más lejano, unido a A por una línea y con su distancia.
        if (z.alcanceMaxM > 0.0) {
            const QGeoCoordinate lejano = m_a.atDistanceAndAzimuth(z.alcanceMaxM, z.azimutMaxDeg);
            MapFeature linea;
            linea.layerId = kCapaZona;
            linea.kind = GeometryKind::Polyline;
            linea.type = QStringLiteral("maxima_visibilidad_linea");
            linea.geometry = { m_a, lejano };
            linea.style.lineColor = QColor(0x0d, 0x47, 0xa1);
            linea.style.lineWidth = 1.5;
            linea.style.labelVisible = false;
            linea.selectable = false;
            m_mapa->addFeature(linea);
            MapFeature punto;
            punto.layerId = kCapaZona;
            punto.kind = GeometryKind::Point;
            punto.type = QStringLiteral("maxima_visibilidad");
            punto.name = tr("Máx. %1 km").arg(z.alcanceMaxM / 1000.0, 0, 'f', 1);
            punto.geometry = { lejano };
            punto.style.pointRadiusPx = 6.0;
            punto.style.lineColor = QColor(0x0d, 0x47, 0xa1);
            punto.style.fillColor = QColor(0x42, 0xa5, 0xf5);
            punto.selectable = false;
            m_mapa->addFeature(punto);
        }
        m_ultimaZona = z;
        m_cierres = z.cierres;
        m_cierresAlcanceKm = m_alcance->value();
        refrescarCierres();
        const QString ciega = z.zonaCiegaM >= 0.0
            ? tr("Zona ciega más cercana: a %1 km de A, al azimut %2°.")
                  .arg(z.zonaCiegaM / 1000.0, 0, 'f', 1).arg(qRound(z.azimutCiegaDeg))
            : tr("Sin zona ciega: se ve todo hasta el alcance.");
        m_textoZona->setText(tr("Del círculo de %1 km: visible %2 %, oculto %3 %, sin dato %4 %. (%5 km² visibles, en %6 tramos.)\n"
                                "Máxima visibilidad: %7 km, al azimut %8°.\n%9\n"
                                "Ángulo de cierre: de %10° (azimut %11°, el más despejado) a %12° (azimut %13°, el más cerrado).\n"
                                "Azul = visible, amarillo = oculto.")
            .arg(m_alcance->value(), 0, 'f', 0).arg(z.porcentaje, 0, 'f', 0).arg(z.porcentajeOculto, 0, 'f', 0)
            .arg(z.porcentajeSinDato, 0, 'f', 0).arg(z.areaKm2, 0, 'f', 0).arg(z.tramos)
            .arg(z.alcanceMaxM / 1000.0, 0, 'f', 1).arg(qRound(z.azimutMaxDeg)).arg(ciega)
            .arg(z.cierreMinDeg, 0, 'f', 1).arg(qRound(z.cierreMinAzimutDeg))
            .arg(z.cierreMaxDeg, 0, 'f', 1).arg(qRound(z.cierreMaxAzimutDeg)));
        return z;
    }

    // Quita del mapa todo lo de la zona visible (las cuñas azules y amarillas, la línea y el marcador de la
    // máxima visibilidad): la capa es solo suya. Hay que vaciarla entera y no quitar «la» entidad, porque la
    // zona son varias; si no, cada cálculo iría apilando las del anterior.
    void borrarZonaVisible()
    {
        m_mapa->clearFeatureLayer(kCapaZona);
        m_ultimaZona = ZonaVisible();
        m_cierres.clear();
        refrescarCierres();
        m_textoZona->setText(tr("Elige el alcance y pulsa «Calcular zona visible»."));
    }

    // Redibuja la gráfica de ángulos de cierre en los 360° con los últimos calculados (o con el aviso de que
    // faltan) y el azimut actual hacia B.
    void refrescarCierres()
    {
        if (!m_graficoCierres)
            return;
        m_graficoCierres->setDatos(m_cierres, m_cierresAlcanceKm, m_a.azimuthTo(m_b),
                                   tr("Pulsa «Calcular zona visible» para dibujar los ángulos de cierre en los 360° "
                                      "hasta el alcance elegido."));
    }

    // Busca los \a cuantos picos más altos en un radio de \a radioM metros alrededor de A. Muestrea la cota en una
    // malla (paso = radio/150, mínimo 30 m), toma como candidatos las celdas que no tienen ninguna vecina más alta
    // y sí alguna más baja (así la llanura plana del mar no cuenta), descartando las del borde del círculo y las
    // que tocan un hueco sin cota (no se puede saber si siguen subiendo), y las recorre de la más alta a la más baja
    // aceptando solo las que quedan a más de una décima del radio de las ya aceptadas, para no devolver diez
    // puntos de la misma cresta. De cada pico calcula distancia, azimut, el ángulo con que se ve desde el ojo
    // de A y si hay línea de vista (con la altura de objetivo del panel de la zona visible). Vacío si no hay elevación o no hay cota en A.
    QVector<Pico> buscarPicos(double radioM, int cuantos = 10) const
    {
        QVector<Pico> res;
        if (!tieneElevacion() || !std::isfinite(cotaEn(m_a)))
            return res;
        const double paso = std::max(30.0, radioM / 150.0);
        const int n = int(radioM / paso);
        const int lado = 2 * n + 1;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double grLat = 180.0 / (M_PI * kRadioTierra);                  // grados de latitud por metro
        const double grLon = grLat / std::max(0.05, std::cos(m_a.latitude() * M_PI / 180.0));
        QVector<double> cota(lado * lado, nan);
        const auto pos = [&](int i, int j) {                                 // i = este, j = norte, en celdas
            return QGeoCoordinate(m_a.latitude() + (j - n) * paso * grLat, m_a.longitude() + (i - n) * paso * grLon);
        };
        for (int j = 0; j < lado; ++j)
            for (int i = 0; i < lado; ++i)
                if (std::hypot((i - n) * paso, (j - n) * paso) <= radioM)
                    cota[j * lado + i] = cotaEn(pos(i, j));
        QVector<Pico> candidatos;
        for (int j = 1; j < lado - 1; ++j) {
            for (int i = 1; i < lado - 1; ++i) {
                const double c = cota[j * lado + i];
                if (!std::isfinite(c))
                    continue;
                bool maximo = true, hayMasBajo = false;
                for (int dj = -1; dj <= 1 && maximo; ++dj) {
                    for (int di = -1; di <= 1; ++di) {
                        if (di == 0 && dj == 0)
                            continue;
                        const double v = cota[(j + dj) * lado + i + di];
                        if (!std::isfinite(v) || v > c) {   // el borde del círculo o un hueco: no se puede decidir
                            maximo = false;
                            break;
                        }
                        hayMasBajo = hayMasBajo || v < c;
                    }
                }
                if (maximo && hayMasBajo) {
                    Pico q;
                    q.posicion = pos(i, j);
                    q.cota = c;
                    candidatos.append(q);
                }
            }
        }
        std::sort(candidatos.begin(), candidatos.end(), [](const Pico &x, const Pico &y) { return x.cota > y.cota; });
        const double separacion = radioM / 10.0;
        const double ojoA = cotaEn(m_a) + m_alturaA->value();
        LineOfSightParams lp;
        lp.voidElevation = sueloMar();
        for (Pico q : candidatos) {
            bool lejos = true;
            for (const Pico &otro : res)
                lejos = lejos && otro.posicion.distanceTo(q.posicion) >= separacion;
            if (!lejos)
                continue;
            q.distanciaM = m_a.distanceTo(q.posicion);
            q.azimutDeg = m_a.azimuthTo(q.posicion);
            q.anguloDeg = std::atan2(q.cota - ojoA - q.distanciaM * q.distanciaM / (2.0 * kK * kRadioTierra),
                                     std::max(q.distanciaM, 1.0)) * 180.0 / M_PI;
            const LineOfSightResult v = m_mapa->lineOfSight(m_a, q.posicion, m_alturaA->value(),
                                                            m_alturaObjetivo->value(), lp);
            q.visible = v.isValid() && v.clear;
            res.append(q);
            if (res.size() >= cuantos)
                break;
        }
        return res;
    }

    // Busca los picos del radio elegido, los pone en la tabla y los marca en el mapa (un punto por pico,
    // con su número y su cota).
    void actualizarPicos()
    {
        borrarPicos();
        if (!tieneElevacion()) {
            statusBar()->showMessage(tr("Sin modelo de elevación no hay picos que buscar."), 5000);
            return;
        }
        m_picos = buscarPicos(m_radioPicos->value() * 1000.0);
        m_tablaPicos->setRowCount(int(m_picos.size()));
        for (int k = 0; k < m_picos.size(); ++k) {
            const Pico &q = m_picos.at(k);
            const QStringList celdas = {
                QString::number(k + 1), tr("%1 m").arg(qRound(q.cota)), tr("%1 km").arg(q.distanciaM / 1000.0, 0, 'f', 1),
                QStringLiteral("%1°").arg(qRound(q.azimutDeg)), QStringLiteral("%1°").arg(q.anguloDeg, 0, 'f', 1),
                q.visible ? tr("sí") : tr("no")
            };
            for (int c = 0; c < celdas.size(); ++c) {
                auto *item = new QTableWidgetItem(celdas.at(c));
                item->setTextAlignment(Qt::AlignCenter);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                m_tablaPicos->setItem(k, c, item);
            }
            MapFeature f;
            f.kind = GeometryKind::Point;
            f.layerId = kCapaPicos;
            f.type = QStringLiteral("pico");
            f.name = tr("%1 · %2 m").arg(k + 1).arg(qRound(q.cota));
            f.geometry = { q.posicion };
            f.style.pointRadiusPx = 6.0;
            f.style.lineColor = QColor(0xbf, 0x36, 0x0c);
            f.style.fillColor = q.visible ? QColor(0xff, 0x70, 0x43) : QColor(0xbc, 0xaa, 0xa4);
            f.selectable = false;
            m_mapa->addFeature(f);
        }
        m_textoPicos->setText(m_picos.isEmpty()
            ? tr("No hay picos en %1 km alrededor de A (terreno llano o sin datos).").arg(m_radioPicos->value(), 0, 'f', 0)
            : tr("Los %1 picos más altos en %2 km alrededor de A. Naranja = se ve desde A; gris = lo tapa el terreno. "
                 "Doble clic en una fila: poner B en ese pico.")
                  .arg(m_picos.size()).arg(m_radioPicos->value(), 0, 'f', 0));
    }

    // Quita los picos de la tabla y del mapa (se llama al mover A, porque ya no son los suyos).
    void borrarPicos()
    {
        m_picos.clear();
        if (m_tablaPicos)
            m_tablaPicos->setRowCount(0);
        m_mapa->clearFeatureLayer(kCapaPicos);
        if (m_textoPicos)
            m_textoPicos->setText(tr("Pulsa «Buscar picos» para listar los más altos alrededor de A."));
    }

    // Pone B en el pico de la fila \a fila de la tabla (su cima), para ver si A lo ve y con qué ángulo.
    void ponerBEnPico(int fila)
    {
        if (fila >= 0 && fila < m_picos.size())
            fijarPunto(false, m_picos.at(fila).posicion);
    }

    // Enciende o apaga el relieve sombreado (necesita el modelo de elevación).
    void activarRelieve(bool on)
    {
        const bool activo = on && tieneElevacion();
        m_mapa->setHillshadeVisible(activo);
        const QSignalBlocker bloqueo(m_relieve);     // la casilla refleja el estado sin volver a dispararlo
        m_relieve->setChecked(activo);
    }

    // Datos para las pruebas.
    const Analisis &ultimoAnalisis() const { return m_ultimo; }
    const ZonaVisible &ultimaZona() const { return m_ultimaZona; }
    QGeoCoordinate puntoA() const { return m_a; }
    QGeoCoordinate puntoB() const { return m_b; }
    qint64 idA() const { return m_idA; }
    qint64 idB() const { return m_idB; }
    QString textoVisualActual() const { return m_textoVisual->text(); }
    QString textoZonaActual() const { return m_textoZona->text(); }
    QListWidget *listaGlosario() const { return m_listaGlosario; }
    CierresWidget *graficoCierres() const { return m_graficoCierres; }
    QTableWidget *tablaPicos() const { return m_tablaPicos; }
    const QVector<Pico> &picos() const { return m_picos; }
    QLineEdit *cajaA() const { return m_editA; }
    QLineEdit *cajaB() const { return m_editB; }
    QCheckBox *casillaMar() const { return m_mar; }
    // Pone el radio de búsqueda de picos (km).
    void ponerRadioPicos(double km) { m_radioPicos->setValue(km); }

private:
    // «123 m» o «sin dato» para una cota.
    static QString textoCota(double cota)
    {
        return std::isfinite(cota) ? QStringLiteral("%1 m").arg(qRound(cota)) : QObject::tr("sin dato");
    }

    // La frase del resultado de la visual: si hay vista y con qué holgura, o dónde bloquea el terreno y
    // qué altura de antena en B haría falta.
    QString textoVisual(const Analisis &a) const
    {
        if (!a.hayElevacion)
            return tr("Sin modelo de elevación: no se puede calcular la línea de vista.");
        if (!a.los.isValid())
            return tr("Sin dato de elevación en A, en B o en parte del trayecto.");
        if (a.los.clear)
            return tr("✔ Hay línea de vista. Holgura mínima: %1 m, a %2 km de A.")
                .arg(a.los.clearanceM, 0, 'f', 0).arg(a.los.blockDistanceM / 1000.0, 0, 'f', 1);
        const double minima = alturaMinimaB();
        const QString consejo = minima >= 0.0
            ? tr("Con la antena de B a %1 m se vería.").arg(qRound(minima))
            : tr("Ni con la antena de B a 5000 m se vería.");
        return tr("✖ El terreno bloquea la visual a %1 km de A (le faltan %2 m de holgura). %3")
            .arg(a.los.blockDistanceM / 1000.0, 0, 'f', 1).arg(-a.los.clearanceM, 0, 'f', 0).arg(consejo);
    }

    // Una spin box de metros con \a valor y límites razonables para alturas y distancias.
    QDoubleSpinBox *cajaMetros(double minimo, double maximo, double valor, const QString &sufijo)
    {
        auto *c = new QDoubleSpinBox(this);
        c->setRange(minimo, maximo);
        c->setDecimals(0);
        c->setSuffix(sufijo);
        c->setValue(valor);
        return c;
    }

    // Crea las marcas arrastrables de A y B, a 12 km a cada lado de \a centro, y las deja vistas.
    void crearPuntos(const QGeoCoordinate &centro)
    {
        m_a = centro.atDistanceAndAzimuth(12000.0, 270.0);
        m_b = centro.atDistanceAndAzimuth(12000.0, 90.0);
        const auto marca = [this](const QString &nombre, const QGeoCoordinate &pos, const QColor &color) {
            MapFeature f;
            f.kind = GeometryKind::Point;
            f.layerId = kCapaPuntos;
            f.name = nombre;
            f.geometry = { pos };
            f.draggable = true;                       // se arrastra con la herramienta Navegar
            f.style.pointRadiusPx = 9.0;
            f.style.lineColor = color.darker(150);
            f.style.fillColor = color;
            return m_mapa->addFeature(f);
        };
        m_idA = marca(QStringLiteral("A"), m_a, QColor(46, 125, 50));
        m_idB = marca(QStringLiteral("B"), m_b, QColor(21, 101, 192));
    }

    // Pone en el panel qué modelo de elevación hay cargado (o que no hay), y habilita lo que lo necesita.
    void actualizarOrigenElevacion()
    {
        const QString descripcion = m_mapa->elevationDescription();
        m_origenElevacion->setText(descripcion.isEmpty() ? tr("Sin modelo de elevación en este paquete.")
                                                         : descripcion);
        for (const QString &aviso : m_mapa->elevationWarnings())
            statusBar()->showMessage(aviso, 8000);
        const bool hay = !descripcion.isEmpty();
        m_botonZona->setEnabled(hay);
        m_relieve->setEnabled(hay);
        m_sol->setEnabled(hay);
    }

    // Paneles: puntos y visual (izquierda), perfil (abajo) y glosario (derecha).
    void crearPaneles()
    {
        auto *panel = new QWidget(this);
        auto *col = new QVBoxLayout(panel);

        auto *grupoElevacion = new QGroupBox(tr("Modelo de elevación (DEM)"), this);
        auto *colElevacion = new QVBoxLayout(grupoElevacion);
        m_origenElevacion = new QLabel(this);
        m_origenElevacion->setWordWrap(true);
        auto *botonDemo = new QPushButton(tr("Usar terreno de demostración"), this);
        m_cota = new QLabel(tr("Cota bajo el cursor: —"), this);
        m_mar = new QCheckBox(tr("Mar = 0 (nivel del mar)"), this);
        m_mar->setChecked(true);
        m_mar->setToolTip(tr("Donde no hay cota (el mar) o es negativa (el fondo marino) se usa 0 m. La "
                             "curvatura de la Tierra se mantiene."));
        colElevacion->addWidget(m_origenElevacion);
        colElevacion->addWidget(m_mar);
        colElevacion->addWidget(botonDemo);
        colElevacion->addWidget(m_cota);
        col->addWidget(grupoElevacion);

        auto *grupoPuntos = new QGroupBox(tr("Observador (A) y objetivo (B)"), this);
        auto *form = new QFormLayout(grupoPuntos);
        m_alturaA = cajaMetros(0.0, 5000.0, 10.0, tr(" m"));
        m_alturaB = cajaMetros(0.0, 5000.0, 10.0, tr(" m"));
        const QString ayudaCoordenada = tr("Escribe la coordenada y pulsa Intro: decimal («23.1136, -82.3666»), "
                                           "sexagesimal («23°06'48\"N 82°21'59\"O»), UTM o MGRS.");
        m_editA = new QLineEdit(this);
        m_editB = new QLineEdit(this);
        m_editA->setToolTip(ayudaCoordenada);
        m_editB->setToolTip(ayudaCoordenada);
        form->addRow(tr("Coordenada de A:"), m_editA);
        form->addRow(tr("Coordenada de B:"), m_editB);
        form->addRow(tr("Altura de antena de A:"), m_alturaA);
        form->addRow(tr("Altura de antena de B:"), m_alturaB);
        m_coordenadas = new QLabel(this);
        m_coordenadas->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(m_coordenadas);
        auto *fila = new QHBoxLayout;
        auto *fijarA = new QPushButton(tr("Fijar A en el mapa"), this);
        auto *fijarB = new QPushButton(tr("Fijar B en el mapa"), this);
        fila->addWidget(fijarA);
        fila->addWidget(fijarB);
        form->addRow(fila);
        col->addWidget(grupoPuntos);

        auto *grupoVista = new QGroupBox(tr("Línea de vista"), this);
        auto *colVista = new QVBoxLayout(grupoVista);
        m_textoVisual = new QLabel(this);
        m_textoVisual->setWordWrap(true);
        colVista->addWidget(m_textoVisual);
        col->addWidget(grupoVista);

        auto *grupoZona = new QGroupBox(tr("Zona visible desde A"), this);
        auto *formZona = new QFormLayout(grupoZona);
        m_alcance = new QDoubleSpinBox(this);
        m_alcance->setRange(1.0, 100.0);
        m_alcance->setDecimals(0);
        m_alcance->setSuffix(tr(" km"));
        m_alcance->setValue(20.0);
        m_alturaObjetivo = cajaMetros(0.0, 5000.0, 2.0, tr(" m"));
        formZona->addRow(tr("Alcance:"), m_alcance);
        formZona->addRow(tr("Altura del objetivo:"), m_alturaObjetivo);
        m_botonZona = new QPushButton(tr("Calcular zona visible"), this);
        m_progreso = new QProgressBar(this);
        m_progreso->setVisible(false);
        m_botonCancelar = new QPushButton(tr("Cancelar"), this);
        m_botonCancelar->setVisible(false);
        auto *botonBorrar = new QPushButton(tr("Borrar zona visible"), this);
        m_textoZona = new QLabel(this);
        m_textoZona->setWordWrap(true);
        formZona->addRow(m_botonZona);
        formZona->addRow(m_progreso);
        formZona->addRow(m_botonCancelar);
        formZona->addRow(botonBorrar);
        formZona->addRow(m_textoZona);
        col->addWidget(grupoZona);

        auto *grupoPicos = new QGroupBox(tr("Picos alrededor de A"), this);
        auto *colPicos = new QVBoxLayout(grupoPicos);
        auto *filaPicos = new QHBoxLayout;
        m_radioPicos = new QDoubleSpinBox(this);
        m_radioPicos->setRange(1.0, 50.0);
        m_radioPicos->setDecimals(0);
        m_radioPicos->setSuffix(tr(" km"));
        m_radioPicos->setValue(10.0);
        auto *botonPicos = new QPushButton(tr("Buscar picos"), this);
        filaPicos->addWidget(new QLabel(tr("Radio:"), this));
        filaPicos->addWidget(m_radioPicos);
        filaPicos->addWidget(botonPicos);
        m_tablaPicos = new QTableWidget(0, 6, this);
        m_tablaPicos->setHorizontalHeaderLabels({ tr("N.º"), tr("Cota"), tr("Distancia"), tr("Azimut"),
                                                  tr("Ángulo"), tr("¿Se ve?") });
        m_tablaPicos->horizontalHeaderItem(4)->setToolTip(tr("Ángulo de cierre que ese pico forma visto desde el ojo "
                                                             "de A: su elevación sobre el horizonte."));
        m_tablaPicos->horizontalHeaderItem(5)->setToolTip(tr("¿Hay línea de vista desde A hasta la cima, con un objetivo "
                                                             "de la altura elegida en la zona visible?"));
        m_tablaPicos->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_tablaPicos->verticalHeader()->setVisible(false);
        m_tablaPicos->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tablaPicos->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tablaPicos->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tablaPicos->setMinimumHeight(190);
        m_textoPicos = new QLabel(this);
        m_textoPicos->setWordWrap(true);
        colPicos->addLayout(filaPicos);
        colPicos->addWidget(m_tablaPicos);
        colPicos->addWidget(m_textoPicos);
        col->addWidget(grupoPicos);

        auto *grupoRelieve = new QGroupBox(tr("Relieve sombreado"), this);
        auto *colRelieve = new QVBoxLayout(grupoRelieve);
        m_relieve = new QCheckBox(tr("Mostrar el relieve"), this);
        m_sol = new QSlider(Qt::Horizontal, this);
        m_sol->setRange(0, 359);
        m_sol->setValue(315);
        m_sol->setToolTip(tr("Azimut del sol, en grados"));
        colRelieve->addWidget(m_relieve);
        colRelieve->addWidget(new QLabel(tr("Azimut del sol:"), this));
        colRelieve->addWidget(m_sol);
        col->addWidget(grupoRelieve);
        col->addStretch(1);

        // El panel es largo: va en un área con desplazamiento para que quepa en pantallas bajas.
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(panel);
        auto *dockPuntos = new QDockWidget(tr("Análisis"), this);
        dockPuntos->setWidget(scroll);
        addDockWidget(Qt::LeftDockWidgetArea, dockPuntos);
        dockPuntos->setMinimumWidth(360);

        m_perfil = new PerfilWidget(this);
        auto *dockPerfil = new QDockWidget(tr("Perfil de elevación"), this);
        dockPerfil->setWidget(m_perfil);
        addDockWidget(Qt::BottomDockWidgetArea, dockPerfil);
        m_graficoCierres = new CierresWidget(this);
        auto *dockCierres = new QDockWidget(tr("Ángulos de cierre (360°)"), this);
        dockCierres->setWidget(m_graficoCierres);
        addDockWidget(Qt::BottomDockWidgetArea, dockCierres);
        splitDockWidget(dockPerfil, dockCierres, Qt::Horizontal);   // lado a lado, no en pestañas

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
        for (const Termino &t : glosario())
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

        connect(botonDemo, &QPushButton::clicked, this, [this] { usarTerrenoDemostracion(); });
        connect(fijarA, &QPushButton::clicked, this, [this] { pedirPunto(true); });
        connect(fijarB, &QPushButton::clicked, this, [this] { pedirPunto(false); });
        connect(m_editA, &QLineEdit::editingFinished, this, [this] { aplicarTexto(true); });
        connect(m_editB, &QLineEdit::editingFinished, this, [this] { aplicarTexto(false); });
        connect(m_mar, &QCheckBox::toggled, this, [this](bool) {
            borrarZonaVisible();                  // la zona y los picos eran de la otra regla del mar
            borrarPicos();
            analizar();
        });
        connect(botonPicos, &QPushButton::clicked, this, [this] { actualizarPicos(); });
        connect(m_tablaPicos, &QTableWidget::cellDoubleClicked, this, [this](int fila, int) { ponerBEnPico(fila); });
        connect(m_tablaPicos, &QTableWidget::cellClicked, this, [this](int fila, int) {
            if (fila >= 0 && fila < m_picos.size())
                m_mapa->setCenter(m_picos.at(fila).posicion);
        });
        connect(m_alturaA, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { analizar(); });
        connect(m_alturaB, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { analizar(); });
        connect(m_botonZona, &QPushButton::clicked, this, [this] { calcularZonaVisible(); });
        connect(m_botonCancelar, &QPushButton::clicked, this, [this] { m_cancelar = true; });
        connect(botonBorrar, &QPushButton::clicked, this, [this] { borrarZonaVisible(); });
        connect(m_relieve, &QCheckBox::toggled, this, [this](bool on) { activarRelieve(on); });
        connect(m_sol, &QSlider::valueChanged, this, [this](int azimut) { m_mapa->setHillshadeSun(azimut, 45.0); });
    }

    // Menús: Terreno (demostración, fijar A y B, zona visible) y Ayuda (glosario, F1).
    void crearMenus()
    {
        QMenu *terreno = menuBar()->addMenu(tr("&Terreno"));
        connect(terreno->addAction(tr("Usar terreno de &demostración")), &QAction::triggered, this,
                [this] { usarTerrenoDemostracion(); });
        terreno->addSeparator();
        auto *fijarA = terreno->addAction(tr("Fijar &A en el mapa"));
        fijarA->setShortcut(QKeySequence(Qt::Key_A));
        connect(fijarA, &QAction::triggered, this, [this] { pedirPunto(true); });
        auto *fijarB = terreno->addAction(tr("Fijar &B en el mapa"));
        fijarB->setShortcut(QKeySequence(Qt::Key_B));
        connect(fijarB, &QAction::triggered, this, [this] { pedirPunto(false); });
        auto *zona = terreno->addAction(tr("Calcular zona &visible"));
        zona->setShortcut(QKeySequence(Qt::Key_V));
        connect(zona, &QAction::triggered, this, [this] { calcularZonaVisible(); });

        QMenu *ayuda = menuBar()->addMenu(tr("A&yuda"));
        QAction *glosarioAct = m_dockGlosario->toggleViewAction();
        glosarioAct->setText(tr("&Glosario"));
        glosarioAct->setShortcut(QKeySequence(Qt::Key_F1));
        ayuda->addAction(glosarioAct);
    }

    // Pide un punto al mapa con un clic (herramienta PickPoint) y lo pone como A o B.
    void pedirPunto(bool esA)
    {
        m_pidiendoA = esA;
        m_mapa->setActiveTool(MapTool::PickPoint);
        statusBar()->showMessage(tr("Haz clic en el mapa para fijar %1 (Esc cancela).").arg(esA ? QStringLiteral("A")
                                                                                              : QStringLiteral("B")));
    }

    // Conecta los eventos del mapa: arrastrar A o B, fijar un punto con un clic y la cota bajo el cursor.
    void conectarSenales()
    {
        connect(m_mapa, &MapWidget::featureDragMovedTo, this,
                [this](qint64 id, const QGeoCoordinate &pos) { alArrastrar(id, pos); });
        connect(m_mapa, &MapWidget::pointPicked, this, [this](const QGeoCoordinate &pos) {
            m_mapa->setActiveTool(MapTool::None);
            fijarPunto(m_pidiendoA, pos);
            statusBar()->clearMessage();
        });
        connect(m_mapa, &MapWidget::pickCancelled, this, [this] { statusBar()->clearMessage(); });
        connect(m_mapa, &MapWidget::mouseMoved, this, [this](const QGeoCoordinate &pos) {
            m_cota->setText(tr("Cota bajo el cursor: %1").arg(textoCota(cotaEn(pos))));
        });
    }

    MapWidget *m_mapa = nullptr;
    PerfilWidget *m_perfil = nullptr;
    QDoubleSpinBox *m_alturaA = nullptr;
    QDoubleSpinBox *m_alturaB = nullptr;
    QDoubleSpinBox *m_alcance = nullptr;
    QDoubleSpinBox *m_alturaObjetivo = nullptr;
    QDoubleSpinBox *m_radioPicos = nullptr;
    QCheckBox *m_mar = nullptr;
    QLineEdit *m_editA = nullptr;
    QLineEdit *m_editB = nullptr;
    QTableWidget *m_tablaPicos = nullptr;
    QLabel *m_textoPicos = nullptr;
    CierresWidget *m_graficoCierres = nullptr;
    QLabel *m_origenElevacion = nullptr;
    QLabel *m_cota = nullptr;
    QLabel *m_coordenadas = nullptr;
    QLabel *m_textoVisual = nullptr;
    QLabel *m_textoZona = nullptr;
    QProgressBar *m_progreso = nullptr;
    QPushButton *m_botonZona = nullptr;
    QPushButton *m_botonCancelar = nullptr;
    QCheckBox *m_relieve = nullptr;
    QSlider *m_sol = nullptr;
    QListWidget *m_listaGlosario = nullptr;
    QDockWidget *m_dockGlosario = nullptr;
    QTemporaryDir m_demoDir;                  // el terreno de demostración, mientras viva la ventana
    QGeoCoordinate m_a, m_b;
    qint64 m_idA = -1, m_idB = -1;
    bool m_pidiendoA = true;
    bool m_cancelar = false;
    Analisis m_ultimo;
    ZonaVisible m_ultimaZona;
    QVector<Pico> m_picos;
    QVector<QPointF> m_cierres;               // los ángulos de cierre de la última zona visible calculada
    double m_cierresAlcanceKm = 0.0;
};

// Entidades de la capa de la zona visible con ese \a tipo (zona_visibilidad, zona_oculta, maxima_visibilidad...).
static QVector<MapFeature> entidadesDeZona(MapWidget *mapa, const QString &tipo)
{
    QVector<MapFeature> v;
    for (const MapFeature &f : mapa->featuresInLayer(kCapaZona))
        if (f.type == tipo)
            v.append(f);
    return v;
}

// Cuántos píxeles de \a imagen tienen exactamente el color \a c.
static int contarPixeles(const QImage &imagen, const QColor &c)
{
    int n = 0;
    for (int y = 0; y < imagen.height(); ++y)
        for (int x = 0; x < imagen.width(); ++x)
            if (QColor(imagen.pixel(x, y)) == c)
                ++n;
    return n;
}

// Prueba sin ventana, sobre el terreno de demostración: línea de vista bloqueada y clara, perfil,
// altura mínima de antena, zona visible, un punto fuera de los datos y el glosario.
static int comprobar(Ventana &v)
{
    QTextStream out(stdout);
    bool todoOk = true;
    const auto verificar = [&](const QString &que, bool ok) {
        out << (ok ? "ok    " : "FALLO ") << que << "\n";
        todoOk = todoOk && ok;
    };
    verificar(QStringLiteral("mapa listo"), v.mapa()->isReady());

    // --- Sin elevación (el paquete de prueba no trae): lo dice y no inventa nada ---
    if (!v.tieneElevacion()) {
        v.analizar();
        verificar(QStringLiteral("sin elevación, la visual lo dice y no hay perfil ni visibilidad"),
                  !v.ultimoAnalisis().hayElevacion && !v.ultimoAnalisis().los.isValid()
                  && !v.ultimoAnalisis().perfil.isValid()
                  && v.textoVisualActual().contains(QStringLiteral("Sin modelo de elevación")));
        verificar(QStringLiteral("sin elevación, la zona visible no se calcula"), !v.calcularZonaVisible().valida);
    }

    // --- Terreno de demostración: una loma de 300 m en lon -82.5 ---
    verificar(QStringLiteral("el terreno de demostración se crea y queda como modelo de elevación"),
              v.usarTerrenoDemostracion() && v.tieneElevacion());
    const double cresta = v.mapa()->elevationAt(QGeoCoordinate(23.50, -82.50));
    const double llano = v.mapa()->elevationAt(QGeoCoordinate(23.50, -82.70));
    out << "      cota en la cresta: " << QString::number(cresta, 'f', 1) << " m, en el llano: "
        << QString::number(llano, 'f', 1) << " m\n";
    verificar(QStringLiteral("la cota de la cresta es ~320 m y la del llano ~20 m"),
              cresta > 310.0 && cresta < 330.0 && llano > 15.0 && llano < 25.0);

    // --- Línea de vista A–B con las dos antenas a 10 m: la loma la bloquea ---
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));
    v.fijarPunto(false, QGeoCoordinate(23.50, -82.38));
    v.ponerAlturas(10.0, 10.0);
    const Analisis &a = v.ultimoAnalisis();
    out << "      A-B: " << QString::number(a.distanciaM / 1000.0, 'f', 2) << " km, holgura "
        << QString::number(a.los.clearanceM, 'f', 1) << " m a " << QString::number(a.los.blockDistanceM / 1000.0, 'f', 2)
        << " km de A\n";
    verificar(QStringLiteral("A y B distan ~24,5 km"), a.distanciaM > 24000.0 && a.distanciaM < 25000.0);
    verificar(QStringLiteral("con antenas de 10 m el terreno bloquea, hacia la mitad, con ~300 m de falta"),
              a.los.isValid() && !a.los.clear && a.los.blockDistanceM > 10500.0 && a.los.blockDistanceM < 14000.0
              && a.los.clearanceM < -250.0 && a.los.clearanceM > -340.0);
    verificar(QStringLiteral("el texto dice que bloquea y qué altura haría falta"),
              v.textoVisualActual().contains(QStringLiteral("bloquea")) && v.textoVisualActual().contains(QStringLiteral("se vería")));

    // --- Perfil ---
    out << "      perfil: " << a.perfil.samples.size() << " muestras, " << QString::number(a.perfil.minElevation, 'f', 0) << ".."
        << QString::number(a.perfil.maxElevation, 'f', 0) << " m\n";
    verificar(QStringLiteral("el perfil tiene >700 muestras, ~24,5 km y llega a ~320 m"),
              a.perfil.samples.size() > 700 && a.perfil.totalDistanceM > 24000.0 && a.perfil.totalDistanceM < 25000.0
              && a.perfil.maxElevation > 310.0 && a.perfil.maxElevation < 330.0);
    v.perfil()->resize(900, 220);
    const int pixelesTerreno = contarPixeles(v.perfil()->grab().toImage(), PerfilWidget::colorTerreno());
    verificar(QStringLiteral("el widget dibuja el terreno del perfil (%1 píxeles de su color)").arg(pixelesTerreno),
              pixelesTerreno > 2000);

    // --- Altura mínima de la antena de B ---
    const double minima = v.alturaMinimaB();
    out << "      altura mínima de B: " << QString::number(minima, 'f', 0) << " m\n";
    verificar(QStringLiteral("la altura mínima de B está entre 500 y 700 m"), minima > 500.0 && minima < 700.0);
    v.ponerAlturas(10.0, minima);
    const bool veConMinima = v.ultimoAnalisis().los.isValid() && v.ultimoAnalisis().los.clear;
    v.ponerAlturas(10.0, minima - 2.0);
    const bool noVeConMenos = v.ultimoAnalisis().los.isValid() && !v.ultimoAnalisis().los.clear;
    verificar(QStringLiteral("con esa altura A ve B y con 2 m menos, no (la bisección es coherente)"),
              veConMinima && noVeConMenos);
    v.ponerAlturas(10.0, 700.0);
    verificar(QStringLiteral("con B a 700 m hay línea de vista y la holgura es positiva"),
              v.ultimoAnalisis().los.clear && v.ultimoAnalisis().los.clearanceM > 0.0
              && v.textoVisualActual().contains(QStringLiteral("Hay línea de vista")));
    v.ponerAlturas(10.0, 10.0);

    // --- Arrastrar A con el ratón: recalcula ---
    const QGeoCoordinate antes = v.puntoA();
    v.alArrastrar(v.idA(), QGeoCoordinate(23.50, -82.58));
    verificar(QStringLiteral("arrastrar A recalcula la distancia (queda más cerca de B)"),
              v.puntoA().longitude() > antes.longitude() && v.ultimoAnalisis().distanciaM < a.distanciaM + 1.0
              && v.ultimoAnalisis().distanciaM < 21000.0);
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));

    // --- Zona visible desde A: la loma la recorta hacia el este ---
    const ZonaVisible z = v.calcularZonaVisible();
    out << "      zona visible: " << z.rayos << " rayos, " << z.tramos << " tramos, " << QString::number(z.areaKm2, 'f', 0)
        << " km² (" << QString::number(z.porcentaje, 'f', 0) << " %), lo más lejos " << QString::number(z.alcanceMaxM / 1000.0, 'f', 1)
        << " km, zona ciega desde " << QString::number(z.zonaCiegaM / 1000.0, 'f', 1) << " km al azimut "
        << QString::number(z.azimutCiegaDeg, 'f', 0) << "°, oculto " << QString::number(z.porcentajeOculto, 'f', 0)
        << " %, sin dato " << QString::number(z.porcentajeSinDato, 'f', 1) << " %\n";
    verificar(QStringLiteral("la zona visible se calcula con 360 rayos"), z.valida && z.rayos == 360);
    verificar(QStringLiteral("la zona ciega más cercana (~12 km) es hacia el este, donde está la loma"),
              z.zonaCiegaM > 11000.0 && z.zonaCiegaM < 13500.0 && z.azimutCiegaDeg > 80.0 && z.azimutCiegaDeg < 100.0);

    // La zona no llega a los 20 km del alcance ni siquiera en el llano: la curvatura de la Tierra pone el
    // horizonte radioeléctrico de un ojo a 10 m y un objetivo a 2 m en √(2·k·R·10)+√(2·k·R·2) ≈ 18,8 km.
    // Con la loma como una pared a 12,24 km, la zona es ese círculo menos el casquete de detrás:
    // 18,84² · acos(12,24/18,84) − 12,24 · √(18,84² − 12,24²) ≈ 132 km² de los 1115 km² del círculo.
    ViewshedParams vp;
    vp.maxRangeM = 20000.0;
    vp.observerHeight = 10.0;
    vp.targetHeight = 2.0;
    vp.stepMeters = 30.0;
    const Viewshed vs = v.mapa()->viewshed(v.puntoA(), vp);
    const double horizonte = std::sqrt(2.0 * kK * kRadioTierra * 10.0) + std::sqrt(2.0 * kK * kRadioTierra * 2.0);
    double alcanceOeste = 0.0;
    for (const ViewshedRay &r : vs.rays)
        if (std::lround(r.azimuthDeg) == 270)
            alcanceOeste = r.visibilityReachM;
    out << "      horizonte radioeléctrico teórico: " << QString::number(horizonte / 1000.0, 'f', 2) << " km; al oeste: "
        << QString::number(alcanceOeste / 1000.0, 'f', 2) << " km\n";
    verificar(QStringLiteral("al oeste (llano) el alcance es el horizonte radioeléctrico (~18,8 km), no los 20 km"),
              std::abs(alcanceOeste - horizonte) < 600.0);
    verificar(QStringLiteral("la superficie visible es la del círculo del horizonte menos el casquete tras la loma (~983 km²)"),
              z.areaKm2 > 950.0 && z.areaKm2 < 1020.0);

    // Coherencia con lineOfSight en 5 azimuts: el último punto visible del rayo se ve y 60 m después, no.
    bool coherente = vs.isValid();
    for (int az : { 45, 90, 135, 180, 270 }) {
        for (const ViewshedRay &r : vs.rays) {
            if (std::lround(r.azimuthDeg) != az)
                continue;
            const QGeoCoordinate dentro = v.puntoA().atDistanceAndAzimuth(std::max(r.visibilityReachM - 30.0, 30.0), az);
            const QGeoCoordinate fuera = v.puntoA().atDistanceAndAzimuth(std::min(r.visibilityReachM + 60.0, 19990.0), az);
            const LineOfSightResult a = v.mapa()->lineOfSight(v.puntoA(), dentro, 10.0, 2.0);
            const LineOfSightResult b = v.mapa()->lineOfSight(v.puntoA(), fuera, 10.0, 2.0);
            coherente = coherente && a.isValid() && a.clear && b.isValid() && !b.clear;
        }
    }
    verificar(QStringLiteral("el alcance de la zona visible concuerda con lineOfSight en 5 azimuts (se ve justo antes, no justo después)"),
              coherente);
    const auto visibles = entidadesDeZona(v.mapa(), QStringLiteral("zona_visibilidad"));
    const auto ocultas = entidadesDeZona(v.mapa(), QStringLiteral("zona_oculta"));
    const auto maximo = entidadesDeZona(v.mapa(), QStringLiteral("maxima_visibilidad"));
    verificar(QStringLiteral("como en la demo: una entidad azul (visible) y otra amarilla (oculto), una cuña por tramo"),
              visibles.size() == 1 && ocultas.size() == 1 && visibles.first().outlines().size() == z.tramos
              && ocultas.first().outlines().size() == z.tramosOcultos
              && visibles.first().style.fillColor == QColor(0x42, 0xa5, 0xf5, 80)
              && ocultas.first().style.fillColor == QColor(0xff, 0xc1, 0x07, 90));
    verificar(QStringLiteral("la máxima visibilidad se pinta: un marcador en el punto visible más lejano (a la distancia de lo más lejos que se ve)"),
              maximo.size() == 1 && maximo.first().name.contains(QStringLiteral("Máx"))
              && std::abs(v.puntoA().distanceTo(maximo.first().geometry.first()) - z.alcanceMaxM) < 100.0);
    verificar(QStringLiteral("visible + oculto + sin dato suman el 100 % del círculo (y casi todo tiene dato)"),
              std::abs(z.porcentaje + z.porcentajeOculto + z.porcentajeSinDato - 100.0) < 0.01 && z.porcentajeSinDato < 5.0);
    v.borrarZonaVisible();
    verificar(QStringLiteral("borrar la zona la quita del mapa"), v.mapa()->featuresInLayer(kCapaZona).isEmpty());

    // --- Desde la cima de un pico redondeado, la zona visible NO es solo el primer tramo ---
    // Caso real (el Pico Turquino, con el 0 % del primer intento): desde una cumbre redondeada no se ve la
    // ladera justo bajo su borde (zona ciega), pero sí las tierras bajas de lejos. El pico de la demostración
    // (880 m, σ = 2 km, en 23.75, -82.25) lo reproduce: con el ojo a 20 m y un objetivo a 10 m el primer
    // tramo llega a ~0,8 km y el terreno vuelve a verse desde ~9,8 km hasta el alcance.
    const QGeoCoordinate cima(23.75, -82.25);
    v.fijarPunto(true, cima);
    v.ponerAlturas(20.0, 10.0);
    v.ponerZona(20.0, 10.0);
    const ZonaVisible zp = v.calcularZonaVisible();
    ViewshedParams pp;
    pp.maxRangeM = 20000.0;
    pp.observerHeight = 20.0;
    pp.targetHeight = 10.0;
    pp.stepMeters = 30.0;
    const Viewshed vsp = v.mapa()->viewshed(cima, pp);
    QVector<QGeoCoordinate> estrella;                    // lo que pintaba el primer intento: solo el primer tramo
    for (const ViewshedRay &r : vsp.rays)
        estrella << cima.atDistanceAndAzimuth(r.visibilityReachM, r.azimuthDeg);
    const double areaPrimerTramoKm2 = polygonArea(estrella) / 1.0e6;
    out << "      desde la cima: cota " << QString::number(v.mapa()->elevationAt(cima), 'f', 0) << " m, zona ciega desde "
        << QString::number(zp.zonaCiegaM / 1000.0, 'f', 2) << " km, se ve el " << QString::number(zp.porcentaje, 'f', 0)
        << " % (" << QString::number(zp.areaKm2, 'f', 0) << " km², en " << zp.tramos << " tramos); solo el primer tramo serían "
        << QString::number(areaPrimerTramoKm2, 'f', 1) << " km²; oculto " << QString::number(zp.porcentajeOculto, 'f', 0)
        << " %, sin dato " << QString::number(zp.porcentajeSinDato, 'f', 1) << " %\n";
    verificar(QStringLiteral("la cima de la demostración está a ~905 m"),
              std::abs(v.mapa()->elevationAt(cima) - 905.0) < 10.0);
    verificar(QStringLiteral("la zona ciega empieza a unos 0,8 km de la cima (el borde de la cumbre)"),
              zp.valida && zp.zonaCiegaM > 600.0 && zp.zonaCiegaM < 1000.0);
    verificar(QStringLiteral("más lejos las tierras bajas vuelven a verse: ~76 % del círculo, no el 0 % del primer tramo"),
              zp.porcentaje > 60.0 && zp.porcentaje < 90.0 && zp.alcanceMaxM > 19000.0 && areaPrimerTramoKm2 < 5.0
              && zp.areaKm2 > 100.0 * areaPrimerTramoKm2);
    bool reaparece = true;
    for (int az : { 45, 90, 200, 300 })
        for (const ViewshedRay &r : vsp.rays)
            if (std::lround(r.azimuthDeg) == az)
                reaparece = reaparece && r.visibleRanges.size() == 2;
    verificar(QStringLiteral("en cada rayo hay dos tramos visibles (la cima y las tierras bajas)"), reaparece);
    bool coherentePico = true;                           // y lineOfSight dice lo mismo: se ve, no se ve, se ve
    for (int az : { 45, 90, 200, 300 }) {
        const auto cerca = v.mapa()->lineOfSight(cima, cima.atDistanceAndAzimuth(300.0, az), 20.0, 10.0);
        const auto medio = v.mapa()->lineOfSight(cima, cima.atDistanceAndAzimuth(3000.0, az), 20.0, 10.0);
        const auto lejos = v.mapa()->lineOfSight(cima, cima.atDistanceAndAzimuth(17000.0, az), 20.0, 10.0);
        coherentePico = coherentePico && cerca.isValid() && cerca.clear && medio.isValid() && !medio.clear
                        && lejos.isValid() && lejos.clear;
    }
    verificar(QStringLiteral("lineOfSight lo confirma en 4 azimuts: a 300 m se ve, a 3 km no, a 17 km sí"), coherentePico);
    verificar(QStringLiteral("lo oculto entre la cima y las tierras bajas se pinta de amarillo: ~24 % del círculo"),
              zp.porcentajeOculto > 15.0 && zp.porcentajeOculto < 30.0 && zp.porcentajeSinDato < 3.0
              && entidadesDeZona(v.mapa(), QStringLiteral("zona_oculta")).size() == 1);
    verificar(QStringLiteral("el texto de la zona habla de la máxima visibilidad y de la zona ciega"),
              v.textoZonaActual().contains(QStringLiteral("Máxima visibilidad")) && v.textoZonaActual().contains(QStringLiteral("Zona ciega")));
    v.borrarZonaVisible();
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));
    v.ponerAlturas(10.0, 10.0);
    v.ponerZona(20.0, 2.0);

    // --- Un punto fuera de los datos: con «mar = 0» es mar (0 m); sin esa regla, no se inventa nada ---
    v.fijarPunto(true, QGeoCoordinate(23.50, -84.50));
    verificar(QStringLiteral("con «mar = 0» (por defecto) un punto sin datos cuenta como mar: cota 0 y visual válida"),
              v.casillaMar()->isChecked() && v.ultimoAnalisis().cotaA == 0.0 && v.ultimoAnalisis().los.isValid());
    v.casillaMar()->setChecked(false);
    verificar(QStringLiteral("con A fuera del terreno con datos la visual no es válida y lo dice"),
              !v.ultimoAnalisis().los.isValid() && v.textoVisualActual().contains(QStringLiteral("Sin dato")));
    verificar(QStringLiteral("con A fuera de los datos tampoco hay zona visible"), !v.calcularZonaVisible().valida);
    v.casillaMar()->setChecked(true);
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));

    // --- Coordenadas escritas de A y B ---
    v.cajaA()->setText(QStringLiteral("23.60, -82.70"));
    v.cajaA()->editingFinished();
    verificar(QStringLiteral("escribir «23.60, -82.70» en la caja de A mueve A ahí"),
              v.puntoA().distanceTo(QGeoCoordinate(23.60, -82.70)) < 2.0
              && v.cajaA()->styleSheet().isEmpty());
    // Con teselas de zoom 10 el centro queda a un píxel (~150 m); con el paquete mínimo del CI (zoom 0-2) el mapa es el mundo
    // entero y no puede centrarse en nada, así que ahí no se comprueba.
    const double pixelM = 40075000.0 / (256.0 * std::pow(2.0, std::max(v.mapa()->zoom(), 0)));
    verificar(QStringLiteral("y el mapa se centra en A (zoom %1, a menos de un píxel)").arg(v.mapa()->zoom()),
              v.mapa()->zoom() < 8 || v.mapa()->center().distanceTo(QGeoCoordinate(23.60, -82.70)) < 1.5 * pixelM);
    v.cajaB()->setText(QStringLiteral("23°30'00\"N 82°20'00\"O"));
    v.cajaB()->editingFinished();
    verificar(QStringLiteral("la coordenada sexagesimal con O de oeste también se entiende (B a 23,5; -82,3333)"),
              v.puntoB().distanceTo(QGeoCoordinate(23.5, -82.0 - 20.0 / 60.0)) < 2.0);
    const QGeoCoordinate antesTexto = v.puntoA();
    v.cajaA()->setText(QStringLiteral("esto no es una coordenada"));
    v.cajaA()->editingFinished();
    verificar(QStringLiteral("un texto que no es coordenada deja A donde estaba y pone la caja en rojo"),
              v.puntoA().distanceTo(antesTexto) < 0.5 && !v.cajaA()->styleSheet().isEmpty());
    v.alArrastrar(v.idA(), QGeoCoordinate(23.55, -82.65));
    verificar(QStringLiteral("al arrastrar A la caja vuelve a mostrar su coordenada (y se quita el rojo)"),
              v.cajaA()->styleSheet().isEmpty() && v.cajaA()->text().contains(QStringLiteral("23.55")));
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));
    v.fijarPunto(false, QGeoCoordinate(23.50, -82.38));

    // --- Ángulo de cierre a lo largo del perfil A–B: es la misma pregunta que lineOfSight ---
    QStringList incoherentes;
    for (double hB : { 10.0, 100.0, 300.0, 500.0, 700.0, 900.0 }) {
        v.ponerAlturas(10.0, hB);
        const AngulosPerfil ap = angulosPerfil(v.ultimoAnalisis(), 10.0, hB);
        if (!ap.valido || (ap.anguloB >= ap.cierreAntesB) != v.ultimoAnalisis().los.clear)
            incoherentes << QString::number(hB, 'f', 0);
    }
    v.ponerAlturas(10.0, 10.0);
    const AngulosPerfil ap = angulosPerfil(v.ultimoAnalisis(), 10.0, 10.0);
    double maxTerreno = -90.0;
    for (const QPointF &q : ap.terreno)
        maxTerreno = std::max(maxTerreno, q.y());
    bool monotono = !ap.cierre.isEmpty();
    for (int i = 1; i < ap.cierre.size(); ++i)
        monotono = monotono && ap.cierre[i].y() >= ap.cierre[i - 1].y();
    out << "      ángulos del perfil con B a 10 m: cierre antes de B " << QString::number(ap.cierreAntesB, 'f', 2)
        << "°, B a " << QString::number(ap.anguloB, 'f', 2) << "°\n";
    verificar(QStringLiteral("el ángulo de cierre del perfil decide igual que lineOfSight con B a 10, 100, 300, 500, 700 y 900 m%1")
                  .arg(incoherentes.isEmpty() ? QString() : QStringLiteral(" (discrepan: ") + incoherentes.join(QLatin1String(", ")) + QLatin1Char(')')),
              incoherentes.isEmpty());
    verificar(QStringLiteral("el ángulo de cierre solo sube (es el máximo acumulado) y acaba en el máximo del terreno"),
              monotono && std::abs(ap.cierre.last().y() - maxTerreno) < 1e-9 && ap.cierreAntesB > 0.5);
    verificar(QStringLiteral("con B a 10 m su ángulo queda por debajo del cierre (lo tapa la loma)"), ap.anguloB < ap.cierreAntesB);
    // Zoom automático: la ventana se centra en B y el cierre justo antes de B, y es mucho más estrecha que el rango completo.
    double zMin = 0.0, zMax = 0.0, tMin = 0.0, tMax = 0.0;
    ejeAngulos(ap, v.ultimoAnalisis().perfil.totalDistanceM, true, &zMin, &zMax);
    ejeAngulos(ap, v.ultimoAnalisis().perfil.totalDistanceM, false, &tMin, &tMax);
    out << "      eje de ángulos: con zoom " << QString::number(zMin, 'f', 2) << "° a " << QString::number(zMax, 'f', 2)
        << "°; completo " << QString::number(tMin, 'f', 2) << "° a " << QString::number(tMax, 'f', 2) << "°\n";
    verificar(QStringLiteral("con zoom el eje contiene a B y al cierre antes de B y nunca es más ancho que el completo (con B muy tapado, el zoom no ayuda y queda el completo)"),
              zMin < ap.anguloB && zMax > ap.anguloB && zMin < ap.cierreAntesB && zMax > ap.cierreAntesB
              && (zMax - zMin) <= (tMax - tMin) + 1e-9);
    // El caso de la captura del usuario: B apenas por encima del cierre (−18,12° frente a −18,27°). Con el zoom, el margen
    // de 0,15° ocupa al menos una octava parte del eje; sin él, una pequeña fracción.
    AngulosPerfil justo = ap;
    justo.anguloB = -18.12;
    justo.cierreAntesB = -18.27;
    justo.cierre.last().setY(-18.27);
    double jMin = 0.0, jMax = 0.0, jtMin = 0.0, jtMax = 0.0;
    ejeAngulos(justo, 3420.0, true, &jMin, &jMax);
    ejeAngulos(justo, 3420.0, false, &jtMin, &jtMax);
    verificar(QStringLiteral("con B 0,15° sobre el cierre, el margen ocupa %1 % del eje con zoom (y %2 % sin él)")
                  .arg(100.0 * 0.15 / (jMax - jMin), 0, 'f', 0).arg(100.0 * 0.15 / (jtMax - jtMin), 0, 'f', 1),
              0.15 / (jMax - jMin) > 0.1 && 0.15 / (jtMax - jtMin) < 0.1);
    verificar(QStringLiteral("el zoom está activo por omisión"), v.perfil()->zoomAngulos());
    v.perfil()->setZoomAngulos(false);
    v.perfil()->setZoomAngulos(true);
    v.perfil()->resize(900, 340);
    verificar(QStringLiteral("el widget dibuja la gráfica del ángulo de cierre (%1 píxeles de su azul)")
                  .arg(contarPixeles(v.perfil()->grab().toImage(), PerfilWidget::colorCierre())),
              contarPixeles(v.perfil()->grab().toImage(), PerfilWidget::colorCierre()) > 300);

    // --- Los 10 picos más altos en 10 km, sobre un terreno con 12 conos conocidos (400 ... 950 m) ---
    QTemporaryDir dirPicos;
    const QGeoCoordinate centroPicos(23.50, -82.50);
    verificar(QStringLiteral("se crea el terreno de 12 conos"), dirPicos.isValid() && crearTerrenoPicos(dirPicos.path(), centroPicos));
    v.mapa()->setElevationDir(dirPicos.path());
    v.fijarPunto(true, centroPicos);
    v.ponerAlturas(10.0, 10.0);
    v.ponerZona(20.0, 10.0);
    v.ponerRadioPicos(10.0);
    QElapsedTimer reloj;
    reloj.start();
    v.actualizarPicos();
    const qint64 msPicos = reloj.elapsed();
    const QVector<Pico> &picos = v.picos();
    out << "      picos: " << picos.size() << " en " << msPicos << " ms";
    for (const Pico &q : picos)
        out << " | " << QString::number(q.cota, 'f', 0) << " m a " << QString::number(q.distanciaM / 1000.0, 'f', 1) << " km, az "
            << QString::number(q.azimutDeg, 'f', 0) << "°, " << QString::number(q.anguloDeg, 'f', 1) << "°";
    out << "\n";
    bool descienden = true, alturasOk = picos.size() == 10, sitiosOk = picos.size() == 10, vistosOk = true;
    for (int k = 0; k < picos.size(); ++k) {
        descienden = descienden && (k == 0 || picos[k].cota <= picos[k - 1].cota);
        const int cerro = 11 - k;                        // 950 m es el cerro 11, 500 m el 2
        alturasOk = alturasOk && picos[k].cota <= 400.0 + 50.0 * cerro + 1.0 && picos[k].cota > 400.0 + 50.0 * cerro - 40.0;
        const double azEsperado = 30.0 * cerro;
        const double dAz = std::fmod(std::abs(picos[k].azimutDeg - azEsperado) + 360.0, 360.0);
        sitiosOk = sitiosOk && std::min(dAz, 360.0 - dAz) < 3.0 && std::abs(picos[k].distanciaM - 6000.0) < 250.0;
        vistosOk = vistosOk && picos[k].visible;
    }
    verificar(QStringLiteral("salen exactamente 10 picos, de más alto a más bajo"), picos.size() == 10 && descienden);
    verificar(QStringLiteral("son los 10 más altos (950 ... 500 m, con la punta del cono algo recortada por la malla); los de 450 y 400 m se quedan fuera"), alturasOk);
    verificar(QStringLiteral("cada pico está donde se puso: a 6 km y en su azimut"), sitiosOk);
    verificar(QStringLiteral("en la llanura todos se ven desde A (la punta de un cono se ve desde lejos)"), vistosOk);
    verificar(QStringLiteral("el ángulo del más alto es el de su cota sobre el ojo de A: ~8,9° (con la curvatura)"),
              !picos.isEmpty()
              && std::abs(picos.first().anguloDeg - std::atan2(picos.first().cota - 10.0 - 6000.0 * 6000.0 / (2.0 * kK * kRadioTierra),
                                                              6000.0) * 180.0 / M_PI) < 0.3
              && picos.first().anguloDeg > 8.0 && picos.first().anguloDeg < 9.5);
    verificar(QStringLiteral("la tabla tiene 10 filas y el mapa 10 marcas de pico"),
              v.tablaPicos()->rowCount() == 10 && v.mapa()->featuresInLayer(kCapaPicos).size() == 10);
    verificar(QStringLiteral("la búsqueda tarda menos de 2 s (%1 ms)").arg(msPicos), msPicos < 2000);
    v.ponerRadioPicos(5.0);
    v.actualizarPicos();
    verificar(QStringLiteral("con un radio de 5 km no hay ningún pico (los conos están a 6 km; el borde del círculo no cuenta)"),
              v.picos().isEmpty());
    v.ponerRadioPicos(10.0);
    v.actualizarPicos();
    v.ponerBEnPico(0);
    verificar(QStringLiteral("«poner B en el pico» lleva B a la cima más alta"),
              v.puntoB().distanceTo(v.picos().first().posicion) < 2.0);
    v.alArrastrar(v.idA(), centroPicos.atDistanceAndAzimuth(500.0, 0.0));
    verificar(QStringLiteral("al mover A la lista de picos se vacía (ya no son los suyos)"),
              v.picos().isEmpty() && v.tablaPicos()->rowCount() == 0 && v.mapa()->featuresInLayer(kCapaPicos).isEmpty());
    v.fijarPunto(true, centroPicos);

    // --- Ángulos de cierre en los 360° hasta el alcance ---
    v.ponerZona(20.0, 10.0);
    v.casillaMar()->setChecked(true);
    const ZonaVisible zc = v.calcularZonaVisible();
    out << "      cierre 360°: " << zc.cierres.size() << " azimuts; más cerrado " << QString::number(zc.cierreMaxDeg, 'f', 2)
        << "° (az " << QString::number(zc.cierreMaxAzimutDeg, 'f', 0) << "°), más despejado " << QString::number(zc.cierreMinDeg, 'f', 3)
        << "° (az " << QString::number(zc.cierreMinAzimutDeg, 'f', 0) << "°)\n";
    verificar(QStringLiteral("hay un ángulo de cierre por cada uno de los 360 azimuts"), zc.valida && zc.cierres.size() == 360);
    verificar(QStringLiteral("lo más cerrado es el cono de 950 m: ~8,9° hacia el azimut 330° (±3°)"),
              zc.cierreMaxDeg > 8.0 && zc.cierreMaxDeg < 9.5 && std::abs(zc.cierreMaxAzimutDeg - 330.0) < 3.0);
    const double azHueco = std::fmod(zc.cierreMinAzimutDeg + 15.0, 30.0);      // los huecos entre conos están en 15°, 45°, ...
    verificar(QStringLiteral("lo más despejado (%1°) es un hueco entre dos conos, a ~15° + 30°·n de azimut, con el horizonte hundido")
                  .arg(zc.cierreMinDeg, 0, 'f', 2),
              std::abs(zc.cierreMinDeg) < 0.2 && std::min(azHueco, 30.0 - azHueco) < 3.0);
    // Mar plano a 25 km al oeste de los conos, alcance 10 km: nada cierra el horizonte, que cae bajo 0° justo en el
    // alcance, a atan((-h - R²/(2·k·Rt)) / R) ≈ -0,091° (el hundimiento del horizonte: se ve «más allá» de la línea de 0°).
    v.fijarPunto(true, centroPicos.atDistanceAndAzimuth(25000.0, 270.0));
    v.ponerZona(10.0, 10.0);
    const ZonaVisible zm = v.calcularZonaVisible();
    const double dip = std::atan2(-10.0 - 1.0e8 / (2.0 * kK * kRadioTierra), 10000.0) * 180.0 / M_PI;
    out << "      mar abierto: cierre de " << QString::number(zm.cierreMinDeg, 'f', 3) << "° a " << QString::number(zm.cierreMaxDeg, 'f', 3)
        << "° (esperado " << QString::number(dip, 'f', 3) << "°)\n";
    verificar(QStringLiteral("sobre el mar abierto el ángulo de cierre es el del hundimiento del horizonte, ~%1°, en los 360 azimuts").arg(dip, 0, 'f', 3),
              zm.valida && std::abs(zm.cierreMinDeg - dip) < 0.01 && std::abs(zm.cierreMaxDeg - dip) < 0.01);
    v.ponerZona(20.0, 10.0);
    v.fijarPunto(true, centroPicos);
    v.calcularZonaVisible();
    verificar(QStringLiteral("el gráfico de los 360° recibe los ángulos y los dibuja (%1 píxeles de relleno)")
                  .arg(contarPixeles(v.graficoCierres()->grab().toImage(), CierresWidget::colorRelleno())),
              v.graficoCierres()->hayDatos() && (v.graficoCierres()->resize(900, 340), true)
              && contarPixeles(v.graficoCierres()->grab().toImage(), CierresWidget::colorRelleno()) > 3000);
    verificar(QStringLiteral("el texto de la zona incluye el ángulo de cierre"),
              v.textoZonaActual().contains(QStringLiteral("Ángulo de cierre")));
    v.borrarZonaVisible();
    verificar(QStringLiteral("borrar la zona vacía el gráfico de los 360°"), !v.graficoCierres()->hayDatos());
    // Con «mar = 0» apagado, el mismo terreno da el mismo horizonte (aquí el mar sí tiene cota 0 en el tile).
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));
    v.fijarPunto(false, QGeoCoordinate(23.50, -82.38));
    v.usarTerrenoDemostracion();
    v.ponerAlturas(10.0, 10.0);
    v.ponerZona(20.0, 2.0);

    // --- Relieve sombreado ---
    v.activarRelieve(true);
    verificar(QStringLiteral("con elevación se puede encender el relieve sombreado"), v.mapa()->isHillshadeVisible());
    v.activarRelieve(false);

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

// Arranca la ventana (o, con --comprobar, la prueba sin ventana; con --captura=, la deja calculada
// sobre el terreno de demostración y guarda una imagen).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("analisis_terreno"));

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

    // --captura=imagen.png: terreno de demostración, relieve y zona visible calculados; espera a las
    // teselas, guarda la ventana y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(strlen("--captura=")));
            ventana.usarTerrenoDemostracion();
            ventana.activarRelieve(true);
            ventana.calcularZonaVisible();
            QTimer::singleShot(3000, &app, [&ventana, &app, destino] {
                ventana.grab().save(destino);
                app.quit();
            });
        }
    }
    return app.exec();
}
