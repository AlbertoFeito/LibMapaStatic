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

//! Cierra el trazado en curso y lo anade a la lista si tiene algo.
void cerrar(QVector<GeoPath> &paths, GeoPath &actual)
{
    if (actual.points.isEmpty())
        return;
    actual.closed = actual.points.size() >= 2
                 && mismoPunto(actual.points.first(), actual.points.last());
    paths.append(actual);
    actual = GeoPath();
}
} // namespace

QVector<GeoPath> readGeoFile(const QString &path, QString *error)
{
    QVector<GeoPath> paths;

    QFile fichero(path);
    if (!fichero.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("No se pudo abrir %1: %2")
                         .arg(path, fichero.errorString());
        return paths;
    }

    QTextStream in(&fichero);
    GeoPath actual;
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

        // "0.0,0.0" SEPARA trazados: cierra el actual y empieza otro.
        if (lon == 0.0 && lat == 0.0) {
            cerrar(paths, actual);
            continue;
        }

        const QGeoCoordinate c(lat, lon);     // el fichero trae lon,lat
        if (!c.isValid()) {
            qCWarning(lcMapaRender)
                << "Vertice fuera de rango en la linea" << nLinea << ":" << linea;
            continue;
        }
        actual.points.append(c);
    }
    // Un ultimo trazado sin "0.0,0.0" al final tambien cuenta.
    cerrar(paths, actual);

    if (paths.isEmpty() && error)
        *error = QStringLiteral("%1 no contiene trazados validos").arg(path);
    return paths;
}

} // namespace libmapa
