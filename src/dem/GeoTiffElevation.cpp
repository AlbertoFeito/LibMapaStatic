#include "dem/GeoTiffElevation.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>
#include <limits>

namespace libmapa {

// Abre el fichero (o todos los .tif/.tiff de la carpeta, sin recursión) y arma
// el índice por celdas de 1°. Los que no se pueden leer se anotan en
// \c warnings() y se saltan.
bool GeoTiffElevation::setPath(const QString &path)
{
    m_path = path;
    m_warnings.clear();
    m_images.clear();
    m_cells.clear();
    m_last = -1;

    QStringList files;
    const QFileInfo info(path);
    if (info.isDir()) {
        const QStringList filters = { QStringLiteral("*.tif"), QStringLiteral("*.tiff"),
                                      QStringLiteral("*.TIF"), QStringLiteral("*.TIFF") };
        const QFileInfoList list = QDir(path).entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo &f : list)
            files << f.absoluteFilePath();
        if (files.isEmpty())
            m_warnings << QStringLiteral("No hay ficheros .tif en %1").arg(path);
    } else if (info.isFile()) {
        files << info.absoluteFilePath();
    } else if (!path.isEmpty()) {
        m_warnings << QStringLiteral("No existe %1").arg(path);
    }

    for (const QString &f : files) {
        auto img = std::make_shared<GeoTiffImage>();
        QString error;
        if (!img->open(f, &error)) {
            m_warnings << error;
            continue;
        }
        m_images.append(img);
        indexImage(int(m_images.size()) - 1);
    }
    return !m_images.isEmpty();
}

// Anota en cada celda de 1° que toca la imagen \a index que hay que mirarla ahí.
// El borde exacto (p. ej. lon = 10.0 como límite este) no cuenta como celda.
void GeoTiffElevation::indexImage(int index)
{
    const GeoTiffImage &img = *m_images.at(index);
    const double eps = 1e-9;
    const int la0 = int(std::floor(img.latMin()));
    const int la1 = int(std::floor(img.latMax() - eps));
    const int lo0 = int(std::floor(img.lonMin()));
    const int lo1 = int(std::floor(img.lonMax() - eps));
    for (int la = la0; la <= std::max(la0, la1); ++la)
        for (int lo = lo0; lo <= std::max(lo0, lo1); ++lo)
            m_cells[cellKey(la, lo)].append(index);
}

// Cota interpolada en \a c. Busca primero en la última imagen que respondió
// (casi siempre acierta en recorridos y viewsheds) y después en las de la celda.
double GeoTiffElevation::elevationAt(const QGeoCoordinate &c) const
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!c.isValid() || m_images.isEmpty())
        return nan;
    const double lat = c.latitude();
    const double lon = c.longitude();

    const int last = m_last.load();
    if (last >= 0 && last < m_images.size()) {
        const double v = m_images.at(last)->interpolate(lon, lat);
        if (!std::isnan(v))
            return v;
    }
    const auto it = m_cells.constFind(cellKey(int(std::floor(lat)), int(std::floor(lon))));
    if (it == m_cells.constEnd())
        return nan;
    for (const int idx : it.value()) {
        if (idx == last)
            continue;
        const double v = m_images.at(idx)->interpolate(lon, lat);
        if (!std::isnan(v)) {
            m_last = idx;
            return v;
        }
    }
    return nan;
}

} // namespace libmapa
