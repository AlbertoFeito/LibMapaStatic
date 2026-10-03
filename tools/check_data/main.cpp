/*!
 * check_data - Comprueba un paquete de datos (mapa.json) antes de distribuirlo.
 *
 * El producto final trabaja sin conexion: lo que falte en el paquete no se
 * puede descargar despues. Esta herramienta dice, ANTES de copiarlo, si estan
 * todos los ficheros, si cada base abre, si sus imagenes se pueden decodificar
 * con este Qt, que zonas y zooms cubre cada capa y cuanto ocupa todo.
 *
 * Uso:
 *   check_data <carpeta del paquete | mapa.json> [--quick] [--max-zoom N] [--strict]
 *
 * Codigo de salida: 0 si no hay errores, 1 si los hay (o, con --strict, si hay
 * avisos), 2 si los argumentos estan mal. Asi se puede usar en un script.
 */

#include "db/SqliteConnectionPool.h"
#include "io/DataPackage.h"
#include "io/PackageCheck.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTextStream>

using namespace libmapa;

// Bytes en texto legible (KiB, MiB o GiB).
static QString humanSize(qint64 bytes)
{
    const double b = static_cast<double>(bytes);
    if (b >= 1024.0 * 1024.0 * 1024.0)
        return QString::number(b / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GiB");
    if (b >= 1024.0 * 1024.0)
        return QString::number(b / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MiB");
    return QString::number(b / 1024.0, 'f', 1) + QStringLiteral(" KiB");
}

// Etiqueta de una gravedad para el informe.
static QString label(PackageCheck::Severity s)
{
    switch (s) {
    case PackageCheck::Severity::Error:   return QStringLiteral("ERROR");
    case PackageCheck::Severity::Warning: return QStringLiteral("AVISO");
    case PackageCheck::Severity::Info:    return QStringLiteral("info ");
    }
    return QString();
}

// Punto de entrada: lee el paquete, lo comprueba (modo rapido o completo) e
// imprime el informe: cabecera del paquete, una ficha por capa base con su
// cobertura por zoom, y los hallazgos ordenados por gravedad.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("check_data"));
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Comprueba un paquete de datos (mapa.json) antes de distribuirlo"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("paquete"),
        QStringLiteral("Carpeta del paquete o su mapa.json"));
    QCommandLineOption optQuick(QStringLiteral("quick"),
        QStringLiteral("Solo lo rapido: sin contar la cobertura por zoom."));
    QCommandLineOption optMaxZoom(QStringLiteral("max-zoom"),
        QStringLiteral("No medir cobertura por encima de este zoom (las BD grandes tardan)."),
        QStringLiteral("N"), QStringLiteral("99"));
    QCommandLineOption optStrict(QStringLiteral("strict"),
        QStringLiteral("Salir con 1 tambien si hay avisos."));
    parser.addOption(optQuick);
    parser.addOption(optMaxZoom);
    parser.addOption(optStrict);
    parser.process(app);

    if (parser.positionalArguments().size() != 1) {
        err << "Uso: check_data <carpeta del paquete | mapa.json> "
               "[--quick] [--max-zoom N] [--strict]\n";
        return 2;
    }
    const QString ruta = parser.positionalArguments().first();

    PackageCheck::Options opciones;
    opciones.coverage = !parser.isSet(optQuick);
    opciones.maxCoverageZoom = parser.value(optMaxZoom).toInt();

    QElapsedTimer reloj;
    reloj.start();
    const auto paquete = DataPackage::load(ruta);
    const PackageCheck r = paquete ? PackageCheck::run(*paquete, opciones)
                                   : PackageCheck::run(ruta, opciones);

    // --- cabecera -------------------------------------------------------
    out << "==========================================================\n";
    if (paquete) {
        const DataPackageInfo &i = paquete->info;
        out << " PAQUETE   : " << i.name << "  (id " << i.id << ")\n";
        out << " Datos     : version " << (i.dataVersion.isEmpty() ? QStringLiteral("-") : i.dataVersion)
            << ", creado " << (i.created.isEmpty() ? QStringLiteral("-") : i.created) << "\n";
        out << " Manifiesto: " << QDir::toNativeSeparators(i.manifestPath) << "\n";
        if (i.bounds.isValid())
            out << " Zona      : N " << i.bounds.topLeft().latitude()
                << "  O " << i.bounds.topLeft().longitude()
                << "  S " << i.bounds.bottomRight().latitude()
                << "  E " << i.bounds.bottomRight().longitude() << "\n";
        out << " Tamano    : " << humanSize(r.totalBytes) << "\n";
    } else {
        out << " PAQUETE   : " << QDir::toNativeSeparators(ruta) << "\n";
    }
    out << "==========================================================\n\n";

    // --- capas base -----------------------------------------------------
    for (const PackageCheck::DatasetReport &d : r.datasets) {
        out << " CAPA " << d.id << "  " << QDir::toNativeSeparators(d.file) << "\n";
        if (!d.opened) {
            out << "   (no se pudo abrir)\n\n";
            continue;
        }
        out << "   " << humanSize(d.bytes) << ", imagenes "
            << (d.imageFormat.isEmpty() ? QStringLiteral("?") : d.imageFormat) << "\n";
        if (!d.levels.isEmpty()) {
            out << "   zoom   teselas en la zona   cobertura\n";
            for (const PackageCheck::LevelCoverage &lc : d.levels) {
                out << "   " << QString::number(lc.z).rightJustified(4)
                    << "   " << QString::number(lc.present).rightJustified(9)
                    << " / " << QString::number(lc.expected).leftJustified(9)
                    << QString::number(lc.fraction() * 100.0, 'f', 1).rightJustified(6)
                    << " %\n";
            }
        }
        out << "\n";
    }

    // --- hallazgos, de mas a menos grave --------------------------------
    for (auto s : {PackageCheck::Severity::Error, PackageCheck::Severity::Warning,
                   PackageCheck::Severity::Info})
        for (const PackageCheck::Finding &f : r.findings)
            if (f.severity == s)
                out << " " << label(s) << "  " << f.subject << ": " << f.message << "\n";

    const int errores = r.count(PackageCheck::Severity::Error);
    const int avisos = r.count(PackageCheck::Severity::Warning);
    out << "\n Resultado: " << errores << " error(es), " << avisos << " aviso(s)"
        << " en " << reloj.elapsed() << " ms. "
        << (errores > 0 ? "El paquete NO esta listo para distribuir."
                        : "El paquete se puede distribuir.")
        << "\n";

    SqliteConnectionPool::closeAllForCurrentThread();
    if (errores > 0)
        return 1;
    return (parser.isSet(optStrict) && avisos > 0) ? 1 : 0;
}
