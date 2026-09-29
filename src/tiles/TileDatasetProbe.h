#ifndef LIBMAPA_TILES_TILEDATASETPROBE_H_
#define LIBMAPA_TILES_TILEDATASETPROBE_H_

#include "tiles/TileDataset.h"

#include <QGeoRectangle>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

// Declaracion adelantada FUERA del namespace: si se pusiera dentro, el
// compilador entenderia libmapa::QSqlDatabase, que es otro tipo distinto.
class QSqlDatabase;

namespace libmapa {

//! Estadisticas de un nivel de zoom, tal y como estan en la BD.
struct ZoomLevelStats
{
    int storedZ = 0;
    int logicalZ = 0;
    qint64 tileCount = 0;
    int xMin = 0, xMax = 0;
    int yMin = 0, yMax = 0;
    //! teselas presentes / teselas que cabrian en el rectangulo x,y.
    //! Por debajo de 1.0 hay huecos que el render debe tolerar.
    double fillRatio = 0.0;

    //! Extension geografica de ESTE nivel, una vez decidido el esquema.
    double lonWest = 0.0, lonEast = 0.0;
    double latNorth = 0.0, latSouth = 0.0;
    double lonSpan() const { return lonEast - lonWest; }
    double latSpan() const { return latNorth - latSouth; }
};

//! Resultado completo de sondear una BD de teselas.
struct ProbeResult
{
    TileDataset dataset;
    QVector<ZoomLevelStats> levels;

    qint64 totalTiles = 0;
    qint64 fileSizeBytes = 0;
    int pageSize = 0;

    //! Extension geografica cubierta, tomada del NIVEL DE REFERENCIA (el que
    //! mas teselas tiene), no del mas detallado: en ambas BD reales el nivel
    //! mas profundo esta a medio poblar y da una extension enganosa.
    QGeoRectangle coverage;

    //! Indice dentro de 'levels' del nivel usado como referencia.
    int referenceLevel = -1;

    //! Como se decidio el esquema y la inversion de z (para el informe).
    QString schemeReason;
    QString zMappingReason;

    //! Resultado de comprobar si filtrar por la columna 's' pierde teselas.
    bool   sFilterVerified = false;
    qint64 sFilterKeeps = 0;
    qint64 sFilterTotal = 0;

    //! Avisos no fatales (columna 's' ausente, niveles con huecos, etc.).
    QStringList warnings;
};

/*!
 * \brief Descubre los parametros reales de una BD de teselas.
 *
 * Esta es la pieza que elimina de raiz la falla F-4: en el codigo original,
 * cutiles.h declaraba
 *
 *     #define ImagenesOSM 0
 *     #define ImagenesSatelitales 1
 *
 * mientras que CBDatos::Cargar_BD_IMG abria la BD Satelital cuando recibia 0
 * y la de OSM cuando recibia 1, justo al reves. Funcionaba solo porque
 * CMapaPlot::Cargar_Imagenes cruzaba los valores a mano, y el comentario
 * "tipoBD = 1; //imagenes OSM" era falso.
 *
 * En vez de constantes cableadas, aqui se le pregunta a la propia BD:
 * que niveles tiene, si z esta invertido, que valor toma 's', de que tamano
 * son las teselas y si el eje Y es XYZ o TMS.
 *
 * Coste: unas pocas consultas agregadas sobre el indice + la decodificacion
 * de UNA imagen. No lee el resto de los 1.4 GB.
 */
class TileDatasetProbe
{
public:
    /*!
     * \param filePath  ruta al .sqlitedb
     * \param id        identificador logico ("osm", "satelital")
     * \param reference extension geografica esperada del mapa. Se usa solo
     *                  para desempatar XYZ vs TMS. Si es invalida, se asume
     *                  XYZ y se anota el aviso correspondiente.
     */
    static std::optional<ProbeResult> probe(const QString &filePath,
                                            const QString &id,
                                            const QGeoRectangle &reference = {});

    //! Informe legible del resultado, para la herramienta de linea de comandos.
    static QString formatReport(const ProbeResult &r);

private:
    static bool detectColumns(QSqlDatabase &db, TileDataset &ds,
                              QStringList &warnings);
    static bool detectZMapping(QSqlDatabase &db, TileDataset &ds,
                               ProbeResult &result,
                               const QGeoRectangle &reference);
    static void detectScheme(QSqlDatabase &db, TileDataset &ds,
                             ProbeResult &result, const QGeoRectangle &reference);
    static void detectTileSize(QSqlDatabase &db, TileDataset &ds,
                               ProbeResult &result);
};

} // namespace libmapa

#endif // LIBMAPA_TILES_TILEDATASETPROBE_H_
