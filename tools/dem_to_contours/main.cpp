/*
 * dem_to_contours - genera CURVAS DE NIVEL (isohipsas) de un DEM y las guarda
 * como CAPA VECTORIAL (.sqlitedb de entidades) que la app lee como una capa más:
 * se activa/desactiva y cada curva lleva su cota (etiqueta en las curvas índice).
 *
 * A diferencia del relieve sombreado (teselas raster), las curvas son vectoriales:
 * nítidas a cualquier zoom, seleccionables y con lectura numérica de la altura.
 *
 * Uso:
 *   dem_to_contours --in <carpeta_hgt | dem.sqlitedb> --out curvas.sqlitedb
 *                   (--cuba | --bbox latN,lonO,latS,lonE)
 *                   [--interval 100]   (separación entre curvas, m)
 *                   [--index 500]      (una de cada N es curva índice, gruesa+etiqueta)
 *                   [--step 150]       (resolución de muestreo, m; súbelo en país entero)
 *                   [--min-length 500] (descarta curvas más cortas que esto, m)
 *                   [--min-level 0]    (no genera curvas por debajo de esa cota;
 *                                       0 = recorta al nivel del mar, sin batimetría)
 *                   [--max-level N]    (no genera curvas por encima de esa cota)
 *                   [--labels peaks|all|none]  (etiquetas de cota; por defecto
 *                                       'peaks' = solo en las cimas, espaciadas)
 *                   [--label-sep 8000 --peak-span 6000]  (separación y tamaño
 *                                       máx. del anillo de cima, en metros)
 *                   [--layer curvas --name "Curvas de nivel"] [--overwrite]
 */

#include "libmapa/Contours.h"
#include "libmapa/MapFeature.h"
#include "db/VectorRepository.h"
#include "dem/HgtElevation.h"
#include "dem/IElevationSource.h"
#include "dem/SqliteElevation.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using namespace libmapa;

// Punto de entrada: abre el DEM, calcula las curvas de nivel en la bbox al
// intervalo pedido y las vuelca como entidades polilínea (una por curva) en una
// capa del .sqlitedb vectorial; las curvas índice (múltiplos de --index) salen
// más gruesas y con la cota como etiqueta. Imprime un resumen al terminar.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString in, out, layer = QStringLiteral("curvas"), name;
    double interval = 100.0, indexEvery = 500.0, step = 150.0, minLen = 0.0;
    double minLevel = -std::numeric_limits<double>::infinity();
    double maxLevel = std::numeric_limits<double>::infinity();
    double latN = 90.0, lonW = 180.0, latS = -90.0, lonE = -180.0;
    QString labelMode = QStringLiteral("peaks");   // peaks | all | none
    double labelSep = 8000.0;    // separación mínima entre etiquetas (m)
    double peakSpan = 6000.0;    // tamaño máx. del anillo "cima" a etiquetar (m)
    bool haveBbox = false, overwrite = false;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &k = args.at(i);
        const auto val = [&]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (k == QLatin1String("--in")) in = val();
        else if (k == QLatin1String("--out")) out = val();
        else if (k == QLatin1String("--layer")) layer = val();
        else if (k == QLatin1String("--name")) name = val();
        else if (k == QLatin1String("--interval")) interval = val().toDouble();
        else if (k == QLatin1String("--index")) indexEvery = val().toDouble();
        else if (k == QLatin1String("--step")) step = val().toDouble();
        else if (k == QLatin1String("--min-length")) minLen = val().toDouble();
        else if (k == QLatin1String("--min-level")) minLevel = val().toDouble();
        else if (k == QLatin1String("--max-level")) maxLevel = val().toDouble();
        else if (k == QLatin1String("--labels")) labelMode = val().toLower();
        else if (k == QLatin1String("--label-sep")) labelSep = val().toDouble();
        else if (k == QLatin1String("--peak-span")) peakSpan = val().toDouble();
        else if (k == QLatin1String("--overwrite")) overwrite = true;
        else if (k == QLatin1String("--cuba")) {
            latN = 23.3; lonW = -85.0; latS = 19.7; lonE = -74.0; haveBbox = true;
        } else if (k == QLatin1String("--bbox")) {
            const QStringList p = val().split(QLatin1Char(','));
            if (p.size() == 4) {
                latN = p[0].toDouble(); lonW = p[1].toDouble();
                latS = p[2].toDouble(); lonE = p[3].toDouble();
                haveBbox = true;
            }
        } else { qWarning() << "Opcion desconocida:" << k; return 2; }
    }

    QTextStream cout(stdout);
    if (in.isEmpty() || out.isEmpty() || !haveBbox) {
        cout << "Uso: dem_to_contours --in <carpeta_hgt|dem.sqlitedb> --out curvas.sqlitedb\n"
                "                     (--cuba | --bbox latN,lonO,latS,lonE)\n"
                "                     [--interval 100] [--index 500] [--step 150]\n"
                "                     [--min-length 500] [--min-level 0] [--max-level N]\n"
                "                     [--labels peaks|all|none] [--label-sep 8000] [--peak-span 6000]\n"
                "                     [--layer curvas --name \"...\"] [--overwrite]\n";
        return 2;
    }
    if (name.isEmpty())
        name = QStringLiteral("Curvas de nivel");
    if (interval <= 0.0) { qCritical() << "--interval debe ser > 0"; return 2; }

    // Origen de elevación: carpeta de .hgt o una BD .sqlitedb.
    std::unique_ptr<IElevationSource> dem;
    const QFileInfo fi(in);
    if (fi.isDir()) {
        auto h = std::make_unique<HgtElevation>();
        h->setDirectory(in);
        dem = std::move(h);
    } else {
        dem = std::make_unique<SqliteElevation>(in);
    }

    if (QFile::exists(out)) {
        if (!overwrite) {
            qCritical() << out << "ya existe (usa --overwrite para sobrescribir).";
            return 1;
        }
        QFile::remove(out);
    }

    ContourParams params;
    params.latN = latN; params.lonW = lonW; params.latS = latS; params.lonE = lonE;
    params.interval = interval;
    params.stepMeters = step;
    params.minLengthMeters = minLen;
    params.minLevel = minLevel;
    params.maxLevel = maxLevel;

    cout << "Calculando curvas (intervalo " << interval << " m, paso " << step << " m)...\n";
    cout.flush();
    // Progreso en la misma línea (cada ~2 %), para que no parezca colgado.
    int ultimo = -1;
    const auto progreso = [&](int hecho, int total) {
        const int pct = total > 0 ? int(qint64(hecho) * 100 / total) : 100;
        if (pct != ultimo && (pct % 2 == 0 || pct == 100)) {
            ultimo = pct;
            cout << "\r  muestreando rejilla: " << pct << " %   ";
            cout.flush();
        }
        return true;   // la consola no cancela
    };
    const QVector<ContourLine> curvas = computeContours(*dem, params, progreso);
    cout << "\r  muestreando rejilla: 100 %   \n";
    if (curvas.isEmpty()) {
        qCritical() << "No se generaron curvas (¿sin cota en la zona?).";
        return 1;
    }

    // Vuelca cada curva como una entidad polilínea. Curvas índice (múltiplos de
    // --index): más gruesas, color más oscuro y con la cota como etiqueta.
    VectorRepository repo;
    if (!repo.open(out)) {
        qCritical() << "No se pudo crear" << out << ":" << repo.lastError();
        return 1;
    }
    LayerInfo capa;
    capa.id = layer;
    capa.displayName = name;
    capa.zOrder = 20;
    repo.saveLayer(capa);

    // Candidata a etiqueta de "cima": anillo índice CERRADO y pequeño (una
    // cumbre), con su cota y centro, para luego quedarnos con las más altas y
    // separadas (en modo peaks).
    struct Cand { int idx; double cota; QGeoCoordinate centro; };
    QVector<Cand> cands;

    QVector<MapFeature> feats;
    feats.reserve(curvas.size());
    int nIndex = 0;
    for (const ContourLine &c : curvas) {
        MapFeature f;
        f.layerId = layer;
        f.kind = GeometryKind::Polyline;
        f.type = QStringLiteral("curva_nivel");
        f.geometry = c.points;
        f.attributes[QStringLiteral("cota")] = c.elevation;

        // ¿Curva índice? (cota múltiplo de indexEvery).
        const bool esIndice = indexEvery > 0.0
            && std::abs(std::remainder(c.elevation, indexEvery)) < interval * 0.25;
        if (esIndice) {
            ++nIndex;
            f.name = QString::number(qRound(c.elevation)) + QStringLiteral(" m");
            f.style.lineColor = QColor(110, 70, 40);
            f.style.lineWidth = 0.9;
        } else {
            f.style.lineColor = QColor(150, 100, 60, 180);
            f.style.lineWidth = 0.4;
        }
        f.style.labelVisible = false;   // se decide abajo según --labels

        // Anillo de cima: índice, cerrado y pequeño → candidato a etiqueta.
        if (esIndice && labelMode == QLatin1String("peaks")
            && c.points.size() >= 4
            && c.points.first().distanceTo(c.points.last()) < 2.0) {
            double laMin = 90, laMax = -90, loMin = 180, loMax = -180, sLa = 0, sLo = 0;
            for (const QGeoCoordinate &p : c.points) {
                laMin = qMin(laMin, p.latitude());  laMax = qMax(laMax, p.latitude());
                loMin = qMin(loMin, p.longitude()); loMax = qMax(loMax, p.longitude());
                sLa += p.latitude();  sLo += p.longitude();
            }
            const double span = QGeoCoordinate(laMin, loMin).distanceTo(QGeoCoordinate(laMax, loMax));
            if (span <= peakSpan) {
                const double n = double(c.points.size());
                cands.push_back({int(feats.size()), c.elevation,
                                 QGeoCoordinate(sLa / n, sLo / n)});
            }
        }
        feats.push_back(f);
    }

    // Decide qué curvas llevan etiqueta. 'all': todas las índice. 'none': ninguna.
    // 'peaks' (defecto): una por cima, la más alta, separadas al menos labelSep.
    int nLabel = 0;
    if (labelMode == QLatin1String("all")) {
        for (MapFeature &f : feats)
            if (!f.name.isEmpty()) { f.style.labelVisible = true; ++nLabel; }
    } else if (labelMode == QLatin1String("peaks")) {
        std::sort(cands.begin(), cands.end(),
                  [](const Cand &a, const Cand &b) { return a.cota > b.cota; });
        QVector<QGeoCoordinate> puestas;
        for (const Cand &c : cands) {
            bool cerca = false;
            for (const QGeoCoordinate &q : puestas)
                if (c.centro.distanceTo(q) < labelSep) { cerca = true; break; }
            if (cerca)
                continue;
            feats[c.idx].style.labelVisible = true;
            puestas.push_back(c.centro);
            ++nLabel;
        }
    }
    // 'none' deja todas sin etiqueta.

    if (!repo.saveFeatures(feats)) {
        qCritical() << "No se pudieron guardar las curvas:" << repo.lastError();
        return 1;
    }
    repo.close();

    cout << "Listo: " << curvas.size() << " curvas (" << nIndex << " índice, "
         << nLabel << " con etiqueta) en " << QFileInfo(out).fileName() << "\n";
    cout << "Cárgalo en la app como capa de entidades (" << layer << ").\n";
    return 0;
}
