#include "cutiles.h"

cUtiles::cUtiles()
{

}

uint cUtiles::HexToDec(QString Hex)
{
    return Hex.toUInt(0, 16);
}

double cUtiles::GmsToGrados(GMS gms)
{
    return ((double)gms.G + (double)gms.M / 60.0 + (double)gms.S / 3600.0);
}

GMS cUtiles::GradosToGms(double Valor)
{
    GMS gms;
    gms.G = (short)Valor;
    gms.M = (uchar)fmod(Valor * 60, 60);
    gms.S = (uchar)fmod(Valor * 3600, 60);
    return gms;
}

double cUtiles::GmsFloatToGrados(GMS_Float gms)
{
    return ((double)gms.G + (double)gms.M / 60.0 + (double)gms.S / 3600.0);
}

double cUtiles::GMFloatToDecimales(GM_Float gm) //---> convierte de Grado Minutos a Grados Decimales
{
    QString aux = QString::number(gm.G);
    aux += '.' + QString::number(gm.M / 6,'f',6).remove('.');
    return (aux.toDouble());
}

GMS_Float cUtiles::GradosToGmsFloat(double Valor)
{
    GMS_Float gms;
    gms.G = (short)Valor;
    gms.M = (short)fmod(Valor * 60, 60);
    gms.S = (float)fmod(Valor * 3600, 60);
    return gms;
}

void cUtiles::SexagesFromQGeoCoordinate_ToGms_Float(QString Valor, GMS_Float &lat, GMS_Float &lon)
{
    QString g = "°";
    QStringList lGeo = Valor.split(',');
    QString a = lGeo.at(0);
    QString b = lGeo.at(1);
    a.replace(g,"*");
    b.replace(g,"*");

    GMS_Float Lat;
    QStringList Dat = a.split('*');
    lat.G = Dat[0].toInt();
    Dat = Dat[1].split("'");
    lat.M = Dat[0].toInt();
    lat.M = QString::number(lat.M).size() < 2 ? QString("0" + QString::number(lat.M)).toInt() : lat.M;
    Dat = Dat[1].split('"');
    lat.S = Dat[0].toFloat();
    lat.S = QString::number(lat.S).size() < 2 ? QString("0" + QString::number(lat.S)).toFloat() : lat.S;
    if (Dat[1].contains("S"))
        lat.G = lat.G * -1;

    Dat = b.split('*');
    lon.G = Dat[0].toInt();
    Dat = Dat[1].split("'");
    lon.M = Dat[0].toInt();
    lon.M = QString::number(lon.M).size() < 2 ? QString("0" + QString::number(lon.M)).toInt() : lon.M;
    Dat = Dat[1].split('"');
    lon.S = Dat[0].toFloat();
    lon.S = QString::number(lon.S).size() < 2 ? QString("0" + QString::number(lon.S)).toFloat() : lon.S;
    if (Dat[1].contains("W"))
        lon.G = lon.G * -1;
}

double cUtiles::getDistancia2D(double x1, double y1, double x2, double y2)
{
    return sqrt(((x1 - x2) * (x1 - x2)) + ((y1 - y2) * (y1 - y2)));
}
double cUtiles::getDistancia3D(double x1, double y1, double z1, double x2, double y2, double z2)
{
    return sqrt(((x1 - x2) * (x1 - x2)) + ((y1 - y2) * (y1 - y2)) + ((z1 - z2) * (z1 - z2)));
}
bool cUtiles::EnSector(double Ang, double SectorInicio, double SectorFin)//para saber si un angulo esta dentro de un sector
{
    if((Ang >= 0 && Ang < 360) && (SectorInicio >= 0 && SectorInicio < 360) && (SectorFin >= 0 && SectorFin < 360))
    {
        bool inside;
        if(SectorInicio == SectorFin)
            inside = (Ang == SectorInicio);
        else if(SectorInicio < SectorFin)
            inside = (Ang >= SectorInicio && Ang <= SectorFin);
        else
            inside = (Ang >= SectorInicio || Ang <= SectorFin);
        return inside;
    }
    return false;
}
double cUtiles::GetAnguloMedio(double SectorInicio, double SectorFin)
{
    if((SectorInicio >= 0 && SectorInicio < 360) && (SectorFin >= 0 && SectorFin < 360))
    {
        double medio;
        if(SectorInicio == SectorFin)
            medio = SectorInicio;
        else if(SectorInicio < SectorFin)
            medio = (SectorInicio + SectorFin) / 2.0;
        else
            medio = fmod((SectorInicio + (360.0 + SectorFin)) / 2.0, 360.0);
        return medio;
    }
    return -1;
}
bool cUtiles::IsPolarModeClockWiseOnRepre(PolarMode pm)//
{
    bool bClockWise;
    switch(pm)
    {
    case PM_0_90:
    case PM_180_270:
    case PM_90_180:
    case PM_270_0:
    {
        bClockWise = false;
        break;
    }
    default:
    {
        bClockWise = true;
        break;
    }
    }
    return bClockWise;
}
bool cUtiles::IsEqualMem(void *Mem1, void *Mem2, uint nSizeInBytes)
{
    for(uint i = 0; i < nSizeInBytes; i++)
    {
        if(((uchar*)Mem1)[i] != ((uchar*)Mem2)[i])
            return false;
    }
    return true;
}

QString cUtiles::Slot_Tomar_Hora_Sistema()
{
    QString Hora=QString::number(QTime::currentTime().hour());
    QString Min=QString::number(QTime::currentTime().minute());
    QString Seg=QString::number(QTime::currentTime().second());
    if (Hora.toInt()<10)
        Hora="0"+Hora;
    if (Min.toInt()<10)
        Min="0"+Min;
    if (Seg.toInt()<10)
        Seg="0"+Seg;
    return (Hora+":"+Min+":"+Seg);
}


QString cUtiles::Slot_Tomar_Fecha_Sistema()
{
    QString Dia=QString::number(QDate::currentDate().day());
    QString Mes=QString::number(QDate::currentDate().month());
    QString Ano=QString::number(QDate::currentDate().year());
    if (Dia.toInt()<10)
        Dia="0"+Dia;
    if (Mes.toInt()<10)
        Mes="0"+Mes;
    return(Dia+"_"+Mes+"_"+Ano);
}

quint64 cUtiles::Slot_Tomar_UTC()
{
    return QDateTime::currentMSecsSinceEpoch();
}


// Funciones para escritura de valores en un fichero de configuración
void cUtiles::WriteConfigString(QString strFileName, QString strSection, QString strEntry, QString strValue)
{
    QSettings s(strFileName, QSettings::IniFormat);
    s.setValue(strSection + "/" + strEntry, strValue);
}
void cUtiles::WriteConfigInt(QString strFileName, QString strSection, QString strEntry, int nValue)
{
    QSettings s(strFileName, QSettings::IniFormat);
    s.setValue(strSection + "/" + strEntry, nValue);
}
void cUtiles::WriteConfigUInt(QString strFileName, QString strSection, QString strEntry, uint nValue)
{
    QSettings s(strFileName, QSettings::IniFormat);
    s.setValue(strSection + "/" + strEntry, nValue);
}
void cUtiles::WriteConfigHex(QString strFileName, QString strSection, QString strEntry, uint nValue)
{
    WriteConfigString(strFileName, strSection, strEntry, QString::number(nValue, 16).toUpper());
}
void cUtiles::WriteConfigDouble(QString strFileName, QString strSection, QString strEntry, double dValue)
{
    QSettings s(strFileName, QSettings::IniFormat);
    s.setValue(strSection + "/" + strEntry, dValue);
}
void cUtiles::WriteConfigBool(QString strFileName, QString strSection, QString strEntry, bool bValue)
{
    QSettings s(strFileName, QSettings::IniFormat);
    s.setValue(strSection + "/" + strEntry, bValue);
}
void cUtiles::WriteConfigRGB(QString strFileName, QString strSection, QString strEntry, QRgb rgbValue)
{
    QString strValue = QString("rgb(%1,%2,%3)")
            .arg(qRed(rgbValue))
            .arg(qGreen(rgbValue))
            .arg(qBlue(rgbValue));
    WriteConfigString(strFileName, strSection, strEntry, strValue);
}
void cUtiles::WriteConfigGMS(QString strFileName, QString strSection, QString strEntry, GMS gmsValue)
{
    QString strValue = QString("gms(%1,%2,%3)")
            .arg(gmsValue.G)
            .arg(gmsValue.M)
            .arg(gmsValue.S);
    WriteConfigString(strFileName, strSection, strEntry, strValue);
}
void cUtiles::WriteConfigGMS(QString strFileName, QString strSection, QString strEntry, double dValue)
{
    cUtiles::WriteConfigGMS(strFileName, strSection, strEntry, cUtiles::GradosToGms(dValue));
}
void cUtiles::WriteConfigData(QString strFileName, QString strSection, QString strEntry, void* pData, uint nDataSize)
{
    QString strValue;
    uchar* pChar = (uchar*)pData;
    for(uint i = 0; i < nDataSize; i++)
    {
        strValue.append(QString("%1").arg((uint)pChar[i], 2, 16, QLatin1Char('0')));
    }
    WriteConfigString(strFileName, strSection, strEntry, strValue);
}


// Funciones para lectura de valores de un fichero de configuración
QString cUtiles::GetConfigString(QString strFileName, QString strSection, QString strEntry, QString strDefault, bool bUpdateFile)
{
    QSettings s(strFileName, QSettings::IniFormat);
    QString strValue = s.value(strSection + "/" + strEntry, strDefault).toString();
    if(bUpdateFile)
        WriteConfigString(strFileName, strSection, strEntry, strValue);
    return strValue;
}
int cUtiles::GetConfigInt(QString strFileName, QString strSection, QString strEntry, int nDefault, bool bUpdateFile)
{
    QSettings s(strFileName, QSettings::IniFormat);
    int nValue = s.value(strSection + "/" + strEntry, nDefault).toInt();
    if(bUpdateFile)
        WriteConfigInt(strFileName, strSection, strEntry, nValue);
    return nValue;
}
uint cUtiles::GetConfigUInt(QString strFileName, QString strSection, QString strEntry, uint nDefault, bool bUpdateFile)
{
    QSettings s(strFileName, QSettings::IniFormat);
    int nValue = s.value(strSection + "/" + strEntry, nDefault).toUInt();
    if(bUpdateFile)
        WriteConfigUInt(strFileName, strSection, strEntry, nValue);
    return nValue;
}
uint cUtiles::GetConfigHex(QString strFileName, QString strSection, QString strEntry, uint nDefault, bool bUpdateFile)
{
    uint nValue = GetConfigString(strFileName, strSection, strEntry, QString::number(nDefault, 16)).toUInt(0, 16);
    if(bUpdateFile)
        WriteConfigHex(strFileName, strSection, strEntry, nValue);
    return nValue;
}
double cUtiles::GetConfigDouble(QString strFileName, QString strSection, QString strEntry, double dDefault, bool bUpdateFile)
{
    QSettings s(strFileName, QSettings::IniFormat);
    double dValue = s.value(strSection + "/" + strEntry, dDefault).toDouble();
    if(bUpdateFile)
        WriteConfigDouble(strFileName, strSection, strEntry, dValue);
    return dValue;
}

bool cUtiles::GetConfigBool(QString strFileName, QString strSection, QString strEntry, bool bDefault, bool bUpdateFile)
{
    QSettings s(strFileName, QSettings::IniFormat);
    bool bValue = s.value(strSection + "/" + strEntry, bDefault).toBool();
    if(bUpdateFile)
        WriteConfigBool(strFileName, strSection, strEntry, bValue);
    return bValue;
}
QRgb cUtiles::GetConfigRGB(QString strFileName, QString strSection, QString strEntry, QRgb rgbDefault, bool bUpdateFile)
{
    QString strDefault = QString("rgb(%1,%2,%3)")
            .arg(qRed(rgbDefault))
            .arg(qGreen(rgbDefault))
            .arg(qBlue(rgbDefault));
    QString strValue = GetConfigString(strFileName, strSection, strEntry, strDefault, false);
    QStringList list = strValue.remove(QRegExp("[rgb()]")).split(',', QString::SkipEmptyParts);
    QRgb rgbValue = qRgb(list[0].toInt(), list[1].toInt(), list[2].toInt());
    if(bUpdateFile)
        WriteConfigRGB(strFileName, strSection, strEntry, rgbValue);
    return rgbValue;
}
GMS cUtiles::GetConfigGMS(QString strFileName, QString strSection, QString strEntry, GMS gmsDefault, bool bUpdateFile)
{
    QString strDefault = QString("gms(%1,%2,%3)")
            .arg(gmsDefault.G)
            .arg(gmsDefault.M)
            .arg(gmsDefault.S);
    QString strValue = GetConfigString(strFileName, strSection, strEntry, strDefault, false);
    QStringList list = strValue.remove(QRegExp("[gms()]")).split(',', QString::SkipEmptyParts);
    GMS gmsValue = GMS(list[0].toInt(), list[1].toInt(), list[2].toInt());
    if(bUpdateFile)
        WriteConfigGMS(strFileName, strSection, strEntry, gmsValue);
    return gmsValue;
}
double cUtiles::GetConfigGMS(QString strFileName, QString strSection, QString strEntry, double dDefault, bool bUpdateFile)
{
    return GmsToGrados(GetConfigGMS(strFileName, strSection, strEntry, GradosToGms(dDefault), bUpdateFile));
}
void cUtiles::GetConfigData(QString strFileName, QString strSection, QString strEntry, void* pData, uint nDataSize, bool bUpdateFile)
{
    QString strDefault;
    uchar* pChar = (uchar*)pData;
    for(uint i = 0; i < nDataSize; i++)
    {
        strDefault.append(QString("%1").arg((uint)pChar[i], 2, 16, QLatin1Char('0')));
    }
    QString strValue = GetConfigString(strFileName, strSection, strEntry, strDefault, false);
    for(uint i = 0, j = 0; i < nDataSize; i++, j += 2)
    {
        pChar[i] = strValue.mid(j, 2).toUInt(0, 16);
    }
    if(bUpdateFile)
        WriteConfigData(strFileName, strSection, strEntry, pData, nDataSize);
}


// Funciones "getAngulo"
double cUtiles::getAngulo_0_90(double Xreal, double Yreal)
{
    double M = fmod(atan2(Xreal, Yreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? 0 : PI;
        else
            M = Xreal > 0 ? RAD90 : RAD270;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_0_270(double Xreal, double Yreal)
{
    double M = fmod(atan2(-Xreal, Yreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? 0 : PI;
        else
            M = Xreal > 0 ? RAD270 : RAD90;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_180_90(double Xreal, double Yreal)
{
    double M = fmod(atan2(Xreal, -Yreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? PI : 0;
        else
            M = Xreal > 0 ? RAD90 : RAD270;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_180_270(double Xreal, double Yreal)
{
    double M = fmod(atan2(-Xreal, -Yreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? PI : 0;
        else
            M = Xreal > 0 ? RAD270 : RAD90;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_90_0(double Xreal, double Yreal)
{
    double M = fmod(atan2(Yreal, Xreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? RAD90 : RAD270;
        else
            M = Xreal > 0 ? 0 : PI;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_90_180(double Xreal, double Yreal)
{
    double M = fmod(atan2(Yreal, -Xreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? RAD90 : RAD270;
        else
            M = Xreal > 0 ? PI : 0;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_270_0(double Xreal, double Yreal)
{
    double M = fmod(atan2(-Yreal, Xreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? RAD270 : RAD90;
        else
            M = Xreal > 0 ?  0: PI;
    }
    return M * GRADOS;
}
double cUtiles::getAngulo_270_180(double Xreal, double Yreal)
{
    double M = fmod(atan2(-Yreal, -Xreal) + RAD360, RAD360);
    if(M == 0)
    {
        if(Xreal == 0)
            M = Yreal >= 0 ? RAD270 : RAD90;
        else
            M = Xreal > 0 ? PI : 0;
    }
    return M * GRADOS;
}


// Funciones para conversión de coordenadas (Pixel, Cartesianas, Polares)
void cUtiles::PixelToUnidad(double UnidadMax, int PixelMax, int Pixel, double* Unidad)
{
    *Unidad = Pixel * (UnidadMax / PixelMax);
}
void cUtiles::UnidadToPixel(double UnidadMax, int PixelMax, double Unidad, int* Pixel)
{
    *Pixel = (int)(Unidad / (UnidadMax / PixelMax));
}
void cUtiles::CalcularPixelCentro(double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int GraphicWidth, int GraphicHeight, int* XPixelCentro, int* YPixelCentro)
{
    *XPixelCentro = qRound(XMinReal / ((XMinReal - XMaxReal) / GraphicWidth));
    *YPixelCentro = qRound(YMaxReal / ((YMaxReal - YMinReal) / GraphicHeight));
}
void cUtiles::PixelToReal(int Xpixel, int Ypixel, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, double* Xreal, double* Yreal)
{
    *Xreal = (XPixelCentro - Xpixel) * ((XMinReal - XMaxReal) / Width);
    *Yreal = (YPixelCentro - Ypixel) * ((YMaxReal - YMinReal) / Height);
}
void cUtiles::RealToPixel(double Xreal, double Yreal, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, int* Xpixel, int* Ypixel)
{
    *Xpixel = qRound(XPixelCentro - Xreal / ((XMinReal - XMaxReal) / Width));
    *Ypixel = qRound(YPixelCentro - Yreal / ((YMaxReal - YMinReal) / Height));
}
void cUtiles::RealToPolar(double Xreal, double Yreal, PolarMode pmPolarMode, double* Angulo, double* Distancia)
{
    *Distancia = sqrt(Xreal * Xreal + Yreal * Yreal);
    switch (pmPolarMode)
    {
    case (PM_0_90):
        *Angulo = cUtiles::getAngulo_0_90(Xreal, Yreal);
        break;
    case (PM_0_270):
        *Angulo = cUtiles::getAngulo_0_270(Xreal, Yreal);
        break;
    case (PM_180_90):
        *Angulo = cUtiles::getAngulo_180_90(Xreal, Yreal);
        break;
    case (PM_180_270):
        *Angulo = cUtiles::getAngulo_180_270(Xreal, Yreal);
        break;
    case (PM_90_0):
        *Angulo = cUtiles::getAngulo_90_0(Xreal, Yreal);
        break;
    case (PM_90_180):
        *Angulo = cUtiles::getAngulo_90_180(Xreal, Yreal);
        break;
    case (PM_270_0):
        *Angulo = cUtiles::getAngulo_270_0(Xreal, Yreal);
        break;
    case (PM_270_180):
        *Angulo = cUtiles::getAngulo_270_180(Xreal, Yreal);
        break;
    }
}
void cUtiles::PolarToReal(double Angulo, double Distancia, PolarMode pmPolarMode, double* Xreal, double* Yreal)
{
    double aciRadianes = Angulo * RADIAN;
    switch (pmPolarMode)
    {
    case (PM_0_90):
        *Xreal = Distancia * sin(aciRadianes);
        *Yreal = Distancia * cos(aciRadianes);
        break;
    case (PM_0_270):
        *Xreal = -Distancia * sin(aciRadianes);
        *Yreal = Distancia * cos(aciRadianes);
        break;
    case (PM_180_90):
        *Xreal = Distancia * sin(aciRadianes);
        *Yreal = -Distancia * cos(aciRadianes);
        break;
    case (PM_180_270):
        *Xreal = -Distancia * sin(aciRadianes);
        *Yreal = -Distancia * cos(aciRadianes);
        break;
    case (PM_90_0):
        *Xreal = Distancia * cos(aciRadianes);
        *Yreal = Distancia * sin(aciRadianes);
        break;
    case (PM_90_180):
        *Xreal = -Distancia * cos(aciRadianes);
        *Yreal = Distancia * sin(aciRadianes);
        break;
    case (PM_270_0):
        *Xreal = Distancia * cos(aciRadianes);
        *Yreal = -Distancia * sin(aciRadianes);
        break;
    case (PM_270_180):
        *Xreal = -Distancia * cos(aciRadianes);
        *Yreal = -Distancia * sin(aciRadianes);
        break;
    }
}
void cUtiles::PixelToPolar(int Xpixel, int Ypixel, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, PolarMode pmPolarMode, double* Angulo, double* Distancia)
{
    PixelToReal(Xpixel, Ypixel, XPixelCentro, YPixelCentro, XMinReal, XMaxReal, YMinReal, YMaxReal, Width, Height, Angulo, Distancia);
    RealToPolar(*Angulo, *Distancia, pmPolarMode, Angulo, Distancia);
}
void cUtiles::PolarToPixel(double Angulo, double Distancia, int XPixelCentro, int YPixelCentro, double XMinReal, double XMaxReal, double YMinReal, double YMaxReal, int Width, int Height, PolarMode pmPolarMode, int* Xpixel, int* Ypixel)
{
    double Xreal, Yreal;
    PolarToReal(Angulo, Distancia, pmPolarMode, &Xreal, &Yreal);
    RealToPixel(Xreal, Yreal, XPixelCentro, YPixelCentro, XMinReal, XMaxReal, YMinReal, YMaxReal, Width, Height, Xpixel, Ypixel);
}


// Funciones para manipulación de textos
QString cUtiles::CodificaString(QString s)
{
    ushort Codigo = qrand() % 24 + 100;
    QString str(Codigo);
    if(s == "")
        return str + Codigo;
    for(int i = 0; i < s.length(); i++)
    {
        str.append(s[i].unicode() + Codigo);
    }
    return str;
}
QByteArray cUtiles::CodificaString(QByteArray s)
{
    char Codigo = qrand() % 24 + 100;
    QByteArray str(1, Codigo);
    if(s == "")
        return str + Codigo;
    for(int i = 0; i < s.length(); i++)
    {
        str.append(s[i] + Codigo);
    }
    return str;
}
QString cUtiles::DecodificaString(QString s)
{
    if(s.length() > 1)
    {
        QString str = "";
        ushort Codigo = s[0].unicode();
        for(int i = 1; i < s.length(); i++)
        {
            str.append(s[i].unicode() - Codigo);
        }
        return str;
    }
    return "";
}
QByteArray cUtiles::DecodificaString(QByteArray s)
{
    if(s.length() > 1)
    {
        QByteArray str = "";
        char Codigo = s[0];
        for(int i = 1; i < s.length(); i++)
        {
            str.append(s[i] - Codigo);
        }
        return str;
    }
    return "";
}
int cUtiles::JoinedStringCount(QString strJoinedString)
{
    return strJoinedString.split("\n").length();
}
QStringList cUtiles::SplitJoinedString(QString strJoinedString)
{
    return strJoinedString.split("\n");
}
bool cUtiles::IsStringInJoinedString(QString strString, QString strJoinedString)
{
    return strJoinedString.split("\n").contains(strString);
}
/**************************************************************/

//------------> Estructura Tipo de Punto
void E_Tipos_Punto::setNewTipo(QString descrip)
{
    lTipos.append(descrip);
}

void E_Tipos_Punto::modificarDescrip(QString descripActual, QString newDescrip)
{
    int i = 0;
    foreach (QString tipoActual, lTipos)
    {
        if (tipoActual == descripActual)
        {
            lTipos.replace(i,newDescrip);
            return;
        }

        i ++;
    }
}

qint32 cUtiles::complemento2(quint32 number, quint8 cantBits)
{
    quint32 fullmax;
    qint32 min, max;
    fullmax = (quint32)pow(2.0, cantBits);
    max = fullmax / 2 - 1;
    min = -(max + 1);
    return (number <= max ? number : number - fullmax);
}

qint32 cUtiles::complemento2(quint32 number, quint8 cantBits)
{
    quint32 fullmax;
    qint32 min, max;
    fullmax = (quint32)pow(2.0, cantBits);
    max = fullmax / 2 - 1;
    min = -(max + 1);
    return (number <= max ? number : number - fullmax);
}

void cUtiles::cargarFicheroCodigosMMSIsegunOMI(QString dirFichMMSI, QStringList &listCodMMSI)
{
    QFile fileMMSI(dirFichMMSI);

    QTextStream Fdata(&fileMMSI);
    if (fileMMSI.open(QIODevice::ReadOnly))
    {
        while (!Fdata.atEnd())
        {
            QString data = Fdata.readLine();
            QString mmsiCountry = data;
            listCodMMSI.append(mmsiCountry);
        }
    }
}

QPixmap cUtiles::determinaBandera(QString MMSI, QString dirCarpetaBandera, QStringList listCodMMSI, QString &nombCountry)
{
    QStringList lMMSIPertenencia;
    QStringList fileNamesBanderas = listarFicherosDirectorio(dirCarpetaBandera);
    QString codeCountry = MMSI.left(3);
    QString country;
    foreach (QString mmsiCountry, listCodMMSI)
    {
        QStringList lData = mmsiCountry.split(';');
        if (codeCountry == lData.at(0))
        {
            QString data = lData.at(1);
            country = data.toUtf8();
            break;
        }
    }

    foreach (QString fileName, fileNamesBanderas)
    {
        QString dirfileNameFlag = fileName;
        fileName = fileName.remove(0,8);
        fileName = fileName.mid(0,fileName.lastIndexOf(','));
        fileName = fileName.mid(0,fileName.lastIndexOf('.'));
        QString countryRecontruc;

        if (country.contains(fileName,Qt::CaseInsensitive))
        {
            nombCountry = fileName;
            return QPixmap(dirCarpetaBandera + "/" + dirfileNameFlag);
        }
    }
    return QPixmap();
}

QStringList cUtiles::getLMMSIPertenencia() const
{
    return lMMSIPertenencia;
}

QString cUtiles::GMS_FLOAT_A_Sexagesimal(GMS_Float Lat, bool COORDENADA)   //Conversion de Grados Decimales a Grados Sexagesimales
{
    QString L = QString::number(Lat.G)+"°"+QString::number(Lat.M)+"'"+QString::number(Lat.S,'f',2)+'"';
    if (COORDENADA == COORDENADA_LATITUD)
    {
        if (Lat.G >=0)
            L.append(" N");
        else
            L.append(" S");
    }
    if (COORDENADA == COORDENADA_LONGITUD)
    {
        if (Lat.G >=0)
            L.append(" E");
        else
            L.append(" O");
    }

    return L;
}

