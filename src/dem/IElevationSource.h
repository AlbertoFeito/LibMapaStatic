#ifndef LIBMAPA_DEM_IELEVATIONSOURCE_H_
#define LIBMAPA_DEM_IELEVATIONSOURCE_H_

#include <QGeoCoordinate>

namespace libmapa {

/*!
 * \brief Origen de elevacion. Abstrae de DONDE salen las muestras de altura.
 *
 * Igual que \c ITileSource hace con las teselas, esta interfaz deja que el resto
 * de la libreria (y las apps) pidan la cota de una coordenada sin saber si viene
 * de ficheros `.hgt` sueltos (\c HgtElevation) o de una base de datos SQLite
 * empaquetable (\c SqliteElevation). El contrato es siempre el mismo: metros, o
 * NaN si no hay dato.
 */
class IElevationSource
{
public:
    virtual ~IElevationSource() = default;

    //! Cota del terreno (metros) en \a c, o NaN si no hay dato (tile ausente,
    //! sin origen configurado, o hueco). Comprobar con std::isnan.
    virtual double elevationAt(const QGeoCoordinate &c) const = 0;
};

} // namespace libmapa

#endif // LIBMAPA_DEM_IELEVATIONSOURCE_H_
