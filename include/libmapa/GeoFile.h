#ifndef LIBMAPA_GEOFILE_H_
#define LIBMAPA_GEOFILE_H_

#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Contenido de un fichero .geo: una lista de vertices.
 *
 * El formato .geo es una linea por vertice, "longitud,latitud," (OJO: primero
 * la longitud), y un "0.0,0.0" final como terminador. Un anillo cerrado repite
 * el primer vertice al final; \a closed lo indica.
 */
struct GeoData
{
    QVector<QGeoCoordinate> points;
    bool closed = false;        //!< El ultimo vertice coincide con el primero.

    bool isEmpty() const { return points.isEmpty(); }
};

/*!
 * \brief Lee un fichero .geo.
 *
 * Se detiene en el terminador "0.0,0.0" o al final del fichero. Las lineas en
 * blanco o mal formadas se saltan. Si no se puede abrir o no hay vertices
 * validos, devuelve un GeoData vacio y escribe el motivo en \a error.
 *
 * El orden del fichero es longitud,latitud; aqui se coloca en el
 * QGeoCoordinate como (latitud, longitud), que es lo que espera la libreria.
 */
LIBMAPA_EXPORT GeoData readGeoFile(const QString &path, QString *error = nullptr);

} // namespace libmapa

#endif // LIBMAPA_GEOFILE_H_
