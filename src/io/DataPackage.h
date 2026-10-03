#ifndef LIBMAPA_IO_DATAPACKAGE_H_
#define LIBMAPA_IO_DATAPACKAGE_H_

#include "libmapa/DataPackage.h"
#include "libmapa/MapFeature.h"
#include "tiles/TileDataset.h"

#include <QGeoCoordinate>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace libmapa {

/*!
 * \brief Paquete de datos sin conexion, leido de su manifiesto `mapa.json`.
 *
 * Reune en UN fichero todo lo que antes se configuraba por separado (el
 * datasets.json, la BD de elevacion, la de entidades y el punto de arranque),
 * con rutas relativas a la carpeta del paquete. Aqui ya vienen resueltas a
 * absolutas.
 *
 * Formato (version 2):
 *
 *     {
 *       "format": "libmapa-package", "version": 2,
 *       "package":  { "id", "name", "dataVersion", "created", "description",
 *                     "bounds": {"north","west","south","east"}, "attribution" },
 *       "start":    { "layer", "center": [lat, lon], "zoom" },
 *       "datasets": [ ...igual que en datasets.json... ],
 *       "elevation":{ "file": "dem.sqlitedb" }   o   { "dir": "hgt" },
 *       "overlays": [ { "id", "name", "file": "x.geo", "zOrder",
 *                       "style": {"lineColor","fillColor","lineWidth"} } ],
 *       "features": { "file": "entidades.db", "seed": "entidades_iniciales.db" }
 *     }
 *
 * Un datasets.json de la version 1 (solo "datasets") tambien se acepta: es un
 * paquete sin nada mas que capas base.
 */
struct DataPackage
{
    //! Una capa vectorial FIJA del paquete: se carga al abrir, no se edita ni se
    //! guarda en la BD de entidades del usuario.
    struct Overlay {
        QString id;
        QString name;
        QString file;            //!< Ruta absoluta al .geo.
        int zOrder = 0;
        FeatureStyle style;
    };

    DataPackageInfo info;

    QString startLayer;          //!< Vacio = el primer dataset.
    QGeoCoordinate startCenter;  //!< Invalido = sin centro declarado.
    int startZoom = -1;          //!< < 0 = sin zoom declarado.

    QVector<TileDataset> datasets;

    QString elevationFile;       //!< BD de elevacion (absoluta), o vacio.
    QString elevationDir;        //!< Carpeta de .hgt (absoluta), o vacio.

    QVector<Overlay> overlays;

    QString featuresFile;        //!< Tal cual viene en el manifiesto (sin resolver).
    QString featuresSeed;        //!< Entidades de partida (absoluta), o vacio.

    //! Avisos no fatales detectados al leer (ficheros que no existen...).
    QStringList warnings;

    //! Nombre por defecto del manifiesto dentro de la carpeta del paquete.
    static QString manifestFileName() { return QStringLiteral("mapa.json"); }

    /*!
     * \brief Lee un paquete. \a path puede ser la CARPETA (se busca mapa.json
     *        dentro) o directamente el fichero JSON.
     *
     * Devuelve nullopt si el manifiesto no existe, no es JSON, es de un formato
     * o version que esta libreria no entiende, o no trae ningun dataset valido;
     * el motivo queda en \a error. Los ficheros que falten NO impiden abrirlo:
     * se anotan en \ref warnings.
     */
    static std::optional<DataPackage> load(const QString &path,
                                           QString *error = nullptr);

    /*!
     * \brief Ruta ESCRIBIBLE de la BD de entidades del usuario.
     *
     * Una ruta relativa en "features.file" NO se resuelve contra la carpeta del
     * paquete (que puede estar en Program Files, de solo lectura) sino contra la
     * carpeta de datos de la aplicacion: AppDataLocation/<package.id>/. Una ruta
     * absoluta se respeta. Vacio si el paquete no declara entidades.
     */
    QString resolveFeaturesPath() const;

    /*!
     * \brief Deja lista la BD de entidades del usuario: crea su carpeta y, si
     *        aun no existe y el paquete trae "seed", copia esa BD de partida.
     *
     * Devuelve la ruta (la de \ref resolveFeaturesPath) o vacio si no hay
     * entidades declaradas o no se pudo crear la carpeta (motivo en \a error).
     */
    QString prepareFeaturesFile(QString *error = nullptr) const;
};

} // namespace libmapa

#endif // LIBMAPA_IO_DATAPACKAGE_H_
