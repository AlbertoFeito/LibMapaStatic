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

} // namespace libmapa

#endif // LIBMAPA_PLACES_H_
