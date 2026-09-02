#ifndef CUTILES_H
#define CUTILES_H

#include <QtGlobal>
#include <QColor>
#include <QtMath>
#include <QTextStream>
#include <QSettings>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QGeoCoordinate>
#include <QMap>
#include <QTime>
#include <QDate>
#include <QDateTime>
#include <QBuffer>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QStringList>
//#include <utiles.h>

#define PI			3.1415926535897932384626433832795
#define RAD90		1.5707963267948966192313216916398
#define RAD270		4.7123889803846898576939650749193
#define RAD360		6.283185307179586476925286766559

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

#define ImagenesOSM 0
#define ImagenesSatelitales 1
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

//------- cant Trayectorias  ----------
#define TODAS -1

enum AIS_Estacion_Tierra {Idefinido,GPS,GLONASS,Combinacion_de_GPSGLONASS,Loran_C,Chayka,Sistema_de_Navegacion_Integrado,Medido,Galileo};

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

struct EDataBuffer
{
    QByteArray Buffer;
    void* fuente;
    int timeRx;
};

struct Buffer
{
   char Data[DATA_MAX_SIZE];
   quint8 Size;
   int TiempoMsec;
   void* Fuente;
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

struct E_Trayectoria
{
    QGeoCoordinate pos;
    float rumbo;
    float velocidad;
    float altura;
    int pertenencia;
    quint64 UTC;
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

struct EPuntoRuta
{
    int prioridad;
    QString descripcion;
    QGeoCoordinate pos;
};

class cUtiles
{
public:

    cUtiles();
    // Funciones útiles
       static uint HexToDec(QString Hex);
//       static void GrayToBin12(ushort *pnGray);
       static double GmsToGrados(GMS gms);
       static GMS GradosToGms(double Valor);
       static double GmsFloatToGrados(GMS_Float gms);
       static GMS_Float GradosToGmsFloat(double Valor);
       static double GMFloatToDecimales(GM_Float gm);
       static void SexagesFromQGeoCoordinate_ToGms_Float(QString Valor, GMS_Float &lat, GMS_Float &lon);       
       static QString GMS_FLOAT_A_Sexagesimal(GMS_Float Lat, bool COORDENADA);

       static double getDistancia2D(double x1, double y1, double x2, double y2);
       static double getDistancia3D(double x1, double y1, double z1, double x2, double y2, double z2);
       static bool EnSector(double Ang, double SectorInicio, double SectorFin);
       static double GetAnguloMedio(double SectorInicio, double SectorFin);
       static bool IsPolarModeClockWiseOnRepre(PolarMode pm);
       static bool IsEqualMem(void* Mem1, void* Mem2, uint nSizeInBytes);

       // Funcion para tomar Hora del sistema
       static QString Slot_Tomar_Hora_Sistema();
       static QString Slot_Tomar_Fecha_Sistema();
       static quint64 Slot_Tomar_UTC();

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
       static QPixmap rotarImagen(QPixmap img, float angulo);

       // Funciones que lista los archivos dentro de un directorio
       static QStringList listarFicherosDirectorio(QString directorio);
       static QPixmap devuelveIcono(int tipoPunto);
       static QString devuelveDirIcono(int tipoPunto);

       //matematica
       static qint32 complemento2(quint32 number, quint8 cantBits);

       //Determinar Bandera segun MMSI
       static void cargarFicheroCodigosMMSIsegunOMI(QString dirFichMMSI, QStringList &listCodMMSI);
       static QPixmap determinaBandera(QString MMSI, QString dirCarpetaBandera, QStringList listCodMMSI, QString &nombCountry);

       QStringList lMMSIPertenencia;
       QStringList getLMMSIPertenencia() const;
};

// De AIS
///===================================== Estructuras y Definiciones ===============================

QString Convertir_Caracteres_a_CadenaBinaria_6bit(const QByteArray& Cadena);
QString Determinar_Estado_de_Navegacion(quint8 EstadoN);
QString Slot_Formar_Texto_con_ASCII6bits(int PosIni_en_CadenaBinaria6bits, int PosFin_en_CadenaBinaria6bits, const QString& CadenaBits, QString AIS_Caracter_6bit[]);
QString Slot_Determinar_Tipo_Embarcacion(int Tipo_Embarcacion);
qint32 complemento2(quint32 number, quint8 cantBits);
QString Slot_Tomar_Fecha_Sistema();
QString Slot_Tomar_Hora_Sistema();
void Determinar_Dif_Horas(int diaIni, int mesIni, int anoIni, int diaFin, int mesFin, int anoFin, int horaIni, int minIni, int segIni, int horaFin, int minFin, int segFin, int *DifHoras, int *DifMin, int *DifSeg);

//struct EZoom             //--------------------------------------------------------------------------------------------------------------------- Estructura para controlar las operaciones de Zoom
//{
//    double ZoomXmin,ZoomYmin,ZoomXmax,ZoomYmax;   //------------------------> Valores que tomará las dimensiones del mapa cuando se hace Zoom Estático
//    bool ZoomArea, ZoomAreaPress;    //----------------------------------------------------> Bandera que controla cuando se desea hacer el Zoom Area
//    int Zoom;
//    int X1,Y1,X2,Y2; //--------------------------------------------------> Valores que tomarán las dimensiones del mapa cuando se hace Zoom Area
//}eZoom;

//struct EDataBuffer
//{
//    QByteArray Buffer;
//    void* fuente;
//    int timeRx;
//};

struct E_Faros
{
    QString Codigo;
    QString NombreFaro;
    QString Tipo;
    QString Descripcion;
    double LatitM;
    QString LatitSexag;
    double LongitM;
    QString LongitSexag;
    QString Caracteristicas;
    QString Altura;
    QString Distancia;
    QString Estructura;
    QString Color;
    QString AlturaEstruc;
    QString Estado;
};

struct E_Enfilaciones
{
    QString Codigo;
    QString NombreEnfilacion;
    QString Tipo;
    QString Descripcion;
    double LatitM;
    QString LatitSexag;
    double LongitM;
    QString LongitSexag;
    QString Caracteristicas;
    QString Altura;
    QString Distancia;
    QString Estructura;
    QString Color;
    QString Orientacion;
    QChar Hemisferio;
};

struct E_Balizas
{
    QString Codigo;
    QString NombreBaliza;
    QString Tipo;
    QString Descripcion;
    double LatitM;
    QString LatitSexag;
    double LongitM;
    QString LongitSexag;
    QString Caracteristicas;
    QString Altura;
    QString Distancia;
    QString Estructura;
    QString Color;
    QString Estado;
};

struct E_Boyas
{
    QString Codigo;
    QString NombreBoya;
    QString Tipo;
    QString Descripcion;
    double LatitM;
    QString LatitSexag;
    double LongitM;
    QString LongitSexag;
    QString Caracteristicas;
    QString Altura;
    QString Distancia;
    QString Estructura;
    QString Color;
    QString Estado;
};

struct EProfund
{
    QVector<double> VectorX;
    QVector<double> VectorY;
    QVector<double> VectorZ;
};

// ---------------------------------------------------> Estructura del Campo Especificado(CESP)--FSPEC
struct CESP8
{
    quint8 Continua : 1;
    quint8 Dato7 : 1;
    quint8 Dato6 : 1;
    quint8 Dato5 : 1;
    quint8 Dato4 : 1;
    quint8 Dato3 : 1;
    quint8 Dato2 : 1;
    quint8 Dato1 : 1;
};

struct CESP_OBJV
{
    quint8 Continua1 : 1;
    quint8 Destino : 1;
    quint8 Nombre : 1;
    quint8 Velocidad : 1;
    quint8 Rumbo : 1;
    quint8 PosMetros : 1;
    quint8 NumObjvSistema : 1;
    quint8 CodMMSI : 1;
    quint8 Continua2 : 1;
    quint8 Dato14 : 1;
    quint8 Hora : 1;
    quint8 Pertenencia : 1;
    quint8 Composicion : 1;
    quint8 Estado_Seguimiento : 1;
    quint8 Tipo_Embarcacion : 1;
    quint8 Dimensiones : 1;
};

struct CESP_OBJV_MUIS
{
    quint8 Continua1 : 1;
    quint8 Destino : 1;
    quint8 Nombre : 1;
    quint8 Velocidad : 1;
    quint8 Rumbo : 1;
    quint8 PosMetros : 1;
    quint8 NumObjvSistema : 1;
    quint8 CodMMSI : 1;
    quint8 Continua2 : 1;
    quint8 Dato14 : 1;
    quint8 Hora : 1;
    quint8 Pertenencia : 1;
    quint8 Composicion : 1;
    quint8 Estado_Seguimiento : 1;
    quint8 Tipo_Embarcacion : 1;
    quint8 Dimensiones : 1;
};

///-------------------------------------------------------- Paquetes de Datos -----------------------------------------------
// ---------------------------------------------------> Paquete de Autentificacion. <-----------------------
struct EAutenticar //----> Tipo de Informacion (7)
{
//    quint8 Comienzo_del_Paquete = 0xA5 ;
//    quint8 ByteControl1 = 02 ;
//    quint8 ByteControl2 = 0x5A ;
//    quint8 CantBytesPaquete /*= 17*/;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion ;
    quint8 FSP; //---------------------------> FillSpec
    char Autenticacion[8];
    int Hora_local;
};

struct EAutenticarMUIS //----> Tipo de Informacion (7)
{
    quint8 Comienzo_del_Paquete /*= 0xA5*/ ;
    quint8 ByteControl1 /*= 02*/ ;
    quint8 ByteControl2 /*= 0x5A*/ ;
    quint16 CantBytesPaquete /*= 17*/;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion ;
    quint8 FSP; //---------------------------> FillSpec
    char Autenticacion[8];
    int Hora_local;
};

//struct GMS //---------------------> Estructura GRADO_MIN_SEG
//{
//    qint16 G;
//    quint8 M;
//    quint8 S;

//    GMS()
//    {
//        G = 0;
//        M = 0;
//        S = 0;
//    }

//    GMS(double valor)
//    {
//        G=(qint16)valor;
//        double aux=qAbs<double>(valor-G)*60;
//        M=(quint8)qAbs(aux);
//        double aux2=(aux-M)*60;
//        S=(quint8)qAbs<double>(aux2);
//    }
//};


//----------------------------------------------------> Paquete de Ubicacion <-------------------------------
struct EUbicacion //----> Tipo de Informacion (3)
{
//    quint8 Comienzo_del_Paquete = 0xA5 ;
//    quint8 ByteControl1 = 02 ;
//    quint8 ByteControl2 = 0x5A ;
//    quint8 CantBytesPaquete = 25;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion;
    quint8 FSP; //---------------------------> FillSpec
    GMS LatGEO;
    GMS LongGEO;
    qint16 Altura;
    quint8 CGS;
    quint8 CES;
    int Hora_Tx;
};
struct EUbicacionMUIS //----> Tipo de Informacion (3)
{
    quint8 Comienzo_del_Paquete;
    quint8 ByteControl1;
    quint8 ByteControl2;
    quint16 CantBytesPaquete;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion;
    quint8 FSP; //---------------------------> FillSpec
    GMS LatGEO;
    GMS LongGEO;
    qint16 Altura;
    quint8 CGS;
    quint8 CES;
    int Hora_Tx;
};

struct ESensorMUIS
{
    quint8 Comienzo_del_Paquete;
    quint8 ByteControl1;
    quint8 ByteControl2;
    quint16 CantBytesPaquete;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion;
    quint8 FSP; //---------------------------> FillSpec
    quint8 EstadoSensor;
    quint16 PeriodoAntena;
    quint16 RhoIni;
    quint16 RhoFin;
    quint16 CitaIni;
    quint16 CitaFin;
    int HoraMUIS;
};

struct EObjetivoMUIS
{
    quint8 Comienzo_del_Paquete;
    quint8 ByteControl1;
    quint8 ByteControl2;
    quint16 CantBytesPaquete;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion;
    quint16 FSP; //---------------------------> FillSpec
    quint16 NumObjetiv;
    quint32 MetrosX : 24;
    quint32 MetrosY : 24;
    int HoraTx;
    quint8 Composicion; //--------------------> Sensores que captaron al objetivo
    quint16 EstTrayectoria;
    quint8 Pertenencia;
    quint16 Velocidad;
    quint16 Altura;
    quint16 Interferencia;
    quint32 SSR : 24;
    quint8 EstatusRespond;
};

//----------------------------------------------------> Paquete de Objetivos <-------------------------------
struct EObjetivo //----> Tipo de Informacion (20)
{
//    quint8 Comienzo_del_Paquete = 0xA5 ;
//    quint8 ByteControl1 = 02 ;
//    quint8 ByteControl2 = 0x5A ;
//    quint8 CantBytesPaquete = 17;
    quint8 Codigo_del_Nivel;
    quint8 Tipo_de_Informacion;
    quint16 FSP; //---------------------------> FillSpec
    quint8 MMSI[9]; // Dato 1
    quint8 NoObjetivoSistema; // Dato 2
    quint8 Xmetros[3]; // Dato 3
    quint8 Ymetros[3];
    quint16 Rumbo; // Dato 4
    quint16 Velocidad; // Dato 5
    quint8 Nombre[15]; // Dato 6
    quint8 Destino[15]; // Dato 7
    quint16 Largo; // Dato 8
    quint8 Ancho;
    quint8 Tipo_Embarcacion; // Dato 9 //---------------------------------------> El valor tiene su equivalente en la funcion: "Slot_Determinar_Tipo_Embarcacion(int Tipo_Embarcacion)"
    quint8 Estado_Seguimiento; // Dato 10
    quint8 Composicion; // Dato 11 //--------------------------------------------> El valor indica la cantidad de Fuentes que ven este Objetivo
    quint8 Pertenencia; // Dato 12
    int Hora; // Dato 13
};

struct RATTM
{
//    char *Encabezado = "$RATTM";
//    char Relleno1 = ',';
    int NumObjetivo;
//    char Relleno2 = ',';
    double Distancia;
//    char Relleno3 = ',';
    double Marcacion;
//    char Relleno4 = ',';
//    char Relleno5 = ',';
    double Velocidad;
//    char Relleno6 = ',';
    int Rumbo;
//    char Relleno7 = ',';
//    char Relleno8 = ',';
//    char Relleno9 = ',';
//    char Relleno10 = ',';
//    char Relleno11 = ',';
//    char Relleno12 = ',';
//    char Relleno13 = ',';
//    char Relleno14 = ',';
//    char Relleno15 = ',';
//    char Relleno16 = ',';
//    char Relleno17 = ',';
//    char Relleno18 = ',';
//    char Relleno19 = ',';
//    char Relleno20 = ',';
    quint8 ChkSum;
};

struct RATLL
{
//    char *Encabezado = {"$RATLL"};
//    char Relleno1 = ',';
    int NumObjetivo;
//    char Relleno2 = ',';
    double Latitud;
//    char Relleno3 = ',';
    double Unidad_Latitud;
//    char Relleno4 = ',';
    double Longitud;
//    char Relleno5 = ',';
    double Unidad_Longitud;
//    char Relleno6 = ',';
//    char Relleno7 = ',';
//    char Relleno8 = ',';
//    char Relleno9 = ',';
//    char Relleno10 = ',';
//    char Relleno11 = ',';
//    char Relleno12 = ',';
//    char Relleno13 = ',';
    quint8 ChkSum;
};

struct TipMensajes
{
    bool Tx_Habilitada;
};

#pragma pack(pop)
//---------------------------------------------- Caracteres ASCII de 6 bits-----------------------------
//char AIS_Caracter_6bit[64]; /*= "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\ ]\^\_ !"\#";*/
//enum AIS_Caracter_6bit {A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,("["),(\_)};
//AIS_Caracter_6bit[0]='@';    AIS_Caracter_6bit[1]='A';    AIS_Caracter_6bit[2]='B';   AIS_Caracter_6bit[3]='C';   AIS_Caracter_6bit[4]='D';    AIS_Caracter_6bit[5]='E';    AIS_Caracter_6bit[6]='F';    AIS_Caracter_6bit[7]='G';
//AIS_Caracter_6bit[8]='H';    AIS_Caracter_6bit[9]='I';    AIS_Caracter_6bit[10]='J';  AIS_Caracter_6bit[11]='K';  AIS_Caracter_6bit[12]='L';   AIS_Caracter_6bit[13]='M';   AIS_Caracter_6bit[14]='N';   AIS_Caracter_6bit[15]='O';
//AIS_Caracter_6bit[16]='P';   AIS_Caracter_6bit[17]='Q';   AIS_Caracter_6bit[18]='R';  AIS_Caracter_6bit[19]='S';  AIS_Caracter_6bit[20]='T';   AIS_Caracter_6bit[21]='U';   AIS_Caracter_6bit[22]='V';   AIS_Caracter_6bit[23]='W';
//AIS_Caracter_6bit[24]='X';   AIS_Caracter_6bit[25]='Y';   AIS_Caracter_6bit[26]='Z';  AIS_Caracter_6bit[27]='[';  AIS_Caracter_6bit[28]='\ ';  AIS_Caracter_6bit[29]=']';   AIS_Caracter_6bit[30]='\^';  AIS_Caracter_6bit[31]='\_';
//AIS_Caracter_6bit[32]=' ';   AIS_Caracter_6bit[33]='!';   AIS_Caracter_6bit[34]='"';  AIS_Caracter_6bit[35]='\#'; AIS_Caracter_6bit[36]='$';   AIS_Caracter_6bit[37]='%';   AIS_Caracter_6bit[38]='&';   AIS_Caracter_6bit[39]='\'';
//AIS_Caracter_6bit[40]='(';   AIS_Caracter_6bit[41]=')';   AIS_Caracter_6bit[42]='\*'; AIS_Caracter_6bit[43]='\+'; AIS_Caracter_6bit[44]=',';   AIS_Caracter_6bit[45]='-';   AIS_Caracter_6bit[46]='.';   AIS_Caracter_6bit[47]='/';
//AIS_Caracter_6bit[48]='0';   AIS_Caracter_6bit[49]='1';   AIS_Caracter_6bit[50]='2';  AIS_Caracter_6bit[51]='3';  AIS_Caracter_6bit[52]='4';   AIS_Caracter_6bit[53]='5';   AIS_Caracter_6bit[54]='6';   AIS_Caracter_6bit[55]='7';
//AIS_Caracter_6bit[56]='8';   AIS_Caracter_6bit[57]='9';   AIS_Caracter_6bit[58]=':';  AIS_Caracter_6bit[59]=';';  AIS_Caracter_6bit[60]='<';   AIS_Caracter_6bit[61]='=';   AIS_Caracter_6bit[62]='>';   AIS_Caracter_6bit[63]='?';

//------------------------------------> Tipos de Provedores de Posicion
//enum AIS_Estacion_Tierra {Idefinido,GPS,GLONASS,Combinacion_de_GPSGLONASS,Loran_C,Chayka,Sistema_de_Navegacion_Integrado,Medido,Galileo};
//    QString AIS_Estacion_Tierra[9];
//    "Idefinido","GPS","GLONASS","Combinacion_de_GPS GLONASS","Loran_C","Chayka","Sistema_de_Navegación_Integrado","Medido","Galileo";
//    std::string AIS_Estacion_Tierra[] =

//------------------------------------> Control de Objetivos <-------------------------------------------
struct E_DatEst_ContrObj
{
    QString FechaIni;
    QString FechaFin;
    QString HoraIni;
    QString HoraFin;
    QString Operador;
};

struct E_DataPosContrObj
{
    double Lat;
    double Long;
    double Vel;
    double Rumbo;
    QString UTC;
    int Estado_Tx;
};

struct E_DatDin_ContrObj
{
    QString Nombre_Sys;
    int NoObjv;
    QString UTC_Ini;
    QString Nombre_Barco;
    QString Fuente;
    QString MMSI;
    float Eslora;
    float manga;
    QString Destino;
    QString Tipo_Barco;
    QList<E_DataPosContrObj> ListDatPos;

    void Resetear();
};



#endif // CUTILES_H
