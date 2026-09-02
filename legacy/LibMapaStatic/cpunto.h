#ifndef CPUNTO_H
#define CPUNTO_H

#include <QObject>
#include <QGeoCoordinate>
#include <QMap>
#include <QImage>
#include <QTimer>
#include "utiles.h"

#define TODAS -1
//------- Tipos de Conexion ----------
#define ETHERNET 1
#define RS232    0
#define COORDENADA_LATITUD 0
#define COORDENADA_LONGITUD 1

//estructura para almacenar la posicion y el momento en que se actualizo
//struct EDataBuffer
//{
//    QByteArray Buffer;
//    void* fuente;
//    int timeRx;
//};
struct E_GeoPosTime
{
    QGeoCoordinate pos;
    qint64 time;
    float altura = 0;
    float rumbo = 0;
    float velocidad = 0;
};

enum TipoPunto{PuntoInteres,VehiculoTerrestre,VehiculoAereo,VehiculoNaval};
enum EstadoVehiculo{EnMovimiento,EnPausa,FinalizadaMision};
enum EstadoDatos{SinActualizacion,Actualizado,ListoParaEliminar};

//------------------------------------------ Clase Punto
class CPunto : public QObject
{
    Q_OBJECT
public:
    explicit CPunto(QObject *parent = 0);

    int getTipoPunto() const;
    void setTipoPunto(TipoPunto value);
    void setNumero(int numero);
    void setNombre(QString nombre);
    void setDescripcion(QString Descripcion);
    void setSimbolo(QPixmap Simbolo);
    void setGeoPos(const QGeoCoordinate geoPos);
    //void setTipo(QString sTipo);
    void setFechaCreacion(QString fecha);
    void setEsEstacionT(bool esEstacionT);
    void setEstadoDatos(const EstadoDatos &estadoDatos);

    int getNumero();
    QString getNombre();
    QString getDescripcion();
    QPixmap getSimbolo();
    QGeoCoordinate getGeoPos();
    //QString getTipo();
    QString getFechaCreacion();
    bool getEsEstacionT() const;
    EstadoDatos getEstadoDatos() const;

signals:

public slots:

private:
    int m_Numero;
    QString m_Nombre;
    QString m_Descripcion;
    QPixmap m_Simbolo;
    QGeoCoordinate m_posGeo;
    QString m_FechaCreacion;
    int m_tipoPunto;
    bool m_esEstacionT = false;
    EstadoDatos m_estadoDatos;
};

//------------------------------------------- Clase Vehiculo
class CVehiculo : public CPunto
{
    Q_OBJECT
public:
    CVehiculo();
    CVehiculo(TipoPunto tipoVehiculo, QString nombreVehiculo, QGeoCoordinate pos, QColor colorTrazaTrayect = Qt::red, float velocidad = 0, float rumbo = 0, float altura = 0);

    void inicializarParametros();

    void addNewPuntoToTrayect(QGeoCoordinate posNew, qint64 time, float rumbo = 0, float velocidad = 0);
    int getCantPuntosTrayecAVisualizar() const;
    void setCantPuntosTrayecAVisualizar(int cantPuntosTrayecAVisualizar); // -1 (Todas)
    QList<E_GeoPosTime> getListTrayect() const;

    int getEstado() const;
    void setEstado(EstadoVehiculo estado);

    float getRumbo() const;
    void setRumbo(float rumbo);

    float getVelocidad() const;
    void setVelocidad(float velocidad);

    int getDistanciaTotalRecorrida() const;

    int getVelocidadMaximaAlcanzada() const;

    int getAlturaMaximaAlcanzada() const;

    int getAlturaMinimaAlcanzada() const;

    int getCantParadas() const;

    float getAltura() const;
    void setAltura(float altura);

    QColor getColorTrazaTrayectoria() const;
    void setColorTrazaTrayectoria(const QColor &colorTrazaTrayectoria);

    void addTrayect(E_GeoPosTime m_Trayect);

    int getNumFuente() const;
    void setNumFuente(int numFuente);

private slots:
    virtual void actualizarEstadisticas();

private:

    QList<E_GeoPosTime> m_listTrayect;
    int m_cantPuntosTrayecAVisualizar = 50;
    float m_velocidad;
    float m_rumbo;
    float m_altura;
    int m_estado;
    int m_distanciaTotalRecorrida;
    int m_velocidadMaximaAlcanzada;
    int m_alturaMaximaAlcanzada;
    int m_alturaMinimaAlcanzada;
    int m_cantParadas;

    int m_contadorTiempoParada = 0;
    QTimer tim;

    QColor m_colorTrazaTrayectoria;

    int m_numFuente;
};

//----------------------------------------------  Clase CAvion
class CAvion : public CVehiculo
{
    Q_OBJECT
public:
    CAvion();
    CAvion(QString nombreAvion, QGeoCoordinate pos, float banqueo = 0, float cabeceo = 0, int rpm = 0, float velocidad = 0, float rumbo = 0, float altura = 0);

    float banqueo() const;
    void setBanqueo(float banqueo);

    float cabeceo() const;
    void setCabeceo(float cabeceo);

    int rpm() const;
    void setRpm(int rpm);

private slots:

private:
    float m_banqueo;
    float m_cabeceo;
    int m_rpm;
};

//----------------------------------------------  Clase CBarco

class CBarco : public CVehiculo
{
    Q_OBJECT
public:
    CBarco();
    CBarco(QString nombreBarco, QString MMSI, QGeoCoordinate pos, float rumbo = 0, float vel = 0);

    void inicializarParametrosBarco();

    QString MMSI() const;
    void setMMSI(const QString &MMSI);

    QString motherShipMMSI() const;
    void setMotherShipMMSI(const QString &motherShipMMSI);

    QString IMONumber() const;
    void setIMOnumber(const QString &IMOnumber);

    quint8 AISVersion() const;
    void setAISVersion(const quint8 &AISVersion);

    QString SignalCall() const;
    void setSignalCall(const QString &SignalCall);

    QString VendorID() const;
    void setVendorID(const QString &VendorID);

    QString TipoNavegacion() const;
    void setTipoNavegacion(const QString &TipoNavegacion);

    QString TipoEmbarcacion() const;
    void setTipoEmbarcacion(QString TipoEmbarcacion);

    float DimensionProa() const;
    void setDimensionProa(float DimensionProa);

    float DimensionPopa() const;
    void setDimensionPopa(float DimensionPopa);

    float DimensionBabor() const;
    void setDimensionBabor(float DimensionBabor);

    float DimensionEstribor() const;
    void setDimensionEstribor(float DimensionEstribor);

    float LargoEmbarcacion() const;
    void setLargoEmbarcacion(float LargoEmbarcacion);

    float AnchoEmbarcacion() const;
    void setAnchoEmbarcacion(float AnchoEmbarcacion);

    QString Dia() const;
    void setDia(const QString &Dia);

    QString Mes() const;
    void setMes(const QString &Mes);

    QString Hora() const;
    void setHora(const QString &Hora);

    QString Min() const;
    void setMin(const QString &Min);

    QString paisProcedencia() const;
    void setPaisProcedencia(const QString &paisProcedencia);

    QString destino() const;
    void setDestino(const QString &destino);

    quint8 pertenencia() const;
    void setPertenencia(const quint8 &pertenencia);

    QString estadoNavegacion() const;
    void setEstadoNavegacion(const QString &estadoNavegacion);

    double cursoSobreTierra() const;
    void setCursoSobreTierra(double cursoSobreTierra);

    int indicadorManiobra() const;
    void setIndicadorManiobra(int indicadorManiobra);

    char estadoSeguimiento() const;
    void setEstadoSeguimiento(char estadoSeguimiento);

    QString tipoProvedorPosicion() const;
    void setTipoProvedorPosicion(const QString &tipoProvedorPosicion);

    int tipProvee() const;
    void setTipProvee(int tipProvee);

    float draught() const;
    void setDraught(float draught);

    QString ETA() const;
    void setETA(const QString &ETA);

    QPixmap bandera() const;
    void setBandera(const QPixmap &bandera);

    float heading() const;
    void setHeading(float heading);

    qint64 UTC() const;
    void setUTC(const qint64 &UTC);

    QString nextPortName() const;
    void setNextPortName(const QString &nextPortName);

    QString nextPortLOCODE() const;
    void setNextPortLOCODE(const QString &nextPortLOCODE);

    QString nextPortISO2() const;
    void setNextPortISO2(const QString &nextPortISO2);

    QString nextPortCountry() const;
    void setNextPortCountry(const QString &nextPortCountry);

    QString lastPortCountry() const;
    void setLastPortCountry(const QString &lastPortCountry);

    QString lastLocationName() const;
    void setLastLocationName(const QString &lastLocationName);

    QString lastPortName() const;
    void setLastPortName(const QString &lastPortName);

    QString lastPortLOCODE() const;
    void setLastPortLOCODE(const QString &lastPortLOCODE);

    QString lastPortDeparture() const;
    void setLastPortDeparture(const QString &lastPortDeparture);

    QString lastPortArrival() const;
    void setLastPortArrival(const QString &lastPortArrival);

    QString lastEventTime() const;
    void setLastEventTime(const QString &lastEventTime);

    QString locationName() const;
    void setLocationName(const QString &LocationName);


    double razonGiro() const;
    void setRazonGiro(double razonGiro);

    QString precisionDataAIS() const;
    void setPrecisionDataAIS(const QString &precisionDataAIS);

    QString nombreBuque() const;
    void setNombreBuque(const QString &nombreBuque);


private slots:

private:

    float m_draught;
    QString m_ETA;
    QPixmap m_bandera;
    float m_heading;
    QString m_lastEventTime;
    QString m_lastPortArrival;
    QString m_lastPortDeparture;
    QString m_lastPortLOCODE;
    QString m_lastPortName;
    QString m_lastPortCountry;
    QString m_nextPortCountry;
    QString m_nextPortISO2;
    QString m_nextPortLOCODE;
    QString m_nextPortName;
    qint64 m_UTC;
    QString m_locationName;

    QString m_MMSI;   //------------------------------------- Codigo(MMSI - Servicio Movil de Identidad Maritima) del Objetivo.
    QString m_motherShipMMSI; //---------------------------------- En caso de ser un Vehiculo auxiliar pequeño, este es el MMSI de la embarcacion Madre.
    QString m_IMOnumber;    //--------------------------------------- Numero de IMO (Organización Marítima Internacional)
    quint8 m_AISVersion;   //------------------------------------- Version AIS( 0 = ITU1371 , 1...3 = Ediciones Futuras )
    QString m_SignalCall;  //------------------------------------- Senal de LLamada
    QString m_VendorID;  //---------------------------------------- Identificacion del Vendedor
    QString m_TipoNavegacion;   //--------------------------------- Tipo de Barco.
    QString m_TipoEmbarcacion; //------------------------------------- Valor Numérico del Tipo de Navegación.
    float m_DimensionProa;
    float m_DimensionPopa;
    float m_DimensionBabor;
    float m_DimensionEstribor;
    float m_LargoEmbarcacion;
    float m_AnchoEmbarcacion;
    QString m_Mes,m_Dia,m_Hora,m_Min;
    QString m_paisProcedencia;
    QString m_destino;
    quint8 m_pertenencia;  //---------------------------------------------------------------------------------------------> Objetivo visto por --> 0: RADAR, 1: AIS, 2: RADAR/AIS, 3: MANUAL
    QString m_estadoNavegacion;   //--------------------------------------------------------------------------------------> Estado de la navegación.
    QString m_precisionDataAIS;

    double m_razonGiro;
    double m_cursoSobreTierra;   //-------------------------------------------------------------------------------------------------------> Curso sobre Tierra del objetivo.
    int m_indicadorManiobra; //----------------------------------------------------------------------------------------------------> Indicador de Maniobras.
    char m_estadoSeguimiento;   //----------------------------------------------------------------------------------------> Estado del Seguimiento del objetivo(Tracking - T - , Dudoso - Q - , Perdido - L-).
    QString m_tipoProvedorPosicion; //---------------------------------------> Tipo de Proveedor de Posicion (GPS, Glonass,Combinacion de ambos, Galileo, etc ) ---- Exclusivo para las estaciones en tierra.
    int m_tipProvee;
    QString m_nombreBuque;

};

//----------------------------------------------  Clase CEstacionTerrena
class CEstacionTerrena : public CPunto
{
    Q_OBJECT
private:
    QString m_tipoProvedorGeoPos;
    QString m_MMSI;
    QString m_Mes;
    QString m_Dia;
    QString m_Hora;
    QString m_Min;

public:

    CEstacionTerrena();
    CEstacionTerrena(QString tipoProveedor);
    QString getTipoProvedorGeoPos();

    void setTipoProvedorGeoPos(QString provedor);
    QString getMMSI() const;
    void setMMSI(const QString &MMSI);
    QString getMes() const;
    void setMes(const QString &Mes);
    QString getDia() const;
    void setDia(const QString &Dia);
    QString getHora() const;
    void setHora(const QString &Hora);
    QString getMin() const;
    void setMin(const QString &Min);
};
#endif // CPUNTO_H
