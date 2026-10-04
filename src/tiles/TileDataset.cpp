#include "tiles/TileDataset.h"

namespace libmapa {

// Enum de esquema -> texto para datasets.json ("XYZ" / "TMS").
QString tileSchemeToString(TileScheme s)
{
    return s == TileScheme::TMS ? QStringLiteral("TMS") : QStringLiteral("XYZ");
}

// Texto -> enum de esquema (sin distinguir mayusculas). *ok=false si no encaja;
// por defecto cae a XYZ (el mas comun).
TileScheme tileSchemeFromString(const QString &s, bool *ok)
{
    if (ok) *ok = true;
    if (s.compare(QLatin1String("TMS"), Qt::CaseInsensitive) == 0)
        return TileScheme::TMS;
    if (s.compare(QLatin1String("XYZ"), Qt::CaseInsensitive) == 0)
        return TileScheme::XYZ;
    if (ok) *ok = false;
    return TileScheme::XYZ;
}

// Serializa el descriptor completo a un objeto JSON (una entrada del array
// "datasets"). Lo usan probe_db y geo_to_tiles al imprimir la configuracion.
QJsonObject TileDataset::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")]          = id;
    o[QStringLiteral("displayName")] = displayName;
    o[QStringLiteral("filePath")]    = filePath;
    o[QStringLiteral("tableName")]   = tableName;
    o[QStringLiteral("zFactor")]     = zFactor;
    o[QStringLiteral("zOffset")]     = zOffset;
    o[QStringLiteral("baseZoom")]    = baseZoom;
    o[QStringLiteral("minZoom")]     = minZoom;
    o[QStringLiteral("maxZoom")]     = maxZoom;
    o[QStringLiteral("recommendedMaxZoom")] = recommendedMaxZoom;
    o[QStringLiteral("typicalFill")] = typicalFill;
    o[QStringLiteral("scheme")]      = tileSchemeToString(scheme);
    o[QStringLiteral("sValue")]      = sValue;
    o[QStringLiteral("tileSize")]    = tileSize;
    o[QStringLiteral("colZ")]        = colZ;
    o[QStringLiteral("colX")]        = colX;
    o[QStringLiteral("colY")]        = colY;
    o[QStringLiteral("colS")]        = colS;
    o[QStringLiteral("colImage")]    = colImage;
    o[QStringLiteral("hasSColumn")]  = hasSColumn;
    return o;
}

// Reconstruye un TileDataset desde una entrada de datasets.json. Cada campo
// tiene un valor por defecto sensato, de modo que un JSON minimo tambien vale.
TileDataset TileDataset::fromJson(const QJsonObject &o)
{
    TileDataset d;
    d.id          = o.value(QStringLiteral("id")).toString();
    d.displayName = o.value(QStringLiteral("displayName")).toString();
    d.filePath    = o.value(QStringLiteral("filePath")).toString();
    d.tableName   = o.value(QStringLiteral("tableName")).toString(QStringLiteral("tiles"));
    d.zFactor     = o.value(QStringLiteral("zFactor")).toInt(1);
    d.zOffset     = o.value(QStringLiteral("zOffset")).toInt(0);
    d.minZoom     = o.value(QStringLiteral("minZoom")).toInt(0);
    d.baseZoom    = o.value(QStringLiteral("baseZoom")).toInt(d.minZoom);
    d.maxZoom     = o.value(QStringLiteral("maxZoom")).toInt(17);
    d.recommendedMaxZoom =
        o.value(QStringLiteral("recommendedMaxZoom")).toInt(d.maxZoom);
    d.typicalFill = o.value(QStringLiteral("typicalFill")).toDouble(1.0);
    d.scheme      = tileSchemeFromString(o.value(QStringLiteral("scheme")).toString());
    d.sValue      = o.value(QStringLiteral("sValue")).toInt(0);
    d.tileSize    = o.value(QStringLiteral("tileSize")).toInt(256);
    d.colZ        = o.value(QStringLiteral("colZ")).toString(QStringLiteral("z"));
    d.colX        = o.value(QStringLiteral("colX")).toString(QStringLiteral("x"));
    d.colY        = o.value(QStringLiteral("colY")).toString(QStringLiteral("y"));
    d.colS        = o.value(QStringLiteral("colS")).toString(QStringLiteral("s"));
    d.colImage    = o.value(QStringLiteral("colImage")).toString(QStringLiteral("image"));
    d.hasSColumn  = o.value(QStringLiteral("hasSColumn")).toBool(true);
    return d;
}

} // namespace libmapa
