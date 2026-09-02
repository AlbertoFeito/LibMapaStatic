#include "cpunto.h"
#include <QMap>
#include <QDebug>

CPunto::CPunto(QObject *parent) : QObject(parent)
{

}

void CPunto::setNumero(int numero)
{
    m_Numero = numero;
}

void CPunto::setNombre(QString nombre)
{
    m_Nombre = nombre;
}

void CPunto::setDescripcion(QString Descripcion)
{
    m_Descripcion = Descripcion;
}

void CPunto::setSimbolo(QPixmap Simbolo)
{
    m_Simbolo = Simbolo;
}

void CPunto::setGeoPos(const QGeoCoordinate geoPos)
{
    m_posGeo = geoPos;
}

void CPunto::setFechaCreacion(QString fecha)
{
    m_FechaCreacion = fecha;
}

int CPunto::getNumero()
{
    return m_Numero;
}

QString CPunto::getNombre()
{
    return m_Nombre;
}

QString CPunto::getDescripcion()
{
    return m_Descripcion;
}

QPixmap CPunto::getSimbolo()
{
    return m_Simbolo;
}

QGeoCoordinate CPunto::getGeoPos()
{
    return m_posGeo;
}

QString CPunto::getFechaCreacion()
{
    return m_FechaCreacion;
}

bool CPunto::getEsEstacionT() const
{
    return m_esEstacionT;
}

EstadoDatos CPunto::getEstadoDatos() const
{
    return m_estadoDatos;
}

void CPunto::setEstadoDatos(const EstadoDatos &estadoDatos)
{
    m_estadoDatos = estadoDatos;
}

void CPunto::setEsEstacionT(bool esEstacionT)
{
    m_esEstacionT = esEstacionT;
}

int CPunto::getTipoPunto() const
{
    return m_tipoPunto;
}

void CPunto::setTipoPunto(TipoPunto value)
{
    m_tipoPunto = value;
}

// -----------------------------------------------Clase Vehiculo
CVehiculo::CVehiculo() : CPunto()
{
    inicializarParametros();
}

CVehiculo::CVehiculo(TipoPunto tipoVehiculo, QString nombreVehiculo, QGeoCoordinate pos, QColor colorTrazaTrayect, float velocidad, float rumbo, float altura): CPunto()
{
    inicializarParametros();

    setTipoPunto(tipoVehiculo);
    setNombre(nombreVehiculo);
    setVelocidad(velocidad);
    setRumbo(rumbo);
    setAltura(altura);
    addNewPuntoToTrayect(pos,QDateTime::currentDateTime().toMSecsSinceEpoch(),rumbo,velocidad);
    setColorTrazaTrayectoria(colorTrazaTrayect);
    m_distanciaTotalRecorrida = 0;
    m_velocidadMaximaAlcanzada = 0;
    m_cantParadas = 0;

    connect(&tim,SIGNAL(timeout()),this,SLOT(actualizarEstadisticas()));
    tim.start(1000);
}

void CVehiculo::inicializarParametros()
{
    setRumbo(0);
    setVelocidad(0);
    setAltura(0);
    setCantPuntosTrayecAVisualizar(20);
    setNombre("");
    setGeoPos(QGeoCoordinate(0,0));
    setDescripcion("");
    setFechaCreacion(QDateTime::currentDateTime().toString());
}

void CVehiculo::addNewPuntoToTrayect(QGeoCoordinate posNew, qint64 time, float rumbo, float velocidad)
{
    setGeoPos(posNew);
    E_GeoPosTime eData;
    eData.pos = posNew;
    eData.time = time;
    eData.rumbo = rumbo;
    eData.velocidad = velocidad;
    m_listTrayect.append(eData);
    //qDebug() << listTrayect.last().pos << " - " << listTrayect.last().time;
}

int CVehiculo::getEstado() const
{
    return m_estado;
}

void CVehiculo::setEstado(EstadoVehiculo estado)
{
    m_estado = estado;
}

float CVehiculo::getRumbo() const
{
    return m_rumbo;
}

void CVehiculo::setRumbo(float rumbo)
{
    m_rumbo = rumbo;
}

float CVehiculo::getVelocidad() const
{
    return m_velocidad;
}

void CVehiculo::setVelocidad(float velocidad)
{
    m_velocidad = velocidad;
}

QList<E_GeoPosTime> CVehiculo::getListTrayect() const
{
    return m_listTrayect;
}

int CVehiculo::getDistanciaTotalRecorrida() const
{
    return m_distanciaTotalRecorrida; //en Metros
}

int CVehiculo::getVelocidadMaximaAlcanzada() const
{
    return m_velocidadMaximaAlcanzada * 3.6; //convirtiendo a Km/h
}

int CVehiculo::getCantParadas() const
{
    return m_cantParadas;
}

void CVehiculo::actualizarEstadisticas()
{
    double velOld = 0;
    double alturaMin = 0;
    double alturaMax = 0;
    for (int i = 0; i < getListTrayect().size() -1; ++i)
    {
        E_GeoPosTime posTimeEstruct = getListTrayect().at(i);
        E_GeoPosTime posTimeEstructNext = getListTrayect().at(i+1);

        // distancia total
        m_distanciaTotalRecorrida += posTimeEstruct.pos.distanceTo(posTimeEstructNext.pos); // en metros;

        velOld = posTimeEstruct.pos.distanceTo(posTimeEstructNext.pos) / (posTimeEstructNext.time - posTimeEstruct.time); // en metros x seg;

        // velocidad maxima
        if (m_velocidadMaximaAlcanzada < velOld)
            m_velocidadMaximaAlcanzada = velOld;

        // cantidad de paradas
        if (velOld == 0)
        {
            if (m_contadorTiempoParada >= 3)
            {
                m_cantParadas ++;
            }
            else
                m_contadorTiempoParada ++;
        }
        else
            m_contadorTiempoParada = 0;

        // altura
        if(i == 0)
        {
            if (posTimeEstruct.altura < posTimeEstructNext.altura)
            {
                alturaMax = posTimeEstructNext.altura;
                alturaMin = posTimeEstruct.altura;
            }
        }
        else
        {
            if (alturaMax < posTimeEstruct.altura)
                alturaMax = posTimeEstruct.altura;
            if (alturaMin > posTimeEstruct.altura)
                alturaMin = posTimeEstruct.altura;
        }
    }
}

int CVehiculo::getNumFuente() const
{
    return m_numFuente;
}

void CVehiculo::setNumFuente(int numFuente)
{
    m_numFuente = numFuente;
}

void CVehiculo::addTrayect(E_GeoPosTime m_Trayect)
{
    m_listTrayect.append(m_Trayect);
}

QColor CVehiculo::getColorTrazaTrayectoria() const
{
    return m_colorTrazaTrayectoria;
}

void CVehiculo::setColorTrazaTrayectoria(const QColor &colorTrazaTrayectoria)
{
    m_colorTrazaTrayectoria = colorTrazaTrayectoria;
}

int CVehiculo::getAlturaMinimaAlcanzada() const
{
    return m_alturaMinimaAlcanzada;
}

int CVehiculo::getAlturaMaximaAlcanzada() const
{
    return m_alturaMaximaAlcanzada;
}

float CVehiculo::getAltura() const
{
    return m_altura;
}

void CVehiculo::setAltura(float altura)
{
    m_altura = altura;
}

int CVehiculo::getCantPuntosTrayecAVisualizar() const
{
    return m_cantPuntosTrayecAVisualizar;
}

void CVehiculo::setCantPuntosTrayecAVisualizar(int cantPuntosTrayecAVisualizar)
{
    if (cantPuntosTrayecAVisualizar == TODAS)
        m_cantPuntosTrayecAVisualizar = getListTrayect().size();
    else
        m_cantPuntosTrayecAVisualizar = cantPuntosTrayecAVisualizar;
}

//------------------------------------------------> clase CAvion
CAvion::CAvion()
{

}

CAvion::CAvion(QString nombreAvion, QGeoCoordinate pos, float banqueo, float cabeceo, int rpm, float velocidad, float rumbo, float altura)
{
    setTipoPunto(VehiculoAereo);
    setNombre(nombreAvion);
    addNewPuntoToTrayect(pos,QDateTime::currentDateTime().toMSecsSinceEpoch(),rumbo,velocidad);
    setBanqueo(banqueo);
    setCabeceo(cabeceo);
    setRpm(rpm);
    setVelocidad(velocidad);
    setRumbo(rumbo);
    setAltura(altura);
}

float CAvion::banqueo() const
{
    return m_banqueo;
}

void CAvion::setBanqueo(float banqueo)
{
    m_banqueo = banqueo;
}

float CAvion::cabeceo() const
{
    return m_cabeceo;
}

void CAvion::setCabeceo(float cabeceo)
{
    m_cabeceo = cabeceo;
}

int CAvion::rpm() const
{
    return m_rpm;
}

void CAvion::setRpm(int rpm)
{
    m_rpm = rpm;
}

//----------------------------------------------  Clase CEstacionTerrena
CEstacionTerrena::CEstacionTerrena(QString tipoProveedor)
{
    m_tipoProvedorGeoPos = tipoProveedor;
}

CEstacionTerrena::CEstacionTerrena()
{

}

QString CEstacionTerrena::getMin() const
{
    return m_Min;
}

void CEstacionTerrena::setMin(const QString &Min)
{
    m_Min = Min;
}

QString CEstacionTerrena::getHora() const
{
    return m_Hora;
}

void CEstacionTerrena::setHora(const QString &Hora)
{
    m_Hora = Hora;
}

QString CEstacionTerrena::getDia() const
{
    return m_Dia;
}

void CEstacionTerrena::setDia(const QString &Dia)
{
    m_Dia = Dia;
}

QString CEstacionTerrena::getMes() const
{
    return m_Mes;
}

void CEstacionTerrena::setMes(const QString &Mes)
{
    m_Mes = Mes;
}

QString CEstacionTerrena::getMMSI() const
{
    return m_MMSI;
}

void CEstacionTerrena::setMMSI(const QString &MMSI)
{
    m_MMSI = MMSI;
}

QString CEstacionTerrena::getTipoProvedorGeoPos()
{
    return m_tipoProvedorGeoPos;
}

void CEstacionTerrena::setTipoProvedorGeoPos(QString provedor)
{
    m_tipoProvedorGeoPos = provedor;
}


//---------------------------------------------- Clase CBarco
CBarco::CBarco()
{
    inicializarParametrosBarco();
    CVehiculo();
}

CBarco::CBarco(QString nombreBarco, QString MMSI, QGeoCoordinate pos, float rumbo, float vel)
{
    inicializarParametrosBarco();
    CVehiculo(VehiculoNaval,nombreBarco,pos,Qt::red,vel,rumbo);
    setNombre(nombreBarco);
    setMMSI(MMSI);
    setRumbo(rumbo);
    setVelocidad(vel);
    addNewPuntoToTrayect(pos,QDateTime::currentDateTime().toMSecsSinceEpoch(),rumbo,vel);
}

void CBarco::inicializarParametrosBarco()
{
    inicializarParametros();
    setTipoPunto(VehiculoNaval);
    setLargoEmbarcacion(0);
    setAnchoEmbarcacion(0);
    setDimensionPopa(0);
    setDimensionProa(0);
    setDimensionBabor(0);
    setDimensionEstribor(0);
    setMMSI("");
    setIMOnumber("");
    setSignalCall("");
    setPaisProcedencia("");
    setDestino("");
    setDraught(0);
    setHeading(0);
    setVendorID("");
    setRazonGiro(0);
    setNombreBuque("");
    setMotherShipMMSI("");
    setTipoNavegacion("");
    setEstadoNavegacion("");
    setTipoProvedorPosicion("");
}

QString CBarco::MMSI() const
{
    return m_MMSI;
}

void CBarco::setMMSI(const QString &MMSI)
{
    m_MMSI = MMSI;
}

QString CBarco::motherShipMMSI() const
{
    return m_motherShipMMSI;
}

void CBarco::setMotherShipMMSI(const QString &motherShipMMSI)
{
    m_motherShipMMSI = motherShipMMSI;
}

QString CBarco::IMONumber() const
{
    return m_IMOnumber;
}

void CBarco::setIMOnumber(const QString &IMOnumber)
{
    m_IMOnumber = IMOnumber;
}

quint8 CBarco::AISVersion() const
{
    return m_AISVersion;
}

void CBarco::setAISVersion(const quint8 &AISVersion)
{
    m_AISVersion = AISVersion;
}

QString CBarco::SignalCall() const
{
    return m_SignalCall;
}

void CBarco::setSignalCall(const QString &SignalCall)
{
    m_SignalCall = SignalCall;
}

QString CBarco::VendorID() const
{
    return m_VendorID;
}

void CBarco::setVendorID(const QString &VendorID)
{
    m_VendorID = VendorID;
}

QString CBarco::TipoNavegacion() const
{
    return m_TipoNavegacion;
}

void CBarco::setTipoNavegacion(const QString &TipoNavegacion)
{
    m_TipoNavegacion = TipoNavegacion;
}

QString CBarco::TipoEmbarcacion() const
{
    return m_TipoEmbarcacion;
}

void CBarco::setTipoEmbarcacion(QString TipoEmbarcacion)
{
    m_TipoEmbarcacion = TipoEmbarcacion;
}

float CBarco::DimensionProa() const
{
    return m_DimensionProa;
}

void CBarco::setDimensionProa(float DimensionProa)
{
    m_DimensionProa = DimensionProa;
}

float CBarco::DimensionPopa() const
{
    return m_DimensionPopa;
}

void CBarco::setDimensionPopa(float DimensionPopa)
{
    m_DimensionPopa = DimensionPopa;
}

float CBarco::DimensionBabor() const
{
    return m_DimensionBabor;
}

void CBarco::setDimensionBabor(float DimensionBabor)
{
    m_DimensionBabor = DimensionBabor;
}

float CBarco::DimensionEstribor() const
{
    return m_DimensionEstribor;
}

void CBarco::setDimensionEstribor(float DimensionEstribor)
{
    m_DimensionEstribor = DimensionEstribor;
}

float CBarco::LargoEmbarcacion() const
{
    return m_LargoEmbarcacion;
}

void CBarco::setLargoEmbarcacion(float LargoEmbarcacion)
{
    m_LargoEmbarcacion = LargoEmbarcacion;
}

float CBarco::AnchoEmbarcacion() const
{
    return m_AnchoEmbarcacion;
}

void CBarco::setAnchoEmbarcacion(float AnchoEmbarcacion)
{
    m_AnchoEmbarcacion = AnchoEmbarcacion;
}

QString CBarco::Dia() const
{
    return m_Dia;
}

void CBarco::setDia(const QString &Dia)
{
    m_Dia = Dia;
}

QString CBarco::Mes() const
{
    return m_Mes;
}

void CBarco::setMes(const QString &Mes)
{
    m_Mes = Mes;
}

QString CBarco::Hora() const
{
    return m_Hora;
}

void CBarco::setHora(const QString &Hora)
{
    m_Hora = Hora;
}

QString CBarco::Min() const
{
    return m_Min;
}

void CBarco::setMin(const QString &Min)
{
    m_Min = Min;
}

QString CBarco::paisProcedencia() const
{
    return m_paisProcedencia;
}

void CBarco::setPaisProcedencia(const QString &paisProcedencia)
{
    m_paisProcedencia = paisProcedencia;
}

QString CBarco::destino() const
{
    return m_destino;
}

void CBarco::setDestino(const QString &destino)
{
    m_destino = destino;
}

quint8 CBarco::pertenencia() const
{
    return m_pertenencia;
}

void CBarco::setPertenencia(const quint8 &pertenencia)
{
    m_pertenencia = pertenencia;
}

QString CBarco::estadoNavegacion() const
{
    return m_estadoNavegacion;
}

void CBarco::setEstadoNavegacion(const QString &estadoNavegacion)
{
    m_estadoNavegacion = estadoNavegacion;
}

double CBarco::cursoSobreTierra() const
{
    return m_cursoSobreTierra;
}

void CBarco::setCursoSobreTierra(double cursoSobreTierra)
{
    m_cursoSobreTierra = cursoSobreTierra;
}

int CBarco::indicadorManiobra() const
{
    return m_indicadorManiobra;
}

void CBarco::setIndicadorManiobra(int indicadorManiobra)
{
    m_indicadorManiobra = indicadorManiobra;
}

char CBarco::estadoSeguimiento() const
{
    return m_estadoSeguimiento;
}

void CBarco::setEstadoSeguimiento(char estadoSeguimiento)
{
    m_estadoSeguimiento = estadoSeguimiento;
}

QString CBarco::tipoProvedorPosicion() const
{
    return m_tipoProvedorPosicion;
}

void CBarco::setTipoProvedorPosicion(const QString &tipoProvedorPosicion)
{
    m_tipoProvedorPosicion = tipoProvedorPosicion;
}

int CBarco::tipProvee() const
{
    return m_tipProvee;
}

void CBarco::setTipProvee(int tipProvee)
{
    m_tipProvee = tipProvee;
}

float CBarco::draught() const
{
    return m_draught;
}

void CBarco::setDraught(float draught)
{
    m_draught = draught;
}

QString CBarco::ETA() const
{
    return m_ETA;
}

void CBarco::setETA(const QString &ETA)
{
    m_ETA = ETA;
}

QPixmap CBarco::bandera() const
{
    return m_bandera;
}

void CBarco::setBandera(const QPixmap &bandera)
{
    m_bandera = bandera;
}

float CBarco::heading() const
{
    return m_heading;
}

void CBarco::setHeading(float heading)
{
    m_heading = heading;
}

qint64 CBarco::UTC() const
{
    return m_UTC;
}

void CBarco::setUTC(const qint64 &UTC)
{
    m_UTC = UTC;
}

QString CBarco::nextPortName() const
{
    return m_nextPortName;
}

void CBarco::setNextPortName(const QString &nextPortName)
{
    m_nextPortName = nextPortName;
}

QString CBarco::nextPortLOCODE() const
{
    return m_nextPortLOCODE;
}

void CBarco::setNextPortLOCODE(const QString &nextPortLOCODE)
{
    m_nextPortLOCODE = nextPortLOCODE;
}

QString CBarco::nextPortISO2() const
{
    return m_nextPortISO2;
}

void CBarco::setNextPortISO2(const QString &nextPortISO2)
{
    m_nextPortISO2 = nextPortISO2;
}

QString CBarco::nextPortCountry() const
{
    return m_nextPortCountry;
}

void CBarco::setNextPortCountry(const QString &nextPortCountry)
{
    m_nextPortCountry = nextPortCountry;
}

QString CBarco::lastPortCountry() const
{
    return m_lastPortCountry;
}

void CBarco::setLastPortCountry(const QString &lastPortCountry)
{
    m_lastPortCountry = lastPortCountry;
}

QString CBarco::lastPortName() const
{
    return m_lastPortName;
}

void CBarco::setLastPortName(const QString &lastPortName)
{
    m_lastPortName = lastPortName;
}

QString CBarco::lastPortLOCODE() const
{
    return m_lastPortLOCODE;
}

void CBarco::setLastPortLOCODE(const QString &lastPortLOCODE)
{
    m_lastPortLOCODE = lastPortLOCODE;
}

QString CBarco::lastPortDeparture() const
{
    return m_lastPortDeparture;
}

void CBarco::setLastPortDeparture(const QString &lastPortDeparture)
{
    m_lastPortDeparture = lastPortDeparture;
}

QString CBarco::lastPortArrival() const
{
    return m_lastPortArrival;
}

void CBarco::setLastPortArrival(const QString &lastPortArrival)
{
    m_lastPortArrival = lastPortArrival;
}

QString CBarco::lastEventTime() const
{
    return m_lastEventTime;
}

void CBarco::setLastEventTime(const QString &lastEventTime)
{
    m_lastEventTime = lastEventTime;
}

void CBarco::setLocationName(const QString &LocationName)
{
    m_locationName = LocationName;
}

double CBarco::razonGiro() const
{
    return m_razonGiro;
}

void CBarco::setRazonGiro(double razonGiro)
{
    m_razonGiro = razonGiro;
}

QString CBarco::precisionDataAIS() const
{
    return m_precisionDataAIS;
}

void CBarco::setPrecisionDataAIS(const QString &precisionDataAIS)
{
    m_precisionDataAIS = precisionDataAIS;
}

QString CBarco::nombreBuque() const
{
    return m_nombreBuque;
}

void CBarco::setNombreBuque(const QString &nombreBuque)
{
    m_nombreBuque = nombreBuque;
}

QString CBarco::locationName() const
{
    return m_locationName;
}
