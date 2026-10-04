#include "dem/HgtElevation.h"

#include <QFile>

namespace libmapa {

// Cambia la carpeta de los `.hgt` y vacia la cache (los tiles cacheados eran de
// la carpeta anterior).
void HgtElevation::setDirectory(const QString &dir)
{
    m_dir = dir;
    clearCache();
}

// Arma el nombre SRTM del tile cuya esquina SUROESTE es (latFloor, lonFloor):
// hemisferio por el signo y relleno con ceros (lat a 2 digitos, lon a 3), p.ej.
// (19, -77) -> "N19W077.hgt". Es el convenio de NASA/USGS y el de AWS Skadi.
QString HgtElevation::fileNameFor(int latFloor, int lonFloor)
{
    const QChar ns = latFloor >= 0 ? QLatin1Char('N') : QLatin1Char('S');
    const QChar ew = lonFloor >= 0 ? QLatin1Char('E') : QLatin1Char('W');
    return QStringLiteral("%1%2%3%4.hgt")
        .arg(ns)
        .arg(qAbs(latFloor), 2, 10, QLatin1Char('0'))
        .arg(ew)
        .arg(qAbs(lonFloor), 3, 10, QLatin1Char('0'));
}

// Lee del disco el `.hgt` del tile y deduce su lado por el tamano. Devuelve false
// si el fichero no existe o su tamano no es 2*lado*lado (la base lo revalida).
bool HgtElevation::loadTile(int latFloor, int lonFloor,
                            QByteArray &data, int &side) const
{
    QFile f(m_dir + QLatin1Char('/') + fileNameFor(latFloor, lonFloor));
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = f.readAll();
    const int lado = isqrtExact(qint64(bytes.size()) / 2);
    if (lado < 2 || qint64(lado) * lado * 2 != bytes.size())
        return false;
    data = bytes;
    side = lado;
    return true;
}

} // namespace libmapa
