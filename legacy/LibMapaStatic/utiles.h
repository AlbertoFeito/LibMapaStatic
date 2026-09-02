#ifndef QUTILES_H
#define QUTILES_H

#pragma once

#include "qcustomplot.h"

#include <QtGlobal>
#include <QColor>
#include <QtMath>
#include <QSettings>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QGeoCoordinate>
#include <QMap>
#include <QTime>
#include <QDate>
#include <QBuffer>
#include <QDir>
#include <QIcon>

//#include "defcomunes.h"

#define PI			3.1415926535897932384626433832795
#define RAD90		1.5707963267948966192313216916398
#define RAD270		4.7123889803846898576939650749193
#define RAD360		6.283185307179586476925286766559


#define TODAS -1

//---------------------------CONSTANTES MATEMATICAS --------------------------------//
#define RADIAN              M_PI/180.0
#define GRADOS              57.2957795130823208767981548141052
#define UN_DIA              86400000

#define DATA_MAX_SIZE 100  //-----------> Tamaño de la sentencia NMEA

//------- Tipos de Conexion ----------
#define ETHERNET 1
#define RS232    0
#define COORDENADA_LATITUD 0
#define COORDENADA_LONGITUD 1

#ifndef BZERO
    #define BZERO(buf)	memset(&buf, 0, sizeof(buf))
#endif


#pragma pack(push, 1)


//------------> Acciones Mapa
#define aNADA -1
#define aInsertarPunto 0
#define aMedirDistancia 1
#define aPintarPoligono 2
#define aSeleccionarUbicacionEnMapa 3
#define aTrazaRuta      4
#define aZoomArea      5

//------------> Estado Acciones Mapa
#define eNADA         -1
#define eInsertandoPunto    0
#define eMidiendo         1
#define ePintandoPoligono 2
#define eTrazandoRuta     3
#define eZoomArea      4

//------------> Tipo de Puntos Geo

#define PuntInteres  0
#define POV  1
#define ILC  2
#define PM  3
#define RADAR  4
#define AIS  5


struct E_PuntoItem
{
    QCPItemEllipse* ellipse = nullptr;
    QCPItemEllipse* ellipse2 = nullptr;
    QCPItemText* text = nullptr;
};

struct EPuntoRuta
{
    int prioridad;
    QString descripcion;
    QGeoCoordinate pos;
    int radio;
};

struct BITFIELD16
{
    uchar Bit0 : 1;
    uchar Bit1 : 1;
    uchar Bit2 : 1;
    uchar Bit3 : 1;
    uchar Bit4 : 1;
    uchar Bit5 : 1;
    uchar Bit6 : 1;
    uchar Bit7 : 1;
    uchar Bit8 : 1;
    uchar Bit9 : 1;
    uchar Bit10 : 1;
    uchar Bit11 : 1;
    uchar Bit12 : 1;
    uchar Bit13 : 1;
    uchar Bit14 : 1;
    uchar Bit15 : 1;
};

struct E_EstTierra
{
    QString MMSI;
    double XposMap;
    double YposMap;
    void PonerDatos(QString MMSI_Code, double XposicMap, double YposicMap);
    double CogerPosX();
    double CogerPosY();
};

struct E_Icons
{
    QString nombreIcon;
    QIcon icon;
};

//------------> Estructuras de Puntos geograficos
struct E_PuntoGeo
{
    int tipo; // POV, ILC, PM,
    QString nombrePunto;
    QString descripcion;
    QPixmap simbolo;
    QGeoCoordinate geoPos;
};

struct E_Tipos_Punto
{
    public:
        void setNewTipo(QString descrip);
        void modificarDescrip(QString descripActual, QString newDescrip);
        void eliminarTipo(QString descripActual);
        int getTipoId(QString descrip);

        QString getTipoDescrip(int id);

    private:
        QStringList lTipos;
};

struct E_DataFuentePendiente
{
    QByteArray data;
    int noFuente;
    quint64 UTC; // hora en UTC en que llego el dato a la fuente.
};

struct Buffer
{
   char Data[DATA_MAX_SIZE];
   quint8 Size;
   int TiempoMsec;
   void* Fuente;
};

struct E_Trayectoria
{
    QGeoCoordinate pos;
    float rumbo;
    float velocidad;
    float altura;
    int pertenencia;
    quint64 UTC;
};

enum PolarMode
{
    PM_0_90 = 0,
    PM_0_270 = 1,
    PM_180_90 = 2,
    PM_180_270 = 3,
    PM_90_0 = 4,
    PM_90_180 = 5,
    PM_270_0 = 6,
    PM_270_180 = 7
};

struct GMS
{
    short G;
    uchar M;
    uchar S;
    GMS()
    {
        G = 0; M = 0; S = 0;
    }
    GMS(short g, uchar m, uchar s)
    {
        G = g; M = m; S = s;
    }
    bool operator ==(GMS gms)
    {
        return (gms.G == G && gms.M == M && gms.S == S);
    }
    bool operator !=(GMS gms)
    {
        return (gms.G != G || gms.M != M || gms.S != S);
    }
};

struct GM_Float
{
    short G;
    float M;
    GM_Float()
    {
        G = 0; M = 0;
    }
    GM_Float(short g, float m)
    {
        G = g; M = m;
    }
    bool operator ==(GM_Float gm)
    {
        return (gm.G == G && gm.M == M);
    }
    bool operator !=(GM_Float gm)
    {
        return (gm.G != G || gm.M != M);
    }
};

struct GMS_Float
{
    short G;
    short M;
    float S;
    GMS_Float()
    {
        G = 0; M = 0; S = 0;
    }
    GMS_Float(short g, short m, float s)
    {
        G = g; M = m; S = s;
    }
    bool operator ==(GMS_Float gms)
    {
        return (gms.G == G && gms.M == M && gms.S == S);
    }
    bool operator !=(GMS_Float gms)
    {
        return (gms.G != G || gms.M != M || gms.S != S);
    }
};

struct DATA
{
    QByteArray buffer;
    int noFuente;
};

struct E_Estados
{
    bool b_E_Dist_Marc;
};

struct E_Dist_Marc
{
    double Lat_1;
    double Long_1;
    double Lat_2;
    double Long_2;
    double Distancia;
    double Marcacion;
};

struct E_Datos_Pantalla
{
    bool b_Est_Dist_Marc;
    quint8 bTipoMapa; //------------------------------> Tipo de Imagen mostrada (0 -- VECT, 1 -- OSM, 2 -- SAT BD, 3 -- OSM BD)
    bool b_Menu_Click_Derecho;
};

//----------------------------> Estructura de datos de la Fuenta Externa
struct E_DataFuenteExt
{
    QString nombre;
    QGeoCoordinate posGeo;
    double velocidad;
    double rumbo;
    QPixmap simbolo;
    QString fecha;
};

//----------------------------> Estructura de datos de los poligonos
struct E_DataPoligono
{
    int idPoligono;
    QString nombrePolig;
    QPen pen;
    int relleno;
    int noVertices;
    QList<QGeoCoordinate> LPuntosGeoPolig;
    QString fechaCreacion;
};

struct E_Dat_BD //------------> Estructura donde se guardan los datos cargados de la BD
{
    quint8 zoom_level;
    int x;
    int y;
    QString ID;
    QByteArray foto;
    int xMax;
    int xMin;
    int yMin;
    int yMax;
};

struct E_Dat_BD_OTROS //------------> Estructura donde se guardan los datos cargados de la BD
{
    double lon;
    double lat;
    QString alt;
};

struct E_Tiles
{
    QPixmap foto;
    int x;
    int y;
    int zoom;
};

struct E_New_Obj
{
    QString tipo; //-----------------> 0: Objetivo , 1: ERL
    int Rumbo;
    double Velocidad;
    QString NombreERL;
    QString Tipo_ERL;
    int AltAnt;
    double Lat;
    double Long;
};

struct E_Target
{
    int noFuentePertenece;
    int noTargetEnFuente;
    QGeoCoordinate pos;
    char tipFuente; //-----------------> 'A': AIS , 'R': RADAR, 'M': Manual
    int Rumbo;
    double Velocidad;
    double Distancia;
    double Marcacion;

    //datos propio del AIS (msg 1,2,3)
    QString codigoMMSI;
    int precisionData;
    QString estadoNav;
    int razonGiro;
    int indManiobra;
};

struct EEscala
{
    int Unidad;
    int Escala;
};

struct E_DataFuentePendienteDecode
{
    QByteArray data;
    int noFuente;
    quint64 UTC; // hora en UTC en que llego el dato a la fuente.
};

//struct Buffer
//{
//   char Data[DATA_MAX_SIZE];
//   quint8 Size;
//   int TiempoMsec;
//   void* Fuente;
//};

struct E_DataPosGeo
{
    int noFuente;
    char tipoFuente; //-----------------> 'A': AIS , 'R': RADAR, 'M': Manual
    QGeoCoordinate pos;
    QString nombre;
};

enum Capas {
    ImagenesSatelitales = 0,
    ImagenesOSM = 1,
    CubaVectorial = 2
};

//------------> Estructura de Iconos

#pragma pack(pop)

//#define math_to_radian M_PI / 180
//#define math_to_degree 180 / M_PI

//namespace math_ext
//{
//    static double to_radians(double value)
//    {
//        return value * math_to_radian;
//    }

//    static double to_degrees(double value)
//    {
//        return value * math_to_degree;
//    }

//}

struct EStatusBar
{
    //-Barra de Progreso ubicada en el StatusBar para Mostrar
    //que se esta trabajando en algo, en segundo Plano.
    //--Estructura que contiene los Objetos del StatusBar
    QLineEdit *le_Latitud,*le_Longitud,*le_Altitud,*le_Distancia,*le_Marcacion,*le_NivelZoom, *le_Escala;
    QLabel *lb_Latitud,*lb_Longitud,*lb_Altitud,*lb_Distancia,*lb_Marcacion,*lb_Mensaje, *lb_NivelZoom, *lb_Escala, *lb_Working;
    QProgressBar *pb_Progreso;
    QTimer* t_Mensaje;
    int contValorProgress;
};

class QUtiles
{
public:

    // Funciones útiles
    static uint HexToDec(QString Hex);
    static void GrayToBin12(ushort *pnGray);
    static double GmsToGrados(GMS gms);
    static GMS GradosToGms(double Valor);
    static double GmsFloatToGrados(GMS_Float gms);
    static GMS_Float GradosToGmsFloat(double Valor);
    static double GMFloatToDecimales(GM_Float gm);
    static void SexagesFromQGeoCoordinate_ToGms_Float(QString Valor, GMS_Float &lat, GMS_Float &lon);

    static double getDistancia2D(double x1, double y1, double x2, double y2);
    static double getDistancia3D(double x1, double y1, double z1, double x2, double y2, double z2);
    static bool EnSector(double Ang, double SectorInicio, double SectorFin);
    static double GetAnguloMedio(double SectorInicio, double SectorFin);
    static bool IsPolarModeClockWiseOnRepre(PolarMode pm);
    static bool IsEqualMem(void* Mem1, void* Mem2, uint nSizeInBytes);

    // Funcion para tomar Hora del sistema
    static QString Slot_Tomar_Hora_Sistema();
    static QString Slot_Tomar_Fecha_Sistema();

    // Funciones para escritura de valores en un fichero de configuración
    static void WriteConfigString(QString strFileName, QString strSection, QString strEntry, QString strValue);
    static void WriteConfigInt(QString strFileName, QString strSection, QString strEntry, int nValue);
    static void WriteConfigUInt(QString strFileName, QString strSection, QString strEntry, uint nValue);
    static void WriteConfigHex(QString strFileName, QString strSection, QString strEntry, uint nValue);
    static void WriteConfigDouble(QString strFileName, QString strSection, QString strEntry, double dValue);
    static void WriteConfigBool(QString strFileName, QString strSection, QString strEntry, bool bValue);
    static void WriteConfigRGB(QString strFileName, QString strSection, QString strEntry, QRgb rgbValue);
    static void WriteConfigGMS(QString strFileName, QString strSection, QString strEntry, double dValue);
    static void WriteConfigGMS(QString strFileName, QString strSection, QString strEntry, GMS gmsValue);
    static void WriteConfigData(QString strFileName, QString strSection, QString strEntry, void* pData, uint nDataSize);

    // Funciones para lectura de valores de un fichero de configuración
    static QString GetConfigString(QString strFileName, QString strSection, QString strEntry, QString strDefault = "", bool bUpdateFile = true);
    static int GetConfigInt(QString strFileName, QString strSection, QString strEntry, int nDefault = 0, bool bUpdateFile = true);
    static uint GetConfigUInt(QString strFileName, QString strSection, QString strEntry, uint nDefault = 0, bool bUpdateFile = true);
    static uint GetConfigHex(QString strFileName, QString strSection, QString strEntry, uint nDefault = 0, bool bUpdateFile = true);
    static double GetConfigDouble(QString strFileName, QString strSection, QString strEntry, double dDefault = 0, bool bUpdateFile = true);
    static bool GetConfigBool(QString strFileName, QString strSection, QString strEntry, bool bDefault = true, bool bUpdateFile = true);
    static QRgb GetConfigRGB(QString strFileName, QString strSection, QString strEntry, QRgb rgbDefault = 0, bool bUpdateFile = true);
    static GMS GetConfigGMS(QString strFileName, QString strSection, QString strEntry, GMS Default = GMS(0,0,0), bool bUpdateFile = true);
    static double GetConfigGMS(QString strFileName, QString strSection, QString strEntry, double dDefault = 0, bool bUpdateFile = true);
    static void GetConfigData(QString strFileName, QString strSection, QString strEntry, void* pData, uint nDataSize, bool bUpdateFile = true);

    // Funciones "getAngulo"
    static double getAngulo_0_90(double Xreal, double Yreal);
    static double getAngulo_0_270(double Xreal, double Yreal);
    static double getAngulo_180_90(double Xreal, double Yreal);
    static double getAngulo_180_270(double Xreal, double Yreal);
    static double getAngulo_90_0(double Xreal, double Yreal);
    static double getAngulo_90_180(double Xreal, double Yreal);
    static double getAngulo_270_0(double Xreal, double Yreal);
    static double getAngulo_270_180(double Xreal, double Yreal);

    // Funciones para conversión de coordenadas (Pixel, Cartesianas, Polares)
    static void PixelToUnidad(double UnidadMax, int PixelMax, int Pixel, double* Unidad);
    static void UnidadToPixel(double UnidadMax, int PixelMax, double Unidad, int* Pixel);
    static void CalcularPixelCentro(double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int GraphicWidth, int GraphicHeight, int* XPixelCentro, int* YPixelCentro);
    static void PixelToReal(int Xpixel, int Ypixel, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, double* Xreal, double* Yreal);
    static void RealToPixel(double Xreal, double Yreal, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, int* Xpixel, int* Ypixel);
    static void RealToPolar(double Xreal, double Yreal, PolarMode pmPolarMode, double* Angulo, double* Distancia);
    static void PolarToReal(double Angulo, double Distancia, PolarMode pmPolarMode, double* Xreal, double* Yreal);
    static void PixelToPolar(int Xpixel, int Ypixel, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, PolarMode pmPolarMode, double* Angulo, double* Distancia);
    static void PolarToPixel(double Angulo, double Distancia, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, PolarMode pmPolarMode, int* Xpixel, int* Ypixel);

    // Funciones para manipulación de textos
    static QString CodificaString(QString s);
    static QString DecodificaString(QString s);
    static QByteArray CodificaString(QByteArray s);
    static QByteArray DecodificaString(QByteArray s);
    static int JoinedStringCount(QString strJoinedString);
    static QStringList SplitJoinedString(QString strJoinedString);
    static bool IsStringInJoinedString(QString strString, QString strJoinedString);

    // Funciones para conversiones de QPixmap
    static QByteArray ConvierteQPixmap_A_QByteArray(QPixmap pix, QString formatoImagen = "PNG");
    static QPixmap ConvierteQByteArray_A_QPixmap(QByteArray pixmapData);
    static QPixmap rotarImagen(QPixmap img, int angulo);

    // Funciones que lista los archivos dentro de un directorio
    static QStringList listarFicherosDirectorio(QString directorio);
    static QPixmap devuelveIcono(int tipoPunto);
    static QString devuelveDirIcono(int tipoPunto);

    //matematica
    static qint32 complemento2(quint32 number, quint8 cantBits);
};

#endif // QUTILES_H
