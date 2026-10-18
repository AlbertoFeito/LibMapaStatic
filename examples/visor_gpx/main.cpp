// visor_gpx - visor de rutas y tracks (GPX, KML, GeoJSON) con libmapa.
//
// Tercer ejemplo de la serie; se lee de arriba abajo. Enseña lo que necesita
// cualquier visor de recorridos (senderismo, post-análisis de misiones):
//   - Abrir un fichero vectorial como capa (importVectorFile) y encuadrarlo.
//   - Inicio y fin de cada ruta marcados (setRouteEndpoints).
//   - Ruta seleccionada (la primera al abrir, o la que se pinche): su longitud y,
//     si el paquete trae elevación, su perfil con ascenso y descenso
//     (elevationProfile). Las capas del paquete (provincias, curvas de nivel)
//     no le quitan la selección: la capa de rutas va por encima de ellas.
//   - Si el GPX trae horas, reproducirlo en el tiempo (TrackPlayer).
//   - Exportar la vista a PDF o imagen (exportView).
//
//   visor_gpx [paquete de datos] [fichero.gpx|.kml|.geojson]   (paquete por defecto: <exe>/datos)
//   visor_gpx <paquete> <fichero> --comprobar
//        -> sin ventana: dice qué ha leído (y el perfil de la primera ruta, si el
//           paquete trae elevación) y sale con 0 si todo está bien.
//   visor_gpx <paquete> <fichero> --captura=imagen.png
//        -> abre la ventana con la primera ruta seleccionada, guarda una imagen y sale.

#include <libmapa/Coordinates.h>
#include <libmapa/MapWidget.h>
#include <libmapa/TrackPlayer.h>

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>

using namespace libmapa;

namespace {

// Capa donde se carga el fichero abierto (se reemplaza al abrir otro).
const QString kCapa = QStringLiteral("fichero");

// Orden de dibujo de esa capa: por encima de las capas del paquete (provincias,
// curvas de nivel...), que traen entidades seleccionables. Si quedara debajo, un
// clic dentro de una provincia la seleccionaría a ella y no a la ruta.
const int kOrdenCapa = 1000;

// Hora para mostrar: UTC, que es en la que viajan los GPX.
QString textoHora(const QDateTime &t)
{
    return t.toUTC().toString(QStringLiteral("dd/MM HH:mm:ss 'UTC'"));
}

} // namespace

// Gráfica sencilla del perfil de elevación de la ruta seleccionada: distancia
// en horizontal, cota en vertical. Sin datos de elevación dice por qué.
class PerfilWidget : public QWidget
{
public:
    explicit PerfilWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(110);
    }

    // Cambia el perfil a dibujar y repinta. Si no hay datos que dibujar, se
    // muestra \a mensaje en su lugar (por qué falta el perfil).
    void setPerfil(const ElevationProfile &perfil, const QString &mensaje = QString())
    {
        m_perfil = perfil;
        m_mensaje = mensaje;
        update();
    }

protected:
    // Dibuja el área bajo la curva con las cotas mínima y máxima rotuladas.
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().base());
        const bool hayDatos = m_perfil.isValid() && std::isfinite(m_perfil.minElevation)
                              && std::isfinite(m_perfil.maxElevation);
        if (!hayDatos) {
            p.setPen(palette().color(QPalette::Mid));
            p.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap,
                       m_mensaje.isEmpty() ? tr("Pincha una ruta para ver su perfil de elevación")
                                           : m_mensaje);
            return;
        }
        const QRectF zona = rect().adjusted(54, 8, -10, -18);
        const double dMax = std::max(m_perfil.totalDistanceM, 1.0);
        const double rango = std::max(m_perfil.maxElevation - m_perfil.minElevation, 1.0);
        QPainterPath curva;
        bool primero = true;
        for (const ElevationSample &s : m_perfil.samples) {
            if (!std::isfinite(s.elevation))
                continue;
            const QPointF q(zona.left() + zona.width() * s.distanceM / dMax,
                            zona.bottom() - zona.height() * (s.elevation - m_perfil.minElevation) / rango);
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
        p.fillPath(area, QColor(46, 125, 50, 70));
        p.setPen(QPen(QColor(46, 125, 50), 1.6));
        p.drawPath(curva);
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, zona.top() - 6, 50, 14), Qt::AlignRight, tr("%1 m").arg(qRound(m_perfil.maxElevation)));
        p.drawText(QRectF(0, zona.bottom() - 8, 50, 14), Qt::AlignRight, tr("%1 m").arg(qRound(m_perfil.minElevation)));
        p.drawText(QRectF(zona.left(), zona.bottom() + 2, zona.width(), 14), Qt::AlignRight,
                   tr("%1 km").arg(m_perfil.totalDistanceM / 1000.0, 0, 'f', 2));
    }

private:
    ElevationProfile m_perfil;
    QString m_mensaje;
};

// Ventana con el mapa, la barra de reproducción (solo si el GPX trae horas) y
// el perfil de elevación debajo.
class Ventana : public QMainWindow
{
public:
    // Monta el mapa sobre el paquete de datos y los menús sobre el mapa.
    explicit Ventana(const QString &datos)
    {
        MapConfig cfg;
        cfg.dataDir = datos;
        m_mapa = new MapWidget(cfg, this);
        m_mapa->setScaleBarVisible(true);
        m_mapa->setRouteEndpoints(RouteEndpoints::Routes);   // inicio verde, fin rojo

        auto *centro = new QWidget(this);
        auto *capas = new QVBoxLayout(centro);
        capas->setContentsMargins(0, 0, 0, 0);
        capas->setSpacing(0);
        capas->addWidget(m_mapa, 1);
        capas->addWidget(crearTransporte());
        m_perfil = new PerfilWidget(this);
        capas->addWidget(m_perfil);
        setCentralWidget(centro);
        resize(1100, 800);

        QMenu *archivo = menuBar()->addMenu(tr("&Archivo"));
        connect(archivo->addAction(tr("&Abrir ruta...")), &QAction::triggered, this, [this] {
            const QString ruta = QFileDialog::getOpenFileName(
                this, tr("Abrir ruta"), QString(), tr("Rutas (*.gpx *.kml *.geojson *.json)"));
            if (!ruta.isEmpty())
                abrir(ruta);
        });
        connect(archivo->addAction(tr("&Exportar vista...")), &QAction::triggered, this, [this] {
            exportar();
        });

        // Al pinchar una ruta: su longitud y su perfil.
        connect(m_mapa, &MapWidget::featureSelected, this, [this](qint64 id) { alSeleccionar(id); });

        m_player = new TrackPlayer(m_mapa, this);
        connect(m_player, &TrackPlayer::timeChanged, this, [this](const QDateTime &t) { avanzarReloj(t); });
        connect(m_player, &TrackPlayer::playingChanged, this, [this](bool on) {
            m_play->setText(on ? tr("⏸ Pausa") : tr("▶ Reproducir"));
        });
        m_transporte->setVisible(false);

        if (!m_mapa->isReady())
            QMessageBox::critical(this, tr("Mapa"), m_mapa->lastError());
    }

    // Abre un GPX/KML/GeoJSON como capa, la encuadra y, si el GPX trae horas,
    // prepara su reproducción. Devuelve false (con aviso) si no se puede leer.
    bool abrir(const QString &ruta)
    {
        QString error;
        m_player->clear();                       // quita lo del fichero anterior
        m_mapa->removeFeatureLayer(kCapa);       // un fichero cada vez
        m_rutaActual = -1;
        m_mapa->addFeatureLayer(kCapa, QFileInfo(ruta).fileName(), kOrdenCapa);
        if (m_mapa->importVectorFile(ruta, kCapa, QFileInfo(ruta).fileName(), &error) < 0) {
            statusBar()->showMessage(tr("No se pudo abrir: %1").arg(error));
            return false;
        }
        m_ruta = ruta;
        setWindowTitle(tr("Visor de rutas - %1").arg(QFileInfo(ruta).fileName()));
        encuadrar();
        m_perfil->setPerfil(ElevationProfile());

        // Un GPX con horas se puede reproducir: aparece la barra de tiempo.
        m_conHoras = ruta.endsWith(QLatin1String(".gpx"), Qt::CaseInsensitive)
                     && m_player->loadFile(ruta);
        m_transporte->setVisible(m_conHoras);
        if (m_conHoras) {
            m_tiempo->setRange(0, int(m_player->startTime().secsTo(m_player->endTime())));
            m_tiempo->setValue(0);
            avanzarReloj(m_player->startTime());
        }
        statusBar()->showMessage(tr("%1: %2 entidades, %3 km en rutas")
            .arg(QFileInfo(ruta).fileName()).arg(numEntidades()).arg(kmEnRutas(), 0, 'f', 2));
        seleccionarPrimeraRuta();     // que el perfil salga de entrada (luego se pincha otra)
        return true;
    }

    // Entidades de la capa abierta.
    int numEntidades() const { return int(m_mapa->featuresInLayer(kCapa).size()); }

    // Longitud sumada de todas las líneas de la capa abierta, en km.
    double kmEnRutas() const
    {
        double total = 0.0;
        for (const MapFeature &f : m_mapa->featuresInLayer(kCapa))
            if (f.kind == GeometryKind::Polyline)
                for (const auto &parte : f.outlines())
                    total += pathLength(parte);
        return total / 1000.0;
    }

    // Selecciona la primera línea de la capa abierta, como si se pinchara (así
    // salen su longitud y su perfil). Devuelve su perfil, o uno vacío si no hay.
    ElevationProfile seleccionarPrimeraRuta()
    {
        for (const MapFeature &f : m_mapa->featuresInLayer(kCapa))
            if (f.kind == GeometryKind::Polyline && f.outlines().size() > 0
                && f.outlines().first().size() > 1) {
                m_mapa->selectFeature(f.id);
                return m_mapa->elevationProfile(f.outlines().first());
            }
        return ElevationProfile();
    }

    // ¿El fichero abierto trae horas (se puede reproducir)?
    bool conHoras() const { return m_conHoras; }
    MapWidget *mapa() const { return m_mapa; }

private:
    // Barra de reproducción: play/pausa, deslizador de tiempo, hora y velocidad.
    QWidget *crearTransporte()
    {
        m_transporte = new QWidget(this);
        auto *fila = new QHBoxLayout(m_transporte);
        fila->setContentsMargins(6, 3, 6, 3);
        m_play = new QPushButton(tr("▶ Reproducir"), m_transporte);
        m_tiempo = new QSlider(Qt::Horizontal, m_transporte);
        m_hora = new QLabel(m_transporte);
        m_hora->setMinimumWidth(130);
        auto *velocidad = new QComboBox(m_transporte);
        for (int v : { 1, 10, 60, 300, 1800 })
            velocidad->addItem(QStringLiteral("×%1").arg(v), v);
        velocidad->setCurrentIndex(2);
        fila->addWidget(m_play);
        fila->addWidget(m_tiempo, 1);
        fila->addWidget(m_hora);
        fila->addWidget(velocidad);

        connect(m_play, &QPushButton::clicked, this, [this] {
            if (m_player->isPlaying())
                m_player->pause();
            else
                m_player->play();
        });
        // El usuario arrastra el deslizador: se va a esa hora.
        connect(m_tiempo, &QSlider::sliderMoved, this, [this](int s) {
            m_player->seek(m_player->startTime().addSecs(s));
        });
        connect(velocidad, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, velocidad](int i) {
            m_player->setSpeed(velocidad->itemData(i).toDouble());
        });
        return m_transporte;
    }

    // El reproductor avanzó: mueve el deslizador (sin disparar su señal) y la hora.
    void avanzarReloj(const QDateTime &t)
    {
        const QSignalBlocker bloqueo(m_tiempo);
        m_tiempo->setValue(int(m_player->startTime().secsTo(t)));
        m_hora->setText(textoHora(t));
    }

    // Encuadra el mapa sobre todo lo de la capa abierta (o centra si es un punto).
    void encuadrar()
    {
        double norte = -90, sur = 90, este = -180, oeste = 180;
        bool hay = false;
        for (const MapFeature &f : m_mapa->featuresInLayer(kCapa))
            for (const auto &parte : f.outlines())
                for (const QGeoCoordinate &c : parte) {
                    norte = std::max(norte, c.latitude());
                    sur = std::min(sur, c.latitude());
                    este = std::max(este, c.longitude());
                    oeste = std::min(oeste, c.longitude());
                    hay = true;
                }
        if (!hay)
            return;
        if (norte - sur < 1e-5 && este - oeste < 1e-5) {
            m_mapa->setCenter(QGeoCoordinate(norte, este));
            m_mapa->setZoom(14);
            return;
        }
        m_mapa->fitBounds(QGeoCoordinate(norte, oeste), QGeoCoordinate(sur, este));
    }

    // Cambió la selección del mapa. Solo cuentan las entidades del fichero abierto:
    // pinchar una provincia, unas curvas de nivel o el fondo no quita la ruta
    // seleccionada (se vuelve a marcar), así que su perfil no desaparece.
    void alSeleccionar(qint64 id)
    {
        const auto f = id >= 0 ? m_mapa->feature(id) : std::nullopt;
        if (f && f->layerId == kCapa) {
            mostrarRuta(*f);
            return;
        }
        if (m_rutaActual >= 0 && id != m_rutaActual)
            m_mapa->selectFeature(m_rutaActual);
    }

    // Una entidad del fichero quedó seleccionada: si es una línea, longitud y perfil.
    void mostrarRuta(const MapFeature &entidad)
    {
        const std::optional<MapFeature> f = entidad;
        if (f->kind != GeometryKind::Polyline || f->outlines().isEmpty()) {
            m_rutaActual = -1;
            m_perfil->setPerfil(ElevationProfile(),
                                tr("«%1» no es una línea: el perfil es solo de rutas y tracks").arg(f->name));
            return;
        }
        m_rutaActual = f->id;
        const QVector<QGeoCoordinate> camino = f->outlines().first();
        const ElevationProfile perfil = m_mapa->elevationProfile(camino);
        const bool conCotas = perfil.isValid() && std::isfinite(perfil.minElevation);
        m_perfil->setPerfil(perfil, conCotas ? QString()
            : tr("Sin datos de elevación para esta ruta: el paquete no trae elevación "
                 "(\"elevation\" en mapa.json) o la ruta cae fuera de la zona que cubre."));
        QString texto = tr("%1: %2 km, %3 puntos")
            .arg(f->name.isEmpty() ? tr("Ruta") : f->name)
            .arg(pathLength(camino) / 1000.0, 0, 'f', 2).arg(camino.size());
        if (conCotas)
            texto += tr(" · ↑ %1 m ↓ %2 m · cotas %3–%4 m").arg(qRound(perfil.gain))
                         .arg(qRound(perfil.loss)).arg(qRound(perfil.minElevation))
                         .arg(qRound(perfil.maxElevation));
        statusBar()->showMessage(texto);
    }

    // Exporta la vista tal cual se ve, con el nombre del fichero como título.
    void exportar()
    {
        const QString destino = QFileDialog::getSaveFileName(
            this, tr("Exportar vista"), QString(), tr("PDF (*.pdf);;Imagen (*.png *.jpg)"));
        if (destino.isEmpty())
            return;
        MapExportOptions o;
        o.title = m_ruta.isEmpty() ? tr("Rutas") : QFileInfo(m_ruta).baseName();
        o.attribution = m_mapa->packageInfo().attribution;
        QString error;
        if (m_mapa->exportView(destino, o, &error))
            statusBar()->showMessage(tr("Vista exportada a %1").arg(destino), 6000);
        else
            QMessageBox::warning(this, tr("Exportar"), error);
    }

    MapWidget *m_mapa = nullptr;
    TrackPlayer *m_player = nullptr;
    PerfilWidget *m_perfil = nullptr;
    QWidget *m_transporte = nullptr;
    QPushButton *m_play = nullptr;
    QSlider *m_tiempo = nullptr;
    QLabel *m_hora = nullptr;
    QString m_ruta;
    qint64 m_rutaActual = -1;      //!< Ruta del fichero seleccionada (-1 = ninguna).
    bool m_conHoras = false;
};

// ¿Parece un fichero de rutas por su extensión?
static bool esFicheroDeRutas(const QString &arg)
{
    for (const char *ext : { ".gpx", ".kml", ".geojson", ".json" })
        if (arg.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
            return true;
    return false;
}

// Arranca la ventana (o, con --comprobar, solo informa por consola).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("visor_gpx"));

    QStringList posicionales;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith(QLatin1String("--")))
            posicionales << a;
    const bool comprobar = app.arguments().contains(QStringLiteral("--comprobar"));

    // Un solo argumento con extensión de ruta = el fichero (paquete por defecto).
    QString datos = QCoreApplication::applicationDirPath() + QStringLiteral("/datos");
    QString fichero;
    if (posicionales.size() == 1 && esFicheroDeRutas(posicionales.first())) {
        fichero = posicionales.first();
    } else {
        if (!posicionales.isEmpty())
            datos = posicionales.first();
        if (posicionales.size() > 1)
            fichero = posicionales.at(1);
    }

    Ventana ventana(datos);
    if (!fichero.isEmpty())
        ventana.abrir(fichero);

    if (comprobar) {
        QTextStream out(stdout);
        const bool listo = ventana.mapa()->isReady();
        out << "listo: " << (listo ? "si" : "no") << "\n";
        out << "entidades: " << ventana.numEntidades() << "\n";
        out << "km en rutas: " << QString::number(ventana.kmEnRutas(), 'f', 2) << "\n";
        out << "con horas: " << (ventana.conHoras() ? "si" : "no") << "\n";
        const ElevationProfile perfil = ventana.seleccionarPrimeraRuta();
        const bool conCotas = perfil.isValid() && std::isfinite(perfil.minElevation);
        out << "perfil de elevacion: " << (conCotas ? "si" : "no (el paquete no trae elevacion, o no cubre la ruta)") << "\n";
        if (conCotas)
            out << "ascenso/descenso: " << qRound(perfil.gain) << " / " << qRound(perfil.loss)
                << " m, cotas " << qRound(perfil.minElevation) << " a " << qRound(perfil.maxElevation) << " m\n";
        return (listo && ventana.numEntidades() > 0) ? 0 : 1;
    }
    ventana.show();

    // --captura=imagen.png: espera a que lleguen las teselas, guarda la ventana y sale.
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QLatin1String("--captura="))) {
            const QString destino = a.mid(int(strlen("--captura=")));
            QTimer::singleShot(2500, &app, [&ventana, &app, destino] {
                ventana.grab().save(destino);
                app.quit();
            });
        }
    }
    return app.exec();
}
