/*!
 * geotiff_check - revisa ficheros GeoTIFF (p. ej. cuadros de Copernicus) y avisa
 * de los que están cortados o dañados.
 *
 * Por qué: un GeoTIFF con la descarga cortada o con bloques que no se pueden
 * descomprimir se lee como «sin dato» en esa zona, y en el mapa aparece un hueco
 * rectangular sin más aviso. Esta herramienta recorre todos los bloques de cada
 * fichero y dice cuáles fallan y dónde.
 *
 * Uso:
 *   geotiff_check <fichero.tif | carpeta> [más…]
 *
 * Devuelve 0 si todos los ficheros están bien y 1 si alguno tiene problemas.
 */

#include "dem/GeoTiff.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <cmath>

using namespace libmapa;

// Ficheros .tif/.tiff de una ruta: ella misma si es un fichero, o los de la carpeta.
static QStringList ficheros(const QString &ruta)
{
    const QFileInfo fi(ruta);
    if (!fi.isDir())
        return { ruta };
    QStringList out;
    const QFileInfoList l = QDir(ruta).entryInfoList({ QStringLiteral("*.tif"), QStringLiteral("*.tiff") },
                                                     QDir::Files, QDir::Name);
    for (const QFileInfo &f : l)
        out << f.absoluteFilePath();
    return out;
}

// Punto de entrada: revisa cada fichero y escribe un resumen por línea.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 2) {
        out << "Uso: geotiff_check <fichero.tif | carpeta> [más…]\n";
        return 2;
    }

    QStringList todos;
    for (int i = 1; i < argc; ++i)
        todos << ficheros(QString::fromLocal8Bit(argv[i]));
    if (todos.isEmpty()) {
        out << "No hay ficheros .tif en las rutas dadas.\n";
        return 2;
    }

    int malos = 0;
    for (const QString &file : todos) {
        GeoTiffImage img;
        QString error;
        if (!img.open(file, &error)) {
            out << "MAL  " << QFileInfo(file).fileName() << ": no se pudo abrir (" << error << ")\n";
            ++malos;
            continue;
        }
        const GeoTiffBlockReport r = img.verifyBlocks();
        const int fallos = r.outOfFile + r.undecodable;
        out << (fallos ? "MAL  " : "ok   ") << QFileInfo(file).fileName() << "  "
            << img.width() << "x" << img.height() << ", " << r.fileSize << " bytes, lat "
            << img.latMin() << ".." << img.latMax() << ", lon " << img.lonMin() << ".." << img.lonMax()
            << ", " << r.total << " bloques";
        if (r.sparse)
            out << " (" << r.sparse << " vacíos, se leen como sin dato)";
        out << "\n";
        for (const QString &p : r.problems)
            out << "       " << p << "\n";
        if (fallos)
            ++malos;
    }
    out << "\n" << todos.size() << " ficheros, " << malos << " con problemas.\n";
    return malos ? 1 : 0;
}
