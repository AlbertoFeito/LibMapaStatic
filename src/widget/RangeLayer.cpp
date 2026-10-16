#include "widget/RangeLayer.h"

#include "geo/MapDecor.h"

#include <QFontMetricsF>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace libmapa {

namespace {

// Topes de seguridad: un anillo de mas de media Tierra o cientos de anillos no
// tienen sentido y harian el dibujo caro.
constexpr int kMaxAnillos = 50;
constexpr double kMaxRadio = 10.0e6;

// Puntos de un trazo geodesico entre dos coordenadas: lo bastante densos para
// que una marcacion larga siga la ortodromica, que en Mercator es curva.
QVector<QGeoCoordinate> geodesica(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    const double d = a.distanceTo(b);
    const double rumbo = a.azimuthTo(b);
    const int n = std::clamp(int(d / 20000.0), 1, 64);   // un punto cada ~20 km
    QVector<QGeoCoordinate> v;
    v.reserve(n + 1);
    for (int i = 0; i <= n; ++i)
        v.append(i == 0 ? a : (i == n ? b : a.atDistanceAndAzimuth(d * i / n, rumbo)));
    return v;
}

} // namespace

// Con antialiasing: los anillos son curvas y sin el se verian escalonados.
// \a targets resuelve los centros/extremos que cuelgan de un objetivo.
RangeLayer::RangeLayer(QCustomPlot *parent, TargetModel *targets)
    : QCPLayerable(parent)
    , m_targets(targets)
{
    setAntialiased(true);
}

RangeLayer::~RangeLayer() = default;

// Funcion de proyeccion de MapView (latitud -> eje Y en grados de Mercator).
void RangeLayer::setAxisMapper(std::function<QPointF(const QGeoCoordinate &)> toAxis)
{
    m_toAxis = std::move(toAxis);
}

// Repinta solo la capa (buffered) donde vive; si aun no esta en una, nada.
void RangeLayer::repintar()
{
    if (QCPLayer *capa = layer())
        capa->replot();
}

// Alta de unos anillos; devuelve su id (comun con las marcaciones).
qint64 RangeLayer::addRings(const RangeRings &rings)
{
    const qint64 id = m_nextId++;
    m_rings.insert(id, rings);
    repintar();
    return id;
}

// Cambia unos anillos existentes. false si el id no es de anillos.
bool RangeLayer::updateRings(qint64 id, const RangeRings &rings)
{
    if (!m_rings.contains(id))
        return false;
    m_rings.insert(id, rings);
    repintar();
    return true;
}

// Marcacion sin sentido por construccion: mismo objetivo en los dos extremos,
// o el mismo punto fijo. Se rechaza al darla de alta.
bool RangeLayer::degenerada(const BearingLine &l)
{
    if (l.fromTarget >= 0 && l.fromTarget == l.toTarget)
        return true;
    return l.fromTarget < 0 && l.toTarget < 0 && l.from.isValid() && l.to.isValid()
        && l.from.distanceTo(l.to) < 1.0;
}

// Alta de una marcacion; devuelve su id, o -1 si no tiene longitud (origen y
// destino iguales).
qint64 RangeLayer::addBearing(const BearingLine &line)
{
    if (degenerada(line))
        return -1;
    const qint64 id = m_nextId++;
    m_bearings.insert(id, line);
    repintar();
    return id;
}

// Cambia una marcacion existente. false si el id no es de marcacion.
bool RangeLayer::updateBearing(qint64 id, const BearingLine &line)
{
    if (!m_bearings.contains(id) || degenerada(line))
        return false;
    m_bearings.insert(id, line);
    repintar();
    return true;
}

// Quita unos anillos o una marcacion por su id.
bool RangeLayer::remove(qint64 id)
{
    const bool quitado = m_rings.remove(id) > 0 || m_bearings.remove(id) > 0;
    if (quitado)
        repintar();
    return quitado;
}

// Quita todo.
void RangeLayer::clear()
{
    if (isEmpty())
        return;
    m_rings.clear();
    m_bearings.clear();
    repintar();
}

// Ids de anillos y marcaciones, ordenados.
QVector<qint64> RangeLayer::ids() const
{
    QVector<qint64> v;
    for (auto it = m_rings.cbegin(); it != m_rings.cend(); ++it)
        v.append(it.key());
    for (auto it = m_bearings.cbegin(); it != m_bearings.cend(); ++it)
        v.append(it.key());
    std::sort(v.begin(), v.end());
    return v;
}

// Copia de unos anillos por id.
bool RangeLayer::rings(qint64 id, RangeRings *out) const
{
    const auto it = m_rings.constFind(id);
    if (it == m_rings.cend())
        return false;
    if (out)
        *out = *it;
    return true;
}

// Copia de una marcacion por id.
bool RangeLayer::bearing(qint64 id, BearingLine *out) const
{
    const auto it = m_bearings.constFind(id);
    if (it == m_bearings.cend())
        return false;
    if (out)
        *out = *it;
    return true;
}

// Separacion actual de unos anillos (fija o automatica), 0 si no se puede.
double RangeLayer::spacing(qint64 id) const
{
    const auto it = m_rings.constFind(id);
    if (it == m_rings.cend())
        return 0.0;
    const QGeoCoordinate c = resolve(it->center, it->targetId);
    return c.isValid() ? pasoEfectivo(*it, c) : 0.0;
}

// Distancia de un punto a una polilinea de pantalla (infinita si esta vacia).
static double distanciaA(const QPointF &p, const QPolygonF &linea)
{
    double mejor = std::numeric_limits<double>::max();
    for (int i = 1; i < linea.size(); ++i) {
        const QPointF a = linea.at(i - 1), b = linea.at(i);
        const QPointF ab = b - a;
        const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
        double t = l2 > 0.0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / l2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        mejor = std::min(mejor, QLineF(p, a + ab * t).length());
    }
    return mejor;
}

// Hit-test de anillos y marcaciones con la misma geometria que el dibujo.
qint64 RangeLayer::markAt(const QPointF &pixel, double tolPx) const
{
    for (auto it = m_bearings.cend(); it != m_bearings.cbegin();) {
        --it;
        QGeoCoordinate a, b;
        if (!ends(*it, &a, &b) || a.distanceTo(b) < 1.0)
            continue;
        QPolygonF linea;
        for (const QGeoCoordinate &p : geodesica(a, b))
            linea.append(screenPos(p));
        if (distanciaA(pixel, linea) <= tolPx)
            return it.key();
    }
    for (auto it = m_rings.cend(); it != m_rings.cbegin();) {
        --it;
        const QGeoCoordinate c = resolve(it->center, it->targetId);
        const double paso = c.isValid() ? pasoEfectivo(*it, c) : 0.0;
        if (!(paso > 0.0))
            continue;
        if (QLineF(pixel, screenPos(c)).length() <= tolPx + 4.0)
            return it.key();
        const int n = std::clamp(it->count, 1, kMaxAnillos);
        for (int i = 1; i <= n; ++i) {
            QPolygonF anillo;
            for (const QGeoCoordinate &p : MapDecor::geodesicCircle(c, paso * i, 96))
                anillo.append(screenPos(p));
            if (distanciaA(pixel, anillo) <= tolPx)
                return it.key();
        }
    }
    return -1;
}

// Rectangulo de pixeles donde se ve el mapa.
QRectF RangeLayer::vista() const
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return QRectF();
    return plot->axisRect() ? QRectF(plot->axisRect()->rect()) : QRectF(plot->viewport());
}

// Separacion con la que se dibujan los anillos centrados en \a c. Fija, o
// automatica: la escala local (pixeles por metro en el centro, Mercator es
// conforme) dice cuantos metros caben en el 45 % del lado corto de la vista,
// y niceRingSpacing lo redondea para que los count anillos quepan.
double RangeLayer::pasoEfectivo(const RangeRings &r, const QGeoCoordinate &c) const
{
    const int n = std::clamp(r.count, 1, kMaxAnillos);
    if (r.spacingMeters > 0.0 && std::isfinite(r.spacingMeters))
        return std::min(r.spacingMeters, kMaxRadio / n);
    if (r.spacingMeters != 0.0)
        return 0.0;                          // negativa o NaN: no se dibuja
    const QRectF v = vista();
    const double px1km = QLineF(screenPos(c), screenPos(c.atDistanceAndAzimuth(1000.0, 90.0))).length();
    if (v.isEmpty() || !(px1km > 0.0))
        return 0.0;
    const double radioM = 0.45 * std::min(v.width(), v.height()) / px1km * 1000.0;
    return std::min(MapDecor::niceRingSpacing(radioM, n, m_units), kMaxRadio / n);
}

// Donde poner el rotulo de una curva: en \a ideal si se ve; si no, en el
// punto visible de la curva mas cercano a el (con un margen al borde para
// que el rotulo no quede cortado). Si no se ve nada, \a ideal (no se vera).
QPointF RangeLayer::dentroDeVista(const QPointF &ideal, const QPolygonF &curva) const
{
    const QRectF v = vista().adjusted(30.0, 12.0, -30.0, -12.0);
    if (v.isEmpty() || v.contains(ideal))
        return ideal;
    QPointF mejor = ideal;
    double dMejor = std::numeric_limits<double>::max();
    for (const QPointF &p : curva) {
        if (!v.contains(p))
            continue;
        const double d = QLineF(p, ideal).length();
        if (d < dMejor) {
            dMejor = d;
            mejor = p;
        }
    }
    return mejor;
}

// Punto donde el segmento desde -> hasta sale de la vista (con margen), para
// rotular un radial cuyo extremo no se ve. Si \a hasta se ve, \a hasta.
QPointF RangeLayer::bordeDeVista(const QPointF &desde, const QPointF &hasta) const
{
    const QRectF v = vista().adjusted(24.0, 12.0, -24.0, -12.0);
    if (v.isEmpty() || v.contains(hasta) || !v.contains(desde))
        return hasta;
    const QLineF seg(desde, hasta);
    const QLineF lados[] = { QLineF(v.topLeft(), v.topRight()), QLineF(v.topRight(), v.bottomRight()),
                             QLineF(v.bottomRight(), v.bottomLeft()), QLineF(v.bottomLeft(), v.topLeft()) };
    for (const QLineF &lado : lados) {
        QPointF corte;
        if (seg.intersects(lado, &corte) == QLineF::BoundedIntersection)
            return corte;
    }
    return hasta;
}

// Punto fijo, o la posicion actual del objetivo si se da uno (invalido si ese
// objetivo ya no existe: lo que cuelga de el no se dibuja).
QGeoCoordinate RangeLayer::resolve(const QGeoCoordinate &fixed, qint64 targetId) const
{
    if (targetId < 0)
        return fixed;
    if (!m_targets)
        return QGeoCoordinate();
    const auto it = m_targets->entries().constFind(targetId);
    return it != m_targets->entries().constEnd() ? it->target.position : QGeoCoordinate();
}

// Origen y destino de una marcacion: destino fijo u objetivo, o, sin destino,
// el punto a lengthMeters con el rumbo dado.
bool RangeLayer::ends(const BearingLine &l, QGeoCoordinate *from, QGeoCoordinate *to) const
{
    const QGeoCoordinate a = resolve(l.from, l.fromTarget);
    if (!a.isValid())
        return false;
    QGeoCoordinate b;
    if (l.toTarget >= 0 || l.to.isValid())
        b = resolve(l.to, l.toTarget);
    else if (std::isfinite(l.bearingDeg) && l.lengthMeters > 0.0)
        b = a.atDistanceAndAzimuth(std::min(l.lengthMeters, kMaxRadio), l.bearingDeg);
    if (!b.isValid())
        return false;
    if (from)
        *from = a;
    if (to)
        *to = b;
    return true;
}

// Extremos resueltos de una marcacion por id.
bool RangeLayer::bearingEnds(qint64 id, QGeoCoordinate *from, QGeoCoordinate *to) const
{
    const auto it = m_bearings.constFind(id);
    return it != m_bearings.cend() && ends(*it, from, to);
}

// Coordenada -> pixel, igual que el resto de capas (eje Mercator).
QPointF RangeLayer::screenPos(const QGeoCoordinate &c) const
{
    QCustomPlot *plot = parentPlot();
    if (!plot || !c.isValid())
        return {};
    const QPointF eje = m_toAxis ? m_toAxis(c) : QPointF(c.longitude(), c.latitude());
    return QPointF(plot->xAxis->coordToPixel(eje.x()), plot->yAxis->coordToPixel(eje.y()));
}

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void RangeLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Rotulo centrado en \a center sobre una caja clara, para leerse en cualquier fondo.
void RangeLayer::label(QCPPainter *painter, const QPointF &center, const QString &txt,
                       const QColor &color) const
{
    const QFontMetricsF fm(painter->font());
    const QSizeF t(fm.horizontalAdvance(txt) + 8.0, fm.height() + 2.0);
    const QRectF caja(center.x() - t.width() / 2.0, center.y() - t.height() / 2.0,
                      t.width(), t.height());
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 215));
    painter->drawRoundedRect(caja, 3.0, 3.0);
    painter->setPen(color.darker(130));
    painter->drawText(caja, Qt::AlignCenter, txt);
}

// Dibuja primero los anillos y despues las marcaciones (encima). Lo que cuelga
// de un objetivo que ya no existe se omite.
void RangeLayer::draw(QCPPainter *painter)
{
    if (!parentPlot() || isEmpty())
        return;
    QFont f = painter->font();
    f.setPointSizeF(7.5);
    f.setBold(true);
    painter->setFont(f);
    for (const RangeRings &r : m_rings)
        drawRings(painter, r);
    for (const BearingLine &l : m_bearings)
        drawBearing(painter, l);
}

// Anillos geodesicos con su distancia arriba, radiales opcionales con su
// marcacion en el extremo, y una cruz en el centro.
void RangeLayer::drawRings(QCPPainter *painter, const RangeRings &r) const
{
    const QGeoCoordinate c = resolve(r.center, r.targetId);
    const int n = std::clamp(r.count, 1, kMaxAnillos);
    if (!c.isValid())
        return;
    const double paso = pasoEfectivo(r, c);
    if (!(paso > 0.0))
        return;
    const QPointF pc = screenPos(c);

    painter->setBrush(Qt::NoBrush);
    for (int i = 1; i <= n; ++i) {
        QPolygonF anillo;
        for (const QGeoCoordinate &p : MapDecor::geodesicCircle(c, paso * i, 96))
            anillo.append(screenPos(p));
        painter->setPen(QPen(QColor(255, 255, 255, 150), 3.0));
        painter->drawPolyline(anillo);
        painter->setPen(QPen(r.color, 1.4, i == n ? Qt::SolidLine : Qt::DashLine));
        painter->drawPolyline(anillo);
        if (r.labels)
            label(painter, dentroDeVista(anillo.first(), anillo),
                  MapDecor::formatDistance(paso * i, m_units), r.color);
    }

    if (r.spokeEveryDeg > 0) {
        const int salto = std::clamp(r.spokeEveryDeg, 5, 180);
        for (int az = 0; az < 360; az += salto) {
            const QPointF fin = screenPos(c.atDistanceAndAzimuth(paso * n, az));
            painter->setPen(QPen(r.color, 1.0, Qt::DotLine));
            painter->drawLine(pc, fin);
            if (r.labels && az != 0) {       // el 000 lo tapa el rotulo del anillo
                const QPointF fuera = screenPos(c.atDistanceAndAzimuth(paso * n * 1.08, az));
                label(painter, bordeDeVista(pc, fuera), MapDecor::formatBearing(az), r.color);
            }
        }
    }

    painter->setPen(QPen(r.color, 1.6));
    painter->drawLine(pc + QPointF(-6, 0), pc + QPointF(6, 0));
    painter->drawLine(pc + QPointF(0, -6), pc + QPointF(0, 6));
    if (r.targetId < 0) {                    // punto fijo (radar, observador): un cuadrado
        painter->drawRect(QRectF(pc - QPointF(3.5, 3.5), QSizeF(7.0, 7.0)));
    }
    if (!r.name.isEmpty()) {
        const QFontMetricsF fm(painter->font());
        label(painter, pc + QPointF(fm.horizontalAdvance(r.name) / 2.0 + 14.0, 12.0), r.name, r.color);
    }
}

// Marcacion: trazo geodesico con halo, flecha en el destino y el rotulo
// "045° · 5.2 NM" en el medio.
void RangeLayer::drawBearing(QCPPainter *painter, const BearingLine &l) const
{
    QGeoCoordinate a, b;
    if (!ends(l, &a, &b) || a.distanceTo(b) < 1.0)
        return;
    QPolygonF linea;
    for (const QGeoCoordinate &p : geodesica(a, b))
        linea.append(screenPos(p));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(255, 255, 255, 170), 4.0, Qt::SolidLine, Qt::RoundCap));
    painter->drawPolyline(linea);
    painter->setPen(QPen(l.color, 1.8, Qt::SolidLine, Qt::RoundCap));
    painter->drawPolyline(linea);

    // Punta de flecha en el destino, orientada por el ultimo tramo en pantalla.
    if (linea.size() >= 2) {
        const QPointF fin = linea.last();
        const QLineF tramo(linea.at(linea.size() - 2), fin);
        if (tramo.length() > 0.5) {
            const double ang = std::atan2(tramo.dy(), tramo.dx());
            const QPointF p1 = fin - QPointF(std::cos(ang - 0.45), std::sin(ang - 0.45)) * 10.0;
            const QPointF p2 = fin - QPointF(std::cos(ang + 0.45), std::sin(ang + 0.45)) * 10.0;
            painter->setBrush(l.color);
            painter->drawPolygon(QPolygonF({ fin, p1, p2 }));
        }
    }
    if (l.label) {
        const QString txt = MapDecor::formatBearing(a.azimuthTo(b)) + QStringLiteral(" · ")
                          + MapDecor::formatDistance(a.distanceTo(b), m_units);
        label(painter, linea.at(linea.size() / 2), txt, l.color);
    }
}

} // namespace libmapa
