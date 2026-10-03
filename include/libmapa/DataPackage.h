#ifndef LIBMAPA_DATAPACKAGE_H_
#define LIBMAPA_DATAPACKAGE_H_

#include <QGeoRectangle>
#include <QMetaType>
#include <QString>

namespace libmapa {

/*!
 * \brief Descripcion de un paquete de datos (lo que dice su mapa.json).
 *
 * Un paquete de datos es una CARPETA con un manifiesto `mapa.json` y todos los
 * ficheros que necesita el mapa sin conexion: las bases de teselas, la de
 * elevacion, las capas fijas `.geo` y, si acaso, unas entidades de partida.
 * Todas las rutas del manifiesto son relativas a esa carpeta, asi que el
 * paquete se puede copiar a otro PC o instalar junto a la aplicacion tal cual.
 *
 * Esta estructura es solo la parte INFORMATIVA, para que la aplicacion la
 * muestre (nombre, version de los datos, atribucion). El resto del manifiesto
 * lo aplica el MapWidget por su cuenta al abrir el paquete.
 */
struct DataPackageInfo
{
    QString id;                //!< Identificador estable ("cuba"). Separa los datos de usuario de cada paquete.
    QString name;              //!< Nombre para la interfaz ("Cuba").
    QString dataVersion;       //!< Version de los DATOS, la pone quien prepara el paquete ("2026.10").
    QString created;           //!< Fecha de creacion, texto ISO ("2026-10-03").
    QString description;
    QGeoRectangle bounds;      //!< Zona que cubre el paquete; invalido si no se declara.
    QString attribution;       //!< Texto de atribucion de los datos (OSM la exige).

    QString directory;         //!< Carpeta absoluta del paquete.
    QString manifestPath;      //!< Ruta absoluta del mapa.json.

    bool isValid() const { return !manifestPath.isEmpty(); }
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::DataPackageInfo)

#endif // LIBMAPA_DATAPACKAGE_H_
