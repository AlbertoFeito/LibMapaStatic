#include "cmapaplot.h"
#include "cgooglemercator.h"
#include "wrjfile.h"
#include <QPixmap>
#include <QRgb>

CMapaPlot::CMapaPlot(QString path, QWidget *parent) : QCustomPlot(parent),tipoBD(0)
{
    projection = new CGoogleMercator();
    QPointF p1;
    QPointF p2;
    p1 =  projection->forward(MIN_LAT, MIN_LON);
    p2 =  projection->forward(MAX_LAT, MAX_LON);

    rect = new QCPItemRect(this);
    m_pathGeneral = path;
    m_bdMapas = new CBDatos(m_pathGeneral);
    axisRect()->setAutoMargins(QCP::msNone);
    iniMapaPlot(p1.y(),p1.x(),p2.y(),p2.x());

    QString dataSimbolos = QDir::currentPath() + "/Recursos/Iconos";
    QStringList lista = QDir(dataSimbolos).entryList();

    //creando ventana nuevo Punto
    m_ventanaNewPunto = new VNuevoPunto(this);
    connect(m_ventanaNewPunto,SIGNAL(signal_nuevoPuntoAceptado(CPunto*)),this,SLOT(agregarNuevoPunto(CPunto*)));

    //llenando listado con los iconos que seran utilizados por los puntos
    int i = 0;
    foreach (QString Simbolo, lista)
    {
        if (i > 2)
        {
            if (!Simbolo.contains(".db"))
            {
                QByteArray d;
                QFile fIcons(dataSimbolos + "/" + Simbolo);
                E_Icons eIcon;
                eIcon.nombreIcon = Simbolo;
                if (fIcons.open(QIODevice::ReadOnly))
                    d = fIcons.readAll();
                fIcons.close();

                QPixmap pixIcon;
                pixIcon.loadFromData(d);
                eIcon.icon = QIcon(pixIcon);
                ListSimbolos.append(eIcon);
            }
        }
        i++;
    }

    m_ventanaNewPunto->setListadoSimbolos(ListSimbolos);


    //creando componente grafico (linea) para la medicion de distancia
    lineaDistancia = new QCPItemLine(this);
    lineaDistancia->setObjectName("medirDistancia");

    m_textoSeleccionPuntoRutaMousePos = new QCPItemText(this);
    m_textoSeleccionPuntoRutaMousePos->setObjectName("textoruta");
    m_textoSeleccionPuntoRutaMousePos->setLayer(sMapaTextoRuta);

    m_listPuntosRutaVisual = new QCPCurve(xAxis,yAxis);
    m_listPuntosRutaVisual->setVisible(true);
    m_listPuntosRutaVisual->setObjectName("puntosruta");
    m_listPuntosRutaVisual->setLayer(sMapaPuntos);
    scStyle = new QCPScatterStyle(QCPScatterStyle::ssDisc,3);
    scStyle->setPen (QPen(QColor(Qt::red)));
    m_listPuntosRutaVisual->setScatterStyle(*scStyle);
    QPen p;
    p.setColor(QColor(255,150,10));
    p.setWidth(3);
    m_listPuntosRutaVisual->setPen(p);

    //patron dash de la representacion de la trayectoria del vehiculo
    qreal space = 4;
    dashes << 1 << space << 3 << space << 9 << space
           << 15 << space << 9 << space;
    //------------------------

    // linea Rumbo vehiculos
    lineaRumboVehiculo = new QCPItemLine(this);
    lineaRumboVehiculo->setObjectName ("rumboVehiculo");
    lineaRumboDestino = new QCPItemLine(this);
    lineaRumboDestino->setObjectName ("rumboVehiculo");
    lineaRumboCalculado = new QCPItemLine(this);
    lineaRumboCalculado->setObjectName ("rumboVehiculo");
    lineaRumboVehiculo->setLayer(sMapaZOOM);
    lineaRumboCalculado->setLayer(sMapaZOOM);
    lineaRumboDestino->setLayer(sMapaZOOM);
    lineEnding.setStyle (QCPLineEnding::esFlatArrow);

    // 4. Conectar señal para actualizar posiciones al hacer zoom
    connect(this, &QCustomPlot::afterReplot, this, &CMapaPlot::updateEllipsePositions);
}

CMapaPlot::~CMapaPlot()
{
    delete projection;
}

void CMapaPlot::iniMapaPlot(double latMin, double longMin, double latMax, double longMax)
{
    //agrego el path de los ficheros

    m_bdMapas->setDirPath(m_pathGeneral);

    //establezco color de fondo
    setBackground(QBrush(clAguas.color()));

    //establezco interacciones
    setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectItems | QCP::iSelectPlottables);

    xAxis->setScaleType(QCPAxis::stLinear);
    xAxis->setBasePen(QPen(Qt::white,1));
    yAxis->setBasePen(QPen(Qt::white,1));
    xAxis->setVisible(false);
    yAxis->setVisible(false);

    //establezco los rangos y color base
    xAxis->setRange(longMin,longMax);
    yAxis->setRange(latMin,latMax);


    //creo las capas q voy a utilizar en el orden de prioridad
    addNewLayer(sMapaCuba);
    addNewLayer(sMapaAguas);
    addNewLayer(sMapaFir);
    addNewLayer(sMapaEjercitos);
    addNewLayer(sMapaCorredores);
    addNewLayer(sMapaZOOM);
    addNewLayer(sMapaTextoRuta);
    addNewLayer(sMapaPoligonos);
    addNewLayer(sMapaPuntos);
    addNewLayer(sMapaTargets);

    //cargo los ficheros de mapa
    loadMapGEO(m_pathGeneral + "/Recursos/Aguas.geo",sMapaAguas,clLimiteAguas);
    loadMapGEO(m_pathGeneral + "/Recursos/FIR.geo",sMapaFir,clLimiteFIR);
    loadMapGEO(m_pathGeneral + "/Recursos/Ejercitos.geo",sMapaEjercitos,clLimiteFIR);
    loadMapGEO(m_pathGeneral + "/Recursos/Corredores.geo",sMapaCorredores,clLimiteFIR);

    tipoBD = 1; //imagenes OSM .
    Inicializacion(m_pathGeneral + "/Recursos/DatosZoom.txt",
                   this->geometry().width(),
                   this->geometry().height());

    penSelectedBuscador.setColor(Qt::green);
    penSelectedBuscador.setWidth(3);
    penSelectedBuscador.setBrush(QBrush(QColor(Qt::green),Qt::Dense4Pattern));
    //mando a pintar todo
    replot();
}

void CMapaPlot::loadMapGEO(const QString &fileName, const QString &slayer, const QPen &pen)
{
    QString Dir = QDir::currentPath() + fileName;
    QFile F(Dir);
    QTextStream Dat(&F);
    QVector<double> x1;
    QVector<double> y1;
    QString datosline;
    QStringList listDatos;
    QPointF punto;

    if (F.open(QIODevice::ReadOnly))
    {
        while(!Dat.atEnd())
        {
            datosline = Dat.readLine();
            listDatos = datosline.split(',');
            punto.setX(listDatos.at(0).toDouble());
            punto.setY(listDatos.at(1).toDouble());

            QPointF inPos = projection->forward(punto.y(), punto.x());

            x1.append(inPos.x());
            y1.append(inPos.y());

            if(datosline.at(0) == '0')
            {
                auto *pPolygon = new QCPCurve(xAxis,yAxis);
                pPolygon->setVisible(true);
                pPolygon->setLayer(slayer);
                pPolygon->setPen(pen);
                x1.removeLast();
                y1.removeLast();
                pPolygon->setData(x1, y1);
                x1.clear();
                y1.clear();
            }
        }
        F.close();
    }
}

bool CMapaPlot::setVisibleLayer(const QString &slayer, bool visible)
{
    layer(slayer)->setVisible(visible);
    layer(slayer)->replot();
    return visible;
}

void CMapaPlot::zoommas(QWheelEvent *event)
{
    actualizMapa = true;

    if (tipoBD == 1)  //--> OSM_IMG solo tenemos hasta el z16
    {
        if (ZoomValido < 16)
        {
            if(ZoomValido < 17)
            {
                QCustomPlot::wheelEvent(event);
                repintarPuntos();
                repintarVehiculos();
                if(layer(sMapaZOOM)->visible())
                    layer(sMapaZOOM)->replot();
            }
        }
    }
    else
    {
        if(ZoomValido < 17)
        {
            QCustomPlot::wheelEvent(event);
            repintarPuntos();
            repintarVehiculos();
            if(layer(sMapaZOOM)->visible())
                layer(sMapaZOOM)->replot();
        }
    }
}

void CMapaPlot::zoommenos(QWheelEvent *event)
{
    actualizMapa = true;

    if(ZoomValido > 5)
    {
        QCustomPlot::wheelEvent(event);
        repintarPuntos();
        repintarVehiculos();

        if(layer(sMapaZOOM)->visible())
            layer(sMapaZOOM)->replot();
    }
}

void CMapaPlot::agregarNuevoPunto(CPunto *puntoNew)
{
    bool b_existe = false;
    switch (puntoNew->getTipoPunto())
    {
    case VehiculoAereo:
    case VehiculoTerrestre:
    case VehiculoNaval:
    {
        for (int i = 0;i < getListVehiculos().size(); i++)
        {
            CVehiculo *target = getListVehiculos().at(i);

            if (puntoNew->getNombre() == target->getNombre())
                b_existe = true;
            if (b_existe)
            {
                target->addNewPuntoToTrayect(target->getGeoPos(),QDateTime::currentDateTime().toMSecsSinceEpoch(),target->getRumbo());
                qDebug() << target->getNombre() << ": trayectoria adicionada (" << target->getGeoPos() << ")";

                QPixmap simb = rotarImagen(target->getSimbolo(),target->getRumbo());

                foreach (QCPItemPixmap *simbItem, m_listPuntosGraficados)
                {
                    if (simbItem->objectName() == target->getNombre())
                    {

                        QPointF posGooMerc = projection->forward(puntoNew->getGeoPos().latitude(), puntoNew->getGeoPos().longitude());
                        QPointF posSimbCentro = convertirPosCoordSimboloDe_IzqSupACentro(posGooMerc,simb);

                        simbItem->topLeft->setCoords(posSimbCentro);
                        simbItem->setPixmap(simb);
                        break;
                    }
                }

                target->setColorTrazaTrayectoria((dynamic_cast<CVehiculo*>(puntoNew))->getColorTrazaTrayectoria());
                foreach (QCPCurve* puntoTray, m_listVehiculosTrayectoria)
                {
                    if (target->getNombre() == puntoTray->objectName())
                    {
                        b_existe = true;
                        int tamTray = target->getListTrayect().length();
                        int cantTrayVisible = target->getCantPuntosTrayecAVisualizar();

                        p.setColor(target->getColorTrazaTrayectoria());
                        puntoTray->setPen(p);

                        QPointF dataPos = projection->forward(((CVehiculo*)target)->getListTrayect().last().pos);

                        // Para visualizar solo la cantidad deseada de puntos de trayectoria
                        if (tamTray <= cantTrayVisible)
                        {
                            puntoTray->addData(dataPos.x(),dataPos.y());
                            qDebug() << puntoTray->data()->size() << " puntos de trayectoria de " << target->getNombre();
                        }
                        else
                        {
                            puntoTray->setData(QVector<double> (0) , QVector<double> (0));
                            for (int i = (tamTray >= cantTrayVisible ? tamTray - cantTrayVisible : 0); i < tamTray; i++)
                            {
                                dataPos = projection->forward(((CVehiculo*)target)->getListTrayect().at(i).pos);
                                puntoTray->addData(dataPos.x(),dataPos.y());
                            }
                        }
                        break;
                    }
                }

                repintarVehiculos();

                break;
            }
        }
        break;
    }
    }
    if (!b_existe)
    {
        QString layerPunto = sMapaPuntos;
        float rumbo = 0;

        auto * puntoItem = new QCPItemPixmap(this);
        auto listaTray = new QCPCurve(xAxis,yAxis);

        QPen pen;

        switch (puntoNew->getTipoPunto())
        {
        case VehiculoAereo:
        case VehiculoNaval:
        case VehiculoTerrestre:
        {
            layerPunto = sMapaTargets;
            rumbo = (dynamic_cast<CVehiculo*>(puntoNew))->getRumbo();
            m_listVehiculosGraficados.append(puntoItem);
            m_listVehiculosTrayectoria.append(listaTray);
            pen.setWidthF(1);
            pen.setDashPattern(dashes);

            CVehiculo* newVehiculo = new CVehiculo(TipoPunto(puntoNew->getTipoPunto()),(dynamic_cast<CVehiculo*>(puntoNew))->getNombre(),(dynamic_cast<CVehiculo*>(puntoNew))->getGeoPos(),(dynamic_cast<CVehiculo*>(puntoNew))->getColorTrazaTrayectoria(),(dynamic_cast<CVehiculo*>(puntoNew))->getVelocidad(),(dynamic_cast<CVehiculo*>(puntoNew))->getRumbo(),(dynamic_cast<CVehiculo*>(puntoNew))->getAltura());
            newVehiculo->setSimbolo((dynamic_cast<CVehiculo*>(puntoNew))->getSimbolo());
            newVehiculo->setNumero(m_listVehiculos.size());
            newVehiculo->setCantPuntosTrayecAVisualizar(100);
            m_listVehiculos.append(newVehiculo);

            qDebug() << newVehiculo->getNombre() << ": nuevo barco (" << newVehiculo->getGeoPos() << ")";

            pen.setColor(newVehiculo->getColorTrazaTrayectoria());
            break;
        }
        case PuntoInteres:
        {
            auto newPunto = new CPunto();
            newPunto->setTipoPunto(PuntoInteres);
            newPunto->setSimbolo(puntoNew->getSimbolo());
            newPunto->setNombre(puntoNew->getNombre());
            newPunto->setGeoPos(puntoNew->getGeoPos());
            newPunto->setDescripcion(puntoNew->getDescripcion());
            newPunto->setFechaCreacion(puntoNew->getFechaCreacion());
            newPunto->setNumero(m_listPuntos.size());
            m_listPuntos.append(newPunto);
            m_listPuntosGraficados.append(puntoItem);
            m_listPuntosTrayectoria.append(listaTray);
            break;
        }
        }
        QPointF pos = projection->forward(puntoNew->getGeoPos());

        listaTray->setObjectName(puntoNew->getNombre());
        listaTray->addData(pos.x(),pos.y());
        listaTray->setPen(pen);
        listaTray->setVisible(true);

        listaTray->setLayer(layerPunto);

        puntoItem->setLayer(layerPunto);
        puntoItem->setObjectName(puntoNew->getNombre());

        QPixmap simb = rotarImagen(puntoNew->getSimbolo(),rumbo);

        QPointF posGooMerc = projection->forward(puntoNew->getGeoPos().latitude(), puntoNew->getGeoPos().longitude());
        QPointF posSimbCentro = convertirPosCoordSimboloDe_IzqSupACentro(posGooMerc,simb);

        puntoItem->topLeft->setCoords(posSimbCentro);
        puntoItem->setPixmap(simb);


        puntoItem->setVisible(true);
        layer(layerPunto)->setVisible(true);

        m_bdMapas->guardarNuevoPunto(puntoNew);
    }
}

void CMapaPlot::eliminarPunto(CPunto *puntoDelete)
{
    foreach (CPunto* punto, m_listPuntos)
    {
        if (puntoDelete == punto)
        {
            m_listPuntos.removeOne(puntoDelete);
            break;
        }
    }
}

void CMapaPlot::pintaPuntos()
{

}

void CMapaPlot::levantarVentanaCreaPoligonos()
{
    //-----> Ventana de Herramienta de Creacion de Poligonos
    if (vPoligono == nullptr)
    {
        vPoligono = new QDockWidget("Polígonos",this);
        vPoligono->setGeometry(0,40,500,50);
        vPoligono->setFloating(false);
        vPoligono->setFeatures(QDockWidget::NoDockWidgetFeatures);
        vPoligono->setAllowedAreas(/*Qt::TopDockWidgetArea |*/ Qt::RightDockWidgetArea);
        auto * horizontalLayout = new QHBoxLayout(vPoligono);
        horizontalLayout->setSpacing(2);

        bColor = new QToolButton(vPoligono);
        bColor->setText("Color");
        bColor->setToolTip("Click para seleccionar el color del polígono.");
        bColor->setGeometry(10,20,50,20);
        bColor->setStyleSheet("background-color: rgb(90,90,90);");

        //-> Color inicial de relleno de poligonos
        colorPoligono = QColor(Qt::white);

        cbRelleno = new QComboBox(vPoligono);
        cbRelleno->setToolTip("Seleccione el tipo de relleno del polígono.");
        cbRelleno->setGeometry(65,20,150,20);
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/sRelleno.png")),"Sin Relleno");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/solido.png")),"Sólido");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/mTupida.png")),"Malla Tupida");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/mMedia.png")),"Malla");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/dDerecha.png")),"Diagonal Derecha");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/dIzquierda.png")),"Diagonal Izquerda");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/horizontal.png")),"Horizontal");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/vertical.png")),"vertical");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/matrizP.png")),"Matriz de Puntos");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/matriz.png")),"Matriz");
        cbRelleno->addItem(QIcon(QPixmap(QDir::currentPath() + "/Recursos/Iconos/matrizD.png")),"Matriz Diagonal");

        leNombrePoligon = new QLineEdit(vPoligono);
        leNombrePoligon->setPlaceholderText("Nombre Polígono");
        leNombrePoligon->setToolTip("Introduzca el nombre del polígono.");
        leNombrePoligon->setGeometry(220,20,200,20);
        leNombrePoligon->setMaxLength(22);

        bOkPoligon = new QToolButton(vPoligono);
        bOkPoligon->setToolTip("Unir y Completar el polígono creado.");
        bOkPoligon->setIcon(QIcon(QDir::currentPath() + "/Recursos/Iconos/aplicar.png"));
        bOkPoligon->setGeometry(425,20,20,20);
        bCancelPoligon = new QToolButton(vPoligono);
        bCancelPoligon->setToolTip("Deshacer el proceso de creación del polígono.");
        bCancelPoligon->setIcon(QIcon(QDir::currentPath() + "/Recursos/Iconos/cancelar.png"));
        bCancelPoligon->setGeometry(450,20,20,20);
        bCerrarHerramienta = new QToolButton(vPoligono);
        bCerrarHerramienta->setToolTip("Cerrar proceso de creación de polígonos.");
        bCerrarHerramienta->setIcon(QIcon(QDir::currentPath() + "/Recursos/Iconos/close.png"));
        bCerrarHerramienta->setGeometry(475,20,20,20);

        horizontalLayout->addWidget(bColor,2,Qt::AlignHCenter);
        horizontalLayout->addWidget(cbRelleno,2,Qt::AlignHCenter);
        horizontalLayout->addWidget(leNombrePoligon,2,Qt::AlignHCenter);
        horizontalLayout->addWidget(bOkPoligon,2,Qt::AlignHCenter);
        horizontalLayout->addWidget(bCancelPoligon,2,Qt::AlignHCenter);
        horizontalLayout->addWidget(bCerrarHerramienta,2,Qt::AlignHCenter);

        connect(bColor,SIGNAL(released()),this,SLOT(SeleccionaColorPoligono()));
        connect(leNombrePoligon,SIGNAL(textChanged(QString)),this,SLOT(ChequeaNombrePoligono(QString)));
        connect(bOkPoligon,SIGNAL(released()),this,SLOT(AceptarPoligono()));
        connect(bCancelPoligon,SIGNAL(released()),this,SLOT(CancelarPoligono()));
        connect(bCerrarHerramienta,SIGNAL(released()),this,SLOT(CancelarCreacionPoligono()));
    }
    if (vPoligono->isHidden())
        vPoligono->show();
    else
        vPoligono->close();
}

void CMapaPlot::AceptarPoligono()
{
    if (leNombrePoligon->text().isEmpty())
    {
        leNombrePoligon->setFocus();
        leNombrePoligon->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 169, 169, 255), stop:0.454545 rgba(255, 191, 191, 255), stop:0.971591 rgba(255, 214, 214, 255), stop:1 rgba(0, 0, 0, 0));");
    }
    else
    {
        if (listPuntosPoligono.size() > 2) //--------> Un poligono tiene mas de dos vertices.
        {
            //--> Limpiando Estructura Poligono despues de que se guardo..
            ePolig.LPuntosGeoPolig.clear();
            ePolig.nombrePolig = "";
            ePolig.pen = QPen(QColor(Qt::white));
            //-----------------------------------

            ePolig.nombrePolig = leNombrePoligon->text();

            //--> Asegurandonos de que el nombre no contenga caracteres especiales.
            for (auto c: ePolig.nombrePolig)
            {
                if (!c.isLetterOrNumber())
                {
                    if (!c.isSpace())
                    {
                        leNombrePoligon->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 180, 102, 255), stop:0.55 rgba(218, 235, 61, 255), stop:0.98 rgba(207, 149, 0, 255), stop:1 rgba(0, 0, 0, 0));");
                        return;
                    }
                }
            }

            //----> Seleccionando Relleno
            QBrush bro = devuelveTipoRelleno(cbRelleno->currentIndex());

            ePolig.relleno = cbRelleno->currentIndex();
            ePolig.pen = colorPoligono;

            for (auto i: listPuntosPoligono)
            {
                QGeoCoordinate puntoGeoPolig = projection->inverse(i.x(),i.y());
                ePolig.LPuntosGeoPolig.append(puntoGeoPolig);
            }

            ePolig.noVertices = ePolig.LPuntosGeoPolig.size();
            ePolig.fechaCreacion = QDateTime::currentDateTime().toString("dd-MM-yyyy | hh:mm:ss");

            //----> agregando poligono nuevo a la lista de Datos de Poligonos
            listPoligonoEstruc.append(ePolig);

            //----> Uniendo vertices y completando Poligono
            pinta_Poligonos(listPuntosPoligono,ePolig.nombrePolig,QPen(colorPoligono),bro);
            listPuntosPoligono.clear();

            //----> Guardando Poligonos en la BD
            m_bdMapas->guardarNuevoPoligono(ePolig);

            //--> Reseteando componentes de las Herramientas del Poligono.
            leNombrePoligon->setText("");
            bColor->setStyleSheet("background-color: rgb(255,255,255)");
            cbRelleno->setCurrentIndex(0);
            //            borraPoligono(ListVerticesPolig);
            ACCIONES = aNADA;
        }
        else
        {
            borraPoligono(ListVerticesPolig,nombVerticesTemporales);
            QMessageBox::warning(this,"Atención..","Debe seleccionar al menos 3 puntos para conformar el Polígono.",QMessageBox::Ok);
        }
    }
}

void CMapaPlot::pinta_Poligonos(QList<QPointF> Lpunto, const QString &Texto, const QPen &Pen,QBrush relleno)
{
    auto poligono = new QCPCurve(xAxis,yAxis);
    poligono->setObjectName(Texto);
    poligono->setName(Texto);
    poligono->setScatterStyle(QCPScatterStyle::ssCrossSquare);
    poligono->setPen(QColor(Qt::darkGray));

    if (relleno.style() == Qt::NoBrush)
        poligono->setPen(Pen);

    poligono->setLayer(sMapaPoligonos);

    relleno.setColor(Pen.color());

    poligono->setBrush(relleno);
    poligono->setVisible(true);

    Lpunto.append(Lpunto.first()); //--> Cerrando el Poligono.
    foreach (QPointF p, Lpunto)
    {
        poligono->addData(p.y(),p.x());
    }

    ListPolig.append(poligono);

    //-------------> Se emite la lista de Poligonos
    listPoligonos = ListPolig;

    auto text = new QCPItemText(this);
    text->setObjectName(Texto);
    text->setText(Texto);
    text->setColor(QColor(Qt::white));
    text->position->setCoords(Lpunto.last().y(),Lpunto.last().x());
    text->setVisible(true);
    text->setLayer(sMapaPoligonos);

    ListTextPolig.append(text);

    //-------------> Se emite la lista de Textos del Poligono
    listTextPoligonos = ListTextPolig;

    layer(sMapaPoligonos)->replot();
}

void CMapaPlot::pinta_Punto_Poligono(QPointF puntoPolig)
{
    //------------------------------------------> Pinta solo los vértices del poligono que se esta creando en el mapa.
    auto * puntoPoli = new QCPCurve(xAxis,yAxis);
    puntoPoli->setObjectName(nombVerticesTemporales);
    puntoPoli->setLayer(sMapaPoligonos);
    puntoPoli->setScatterStyle(QCPScatterStyle::ssCrossCircle);
    puntoPoli->setPen(QPen(QColor(Qt::yellow)));
    puntoPoli->addData(puntoPolig.y(),puntoPolig.x());

    layer(sMapaPoligonos)->replot();
    ListVerticesPolig.append(puntoPoli);
}

void CMapaPlot::repintarPuntos()
{
    foreach (CPunto* punto, m_listPuntos)
    {
        foreach (QCPItemPixmap* puntoVisual, m_listPuntosGraficados)
        {
            if (punto->getNombre() == puntoVisual->objectName())
            {
                double xSimbolo = puntoVisual->topLeft->coords().x();
                double ySimbolo = puntoVisual->topLeft->coords().y();
                // Si el Pixmap del punto esta dentro del rango visible
                if ( (xSimbolo >= xAxis->range().lower) && (xSimbolo <= xAxis->range().upper) && (ySimbolo >= yAxis->range().lower) && (ySimbolo <= yAxis->range().upper))
                {
                    float rumbo = 0;
                    QGeoCoordinate pos = punto->getGeoPos();
                    if (punto->getTipoPunto() == VehiculoAereo || punto->getTipoPunto() == VehiculoTerrestre || punto->getTipoPunto() == VehiculoNaval)
                    {
                        rumbo = (dynamic_cast<CVehiculo*>(punto))->getRumbo();
                        pos = (dynamic_cast<CVehiculo*>(punto))->getListTrayect().last().pos;
                    }
                    QPixmap simbolo = QUtiles::rotarImagen(punto->getSimbolo(),rumbo);

                    puntoVisual->topLeft->setCoords(convertirPosCoordSimboloDe_IzqSupACentro(projection->forward(pos),punto->getSimbolo()));
                    puntoVisual->setPixmap(simbolo);

                    foreach (QCPItemLine * lineDescrip, m_ListLineDescripVehiculo)
                    {
                        if (lineDescrip->objectName() == puntoVisual->objectName())
                        {
                            if (lineDescrip->visible())
                            {
                                double xPixel = xAxis->coordToPixel(puntoVisual->topLeft->coords().x());
                                double yPixel = yAxis->coordToPixel(puntoVisual->topLeft->coords().y());

                                QPointF coordCentro = QPointF(xAxis->pixelToCoord(xPixel + float(simbolo.width() / 2)),yAxis->pixelToCoord(float(yPixel + simbolo.height() / 2)));
                                lineDescrip->start->setCoords(coordCentro);
                                break;
                            }
                        }
                    }

                }
            }
        }

        layer(sMapaPuntos)->replot();

        foreach (QCPItemText * textVehiculo, m_ListTextVehiculo)
        {
            if (textVehiculo->objectName() == punto->getNombre())
            {
                if (textVehiculo->visible())
                {
                    textVehiculo->setText(punto->getDescripcion());
                    QPointF posPixel = projection->forward(punto->getGeoPos());
                    double xPixel = xAxis->coordToPixel(posPixel.x());
                    double yPixel = yAxis->coordToPixel(posPixel.y());
                    QPointF posPixelNew;
                    posPixelNew.setX(xAxis->pixelToCoord(xPixel + 50));
                    posPixelNew.setY(yAxis->pixelToCoord(yPixel - 20));

                    textVehiculo->position->setCoords(posPixelNew);

                    break;
                }
            }
        }


    }
}

void CMapaPlot::repintarVehiculos()
{
    foreach (QCPItemPixmap* imagenVehiculo, m_listVehiculosGraficados)
    {
        foreach (CVehiculo* punto, m_listVehiculos)
        {
            QString nombPunto = punto->getNombre();
            QString nombImagenVehiculo = imagenVehiculo->objectName();
            if (nombPunto == nombImagenVehiculo)
            {
                double xSimbolo = imagenVehiculo->topLeft->coords().x();
                double ySimbolo = imagenVehiculo->topLeft->coords().y();
                // Si el Pixmap del punto esta dentro del rango visible
                if ( (xSimbolo >= xAxis->range().lower) && (xSimbolo <= xAxis->range().upper) && (ySimbolo >= yAxis->range().lower) && (ySimbolo <= yAxis->range().upper))
                {
                    float rumbo = punto->getRumbo();
                    QGeoCoordinate pos = punto->getListTrayect().last().pos;
                    QPixmap simbolo = QUtiles::rotarImagen(punto->getSimbolo(),rumbo);

                    imagenVehiculo->topLeft->setCoords(convertirPosCoordSimboloDe_IzqSupACentro(projection->forward(pos),simbolo));
                    imagenVehiculo->setPixmap(simbolo);
                    layer(sMapaTargets)->replot();
                }
                //                else
                //                    qDebug() << "Objeto vehículo" << imagenVehiculo->objectName() << ": No se encuentra dentro del rango visible.";
            }
        }
    }
}

void CMapaPlot::moverMapaDireccionIzq()
{
    QCPRange Rango = xAxis->range();
    double CoeF = -(Rango.upper - Rango.lower) * 0.02;
    xAxis->setRange(xAxis->range().lower + CoeF,xAxis->range().upper + CoeF);
    replot();
}

void CMapaPlot::moverMapaDireccionDer()
{
    QCPRange Rango = xAxis->range();
    double CoeF = (Rango.upper - Rango.lower) * 0.02;
    xAxis->setRange(xAxis->range().lower + CoeF,xAxis->range().upper + CoeF);
    replot();
}

void CMapaPlot::moverMapaDireccionArriba()
{
    QCPRange Rango = yAxis->range();
    double CoeF = (Rango.upper - Rango.lower) * 0.02;
    yAxis->setRange(yAxis->range().lower + CoeF,yAxis->range().upper + CoeF);
    replot();
}

void CMapaPlot::moverMapaDireccionAbajo()
{
    QCPRange Rango = yAxis->range();
    double CoeF = -(Rango.upper - Rango.lower) * 0.02;
    yAxis->setRange(yAxis->range().lower + CoeF,yAxis->range().upper + CoeF);
    replot();
}

void CMapaPlot::CancelarPoligono()
{
    borraPoligono(ListVerticesPolig, nombVerticesTemporales);
}

void CMapaPlot::CancelarCreacionPoligono()
{
    vPoligono->close();
    delete vPoligono;
    vPoligono = nullptr;
}

void CMapaPlot::SeleccionaColorPoligono()
{
    colorPoligono = QColorDialog::getColor(Qt::white,this,"Seleccione el color de su Polígono.");
    if (!colorPoligono.isValid())
        colorPoligono = QColor(Qt::white);

    bColor->setStyleSheet("background-color: rgb(" + QString::number(colorPoligono.toRgb().red()) + ", " + QString::number(colorPoligono.toRgb().green()) + ", " + QString::number(colorPoligono.toRgb().blue()) +");");
}

bool CMapaPlot::ChequeaNombreCorrecto(const QString &nombre)
{
    bool correcto = true;
    if (!nombre.isEmpty())
    {
        foreach (QChar c, nombre)
        {
            if (!c.isLetterOrNumber()) //---> Comprobando que el caracter solo sea Numero o Letras
            {
                if (!c.isSpace()) //----> el Espacio esta permitido
                {
                    correcto = false;
                    break;
                }
            }
        }
    }
    return correcto;
}

void CMapaPlot::ChequeaNombrePoligono(const QString &arg1)
{
    leNombrePoligon->setStyleSheet("");
    if (!arg1.isEmpty())
    {
        QChar c = arg1.at(arg1.size() - 1);
        if (!c.isLetterOrNumber()) //---> Comprobando que el caracter solo sea Numero o Letras
        {
            if (!c.isSpace()) //----> el Espacio esta permitido
            {
                int pos = arg1.size() - 1;
                leNombrePoligon->setText(leNombrePoligon->text().remove(pos,1));
                leNombrePoligon->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 180, 102, 255), stop:0.55 rgba(218, 235, 61, 255), stop:0.98 rgba(207, 149, 0, 255), stop:1 rgba(0, 0, 0, 0));");
            }
            else
                leNombrePoligon->setStyleSheet("");
        }
        else
        {
            leNombrePoligon->setStyleSheet("");
        }
    }
}

void CMapaPlot::borraPoligono(QList<QCPCurve *> &LPolig, QString nombPolig)
{
    layer(sMapaPoligonos)->replot();
    foreach (QCPCurve* pol, LPolig)
    {
        if (pol->objectName() == nombPolig)
        {
            pol->data().clear();
            delete pol;
        }
    }
    //    LPolig.clear();
    listPuntosPoligono.clear();
}

int CMapaPlot::getTipoBD() const
{
    return tipoBD;
}

void CMapaPlot::setTipoBD(int newTipoBD)
{
    if (tipoBD == newTipoBD)
        return;
    tipoBD = newTipoBD;
    emit tipoBDChanged();
}

void CMapaPlot::setPathGeneral(const QString &pathGeneral)
{
    m_pathGeneral = pathGeneral;
}

void CMapaPlot::Inicializacion(const QString CaminoFich, int NuevaWidthPant, int NuevaHeightPant)
{
    QFile fileInf(CaminoFich);
    QTextStream out(&fileInf);
    QVector<double>RangoX;
    QVector<double>RangoY;
    QVector<int>iZoom;

    if((NuevaWidthPant==1280 && NuevaHeightPant==1024) ||
        (NuevaWidthPant==2560 && NuevaHeightPant==2048) ||
        (NuevaWidthPant==5120 && NuevaHeightPant==4096))
    {
        RelAspect=1.25;
    }
    else if((NuevaWidthPant==800 && NuevaHeightPant==600) ||
             (NuevaWidthPant==1024 && NuevaHeightPant==768) ||
             (NuevaWidthPant==1152 && NuevaHeightPant==864) ||
             (NuevaWidthPant==1400 && NuevaHeightPant==1050) ||
             (NuevaWidthPant==1600 && NuevaHeightPant==1200) ||
             (NuevaWidthPant==1920 && NuevaHeightPant==1440) ||
             (NuevaWidthPant==2048 && NuevaHeightPant==1536) ||
             (NuevaWidthPant==3200 && NuevaHeightPant==2400) ||
             (NuevaWidthPant==6400 && NuevaHeightPant==4800))
    {
        RelAspect=1.33;
    }
    else if((NuevaWidthPant==1600 &&  NuevaHeightPant==900) ||
             (NuevaWidthPant==3200 && NuevaHeightPant==2048) ||
             (NuevaWidthPant==6400 && NuevaHeightPant==4096))
    {
        RelAspect=1.56;
    }
    else if((NuevaWidthPant==1280 && NuevaHeightPant==800) ||
             (NuevaWidthPant==1440 && NuevaHeightPant==900) ||
             (NuevaWidthPant==1680 && NuevaHeightPant==1050) ||
             (NuevaWidthPant==1920 && NuevaHeightPant==1200) ||
             (NuevaWidthPant==2560 && NuevaHeightPant==1600) ||
             (NuevaWidthPant==3840 && NuevaHeightPant==2400) ||
             (NuevaWidthPant==7680 && NuevaHeightPant==4800))
    {
        RelAspect=1.6;
    }
    else if(NuevaWidthPant==1280 && NuevaHeightPant==768)
    {
        RelAspect=1.67;
    }
    else if((NuevaWidthPant==1366 && NuevaHeightPant==768) ||
             (NuevaWidthPant==1280 && NuevaHeightPant==720) ||
             (NuevaWidthPant==2048 && NuevaHeightPant==1152))
    {
        RelAspect=1.78;
    }
    else if(NuevaWidthPant==640 && NuevaHeightPant==350)
    {
        RelAspect=1.83;
    }
    else if(NuevaWidthPant==720 && NuevaHeightPant==350)
    {
        RelAspect=2.06;
    }
    else if(NuevaWidthPant==720 && NuevaHeightPant==348)
    {
        RelAspect=2.07;
    }
    else if(NuevaWidthPant==1280 && NuevaHeightPant==600)
    {
        RelAspect=2.13;
    }
    else
    {
        RelAspect=1.33;
    }

    if (fileInf.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        int CantZ=0;
        while (!out.atEnd())
        {
            QString sLine = out.readLine();
            QStringList sListLine = sLine.split(',');

            iZoom.append(sListLine.at(0).toInt());
            RangoX.append(sListLine.at(1).toDouble());
            RangoY.append(sListLine.at(2).toDouble());
            CantZ++;
        }

        for(int i=0 ; i<CantZ ;i++)
        {
            pListZoom = new ListaZoom;
            pListZoom->rangoX=RangoX.at(i);
            pListZoom->rangoY=RangoY.at(i);
            pListZoom->NumZoom=iZoom.at(i);

            m_ListaZoom.append(pListZoom);
        }
        fileInf.close();

        Cargar_Imagenes();
    }
}

void CMapaPlot::ValidaZoomActivo()
{
    QCPRange rangeX = xAxis->range();//rango del sistema
    QCPRange rangeY = yAxis->range();//rango del sistema

    double rX = fabs(rangeX.lower) - fabs(rangeX.upper);
    double rY = (rangeY.upper) - (rangeY.lower);

    QGeoCoordinate geoPos = projection->inverse(rY,rX);
    QGeoCoordinate geoXYMax, geoXYMin;
    ListaZoom *pLZoom;
    for(int i=m_ListaZoom.size()-1 ; i>=0  ;i--)
    {
        pLZoom = m_ListaZoom.at(i);
        if(pLZoom->rangoX > geoPos.longitude() && pLZoom->rangoY > geoPos.latitude() )
        {
            ZoomValido = pLZoom->NumZoom;
            geoXYMax = projection->inverse(rangeY.upper,rangeX.upper);
            geoXYMin = projection->inverse(rangeY.lower,rangeX.lower);
            Nxmin = mcalc.Convertir_Long_a_TilesX(geoXYMin.longitude(),ZoomValido - 1)-1;
            Nxmax = mcalc.Convertir_Long_a_TilesX(geoXYMax.longitude(),ZoomValido - 1)+1;
            Nymin = mcalc.Convertir_Lat_a_TilesY(geoXYMax.latitude(),ZoomValido - 1)-1;
            Nymax = mcalc.Convertir_Lat_a_TilesY(geoXYMin.latitude(),ZoomValido - 1)+1;
            break;
        }
    }
}

void CMapaPlot::pinta_ZoomArea(QList<QPointF> Lpuntos)
{
    rect->topLeft->setCoords(Lpuntos[0].x(),Lpuntos[0].y());
    rect->bottomRight->setCoords(Lpuntos[1].x(),Lpuntos[1].y());
    rect->topLeft->setType(QCPItemPosition::ptPlotCoords);
    rect->bottomRight->setType(QCPItemPosition::ptPlotCoords);
    rect->setPen(QPen(Qt::red));
    rect->setBrush(QBrush(QColor(Qt::darkRed),Qt::DiagCrossPattern));
    rect->setVisible(true);
    rect->setLayer(sMapaZOOM);
    layer(sMapaZOOM)->replot();
}

void CMapaPlot::pinta_Lineas(QList<QPointF> Lpuntos/*, QColor colorTrazo*/, QCPItemLine *LineaPintar, QCPLineEnding::EndingStyle iniLinea, QCPLineEnding::EndingStyle finLinea)
{
    int i = 0;
    foreach (QPointF p, Lpuntos)
    {
        if (i > 0)
        {
            LineaPintar->setHead(finLinea);
            LineaPintar->setTail(iniLinea);
            //            LineaPintar->setPen(QPen(colorTrazo));
            LineaPintar->setLayer(sMapaZOOM);
            LineaPintar->setVisible(true);

            LineaPintar->start->setCoords(pAnterior);
            LineaPintar->end->setCoords(p);
        }
        else
        {
            pAnterior = p;
        }
        i ++;
    }

    layer(sMapaZOOM)->replot();
}

void CMapaPlot::pinta_PuntoRuta(QList<EPuntoRuta> Lpuntos, QStringList &texto, bool visible)
{
    if (!visible)
    {
        foreach (auto item, m_plotItems)
        {
            item.text->setVisible(false);
            item.ellipse->setVisible(false);
            item.ellipse2->setVisible(false);
        }
        m_listPuntosRutaVisual->setVisible(false);
        layer(sMapaPuntos)->replot();
        return;
    }

    foreach (auto item, m_plotItems)
    {
        removeItem (item.text);
        removeItem (item.ellipse);
        removeItem (item.ellipse2);
    }

    m_plotItems.clear();

    QVector<double> x,y;
    for (int i = 0; i < Lpuntos.size(); ++i)
    {
        QPointF metros = projection->forward (Lpuntos.at(i).pos);
        x.append(metros.y());
        y.append(metros.x());
    }
    m_listPuntosRutaVisual->setData(y,x);

    m_listPuntosRutaVisual->setVisible(true);


    // 2. Ajustar ejes primero
    //      axisRect()->/*setAspectRatio*/(1.0, QCPAxisRect::KeepAxisRatioExpanding);

    // 3. Crear elipses después de ajustar ejes
    const double ellipseSize = 20.0;//llevar a metros
    const double textOffset = 10.0;
    int i = 0;
    for (const auto &point : *m_listPuntosRutaVisual->data ()) {
        E_PuntoItem item;
        QPointF center(point.key, point.value);
        QPointF pixelCenter = coordToPixel(center);

        // Elipse
        item.ellipse = new QCPItemEllipse(this);
        item.ellipse->setLayer(sMapaPuntos);
        item.ellipse->topLeft->setCoords (center.x () - radioAcercamiento, center.y () + radioAcercamiento);
        item.ellipse->bottomRight->setCoords(center.x () + radioAcercamiento, center.y () - radioAcercamiento);
        // Elipse
        item.ellipse2 = new QCPItemEllipse(this);
        item.ellipse2->setLayer(sMapaPuntos);
        item.ellipse2->topLeft->setCoords (center.x (), center.y ());
        item.ellipse2->bottomRight->setCoords(center.x (), center.y ());

        QPen pen;
        pen.setWidth(3);
        pen.setColor(Qt::red);
        item.ellipse->setPen(pen);
        item.ellipse->setBrush(Qt::transparent);
        item.ellipse2->setPen(pen);
        item.ellipse2->setBrush(Qt::transparent);

        // Texto
        item.text = new QCPItemText(this);
        item.text->setLayer(sMapaPuntos);
        item.text->setBrush(QBrush(QColor(Qt::yellow),Qt::BrushStyle::SolidPattern));
        item.text->setText(texto.at(i));
        item.text->setTextAlignment (Qt::AlignCenter);
        item.text->setFont(QFont("Arial", 8));
        item.text->setColor(Qt::black);
        item.text->position->setPixelPosition(pixelCenter - QPointF(2, textOffset));
        item.text->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);

        m_plotItems.append(item);
        i++;
    }

    layer(sMapaPuntos)->replot();


}

QPointF CMapaPlot::coordToPixel(const QPointF &coord) const {
    return QPointF(
        xAxis->coordToPixel(coord.x()),  // Usar método del eje X
        yAxis->coordToPixel(coord.y())   // Usar método del eje Y
        );
}

void CMapaPlot::updateEllipsePositions()
{
    //    //home
    if(!(puntoItem.text == nullptr))
    {
        QPointF center = projection->forward(m_posHome.latitude(), m_posHome.longitude());
        QPointF pixelCenter = coordToPixel(center);
        // Crear un círculo verde de 50px
        puntoItem.ellipse->topLeft->setPixelPosition (QPointF(pixelCenter.x ()- 20, pixelCenter.y ()+ 20));   // Esquina superior izquierda
        puntoItem.ellipse->bottomRight->setPixelPosition(QPointF(pixelCenter.x () + 20, pixelCenter.y () - 20)); // Esquina inferior derecha (diámetro 50px)
        puntoItem.ellipse2->topLeft->setPixelPosition (QPointF(pixelCenter.x (), pixelCenter.y ()));   // Esquina superior izquierda
        puntoItem.ellipse2->bottomRight->setPixelPosition(QPointF(pixelCenter.x () , pixelCenter.y () )); // Esquina inferior derecha (diámetro 50px)

        // Crear texto centrado sobre el círculo
        puntoItem.text->position->setCoords(center.x (), center.y ()); // Mismo centro que el círculo
        puntoItem.text->setPositionAlignment(Qt::AlignCenter);
        puntoItem.text->setTextAlignment(Qt::AlignCenter);
    }

    if(!m_listPuntosRutaVisual || !m_listPuntosRutaVisual->data()) return;

    //     updateAspectRatio(); // Actualizar relación de aspecto en cada actualización

    //    const double ellipseSize = radioAcercamiento;
    const double textOffset = 20.0;

    QSharedPointer<QCPCurveDataContainer> data = m_listPuntosRutaVisual->data();
    for(int i=0; i<data->size() && i<m_plotItems.size(); ++i)
    {
        QPointF center(data->at(i)->key, data->at(i)->value);
        QPointF pixelCenter = coordToPixel(center);
        //        // Actualizar elipse
        m_plotItems[i].ellipse->topLeft->setCoords (center.x () - radioAcercamiento, center.y () + radioAcercamiento);
        m_plotItems[i].ellipse->bottomRight->setCoords(center.x () + radioAcercamiento, center.y () - radioAcercamiento);
        m_plotItems[i].ellipse2->topLeft->setCoords (center.x (), center.y ());
        m_plotItems[i].ellipse2->bottomRight->setCoords(center.x (), center.y ());

        // Actualizar texto
        m_plotItems[i].text->position->setPixelPosition(pixelCenter + QPointF(0, textOffset));
    }


}

E_PuntoItem CMapaPlot::getPuntoItem() const
{
    return puntoItem;
}

void CMapaPlot::updateLinePosition() {

    // Convertir punto inicial a píxeles
    QPointF startPixels(
                this->xAxis->coordToPixel(posPixelsDron.x()),
                this->yAxis->coordToPixel(posPixelsDron.y())
                );
    // Convertir punto chequeo proximo a píxeles
    QPointF endPixels(
                this->xAxis->coordToPixel(posCheckProximo.x()),
                this->yAxis->coordToPixel(posCheckProximo.y())
                );
    // Actualizar la línea al proximo punto
    double dx = endPixels.x () - startPixels.x ();
    double dy = endPixels.y ()- startPixels.y ();
    double longitud = sqrt(dx*dx + dy*dy);
    if (longitud > 0) {
        double escala = this->width ()/ longitud;//20 pixeles
        double px_final = startPixels.x () + dx * escala;
        double py_final = startPixels.y () + dy * escala;

        double x_final = xAxis->pixelToCoord(px_final);
        double y_final = yAxis->pixelToCoord(py_final);

        lineaRumboDestino->start->setCoords(posPixelsDron.x (), posPixelsDron.y ());
        lineaRumboDestino->end->setCoords(x_final, y_final);
//        lineaRumboDestino->setVisible(true);
    } else {
//        lineaRumboDestino->setVisible(false);
    }

    //    // Punto Rumbo GPS*******************************
    // 2. Actualizar línea de azimut
    if (azimuthGPS >= 0) {  // Usar valor negativo para ocultar
        // Convertir ángulo a coordenadas de píxeles
        double anguloRad = qDegreesToRadians(azimuthGPS);
        double dxAz = this->width () * sin(anguloRad);
        double dyAz = -this->width () * cos(anguloRad); // Negativo por eje Y invertido

        // Calcular posición final
        double xAzFinal = xAxis->pixelToCoord(startPixels.x () + dxAz);
        double yAzFinal = yAxis->pixelToCoord(startPixels.y () + dyAz);

        lineaRumboVehiculo->start->setCoords(posPixelsDron.x (), posPixelsDron.y ());
        lineaRumboVehiculo->end->setCoords(xAzFinal, yAzFinal);
//        lineaRumboVehiculo->setVisible(true);
    } else {
//        lineaRumboVehiculo->setVisible(false);
    }

    // Punto Rumbo Calculado AP************************
    if (azimuthC >= 0) {  // Usar valor negativo para ocultar
        // Convertir ángulo a coordenadas de píxeles
        double anguloRad = qDegreesToRadians(azimuthC);
        double dxAz = this->width () * sin(anguloRad);
        double dyAz = -this->width () * cos(anguloRad); // Negativo por eje Y invertido

        // Calcular posición final
        double xAzFinal = xAxis->pixelToCoord(startPixels.x () + dxAz);
        double yAzFinal = yAxis->pixelToCoord(startPixels.y () + dyAz);

        lineaRumboCalculado->start->setCoords(posPixelsDron.x (), posPixelsDron.y ());
        lineaRumboCalculado->end->setCoords(xAzFinal, yAzFinal);
        lineaRumboCalculado->setVisible(true);
    } else {
        lineaRumboCalculado->setVisible(false);
    }
}


float CMapaPlot::getRadioAcercamiento() const
{
    return radioAcercamiento;
}

void CMapaPlot::setRadioAcercamiento(float value)
{
    radioAcercamiento = value;
    updateEllipsePositions ();
    layer(sMapaPuntos)->replot();
}

void CMapaPlot::devuelveItemVehiculoSeleccionado()
{
    QCPItemPixmap* itemVehiculo = (QCPItemPixmap*)sender();

    foreach (CVehiculo* vehiculo, m_listVehiculos)
    {
        if (itemVehiculo->objectName() == vehiculo->getNombre())
        {
            emit sig_devuelveNombreVehiculoSeleccionado(vehiculo->getNombre());
            break;
        }
    }
}

void CMapaPlot::slot_renderRectangle(QCPRange pX, QCPRange pY)
{
    Q_UNUSED(pY)Q_UNUSED(pX)
}

void CMapaPlot::slot_actualiza_rangos(QCPRange rangoX, QCPRange rangoY)
{
    xAxis->setRange(rangoX);
    yAxis->setRange(rangoY);
    Cargar_Imagenes();
    replot();
}

void CMapaPlot::MedirDistancia(QPointF punto1, QPointF punto2)
{
    double dist, azim;
    QString distancia, azimuth;
    QGeoCoordinate geoPunto1 = projection->inverse(punto1.x(),punto1.y());
    QGeoCoordinate geoPunto2 = projection->inverse(punto2.x(),punto2.y());

    dist = geoPunto1.distanceTo(geoPunto2);
    azim = geoPunto1.azimuthTo(geoPunto2);

    if (dist >= 1000)
    {
        dist = dist / 1000.0; //------> conviertiendo a Km
        distancia = QString::number(dist,'f',3) + " Km";
    }
    else
        distancia = QString::number(dist) + " m";

    azimuth = QString::number(azim,'f',1) + " º";

    emit sig_enviaMedicionAzimuthDistancia(azimuth,distancia);
}

void CMapaPlot::AccionarMedirDistancia()
{
    if (ACCIONES == aMedirDistancia)
    {
        ACCIONES = aNADA;
        bEnAccion = false;

        lineaDistancia->setVisible(false);
        layer(sMapaZOOM)->replot();
    }
    else
    {
        ACCIONES = aMedirDistancia;
    }
}

void CMapaPlot::AccionarZoomArea()
{
    if (ACCIONES == aZoomArea)
    {
        ACCIONES = aNADA;
    }
    else
    {
        ACCIONES = aMedirDistancia;
        AccionarMedirDistancia();
        ACCIONES = aZoomArea;
    }
}

void CMapaPlot::AccionarCrearPoligono()
{
    levantarVentanaCreaPoligonos();

    if (ACCIONES == aPintarPoligono)
        ACCIONES = aNADA;
    else
    {
        ACCIONES = aPintarPoligono;
    }
}

void CMapaPlot::AccionarNuevoPunto()
{
    m_ventanaNewPunto->show();
}

void CMapaPlot::AccionarZoomMas()
{
    if (tipoBD == ImagenesOSM)  //--> OSM_IMG solo tenemos hasta el z16
    {
        if (ZoomValido < 16)
        {
            //            if(ZoomValido < 17)
            //            {
            QCPRange range;
            range = xAxis->range();
            //    if (range.upper + 0.85 < xMap2 )
            xAxis->scaleRange(0.85, (range.lower + range.upper) / 2);

            range = yAxis->range();
            //    if (range.upper + 0.85< yMap2)
            yAxis->scaleRange(0.85, (range.lower + range.upper) / 2);
            //            }
            Cargar_Imagenes();
            replot();
            repintarPuntos();
            repintarVehiculos();
            if(layer(sMapaZOOM)->visible())
                layer(sMapaZOOM)->replot();
        }
    }
    else
    {
        if(ZoomValido < 17)
        {
            QCPRange range;
            range = xAxis->range();
            //    if (range.upper + 0.85 < xMap2 )
            xAxis->scaleRange(0.85, (range.lower + range.upper) / 2);

            range = yAxis->range();
            //    if (range.upper + 0.85< yMap2)
            yAxis->scaleRange(0.85, (range.lower + range.upper) / 2);

            Cargar_Imagenes();
            replot();
            repintarPuntos();
            repintarVehiculos();
            if(layer(sMapaZOOM)->visible())
                layer(sMapaZOOM)->replot();
        }
    }
}

void CMapaPlot::AccionarZoomMenos()
{
    if(ZoomValido > 5)
    {
        QCPRange range;
        range = xAxis->range();
        //    if (range.lower - 1.17647058823529 > xMap1 && range.upper)
        xAxis->scaleRange(1.17647058823529, (range.lower + range.upper) / 2);

        range = yAxis->range();
        //    if (range.lower - 1.17647058823529 > yMap1)
        yAxis->scaleRange(1.17647058823529, (range.lower + range.upper) / 2);

        Cargar_Imagenes();
        replot();
        repintarPuntos();
        repintarVehiculos();
        if(layer(sMapaZOOM)->visible())
            layer(sMapaZOOM)->replot();
    }
}

void CMapaPlot::AccionarSeleccionarPuntoEnMapa()
{
    ACCIONES = aSeleccionarUbicacionEnMapa;
}

void CMapaPlot::AccionarPintarRuta()
{
    if (ACCIONES == aTrazaRuta)
        ACCIONES = aNADA;
    else
    {
        ACCIONES = aTrazaRuta;
    }
}

void CMapaPlot::nuevoPunto(TipoPunto tipoPunto, QGeoCoordinate pos, EstadoDatos estadoActualizacion, QString nombrePunto, QColor colorTrazaTrayect, QPixmap simbolo, QString descrip, bool visibleDescrip, float velocidad, float rumbo, float altura)
{
    //    CPunto* newPunto;
    //    CVehiculo *newVehiculo;
    bool b_existe = false;

    switch (tipoPunto)
    {
    case VehiculoAereo:
    case VehiculoNaval:
    case VehiculoTerrestre:
    {

        for (int i = 0;i < getListVehiculos().size(); i++)
        {
            CVehiculo *target = getListVehiculos().at(i);

            if (nombrePunto == target->getNombre())
                b_existe = true;
            if (b_existe)
            {

                target->setEstadoDatos(estadoActualizacion);
                if (estadoActualizacion == Actualizado && !b_SeCambioCapa)
                    target->addNewPuntoToTrayect(pos,QDateTime::currentDateTime().toMSecsSinceEpoch(),rumbo);

                QPixmap simb = rotarImagen(simbolo,rumbo);
                target->setSimbolo(simb);

                foreach (QCPItemPixmap *simbItem, m_listVehiculosGraficados)
                {
                    if (simbItem->objectName() == target->getNombre())
                    {
                        double xSimbolo = simbItem->topLeft->coords().x();
                        double ySimbolo = simbItem->topLeft->coords().y();
                        if ( (xSimbolo >= xAxis->range().lower) && (xSimbolo <= xAxis->range().upper) && (ySimbolo >= yAxis->range().lower) && (ySimbolo <= yAxis->range().upper))
                        {
                            QPointF posGooMerc = projection->forward(pos.latitude(), pos.longitude());
                            QPointF posSimbCentro = convertirPosCoordSimboloDe_IzqSupACentro(posGooMerc,simb);

                            simbItem->topLeft->setCoords(posSimbCentro);
                            simbItem->setPixmap(simb);
                            simbItem->setAntialiased(true);

                            // Revisar

                            foreach (QCPItemText* descripVehiculo, m_ListTextVehiculo)
                            {
                                //                                descripVehiculo->setSelected(false);
                                descripVehiculo->setVisible(visibleDescrip);
                                //                                descripVehiculo;
                                if (pos.latitude() != 0)
                                {
                                    QPointF posPixel = projection->forward(pos);
                                    int xPixel = xAxis->coordToPixel(posPixel.x());
                                    int yPixel = yAxis->coordToPixel(posPixel.y());
                                    QPointF posPixelNew;
                                    posPixelNew.setX(xAxis->pixelToCoord(xPixel + 50));
                                    posPixelNew.setY(yAxis->pixelToCoord(yPixel - 20));


                                    descripVehiculo->position->setCoords(posPixelNew);

                                    descripVehiculo->setText(descrip);
                                    foreach (QCPItemLine * lineDescrip, m_ListLineDescripVehiculo)
                                    {
                                        if (lineDescrip->objectName() == descripVehiculo->objectName())
                                        {
                                            if (lineDescrip->visible())
                                            {
                                                int xPixelMin = xAxis->coordToPixel(simbItem->topLeft->coords().x());
                                                int yPixelMin = yAxis->coordToPixel(simbItem->topLeft->coords().y());

                                                int xPixelMax = xPixelMin + simb.width();
                                                int yPixelMax = yPixelMin + simb.height();

                                                int xPixelDif = (xPixelMin + xPixelMax) / 2;
                                                int yPixelDif = (yPixelMin + yPixelMax) / 2;
                                                int xCoordDif = xAxis->pixelToCoord(xPixelDif);
                                                int yCoordDif = yAxis->pixelToCoord(yPixelDif);

                                                QPointF coordCentro = QPointF(xCoordDif, yCoordDif);
                                                lineDescrip->start->setCoords(coordCentro);

                                                if (rumbo >= 0 && rumbo <= 90) lineDescrip->end->setParentAnchor(descripVehiculo->topRight);
                                                if (rumbo > 90 && rumbo <= 180) lineDescrip->end->setParentAnchor(descripVehiculo->bottomRight);
                                                if (rumbo > 180 && rumbo <= 270) lineDescrip->end->setParentAnchor(descripVehiculo->bottomLeft);
                                                if (rumbo > 270 && rumbo <= 359) lineDescrip->end->setParentAnchor(descripVehiculo->topLeft);

                                                break;
                                            }
                                        }
                                    }
                                }
                            }

                            layer(sMapaPuntos)->replot();
                        }
                        break;
                    }
                }

                foreach (QCPCurve* puntoTray, m_listVehiculosTrayectoria)
                {
                    if (target->getNombre() == puntoTray->objectName())
                    {
                        b_existe = true;
                        int tamTray = target->getListTrayect().length();
                        int cantTrayVisible = target->getCantPuntosTrayecAVisualizar(); //* 2;

                        p.setColor(target->getColorTrazaTrayectoria());
                        p.setDashPattern(dashes);
                        puntoTray->setPen(p);

                        QPointF dataPos = projection->forward(((CVehiculo*)target)->getListTrayect().last().pos);

                        if (estadoActualizacion == SinActualizacion)
                        {
                            puntoTray->pen().color().setAlpha(50);
                            layer(sMapaTargets)->replot();
                            return;
                        }
                        // Para visualizar solo la cantidad deseada de puntos de trayectoria
                        if (tamTray <= cantTrayVisible)
                        {
                            puntoTray->addData(dataPos.x(),dataPos.y());
                        }
                        else
                        {
                            puntoTray->pen().setDashPattern(dashes);
                            puntoTray->setData(QVector<double> (0) , QVector<double> (0));
                            for (int i = (tamTray >= cantTrayVisible ? tamTray - cantTrayVisible : 0); i < tamTray; i++)
                            {
                                dataPos = projection->forward(((CVehiculo*)target)->getListTrayect().at(i).pos);
                                puntoTray->addData(dataPos.x(),dataPos.y());
                            }
                        }
                        break;
                    }
                }
                layer(sMapaTargets)->replot();
                //                repintarVehiculos();
                //                m_bdMapas->insertarNuevaTrayectoria(nombrePunto,pos.latitude(),pos.longitude(),target->getListTrayect().last().time.toString());

                break;
            }
        }
        break;
    }
    case PuntoInteres:
    {
        foreach (CPunto* punto, m_listPuntos)
        {
            if (punto->getNombre() == nombrePunto)
            {
                punto->setGeoPos(pos);
                punto->setFechaCreacion(QDateTime::currentDateTime().toString());
                punto->setDescripcion(descrip);
                //                m_bdMapas->guardarNuevoPunto(punto); //revisar para personalizar segun tipo de punto.

                foreach (QCPItemText * textVehiculo, m_ListTextVehiculo)
                {
                    if (textVehiculo->objectName() == punto->getNombre())
                    {
                        if (textVehiculo->visible())
                        {
                            textVehiculo->setText(descrip);
                            textVehiculo->setPositionAlignment (Qt::AlignCenter);
                            textVehiculo->setTextAlignment (Qt::AlignCenter);
                            QPointF posPixel = projection->forward(pos);
                            int xPixel = xAxis->coordToPixel(posPixel.x());
                            int yPixel = yAxis->coordToPixel(posPixel.y());
                            QPointF posPixelNew;
                            posPixelNew.setX(xAxis->pixelToCoord(xPixel + 10));
                            posPixelNew.setY(yAxis->pixelToCoord(yPixel));

                            textVehiculo->position->setCoords(posPixelNew);

                            break;
                        }
                    }
                }
                b_existe = true;
                break;
            }
        }

        break;
    }
    }

    if (!b_existe)
    {
        QString layerPunto = sMapaPuntos;
        //            float rumbo = 0;

        QPointF posPixel = projection->forward(pos);
        int xPixel = xAxis->coordToPixel(posPixel.x());
        int yPixel = yAxis->coordToPixel(posPixel.y());
        QPointF posPixelNew;
        posPixelNew.setX(xAxis->pixelToCoord(xPixel + 50));
        posPixelNew.setY(yAxis->pixelToCoord(yPixel - 20));

        QCPItemPixmap * puntoItem = new QCPItemPixmap(this);
        QCPItemText * puntoTexto = new QCPItemText(this);
        puntoTexto->setVisible(visibleDescrip);
        puntoTexto->setLayer(layerPunto);
        puntoTexto->setPositionAlignment (Qt::AlignCenter);
        puntoTexto->setTextAlignment(Qt::AlignCenter);
        puntoTexto->setPen(QPen(QColor(Qt::yellow)));
        puntoTexto->setBrush(QBrush(QColor(Qt::black),Qt::SolidPattern));
        puntoTexto->setObjectName(nombrePunto);
        puntoTexto->setSelectable(true);
        puntoTexto->position->setCoords(posPixelNew);

        m_ListTextVehiculo.append(puntoTexto);

        QCPItemLine* lineDescrip = new QCPItemLine(this);
        lineDescrip->setVisible(false);
        lineDescrip->setLayer(layerPunto);
        lineDescrip->setObjectName(nombrePunto);
        lineDescrip->setHead(QCPLineEnding::esNone);
        lineDescrip->setTail(QCPLineEnding::esNone);
        lineDescrip->setPen(QPen(QColor(tipoBD == ImagenesSatelitales ? Qt::yellow : Qt::cyan)));
        lineDescrip->start->setCoords(posPixelNew.x(),posPixelNew.y());
        lineDescrip->end->setParentAnchor(puntoTexto->bottomLeft);

        m_ListLineDescripVehiculo.append(lineDescrip);

        QCPCurve* listaTray = new QCPCurve(xAxis,yAxis);

        QPen pen;

        switch (tipoPunto)
        {
        case VehiculoAereo:
        case VehiculoNaval:
        case VehiculoTerrestre:
        {
            layerPunto = sMapaTargets;
            //                rumbo = ((CVehiculo*)puntoNew)->getRumbo();
            m_listVehiculosGraficados.append(puntoItem);
            m_listVehiculosTrayectoria.append(listaTray);
            pen.setWidthF(1);
            pen.setDashPattern(dashes);

            CVehiculo* newVehiculo = new CVehiculo(TipoPunto(tipoPunto),nombrePunto,pos,tipoBD == ImagenesSatelitales ? Qt::yellow : colorTrazaTrayect,velocidad,rumbo,altura);
            newVehiculo->setSimbolo(simbolo);
            newVehiculo->setNumero(m_listVehiculos.size() + 1);
            newVehiculo->setCantPuntosTrayecAVisualizar(20);
            newVehiculo->setEstadoDatos(estadoActualizacion);

            m_listVehiculos.append(newVehiculo);

            //            m_bdMapas->guardarNuevoPunto(newVehiculo); //revisar para personalizar segun tipo de punto.

            //            qDebug() << newVehiculo->getNombre() << ": nuevo barco (" << newVehiculo->getGeoPos() << ")";

            pen.setColor(newVehiculo->getColorTrazaTrayectoria());
            qDebug()<<descrip<<"libMapa";
            puntoTexto->setText(descrip);
            puntoTexto->setPositionAlignment (Qt::AlignCenter);
            puntoTexto->setTextAlignment (Qt::AlignCenter);
            puntoTexto->setColor(Qt::yellow);

            break;
        }
        case PuntoInteres:
        {
            CPunto* newPunto = new CPunto();
            newPunto->setTipoPunto(PuntoInteres);
            newPunto->setSimbolo(simbolo);
            newPunto->setNombre(nombrePunto);
            newPunto->setGeoPos(pos);
            newPunto->setEstadoDatos(estadoActualizacion);
            //            qDebug() << "Punto Interes: " << pos;
            newPunto->setDescripcion(descrip);
            newPunto->setFechaCreacion(QDateTime::currentDateTime().toString());
            newPunto->setNumero(m_listPuntos.size() + 1);
            m_listPuntos.append(newPunto);

            puntoTexto->setText(descrip);
            puntoTexto->setPositionAlignment (Qt::AlignCenter);
            puntoTexto->setTextAlignment (Qt::AlignCenter);
            puntoTexto->setColor(Qt::yellow);

            m_bdMapas->guardarNuevoPunto(newPunto); //revisar para personalizar segun tipo de punto.

            m_listPuntosGraficados.append(puntoItem);
            m_listPuntosTrayectoria.append(listaTray);
            break;
        }
        }
        QPointF posPunto = projection->forward(pos);

        listaTray->setObjectName(nombrePunto);
        listaTray->addData(posPunto.x(),posPunto.y());
        listaTray->setPen(pen);
        listaTray->setVisible(true);

        listaTray->setLayer(layerPunto);

        puntoItem->setLayer(layerPunto);
        puntoItem->setObjectName(nombrePunto);

        QPixmap simb = rotarImagen(simbolo,rumbo);

        QPointF posGooMerc = projection->forward(pos.latitude(), pos.longitude());
        QPointF posSimbCentro = convertirPosCoordSimboloDe_IzqSupACentro(posGooMerc,simb);

        puntoItem->topLeft->setCoords(posSimbCentro);
        puntoItem->setPixmap(simb);
        puntoItem->setAntialiased(true);
        puntoItem->setVisible(true);
        layer(layerPunto)->setVisible(true);
    }
    repintarPuntos();
    layer(sMapaTargets)->replot();
}

void CMapaPlot::pintaElipseHome(QGeoCoordinate posHome)
{
    m_posHome = posHome;
    QPointF center = projection->forward(posHome.latitude(), posHome.longitude());
    QPointF pixelCenter = coordToPixel(center);
    // Crear un círculo verde de 50px
    puntoItem.ellipse = new QCPItemEllipse(this);
    puntoItem.ellipse->topLeft->setPixelPosition (QPointF(pixelCenter.x ()- 20, pixelCenter.y ()+ 20));   // Esquina superior izquierda
    puntoItem.ellipse->bottomRight->setPixelPosition(QPointF(pixelCenter.x () + 20, pixelCenter.y () - 20)); // Esquina inferior derecha (diámetro 50px)
    puntoItem.ellipse->setPen(QPen(Qt::green)); // Sin borde

    puntoItem.ellipse2 = new QCPItemEllipse(this);
    puntoItem.ellipse2->topLeft->setPixelPosition (QPointF(pixelCenter.x (), pixelCenter.y ()));   // Esquina superior izquierda
    puntoItem.ellipse2->bottomRight->setPixelPosition(QPointF(pixelCenter.x (), pixelCenter.y ())); // Esquina inferior derecha (diámetro 50px)
    puntoItem.ellipse2->setPen(QPen(Qt::green)); // Sin borde

    // Crear texto centrado sobre el círculo
    puntoItem.text = new QCPItemText(this);
    puntoItem.text->position->setCoords(center.x (), center.y ()); // Mismo centro que el círculo
    puntoItem.text->setPositionAlignment(Qt::AlignCenter);
    puntoItem.text->setText("H");
    puntoItem.text->setTextAlignment(Qt::AlignCenter);

    QFont font;
    font.setPixelSize(40); // Ajusta el tamaño para que quepa en el círculo
    puntoItem.text->setFont(font);
    puntoItem.text->setColor(Qt::white); // Color del texto

    puntoItem.ellipse->setLayer (sMapaZOOM);
    puntoItem.ellipse2->setLayer (sMapaZOOM);
    puntoItem.text->setLayer (sMapaZOOM);

    layer (sMapaZOOM)->replot ();
}

void CMapaPlot::pintaRumbosVehiculo(QGeoCoordinate posVehiculo,
                                    int Realimentacion_Curso,
                                    int Curso_Deseado,
                                    QGeoCoordinate posProxPunto,
                                    QColor colorRumboVehiculo,
                                    QColor colorRumboCalculado,
                                    QColor colorRumboDestino,
                                    bool automatico )
{
    pen1.setColor (colorRumboVehiculo);
    pen1.setWidth (3);
    pen2.setColor (colorRumboCalculado);
    pen2.setWidth (3);
    pen3.setColor (colorRumboDestino);
    pen3.setWidth (3);

    lineaRumboVehiculo->setPen (pen1);
    lineaRumboVehiculo->setVisible(automatico);
    lineaRumboDestino->setPen (pen2);
    lineaRumboDestino->setVisible(automatico);
    lineaRumboCalculado->setPen (pen3);
    // RumboGPS
    azimuthGPS = Realimentacion_Curso;
    posPixelsDron = projection->forward (posVehiculo);
    posCheckProximo = projection->forward (posProxPunto);

    // RumboC
    azimuthC = Curso_Deseado;

    updateLinePosition ();    //direccion del movimiento

    layer (sMapaZOOM)->replot ();
}

void CMapaPlot::borrarElementosPorNombre(const QString &nombreElemento) {
    // Buscar y eliminar ITEMS (líneas, texto, pixmaps)
    QList<QCPAbstractItem*> itemsABorrar;
    for (int i = 0; i < this->itemCount(); ++i) {
        QCPAbstractItem *item = this->item(i);
        if (item->objectName () == nombreElemento) {
            itemsABorrar.append(item);
        }
    }
    foreach (QCPAbstractItem *item, itemsABorrar) {

        // Remover de las listas específicas
        if (QCPItemText *textItem = qobject_cast<QCPItemText*>(item)) {
            m_ListTextVehiculo.removeOne(textItem);
        } else if (QCPItemLine *lineItem = qobject_cast<QCPItemLine*>(item)) {
            m_ListLineDescripVehiculo.removeOne(lineItem);
        } else if (QCPItemPixmap *pixmapItem = qobject_cast<QCPItemPixmap*>(item)) {
            m_listPuntosGraficados.removeOne(pixmapItem);
            m_listVehiculosGraficados.removeOne(pixmapItem);
        }
        this->removeItem(item); // Desvincula del gráfico
        //        delete item; // Elimina el objeto (seguro si no hay padre externo)
    }

    // Buscar y eliminar PLOTABLES (curvas)
    QList<QCPAbstractPlottable*> plottablesABorrar;
    for (int i = 0; i < this->plottableCount(); ++i) {
        QCPAbstractPlottable *plottable = this->plottable(i);
        if (plottable->name() == nombreElemento) {
            plottablesABorrar.append(plottable);
        }
    }
    foreach (QCPAbstractPlottable *plottable, plottablesABorrar) {

        if (QCPCurve *curva = qobject_cast<QCPCurve*>(plottable)) {
            m_listVehiculosTrayectoria.removeOne(curva);
        }
        this->removePlottable(plottable); // Desvincula del gráfico
        //        delete plottable; // Elimina el objeto
    }
    foreach (CVehiculo* vehic, m_listVehiculos)
    {
        if(vehic->getNombre () == nombreElemento)
        {
            m_listVehiculos.removeOne (vehic);
            delete vehic; // Elimina el objeto
        }
    }
    layer(sMapaTargets)->replot();
    layer(sMapaPuntos)->replot();

}
void CMapaPlot::eliminarPunto(QString nombre)
{
    bool encontrado = false;
    foreach (CPunto* punto, m_listPuntos)
    {
        foreach (QCPItemPixmap* simbPunto, m_listPuntosGraficados)
        {
            if (punto->getNombre() == nombre && nombre == simbPunto->objectName())
            {
                simbPunto->setPixmap(QPixmap());
                simbPunto->topLeft->setCoords(0,0);
                delete simbPunto;
                encontrado = true;

                foreach (QCPItemLine * lineDescrip, m_ListLineDescripVehiculo)
                {
                    if (lineDescrip->objectName() == simbPunto->objectName())
                    {
                        BorraItem_PorPtro_Layer(this,lineDescrip->layer()->name(),lineDescrip);
                        m_ListLineDescripVehiculo.removeOne(lineDescrip);
                        break;
                    }
                }
                break;
            }
        }
        if (encontrado)
            break;
    }
    if (!encontrado)
    {
        foreach (CVehiculo* vehic, m_listVehiculos)
        {
            foreach (QCPItemPixmap* simbVehic, m_listVehiculosGraficados)
            {
                if (vehic->getNombre() == nombre && nombre == simbVehic->objectName())
                {
                    foreach (QCPCurve* tray, m_listVehiculosTrayectoria)
                    {
                        if (tray->objectName() == simbVehic->objectName())
                        {
                            foreach (QCPItemLine * lineDescrip, m_ListLineDescripVehiculo)
                            {
                                if (lineDescrip->objectName() == simbVehic->objectName())
                                {
                                    foreach (QCPItemText* textoDescrip, m_ListTextVehiculo)
                                    {
                                        if (textoDescrip->objectName() == lineDescrip->objectName())
                                        {
                                            BorraItem_PorPtro_Layer(this,textoDescrip->layer()->name(),textoDescrip);
                                            m_ListTextVehiculo.removeOne(textoDescrip);
                                            break;
                                        }
                                    }
                                    BorraItem_PorPtro_Layer(this,lineDescrip->layer()->name(),lineDescrip);
                                    m_ListLineDescripVehiculo.removeOne(lineDescrip);
                                    break;
                                }
                            }

                            simbVehic->setPixmap(QPixmap());
                            encontrado = true;
                            m_listVehiculos.removeOne(vehic);
                            tray->setData(QVector<double>(0,0),QVector<double>(0));
                            m_listVehiculosTrayectoria.removeOne(tray);
                            m_listVehiculosGraficados.removeOne(simbVehic);
                            delete vehic;
                            BorraItem_PorPtro_Layer(this,simbVehic->layer()->name(),simbVehic);
                            BorraPlottable_PorPtro_Layer(this,tray->layer()->name(),tray);
                            break;
                        }
                    }
                    if (encontrado)
                        break;
                }
                if (encontrado)
                    break;
            }
            if (encontrado)
                break;
        }
    }
    layer(sMapaTargets)->replot();
    layer(sMapaPuntos)->replot();
}

void CMapaPlot::eliminarPunto(TipoPunto tipo)
{

}


void CMapaPlot::Cargar_Imagenes()
{
    QString sCarp;
    QString sTipo;
    ListBDPintar.clear();
    ValidaZoomActivo();
    int iZoom = ZoomValido;
    sTipo.setNum(iZoom,10);

    if (tipoBD == ImagenesSatelitales)
    {
        m_ListBD = Cargar_Base_Datos_Map(ZoomValido,0);//-> BD Img Satelitales
    }

    if (tipoBD == ImagenesOSM)
    {
        if (ZoomValido == 17)
            ZoomValido = 16;
        m_ListBD = Cargar_Base_Datos_Map(ZoomValido,1);//-> BD Img Satelitales
    }


    ListBDPintar = m_ListBD;
    foreach (QCPItemPixmap* pix, ListIMG) removeItem(pix);
    ListIMG.clear();


    double posX;
    double posY;

    for( int j=Nxmin; j<=Nxmax ; j++)
    {
        sCarp.setNum(j,10);
        sCarp = "x"+sCarp;
        for(int i=Nymin ; i<Nymax+1;i++ )
        {
            for(int m=0 ; m<ListBDPintar.size() ; m++)
            {
                if(ListBDPintar.at(m).x == j && ListBDPintar.at(m).y == i)
                {
                    QCPItemPixmap * imagen = new QCPItemPixmap(this);
                    QPointF geoPosXY;

                    imagen->setLayer(sMapaCuba);

                    imagen->setSelectable(false);
                    posX = mcalc.Convertir_Tiles_a_Long(j,ZoomValido-1);
                    posY = mcalc.Convertir_Tiles_a_Lat(i,ZoomValido-1);
                    geoPosXY = projection->forward(posY,posX);
                    imagen->topLeft->setCoords(geoPosXY.x(), geoPosXY.y());

                    posX = mcalc.Convertir_Tiles_a_Long(j+1,ZoomValido-1);
                    posY = mcalc.Convertir_Tiles_a_Lat(i+1,ZoomValido-1);

                    geoPosXY = projection->forward(posY,posX);
                    imagen->bottomRight->setCoords(geoPosXY.x(), geoPosXY.y());
                    ListIMG.append(imagen);
                    QPixmap Pix;

                    Pix.loadFromData(ListBDPintar.at(m).foto);
                    imagen->setScaled(true,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                    imagen->setPixmap(Pix);
                    break;
                }
            }
        }
    }

    layer(sMapaZOOM)->replot();
    //    if (tipoBD == 0)
    //        layer(sMapaCuba)->replot();
    //    if (tipoBD == 1)
    //        layer(sMapaCuba)->replot();
    //    layer(sMapaPuntos)->replot();

}

void CMapaPlot::ponerCantPuntosTrayectoriaVisible(int cantPuntos)
{
    foreach (void* vehiculo, m_listVehiculos)
    ((CVehiculo*)vehiculo)->setCantPuntosTrayecAVisualizar(cantPuntos);
}

void CMapaPlot::ponerCantPuntosTrayectoriaVisibleVehiculo(int cantPuntos, CVehiculo *vehiculo)
{
    vehiculo->setCantPuntosTrayecAVisualizar(cantPuntos);
}

void CMapaPlot::mostrarCapa(QString nombCapa, bool visible)
{
    layer(nombCapa)->setVisible(visible);
    if (layer(nombCapa)->visible())
        layer(nombCapa)->replot();
}

QList<E_Dat_BD> CMapaPlot::Cargar_Base_Datos_Map(int ZoomValido, int tipoBD)
{
    double xMin,yMin,xMax,yMax;
    tomarRangoMapa(xMin,xMax,yMin,yMax);
    QList<E_Dat_BD> lData = m_bdMapas->Cargar_BD_IMG(ZoomValido,tipoBD,Nxmin,Nxmax,Nymin,Nymax);
    return lData;
}

void CMapaPlot::tomarRangoMapa(double &xMin, double &xMax, double &yMin, double &yMax)
{
    xMin = xAxis->range().lower;
    xMax = xAxis->range().upper;
    yMin = yAxis->range().lower;
    yMax = yAxis->range().upper;
}

void CMapaPlot::addNewLayer(const QString newLayer, bool visible)
{
    //creo las layer todas son del tipo buferiadas y se crean de <
    QCPLayer* m_layer = layer(newLayer);
    if (m_layer == nullptr)
    {
        addLayer(newLayer,0,limBelow);
        m_layer = layer(newLayer);
        m_layer->setMode(QCPLayer::lmBuffered);
        m_layer->setVisible(visible);
    }
}

void CMapaPlot::seleccionarTarget(QString idTarget)
{
    bool selected = false;
    QCPItemPixmap * simb;
    foreach (QCPItemPixmap* ItemVeh, m_listVehiculosGraficados)
    {
        ItemVeh->setSelected(false);
        if (idTarget == ItemVeh->objectName())
        {
            ItemVeh->setSelected(true);
            ItemVeh->setSelectedPen(penSelectedBuscador);
            //            layer(sMapaTargets)->replot();
            emit sig_devuelveNombreVehiculoSeleccionado(idTarget);
            selected = true;
            simb = ItemVeh;
        }
    }

    //  Centrar en el mapa el target seleccionado

    //    double upperX = xAxis->range().upper;
    //    double upperY = yAxis->range().upper;
    //    double lowerX = xAxis->range().lower;
    //    double lowerY = yAxis->range().lower;

    //    double xMedia = (upperX - lowerX) / 2;
    //    double yMedia = (upperY - lowerY) / 2;
    //    QPointF puntoPos = simb->topLeft->coords();

    //    xAxis->setRangeLower(puntoPos.x() - xMedia);
    //    xAxis->setRangeUpper(puntoPos.x() + xMedia);
    //    yAxis->setRangeLower(puntoPos.y() - yMedia);
    //    yAxis->setRangeUpper(puntoPos.y() + yMedia);

    replot();
}

void CMapaPlot::loadMapWRJ(QString fileName, const QString slayer, QPen pen)
{
    wrjfile_header h;

    std::ifstream cubaWrj(fileName.toLatin1(), std::ios::in | std::ios::binary);

    if (cubaWrj.is_open())
    {
        wrjfile::read_header(&cubaWrj, h);

        if (h.geometry == '3' || h.geometry == '5') //---> Polilineas y Poligonos
        {
            for (int i = 0; i < h.nFeatures; i++)
            {
                QCPCurve *polyLine = new QCPCurve(xAxis,yAxis);
                polyLine->setPen(QPen(QColor(255,255,255),1));
                polyLine->setBrush(QBrush(pen.color()));
                polyLine->setLayer (slayer);
                int nPoints = 0;
                cubaWrj.read((char*)&nPoints, 4);

                for (int j = 0; j < nPoints; j++)
                {
                    double x,y;

                    wrjfile::read_point(&cubaWrj, x, y);

                    polyLine->addData(x, y);
                }
            }
        }
        cubaWrj.close();
    }
}

void CMapaPlot::wheelEvent(QWheelEvent *event)
{
    int wheelDelta;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    wheelDelta = event->angleDelta().y();
#else
    wheelDelta = event->delta();
#endif

    if (wheelDelta > 0)
        zoommas(event);
    else
        zoommenos(event);

    Cargar_Imagenes();
    //    replot();

}

void CMapaPlot::mouseMoveEvent(QMouseEvent *event)
{
    QPoint posMousePixel = QPoint(event->pos().x(),event->pos().y());
    QPointF posActualGeo = QPointF(yAxis->pixelToCoord(event->pos().y()),
                                   xAxis->pixelToCoord(event->pos().x()));

    QGeoCoordinate posAct = projection->inverse(posActualGeo.x(),posActualGeo.y());
    emit sig_enviaPosActual(posAct);

    if (ACCIONES == aZoomArea && bEnAccion)
    {
        pinta_ZoomArea(QList<QPointF>( ) <<
                       QPointF(IniMousePos.y(),IniMousePos.x()) <<
                       QPointF(posActualGeo.y(),posActualGeo.x()));
    }
    else if (ACCIONES == aMedirDistancia && bEnAccion)
    {
        MedirDistancia(IniMousePos,posActualGeo);
        lineaDistancia->setVisible(true);
        pinta_Lineas(QList<QPointF>( ) << QPointF(IniMousePos.y(),IniMousePos.x()) << QPointF(posActualGeo.y(),posActualGeo.x())/*,QColor(Qt::cyan)*/,lineaDistancia,QCPLineEnding::esBar,QCPLineEnding::esLineArrow);
    }
    else if (ACCIONES == aTrazaRuta)
    {

        setCursor(Qt::PointingHandCursor);
        //        m_textoSeleccionPuntoRutaMousePos->setText(posAct.toString());
        //        m_textoSeleccionPuntoRutaMousePos->setVisible(true);
        //        m_textoSeleccionPuntoRutaMousePos->position->setCoords(posActualGeo.y() + 20, posActualGeo.x()+20);
        //        layer(sMapaZOOM)->replot();
    }
    else if (ACCIONES == aNADA)
    {
        QString nombreBuqueMasCercano = "";
        foreach (CVehiculo* vehic, m_listVehiculos)
        {
            QPointF posVehicCoord = projection->forward(vehic->getGeoPos());
            QPoint posVehicPixel = QPoint(xAxis->coordToPixel(posVehicCoord.x()),yAxis->coordToPixel(posVehicCoord.y()));

            if (posMousePixel.x() < posVehicPixel.x() + 10 && posMousePixel.x() > posVehicPixel.x() - 10 && posMousePixel.y() < posVehicPixel.y() + 10 && posMousePixel.y() > posVehicPixel.y() - 10)
                nombreBuqueMasCercano = vehic->getNombre();
        }
        if (!nombreBuqueMasCercano.isEmpty())
        {
            emit sig_devuelveNombreVehiculoSeleccionado(nombreBuqueMasCercano);
            foreach (QCPItemPixmap* simbVehiculo, m_listVehiculosGraficados)
            {
                simbVehiculo->setSelected(false);
            }

            foreach (QCPItemPixmap* simbVehiculo, m_listVehiculosGraficados)
            {
                if (simbVehiculo->objectName() == nombreBuqueMasCercano)
                {
                    simbVehiculo->setSelectedPen(QPen(QBrush(tipoBD == ImagenesSatelitales ? Qt::cyan : Qt::yellow,Qt::SolidPattern),3));
                    simbVehiculo->setSelected(true);
                    break;
                }
            }
            layer(sMapaTargets)->replot();
        }
        else
        {
            //            foreach (QCPItemText* descripVehiculo, m_ListTextVehiculo)
            //            {
            //                    descripVehiculo->setSelected(false);
            //                    descripVehiculo->setVisible(false);
            //            }
            foreach (QCPItemPixmap* simbVehiculo, m_listPuntosGraficados)
            {
                simbVehiculo->setSelected(false);
                //                simbVehiculo->setVisible(false);
            }

            layer(sMapaPuntos)->replot();
        }

        if(m_selectedItem >= 0) {
            QPointF delta = pixelToCoordDelta(event->pos() - m_dragStart);
            movePlotItem(m_selectedItem, m_originalPosition + delta);
            replot();
            event->accept();
        }
        QCustomPlot::mouseMoveEvent(event);

        setCursor(Qt::CrossCursor);
    }
    else
    {
        QCustomPlot::mouseMoveEvent(event);

        setCursor(Qt::CrossCursor);

    }
}

void CMapaPlot::mousePressEvent(QMouseEvent *event)
{
    Cargar_Imagenes();

    if (ACCIONES == aMedirDistancia || ACCIONES == aPintarPoligono || ACCIONES == aZoomArea)
    {
        IniMousePos = QPointF(yAxis->pixelToCoord(event->pos().y()),
                              xAxis->pixelToCoord(event->pos().x()));
        bEnAccion = true;
    }
    else
    {
        m_selectedItem = findItemNearPos(event->pos());
        if(m_selectedItem >= 0) {
            m_dragStart = event->pos();
            m_originalPosition = QPointF(
                m_listPuntosRutaVisual->data()->at(m_selectedItem)->key,
                m_listPuntosRutaVisual->data()->at(m_selectedItem)->value
                );
            event->accept();
            return;
        }
    }
    QCustomPlot::mousePressEvent(event);
}

void CMapaPlot::mouseReleaseEvent(QMouseEvent *event)
{
    QPoint posMousePixel = QPoint(event->pos().x(),event->pos().y());
    QPointF PuntoFin = QPointF(yAxis->pixelToCoord(event->pos().y()),
                               xAxis->pixelToCoord(event->pos().x()));

    QGeoCoordinate pos = projection->inverse(PuntoFin.x(),PuntoFin.y());
    emit sig_mouseReleaseQPointF(pos);
    //    qDebug()<<"pos"<<pos.latitude ()<<pos.longitude ();

    //---> Seleccionar Ubicacion en el Mapa
    if (ACCIONES == aSeleccionarUbicacionEnMapa)
    {

        emit sig_enviaPosSeleccionadaEnMapa(pos);
        ACCIONES = aNADA;
    }

    //---> Pintar Medicion de Distancia
    if (ACCIONES == aMedirDistancia && bEnAccion)
    {
        MedirDistancia(IniMousePos,PuntoFin);
    }

    //---> Pintar Zoom Area
    if (ACCIONES == aZoomArea)
    {
        ACCIONES = aNADA;
        QPointF p1,p2;
        double xmin1,xmax1,ymin1,ymax1;

        p1.setX(IniMousePos.y());
        p1.setY(IniMousePos.x());
        p2.setX(PuntoFin.y());
        p2.setY(PuntoFin.x());

        xmin1 = p1.x() < p2.x() ? p1.x() : p2.x();
        ymin1 = p1.y() < p2.y() ? p1.y() : p2.y();
        ymax1 = p1.y() < p2.y() ? p2.y() : p1.y();

        int resolX = this->width();
        int resolY = this->height();

        float coef = (float) resolX/ resolY;
        double AspRatioCx = (ymax1 - ymin1) * coef;
        xmax1 = xmin1 + AspRatioCx;

        slot_actualiza_rangos(QCPRange(xmin1,xmax1),QCPRange(ymin1,ymax1));

        pinta_ZoomArea(QList<QPointF>() << QPointF(0,0) << QPointF(0,0));

        repintarPuntos();
        repintarVehiculos();
        emit sig_finZoomArea();
    }

    //---> Click derecho
    if (event->button() == Qt::RightButton && ACCIONES != aTrazaRuta)
    {
        int x = event->pos().x();
        int y = event->pos().y();
        double xMap = xAxis->pixelToCoord(x);
        double yMap = yAxis->pixelToCoord(y);
        emit sig_Enviar_Click_Derecho(QPointF(xMap,yMap));

        //        QGeoCoordinate pos = projection->inverse(PuntoFin.x(),PuntoFin.y());
        emit sig_devuelveWidgetClickDerechoParaDisenoExterno(pos);
    }
    else if (event->button() == Qt::RightButton && ACCIONES == aTrazaRuta)
    {
        if (m_ventanaTrazaRuta == nullptr)
        {
            m_ventanaTrazaRuta = new vTrazaRuta(m_listPuntosRuta,m_plotItems,this);
            connect(m_ventanaTrazaRuta,&vTrazaRuta::devuelveListadoPuntosRuta,[&](QList<EPuntoRuta> ruta){
                m_bdMapas->guardarNuevaRuta(ruta);
            });
        }
        else
        {
            m_ventanaTrazaRuta->setLPuntosRuta(m_listPuntosRuta);
            m_ventanaTrazaRuta->setLTextoPuntosRuta(m_plotItems);
            m_ventanaTrazaRuta->mostrarListadoruta();
            m_ventanaTrazaRuta->show();
        }

        m_listPuntosRuta.clear();
        m_plotItems.clear();
        bEnAccion = false;

        ACCIONES = aNADA;
        return;
    }

    if (ACCIONES == aTrazaRuta)
    {
        //        QCPItemLine* lineaRuta = new QCPItemLine(this);
        //        lineaRuta->setVisible(true);
        //        lineaRuta->setObjectName("ruta");
        //        lineaRuta->setLayer(sMapaZOOM);
        //        lineaRuta->setPen(QPen(QColor(Qt::darkRed)));
        EPuntoRuta punto;
        punto.pos = pos;
        m_listPuntosRuta.append(punto);
        QStringList textoPunto = { "Arial", "Helvetica", "Times" };
        pinta_PuntoRuta(m_listPuntosRuta,textoPunto,true);
        //        if (m_listPuntosRuta.size() > 1)
        //        {
        //            QPointF pAnt(m_listPuntosRuta.at(m_listPuntosRuta.size()-2).x(),m_listPuntosRuta.at(m_listPuntosRuta.size()-2).y());
        //            pinta_Lineas(QList<QPointF>( ) << pAnt << PuntoFin,lineaRuta,QCPLineEnding::esSquare,QCPLineEnding::esLineArrow);
        //        }
    }

    //---> Pintar Poligonos
    if (ACCIONES == aPintarPoligono && bEnAccion)
    {
        //        emit sig_mouseRelease(event);
        setCursor(Qt::CrossCursor);
        pinta_Punto_Poligono(PuntoFin);
        listPuntosPoligono.append(PuntoFin);
    }
    else
    {
        //        emit sig_mouseRelease(event);
        QCustomPlot::mouseReleaseEvent(event);
    }

    // Acciones de interaccion con los puntos
    if (ACCIONES == aNADA)
    {
        if(m_selectedItem >= 0) {
            m_selectedItem = -1;
            event->accept();
        }
        //        if(m_selectedItem >= 0) {
        //                QPointF delta = pixelToCoordDelta(event->pos() - m_dragStart);
        //                movePlotItem(m_selectedItem, m_originalPosition + delta);
        //                replot();
        //                 m_selectedItem = -1;
        //                event->accept();
        //        }
        //Revisar Codigo

        //        QString nombreBuqueMasCercano = "";
        //        foreach (CVehiculo* vehic, m_listVehiculos)
        //        {
        //            QPointF posVehicCoord = projection->forward(vehic->getGeoPos());
        //            QPoint posVehicPixel = QPoint(xAxis->coordToPixel(posVehicCoord.x()),yAxis->coordToPixel(posVehicCoord.y()));

        //            if (posMousePixel.x() < posVehicPixel.x() + 10 && posMousePixel.x() > posVehicPixel.x() - 10 && posMousePixel.y() < posVehicPixel.y() + 10 && posMousePixel.y() > posVehicPixel.y() - 10)
        //                    nombreBuqueMasCercano = vehic->getNombre();
        //        }

        //        if (!nombreBuqueMasCercano.isEmpty())
        //        {
        //            emit sig_devuelveNombreVehiculoSeleccionado(nombreBuqueMasCercano);
        //            foreach (QCPItemText* descripVehiculo, m_ListTextVehiculo)
        //            {
        //                if (descripVehiculo->objectName() == nombreBuqueMasCercano)
        //                {
        //                    descripVehiculo->setSelectedPen(QPen(QBrush(tipoBD == ImagenesSatelitales ? Qt::cyan : Qt::yellow,Qt::SolidPattern),3));
        //                    descripVehiculo->setSelected(true);
        //                    descripVehiculo->setVisible(true);
        //                    break;
        //                }
        //                else
        //                {
        //                    descripVehiculo->setSelected(false);
        //                    descripVehiculo->setVisible(false);
        //                }
        //            }

        //            foreach (QCPItemLine* lineaDescrip, m_ListLineDescripVehiculo)
        //            {
        //                if (lineaDescrip->objectName() == nombreBuqueMasCercano)
        //                {
        //                    lineaDescrip->setVisible(true);
        //                    break;
        //                }
        //                else
        //                    lineaDescrip->setVisible(false);
        //            }

        //            replot();
        //        }
        emit sig_mouseRelease(event);


        QCustomPlot::mouseReleaseEvent(event);
    }

    bEnAccion = false;
}

void CMapaPlot::keyPressEvent(QKeyEvent *event)
{
    int Tecla=event->key();
    if (Tecla==Qt::Key_Up)
    {
        moverMapaDireccionArriba();
    }
    if (Tecla==Qt::Key_Right)
    {
        moverMapaDireccionDer();
    }
    if (Tecla==Qt::Key_Down)
    {
        moverMapaDireccionAbajo();
    }
    if (Tecla==Qt::Key_Left)
    {
        moverMapaDireccionIzq();
    }
    if (Tecla==Qt::Key_Plus)
    {
        AccionarZoomMas();
    }
    if (Tecla==Qt::Key_Minus)
    {
        AccionarZoomMenos();
    }

    //    if (Tecla==Qt::Key_Alt)
    //    {
    //        if (contTeclasConsecutiv = 0)
    //        {
    //            teclas[0] = Qt::Key_Alt;
    //            contTeclasConsecutiv ++ ;
    //        }
    //        else
    //            contTeclasConsecutiv = 0;
    //    }
    //    if (Tecla==Qt::Key_F4)
    //    {
    //        if (contTeclasConsecutiv == 1)
    //        {
    //            teclas[1] = Qt::Key_F4;

    //            QProcess quitarSASNanterior;
    //            quitarSASNanterior.start("taskkill /f /im SASN.exe");
    //        }
    //    }


    update();
}

void CMapaPlot::updateItemPositions() {
    if(!m_listPuntosRutaVisual || !m_listPuntosRutaVisual->data())
        return;

    const double ellipseSize = 15.0;
    const double textOffset = 20.0;

    QSharedPointer<QCPCurveDataContainer> data = m_listPuntosRutaVisual->data();
    for(int i=0; i<data->size() && i<m_plotItems.size(); ++i) {
        QPointF center(data->at(i)->key, data->at(i)->value);
        QPointF pixelCenter = coordToPixel(center);

        // Actualizar elipse
        m_plotItems[i].ellipse->topLeft->setPixelPosition(pixelCenter - QPointF(ellipseSize/2, ellipseSize/2));
        m_plotItems[i].ellipse->bottomRight->setPixelPosition(pixelCenter + QPointF(ellipseSize/2, ellipseSize/2));
        m_plotItems[i].ellipse2->topLeft->setPixelPosition(pixelCenter);
        m_plotItems[i].ellipse2->bottomRight->setPixelPosition(pixelCenter);

        // Actualizar texto
        m_plotItems[i].text->position->setPixelPosition(pixelCenter + QPointF(0, textOffset));
    }
}

QCPItemLine *CMapaPlot::getLineaRumboCalculado() const
{
    return lineaRumboCalculado;
}

QCPItemLine *CMapaPlot::getLineaRumboDestino() const
{
    return lineaRumboDestino;
}

QCPItemLine *CMapaPlot::getLineaRumboVehiculo() const
{
    return lineaRumboVehiculo;
}

void CMapaPlot::movePlotItem(int index, const QPointF &newPos)
{
    if(index < 0 || index >= m_plotItems.size()) return;

    // Actualizar datos de la curva
    if(QSharedPointer<QCPCurveDataContainer> data = m_listPuntosRutaVisual->data()) {
        if(index < data->size()) {
            QCPCurveDataContainer::iterator point = data->begin() + index;
            point->key = newPos.x();
            point->value = newPos.y();
            emit pointMoved (index);
        }
    }

    // Forzar actualización de posiciones
    updateItemPositions();
}

QPointF CMapaPlot::pixelToCoordDelta(const QPoint &pixelDelta)
{
    return QPointF(
        xAxis->pixelToCoord(pixelDelta.x()) - xAxis->pixelToCoord(0),
        yAxis->pixelToCoord(pixelDelta.y()) - yAxis->pixelToCoord(0)
        );
}

int CMapaPlot::findItemNearPos(const QPoint &pos)
{
    const double tolerance = 15.0;

    for(int i=0; i<m_plotItems.size(); ++i) {
        QPointF center = coordToPixel(QPointF(
            m_listPuntosRutaVisual->data()->at(i)->key,
            m_listPuntosRutaVisual->data()->at(i)->value
            ));

        if(QVector2D(pos - center.toPoint()).length() <= tolerance) {
            return i;
        }
    }
    return -1;
}
void CMapaPlot::rellenaProximoPunto(int index)
{
    m_plotItems[index].ellipse->setPen (QPen(QColor(Qt::green),3));
    m_plotItems[index].ellipse2->setPen (QPen(QColor(Qt::green),3));
    m_plotItems[index].ellipse->layer ()->replot ();
}

void CMapaPlot::resizeEvent()
{
    iniMapaPlot(yAxis->range().lower,yAxis->range().upper,xAxis->range().lower,xAxis->range().upper);
}

QBrush CMapaPlot::devuelveTipoRelleno(int tipo)
{
    QBrush bro;
    switch (tipo)
    {
    case 0: bro.setStyle(Qt::NoBrush); return bro;
    case 1: bro.setStyle(Qt::SolidPattern); return bro;
    case 2: bro.setStyle(Qt::Dense5Pattern); return bro;
    case 3: bro.setStyle(Qt::Dense4Pattern); return bro;
    case 4: bro.setStyle(Qt::BDiagPattern); return bro;
    case 5: bro.setStyle(Qt::FDiagPattern); return bro;
    case 6: bro.setStyle(Qt::HorPattern); return bro;
    case 7: bro.setStyle(Qt::VerPattern); return bro;
    case 8: bro.setStyle(Qt::Dense1Pattern); return bro;
    case 9: bro.setStyle(Qt::CrossPattern); return bro;
    case 10: bro.setStyle(Qt::DiagCrossPattern); return bro;
    case 11: bro.setStyle(Qt::RadialGradientPattern); return bro;
    default: bro.setStyle(Qt::NoBrush); return bro;
    }
}

QPointF CMapaPlot::convertirPosCoordSimboloDe_IzqSupACentro(QPointF posGeoExacta, QPixmap simbolo)
{
    int posXMenuPunto = xAxis->coordToPixel(posGeoExacta.x());
    int tamMedioXSimbolo = simbolo.size().width() / 2;
    int difX = qAbs(posXMenuPunto - tamMedioXSimbolo);
    posXMenuPunto = difX;
    int posYMenuPunto = yAxis->coordToPixel(posGeoExacta.y());
    int tamMedioYSimbolo = simbolo.size().height() / 2;
    int difY = qAbs(posYMenuPunto - tamMedioYSimbolo);
    posYMenuPunto = difY;

    QPointF posGooMerc;
    posGooMerc.setX(xAxis->pixelToCoord(posXMenuPunto));
    posGooMerc.setY(yAxis->pixelToCoord(posYMenuPunto));

    return posGooMerc;
}

QList<QCPItemLine *> CMapaPlot::getListLineDescripVehiculo() const
{
    return m_ListLineDescripVehiculo;
}

QList<QCPItemText *> CMapaPlot::getListTextVehiculo() const
{
    return m_ListTextVehiculo;
}

vTrazaRuta *CMapaPlot::getVentanaTrazaRuta() const
{
    return m_ventanaTrazaRuta;
}

QList<CPunto *> CMapaPlot::getListPuntos() const
{
    return m_listPuntos;
}

QList<CVehiculo *> CMapaPlot::getListVehiculos() const
{
    return m_listVehiculos;
}

QCPItemLine *CMapaPlot::getLineaDistancia() const
{
    return lineaDistancia;
}

qint8 CMapaPlot::getACCIONES() const
{
    return ACCIONES;
}

CProjection *CMapaPlot::getProjection() const
{
    return projection;
}

void CMapaPlot::setProjection(CProjection *value)
{
    projection = value;
}

VNuevoPunto *CMapaPlot::ventanaNewPunto() const
{
    return m_ventanaNewPunto;
}

QPixmap CMapaPlot::devuelveImagenRelleno(int tipo)
{
    QPixmap pix;
    switch (tipo)
    {
    case 0: pix.load(QDir::currentPath() + "/Recursos/Iconos/sRelleno.png");return pix;
    case 1: pix.load(QDir::currentPath() + "/Recursos/Iconos/solido.png");return pix;
    case 2: pix.load(QDir::currentPath() + "/Recursos/Iconos/mTupida.png");return pix;
    case 3: pix.load(QDir::currentPath() + "/Recursos/Iconos/mMedia.png");return pix;
    case 4: pix.load(QDir::currentPath() + "/Recursos/Iconos/dDerecha.png");return pix;
    case 5: pix.load(QDir::currentPath() + "/Recursos/Iconos/dIzquierda.png");return pix;
    case 6: pix.load(QDir::currentPath() + "/Recursos/Iconos/horizontal.png");return pix;
    case 7: pix.load(QDir::currentPath() + "/Recursos/Iconos/vertical.png");return pix;
    case 8: pix.load(QDir::currentPath() + "/Recursos/Iconos/matrizP.png");return pix;
    case 9: pix.load(QDir::currentPath() + "/Recursos/Iconos/matriz.png");return pix;
    case 10: pix.load(QDir::currentPath() + "/Recursos/Iconos/matrizD.png");return pix;

    default: pix.load(QDir::currentPath() + "/Recursos/Iconos/sRelleno.png");return pix;
    }
}

QPixmap CMapaPlot::rotarImagen(QPixmap img, float angulo)
{
    if (img.isNull()) {
        return QPixmap();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Para Qt 6 - usar QTransform
    QTransform transform;
    transform.rotate(angulo);
    QPixmap pix = img.transformed(transform, Qt::SmoothTransformation);
#else
    // Para Qt 5 - usar QMatrix (compatible)
    QMatrix matrix;
    matrix.rotate(angulo);
    QPixmap pix = img.transformed(matrix, Qt::SmoothTransformation);
#endif

    return pix;
}

int CMapaPlot::BorraPlottables_Layer(QCustomPlot *pCplot, QString sNameLayer)//ok
{
    int ContBorrados=0;
    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {
        if(qobject_cast<QCPAbstractPlottable*>(Borra))
        {
            pCplot->removePlottable((QCPAbstractPlottable*)Borra);
            ContBorrados++;
        }
    }
    return(ContBorrados);
}

int CMapaPlot::BorraItems_Layer(QCustomPlot *pCplot, QString sNameLayer)//ok
{
    int ContBorrados=0;
    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {
        if(qobject_cast<QCPAbstractItem*>(Borra))
        {
            pCplot->removeItem((QCPAbstractItem*)Borra);
            ContBorrados++;
        }
    }
    return(ContBorrados);
}

int CMapaPlot::BorraPlottables_Items_Layer(QCustomPlot *pCplot, QString sNameLayer)//ok
{
    int ContBorrados=0;
    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {
        if(qobject_cast<QCPAbstractPlottable*>(Borra))
        {
            pCplot->removePlottable((QCPAbstractPlottable*)Borra);
            ContBorrados++;
        }
        else if(qobject_cast<QCPAbstractItem*>(Borra))
        {
            pCplot->removeItem((QCPAbstractItem*)Borra);
            ContBorrados++;
        }
    }
    return(ContBorrados);
}

bool CMapaPlot::BorraPlottable_PorNombre_Layer(QCustomPlot *pCplot, QString sNameLayer, QString sNamePlottable)//ok
{
    bool ok=false;
    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {
        if(qobject_cast<QCPAbstractPlottable*>(Borra))
        {
            if(Borra->objectName().compare(sNamePlottable)==0)
                pCplot->removePlottable((QCPAbstractPlottable*)Borra);
            ok=true;
        }
    }
    return(ok);
}

bool CMapaPlot::BorraItem_PorNombre_Layer(QCustomPlot *pCplot, QString sNameLayer, QString sNameItem)//ok
{
    bool ok=false;

    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {

        if(qobject_cast<QCPAbstractItem*>(Borra))
        {
            if(Borra->objectName().compare(sNameItem)==0)
            {
                pCplot->removeItem((QCPAbstractItem*)Borra);
            }
            ok=true;
        }
    }
    return(ok);
}

bool CMapaPlot::BorraPlottable_PorPtro_Layer(QCustomPlot *pCplot, QString sNameLayer, QCPAbstractPlottable *plottable)//ok
{
    bool ok=false;
    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {
        if(qobject_cast<QCPAbstractPlottable*>(Borra))
        {
            if(Borra == plottable)
                pCplot->removePlottable((QCPAbstractPlottable*)Borra);
            ok=true;
            break;
        }
    }
    return(ok);
}

bool CMapaPlot::BorraItem_PorPtro_Layer(QCustomPlot *pCplot, QString sNameLayer, QCPAbstractItem *item)//ok
{
    bool ok=false;
    for(QCPLayerable *Borra : pCplot->layer(sNameLayer)->children())
    {
        if(qobject_cast<QCPAbstractItem*>(Borra))
        {
            if(Borra == item)
                pCplot->removeItem((QCPAbstractItem*)Borra);
            ok=true;
            break;
        }
    }
    return(ok);
}

QVector<QPointF> CMapaPlot::currentPoints() const {
    QVector<QPointF> points;
    if(m_listPuntosRutaVisual && m_listPuntosRutaVisual->data()) {
        QSharedPointer<QCPCurveDataContainer> data = m_listPuntosRutaVisual->data();
        for(int i=0; i<data->size(); ++i) {
            points.append(QPointF(data->at(i)->key, data->at(i)->value));
        }
    }
    return points;
}



