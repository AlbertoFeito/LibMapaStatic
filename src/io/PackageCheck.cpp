#include "io/PackageCheck.h"

#include "db/SqliteConnectionPool.h"
#include "geo/TileMatrix.h"
#include "io/DataPackage.h"
#include "libmapa/GeoFile.h"
#include "tiles/RMapsTileSource.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

namespace libmapa {

namespace {

using Severity = PackageCheck::Severity;

// ¿Esta 'file' DENTRO de la carpeta del paquete? Lo que quede fuera (ruta
// absoluta o con "../") no viaja al copiar la carpeta a otro PC.
bool insidePackage(const QString &directory, const QString &file)
{
    const QString base = QDir::cleanPath(directory) + QLatin1Char('/');
    return QDir::cleanPath(file).startsWith(base, Qt::CaseInsensitive);
}

// Tamano de un fichero, o de todos los de una carpeta (para los .hgt).
qint64 sizeOf(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isFile())
        return fi.size();
    qint64 total = 0;
    if (fi.isDir())
        for (const QFileInfo &f : QDir(path).entryInfoList(QDir::Files))
            total += f.size();
    return total;
}

// Formato de imagen por los primeros bytes, para poder decir CUAL falta cuando
// QImageReader no reconoce nada (sin el plugin, ni siquiera lo identifica).
QString sniffFormat(const QByteArray &data)
{
    if (data.startsWith("\xFF\xD8"))
        return QStringLiteral("jpeg");
    if (data.startsWith("\x89PNG"))
        return QStringLiteral("png");
    if (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
        return QStringLiteral("webp");
    if (data.startsWith("GIF8"))
        return QStringLiteral("gif");
    return QString();
}

// Lista de formatos de imagen que este Qt sabe leer, para el mensaje de error.
QString supportedFormats()
{
    QStringList out;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        out << QString::fromLatin1(f);
    return out.join(QLatin1String(", "));
}

} // namespace

// Cuenta hallazgos de una gravedad.
int PackageCheck::count(Severity s) const
{
    int n = 0;
    for (const Finding &f : findings)
        if (f.severity == s)
            ++n;
    return n;
}

// Avisos y errores como texto plano, "sujeto: mensaje", sin los Info.
QStringList PackageCheck::problems() const
{
    QStringList out;
    for (const Finding &f : findings)
        if (f.severity != Severity::Info)
            out << QStringLiteral("%1: %2").arg(f.subject, f.message);
    return out;
}

// Anade un hallazgo al informe.
void PackageCheck::add(Severity s, const QString &subject, const QString &message)
{
    findings.append(Finding{s, subject, message});
}

// Lee el manifiesto y lo comprueba. Si ni siquiera se puede leer, el informe
// lleva un unico error con el motivo.
PackageCheck PackageCheck::run(const QString &path, const Options &options)
{
    QString error;
    const auto paquete = DataPackage::load(path, &error);
    if (!paquete) {
        PackageCheck r;
        r.add(Severity::Error, QStringLiteral("mapa.json"), error);
        return r;
    }
    return run(*paquete, options);
}

// Todas las comprobaciones, en el orden en que fallaria el mapa: sin driver
// SQLite no abre nada; sin una base, falta esa capa; si sus imagenes no se
// decodifican, sale en blanco; el resto (zona, atribucion, portabilidad) no
// rompe el mapa pero conviene saberlo antes de distribuir.
PackageCheck PackageCheck::run(const DataPackage &p, const Options &options)
{
    PackageCheck r;
    r.manifestPath = p.info.manifestPath;
    const QString dir = p.info.directory;

    // Avisos del lector que no son "falta un fichero" (eso se comprueba aqui
    // abajo, con su gravedad real): entradas del manifiesto que se ignoraron.
    for (const QString &w : p.warnings)
        if (!w.startsWith(QLatin1String("Falta")))
            r.add(Severity::Warning, QStringLiteral("mapa.json"), w);

    // --- Qt -------------------------------------------------------------
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        r.add(Severity::Error, QStringLiteral("Qt"),
              QStringLiteral("Falta el driver QSQLITE (plugin sqldrivers/qsqlite): "
                             "no se puede abrir ninguna base. Despliega la app con "
                             "windeployqt."));
        return r;
    }

    // --- package --------------------------------------------------------
    if (p.info.attribution.isEmpty())
        r.add(Severity::Warning, QStringLiteral("package"),
              QStringLiteral("Sin \"attribution\": OSM y otras fuentes exigen citarlas."));
    if (!p.info.bounds.isValid())
        r.add(Severity::Warning, QStringLiteral("package"),
              QStringLiteral("Sin \"bounds\": no se puede medir la cobertura de la zona."));

    QSet<QString> ids;
    for (const TileDataset &d : p.datasets) {
        if (ids.contains(d.id))
            r.add(Severity::Error, d.id,
                  QStringLiteral("Identificador repetido: solo se usara una de las capas."));
        ids.insert(d.id);
    }
    if (!p.startLayer.isEmpty() && !ids.contains(p.startLayer))
        r.add(Severity::Warning, QStringLiteral("start"),
              QStringLiteral("La capa de arranque \"%1\" no existe; se usara \"%2\".")
                  .arg(p.startLayer, p.datasets.first().id));

    // --- portabilidad: todo dentro de la carpeta ------------------------
    auto revisarRuta = [&](const QString &subject, const QString &file) {
        if (!file.isEmpty() && !insidePackage(dir, file))
            r.add(Severity::Warning, subject,
                  QStringLiteral("%1 esta FUERA de la carpeta del paquete: no viajara "
                                 "al copiarla.").arg(QDir::toNativeSeparators(file)));
    };
    for (const TileDataset &d : p.datasets)
        revisarRuta(d.id, d.filePath);
    revisarRuta(QStringLiteral("elevation"), p.elevationFile);
    revisarRuta(QStringLiteral("elevation"), p.elevationDir);
    for (const DataPackage::Overlay &ov : p.overlays)
        revisarRuta(ov.id, ov.file);
    revisarRuta(QStringLiteral("features"), p.featuresSeed);
    if (!p.featuresFile.isEmpty() && !QFileInfo(p.featuresFile).isRelative())
        r.add(Severity::Warning, QStringLiteral("features"),
              QStringLiteral("Ruta absoluta: en otro PC esa carpeta puede no existir."));

    // --- capas base -----------------------------------------------------
    for (const TileDataset &d : p.datasets) {
        DatasetReport rep;
        rep.id = d.id;
        rep.file = d.filePath;

        if (!QFileInfo::exists(d.filePath)) {
            r.add(Severity::Error, d.id,
                  QStringLiteral("No existe %1").arg(QDir::toNativeSeparators(d.filePath)));
            r.datasets.append(rep);
            continue;
        }
        rep.bytes = sizeOf(d.filePath);
        r.totalBytes += rep.bytes;

        RMapsTileSource src(d);
        if (!src.open()) {
            r.add(Severity::Error, d.id,
                  QStringLiteral("No se puede abrir como base de teselas: %1")
                      .arg(src.lastError()));
            r.datasets.append(rep);
            continue;
        }
        rep.opened = true;

        // Una tesela real del nivel de fondo (o, si esta vacio, del primero que
        // tenga alguna) y se DECODIFICA: es lo que falla sin el plugin qjpeg.
        const int fondo = d.effectiveBaseZoom();
        QByteArray muestra = src.anyTile(fondo);
        if (muestra.isEmpty()) {
            r.add(Severity::Warning, d.id,
                  QStringLiteral("El nivel de fondo z=%1 no tiene teselas: en zonas "
                                 "sin detalle (mar abierto) se vera vacio.").arg(fondo));
            for (int z = d.minZoom; z <= d.maxZoom && muestra.isEmpty(); ++z)
                muestra = src.anyTile(z);
        }
        if (muestra.isEmpty()) {
            r.add(Severity::Error, d.id,
                  QStringLiteral("La base no tiene ninguna tesela en z=%1..%2 (revisa "
                                 "zFactor/zOffset y sValue).").arg(d.minZoom).arg(d.maxZoom));
        } else {
            QBuffer buffer(&muestra);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            rep.imageFormat = QString::fromLatin1(reader.format());
            if (rep.imageFormat.isEmpty())
                rep.imageFormat = sniffFormat(muestra);
            if (reader.read().isNull())
                r.add(Severity::Error, d.id,
                      QStringLiteral("Sus imagenes (%1) no se pueden decodificar: falta el "
                                     "plugin de imagen de Qt (imageformats). Formatos "
                                     "disponibles: %2")
                          .arg(rep.imageFormat.isEmpty() ? QStringLiteral("formato desconocido")
                                                         : rep.imageFormat,
                               supportedFormats()));
        }

        // Cobertura de la zona del paquete, nivel a nivel (solo modo completo).
        if (options.coverage && p.info.bounds.isValid()) {
            const int hasta = qMin(d.recommendedMaxZoom, options.maxCoverageZoom);
            for (int z = d.minZoom; z <= hasta; ++z) {
                const TileMatrix::TileRange rango = TileMatrix::rangeFor(
                    p.info.bounds.topLeft(), p.info.bounds.bottomRight(), z);
                LevelCoverage lc;
                lc.z = z;
                lc.expected = rango.count();
                lc.present = src.countInRange(z, rango.xMin, rango.xMax,
                                              rango.yMin, rango.yMax);
                rep.levels.append(lc);
                if (lc.present == 0)
                    r.add(Severity::Warning, d.id,
                          QStringLiteral("z=%1 no tiene ninguna tesela dentro de la zona "
                                         "del paquete.").arg(z));
            }
        }
        r.datasets.append(rep);
    }

    // --- elevacion ------------------------------------------------------
    if (!p.elevationFile.isEmpty()) {
        if (!QFileInfo::exists(p.elevationFile)) {
            r.add(Severity::Error, QStringLiteral("elevation"),
                  QStringLiteral("No existe %1").arg(QDir::toNativeSeparators(p.elevationFile)));
        } else {
            r.totalBytes += sizeOf(p.elevationFile);
            QSqlDatabase db = SqliteConnectionPool::connectionFor(
                QStringLiteral("check_dem"), p.elevationFile,
                SqliteConnectionPool::Mode::ReadOnly);
            QSqlQuery q(db);
            if (!db.isOpen() || !q.exec(QStringLiteral("SELECT COUNT(*) FROM dem_tiles"))
                || !q.next()) {
                r.add(Severity::Error, QStringLiteral("elevation"),
                      QStringLiteral("No es una base de elevacion valida (falta la tabla "
                                     "dem_tiles; generala con dem_to_db)."));
            } else {
                const qint64 tiles = q.value(0).toLongLong();
                if (tiles == 0)
                    r.add(Severity::Error, QStringLiteral("elevation"),
                          QStringLiteral("La base de elevacion esta vacia."));
                else
                    r.add(Severity::Info, QStringLiteral("elevation"),
                          QStringLiteral("%1 tiles de 1x1 grado").arg(tiles));
            }
        }
    } else if (!p.elevationDir.isEmpty()) {
        const QStringList hgt = QDir(p.elevationDir).entryList(
            {QStringLiteral("*.hgt"), QStringLiteral("*.HGT")}, QDir::Files);
        if (hgt.isEmpty())
            r.add(Severity::Error, QStringLiteral("elevation"),
                  QStringLiteral("La carpeta %1 no tiene ficheros .hgt")
                      .arg(QDir::toNativeSeparators(p.elevationDir)));
        else
            r.add(Severity::Info, QStringLiteral("elevation"),
                  QStringLiteral("%1 ficheros .hgt").arg(hgt.size()));
        r.totalBytes += sizeOf(p.elevationDir);
    }

    // --- capas fijas ----------------------------------------------------
    for (const DataPackage::Overlay &ov : p.overlays) {
        if (!QFileInfo::exists(ov.file)) {
            r.add(Severity::Error, ov.id,
                  QStringLiteral("No existe %1").arg(QDir::toNativeSeparators(ov.file)));
            continue;
        }
        r.totalBytes += sizeOf(ov.file);
        QString motivo;
        const QVector<GeoPath> trazados = readGeoFile(ov.file, &motivo);
        if (trazados.isEmpty())
            r.add(Severity::Error, ov.id, QStringLiteral("No se puede leer: %1").arg(motivo));
        else
            r.add(Severity::Info, ov.id,
                  QStringLiteral("%1 trazado(s)").arg(trazados.size()));
    }

    // --- entidades de partida -------------------------------------------
    if (!p.featuresSeed.isEmpty()) {
        if (QFileInfo::exists(p.featuresSeed))
            r.totalBytes += sizeOf(p.featuresSeed);
        else
            r.add(Severity::Warning, QStringLiteral("features"),
                  QStringLiteral("No existe la BD de partida %1: el usuario empezara "
                                 "sin entidades.").arg(QDir::toNativeSeparators(p.featuresSeed)));
    }

    return r;
}

} // namespace libmapa
