#ifndef LIBMAPA_DEM_GEOTIFFELEVATION_H_
#define LIBMAPA_DEM_GEOTIFFELEVATION_H_

#include "dem/GeoTiff.h"
#include "dem/IElevationSource.h"

#include <QGeoCoordinate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

namespace libmapa {

/*!
 * \brief Fuente de elevación que lee modelos digitales de terreno en GeoTIFF.
 *
 * Acepta un fichero `.tif`/`.tiff` o una CARPETA con varios (cada uno cubre su
 * zona; las zonas pueden solaparse). Da la cota en metros por interpolación
 * bilineal, o NaN donde no hay dato: fuera de todos los ficheros, o sobre un
 * valor «sin dato» del DEM. Respeta el mismo contrato que \c HgtElevation y
 * \c SqliteElevation, así que el resto de la librería (perfil, visibilidad,
 * viewshed, curvas de nivel, relieve) lo usa sin saber de dónde viene la cota.
 *
 * Solo se lee la cabecera de cada fichero al abrir; los píxeles se leen bajo
 * demanda (ver \c GeoTiffImage), de modo que un DEM de varios GB no ocupa memoria.
 * Se usa la primera banda y se supone que la unidad vertical es el metro.
 */
class GeoTiffElevation : public IElevationSource
{
public:
    GeoTiffElevation() = default;
    explicit GeoTiffElevation(const QString &path) { setPath(path); }

    /*!
     * \brief Fija el fichero GeoTIFF o la carpeta de GeoTIFF.
     * \return true si se pudo abrir al menos una imagen. Los ficheros que no se
     *         pudieron leer quedan en \c warnings().
     */
    bool setPath(const QString &path);

    QString path() const { return m_path; }
    int imageCount() const { return int(m_images.size()); }

    //! Zona (en grados) que cubren, juntas, todas las imágenes abiertas. false si
    //! no hay ninguna.
    bool extent(double *latMin, double *latMax, double *lonMin, double *lonMax) const;

    //! Motivos por los que algún fichero de la ruta no se pudo usar.
    QStringList warnings() const { return m_warnings; }

    //! Cota (m) interpolada en \a c, o NaN si no hay dato.
    double elevationAt(const QGeoCoordinate &c) const override;

private:
    //! Clave de la celda de 1° que contiene (latFloor, lonFloor).
    static qint64 cellKey(int latFloor, int lonFloor)
    { return qint64(latFloor) * 1000 + lonFloor; }

    //! Añade \a image al índice por celdas de 1°.
    void indexImage(int index);

    QString m_path;
    QStringList m_warnings;
    QVector<std::shared_ptr<GeoTiffImage>> m_images;
    QHash<qint64, QVector<int>> m_cells;     //!< celda de 1° -> imágenes que la tocan
    mutable std::atomic<int> m_last{-1};      //!< última imagen que respondió (atajo)
};

} // namespace libmapa

#endif // LIBMAPA_DEM_GEOTIFFELEVATION_H_
