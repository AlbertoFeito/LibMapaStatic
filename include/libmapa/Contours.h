#pragma once

// Curvas de nivel (isohipsas) a partir de un DEM, por marching squares.
// El resultado son polilíneas en coordenadas geográficas, cada una con su cota;
// una herramienta o la app las guarda como capa vectorial o las dibuja.

#include <QGeoCoordinate>
#include <QVector>

#include <limits>

namespace libmapa {

class IElevationSource;

//! Parámetros de generación de curvas de nivel.
struct ContourParams
{
    //! Caja geográfica a cubrir (latN >= latS, lonW <= lonE).
    double latN = 90.0;
    double lonW = -180.0;
    double latS = -90.0;
    double lonE = 180.0;

    //! Separación entre curvas, en metros (p. ej. 100).
    double interval = 100.0;
    //! Cota de referencia: una curva pasa exactamente por aquí (por defecto 0).
    double base = 0.0;
    //! Resolución de muestreo de la rejilla, en metros. Más fino = más detalle
    //! y más memoria/tiempo. Para un país entero conviene subirlo (p. ej. 200).
    double stepMeters = 150.0;
    //! Descarta curvas con longitud total menor que esto (metros); 0 = ninguna.
    double minLengthMeters = 0.0;
    //! Cota mínima a generar (metros): no se emiten curvas por debajo. Por
    //! defecto -infinito (sin recorte); p. ej. 0 recorta al nivel del mar y
    //! omite las curvas batimétricas.
    double minLevel = -std::numeric_limits<double>::infinity();
    //! Cota máxima a generar (metros); por defecto +infinito (sin recorte).
    double maxLevel = std::numeric_limits<double>::infinity();
};

//! Una curva de un nivel de cota (polilínea; cerrada si vuelve a su inicio).
struct ContourLine
{
    double elevation = 0.0;
    QVector<QGeoCoordinate> points;
};

//! Genera las curvas de nivel del DEM dentro de la bbox. Devuelve vacío si no
//! hay cota en la zona. Las celdas con algún vértice sin dato (NaN) se saltan.
QVector<ContourLine> computeContours(const IElevationSource &src,
                                     const ContourParams &params);

} // namespace libmapa
