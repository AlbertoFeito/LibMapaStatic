#include "LibMapaStatic.h"
#include <QFile>
#include <QDebug>

LibMapaStatic::LibMapaStatic(const QString& path, QWidget *parent)
    : QWidget(parent)
    , oMapa(nullptr)
    , DirPath(path)
    , gridla(nullptr)
    , m_hLayoutMapa(nullptr)
    , m_tb_medirDistancia(nullptr)
    , m_tb_nuevoPoligono(nullptr)
    , m_tb_nuevoPunto(nullptr)
    , m_tb_zoomArea(nullptr)
    , m_tb_trazaRuta(nullptr)
    , m_tb_zMas(nullptr)
    , m_tb_zMenos(nullptr)
    , menuClickDerecho(nullptr)
    , m_l_azimut(nullptr)
    , m_l_distancia(nullptr)
    , m_l_geoPosMouse(nullptr)
    , m_le_azimut(nullptr)
    , m_le_distancia(nullptr)
    , m_le_geoPosMouse(nullptr)
    , m_l_cantPuntos(nullptr)
    , m_sb_cantPuntos(nullptr)
    , tim(nullptr)
    , b_seCambioCapa(false)
{
    // Verificar que el path existe
    if (!QDir(DirPath).exists()) {
        qWarning() << "Directorio no existe:" << DirPath;
        return;
    }

    try {
        oMapa = new CMapaPlot(DirPath, this);
        oMapa->setCursor(Qt::CrossCursor);

        // Verificar recursos antes de usarlos
        QString lineaDistanciaIcon = DirPath + "/Recursos/Iconos/regla.png";
        if (verificarRecursosExisten(lineaDistanciaIcon)) {
            oMapa->getLineaDistancia()->setPen(QPen(QColor(Qt::darkBlue)));
        }

        // Conexiones con sintaxis moderna
        connect(oMapa, &CMapaPlot::sig_enviaMedicionAzimuthDistancia,
                this, &LibMapaStatic::mostrarMedicionDistancia);
        connect(oMapa, &CMapaPlot::sig_enviaPosActual,
                this, &LibMapaStatic::mostrarPosActualMouse);
        connect(oMapa, &CMapaPlot::sig_devuelveWidgetClickDerechoParaDisenoExterno,
                this, &LibMapaStatic::devuelveWidgetClickDerechoParaDisenoExterno);
        connect(oMapa, &CMapaPlot::sig_finZoomArea,
                this, &LibMapaStatic::terminoZoomArea);

        inicializarComponentesVisuales();

        // Crear widget del menú click derecho
        menuClickDerecho = new QDialog(this);
        menuClickDerecho->setObjectName("menuClickDerecho");

    } catch (const std::exception& e) {
        qCritical() << "Error inicializando LibMapaStatic:" << e.what();
        limpiarRecursos();
    }
}

LibMapaStatic::~LibMapaStatic()
{
    limpiarRecursos();
}

void LibMapaStatic::limpiarRecursos()
{
    // Detener y eliminar timer
    if (tim) {
        tim->stop();
        delete tim;
        tim = nullptr;
    }

    // oMapa se elimina automáticamente al ser hijo de this
    // menuClickDerecho se elimina automáticamente al ser hijo de this

    // Los demás widgets son hijos de oMapa o this, se eliminan automáticamente
}

bool LibMapaStatic::verificarRecursosExisten(const QString& recursoPath)
{
    if (!QFile::exists(recursoPath)) {
        qWarning() << "Recurso no encontrado:" << recursoPath;
        return false;
    }
    return true;
}

void LibMapaStatic::inicializarComponentesVisuales()
{
    if (!oMapa) return;

    // Layout principal
    gridla = new QVBoxLayout(this);
    gridla->setContentsMargins(2, 2, 2, 2);
    setLayout(gridla);

    m_hLayoutMapa = new QHBoxLayout();
    m_hLayoutMapa->setContentsMargins(0, 0, 0, 0);
    m_hLayoutMapa->addWidget(oMapa);

    // Crear herramientas gráficas
    crearBotonesHerramientas();
    crearBarraEstado();
    crearBotonesZoom();
    configurarConexiones();

    gridla->addLayout(m_hLayoutMapa);
}

void LibMapaStatic::crearBotonesHerramientas()
{
    QFont fontBold;
    fontBold.setBold(true);

    // Botón medir distancia
    m_tb_medirDistancia = crearBotonHerramienta(
        "Medir distancia entre dos puntos.",
        DirPath + "/Recursos/Iconos/regla.png",
        QPoint(10, 10),
        "Distancia"
        );

    // Botón zoom área
    m_tb_zoomArea = crearBotonHerramienta(
        "Hacer Zoom Area en una zona específica.",
        DirPath + "/Recursos/Iconos/zoomArea.png",
        QPoint(50, 10),
        "ZoomArea"
        );

    // Botón crear polígonos
    m_tb_nuevoPoligono = crearBotonHerramienta(
        "Dibujar Zona de Interés.",
        DirPath + "/Recursos/Iconos/poligono.png",
        QPoint(90, 10),
        "Poligono"
        );

    // Botón insertar punto
    m_tb_nuevoPunto = crearBotonHerramienta(
        "Insertar nuevo Punto",
        DirPath + "/Recursos/Iconos/punto.png",
        QPoint(150, 10),
        "Punto"
        );

    // Botón trazar ruta
    m_tb_trazaRuta = crearBotonHerramienta(
        "Trazar Ruta",
        DirPath + "/Recursos/Iconos/ruta.png",
        QPoint(190, 10),
        "Ruta"
        );
    m_tb_trazaRuta->setCheckable(true);
}

QToolButton* LibMapaStatic::crearBotonHerramienta(const QString& toolTip,
                                                  const QString& iconPath,
                                                  const QPoint& position,
                                                  const QString& text)
{
    auto button = new QToolButton(oMapa);
    button->setToolTip(toolTip);
    button->setCheckable(true);

    // Aplicar estilo consistente
    QString styleSheet =
        "QToolButton::checked{ background-color: #aaffcc;}"
        "QToolButton::hover{background-color: #ffaf5d;}"
        "QToolButton::pressed{ background-color: #aaffcc;}"
        "QToolButton{background-color: qlineargradient(spread:pad, x1:1, y1:0, x2:1, y2:1,"
        "stop:0 rgba(82, 82, 82, 100),stop:0.995192 rgba(75, 75, 75, 100));"
        "color: #fff;border: 1px solid #777777;border-radius: 10px;padding: 2px;}";

    button->setStyleSheet(styleSheet);

    // Configurar ícono si existe
    if (verificarRecursosExisten(iconPath)) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(30, 30));
    } else {
        qWarning() << "Icono no disponible para botón:" << text;
    }

    button->setGeometry(position.x(), position.y(), 30, 30);
    button->setText(text);

    return button;
}

void LibMapaStatic::crearBarraEstado()
{
    // Posición actual del mouse
    m_le_geoPosMouse = new QLineEdit(oMapa);
    m_le_geoPosMouse->setGeometry(10, oMapa->height() + 60, 200, 30);
    m_le_geoPosMouse->setStyleSheet(
        "color: rgb(0, 0, 127);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 45), stop:0.05 rgba(30, 18, 167, 45),"
        "stop:0.36 rgba(31, 19, 166, 45), stop:0.6 rgba(129, 122, 129, 45),"
        "stop:0.75 rgba(54, 70, 170, 45), stop:0.797753 rgba(86, 94, 244, 45),"
        "stop:0.86 rgba(49, 75, 193, 45), stop:0.935 rgba(11, 20, 184, 45));"
        );
    m_le_geoPosMouse->setAlignment(Qt::AlignCenter);
    m_le_geoPosMouse->setReadOnly(true);

    // Azimut
    m_l_azimut = new QLabel("Rumbo", oMapa);
    m_l_azimut->setGeometry(220, oMapa->height() + 60, 50, 30);
    aplicarEstiloLabelBarraEstado(m_l_azimut);
    m_l_azimut->setVisible(false);

    m_le_azimut = new QLineEdit(oMapa);
    m_le_azimut->setGeometry(270, oMapa->height() + 60, 80, 30);
    aplicarEstiloLineEditBarraEstado(m_le_azimut);
    m_le_azimut->setVisible(false);

    // Distancia
    m_l_distancia = new QLabel("Distancia", oMapa);
    m_l_distancia->setGeometry(330, oMapa->height() + 60, 80, 30);
    aplicarEstiloLabelBarraEstado(m_l_distancia);
    m_l_distancia->setVisible(false);

    m_le_distancia = new QLineEdit(oMapa);
    m_le_distancia->setGeometry(410, oMapa->height() + 60, 110, 30);
    aplicarEstiloLineEditBarraEstado(m_le_distancia);
    m_le_distancia->setVisible(false);
}

void LibMapaStatic::aplicarEstiloLabelBarraEstado(QLabel* label)
{
    label->setStyleSheet(
        "color: rgb(0, 0, 0);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 142), stop:0.05 rgba(30, 18, 167, 136),"
        "stop:0.36 rgba(31, 19, 166, 136), stop:0.6 rgba(129, 122, 129, 136),"
        "stop:0.75 rgba(54, 70, 170, 136), stop:0.797753 rgba(86, 94, 244, 136),"
        "stop:0.86 rgba(49, 75, 193, 136), stop:0.935 rgba(11, 20, 184, 138));"
        );
    label->setAlignment(Qt::AlignCenter);

    QFont font;
    font.setBold(true);
    label->setFont(font);
}

void LibMapaStatic::aplicarEstiloLineEditBarraEstado(QLineEdit* lineEdit)
{
    lineEdit->setStyleSheet(
        "color: rgb(0, 0, 127);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 45), stop:0.05 rgba(30, 18, 167, 45),"
        "stop:0.36 rgba(31, 19, 166, 45), stop:0.6 rgba(129, 122, 129, 45),"
        "stop:0.75 rgba(54, 70, 170, 45), stop:0.797753 rgba(86, 94, 244, 45),"
        "stop:0.86 rgba(49, 75, 193, 45), stop:0.935 rgba(11, 20, 184, 45));"
        );
    lineEdit->setAlignment(Qt::AlignCenter);
    lineEdit->setReadOnly(true);

    QFont font;
    font.setBold(true);
    lineEdit->setFont(font);
}

void LibMapaStatic::crearBotonesZoom()
{
    // Botón zoom más
    m_tb_zMas = new QToolButton(oMapa);
    m_tb_zMas->setToolTip("Acercar");
    if (verificarRecursosExisten(DirPath + "/Recursos/Iconos/lupa+.png")) {
        m_tb_zMas->setIcon(QIcon(DirPath + "/Recursos/Iconos/lupa+.png"));
    }
    m_tb_zMas->setIconSize(QSize(30, 30));
    m_tb_zMas->setGeometry(oMapa->width() + 80, oMapa->height() - (oMapa->height() / 2 + 40), 30, 30);
    m_tb_zMas->setStyleSheet(
        "QToolButton::pressed{ background-color: #aaffcc;}"
        "QToolButton::hover{background-color: #ffaf5d;}"
        "QToolButton{background-color: qlineargradient(spread:pad, x1:1, y1:0, x2:1, y2:1,"
        "stop:0 rgba(82, 82, 82, 100),stop:0.995192 rgba(75, 75, 75, 100));"
        "color: #fff;border: 1px solid #777777;border-radius: 10px;padding: 2px;}"
        );

    // Botón zoom menos
    m_tb_zMenos = new QToolButton(oMapa);
    m_tb_zMenos->setToolTip("Alejar");
    if (verificarRecursosExisten(DirPath + "/Recursos/Iconos/lupa-.png")) {
        m_tb_zMenos->setIcon(QIcon(DirPath + "/Recursos/Iconos/lupa-.png"));
    }
    m_tb_zMenos->setIconSize(QSize(30, 30));
    m_tb_zMenos->setGeometry(oMapa->width() + 80, oMapa->height() - (oMapa->height() / 2), 30, 30);
    m_tb_zMenos->setStyleSheet(m_tb_zMas->styleSheet());
}

void LibMapaStatic::configurarConexiones()
{
    // Conexiones de botones con funciones
    connect(m_tb_medirDistancia, &QToolButton::clicked,
            this, &LibMapaStatic::accionarMedirDistancia);
    connect(oMapa, &CMapaPlot::sig_finMedirDistancia,
            this, &LibMapaStatic::accionarMedirDistancia);

    // Timer para chequear estado de medición
    tim = new QTimer(this);
    connect(tim, &QTimer::timeout,
            this, &LibMapaStatic::chequearEstadoMedicionDistancia);
    tim->start(300);

    // Conexiones de herramientas
    connect(m_tb_zoomArea, &QToolButton::clicked,
            oMapa, &CMapaPlot::AccionarZoomArea);
    connect(m_tb_nuevoPoligono, &QToolButton::clicked,
            oMapa, &CMapaPlot::AccionarCrearPoligono);
    connect(m_tb_nuevoPunto, &QToolButton::clicked,
            oMapa, &CMapaPlot::AccionarNuevoPunto);
    connect(m_tb_trazaRuta, &QToolButton::clicked,
            oMapa, &CMapaPlot::AccionarPintarRuta);
    connect(m_tb_zMas, &QToolButton::clicked,
            oMapa, &CMapaPlot::AccionarZoomMas);
    connect(m_tb_zMenos, &QToolButton::clicked,
            oMapa, &CMapaPlot::AccionarZoomMenos);
}

// Implementación de getters
CMapaPlot *LibMapaStatic::getOMapa() const { return oMapa; }
QString LibMapaStatic::getDirPath() const { return DirPath; }
QToolButton *LibMapaStatic::getTb_zMas() const { return m_tb_zMas; }
QToolButton *LibMapaStatic::getTb_zMenos() const { return m_tb_zMenos; }
QToolButton *LibMapaStatic::getTb_trazaRuta() const { return m_tb_trazaRuta; }
QToolButton *LibMapaStatic::getTb_zoomArea() const { return m_tb_zoomArea; }
QToolButton *LibMapaStatic::getTb_nuevoPunto() const { return m_tb_nuevoPunto; }
QToolButton *LibMapaStatic::getTb_nuevoPoligono() const { return m_tb_nuevoPoligono; }
QToolButton *LibMapaStatic::getTb_medirDistancia() const { return m_tb_medirDistancia; }

// Implementación de setters
void LibMapaStatic::setOMapa(CMapaPlot *newOMapa)
{
    if (oMapa != newOMapa) {
        if (oMapa) {
            oMapa->deleteLater();
        }
        oMapa = newOMapa;
        if (oMapa) {
            oMapa->setParent(this);
        }
    }
}

void LibMapaStatic::setPathFicheros(const QString &path)
{
    if (oMapa) {
        oMapa->setPathGeneral(path);
    }
}

void LibMapaStatic::setTb_zMenos(QToolButton *tb_zMenos) { m_tb_zMenos = tb_zMenos; }
void LibMapaStatic::setTb_trazaRuta(QToolButton *tb_trazaRuta) { m_tb_trazaRuta = tb_trazaRuta; }
void LibMapaStatic::setTb_zoomArea(QToolButton *tb_zoomArea) { m_tb_zoomArea = tb_zoomArea; }
void LibMapaStatic::setTb_nuevoPunto(QToolButton *tb_nuevoPunto) { m_tb_nuevoPunto = tb_nuevoPunto; }
void LibMapaStatic::setTb_nuevoPoligono(QToolButton *tb_nuevoPoligono) { m_tb_nuevoPoligono = tb_nuevoPoligono; }
void LibMapaStatic::setTb_medirDistancia(QToolButton *tb_medirDistancia) { m_tb_medirDistancia = tb_medirDistancia; }

// Implementación de slots
void LibMapaStatic::seleccionarUbicacionEnMapa()
{
    if (oMapa) {
        oMapa->AccionarSeleccionarPuntoEnMapa();
    }
}

void LibMapaStatic::mostrarMedicionDistancia(const QString &azimut, const QString &distancia)
{
    if (m_le_azimut && m_le_distancia) {
        m_le_azimut->setText(azimut);
        m_le_distancia->setText(distancia);
    }
}

void LibMapaStatic::mostrarPosActualMouse(const QGeoCoordinate &geoPosPunto)
{
    if (m_le_geoPosMouse) {
        m_le_geoPosMouse->setText(geoPosPunto.toString());
    }
}

void LibMapaStatic::mostrarOcultarControlesGraficos(bool visible)
{
    if (m_tb_medirDistancia) m_tb_medirDistancia->setVisible(visible);
    if (m_tb_nuevoPunto) m_tb_nuevoPunto->setVisible(visible);
    if (m_tb_nuevoPoligono) m_tb_nuevoPoligono->setVisible(visible);
    if (m_tb_zoomArea) m_tb_zoomArea->setVisible(visible);
}

void LibMapaStatic::mostrarCapaImgSatSeleccionada(bool capa)
{
    if (!capa || !oMapa) return;

    b_seCambioCapa = true;
    mostrarCapaSeleccionada(ImagenesSatelitales);

    if (oMapa->getLineaDistancia()) {
        oMapa->getLineaDistancia()->setPen(QPen(QColor(Qt::cyan)));
    }

    aplicarEstiloCapaSatelital();
    actualizarElementosVisualesCapaSatelital();
}

void LibMapaStatic::aplicarEstiloCapaSatelital()
{
    // Estilos para etiquetas
    QString estiloLabel =
        "color: rgb(0, 0, 0);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 142), stop:0.05 rgba(30, 18, 167, 136),"
        "stop:0.36 rgba(31, 19, 166, 136), stop:0.6 rgba(129, 122, 129, 136),"
        "stop:0.75 rgba(54, 70, 170, 136), stop:0.797753 rgba(86, 94, 244, 136),"
        "stop:0.86 rgba(49, 75, 193, 136), stop:0.935 rgba(11, 20, 184, 138));";

    if (m_l_distancia) m_l_distancia->setStyleSheet(estiloLabel);
    if (m_l_azimut) m_l_azimut->setStyleSheet(estiloLabel);

    // Estilos para campos de texto
    QString estiloLineEdit =
        "color: rgb(60, 220, 255);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 70), stop:0.05 rgba(30, 18, 167, 70),"
        "stop:0.36 rgba(31, 19, 166, 70), stop:0.6 rgba(129, 122, 129, 70),"
        "stop:0.75 rgba(54, 70, 170, 70), stop:0.797753 rgba(86, 94, 244, 70),"
        "stop:0.86 rgba(49, 75, 193, 70), stop:0.935 rgba(11, 20, 184, 70));";

    if (m_le_distancia) m_le_distancia->setStyleSheet(estiloLineEdit);
    if (m_le_azimut) m_le_azimut->setStyleSheet(estiloLineEdit);
    if (m_le_geoPosMouse) m_le_geoPosMouse->setStyleSheet(estiloLineEdit);
}

void LibMapaStatic::actualizarElementosVisualesCapaSatelital()
{
    if (!oMapa) return;

    // Actualizar vehículos
    for (CVehiculo* vehiculo : oMapa->getListVehiculos()) {
        vehiculo->setColorTrazaTrayectoria(Qt::yellow);
    }

    // Actualizar puntos
    for (CPunto* punto : oMapa->getListPuntos()) {
        if (punto->getNombre().contains("Estacion")) {
            QString iconPath = DirPath + "/Recursos/Iconos/estacionterrena_SAT.png";
            if (verificarRecursosExisten(iconPath)) {
                punto->setSimbolo(QPixmap(iconPath).scaled(QSize(20,30),
                                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }
        if (punto->getNombre().contains("SISTEMA")) {
            QString iconPath = DirPath + "/Recursos/Iconos/puntos/home_SAT.png";
            if (verificarRecursosExisten(iconPath)) {
                punto->setSimbolo(QPixmap(iconPath).scaled(QSize(25,25),
                                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }
    }

    // Actualizar textos
    for (QCPItemText* textVehiculo : oMapa->getListTextVehiculo()) {
        textVehiculo->setColor(QColor(Qt::yellow));
        textVehiculo->setPen(QColor(Qt::yellow));
    }

    // Actualizar líneas
    for (QCPItemLine* textLineDescrip : oMapa->getListLineDescripVehiculo()) {
        textLineDescrip->setPen(QColor(Qt::yellow));
    }
}

void LibMapaStatic::mostrarCapaImgOSMSeleccionada(bool capa)
{
    if (!capa || !oMapa) return;

    b_seCambioCapa = true;
    mostrarCapaSeleccionada(ImagenesOSM);

    if (oMapa->getLineaDistancia()) {
        oMapa->getLineaDistancia()->setPen(QPen(QColor(Qt::darkBlue)));
    }

    aplicarEstiloCapaOSM();
    actualizarElementosVisualesCapaOSM();
}

void LibMapaStatic::aplicarEstiloCapaOSM()
{
    // Estilos para etiquetas (igual que satelital)
    QString estiloLabel =
        "color: rgb(0, 0, 0);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 142), stop:0.05 rgba(30, 18, 167, 136),"
        "stop:0.36 rgba(31, 19, 166, 136), stop:0.6 rgba(129, 122, 129, 136),"
        "stop:0.75 rgba(54, 70, 170, 136), stop:0.797753 rgba(86, 94, 244, 136),"
        "stop:0.86 rgba(49, 75, 193, 136), stop:0.935 rgba(11, 20, 184, 138));";

    if (m_l_distancia) m_l_distancia->setStyleSheet(estiloLabel);
    if (m_l_azimut) m_l_azimut->setStyleSheet(estiloLabel);

    // Estilos para campos de texto
    QString estiloLineEdit =
        "color: rgb(0, 0, 127);"
        "background-color: qlineargradient(spread:pad, x1:0, y1:1, x2:0, y2:0,"
        "stop:0 rgba(139, 154, 238, 70), stop:0.05 rgba(30, 18, 167, 70),"
        "stop:0.36 rgba(31, 19, 166, 70), stop:0.6 rgba(129, 122, 129, 70),"
        "stop:0.75 rgba(54, 70, 170, 70), stop:0.797753 rgba(86, 94, 244, 70),"
        "stop:0.86 rgba(49, 75, 193, 70), stop:0.935 rgba(11, 20, 184, 70));";

    if (m_le_distancia) m_le_distancia->setStyleSheet(estiloLineEdit);
    if (m_le_azimut) m_le_azimut->setStyleSheet(estiloLineEdit);
    if (m_le_geoPosMouse) m_le_geoPosMouse->setStyleSheet(estiloLineEdit);
}

void LibMapaStatic::actualizarElementosVisualesCapaOSM()
{
    if (!oMapa) return;

    // Actualizar vehículos
    for (CVehiculo* vehiculo : oMapa->getListVehiculos()) {
        vehiculo->setColorTrazaTrayectoria(Qt::red);
    }

    // Actualizar puntos
    for (CPunto* punto : oMapa->getListPuntos()) {
        if (punto->getNombre().contains("Estacion")) {
            QString iconPath = DirPath + "/Recursos/Iconos/estacionterrena_OSM.png";
            if (verificarRecursosExisten(iconPath)) {
                punto->setSimbolo(QPixmap(iconPath).scaled(QSize(20,30),
                                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }
        if (punto->getNombre().contains("SISTEMA")) {
            QString iconPath = DirPath + "/Recursos/Iconos/puntos/home_OSM.png";
            if (verificarRecursosExisten(iconPath)) {
                punto->setSimbolo(QPixmap(iconPath).scaled(QSize(25,25),
                                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }
    }

    // Actualizar textos
    for (QCPItemText* textVehiculo : oMapa->getListTextVehiculo()) {
        textVehiculo->setColor(QColor(Qt::red));
        textVehiculo->setPen(QColor(Qt::red));
    }

    // Actualizar líneas
    for (QCPItemLine* textLineDescrip : oMapa->getListLineDescripVehiculo()) {
        textLineDescrip->setPen(QColor(Qt::red));
    }
}

void LibMapaStatic::mostrarCapaCubaVectSeleccionada(bool capa)
{
    Q_UNUSED(capa)
    //if (capa) mostrarCapaSeleccionada(CubaVectorial);
}

void LibMapaStatic::mostrarConfiguracion(bool abrir)
{
    Q_UNUSED(abrir)
    // Implementar si es necesario
}

void LibMapaStatic::devuelveWidgetClickDerechoParaDisenoExterno(const QGeoCoordinate &posGeoMouse)
{
    emit sig_devuelveWidgetClickDerechoParaDisenoExterno(menuClickDerecho, posGeoMouse);
}

void LibMapaStatic::terminoZoomArea()
{
    if (m_tb_zoomArea) {
        m_tb_zoomArea->setChecked(false);
    }
}

void LibMapaStatic::accionarMedirDistancia()
{
    try {
        if (m_l_distancia) m_l_distancia->setVisible(true);
        if (m_le_distancia) m_le_distancia->setVisible(true);
        if (m_l_azimut) m_l_azimut->setVisible(true);
        if (m_le_azimut) m_le_azimut->setVisible(true);

        if (oMapa) {
            oMapa->AccionarMedirDistancia();
        }

    } catch (const std::exception& e) {
    } catch (...) {
    }
}

void LibMapaStatic::chequearEstadoMedicionDistancia()
{
    if (!oMapa) return;

    if (oMapa->getACCIONES() != aMedirDistancia) {
        if (m_l_distancia) m_l_distancia->setVisible(false);
        if (m_le_distancia) m_le_distancia->setVisible(false);
        if (m_l_azimut) m_l_azimut->setVisible(false);
        if (m_le_azimut) m_le_azimut->setVisible(false);
        if (m_tb_medirDistancia) m_tb_medirDistancia->setChecked(false);
    }

    if (oMapa->getACCIONES() == aNADA) {
        if (m_tb_nuevoPoligono) m_tb_nuevoPoligono->setChecked(false);
        if (m_tb_trazaRuta) m_tb_trazaRuta->setChecked(false);
    }
}

void LibMapaStatic::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (!oMapa || !m_tb_zMas || !m_tb_zMenos) return;

    try {
        int posX = event->size().width() - (m_tb_zMas->width() + 20);
        m_tb_zMas->move(posX, event->size().height() * 0.5);
        m_tb_zMenos->move(posX, (event->size().height() * 0.5) + m_tb_zMas->height() + 10);

        int posY = event->size().height() - (m_le_geoPosMouse->height() + 20);

        if (m_le_geoPosMouse) m_le_geoPosMouse->move(10, posY);
        if (m_l_azimut) m_l_azimut->move(m_le_geoPosMouse->x() + m_le_geoPosMouse->width() + 10, posY);
        if (m_le_azimut) m_le_azimut->move(m_l_azimut->x() + m_l_azimut->width(), posY);
        if (m_l_distancia) m_l_distancia->move(m_le_azimut->x() + m_le_azimut->width() + 10, posY);
        if (m_le_distancia) m_le_distancia->move(m_l_distancia->x() + m_l_distancia->width(), posY);

    } catch (const std::exception& e) {
        qWarning() << "Error en resizeEvent:" << e.what();
    }
}

void LibMapaStatic::mostrarCapaSeleccionada(int capa)
{
    if (oMapa) {
        oMapa->setTipoBD(capa);
        oMapa->Cargar_Imagenes();
        oMapa->layer("sMapaCuba")->replot();
    }
}
