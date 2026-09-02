#include "ccalculos.h"

CCalculos::CCalculos()
= default;

QString CCalculos::Convertir_a_Sexagesimal(float Lat)
{
    G = truncf(Lat);
    M = truncf((Lat - G)*60);
    if(M==60){ G+=1; M=0;}
    S = roundf((Lat-(G+M/60))*3600);
    if(S==60){M+=1; S=0;}

        return QString::number(G)+"°"+QString::number(M)+"'"+QString::number(S)+'"';
}

double CCalculos::Calcular_Distancia_2Puntos(QPointF Punt1, QPointF Punt2)
{

    GeoIni.setLatitude(Punt1.x());
    GeoIni.setLongitude(Punt1.y());
    GeoFin.setLatitude(Punt2.x());
    GeoFin.setLongitude(Punt2.y());
    double Distan = GeoIni.distanceTo(GeoFin);
    return Distan;

}

double CCalculos::Calcular_Marcacion(QPointF Punt1, QPointF Punt2)
{
    GeoIni.setLatitude(Punt1.x());
    GeoIni.setLongitude(Punt1.y());
    GeoFin.setLatitude(Punt2.x());
    GeoFin.setLongitude(Punt2.y());
    double Marc = GeoIni.azimuthTo(GeoFin);
    return Marc;
}

double CCalculos::Convertir_Tiles_a_Lat(int y,quint8 zoom)
{
    double n = M_PI - 2.0 * M_PI * y / pow(2.0, zoom);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

double CCalculos::Convertir_Tiles_a_Long(int x, quint8 zoom)
{
    return x / pow(2.0, zoom) * 360.0 - 180;
}

int CCalculos::Convertir_Long_a_TilesX(double lon, int z)
{
    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
}

int CCalculos::Convertir_Lat_a_TilesY(double lat, int z)
{
    return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z)));
}

int CCalculos::Devuelve_Zoom_Verdadero_Cuba_Satelital(int zoomlocal)
{
    switch (zoomlocal)
    {
        case 1: return 16;break;
        case 2: return 15;break;
        case 3: return 14;break;
        case 4: return 13;break;
        case 5: return 12;break;
        case 6: return 11;break;
        case 7: return 10;break;
        case 8: return 9;break;
        case 9: return 8;break;
        case 10: return 7;break;
        case 11: return 6;break;
        case 12: return 5;break;
        case 13: return 4;break;
        case 14: return 3;break;
        case 15: return 2;break;
        case 16: return 1;break;
        case 17: return 0;break;
        case 18: return -1;break;
    }

    return - 2;
}
