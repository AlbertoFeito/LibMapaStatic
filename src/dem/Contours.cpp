#include "libmapa/Contours.h"

#include "dem/IElevationSource.h"

#include <QHash>
#include <QtMath>

#include <cmath>
#include <vector>

namespace libmapa {

namespace {

// Identificador único de una arista de la rejilla (donde cae un cruce de la
// curva): orientación (H=0 horizontal, V=1 vertical) + fila + columna. Dos
// segmentos que comparten un cruce comparten esta clave exacta → se unen sin
// depender de tolerancias de coma flotante.
inline quint64 edgeId(int orient, int r, int c)
{
    return (quint64(orient) << 63) | (quint64(quint32(r)) << 31) | quint64(quint32(c));
}

// Interpola la posición del cruce de la curva (cota = level) en el segmento
// recto entre dos nodos de la rejilla con cotas za/zb.
inline QGeoCoordinate cruce(double latA, double lonA, double za,
                            double latB, double lonB, double zb, double level)
{
    const double t = (zb == za) ? 0.5 : (level - za) / (zb - za);
    return QGeoCoordinate(latA + (latB - latA) * t, lonA + (lonB - lonA) * t);
}

// Un segmento de curva en una celda: conecta los cruces de dos aristas.
struct Segmento { quint64 a; quint64 b; };

} // namespace

// Genera curvas de nivel por marching squares sobre una rejilla muestreada del
// DEM. Recorre la rejilla por filas (solo dos filas de nodos en memoria a la
// vez); por cada celda y cada nivel que la cruza emite 1-2 segmentos; al final
// une los segmentos de cada nivel en polilíneas encadenando por arista.
QVector<ContourLine> computeContours(const IElevationSource &src,
                                     const ContourParams &params)
{
    QVector<ContourLine> salida;

    const double latN = qMax(params.latN, params.latS);
    const double latS = qMin(params.latN, params.latS);
    const double lonW = qMin(params.lonW, params.lonE);
    const double lonE = qMax(params.lonW, params.lonE);
    const double intervalo = qAbs(params.interval);
    if (intervalo <= 0.0 || latN <= latS || lonE <= lonW)
        return salida;

    // Tamaño de la rejilla a partir del paso en metros (aprox. esférica).
    const double midLat = (latN + latS) * 0.5;
    const double latSpanM = (latN - latS) * 111320.0;
    const double lonSpanM = (lonE - lonW) * 111320.0 * std::cos(qDegreesToRadians(midLat));
    const double paso = qMax(1.0, params.stepMeters);
    const int nRows = qMax(2, int(std::lround(latSpanM / paso)) + 1);
    const int nCols = qMax(2, int(std::lround(lonSpanM / paso)) + 1);

    const auto latAt = [&](int r) { return latN - (latN - latS) * double(r) / double(nRows - 1); };
    const auto lonAt = [&](int c) { return lonW + (lonE - lonW) * double(c) / double(nCols - 1); };

    // Segmentos y posiciones de cruce agrupados por índice de nivel (k tal que
    // cota = base + k·intervalo). Se asigna el nivel en cuanto aparece.
    QHash<int, QVector<Segmento>> segs;
    QHash<int, QHash<quint64, QGeoCoordinate>> pos;

    std::vector<double> filaArriba, filaAbajo;
    filaArriba.resize(std::size_t(nCols));
    filaAbajo.resize(std::size_t(nCols));
    const auto muestrearFila = [&](int r, std::vector<double> &dst) {
        const double lat = latAt(r);
        for (int c = 0; c < nCols; ++c)
            dst[std::size_t(c)] = src.elevationAt(QGeoCoordinate(lat, lonAt(c)));
    };
    muestrearFila(0, filaArriba);

    for (int r = 0; r < nRows - 1; ++r) {
        muestrearFila(r + 1, filaAbajo);
        const double latR = latAt(r), latR1 = latAt(r + 1);

        for (int c = 0; c < nCols - 1; ++c) {
            const double za = filaArriba[std::size_t(c)];       // TL (r,  c)
            const double zb = filaArriba[std::size_t(c + 1)];   // TR (r,  c+1)
            const double zc = filaAbajo[std::size_t(c + 1)];    // BR (r+1,c+1)
            const double zd = filaAbajo[std::size_t(c)];        // BL (r+1,c)
            if (std::isnan(za) || std::isnan(zb) || std::isnan(zc) || std::isnan(zd))
                continue;

            double cmin = qMin(qMin(za, zb), qMin(zc, zd));
            double cmax = qMax(qMax(za, zb), qMax(zc, zd));
            const double lonC = lonAt(c), lonC1 = lonAt(c + 1);

            // Niveles que cruzan esta celda: k en (cmin, cmax], recortado al
            // rango de cota pedido [minLevel, maxLevel].
            const double loLim = qMax(cmin, params.minLevel);
            const double hiLim = qMin(cmax, params.maxLevel);
            int kLo = int(std::ceil((loLim - params.base) / intervalo));
            int kHi = int(std::floor((hiLim - params.base) / intervalo));
            for (int k = kLo; k <= kHi; ++k) {
                const double L = params.base + double(k) * intervalo;
                // Esquina "por encima" del nivel.
                const int caso = (za >= L ? 8 : 0) | (zb >= L ? 4 : 0)
                               | (zc >= L ? 2 : 0) | (zd >= L ? 1 : 0);
                if (caso == 0 || caso == 15)
                    continue;

                // Cruces por arista (solo donde los extremos cruzan L).
                const quint64 idT = edgeId(0, r, c);          // superior  (a-b)
                const quint64 idB = edgeId(0, r + 1, c);      // inferior  (d-c)
                const quint64 idL = edgeId(1, r, c);          // izquierda (a-d)
                const quint64 idR = edgeId(1, r, c + 1);      // derecha   (b-c)
                auto &P = pos[k];
                auto emite = [&](quint64 e1, quint64 e2) { segs[k].push_back({e1, e2}); };
                const bool xT = (za >= L) != (zb >= L);
                const bool xB = (zd >= L) != (zc >= L);
                const bool xL = (za >= L) != (zd >= L);
                const bool xR = (zb >= L) != (zc >= L);
                if (xT) P[idT] = cruce(latR, lonC, za, latR, lonC1, zb, L);
                if (xB) P[idB] = cruce(latR1, lonC, zd, latR1, lonC1, zc, L);
                if (xL) P[idL] = cruce(latR, lonC, za, latR1, lonC, zd, L);
                if (xR) P[idR] = cruce(latR, lonC1, zb, latR1, lonC1, zc, L);

                // Conexiones por caso (marching squares). Saddles (5,10) → 2.
                switch (caso) {
                case 1: case 14: emite(idL, idB); break;
                case 2: case 13: emite(idB, idR); break;
                case 3: case 12: emite(idL, idR); break;
                case 4: case 11: emite(idT, idR); break;
                case 6: case 9:  emite(idT, idB); break;
                case 7: case 8:  emite(idT, idL); break;
                case 5:          emite(idT, idL); emite(idB, idR); break;
                case 10:         emite(idT, idR); emite(idB, idL); break;
                default: break;
                }
            }
        }
        filaArriba.swap(filaAbajo);
    }

    // Encadena los segmentos de cada nivel en polilíneas. Grafo cuyos nodos son
    // los cruces (clave de arista); cada nodo tiene grado <= 2, así que salen
    // cadenas abiertas y bucles cerrados.
    const double minLen = params.minLengthMeters;
    for (auto it = segs.constBegin(); it != segs.constEnd(); ++it) {
        const int k = it.key();
        const QVector<Segmento> &lista = it.value();
        const QHash<quint64, QGeoCoordinate> &P = pos[k];
        const double L = params.base + double(k) * intervalo;

        QHash<quint64, QVector<int>> inc;   // nodo -> índices de segmento
        inc.reserve(lista.size() * 2);
        for (int i = 0; i < lista.size(); ++i) {
            inc[lista[i].a].push_back(i);
            inc[lista[i].b].push_back(i);
        }
        QVector<bool> usado(lista.size(), false);
        const auto otro = [&](int s, quint64 nodo) {
            return lista[s].a == nodo ? lista[s].b : lista[s].a;
        };
        // Camina desde 'nodo' consumiendo segmentos sin usar hasta agotar.
        const auto camina = [&](quint64 nodo) {
            ContourLine linea;
            linea.elevation = L;
            linea.points.push_back(P.value(nodo));
            quint64 cur = nodo;
            for (;;) {
                int sig = -1;
                for (int s : inc.value(cur))
                    if (!usado[s]) { sig = s; break; }
                if (sig < 0)
                    break;
                usado[sig] = true;
                cur = otro(sig, cur);
                linea.points.push_back(P.value(cur));
            }
            return linea;
        };

        // Primero cadenas abiertas (nodos de grado 1); luego bucles.
        for (auto n = inc.constBegin(); n != inc.constEnd(); ++n) {
            if (n.value().size() != 1 || usado[n.value().first()])
                continue;
            ContourLine linea = camina(n.key());
            if (linea.points.size() >= 2)
                salida.push_back(linea);
        }
        for (int i = 0; i < lista.size(); ++i) {
            if (usado[i])
                continue;
            ContourLine linea = camina(lista[i].a);
            if (linea.points.size() >= 2)
                salida.push_back(linea);
        }

        // Descarta curvas demasiado cortas, si se pidió.
        if (minLen > 0.0) {
            for (int i = int(salida.size()) - 1; i >= 0; --i) {
                if (salida[i].elevation != L)
                    continue;
                double len = 0.0;
                const QVector<QGeoCoordinate> &p = salida[i].points;
                for (int j = 1; j < p.size(); ++j)
                    len += p[j - 1].distanceTo(p[j]);
                if (len < minLen)
                    salida.remove(i);
            }
        }
    }

    return salida;
}

} // namespace libmapa
