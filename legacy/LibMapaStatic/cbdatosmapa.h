#ifndef CBDATOS_H
#define CBDATOS_H

#include <QObject>
#include "QtSql/QSqlDatabase"
#include "QtSql/QSqlDriver"
#include "QtSql/QSqlQuery"
//#include "../comunes/cutiles.h"
#include "cpunto.h"
#include <QDebug>
#include "QtSql/QSqlError"

class CBDatos : public QObject
{
    Q_OBJECT
public:
    CBDatos(QString path);

    int Tipo_BD = ImagenesSatelitales; //--------------> (0: Imagenes Satelitales, 1:Imagenes Open Street Map[OSM],  2: Configuracion)

    E_Dat_BD e_DatBD;
    QList<E_Dat_BD> ListDatBD;

    void Poner_Tipo_de_BD(int tipo); //-----------------------> Seleccionar tipo de BD (0: Imagenes Satelitales, 1:Imagenes Open Street Map[OSM], 2: Configuracion)
    QString Dice_Tipo_BD_Cargada();  //-----------------------> Devuelve el tipo de BD cargada;
    QList<E_Dat_BD> Cargar_BD_IMG(quint8 Zoom_Level, bool Tip_BD, int xMin, int xMax, int yMin, int yMax);
    QList<E_Dat_BD_OTROS> Cargar_BD_OTROS(const QString &capa, double xMin, double xMax, double yMin, double yMax);

    E_Dat_BD Tomar_Rango_Mapa_BD();

    //--> ultimo punto existente
    int NoUltimoPunto;
    //--> Ultimo poligono existente
    int NoUltimoPoligono;

    //--> acciones dentro de la BD
    bool abrirBD();
    void guardarConfigIni(const QStringList &listParametros);
    void guardarNuevoPuntoVehiculo(void *newPunto);
    void guardarNuevoPunto(void *newPunto);
    void guardarNuevoPoligono(const E_DataPoligono &newPoligono);
    void guardarNuevaRuta(const QList<EPuntoRuta> &newRuta);
    void insertarNuevaTrayectoria(const QString &nombrePunto, double Lat, double Lon, const QString &fecha);
    QList<void*> cargarPuntos(QString nombreTabla);
    QList<E_DataPoligono> cargarPoligonos();
    bool eliminarPunto(void* punto);
    bool eliminarPoligono(const E_DataPoligono &poligono);
    void guardarUltimoRangoMapa(int xMin, int xMax, int yMin, int yMax);

    QString getDirPath() const;
    void setDirPath(const QString &value);

signals:
    void signal_DevuelveListadoUltimosBuquesAlmacenados(QList<CBarco*> listBuques);
    void signal_ConsultaInvalida();
    void signal_NoExistenElementos();
    void signal_devuelveUltimoNoPunto(int NoPunto);
    void signal_devuelveUltimoNoPoligono(int NoPoligono);

private:
    QSqlDatabase bd_Puntos;
    QSqlDatabase bd_vehiculos;
    QSqlDatabase bd_Sat;
    QSqlDatabase bd_Osm;
    QSqlDatabase bd_Otros;
    QSqlDatabase bd_Vegetacion;
    QSqlDatabase bd_Vial;
    QSqlDatabase bd_ZonaBaja;
    QString DirPath;
};

#endif // CBDATOS_H
