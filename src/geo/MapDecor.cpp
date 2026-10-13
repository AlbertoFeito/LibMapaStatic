#include "geo/MapDecor.h"

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

} // namespace MapDecor
} // namespace libmapa
