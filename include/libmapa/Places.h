#ifndef LIBMAPA_PLACES_H_
#define LIBMAPA_PLACES_H_

#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QString>

namespace libmapa {

/*!
 * \brief Un lugar con nombre del índice de búsqueda (ciudad, aeropuerto, bahía…).
 *
 * Sale de `MapWidget::searchPlaces`. El índice es una base de datos
 * `lugares.sqlitedb` local (la genera `osm_lugares_a_sqlitedb.py` desde
 * OpenStreetMap), así que buscar no necesita internet.
 */
struct LIBMAPA_EXPORT Place
{
    qint64 id = 0;                 //!< Identificador dentro de la base de datos.
    QString name;                  //!< Nombre tal como está en el mapa ("Guantánamo").
    //! Tipo interno: city, town, village, hamlet, suburb, island, islet, locality,
    //! airport, harbour, bay, cape, peak o strait.
    QString type;
    QGeoCoordinate coordinate;     //!< Posición (centro, si el lugar es una superficie).
    qint64 population = 0;         //!< Habitantes, 0 si no se conocen.

    //! Tipo en español para mostrarlo ("ciudad", "aeropuerto"…).
    QString typeLabel() const;

    //! Zoom al que conviene centrar el mapa en este lugar (una ciudad se ve a
    //! zoom más bajo que una aldea o un aeropuerto).
    int suggestedZoom() const;
};

/*!
 * \brief Un punto de interés del MBTiles vectorial (farmacia, hospital, gasolinera…).
 *
 * Sale de `MapWidget::searchPointsOfInterest`. A diferencia de `Place`, no sale de
 * un índice aparte: se lee directamente de la capa `poi` de las teselas vectoriales
 * que hay alrededor del mapa, así que sirve cualquier MBTiles de OpenMapTiles.
 */
struct LIBMAPA_EXPORT PointOfInterest
{
    QString name;                  //!< Nombre; vacío si el MBTiles no lo trae.
    QString type;                  //!< `class` de OpenMapTiles: "pharmacy", "fuel"…
    QString subtype;               //!< `subclass`: "hotel", "bakery"… (puede estar vacío).
    QGeoCoordinate coordinate;
    double distanceM = 0.0;        //!< Distancia al centro del mapa en la búsqueda.

    //! Tipo en español para mostrarlo ("farmacia", "hotel"…); el tipo tal cual si no lo conoce.
    QString typeLabel() const;
    //! El nombre, o el tipo si no tiene ("Farmacia"): siempre algo que mostrar.
    QString displayName() const;
};

/*!
 * \brief Resultado de \c MapWidget::importPointsOfInterest.
 *
 * Si \c tooLarge, no se leyó nada: la zona abarca \c tiles teselas y el tope es
 * \c MapWidget::kMaxPoiImportTiles.
 */
struct LIBMAPA_EXPORT PoiImportResult
{
    int found = 0;          //!< POI de la zona que casan con el texto.
    int added = 0;          //!< Entidades nuevas creadas.
    int skipped = 0;        //!< Ya estaban en la capa (mismo tipo, nombre y sitio).
    int tiles = 0;          //!< Teselas que abarca la zona.
    bool tooLarge = false;  //!< La zona abarca demasiadas teselas; no se leyó nada.
    bool cancelled = false; //!< El usuario cortó la importación a medias.
    bool truncated = false; //!< Se llegó al tope de entidades y se dejaron POI sin añadir.
    QString error;          //!< Motivo si no se pudo (sin capa vectorial, zona vacía...).

    //! ¿Terminó sin error ni corte?
    bool ok() const { return error.isEmpty() && !tooLarge && !cancelled; }
};

/*!
 * \brief Una ruta por la red vial (resultado de \c MapWidget::findRoute).
 *
 * Si \c ok() es false, \c error dice por que (y el resto no vale).
 */
struct LIBMAPA_EXPORT RouteResult
{
    QString error;                          //!< Vacio si se encontro ruta.
    QVector<QGeoCoordinate> path;           //!< El trazado, del origen al destino.
    double distanceM = 0.0;                 //!< Longitud (m), con los tramos de enlace a la via.
    double durationS = 0.0;                 //!< Tiempo estimado en coche (s), por la velocidad de cada tipo de via.
    //! Metros por tipo de via ("primary", "minor"...), de mas a menos.
    QVector<QPair<QString, double>> byClass;
    double snapFromM = 0.0;                 //!< Lo que hubo que andar del origen a la via mas cercana.
    double snapToM = 0.0;                   //!< Lo mismo del destino.
    int tiles = 0;                          //!< Teselas leidas para armar la red.
    int nodes = 0;                          //!< Cruces y vertices de la red armada.

    bool ok() const { return error.isEmpty() && path.size() >= 2; }
};

} // namespace libmapa

#endif // LIBMAPA_PLACES_H_
