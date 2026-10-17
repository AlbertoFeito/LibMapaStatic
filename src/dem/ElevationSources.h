#ifndef LIBMAPA_DEM_ELEVATIONSOURCES_H_
#define LIBMAPA_DEM_ELEVATIONSOURCES_H_

#include "dem/IElevationSource.h"

#include <QString>

#include <memory>

namespace libmapa {

/*!
 * \brief ¿La ruta apunta a datos GeoTIFF? (un `.tif`/`.tiff`, o una carpeta que
 *        tiene alguno y ningún `.hgt`).
 */
bool isGeoTiffElevationPath(const QString &path);

/*!
 * \brief Abre el origen de elevación que corresponde a \a path, sea cual sea.
 *
 *  - Fichero `.tif`/`.tiff`            -> GeoTiffElevation
 *  - Carpeta con `.hgt`                -> HgtElevation (como siempre)
 *  - Carpeta con `.tif` y sin `.hgt`   -> GeoTiffElevation
 *  - Cualquier otro fichero            -> SqliteElevation (como siempre)
 *
 * Así quien configure la elevación (MapConfig, un paquete de datos, la demo) no
 * necesita saber el formato: lo da la ruta. Con \a path vacío devuelve null. Si
 * es GeoTIFF devuelve siempre la fuente (si no se pudo leer ninguna imagen,
 * responde NaN) y deja en \a error, una por línea, las razones por las que algún
 * fichero no se pudo usar (también si solo fallan algunos).
 */
std::unique_ptr<IElevationSource> openElevationSource(const QString &path,
                                                      QString *error = nullptr);

} // namespace libmapa

#endif // LIBMAPA_DEM_ELEVATIONSOURCES_H_
