#ifndef LIBMAPA_DEM_HGTELEVATION_H_
#define LIBMAPA_DEM_HGTELEVATION_H_

#include "dem/GridElevation.h"

#include <QByteArray>
#include <QString>

namespace libmapa {

/*!
 * \brief Fuente de elevacion que lee ficheros SRTM `.hgt` sueltos de una carpeta.
 *
 * Los `.hgt` se nombran por su esquina SUROESTE (`N19W077.hgt` cubre 19-20 N y
 * 77-76 O). Esta clase solo sabe LOCALIZAR y LEER el fichero de un tile; toda la
 * logica de cache, lectura de muestras e interpolacion vive en \c GridElevation.
 * La resolucion (90 m / 30 m) se autodetecta por el tamano del fichero.
 *
 * Uso:
 *   HgtElevation dem;
 *   dem.setDirectory("/ruta/a/los/hgt");
 *   double m = dem.elevationAt(QGeoCoordinate(19.989, -76.996));  // Pico Turquino
 *   if (std::isnan(m)) { ...sin dato... }
 */
class HgtElevation : public GridElevation
{
public:
    HgtElevation() = default;

    //! Fija la carpeta de los `.hgt` (nombres tipo `N19W077.hgt`). Vacia la cache.
    void setDirectory(const QString &dir);
    QString directory() const { return m_dir; }

protected:
    //! Lee el fichero `.hgt` del tile y deduce su lado por el tamano. false si no
    //! existe o el tamano no es un cuadrado perfecto de int16.
    bool loadTile(int latFloor, int lonFloor,
                  QByteArray &data, int &side) const override;

private:
    //! Nombre SRTM del tile cuya esquina SO es (latFloor, lonFloor): hemisferio
    //! por el signo, `N%02d` / `S`, `E%03d` / `W`. (19,-77) -> "N19W077.hgt".
    static QString fileNameFor(int latFloor, int lonFloor);

    QString m_dir;
};

} // namespace libmapa

#endif // LIBMAPA_DEM_HGTELEVATION_H_
