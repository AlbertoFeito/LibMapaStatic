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
constexpr int kMaxAnillos = 100;
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

// Punto de la polilinea a la mitad de su longitud (en pantalla). Con pocos
// puntos (una marcacion de menos de 40 km solo tiene los dos extremos), el punto
// "del medio" de la lista seria el destino, no el centro de la linea.
QPointF puntoMedio(const QPolygonF &linea)
{
    if (linea.size() < 2)
        return linea.isEmpty() ? QPointF() : linea.first();
    double total = 0.0;
    for (int i = 1; i < linea.size(); ++i)
        total += QLineF(linea.at(i - 1), linea.at(i)).length();
    double falta = total / 2.0;
    for (int i = 1; i < linea.size(); ++i) {
        const QLineF tramo(linea.at(i - 1), linea.at(i));
        if (falta <= tramo.length() || i == linea.size() - 1)
            return tramo.length() > 0.0 ? tramo.pointAt(falta / tramo.length()) : tramo.p1();
        falta -= tramo.length();
    }
    return linea.last();
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

// Como encontrar la posicion de una entidad punto (anillos con featureId).
void RangeLayer::setFeatureResolver(std::function<QGeoCoordinate(qint64)> resolver)
{
    m_featurePos = std::move(resolver);
}

// Si algun anillo o marcacion cuelga de esa entidad (p. ej. se esta
// arrastrando), se repinta la capa para que la siga.
void RangeLayer::featureChanged(qint64 id)
{
    for (const RangeRings &r : m_rings) {
        if (r.targetId < 0 && r.featureId == id) {
            repintar();
            return;
        }
    }
    for (const BearingLine &l : m_bearings) {
        if ((l.fromTarget < 0 && l.fromFeature == id) || (l.toTarget < 0 && l.toFeature == id)) {
            repintar();
            return;
        }
    }
}

// Extremo de una marcacion: su objetivo, su entidad punto o el punto fijo.
QGeoCoordinate RangeLayer::extremo(const QGeoCoordinate &fijo, qint64 targetId, qint64 featureId) const
{
    if (targetId >= 0)
        return resolve(QGeoCoordinate(), targetId);
    if (featureId >= 0)
        return m_featurePos ? m_featurePos(featureId) : QGeoCoordinate();
    return fijo;
}

// Centro actual de unos anillos: su objetivo, su entidad o el punto fijo.
QGeoCoordinate RangeLayer::centro(const RangeRings &r) const
{
    if (r.targetId >= 0)
        return resolve(QGeoCoordinate(), r.targetId);
    if (r.featureId >= 0)
        return m_featurePos ? m_featurePos(r.featureId) : QGeoCoordinate();
    return r.center;
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
    if (l.fromTarget < 0 && l.toTarget < 0 && l.fromFeature >= 0 && l.fromFeature == l.toFeature)
        return true;
    return l.fromTarget < 0 && l.toTarget < 0 && l.fromFeature < 0 && l.toFeature < 0
        && l.from.isValid() && l.to.isValid() && l.from.distanceTo(l.to) < 1.0;
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
    const QGeoCoordinate c = centro(*it);
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
        const QGeoCoordinate c = centro(*it);
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
    const QGeoCoordinate a = extremo(l.from, l.fromTarget, l.fromFeature);
    if (!a.isValid())
        return false;
    QGeoCoordinate b;
    if (l.toTarget >= 0 || l.toFeature >= 0 || l.to.isValid())
        b = extremo(l.to, l.toTarget, l.toFeature);
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

// Anota un rotulo para pintarlo despues, por encima de todo (drawLabels). Se
// pinta aparte para que ni los objetivos ni las entidades lo tapen.
void RangeLayer::label(const QPointF &anchor, const QString &txt, const QColor &color,
                       const QPointF &offset, qint64 bearingId, double opacity) const
{
    m_pending.append({anchor, txt, color, offset, bearingId, opacity});
}

// Recuerda donde esta el raton y, si ahora esta sobre otro rotulo suave (o ya no esta sobre
// ninguno), repinta la capa para destacarlo. Sin cambio no hace nada: se llama en cada
// movimiento del raton.
void RangeLayer::setHoverPixel(const QPointF &pixel)
{
    m_hoverPx = pixel;
    QRectF nueva;
    for (const QRectF &b : m_softBoxes)
        if (b.adjusted(-3.0, -3.0, 3.0, 3.0).contains(pixel)) {
            nueva = b;
            break;
        }
    if (nueva == m_hoverBox)
        return;
    m_hoverBox = nueva;
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

// Pinta los rotulos anotados por draw(): cada uno centrado en anclaje + offset
// sobre una caja clara (se lee en cualquier fondo). Si esta separado de su
// anclaje, una linea guia del color de la medida los une. Guarda las cajas de
// los de marcacion: de ahi sale bearingLabelAt.
void RangeLayer::drawLabels(QCPPainter *painter) const
{
    m_bearingLabelBoxes.clear();
    m_softBoxes.clear();
    if (m_pending.isEmpty())
        return;
    painter->save();
    QFont f = painter->font();
    f.setPointSizeF(7.5);
    f.setBold(true);
    // Los rotulos suaves (anillos y radiales) son mas pequenos, sin negrita y translucidos; el que
    // tiene el raton encima se pinta el ultimo, opaco y en negrita, para que se lea.
    QFont fSuave = f;
    fSuave.setPointSizeF(6.5);
    fSuave.setBold(false);
    const QFontMetricsF fmDuro(f);
    const QFontMetricsF fmSuave(fSuave);
    QVector<int> orden;
    int destacado = -1;
    for (int i = 0; i < m_pending.size(); ++i) {
        const PendingLabel &p = m_pending.at(i);
        if (p.opacity < 1.0) {
            const QSizeF t(fmSuave.horizontalAdvance(p.text) + 6.0, fmSuave.height() + 1.0);
            const QPointF c = p.anchor + p.offset;
            const QRectF caja(c.x() - t.width() / 2.0, c.y() - t.height() / 2.0, t.width(), t.height());
            m_softBoxes.append(caja);
            if (caja == m_hoverBox)
                destacado = i;
            if (caja != m_hoverBox)
                orden.append(i);
        } else {
            orden.append(i);
        }
    }
    if (destacado >= 0)
        orden.append(destacado);
    for (const int idx : orden) {
        const PendingLabel &p = m_pending.at(idx);
        const bool suave = p.opacity < 1.0 && idx != destacado;
        const QFont &fuente = suave ? fSuave : f;
        painter->setFont(fuente);
        const QFontMetricsF fm(fuente);
        painter->setOpacity(suave ? std::clamp(p.opacity, 0.1, 1.0) : 1.0);
        const QSizeF t(fm.horizontalAdvance(p.text) + (suave ? 6.0 : 8.0), fm.height() + (suave ? 1.0 : 2.0));
        const QPointF centro = p.anchor + p.offset;
        const QRectF caja(centro.x() - t.width() / 2.0, centro.y() - t.height() / 2.0,
                          t.width(), t.height());
        if (p.offset.manhattanLength() > 1.0) {
            painter->setPen(QPen(p.color, 1.0, Qt::SolidLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(p.anchor, caja.center());
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, suave ? 190 : 235));
        painter->drawRoundedRect(caja, 3.0, 3.0);
        painter->setPen(p.color.darker(130));
        painter->drawText(caja, Qt::AlignCenter, p.text);
        if (p.bearingId >= 0)
            m_bearingLabelBoxes.append({p.bearingId, caja});
    }
    painter->restore();
}

// Marcacion cuyo rotulo esta bajo el pixel: se recorre del ultimo al primero
// porque el ultimo pintado queda encima.
qint64 RangeLayer::bearingLabelAt(const QPointF &pixel) const
{
    for (int i = static_cast<int>(m_bearingLabelBoxes.size()) - 1; i >= 0; --i)
        if (m_bearingLabelBoxes.at(i).second.contains(pixel))
            return m_bearingLabelBoxes.at(i).first;
    return -1;
}

// Mueve el rotulo de una marcacion y repinta la capa.
bool RangeLayer::setBearingLabelOffset(qint64 id, const QPointF &offsetPx)
{
    const auto it = m_bearings.find(id);
    if (it == m_bearings.end())
        return false;
    it->labelOffsetPx = offsetPx;
    repintar();
    return true;
}

// Dibuja primero los anillos y despues las marcaciones (encima). Lo que cuelga
// de un objetivo que ya no existe se omite. Los rotulos no se pintan aqui: se
// anotan y los pinta drawLabels, por encima de objetivos y entidades.
void RangeLayer::draw(QCPPainter *painter)
{
    m_pending.clear();
    if (!parentPlot() || isEmpty())
        return;
    QFont f = painter->font();
    f.setPointSizeF(7.5);
    f.setBold(true);
    painter->setFont(f);
    for (const RangeRings &r : m_rings)
        drawRings(painter, r);
    for (auto it = m_bearings.constBegin(); it != m_bearings.constEnd(); ++it)
        drawBearing(painter, it.key(), it.value());
}

// Anillos geodesicos con su distancia arriba, radiales opcionales con su
// marcacion en el extremo, y una cruz en el centro.
void RangeLayer::drawRings(QCPPainter *painter, const RangeRings &r) const
{
    const QGeoCoordinate c = centro(r);
    const int n = std::clamp(r.count, 1, kMaxAnillos);
    if (!c.isValid())
        return;
    const double paso = pasoEfectivo(r, c);
    if (!(paso > 0.0))
        return;
    const QPointF pc = screenPos(c);
    const bool propio = r.targetId >= 0 || r.featureId >= 0;   // con dueño que lo marca

    // Enfasis (opcional): un anillo de cada N y un radial de cada N grados con
    // otro color y algo mas gruesos, para leer una escala de un vistazo.
    const QColor colorEnfasis = r.majorColor.isValid() ? r.majorColor : r.color.darker(160);
    painter->setBrush(Qt::NoBrush);
    for (int i = 1; i <= n; ++i) {
        QPolygonF anillo;
        for (const QGeoCoordinate &p : MapDecor::geodesicCircle(c, paso * i, 96))
            anillo.append(screenPos(p));
        const bool mayor = r.majorRingEvery > 0 && i % r.majorRingEvery == 0;
        const QColor colorAnillo = mayor ? colorEnfasis : r.color;
        painter->setPen(QPen(QColor(255, 255, 255, 150), 3.0));
        painter->drawPolyline(anillo);
        painter->setPen(QPen(colorAnillo, mayor ? 2.0 : 1.4,
                             i == n ? Qt::SolidLine : Qt::DashLine));
        painter->drawPolyline(anillo);
        if (r.labels && r.distanceLabels)
            label(dentroDeVista(anillo.first(), anillo),
                  MapDecor::formatDistance(paso * i, m_units), colorAnillo, QPointF(), -1,
                  std::clamp(r.labelOpacity, 0.1, 1.0) < 1.0 ? std::clamp(r.labelOpacity, 0.1, 1.0)
                                                             : 1.0);
    }

    if (r.spokeEveryDeg > 0) {
        const int salto = std::clamp(r.spokeEveryDeg, 5, 180);
        const QColor colorRadial = r.spokeColor.isValid() ? r.spokeColor : r.color;
        for (int az = 0; az < 360; az += salto) {
            const bool mayor = r.majorSpokeEveryDeg > 0 && az % r.majorSpokeEveryDeg == 0;
            const QColor colorEste = mayor ? colorEnfasis : colorRadial;
            const QPointF fin = screenPos(c.atDistanceAndAzimuth(paso * n, az));
            painter->setPen(QPen(colorEste, mayor ? 1.6 : 1.0, Qt::DotLine));
            // Con dueño (objetivo o entidad) el radial empieza fuera de su
            // simbolo, para no taparlo.
            const QLineF radial(pc, fin);
            const double hueco = propio ? 14.0 : 0.0;
            if (radial.length() > hueco)
                painter->drawLine(radial.pointAt(hueco / radial.length()), fin);
            if (r.labels && r.bearingLabels && az != 0) {   // el 000 lo tapa el rotulo del anillo
                const QPointF fuera = screenPos(c.atDistanceAndAzimuth(paso * n * 1.08, az));
                label(bordeDeVista(pc, fuera), MapDecor::formatBearing(az), colorEste, QPointF(), -1,
                      std::clamp(r.labelOpacity, 0.1, 1.0));
            }
        }
    }

    if (propio)
        return;                              // su objetivo o su entidad ya marcan el centro
    painter->setPen(QPen(r.color, 1.6));
    painter->drawLine(pc + QPointF(-6, 0), pc + QPointF(6, 0));
    painter->drawLine(pc + QPointF(0, -6), pc + QPointF(0, 6));
    painter->drawRect(QRectF(pc - QPointF(3.5, 3.5), QSizeF(7.0, 7.0)));   // punto fijo
    if (!r.name.isEmpty()) {
        const QFontMetricsF fm(painter->font());
        label(pc + QPointF(fm.horizontalAdvance(r.name) / 2.0 + 14.0, 12.0), r.name, r.color);
    }
}

// Marcacion: trazo geodesico con halo, flecha en el destino y el rotulo
// "045° · 5.2 NM" en el medio.
void RangeLayer::drawBearing(QCPPainter *painter, qint64 id, const BearingLine &l) const
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
        label(puntoMedio(linea), txt, l.color, l.labelOffsetPx, id);
    }
}

} // namespace libmapa
