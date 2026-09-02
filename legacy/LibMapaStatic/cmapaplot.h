#ifndef CMAPAPLOT_H
#define CMAPAPLOT_H

#include <QFile>
#include "qcustomplot.h"
#include "constantes.h"
#include "cprojection.h"
#include "ccalculos.h"
#include "cbdatosmapa.h"
#include <QToolButton>
#include <QLayout>
#include "cpunto.h"
#include "vnuevopunto.h"
#include "vtrazaruta.h"


class ListaZoom
{
public:
    ListaZoom()
    {
        NumZoom=0;
        restX=0;
        restY=0;
        rangoX=0;
        rangoY=0;
        CantX=0;
        CantY=0;
    }

    ListaZoom(int NZ, double rX, double rY, int Cx, int Cy, double ranX, double ranY,
              QVector<double> CuadX, QVector<double> CuadY)
    {
        NumZoom=NZ;
        restX=rX;
        restY=rY;
        CantX=Cx;
        CantY=Cy;
        rangoX=ranX;
        rangoY=ranY;
        CuadGeoLongX=CuadX;
        CuadGeoLatY=CuadY;
    }
    virtual ~ListaZoom(void)
    {

    }

    int NumZoom;
    double restX;
    double restY;
    int CantX;
    int CantY;
    double rangoX;
    double rangoY;
    QVector<double> CuadGeoLongX;
    QVector<double> CuadGeoLatY;
};

class CMapaPlot : public QCustomPlot
{
	Q_OBJECT

public:
    CMapaPlot(QString path,QWidget *parent = 0);
    virtual ~CMapaPlot();

    friend class ListaZoom;

    //Cuando se cambia la capa del mapa
    bool b_SeCambioCapa = false;

    void iniMapaPlot(double latMin, double longMin, double latMax, double longMax);
    bool setVisibleLayer(const QString &slayer, bool visible);
    void addNewLayer(const QString newLayer, bool visible = true);
    void seleccionarTarget(QString idTarget);

    int getTipoBD() const;
    void setTipoBD(int newTipoBD);

    void Cargar_Imagenes();




    QString getPathGeneral() const;
    void setPathGeneral(const QString &pathGeneral);

    VNuevoPunto *ventanaNewPunto() const;

    CProjection *getProjection() const;
    void setProjection(CProjection *value);

    qint8 getACCIONES() const;

    QCPItemLine *getLineaDistancia() const;

    QList<CVehiculo *> getListVehiculos() const;

    QList<CPunto *> getListPuntos() const;

    vTrazaRuta *getVentanaTrazaRuta() const;

    QList<QCPItemText *> getListTextVehiculo() const;

    QList<QCPItemLine *> getListLineDescripVehiculo() const;

    float getRadioAcercamiento() const;
    void setRadioAcercamiento(float value);

    void rellenaProximoPunto(int index);
    E_PuntoItem getPuntoItem() const;

    QCPItemLine *getLineaRumboVehiculo() const;

    QCPItemLine *getLineaRumboDestino() const;

    QCPItemLine *getLineaRumboCalculado() const;

public slots:
    void slot_renderRectangle(QCPRange pX, QCPRange pY);
    void slot_actualiza_rangos(QCPRange rangoX, QCPRange rangoY);
    void pinta_ZoomArea(QList<QPointF> Lpuntos);
    void pinta_Lineas(QList<QPointF> Lpuntos, QCPItemLine *LineaPintar, QCPLineEnding::EndingStyle iniLinea, QCPLineEnding::EndingStyle finLinea);
    void pinta_PuntoRuta(QList<EPuntoRuta> Lpuntos, QStringList &texto, bool visible);

    void devuelveItemVehiculoSeleccionado();

    void MedirDistancia(QPointF punto1, QPointF punto2);
    void AccionarMedirDistancia();
    void AccionarZoomArea();
    void AccionarCrearPoligono();
    void AccionarNuevoPunto();
    void AccionarZoomMas();
    void AccionarZoomMenos();
    void AccionarSeleccionarPuntoEnMapa();
    void AccionarPintarRuta();
    void ponerCantPuntosTrayectoriaVisible(int cantPuntos);
    void ponerCantPuntosTrayectoriaVisibleVehiculo(int cantPuntos, CVehiculo *vehiculo = NULL);

    void mostrarCapa(QString nombCapa, bool visible);
    void nuevoPunto(TipoPunto tipoPunto, QGeoCoordinate pos, EstadoDatos estadoActualizacion, QString nombrePunto, QColor colorTrazaTrayect = Qt::red,  QPixmap simbolo = QPixmap(QDir::currentPath() + "/Recursos/Iconos/07.png"), QString descrip = "", bool visibleDescrip = true, float velocidad = 0, float rumbo = 0, float altura = 0);
    void pintaElipseHome(QGeoCoordinate posHome);
    void pintaRumbosVehiculo(QGeoCoordinate posVehiculo, int rumboVehiculo, int rumboCalculado , QGeoCoordinate posProxPunto, QColor colorRumboVehiculo, QColor colorRumboCalculado, QColor colorRumboDestino, bool automatico); // si no se varia el valor de 360, no se pintara el rumb calculado
    void eliminarPunto(QString nombre);
    void eliminarPunto(TipoPunto tipo);
//    void nuevoPunto(TipoPunto tipoPunto = VehiculoNaval, QGeoCoordinate pos, QString nombrePunto, QColor colorTrazaTrayect = Qt::red,  QPixmap simbolo = QPixmap(QDir::currentPath() + "/Recursos/Iconos/07.png"),float velocidad = 0, float rumbo = 0, float altura = 0);
    // COnexiones de Herramienta Poligonos
    void ChequeaNombrePoligono(const QString &arg1);
    bool ChequeaNombreCorrecto(const QString &nombre);
    void SeleccionaColorPoligono();
    void CancelarCreacionPoligono();
    void CancelarPoligono();
    void AceptarPoligono();
    void agregarNuevoPunto(CPunto* puntoNew);

    // Funcion de repintado de Puntos y trayectorias
    void repintarPuntos();
    void repintarVehiculos();

    // Mover mapa
    void moverMapaDireccionIzq();
    void moverMapaDireccionDer();
    void moverMapaDireccionArriba();
    void moverMapaDireccionAbajo();

    //Funciones para borrar QCPItems (Layerables, item, Plotables)
    int BorraPlottables_Layer(QCustomPlot *, QString sNameLayer);
    int BorraItems_Layer(QCustomPlot *, QString sNameLayer);
    int BorraPlottables_Items_Layer(QCustomPlot *, QString sNameLayer);

    bool BorraPlottable_PorNombre_Layer(QCustomPlot *, QString sNameLayer, QString sNamePlottable);
    bool BorraItem_PorNombre_Layer(QCustomPlot *, QString sNameLayer, QString sNameItem);

    bool BorraPlottable_PorPtro_Layer(QCustomPlot *, QString sNameLayer,QCPAbstractPlottable *plottable);
    bool BorraItem_PorPtro_Layer(QCustomPlot *, QString sNameLayer,QCPAbstractItem *item);


    QVector<QPointF> currentPoints() const;

    void borrarElementosPorNombre(const QString &nombreElemento);
signals:
    void tipoBDChanged();
    void sig_enviaPosActual(QGeoCoordinate pos);
    void sig_enviaPosSeleccionadaEnMapa(QGeoCoordinate pos);
    void sig_finZoomArea();
    void sig_finMedirDistancia();
    void sig_mouseRelease(QMouseEvent * event);
    void sig_mouseReleaseQPointF(QGeoCoordinate pos);
    void sig_Enviar_Click_Derecho(QPointF punto);
    void sig_enviaMedicionAzimuthDistancia(QString azimuth, QString dist);
    // señal que retorna un widget para su diseño externo cuando se da Click Derecho.
    void sig_devuelveWidgetClickDerechoParaDisenoExterno(QGeoCoordinate posGeoMouse);

    void sig_devuelveNombreVehiculoSeleccionado(QString nombreVehiculo);

    //rutas
    void pointMoved(int index);

protected:
    virtual void wheelEvent(QWheelEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void resizeEvent();

    int findItemNearPos(const QPoint &pos);
private:

    void loadMapGEO(const QString &fileName, const QString &slayer, const QPen &pen = QPen(QColor(0,0,255),0));
    void loadMapWRJ(QString fileName, const QString slayer, QPen pen = QPen(QColor(0,0,255),0));

    void Inicializacion(const QString CaminoFich, int NuevaWidthPant, int NuevaHeightPant);
    QList<E_Dat_BD> Cargar_Base_Datos_Map(int ZoomValido, int tipoBD); //0 == Satelital, 1= OSM
    void tomarRangoMapa(double &xMin, double &xMax, double &yMin, double &yMax);

    void ValidaZoomActivo();
    void zoommas(QWheelEvent *event);
    void zoommenos(QWheelEvent *event);

    void eliminarPunto(CPunto* puntoDelete);
    void pintaPuntos();
    QPixmap rotarImagen(QPixmap img, float angulo);

    // Herramienta Poligonos
    void levantarVentanaCreaPoligonos();
    void borraPoligono(QList<QCPCurve *> &LPolig, QString nombPolig);
    void ChequeaNombrePunto(QString arg1);

    void pinta_Poligonos(QList<QPointF> Lpunto, const QString &Texto, const QPen &Pen, QBrush relleno);
    void pinta_Punto_Poligono(QPointF puntoPolig);



    QPixmap devuelveImagenRelleno(int tipo);
    QBrush devuelveTipoRelleno(int tipo);

    QPointF convertirPosCoordSimboloDe_IzqSupACentro(QPointF posGeoExacta, QPixmap simbolo);

    //rutas
    QPointF pixelToCoordDelta(const QPoint &pixelDelta);
    void movePlotItem(int index, const QPointF &newPos);
    void updateItemPositions();

    CProjection *projection;
    bool actualizMapa = false;
    CCalculos mcalc;
    int Nxmax{};
    int Nxmin{};
    int Nymin{};
    int Nymax{};
    quint8 ZoomValido{};
    int tipoBD;
    float RelAspect{};
    ListaZoom *pListZoom{};
    QList<ListaZoom*>m_ListaZoom;
    QCPItemRect * rect;
    QList<E_Dat_BD> ListBDPintar;
    QList<QCPItemPixmap*> ListIMG;
    CBDatos * m_bdMapas;
    QList<E_Dat_BD> m_ListBD;

    qint8 ACCIONES = aNADA; // descripcion en la clase "utiles.h"  en la seccion Acciones Mapa
    bool bEnAccion = false;
    //Pos Geo Mouse
    QPointF IniMousePos;
    QPointF UltimoMousePos;

    //Flecha de medir distancia
    QCPLineEnding FinLineDist;
    QCPLineEnding IniLineDist;
    QPointF pAnterior;
    QCPItemLine *lineaDistancia;

    //herramienta crear poligonos
    //-----------> Ventana de Creacion de Poligonos
    QDockWidget* vPoligono = nullptr;
    QToolButton* bColor = nullptr;
    QComboBox* cbRelleno = nullptr;
    QLineEdit* leNombrePoligon = nullptr;
    QToolButton* bOkPoligon = nullptr;
    QToolButton* bCancelPoligon = nullptr;
    QToolButton* bCerrarHerramienta = nullptr;
    QColor colorPoligono;

    //paths ficheros
    QString m_pathGeneral;

    //Lista de puntos
    QList<CPunto*> m_listPuntos;
    QList<CVehiculo*> m_listVehiculos;
    QList<QCPItemPixmap*> m_listPuntosGraficados;
    QList<QCPItemPixmap*> m_listVehiculosGraficados;
    QList<QCPCurve*> m_listPuntosTrayectoria;
    QList<QCPCurve*> m_listVehiculosTrayectoria;
    QList<QCPItemText*> m_ListTextVehiculo;
    QList<QCPItemLine*> m_ListLineDescripVehiculo;
    QList<QCPItemEllipse*> m_ListElipsesRadio;

    //-----------> Poligonos
    QString nombVerticesTemporales = "vTemp";
    E_DataPoligono ePolig;
    QList<E_DataPoligono> listPoligonoEstruc;
    QList<QCPCurve*> listPoligonos;
    QList<QCPItemText*> listTextPoligonos;
    QList<QPointF> listPuntosPoligono;
    QList<QPointF> listPuntosRuta;
    //----> Poligonos visualizacion
    QList<QCPCurve*> ListPolig;
    QList<QCPCurve*> ListVerticesPolig;
    QList<QCPCurve*> ListVerticesRuta;
    QList<QCPItemText*> ListTextPolig;
    QList<QCPItemText*> ListTextRuta;
    QList<E_DataPoligono> ListDatPolig;

    //nuevo punto (ventana UI)
    VNuevoPunto* m_ventanaNewPunto;
    QList<E_Icons> ListSimbolos;

    //replotear listado de puntos
    QTimer m_timRepintado;

    //Traza Ruta
    vTrazaRuta* m_ventanaTrazaRuta = NULL;
    QList<EPuntoRuta> m_listPuntosRuta;
    QCPCurve* m_listPuntosRutaVisual;
    QList<E_PuntoItem> m_plotItems;
    QList<QCPCurve*> m_listPuntoRutaCurva;
    QCPItemText* m_textoSeleccionPuntoRutaMousePos;

    //Linea Rumbo
    QCPItemLine* lineaRumboVehiculo;
    QCPItemLine* lineaRumboDestino;
    QCPItemLine* lineaRumboCalculado;
    QCPLineEnding lineEnding;
    QPen pen1;
    QPen pen2;
    QPen pen3;

    //Patron dash de la visualizcion de trayectorias
    QVector<qreal> dashes;
    QPen p;
    QPen penSelectedBuscador;
    //Estilo de punto de visualizacion de la trayectoria
    QCPScatterStyle myScatter;


    QPointF coordToPixel(const QPointF &coord) const;
    void updateEllipsePositions();

    //ruta
    E_PuntoItem puntoItem;    
    int m_selectedItem = -1;
    QPoint m_dragStart;
    QPointF m_originalPosition;
    void updateLinePosition();//para recalcular coordenadas del proximo punto en pixels
    QPointF posPixelsDron;//posicion del dron en pixels
    QPointF posCheckProximo;//posicion del proximo punto en pixels
    int lengthPixels = 100;//distancia en pixels
    float azimuthProximoPunto;//azimut del proximo punto
    float azimuthMovimiento;//azimut del gps direccion del movimiento
    float azimuthDeseado;//azimut deseado
    float radioAcercamiento = 20;
    QGeoCoordinate m_posHome;
    float azimuthGPS;//azimut del proximo punto    QPointF posPixelsC;//posicion del dron en pixels
    float azimuthC;//azimut del proximo punto
    QCPScatterStyle *scStyle;//punto dentro de la elipse

};




#endif // CMAPAPLOT_H
