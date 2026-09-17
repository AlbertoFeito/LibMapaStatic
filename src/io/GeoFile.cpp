#include "libmapa/GeoFile.h"

#include "core/Logging.h"

#include <QFile>
#include <QTextStream>
#include <cmath>

namespace libmapa {

namespace {
//! Dos coordenadas se consideran el mismo vertice si difieren menos de esto.
constexpr double kEps = 1e-6;

bool mismoPunto(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    return std::fabs(a.latitude() - b.latitude()) < kEps
        && std::fabs(a.longitude() - b.longitude()) < kEps;
}
} // namespace

GeoData readGeoFile(const QString &path, QString *error)
{
    GeoData data;

    QFile fichero(path);
    if (!fichero.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("No se pudo abrir %1: %2")
                         .arg(path, fichero.errorString());
        return data;
    }

    QTextStream in(&fichero);
    int nLinea = 0;
    while (!in.atEnd()) {
        const QString linea = in.readLine().trimmed();
        ++nLinea;
        if (linea.isEmpty())
            continue;

        const QStringList campos = linea.split(QLatin1Char(','));
        if (campos.size() < 2)
            continue;

        bool okLon = false, okLat = false;
        const double lon = campos.at(0).trimmed().toDouble(&okLon);
        const double lat = campos.at(1).trimmed().toDouble(&okLat);
        if (!okLon || !okLat) {
            qCWarning(lcMapaRender)
                << "Linea" << nLinea << "de" << path << "no es un vertice:" << linea;
            continue;
        }

        // "0.0,0.0" es el terminador del formato, no un vertice.
        if (lon == 0.0 && lat == 0.0)
            break;

        const QGeoCoordinate c(lat, lon);     // el fichero trae lon,lat
        if (!c.isValid()) {
            qCWarning(lcMapaRender)
                << "Vertice fuera de rango en la linea" << nLinea << ":" << linea;
            continue;
        }
        data.points.append(c);
    }

    if (data.points.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 no contiene vertices validos").arg(path);
        return data;
    }

    data.closed = data.points.size() >= 2
               && mismoPunto(data.points.first(), data.points.last());
    return data;
}

} // namespace libmapa
