#include "tiles/TileDatasetProbe.h"

#include "core/Logging.h"
#include "db/SqliteConnectionPool.h"
#include "geo/TileMatrix.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que
// hay que pedirlo explicitamente: sin esto, bindValue() no compila en
// Qt 5.14 porque QVariant es un tipo incompleto y no hay conversion
// posible desde int, QString ni QByteArray.
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <climits>
#include <algorithm>
#include <cmath>

namespace libmapa {

namespace {

//! Solapamiento de dos intervalos [a1,a2] y [b1,b2]. 0 si no se tocan.
double overlap(double a1, double a2, double b1, double b2)
{
    if (a1 > a2) std::swap(a1, a2);
    if (b1 > b2) std::swap(b1, b2);
    return std::max(0.0, std::min(a2, b2) - std::max(a1, b1));
}

} // namespace

bool TileDatasetProbe::detectColumns(QSqlDatabase &db, TileDataset &ds,
                                     QStringList &warnings)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT * FROM %1 LIMIT 1").arg(ds.tableName))) {
        qCCritical(lcMapaTiles) << "No existe la tabla" << ds.tableName
                                << "-" << q.lastError().text();
        return false;
    }

    const QSqlRecord rec = q.record();
    QStringList cols;
    for (int i = 0; i < rec.count(); ++i)
        cols << rec.fieldName(i);

    auto pick = [&cols](std::initializer_list<const char *> candidates,
                        QString *out) -> bool {
        for (const char *c : candidates) {
            for (const QString &col : cols) {
                if (col.compare(QLatin1String(c), Qt::CaseInsensitive) == 0) {
                    *out = col;
                    return true;
                }
            }
        }
        return false;
    };

    if (!pick({"z", "zoom_level", "zoom"}, &ds.colZ)
        || !pick({"x", "tile_column"}, &ds.colX)
        || !pick({"y", "tile_row"}, &ds.colY)
        || !pick({"image", "tile_data", "img"}, &ds.colImage)) {
        qCCritical(lcMapaTiles) << "Columnas no reconocidas en" << ds.tableName
                                << ":" << cols;
        return false;
    }

    ds.hasSColumn = pick({"s"}, &ds.colS);
    if (!ds.hasSColumn)
        warnings << QStringLiteral("La tabla no tiene columna 's'; "
                                   "se omite del WHERE.");
    return true;
}

namespace {

//! Menor b tal que 2^b > maxIndex. Es decir, cuantos niveles de rejilla hacen
//! falta para que un indice de tesela quepa en el mundo.
int bitsNeededFor(int maxIndex)
{
    int b = 0;
    while ((1 << b) <= maxIndex && b < 31)
        ++b;
    return b;
}

} // namespace

bool TileDatasetProbe::detectZMapping(QSqlDatabase &db, TileDataset &ds,
                                      ProbeResult &result,
                                      const QGeoRectangle &reference)
{
    QSqlQuery q(db);

    // Niveles presentes y cuantas teselas hay en cada uno.
    if (!q.exec(QStringLiteral("SELECT %1, COUNT(*), MIN(%2), MAX(%2), "
                               "MIN(%3), MAX(%3) FROM %4 GROUP BY %1 ORDER BY %1")
                    .arg(ds.colZ, ds.colX, ds.colY, ds.tableName))) {
        qCCritical(lcMapaTiles) << "Consulta de niveles fallo:"
                                << q.lastError().text();
        return false;
    }

    QVector<ZoomLevelStats> raw;
    while (q.next()) {
        ZoomLevelStats s;
        s.storedZ   = q.value(0).toInt();
        s.tileCount = q.value(1).toLongLong();
        s.xMin      = q.value(2).toInt();
        s.xMax      = q.value(3).toInt();
        s.yMin      = q.value(4).toInt();
        s.yMax      = q.value(5).toInt();
        raw.append(s);
        result.totalTiles += s.tileCount;
    }

    if (raw.isEmpty()) {
        qCCritical(lcMapaTiles) << "La tabla" << ds.tableName << "esta vacia";
        return false;
    }

    const int zMin = raw.first().storedZ;
    const int zMax = raw.last().storedZ;

    // Criterio de inversion: la anchura del mundo en teselas es 2^z, asi que
    // en un esquema DIRECTO el nivel de z mayor tiene mas teselas que el de z
    // menor. Si ocurre lo contrario, z esta invertido (es un "nivel de
    // reduccion", como el "18 - Zoom_Level" del codigo satelital original).
    bool inverted = false;
    if (raw.size() >= 2) {
        const qint64 atLow  = raw.first().tileCount;
        const qint64 atHigh = raw.last().tileCount;
        inverted = (atHigh < atLow);
        result.zMappingReason =
            QStringLiteral("z=%1 tiene %2 teselas y z=%3 tiene %4; "
                           "el nivel de mas detalle es z=%5 -> %6")
                .arg(zMin).arg(atLow).arg(zMax).arg(atHigh)
                .arg(inverted ? zMin : zMax)
                .arg(inverted ? QStringLiteral("z INVERTIDO")
                              : QStringLiteral("z directo"));
    } else {
        result.zMappingReason =
            QStringLiteral("Solo hay un nivel (z=%1); se asume z directo").arg(zMin);
        result.warnings << QStringLiteral("Un solo nivel de zoom: la deteccion "
                                          "de inversion no es concluyente.");
    }

    // -------------------------------------------------------------------
    // Determinar el OFFSET ABSOLUTO, en AMBAS direcciones.
    //
    // No basta con saber si z esta invertido: hay que saber respecto a que
    // constante, porque los indices x/y de la tabla estan siempre en la
    // rejilla del zoom VERDADERO (2^zVerdadero teselas por lado), y la
    // columna z puede llevar cualquier desplazamiento.
    //
    // Los dos casos reales del proyecto lo demuestran:
    //   Cuba_Satelital : storedZ = 17 - zVerdadero   (zFactor -1, offset 17)
    //   Cuba_OSM       : storedZ = zVerdadero + 1    (zFactor +1, offset  1)
    //
    // La primera version de esta funcion solo buscaba el offset cuando z
    // estaba invertido y daba por hecho offset 0 en el caso directo. Con la
    // BD de OSM real eso situaba el mapa en el norte de Canada. El aviso de
    // solapamiento lo delato, pero el descriptor salia mal.
    //
    // El offset se acota primero por geometria (los indices tienen que caber
    // en 2^zLogico) y se afina, si hay referencia, por LONGITUD, que es
    // independiente del esquema XYZ/TMS y por tanto se puede decidir antes.
    // -------------------------------------------------------------------
    auto logicalFor = [](int storedZ, bool inv, int offset) {
        return inv ? (offset - storedZ) : (storedZ - offset);
    };

    auto fitsWorld = [&raw, &logicalFor](bool inv, int offset) {
        for (const ZoomLevelStats &s : raw) {
            const int logical = logicalFor(s.storedZ, inv, offset);
            if (logical < 0 || logical > 30) return false;
            const int n = TileMatrix::tilesPerSide(logical);
            if (s.xMax >= n || s.yMax >= n) return false;
        }
        return true;
    };

    // Cota geometrica. Para cada nivel hace falta
    //     2^logicalZ > max(xMax, yMax)   =>   logicalZ >= bits
    // que se traduce en un limite sobre el offset segun la direccion.
    int offsetLo = 0, offsetHi = 0;
    {
        bool first = true;
        for (const ZoomLevelStats &s : raw) {
            const int bits = bitsNeededFor(std::max(s.xMax, s.yMax));
            if (inverted) {
                // logicalZ = offset - storedZ >= bits  =>  offset >= storedZ+bits
                const int lo = s.storedZ + bits;
                offsetLo = first ? lo : std::max(offsetLo, lo);
            } else {
                // logicalZ = storedZ - offset >= bits  =>  offset <= storedZ-bits
                const int hi = s.storedZ - bits;
                offsetHi = first ? hi : std::min(offsetHi, hi);
            }
            first = false;
        }
        if (inverted) offsetHi = offsetLo + 12;
        else          offsetLo = offsetHi - 12;
    }

    int bestOffset = INT_MIN;
    double bestScore = -1.0;
    const bool haveRef = reference.isValid();

    for (int c = offsetLo; c <= offsetHi; ++c) {
        if (!fitsWorld(inverted, c))
            continue;

        if (!haveRef) {
            // Sin referencia no se puede decidir con datos. Se toma el
            // extremo que impone la geometria: el ajuste mas apretado.
            if (bestOffset == INT_MIN)
                bestOffset = inverted ? c : c;
            if (!inverted)
                bestOffset = std::max(bestOffset, c);
            continue;
        }

        const double refW = reference.topLeft().longitude();
        const double refE = reference.bottomRight().longitude();

        double score = 0.0;
        for (const ZoomLevelStats &s : raw) {
            const int lz = logicalFor(s.storedZ, inverted, c);
            const double lonW = TileMatrix::tileXToLongitude(s.xMin, lz);
            const double lonE = TileMatrix::tileXToLongitude(s.xMax + 1, lz);
            const double ov = overlap(lonW, lonE, refW, refE);
            const double span = std::abs(lonE - lonW);
            // Se premia el solape y se penaliza abarcar de mas, para que no
            // gane un offset que cubra medio planeta y "contenga" la
            // referencia por pura anchura.
            score += (span > 0.0) ? (ov / span) * ov : 0.0;
        }

        if (score > bestScore) {
            bestScore = score;
            bestOffset = c;
        }
    }

    if (bestOffset == INT_MIN) {
        bestOffset = inverted ? offsetLo : offsetHi;
        result.warnings << QStringLiteral(
            "No se encontro un offset de zoom coherente; se usa %1.")
            .arg(bestOffset);
    }

    ds.zFactor = inverted ? -1 : 1;
    ds.zOffset = bestOffset;
    if (inverted) {
        ds.minZoom = bestOffset - zMax;
        ds.maxZoom = bestOffset - zMin;
    } else {
        ds.minZoom = zMin - bestOffset;
        ds.maxZoom = zMax - bestOffset;
    }

    result.zMappingReason += QStringLiteral(
        "; storedZ = %1*logicalZ %2 %3 (%4)")
        .arg(ds.zFactor)
        .arg(bestOffset >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
        .arg(std::abs(bestOffset))
        .arg(haveRef ? QStringLiteral("offset elegido por solape en longitud")
                     : QStringLiteral("offset por cota geometrica, SIN verificar"));

    if (!haveRef) {
        result.warnings << QStringLiteral(
            "Offset de zoom no verificado con datos. Pasar --ref-bbox.");
    }

    // Rellenar el zoom logico y ordenar por detalle creciente.
    for (ZoomLevelStats &s : raw)
        s.logicalZ = ds.logicalZ(s.storedZ);
    std::sort(raw.begin(), raw.end(),
              [](const ZoomLevelStats &a, const ZoomLevelStats &b) {
                  return a.logicalZ < b.logicalZ;
              });
    // Ratio de relleno: cuantas teselas hay frente a las que cabrian en el
    // rectangulo. Un valor bajo significa huecos (tipico en costas: el mar
    // no se descarga). El motor de render tiene que tolerarlo.
    for (ZoomLevelStats &s : raw) {
        const qint64 box = static_cast<qint64>(s.xMax - s.xMin + 1)
                           * static_cast<qint64>(s.yMax - s.yMin + 1);
        s.fillRatio = box > 0 ? static_cast<double>(s.tileCount)
                                    / static_cast<double>(box)
                              : 0.0;
    }
    result.levels = raw;

    // Detectar huecos en la secuencia de niveles.
    for (int i = 1; i < raw.size(); ++i) {
        if (raw[i].logicalZ != raw[i - 1].logicalZ + 1) {
            result.warnings << QStringLiteral("Salto entre los niveles logicos "
                                              "%1 y %2.")
                                   .arg(raw[i - 1].logicalZ).arg(raw[i].logicalZ);
        }
    }

    // -------------------------------------------------------------------
    // Columna 's': hay que VERIFICAR que sirve como filtro, no suponerlo.
    //
    // En la BD de OSM real, 's' vale 2686452, que no parece un discriminador
    // sino un dato colado en esa columna. Si el valor no fuese constante,
    // meterlo en el WHERE haria desaparecer teselas en silencio: el mapa
    // saldria con huecos y nadie sabria por que.
    //
    // Asi que se comparan los recuentos con y sin el filtro, y solo se usa
    // si no pierde ni una fila.
    // -------------------------------------------------------------------
    if (ds.hasSColumn) {
        QSqlQuery qs(db);
        if (qs.exec(QStringLiteral("SELECT DISTINCT %1 FROM %2 LIMIT 5")
                        .arg(ds.colS, ds.tableName))) {
            QList<int> values;
            while (qs.next())
                values << qs.value(0).toInt();

            if (values.isEmpty()) {
                ds.hasSColumn = false;
            } else {
                ds.sValue = values.first();
                if (values.size() > 1) {
                    result.warnings
                        << QStringLiteral("La columna 's' toma al menos %1 "
                                          "valores distintos.")
                               .arg(values.size());
                }
            }
        }

        if (ds.hasSColumn && !raw.isEmpty()) {
            // Se prueba sobre el nivel mas poblado, el mas representativo.
            const ZoomLevelStats *densest = &raw.first();
            for (const ZoomLevelStats &s : raw)
                if (s.tileCount > densest->tileCount)
                    densest = &s;

            qint64 withoutFilter = -1, withFilter = -1;
            QSqlQuery qa(db), qb(db);

            qa.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 = :z")
                           .arg(ds.tableName, ds.colZ));
            qa.bindValue(QStringLiteral(":z"), densest->storedZ);
            if (qa.exec() && qa.next())
                withoutFilter = qa.value(0).toLongLong();

            qb.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 "
                                      "WHERE %2 = :z AND %3 = :s")
                           .arg(ds.tableName, ds.colZ, ds.colS));
            qb.bindValue(QStringLiteral(":z"), densest->storedZ);
            qb.bindValue(QStringLiteral(":s"), ds.sValue);
            if (qb.exec() && qb.next())
                withFilter = qb.value(0).toLongLong();

            if (withoutFilter >= 0 && withFilter >= 0) {
                result.sFilterVerified = true;
                result.sFilterKeeps = withFilter;
                result.sFilterTotal = withoutFilter;

                if (withFilter < withoutFilter) {
                    ds.hasSColumn = false;   // el filtro perderia teselas
                    result.warnings << QStringLiteral(
                        "Filtrar por '%1' = %2 perderia %3 de %4 teselas en el "
                        "nivel z=%5. Se DESACTIVA el filtro por esa columna.")
                        .arg(ds.colS).arg(ds.sValue)
                        .arg(withoutFilter - withFilter)
                        .arg(withoutFilter).arg(densest->storedZ);
                }
            }
        }
    }

    return true;
}

void TileDatasetProbe::detectScheme(QSqlDatabase &db, TileDataset &ds,
                                    ProbeResult &result,
                                    const QGeoRectangle &reference)
{
    Q_UNUSED(db)

    if (result.levels.isEmpty())
        return;

    // -------------------------------------------------------------------
    // 1. Esquema del eje Y: VOTACION PONDERADA entre todos los niveles.
    //
    // La version anterior decidia mirando solo el nivel mas detallado, y en
    // las dos BD reales ese nivel es el menos representativo: en la de OSM,
    // z=16 son 23407 teselas en una franja de 0.099 grados de longitud
    // situada en mar abierto al oeste de Cuba. Basar una decision global en
    // ese nivel es fragil. Ahora vota cada nivel, con peso proporcional a su
    // numero de teselas.
    // -------------------------------------------------------------------
    if (!reference.isValid()) {
        ds.scheme = TileScheme::XYZ;
        result.schemeReason = QStringLiteral(
            "Sin extension de referencia: se asume XYZ, sin verificar.");
        result.warnings << QStringLiteral(
            "Esquema XYZ/TMS no verificado. Pasar --ref-bbox para decidirlo.");
    } else {
        const double refS = reference.bottomRight().latitude();
        const double refN = reference.topLeft().latitude();

        double voteXyz = 0.0, voteTms = 0.0;
        for (const ZoomLevelStats &l : result.levels) {
            const int z = l.logicalZ;
            const int n = TileMatrix::tilesPerSide(z);
            const double w = static_cast<double>(l.tileCount);

            const double nXyz = TileMatrix::tileYToLatitude(l.yMin, z);
            const double sXyz = TileMatrix::tileYToLatitude(l.yMax + 1, z);

            const double nTms = TileMatrix::tileYToLatitude(n - 1 - l.yMax, z);
            const double sTms = TileMatrix::tileYToLatitude(n - l.yMin, z);

            const double spanXyz = std::abs(nXyz - sXyz);
            const double spanTms = std::abs(nTms - sTms);

            if (spanXyz > 0.0)
                voteXyz += w * overlap(sXyz, nXyz, refS, refN) / spanXyz;
            if (spanTms > 0.0)
                voteTms += w * overlap(sTms, nTms, refS, refN) / spanTms;
        }

        ds.scheme = (voteTms > voteXyz) ? TileScheme::TMS : TileScheme::XYZ;
        result.schemeReason = QStringLiteral(
            "Votacion ponderada por teselas sobre %1 niveles: "
            "XYZ=%2, TMS=%3 -> %4")
            .arg(result.levels.size())
            .arg(voteXyz, 0, 'f', 0).arg(voteTms, 0, 'f', 0)
            .arg(tileSchemeToString(ds.scheme));

        if (voteXyz <= 0.0 && voteTms <= 0.0) {
            result.warnings << QStringLiteral(
                "Ningun nivel solapa con la extension de referencia en "
                "latitud. Revisar --ref-bbox o el mapeo de zoom.");
        }
    }

    // -------------------------------------------------------------------
    // 2. Extension geografica de cada nivel, ya con el esquema decidido.
    // -------------------------------------------------------------------
    for (ZoomLevelStats &l : result.levels) {
        const int z = l.logicalZ;
        const int n = TileMatrix::tilesPerSide(z);

        l.lonWest = TileMatrix::tileXToLongitude(l.xMin, z);
        l.lonEast = TileMatrix::tileXToLongitude(l.xMax + 1, z);

        if (ds.scheme == TileScheme::TMS) {
            l.latNorth = TileMatrix::tileYToLatitude(n - 1 - l.yMax, z);
            l.latSouth = TileMatrix::tileYToLatitude(n - l.yMin, z);
        } else {
            l.latNorth = TileMatrix::tileYToLatitude(l.yMin, z);
            l.latSouth = TileMatrix::tileYToLatitude(l.yMax + 1, z);
        }
    }

    // -------------------------------------------------------------------
    // 3. Nivel de REFERENCIA: el que mas teselas tiene.
    //
    // Es el que mejor describe que cubre realmente la BD. Tomarlo del nivel
    // mas profundo daba extensiones falsas: 0.099 grados de ancho en la BD
    // de OSM y 7.8 en vez de 10.9 en la satelital.
    // -------------------------------------------------------------------
    int refIdx = 0;
    for (int i = 1; i < result.levels.size(); ++i)
        if (result.levels[i].tileCount > result.levels[refIdx].tileCount)
            refIdx = i;

    result.referenceLevel = refIdx;
    const ZoomLevelStats &ref = result.levels.at(refIdx);

    result.coverage = QGeoRectangle(QGeoCoordinate(ref.latNorth, ref.lonWest),
                                    QGeoCoordinate(ref.latSouth, ref.lonEast));

    // Relleno tipico: el del nivel de referencia. Es lo que decide cuanta
    // escalera de respaldo necesita esta BD en tiempo de ejecucion.
    ds.typicalFill = ref.fillRatio;

    // -------------------------------------------------------------------
    // Nivel de fondo garantizado: el MAS GRUESO que este completo y que
    // abarque al menos la extension de referencia.
    //
    // Es el unico que sirve sobre mar abierto, porque estas BD solo guardan
    // teselas donde hay tierra salvo en los niveles muy gruesos. En la
    // satelital, z1..z5 cubren el mundo entero; de z6 en adelante, solo Cuba.
    // -------------------------------------------------------------------
    ds.baseZoom = ds.minZoom;
    for (const ZoomLevelStats &l : result.levels) {
        if (l.fillRatio < 0.99)
            continue;
        const bool cubreLon = l.lonWest <= ref.lonWest + 1e-6
                              && l.lonEast >= ref.lonEast - 1e-6;
        const bool cubreLat = l.latNorth >= ref.latNorth - 1e-6
                              && l.latSouth <= ref.latSouth + 1e-6;
        if (cubreLon && cubreLat) {
            ds.baseZoom = l.logicalZ;    // el primero que cumple es el mas grueso
            break;
        }
    }

    // -------------------------------------------------------------------
    // 4. Niveles anomalos y zoom maximo recomendado.
    //
    // Un nivel que cubre una fraccion minima del area de referencia deja la
    // pantalla resolviendose casi entera por respaldo de tesela padre. Se
    // detecta y se recorta el zoom que la interfaz deberia ofrecer.
    // -------------------------------------------------------------------
    constexpr double kMinSpanFraction = 0.25;

    ds.recommendedMaxZoom = ds.maxZoom;
    for (int i = static_cast<int>(result.levels.size()) - 1; i >= 0; --i) {
        const ZoomLevelStats &l = result.levels.at(i);
        if (l.logicalZ <= ref.logicalZ)
            break;                      // por debajo de la referencia no se toca

        const double fracLon = ref.lonSpan() > 0.0 ? l.lonSpan() / ref.lonSpan() : 1.0;
        const double fracLat = ref.latSpan() > 0.0 ? l.latSpan() / ref.latSpan() : 1.0;

        // Segundo criterio: densidad relativa. Un nivel puede abarcar toda la
        // isla y aun asi ser inservible si dentro de ese rectangulo apenas
        // hay teselas. Es el caso de la BD satelital, que al nivel 18 llega
        // al 0.23% de relleno frente al 32% del nivel de referencia: casi
        // toda la pantalla se resolveria por respaldo de tesela padre.
        const double fracFill = ref.fillRatio > 0.0 ? l.fillRatio / ref.fillRatio : 1.0;

        const bool sliver = (fracLon < kMinSpanFraction || fracLat < kMinSpanFraction);
        const bool sparse = (fracFill < kMinSpanFraction);

        if (sliver || sparse) {
            ds.recommendedMaxZoom = l.logicalZ - 1;
            result.warnings << QStringLiteral(
                "Nivel %1 (%2 teselas): cubre el %3 % en longitud y el %4 % en "
                "latitud del nivel de referencia %5, con una densidad del "
                "%6 % de la suya (%7). Zoom maximo recomendado: %8.")
                .arg(l.logicalZ).arg(l.tileCount)
                .arg(fracLon * 100.0, 0, 'f', 1)
                .arg(fracLat * 100.0, 0, 'f', 1)
                .arg(ref.logicalZ)
                .arg(fracFill * 100.0, 0, 'f', 1)
                .arg(sliver ? QStringLiteral("franja estrecha")
                            : QStringLiteral("demasiado disperso"))
                .arg(ds.recommendedMaxZoom);
        }
    }
}

void TileDatasetProbe::detectTileSize(QSqlDatabase &db, TileDataset &ds,
                                      ProbeResult &result)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM %2 WHERE %1 IS NOT NULL LIMIT 1")
                    .arg(ds.colImage, ds.tableName))
        || !q.next()) {
        result.warnings << QStringLiteral("No se pudo leer ninguna imagen; "
                                          "se asume tileSize=256.");
        return;
    }

    const QByteArray blob = q.value(0).toByteArray();
    if (blob.isEmpty()) {
        result.warnings << QStringLiteral("La primera imagen esta vacia; "
                                          "se asume tileSize=256.");
        return;
    }

    // QImageReader lee solo la cabecera: no decodifica el pixmap entero.
    QBuffer buf;
    buf.setData(blob);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    const QSize size = reader.size();

    if (!size.isValid()) {
        result.warnings << QStringLiteral("Formato de imagen no reconocido (%1 "
                                          "bytes); se asume tileSize=256.")
                               .arg(blob.size());
        return;
    }

    ds.tileSize = size.width();
    if (size.width() != size.height()) {
        result.warnings << QStringLiteral("Teselas no cuadradas (%1x%2).")
                               .arg(size.width()).arg(size.height());
    }
}

std::optional<ProbeResult> TileDatasetProbe::probe(const QString &filePath,
                                                   const QString &id,
                                                   const QGeoRectangle &reference)
{
    const QFileInfo fi(filePath);
    if (!fi.exists()) {
        qCCritical(lcMapaTiles) << "No existe:" << filePath;
        return std::nullopt;
    }

    // Se usa un id de sondeo distinto para no pisar la conexion de produccion.
    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("probe_%1").arg(id), filePath,
        SqliteConnectionPool::Mode::ReadOnly);

    if (!db.isOpen())
        return std::nullopt;

    ProbeResult result;
    result.fileSizeBytes = fi.size();
    result.dataset.id = id;
    result.dataset.displayName = id;
    result.dataset.filePath = fi.absoluteFilePath();

    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA page_size")) && q.next())
        result.pageSize = q.value(0).toInt();

    // Localizar la tabla de teselas.
    QStringList tables;
    if (q.exec(QStringLiteral("SELECT name FROM sqlite_master "
                              "WHERE type='table'"))) {
        while (q.next())
            tables << q.value(0).toString();
    }
    if (!tables.contains(QStringLiteral("tiles"), Qt::CaseInsensitive)) {
        bool found = false;
        for (const QString &t : tables) {
            if (t.startsWith(QLatin1String("sqlite_")))
                continue;
            result.dataset.tableName = t;
            found = true;
            break;
        }
        if (!found) {
            qCCritical(lcMapaTiles) << "No hay tabla de teselas en" << filePath;
            return std::nullopt;
        }
        result.warnings << QStringLiteral("No hay tabla 'tiles'; se usa '%1'.")
                               .arg(result.dataset.tableName);
    }

    if (!detectColumns(db, result.dataset, result.warnings))
        return std::nullopt;
    if (!detectZMapping(db, result.dataset, result, reference))
        return std::nullopt;

    detectScheme(db, result.dataset, result, reference);
    detectTileSize(db, result.dataset, result);

    return result;
}

QString TileDatasetProbe::formatReport(const ProbeResult &r)
{
    QString s;
    QTextStream o(&s);

    const TileDataset &d = r.dataset;

    o << "==========================================================\n";
    o << " DATASET: " << d.id << "\n";
    o << "==========================================================\n";
    o << " Fichero      : " << d.filePath << "\n";
    o << " Tamano       : "
      << QString::number(static_cast<double>(r.fileSizeBytes) / 1048576.0, 'f', 1)
      << " MiB (" << r.fileSizeBytes << " bytes)\n";
    o << " page_size    : " << r.pageSize << "\n";
    o << " Tabla        : " << d.tableName << "\n";
    o << " Columnas     : z=" << d.colZ << " x=" << d.colX << " y=" << d.colY
      << " image=" << d.colImage;
    if (d.hasSColumn) o << " s=" << d.colS << " (valor " << d.sValue << ")";
    else              o << " [sin columna s]";
    o << "\n";
    o << " Teselas      : " << r.totalTiles << "\n";
    o << " Tamano tesela: " << d.tileSize << " px\n";
    o << " Relleno tipico: " << QString::number(d.typicalFill * 100.0, 'f', 1)
      << " % (nivel de referencia)\n";
    o << "\n";

    o << " MAPEO DE ZOOM\n";
    o << "   storedZ = " << d.zFactor << " * logicalZ + " << d.zOffset << "\n";
    o << "   zoom logico disponible : [" << d.minZoom << " .. " << d.maxZoom << "]\n";
    o << "   nivel de fondo garantizado: z=" << d.baseZoom
      << " (el mas grueso completo; el unico util sobre mar abierto)\n";
    o << "   zoom logico recomendado: [" << d.minZoom << " .. "
      << d.recommendedMaxZoom << "]\n";
    o << "   motivo: " << r.zMappingReason << "\n";
    o << "\n";

    o << " ESQUEMA EJE Y: " << tileSchemeToString(d.scheme) << "\n";
    o << "   motivo: " << r.schemeReason << "\n";
    o << "\n";

    if (r.coverage.isValid()) {
        o << " EXTENSION CUBIERTA";
        if (r.referenceLevel >= 0
            && r.referenceLevel < static_cast<int>(r.levels.size()))
            o << "  (segun el nivel de referencia z="
              << r.levels.at(r.referenceLevel).logicalZ << ", el mas poblado)";
        o << "\n";
        o << "   NO: " << QString::number(r.coverage.topLeft().latitude(), 'f', 5)
          << ", " << QString::number(r.coverage.topLeft().longitude(), 'f', 5) << "\n";
        o << "   SE: " << QString::number(r.coverage.bottomRight().latitude(), 'f', 5)
          << ", " << QString::number(r.coverage.bottomRight().longitude(), 'f', 5) << "\n";
        o << "\n";
    }

    if (r.sFilterVerified) {
        o << " FILTRO POR COLUMNA '" << d.colS << "'\n";
        o << "   con filtro " << r.sFilterKeeps << " de " << r.sFilterTotal
          << " teselas -> " << (d.hasSColumn ? "SE USA" : "DESACTIVADO") << "\n\n";
    }

    o << " NIVELES\n";
    o << "   zLog  zBD      teselas  relleno   lon[oeste..este]   lat[sur..norte]\n";
    o << "   ----  ----  ----------  -------  -----------------  -----------------\n";
    for (int i = 0; i < static_cast<int>(r.levels.size()); ++i) {
        const ZoomLevelStats &l = r.levels.at(i);
        o << "   " << QString::number(l.logicalZ).rightJustified(4)
          << "  " << QString::number(l.storedZ).rightJustified(4)
          << "  " << QString::number(l.tileCount).rightJustified(10)
          << "  " << QString::number(l.fillRatio * 100.0, 'f', 1).rightJustified(6) << "%"
          << "  " << QStringLiteral("%1..%2")
                        .arg(l.lonWest, 0, 'f', 2).arg(l.lonEast, 0, 'f', 2)
                        .rightJustified(17)
          << "  " << QStringLiteral("%1..%2")
                        .arg(l.latSouth, 0, 'f', 2).arg(l.latNorth, 0, 'f', 2)
                        .rightJustified(17);
        if (i == r.referenceLevel) o << "  <-- referencia";
        if (l.logicalZ > d.recommendedMaxZoom) o << "  [fuera del zoom recomendado]";
        o << "\n";
    }
    o << "\n";

    if (!r.warnings.isEmpty()) {
        o << " AVISOS\n";
        for (const QString &w : r.warnings)
            o << "   - " << w << "\n";
        o << "\n";
    }

    return s;
}

} // namespace libmapa
