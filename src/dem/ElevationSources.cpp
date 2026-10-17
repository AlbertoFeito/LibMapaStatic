#include "dem/ElevationSources.h"

#include "dem/GeoTiffElevation.h"
#include "dem/HgtElevation.h"
#include "dem/SqliteElevation.h"

#include <QDir>
#include <QFileInfo>

namespace libmapa {

namespace {

// ¿\a name termina en .tif o .tiff (sin distinguir mayúsculas)?
bool hasTiffSuffix(const QString &name)
{
    const QString s = QFileInfo(name).suffix().toLower();
    return s == QLatin1String("tif") || s == QLatin1String("tiff");
}

} // namespace

// Ver la cabecera: un .tif, o una carpeta con .tif y sin .hgt.
bool isGeoTiffElevationPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isFile())
        return hasTiffSuffix(path);
    if (!info.isDir())
        return false;
    const QDir dir(path);
    if (!dir.entryList({ QStringLiteral("*.hgt"), QStringLiteral("*.HGT") }, QDir::Files).isEmpty())
        return false;
    return !dir.entryList({ QStringLiteral("*.tif"), QStringLiteral("*.tiff"),
                            QStringLiteral("*.TIF"), QStringLiteral("*.TIFF") },
                          QDir::Files).isEmpty();
}

// Crea la fuente según la ruta. Todo lo que no es GeoTIFF se comporta exactamente
// como antes: carpeta -> HgtElevation, otro fichero -> SqliteElevation.
std::unique_ptr<IElevationSource> openElevationSource(const QString &path, QString *error)
{
    if (path.isEmpty())
        return nullptr;
    if (isGeoTiffElevationPath(path)) {
        auto tif = std::make_unique<GeoTiffElevation>();
        tif->setPath(path);
        if (error && !tif->warnings().isEmpty())          // tambien si solo fallan algunos
            *error = tif->warnings().join(QLatin1Char('\n'));
        return tif;
    }
    if (QFileInfo(path).isDir()) {
        auto hgt = std::make_unique<HgtElevation>();
        hgt->setDirectory(path);
        return hgt;
    }
    return std::make_unique<SqliteElevation>(path);
}

} // namespace libmapa
