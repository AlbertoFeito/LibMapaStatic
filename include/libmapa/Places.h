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

} // namespace libmapa

#endif // LIBMAPA_PLACES_H_
