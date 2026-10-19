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
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
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

// Lo que dice la zona visible desde A: su superficie y sus alcances mayor y menor.
struct ZonaVisible {
    bool valida = false;
    int rayos = 0;
    double areaKm2 = 0.0;
    double porcentaje = 0.0;      // de la superficie del círculo de alcance
    double alcanceMaxM = 0.0;
    double azimutMaxDeg = 0.0;
    double alcanceMinM = 0.0;
    double azimutMinDeg = 0.0;
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
        { QObject::tr("Curvatura y refracción"),
          QObject::tr("La Tierra se abomba entre A y B y la atmósfera curva un poco los rayos hacia abajo. Se "
                      "calcula con un radio efectivo de 4/3 del de la Tierra (k = 4/3), que es la refracción "
                      "estándar.") },
        { QObject::tr("Zona visible"),
          QObject::tr("Hasta dónde se ve, desde A, un objetivo a la altura elegida, en cada dirección. Se calcula "
                      "con 360 rayos (viewshed). El dibujo es la zona ininterrumpida desde A: más allá de una "
                      "loma puede haber terreno visible que no se pinta.") },
        { QObject::tr("Alcance"),
          QObject::tr("La distancia máxima que se analiza desde A en cada dirección. Un alcance mayor tarda más.") },
        { QObject::tr("Azimut"),
          QObject::tr("La dirección en grados verdaderos: 0° norte, 90° este, 180° sur, 270° oeste.") },
        { QObject::tr("Relieve sombreado"),
          QObject::tr("Pintar el terreno como si le diera el sol desde una dirección, para ver las lomas y los "
                      "valles. Se calcula en vivo del modelo de elevación.") },
        { QObject::tr("Terreno de demostración"),
          QObject::tr("Un relieve inventado (una loma de 300 m en medio de una llanura suave) para probar la "
                      "aplicación sin datos reales.") },
    };
}

// Los términos que la ventana muestra tal cual (títulos de grupos, etiquetas y botones): cada uno
// debe estar definido en el glosario.
static QStringList terminosEnPantalla()
{
    return {
        QObject::tr("Observador (A)"), QObject::tr("Objetivo (B)"), QObject::tr("Altura de antena"),
        QObject::tr("Cota"), QObject::tr("Modelo de elevación (DEM)"), QObject::tr("Línea de vista"),
        QObject::tr("Holgura"), QObject::tr("Perfil de elevación"), QObject::tr("Curvatura y refracción"),
        QObject::tr("Zona visible"), QObject::tr("Alcance"), QObject::tr("Azimut"),
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
            const double cota = 10.0 + 20.0 * (lat - 23.0) + loma + cerro;
            qToBigEndian<qint16>(qint16(std::lround(cota)), p + (qint64(fila) * lado + col) * 2);
        }
    }
    QFile f(dir + QStringLiteral("/N23W083.hgt"));
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// Dibuja el perfil de elevación entre A y B con la recta de la visual encima. El terreno se dibuja
// «efectivo»: su cota más el abombamiento de la Tierra a esa distancia, d·(D−d)/(2·k·R), de modo que
// la visual es una recta y lo que la corta es justo lo que bloquea. El punto crítico (donde bloquea,
// o el de menor holgura) se marca con un círculo.
class PerfilWidget : public QWidget
{
public:
    // Color del relleno del terreno (opaco, para poder contarlo en las pruebas).
    static QColor colorTerreno() { return QColor(141, 110, 99); }

    explicit PerfilWidget(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(170); }

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
    // Dibuja el terreno efectivo, la visual, las antenas y el punto crítico, con los ejes rotulados.
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
        const QRectF zona = rect().adjusted(58, 12, -14, -22);
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
        p.drawText(QRectF(zona.left() + 6, zona.top(), zona.width() * 0.6, 16), Qt::AlignLeft,
                   tr("Terreno más la curvatura de la Tierra (k = 4/3): las cotas del eje son «efectivas»"));
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, zona.top() - 8, 54, 14), Qt::AlignRight, tr("%1 m").arg(qRound(maximo)));
        p.drawText(QRectF(0, zona.bottom() - 8, 54, 14), Qt::AlignRight, tr("%1 m").arg(qRound(minimo)));
        p.drawText(QRectF(zona.left(), zona.bottom() + 3, 40, 14), Qt::AlignLeft, QStringLiteral("A"));
        p.drawText(QRectF(zona.right() - 40, zona.bottom() + 3, 40, 14), Qt::AlignRight, QStringLiteral("B"));
        p.drawText(QRectF(zona.left(), zona.bottom() + 3, zona.width(), 14), Qt::AlignHCenter,
                   tr("%1 km").arg(D / 1000.0, 0, 'f', 2));
    }

private:
    Analisis m_a;
    double m_alturaA = 0.0;
    double m_alturaB = 0.0;
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
        m_mapa->addFeatureLayer(kCapaPuntos, tr("Puntos"), kOrdenCapa);
        m_mapa->setFeatureLayerTransient(kCapaPuntos);

        crearPaneles();
        crearMenus();
        conectarSenales();
        crearPuntos(m_mapa->center());
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
        analizar();
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
        if (id == m_idA)
            m_a = posicion;
        else if (id == m_idB)
            m_b = posicion;
        else
            return;
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
            a.perfil = m_mapa->elevationProfile({ m_a, m_b }, pp);
            LineOfSightParams lp;
            lp.stepMeters = paso;
            a.los = m_mapa->lineOfSight(m_a, m_b, m_alturaA->value(), m_alturaB->value(), lp);
            a.cotaA = m_mapa->elevationAt(m_a);
            a.cotaB = m_mapa->elevationAt(m_b);
            if (!a.los.isValid())
                mensaje = tr("Sin cota en A, en B o en parte del trayecto: no se puede decidir. Mueve los puntos "
                             "a una zona con modelo de elevación.");
        }
        m_ultimo = a;

        m_coordenadas->setText(tr("A: %1  ·  cota %2\nB: %3  ·  cota %4\nDistancia: %5 km")
            .arg(formatCoordinate(m_a, CoordinateFormat::Decimal), textoCota(a.cotaA),
                 formatCoordinate(m_b, CoordinateFormat::Decimal), textoCota(a.cotaB))
            .arg(a.distanciaM / 1000.0, 0, 'f', 2));
        m_textoVisual->setText(textoVisual(a));
        m_perfil->setDatos(a, m_alturaA->value(), m_alturaB->value(), mensaje);
    }

    // La menor altura de antena de B (metros sobre el terreno, redondeada hacia arriba al metro) con la
    // que A ve B, manteniendo la altura de A, o -1 si ni con 5000 m se vería. Es una bisección sobre
    // lineOfSight: la visibilidad solo mejora al subir la antena.
    double alturaMinimaB() const
    {
        const auto ve = [this](double alturaB) {
            const LineOfSightResult r = m_mapa->lineOfSight(m_a, m_b, m_alturaA->value(), alturaB);
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

        // Una cuña por azimut: hasta donde llega la zona ininterrumpida desde A. Los puntos van de
        // azimut en azimut, así que forman un polígono en estrella alrededor de A.
        QVector<QGeoCoordinate> contorno;
        z.alcanceMaxM = -1.0;
        z.alcanceMinM = std::numeric_limits<double>::max();
        for (const ViewshedRay &r : vs.rays) {
            contorno << m_a.atDistanceAndAzimuth(r.visibilityReachM, r.azimuthDeg);
            if (r.visibilityReachM > z.alcanceMaxM) {
                z.alcanceMaxM = r.visibilityReachM;
                z.azimutMaxDeg = r.azimuthDeg;
            }
            if (r.visibilityReachM < z.alcanceMinM) {
                z.alcanceMinM = r.visibilityReachM;
                z.azimutMinDeg = r.azimuthDeg;
            }
        }
        z.rayos = int(vs.rays.size());
        z.areaKm2 = polygonArea(contorno) / 1.0e6;
        z.porcentaje = 100.0 * z.areaKm2 / (M_PI * std::pow(vp.maxRangeM / 1000.0, 2.0));
        z.valida = true;

        MapFeature poligono;
        poligono.kind = GeometryKind::Polygon;
        poligono.layerId = kCapaZona;
        poligono.geometry = contorno;
        poligono.selectable = false;
        poligono.style.lineColor = QColor(46, 125, 50);
        poligono.style.fillColor = QColor(46, 125, 50, 70);
        poligono.style.lineWidth = 2.0;
        poligono.style.labelVisible = false;
        m_idZona = m_mapa->addFeature(poligono);
        m_ultimaZona = z;
        m_textoZona->setText(tr("Se ve el %1 % del círculo de %2 km (%3 km²).\nMayor alcance: %4 km al azimut %5°.\n"
                                "Menor alcance: %6 km al azimut %7°.")
            .arg(z.porcentaje, 0, 'f', 0).arg(m_alcance->value(), 0, 'f', 0).arg(z.areaKm2, 0, 'f', 0)
            .arg(z.alcanceMaxM / 1000.0, 0, 'f', 1).arg(qRound(z.azimutMaxDeg))
            .arg(z.alcanceMinM / 1000.0, 0, 'f', 1).arg(qRound(z.azimutMinDeg)));
        return z;
    }

    // Quita del mapa la zona visible dibujada, si la hay.
    void borrarZonaVisible()
    {
        if (m_idZona >= 0)
            m_mapa->removeFeature(m_idZona);
        m_idZona = -1;
        m_ultimaZona = ZonaVisible();
        m_textoZona->setText(tr("Elige el alcance y pulsa «Calcular zona visible»."));
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
    QListWidget *listaGlosario() const { return m_listaGlosario; }

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
        colElevacion->addWidget(m_origenElevacion);
        colElevacion->addWidget(botonDemo);
        colElevacion->addWidget(m_cota);
        col->addWidget(grupoElevacion);

        auto *grupoPuntos = new QGroupBox(tr("Observador (A) y objetivo (B)"), this);
        auto *form = new QFormLayout(grupoPuntos);
        m_alturaA = cajaMetros(0.0, 5000.0, 10.0, tr(" m"));
        m_alturaB = cajaMetros(0.0, 5000.0, 10.0, tr(" m"));
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

        auto *dockPuntos = new QDockWidget(tr("Análisis"), this);
        dockPuntos->setWidget(panel);
        addDockWidget(Qt::LeftDockWidgetArea, dockPuntos);
        dockPuntos->setMinimumWidth(330);

        m_perfil = new PerfilWidget(this);
        auto *dockPerfil = new QDockWidget(tr("Perfil de elevación"), this);
        dockPerfil->setWidget(m_perfil);
        addDockWidget(Qt::BottomDockWidgetArea, dockPerfil);

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
            m_cota->setText(tr("Cota bajo el cursor: %1").arg(textoCota(tieneElevacion() ? m_mapa->elevationAt(pos)
                                                                                         : std::numeric_limits<double>::quiet_NaN())));
        });
    }

    MapWidget *m_mapa = nullptr;
    PerfilWidget *m_perfil = nullptr;
    QDoubleSpinBox *m_alturaA = nullptr;
    QDoubleSpinBox *m_alturaB = nullptr;
    QDoubleSpinBox *m_alcance = nullptr;
    QDoubleSpinBox *m_alturaObjetivo = nullptr;
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
    qint64 m_idA = -1, m_idB = -1, m_idZona = -1;
    bool m_pidiendoA = true;
    bool m_cancelar = false;
    Analisis m_ultimo;
    ZonaVisible m_ultimaZona;
};

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
    out << "      zona visible: " << z.rayos << " rayos, " << QString::number(z.areaKm2, 'f', 0) << " km² ("
        << QString::number(z.porcentaje, 'f', 0) << " %), alcance " << QString::number(z.alcanceMinM / 1000.0, 'f', 1) << ".."
        << QString::number(z.alcanceMaxM / 1000.0, 'f', 1) << " km, mín. al azimut " << QString::number(z.azimutMinDeg, 'f', 0) << "°\n";
    verificar(QStringLiteral("la zona visible se calcula con 360 rayos"), z.valida && z.rayos == 360);
    verificar(QStringLiteral("el menor alcance (~12 km) es hacia el este, donde está la loma"),
              z.alcanceMinM > 11000.0 && z.alcanceMinM < 13500.0 && z.azimutMinDeg > 80.0 && z.azimutMinDeg < 100.0);

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
    verificar(QStringLiteral("la zona queda dibujada en el mapa"), v.mapa()->featuresInLayer(kCapaZona).size() == 1);
    v.borrarZonaVisible();
    verificar(QStringLiteral("borrar la zona la quita del mapa"), v.mapa()->featuresInLayer(kCapaZona).isEmpty());

    // --- Un punto fuera de los datos: no se inventa nada ---
    v.fijarPunto(true, QGeoCoordinate(23.50, -84.50));
    verificar(QStringLiteral("con A fuera del terreno con datos la visual no es válida y lo dice"),
              !v.ultimoAnalisis().los.isValid() && v.textoVisualActual().contains(QStringLiteral("Sin dato")));
    verificar(QStringLiteral("con A fuera de los datos tampoco hay zona visible"), !v.calcularZonaVisible().valida);
    v.fijarPunto(true, QGeoCoordinate(23.50, -82.62));

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
