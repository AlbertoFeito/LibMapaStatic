#ifndef LIBMAPA_GEOFILE_H_
#define LIBMAPA_GEOFILE_H_

#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Un trazado de un fichero .geo: una lista de vertices.
 *
 * \a closed indica que el ultimo vertice coincide con el primero (un anillo).
 */
struct GeoPath
{
    QVector<QGeoCoordinate> points;
    bool closed = false;

    bool isEmpty() const { return points.isEmpty(); }
};

/*!
 * \brief Lee un fichero .geo, que puede contener VARIOS trazados.
 *
 * El formato .geo es una linea por vertice, "longitud,latitud," (OJO: primero
 * la longitud), y "0.0,0.0" como SEPARADOR entre trazados (no un simple fin de
 * fichero). Asi un mismo fichero lleva desde un solo anillo (las aguas
 * jurisdiccionales) hasta decenas de polilineas (corredores, divisiones
 * administrativas).
 *
 * Devuelve un trazado por cada bloque separado por "0.0,0.0". Las lineas en
 * blanco o mal formadas se saltan; los bloques vacios tambien. El orden del
 * fichero es longitud,latitud; aqui se coloca como (latitud, longitud).
 *
 * Si no se puede abrir o no hay ningun vertice valido, devuelve una lista
 * vacia y escribe el motivo en \a error.
 */
LIBMAPA_EXPORT QVector<GeoPath> readGeoFile(const QString &path,
                                            QString *error = nullptr);

} // namespace libmapa

#endif // LIBMAPA_GEOFILE_H_
