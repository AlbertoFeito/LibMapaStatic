#include "tiles/TileDataset.h"

#include "tiles/MBTiles.h"

#include <QJsonArray>

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
    if (mbtiles)
        o[QStringLiteral("format")] = QStringLiteral("mbtiles");
    if (vector)
        o[QStringLiteral("vector")] = true;
    if (!vectorStyleFile.isEmpty())
        o[QStringLiteral("vectorStyle")] = vectorStyleFile;
    if (!vectorLandMask.isEmpty())
        o[QStringLiteral("landMask")] = vectorLandMask;
    if (!vectorLandLayer.isEmpty())
        o[QStringLiteral("landLayer")] = vectorLandLayer;
    if (!vectorHiddenGroups.isEmpty())
        o[QStringLiteral("hiddenGroups")] = QJsonArray::fromStringList(vectorHiddenGroups);
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

    // Un MBTiles (por "format" o por la extension) parte de su propia disposicion
    // de columnas; lo que el JSON diga a mano manda sobre ella. Para una base
    // RMaps los valores iniciales son los de siempre, asi que nada cambia.
    if (o.value(QStringLiteral("format")).toString().compare(QLatin1String("mbtiles"),
                                                             Qt::CaseInsensitive) == 0
        || MBTiles::isMBTilesPath(d.filePath))
        MBTiles::applyLayout(d);

    d.tableName   = o.value(QStringLiteral("tableName")).toString(d.tableName);
    d.zFactor     = o.value(QStringLiteral("zFactor")).toInt(d.zFactor);
    d.zOffset     = o.value(QStringLiteral("zOffset")).toInt(d.zOffset);
    d.minZoom     = o.value(QStringLiteral("minZoom")).toInt(0);
    d.baseZoom    = o.value(QStringLiteral("baseZoom")).toInt(d.minZoom);
    d.maxZoom     = o.value(QStringLiteral("maxZoom")).toInt(17);
    d.recommendedMaxZoom =
        o.value(QStringLiteral("recommendedMaxZoom")).toInt(d.maxZoom);
    d.typicalFill = o.value(QStringLiteral("typicalFill")).toDouble(1.0);
    if (o.contains(QStringLiteral("scheme")))
        d.scheme  = tileSchemeFromString(o.value(QStringLiteral("scheme")).toString());
    d.sValue      = o.value(QStringLiteral("sValue")).toInt(0);
    d.tileSize    = o.value(QStringLiteral("tileSize")).toInt(256);
    d.colZ        = o.value(QStringLiteral("colZ")).toString(d.colZ);
    d.colX        = o.value(QStringLiteral("colX")).toString(d.colX);
    d.colY        = o.value(QStringLiteral("colY")).toString(d.colY);
    d.colS        = o.value(QStringLiteral("colS")).toString(d.colS);
    d.colImage    = o.value(QStringLiteral("colImage")).toString(d.colImage);
    d.hasSColumn  = o.value(QStringLiteral("hasSColumn")).toBool(d.hasSColumn);
    d.vector      = o.value(QStringLiteral("vector")).toBool(false);
    d.vectorStyleFile = o.value(QStringLiteral("vectorStyle")).toString();
    d.vectorLandMask  = o.value(QStringLiteral("landMask")).toString();
    d.vectorLandLayer = o.value(QStringLiteral("landLayer")).toString();
    for (const QJsonValue &g : o.value(QStringLiteral("hiddenGroups")).toArray())
        if (!g.toString().isEmpty())
            d.vectorHiddenGroups << g.toString();
    return d;
}

} // namespace libmapa
