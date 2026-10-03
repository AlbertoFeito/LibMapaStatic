#include "io/DataPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace libmapa {

namespace {

// Ultima version del manifiesto que esta libreria sabe leer. Un paquete mas
// nuevo se rechaza con un mensaje claro en vez de abrirse a medias.
constexpr int kMaxManifestVersion = 2;

// Resuelve una ruta del manifiesto: las relativas, contra la carpeta del
// paquete; las absolutas se respetan. Vacio sigue vacio.
QString resolve(const QDir &base, const QString &path)
{
    if (path.isEmpty())
        return path;
    return QDir::cleanPath(QFileInfo(path).isRelative() ? base.absoluteFilePath(path)
                                                        : path);
}

// Lee un color "#rrggbb" o "#aarrggbb" (el alfa va DELANTE, como en Qt). Si la
// clave falta o el texto no es un color, deja el valor por defecto.
QColor readColor(const QJsonObject &o, const QString &key, const QColor &fallback)
{
    const QString text = o.value(key).toString();
    if (text.isEmpty())
        return fallback;
    const QColor c(text);
    return c.isValid() ? c : fallback;
}

} // namespace

// Lee el manifiesto (carpeta o fichero), valida formato y version, resuelve
// todas las rutas contra la carpeta del paquete y anota como AVISO, sin fallar,
// los ficheros que no existan: asi una capa ausente no deja sin mapa al resto.
std::optional<DataPackage> DataPackage::load(const QString &path, QString *error)
{
    auto fallo = [error](const QString &motivo) {
        if (error)
            *error = motivo;
        return std::optional<DataPackage>();
    };

    // Una ruta que no existe y no acaba en .json se trata como carpeta: asi el
    // error dice que falta ".../datos/mapa.json", que es lo que hay que poner.
    const QFileInfo entrada(path);
    const bool esCarpeta = entrada.isDir()
        || (!entrada.exists()
            && entrada.suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) != 0);
    const QString manifiesto = esCarpeta
        ? QDir(entrada.absoluteFilePath()).absoluteFilePath(manifestFileName())
        : entrada.absoluteFilePath();

    QFile file(manifiesto);
    if (!file.open(QIODevice::ReadOnly))
        return fallo(QStringLiteral("No se encuentra el manifiesto %1").arg(manifiesto));

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject())
        return fallo(QStringLiteral("JSON invalido en %1: %2")
                         .arg(manifiesto, parseError.errorString()));
    const QJsonObject root = doc.object();

    // "format" es opcional para que un datasets.json de la version 1 siga
    // valiendo; si viene, tiene que ser el nuestro.
    const QString format = root.value(QStringLiteral("format")).toString();
    if (!format.isEmpty() && format != QLatin1String("libmapa-package"))
        return fallo(QStringLiteral("%1 no es un paquete de libmapa (format \"%2\")")
                         .arg(manifiesto, format));
    const int version = root.value(QStringLiteral("version")).toInt(1);
    if (version > kMaxManifestVersion)
        return fallo(QStringLiteral("%1 es de la version %2 del formato; esta "
                                    "libreria solo entiende hasta la %3")
                         .arg(manifiesto).arg(version).arg(kMaxManifestVersion));

    DataPackage p;
    const QDir base = QFileInfo(manifiesto).absoluteDir();
    p.info.directory = base.absolutePath();
    p.info.manifestPath = manifiesto;

    // --- package --------------------------------------------------------
    const QJsonObject pkg = root.value(QStringLiteral("package")).toObject();
    p.info.id = pkg.value(QStringLiteral("id")).toString();
    if (p.info.id.isEmpty())
        p.info.id = QFileInfo(p.info.directory).fileName().toLower();
    p.info.name = pkg.value(QStringLiteral("name")).toString(p.info.id);
    p.info.dataVersion = pkg.value(QStringLiteral("dataVersion")).toString();
    p.info.created = pkg.value(QStringLiteral("created")).toString();
    p.info.description = pkg.value(QStringLiteral("description")).toString();
    p.info.attribution = pkg.value(QStringLiteral("attribution")).toString();

    const QJsonObject b = pkg.value(QStringLiteral("bounds")).toObject();
    if (b.contains(QStringLiteral("north")) && b.contains(QStringLiteral("west"))
        && b.contains(QStringLiteral("south")) && b.contains(QStringLiteral("east"))) {
        p.info.bounds = QGeoRectangle(
            QGeoCoordinate(b.value(QStringLiteral("north")).toDouble(),
                           b.value(QStringLiteral("west")).toDouble()),
            QGeoCoordinate(b.value(QStringLiteral("south")).toDouble(),
                           b.value(QStringLiteral("east")).toDouble()));
    }

    // --- start ----------------------------------------------------------
    const QJsonObject start = root.value(QStringLiteral("start")).toObject();
    p.startLayer = start.value(QStringLiteral("layer")).toString();
    p.startZoom = start.value(QStringLiteral("zoom")).toInt(-1);
    const QJsonArray centro = start.value(QStringLiteral("center")).toArray();
    if (centro.size() == 2 && centro.at(0).isDouble() && centro.at(1).isDouble())
        p.startCenter = QGeoCoordinate(centro.at(0).toDouble(), centro.at(1).toDouble());

    // --- datasets -------------------------------------------------------
    for (const QJsonValue &v : root.value(QStringLiteral("datasets")).toArray()) {
        TileDataset d = TileDataset::fromJson(v.toObject());
        d.filePath = resolve(base, d.filePath);
        if (!d.isValid()) {
            p.warnings << QStringLiteral("Dataset \"%1\" incompleto: se ignora").arg(d.id);
            continue;
        }
        if (!QFileInfo::exists(d.filePath))
            p.warnings << QStringLiteral("Falta la base de teselas de \"%1\": %2")
                              .arg(d.id, d.filePath);
        p.datasets.append(d);
    }
    if (p.datasets.isEmpty())
        return fallo(QStringLiteral("%1 no contiene datasets validos").arg(manifiesto));

    // --- elevation ------------------------------------------------------
    const QJsonObject elev = root.value(QStringLiteral("elevation")).toObject();
    p.elevationFile = resolve(base, elev.value(QStringLiteral("file")).toString());
    p.elevationDir = resolve(base, elev.value(QStringLiteral("dir")).toString());
    if (!p.elevationFile.isEmpty() && !QFileInfo::exists(p.elevationFile))
        p.warnings << QStringLiteral("Falta la base de elevacion: %1").arg(p.elevationFile);
    if (!p.elevationDir.isEmpty() && !QFileInfo(p.elevationDir).isDir())
        p.warnings << QStringLiteral("Falta la carpeta de elevacion: %1").arg(p.elevationDir);

    // --- overlays -------------------------------------------------------
    for (const QJsonValue &v : root.value(QStringLiteral("overlays")).toArray()) {
        const QJsonObject o = v.toObject();
        Overlay ov;
        ov.id = o.value(QStringLiteral("id")).toString();
        ov.file = resolve(base, o.value(QStringLiteral("file")).toString());
        if (ov.id.isEmpty() || ov.file.isEmpty()) {
            p.warnings << QStringLiteral("Capa fija sin \"id\" o sin \"file\": se ignora");
            continue;
        }
        ov.name = o.value(QStringLiteral("name")).toString(ov.id);
        ov.zOrder = o.value(QStringLiteral("zOrder")).toInt(0);

        const QJsonObject st = o.value(QStringLiteral("style")).toObject();
        ov.style.lineColor = readColor(st, QStringLiteral("lineColor"), ov.style.lineColor);
        ov.style.fillColor = readColor(st, QStringLiteral("fillColor"), ov.style.fillColor);
        ov.style.lineWidth = st.value(QStringLiteral("lineWidth")).toDouble(ov.style.lineWidth);
        ov.style.labelVisible = st.value(QStringLiteral("labelVisible")).toBool(false);

        if (!QFileInfo::exists(ov.file))
            p.warnings << QStringLiteral("Falta la capa fija \"%1\": %2").arg(ov.id, ov.file);
        p.overlays.append(ov);
    }

    // --- features -------------------------------------------------------
    const QJsonObject feats = root.value(QStringLiteral("features")).toObject();
    p.featuresFile = feats.value(QStringLiteral("file")).toString();
    p.featuresSeed = resolve(base, feats.value(QStringLiteral("seed")).toString());
    if (!p.featuresSeed.isEmpty() && !QFileInfo::exists(p.featuresSeed))
        p.warnings << QStringLiteral("Faltan las entidades de partida: %1").arg(p.featuresSeed);

    return p;
}

// Ficheros del paquete, sin repetir y solo los que existen. La carpeta de .hgt
// se expande a sus ficheros: copiar la carpeta entera podria arrastrar otros.
QStringList DataPackage::files() const
{
    QStringList out;
    auto anadir = [&out](const QString &f) {
        if (!f.isEmpty() && QFileInfo(f).isFile() && !out.contains(f))
            out << f;
    };
    anadir(info.manifestPath);
    for (const TileDataset &d : datasets)
        anadir(d.filePath);
    anadir(elevationFile);
    if (!elevationDir.isEmpty())
        for (const QFileInfo &f : QDir(elevationDir).entryInfoList(
                 {QStringLiteral("*.hgt"), QStringLiteral("*.HGT")}, QDir::Files))
            anadir(f.absoluteFilePath());
    for (const Overlay &ov : overlays)
        anadir(ov.file);
    anadir(featuresSeed);
    return out;
}

// Ruta escribible de la BD de entidades: relativa -> carpeta de datos de la app
// separada por paquete (para que dos paquetes no compartan entidades); absoluta
// -> tal cual. La carpeta del paquete NO se usa: puede ser de solo lectura.
QString DataPackage::resolveFeaturesPath() const
{
    if (featuresFile.isEmpty())
        return QString();
    if (!QFileInfo(featuresFile).isRelative())
        return QDir::cleanPath(featuresFile);

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(appData).absoluteFilePath(info.id + QLatin1Char('/') + featuresFile);
}

// Crea la carpeta de la BD de entidades y, la PRIMERA vez (si aun no existe),
// copia la BD de partida del paquete. La copia hereda el atributo de solo
// lectura del original (habitual si el paquete esta instalado), asi que se le
// devuelven los permisos de escritura: si no, el guardado automatico fallaria.
QString DataPackage::prepareFeaturesFile(QString *error) const
{
    const QString ruta = resolveFeaturesPath();
    if (ruta.isEmpty())
        return ruta;

    const QFileInfo fi(ruta);
    if (!QDir().mkpath(fi.absolutePath())) {
        if (error)
            *error = QStringLiteral("No se pudo crear la carpeta %1").arg(fi.absolutePath());
        return QString();
    }

    if (!fi.exists() && !featuresSeed.isEmpty() && QFileInfo::exists(featuresSeed)) {
        if (QFile::copy(featuresSeed, ruta))
            QFile::setPermissions(ruta, QFile::permissions(ruta)
                                            | QFileDevice::WriteOwner
                                            | QFileDevice::WriteUser);
        else if (error)
            *error = QStringLiteral("No se pudo copiar %1 a %2").arg(featuresSeed, ruta);
    }
    return ruta;
}

} // namespace libmapa
