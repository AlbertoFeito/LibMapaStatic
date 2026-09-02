#ifndef CCALCULOS_H
#define CCALCULOS_H
#define Radio_Tierra 6371  //km
//
#include <QObject>
#include <utiles.h>
#include <QPointF>
#include <definiciones.h>
#include <QtPositioning/QGeoCoordinate>

class CCalculos
{
public:
    CCalculos();

    QList<E_Dat_BD> List_IMG_BD;

    QGeoCoordinate GeoIni;
    QGeoCoordinate GeoFin;

//    double Distancia(double,double);
//    double Latitud_2(double dy, double Latitud_1);
//    double Longitud_2(double dx, double Longitud_1, double Latitud_2);
//    float Convertir_a_Decimales_2(float LatG, float LatM, float LatS); // ----------------------------------------------> Convierte de Grados Sexagesimales expresados en GMS a Decimales
//    double Convertir_a_Decimales(double L_Dec); // ---------------------------------------------------------------------> Convierte de Grados Sexagesimales a Decimales
    QString Convertir_a_Sexagesimal(float Lat); //----------------------------------------------------------------------> Convierte de Grados Decimales a Sexagesimales
//    QString Convertir_a_Sexagesimal_2(float Lat); //--------------------------------------------------------------------> Randy

    double Calcular_Distancia_2Puntos(QPointF Punt1, QPointF Punt2); //-------------------------------------------------> Calcula Distancia entre dos puntos por medio de ley de los cosenos
    double Calcular_Marcacion(QPointF Punt1, QPointF Punt2);//---//-----------------------------------------------------> Calcula Marcacion entre dos puntos
    double Convertir_Tiles_a_Lat(int y, quint8 zoom);
    double Convertir_Tiles_a_Long(int x, quint8 zoom);
    int Convertir_Long_a_TilesX(double lon, int z);
    int Convertir_Lat_a_TilesY(double lat, int z);    
    int Devuelve_Zoom_Verdadero_Cuba_Satelital(int zoomlocal);

 private:
    int G{},M{},S{};
};

#endif // CCALCULOS_H
