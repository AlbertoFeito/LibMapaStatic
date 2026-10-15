#include "geo/MapDecor.h"

#include <algorithm>
#include <cmath>

namespace libmapa {
namespace MapDecor {

namespace {

constexpr double kMetersPerNauticalMile = 1852.0;

// Mayor valor "redondo" (1, 2 o 5 por una potencia de 10) que no supera \a v.
// Es la serie que usan las escalas de las cartas: se lee de un vistazo.
double redondoPorDebajo(double v)
{
    const double pot = std::pow(10.0, std::floor(std::log10(v)));
    const double m = v / pot;                 // en [1, 10)
    const double base = m >= 5.0 - 1e-9 ? 5.0 : (m >= 2.0 - 1e-9 ? 2.0 : 1.0);
    return base * pot;
}

// Numero sin ceros sobrantes: 2 -> "2", 0.5 -> "0.5". Formato fijo con punto
// decimal para que la etiqueta no dependa del idioma del sistema.
QString numero(double v)
{
    return QString::number(v, 'g', 6);
}

// Texto de un angulo absoluto en grados/minutos/segundos con la precision del
// paso: solo grados si el paso es de grados enteros, minutos si es de minutos,
// segundos si baja de un minuto. Redondea al segundo para no arrastrar
// decimales del calculo (23.4999999 -> 23°30').
QString sexagesimal(double absDeg, double stepDeg)
{
    const long long totalSeg = std::llround(absDeg * 3600.0);
    const long long g = totalSeg / 3600;
    const long long m = (totalSeg % 3600) / 60;
    const long long s = totalSeg % 60;
    const QString grados = QString::number(g) + QChar(0x00B0);
    if (stepDeg >= 1.0 - 1e-9)
        return grados;
    const QString minutos = grados + QStringLiteral("%1'").arg(m, 2, 10, QLatin1Char('0'));
    if (stepDeg >= 1.0 / 60.0 - 1e-9)
        return minutos;
    return minutos + QStringLiteral("%1\"").arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

// Barra de escala: el mayor valor redondo que cabe en maxPixels, convertido a
// pixeles con la resolucion dada. En millas nauticas se redondea en millas (0.5,
// 1, 2 NM...), no en metros, para que la etiqueta salga redonda.
ScaleBar scaleBar(double metersPerPixel, double maxPixels, ScaleUnits units)
{
    ScaleBar b;
    if (!(metersPerPixel > 0.0) || !std::isfinite(metersPerPixel) || !(maxPixels > 0.0))
        return b;

    const double unidad = units == ScaleUnits::Nautical ? kMetersPerNauticalMile : 1.0;
    const double maxUnidades = metersPerPixel * maxPixels / unidad;
    const double valor = redondoPorDebajo(maxUnidades);
    b.meters = valor * unidad;
    b.pixels = b.meters / metersPerPixel;

    if (units == ScaleUnits::Nautical)
        b.label = numero(valor) + QStringLiteral(" NM");
    else if (valor >= 1000.0)
        b.label = numero(valor / 1000.0) + QStringLiteral(" km");
    else
        b.label = numero(valor) + QStringLiteral(" m");
    return b;
}

// Recorre los pasos de carta de mayor a menor y se queda con el mas fino que no
// supere maxLines lineas en el tramo visible. Si ni el mas grueso cabe (tramo
// enorme con pocas lineas permitidas) se usa el mas grueso.
double graticuleStep(double spanDeg, int maxLines)
{
    static const double pasos[] = {
        90.0, 45.0, 30.0, 20.0, 15.0, 10.0, 5.0, 2.0, 1.0,
        30.0 / 60.0, 20.0 / 60.0, 15.0 / 60.0, 10.0 / 60.0, 5.0 / 60.0,
        2.0 / 60.0, 1.0 / 60.0,
        30.0 / 3600.0, 20.0 / 3600.0, 15.0 / 3600.0, 10.0 / 3600.0,
        5.0 / 3600.0, 2.0 / 3600.0, 1.0 / 3600.0,
    };
    const double tramo = std::fabs(spanDeg);
    const int lineas = maxLines < 1 ? 1 : maxLines;
    double elegido = pasos[0];
    for (double p : pasos) {
        if (tramo / p > lineas)
            break;
        elegido = p;
    }
    return elegido;
}

// Latitud con hemisferio (N/S). El ecuador va sin letra.
QString formatLatitude(double latitudeDeg, double stepDeg)
{
    const QString txt = sexagesimal(std::fabs(latitudeDeg), stepDeg);
    if (std::llround(std::fabs(latitudeDeg) * 3600.0) == 0)
        return txt;
    return txt + (latitudeDeg > 0.0 ? QStringLiteral("N") : QStringLiteral("S"));
}

// Longitud con hemisferio (E/W), normalizada a (-180, 180]. El meridiano 0 y
// el 180 van sin letra.
QString formatLongitude(double longitudeDeg, double stepDeg)
{
    double lon = std::fmod(longitudeDeg, 360.0);
    if (lon > 180.0)
        lon -= 360.0;
    else if (lon <= -180.0)
        lon += 360.0;
    const long long seg = std::llround(std::fabs(lon) * 3600.0);
    const QString txt = sexagesimal(std::fabs(lon), stepDeg);
    if (seg == 0 || seg == 180LL * 3600LL)
        return txt;
    return txt + (lon > 0.0 ? QStringLiteral("E") : QStringLiteral("W"));
}

// Numero con los decimales pedidos y sin ceros de relleno a la derecha.
static QString sinCeros(double v, int decimales)
{
    QString s = QString::number(v, 'f', decimales);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

// Decimales para un valor en su unidad: 2 hasta 10, 1 hasta 100, 0 despues.
static int decimalesPara(double v)
{
    return v < 10.0 ? 2 : (v < 100.0 ? 1 : 0);
}

// Distancia legible en la unidad pedida (ver cabecera).
QString formatDistance(double meters, ScaleUnits units)
{
    if (!std::isfinite(meters))
        return QString();
    const double m = std::abs(meters);
    if (units == ScaleUnits::Nautical) {
        const double nm = m / kMetersPerNauticalMile;
        return sinCeros(nm, decimalesPara(nm)) + QStringLiteral(" NM");
    }
    if (m < 1000.0)
        return QString::number(std::lround(m)) + QStringLiteral(" m");
    const double km = m / 1000.0;
    return sinCeros(km, decimalesPara(km)) + QStringLiteral(" km");
}

// Superficie legible en la unidad pedida (ver cabecera).
QString formatArea(double squareMeters, ScaleUnits units)
{
    if (!std::isfinite(squareMeters))
        return QString();
    const double m2 = std::abs(squareMeters);
    const QString cuadrado = QString(QChar(0x00B2));
    if (units == ScaleUnits::Nautical) {
        const double nm2 = m2 / (kMetersPerNauticalMile * kMetersPerNauticalMile);
        return sinCeros(nm2, decimalesPara(nm2)) + QStringLiteral(" NM") + cuadrado;
    }
    if (m2 < 1.0e4)
        return QString::number(std::lround(m2)) + QStringLiteral(" m") + cuadrado;
    if (m2 < 1.0e6) {
        const double ha = m2 / 1.0e4;
        return sinCeros(ha, decimalesPara(ha)) + QStringLiteral(" ha");
    }
    const double km2 = m2 / 1.0e6;
    return sinCeros(km2, decimalesPara(km2)) + QStringLiteral(" km") + cuadrado;
}

// Rumbo con tres cifras y el simbolo de grado.
QString formatBearing(double degrees)
{
    if (!std::isfinite(degrees))
        return QString();
    long g = std::lround(std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0));
    if (g >= 360)
        g -= 360;
    return QStringLiteral("%1").arg(g, 3, 10, QLatin1Char('0')) + QChar(0x00B0);
}

// Puntos a la misma distancia del centro en azimuts equiespaciados (ver cabecera).
QVector<QGeoCoordinate> geodesicCircle(const QGeoCoordinate &center, double radiusMeters,
                                       int segments)
{
    QVector<QGeoCoordinate> v;
    if (!center.isValid() || !(radiusMeters > 0.0) || !std::isfinite(radiusMeters))
        return v;
    const int n = std::max(8, segments);
    v.reserve(n + 1);
    for (int i = 0; i < n; ++i)
        v.append(center.atDistanceAndAzimuth(radiusMeters, 360.0 * i / n));
    v.append(v.first());
    return v;
}

} // namespace MapDecor
} // namespace libmapa
