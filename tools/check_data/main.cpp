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
 *              [--export <carpeta destino>] [--verbose]
 *
 * Con --export, si no hay errores, copia el paquete a otra carpeta: el
 * mapa.json y SOLO los ficheros que referencia (la carpeta de origen puede
 * tener otras cosas que el mapa no usa), respetando sus rutas relativas.
 * Reanudable: lo que ya esta copiado con el mismo tamano no se vuelve a copiar.
 * Al terminar comprueba la copia.
 *
 * Codigo de salida: 0 si no hay errores, 1 si los hay (o, con --strict, si hay
 * avisos, o si la exportacion falla), 2 si los argumentos estan mal. Asi se
 * puede usar en un script.
 */

#include "db/SqliteConnectionPool.h"
#include "io/DataPackage.h"
#include "io/PackageCheck.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
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

// Copia los ficheros del paquete a 'destino' respetando sus rutas relativas.
// Se niega si alguno queda FUERA de la carpeta del paquete (el mapa.json copiado
// apuntaria a un sitio que no existe en el destino). Salta los que ya estan con
// el mismo tamano, para poder reanudar una copia de varios GB. false si algo
// falla; el motivo va a 'err'.
static bool exportPackage(const DataPackage &p, const QString &destino,
                          QTextStream &out, QTextStream &err)
{
    const QDir origen(p.info.directory);
    const QDir dst(destino);
    if (QDir::cleanPath(dst.absolutePath()).compare(QDir::cleanPath(origen.absolutePath()),
                                                    Qt::CaseInsensitive) == 0) {
        err << "El destino es la misma carpeta que el paquete.\n";
        return false;
    }

    const QStringList ficheros = p.files();
    for (const QString &f : ficheros) {
        if (origen.relativeFilePath(f).startsWith(QLatin1String(".."))) {
            err << "No se puede exportar: " << QDir::toNativeSeparators(f)
                << " esta fuera de la carpeta del paquete.\n";
            return false;
        }
    }

    out << "\n Exportando " << ficheros.size() << " fichero(s) a "
        << QDir::toNativeSeparators(dst.absolutePath()) << "\n";
    for (const QString &f : ficheros) {
        const QString rel = origen.relativeFilePath(f);
        const QString destinoF = dst.absoluteFilePath(rel);
        const QFileInfo fi(f);
        out << "   " << rel << "  (" << humanSize(fi.size()) << ") ... " << Qt::flush;

        const QFileInfo ya(destinoF);
        if (ya.exists() && ya.size() == fi.size()) {
            out << "ya estaba\n";
            continue;
        }
        if (!QDir().mkpath(ya.absolutePath())
            || (ya.exists() && !QFile::remove(destinoF))
            || !QFile::copy(f, destinoF)) {
            out << "FALLO\n";
            err << "No se pudo copiar " << QDir::toNativeSeparators(f) << " a "
                << QDir::toNativeSeparators(destinoF) << "\n";
            return false;
        }
        // La copia de un fichero instalado (solo lectura) seguiria siendolo.
        QFile::setPermissions(destinoF, QFile::permissions(destinoF)
                                            | QFileDevice::WriteOwner);
        out << "ok\n";
    }
    return true;
}

// Punto de entrada: lee el paquete, lo comprueba (modo rapido o completo) e
// imprime el informe: cabecera del paquete, una ficha por capa base con su
// cobertura por zoom, y los hallazgos ordenados por gravedad. Con --export,
// ademas lo copia (solo si no hay errores) y comprueba la copia.
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
    QCommandLineOption optExport(QStringLiteral("export"),
        QStringLiteral("Si no hay errores, copiar el paquete (solo sus ficheros) a esta carpeta."),
        QStringLiteral("carpeta"));
    QCommandLineOption optVerbose(QStringLiteral("verbose"),
        QStringLiteral("Mostrar tambien el registro interno de la libreria."));
    parser.addOption(optQuick);
    parser.addOption(optMaxZoom);
    parser.addOption(optStrict);
    parser.addOption(optExport);
    parser.addOption(optVerbose);
    parser.process(app);

    // El registro interno (que BD abre y cierra) es ruido en un informe.
    if (!parser.isSet(optVerbose))
        QLoggingCategory::setFilterRules(QStringLiteral("libmapa.*.debug=false\n"
                                                        "libmapa.*.info=false"));

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
    if (errores > 0) {
        if (parser.isSet(optExport))
            err << " No se exporta: primero hay que corregir los errores.\n";
        return 1;
    }

    // --- exportar y comprobar la copia ----------------------------------
    if (parser.isSet(optExport) && paquete) {
        const QString destino = parser.value(optExport);
        if (!exportPackage(*paquete, destino, out, err))
            return 1;
        PackageCheck::Options rapido;
        rapido.coverage = false;
        const PackageCheck copia = PackageCheck::run(destino, rapido);
        SqliteConnectionPool::closeAllForCurrentThread();
        if (copia.hasErrors()) {
            err << " La copia tiene errores:\n   "
                << copia.problems().join(QStringLiteral("\n   ")) << "\n";
            return 1;
        }
        out << " Copia comprobada: lista para ir junto a la aplicacion.\n";
    }

    return (parser.isSet(optStrict) && avisos > 0) ? 1 : 0;
}
