#include "vector/VectorTileRenderer.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <utility>

namespace libmapa {

namespace {

// Un rótulo pendiente: se colocan todos al final, encima de la geometría. Si
// \c line tiene puntos, el texto va A LO LARGO de esa línea (una calle); si no,
// sobre \c anchor (un punto).
struct LabelCandidate
{
    QPointF anchor;          // en píxeles de la tesela
    QString text;
    const VectorRule *rule;
    QVector<QPointF> line;   // en píxeles de la tesela
};

// Una letra de un rótulo a lo largo de una línea: su centro y su ángulo.
struct Glyph
{
    QString text;
    QPointF center;
    double angleDeg;
    double advance;
};

// Punto y ángulo (en grados) a la distancia \a d desde el inicio de la línea.
// \a cum son las longitudes acumuladas de \a pts (cum[0] = 0).
bool pointAt(const QVector<QPointF> &pts, const QVector<double> &cum, double d,
             QPointF *at, double *angleDeg)
{
    for (int i = 1; i < pts.size(); ++i) {
        if (d <= cum[i] || i == pts.size() - 1) {
            const double seg = cum[i] - cum[i - 1];
            if (seg <= 0)
                continue;
            const double t = qBound(0.0, (d - cum[i - 1]) / seg, 1.0);
            const QPointF a = pts[i - 1], b = pts[i];
            *at = a + (b - a) * t;
            *angleDeg = std::atan2(b.y() - a.y(), b.x() - a.x()) * 180.0 / 3.14159265358979323846;
            return true;
        }
    }
    return false;
}

/*
 * Coloca un rótulo a lo largo de una línea, letra a letra, empezando en \a start
 * (px desde el inicio). Falla si la línea dobla demasiado bajo el texto (más de
 * 35° entre letras seguidas: se leería mal) o si alguna letra se sale de la
 * tesela (la vecina no lo terminaría: mejor no ponerlo que cortarlo).
 */
bool layoutAlong(const QVector<QPointF> &pts, const QVector<double> &cum, double start,
                 const QString &text, const QFontMetricsF &fm, int sizePx, QVector<Glyph> *out)
{
    out->clear();
    const double h = fm.height();
    double d = start;
    double prevAngle = 0.0;
    for (int i = 0; i < text.size(); ++i) {
        const QString ch = text.mid(i, 1);
        const double adv = fm.horizontalAdvance(ch);
        QPointF c;
        double ang = 0.0;
        if (!pointAt(pts, cum, d + adv / 2.0, &c, &ang))
            return false;
        if (i > 0) {
            double diff = std::fabs(ang - prevAngle);
            if (diff > 180.0)
                diff = 360.0 - diff;
            if (diff > 35.0)
                return false;
        }
        const double m = h / 2.0 + 2.0;
        if (c.x() < m || c.y() < m || c.x() > sizePx - m || c.y() > sizePx - m)
            return false;
        out->append(Glyph{ ch, c, ang, adv });
        prevAngle = ang;
        d += adv;
    }
    return true;
}

// Índices (en la tabla de claves de la capa) de las claves de un filtro. Una
// clave que la capa no trae queda en -1: su valor será siempre inválido.
QVector<int> keyIndices(const mvt::Layer &layer,
                        const QVector<QPair<QString, QStringList>> &filter)
{
    QVector<int> idx;
    idx.reserve(filter.size());
    for (const auto &f : filter)
        idx.append(layer.keyIndex(f.first));
    return idx;
}

// ¿El valor de la propiedad está en la lista? Se compara como texto, así un
// filtro "1" casa con un entero 1 y "true" con un booleano.
bool valueIn(const QVariant &v, const QStringList &values)
{
    if (!v.isValid())
        return false;
    const QString s = v.userType() == QMetaType::Bool
                          ? (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                          : v.toString();
    return values.contains(s);
}

// Filtro y exclusiones de la regla sobre una entidad.
bool matches(const mvt::Layer &layer, const mvt::Feature &f, const VectorRule &rule,
             const QVector<int> &filterIdx, const QVector<int> &excludeIdx)
{
    for (int i = 0; i < rule.filter.size(); ++i)
        if (!valueIn(layer.value(f, filterIdx[i]), rule.filter[i].second))
            return false;
    for (int i = 0; i < rule.exclude.size(); ++i)
        if (valueIn(layer.value(f, excludeIdx[i]), rule.exclude[i].second))
            return false;
    return true;
}

// Primer campo de texto no vacío de la lista de preferencia.
QString labelText(const mvt::Layer &layer, const mvt::Feature &f, const QStringList &fields)
{
    for (const QString &field : fields) {
        const QString s = layer.value(f, field).toString().trimmed();
        if (!s.isEmpty())
            return s;
    }
    return QString();
}

// Pluma de trazo: extremos y uniones redondos (las vías se ven continuas al
// juntar tramos) y, si es discontinua, guiones de unos 5 px con huecos de 3 px
// sea cual sea el grosor (QPen mide el patrón en múltiplos del grosor).
QPen strokePen(const VectorRule &rule, double width)
{
    QPen pen(rule.line, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (rule.dashed) {
        const double w = qMax(width, 0.1);
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({ qMax(5.0 / w, 1.0), qMax(3.0 / w, 1.0) });
    }
    return pen;
}

// ¿Choca \a box con algún rótulo ya puesto?
bool clashes(const QVector<QRectF> &placed, const QRectF &box)
{
    for (const QRectF &o : placed)
        if (o.intersects(box))
            return true;
    return false;
}

/*
 * Rótulo a lo largo de una línea (el nombre de una calle). Se prueba en el
 * centro de la línea y, si no cabe o choca, a un tercio y a dos tercios. El texto
 * se lee siempre de izquierda a derecha: si la línea va «hacia atrás», se le da la
 * vuelta. Cada calle se rotula una vez por tesela (\a done).
 */
bool placeAlongLine(QPainter &p, const LabelCandidate &c, const QFont &font,
                    const QFontMetricsF &fm, int sizePx, QVector<QRectF> *placed,
                    QSet<QString> *done)
{
    if (done->contains(c.text))
        return false;
    QVector<QPointF> pts = c.line;
    if (pts.size() < 2)
        return false;
    if (pts.last().x() < pts.first().x())
        std::reverse(pts.begin(), pts.end());
    QVector<double> cum(pts.size(), 0.0);
    for (int i = 1; i < pts.size(); ++i) {
        const QPointF d = pts[i] - pts[i - 1];
        cum[i] = cum[i - 1] + std::sqrt(d.x() * d.x() + d.y() * d.y());
    }
    const double length = cum.last();
    const double w = fm.horizontalAdvance(c.text);
    if (w + 10.0 > length)
        return false;

    const double h = fm.height();
    QVector<Glyph> glyphs;
    for (double t : { 0.5, 0.3, 0.7 }) {
        const double start = qBound(0.0, t * length - w / 2.0, length - w);
        if (!layoutAlong(pts, cum, start, c.text, fm, sizePx, &glyphs))
            continue;
        QVector<QRectF> boxes;
        for (const Glyph &g : glyphs)
            boxes.append(QRectF(g.center.x() - h / 2.0, g.center.y() - h / 2.0, h, h));
        bool clash = false;
        for (const QRectF &b : boxes)
            if (clashes(*placed, b)) {
                clash = true;
                break;
            }
        if (clash)
            continue;

        // Cada letra en su sitio y girada; el halo y el texto, de una vez.
        QPainterPath path;
        const double baseline = (fm.ascent() - fm.descent()) / 2.0;
        for (const Glyph &g : glyphs) {
            QPainterPath one;
            one.addText(QPointF(-g.advance / 2.0, baseline), font, g.text);
            QTransform tr;
            tr.translate(g.center.x(), g.center.y());
            tr.rotate(g.angleDeg);
            path.addPath(tr.map(one));
        }
        const VectorRule &r = *c.rule;
        p.strokePath(path, QPen(r.haloColor, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.fillPath(path, r.textColor);
        *placed += boxes;
        done->insert(c.text);
        return true;
    }
    return false;
}

// Dibuja el marcador de un punto centrado en \a at, con la forma, el tamaño y el
// color de la regla. El círculo va liso (como siempre); las demás formas llevan un
// contorno fino del color del halo, para que se lean sobre cualquier fondo.
void drawMarker(QPainter &p, const QPointF &at, const VectorRule &r)
{
    const double k = r.pointRadius;
    p.setBrush(r.pointColor);
    if (r.pointShape == PointShape::Circle) {
        p.setPen(Qt::NoPen);
        p.drawEllipse(at, k, k);
        return;
    }
    p.setPen(QPen(r.haloColor, 1.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    switch (r.pointShape) {
    case PointShape::Square:
        p.drawRect(QRectF(at.x() - k, at.y() - k, 2 * k, 2 * k));
        break;
    case PointShape::Triangle: {
        const QPointF tri[3] = { QPointF(at.x(), at.y() - k * 1.15), QPointF(at.x() + k * 1.1, at.y() + k * 0.85),
                                 QPointF(at.x() - k * 1.1, at.y() + k * 0.85) };
        p.drawPolygon(tri, 3);
        break;
    }
    case PointShape::Diamond: {
        const QPointF d[4] = { QPointF(at.x(), at.y() - k * 1.2), QPointF(at.x() + k * 1.2, at.y()),
                               QPointF(at.x(), at.y() + k * 1.2), QPointF(at.x() - k * 1.2, at.y()) };
        p.drawPolygon(d, 4);
        break;
    }
    case PointShape::Cross: {
        const double a = k * 1.25, t = k * 0.45;     // brazo y mitad del grosor
        const QPointF c[12] = { { -t, -a }, { t, -a }, { t, -t }, { a, -t }, { a, t }, { t, t },
                                { t, a }, { -t, a }, { -t, t }, { -a, t }, { -a, -t }, { -t, -t } };
        QPointF pts[12];
        for (int i = 0; i < 12; ++i)
            pts[i] = at + c[i];
        p.drawPolygon(pts, 12);
        break;
    }
    case PointShape::Circle:
        break;
    }
}

// Coloca y pinta los rótulos, en el orden de las reglas: con halo, dentro de la
// tesela y sin solaparse (el primero que llega se queda el sitio).
void drawLabels(QPainter &p, const QVector<LabelCandidate> &labels, int sizePx)
{
    QVector<QRectF> placed;
    QSet<QString> streetsDone;
    const QRectF tileRect(0, 0, sizePx, sizePx);
    for (const LabelCandidate &c : labels) {
        const VectorRule &r = *c.rule;
        QFont font;
        font.setPixelSize(qMax(1, qRound(r.textSize)));
        font.setBold(r.bold);
        font.setItalic(r.italic);
        const QFontMetricsF fm(font);
        if (!c.line.isEmpty()) {
            placeAlongLine(p, c, font, fm, sizePx, &placed, &streetsDone);
            continue;
        }
        const double w = fm.horizontalAdvance(c.text);
        const double h = fm.height();
        if (w + 4 > sizePx)
            continue;

        // Centrado sobre el ancla; si hay punto, encima de él.
        QRectF box(c.anchor.x() - w / 2, c.anchor.y() - h / 2, w, h);
        if (r.pointRadius > 0)
            box.moveBottom(c.anchor.y() - r.pointRadius - 1);
        // Dentro de la tesela con 2 px de margen: se desliza, no se corta.
        box.moveLeft(qBound(2.0, box.left(), sizePx - 2.0 - w));
        box.moveTop(qBound(2.0, box.top(), sizePx - 2.0 - h));
        if (!tileRect.contains(box))
            continue;

        const QRectF guard = box.adjusted(-2, -1, 2, 1);
        if (clashes(placed, guard))
            continue;
        placed.append(guard);

        if (r.pointRadius > 0)
            drawMarker(p, c.anchor, r);
        QPainterPath path;
        path.addText(QPointF(box.left(), box.top() + fm.ascent()), font, c.text);
        p.strokePath(path, QPen(r.haloColor, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.fillPath(path, r.textColor);
    }
}

} // namespace

// Se queda con su copia del estilo: el renderizador vive en el hilo trabajador.
VectorTileRenderer::VectorTileRenderer(VectorStyle style)
    : m_style(std::move(style))
{
}

// Sin saber qué tesela es no se puede usar la máscara: solo fondo y reglas.
QImage VectorTileRenderer::render(const mvt::Tile &tile, int zoom, int sizePx) const
{
    return renderImpl(tile, zoom, nullptr, sizePx);
}

// Con la tesela entera: mar y tierra de la máscara (si la hay) y luego las reglas.
QImage VectorTileRenderer::render(const mvt::Tile &tile, const TileKey &key, int sizePx) const
{
    return renderImpl(tile, key.z, &key, sizePx);
}

// Pinta regla a regla (una sola ruta por regla: un trazo continuo y mucho más
// rápido que una llamada por entidad) y deja los rótulos para el final. Con
// máscara de tierra y tesela conocida, el fondo es el mar y la tierra va encima.
QImage VectorTileRenderer::renderImpl(const mvt::Tile &tile, int zoom, const TileKey *key,
                                      int sizePx) const
{
    QImage img(sizePx, sizePx, QImage::Format_ARGB32_Premultiplied);
    const bool withMask = m_landMask && key;
    img.fill(withMask ? m_style.sea : m_style.background);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (withMask)
        m_landMask->paint(p, key->z, key->x, key->y, sizePx, m_style.background);

    const bool canLabel = qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr;
    QVector<LabelCandidate> labels;

    for (const VectorRule &rule : m_style.rules) {
        if (zoom < rule.minZoom || zoom > rule.maxZoom)
            continue;
        if (!rule.group.isEmpty() && m_hiddenGroups.contains(rule.group))
            continue;
        const mvt::Layer *layer = tile.layer(rule.layer);
        if (!layer || layer->features.isEmpty())
            continue;

        const double k = double(sizePx) / double(layer->extent);
        const QVector<int> filterIdx = keyIndices(*layer, rule.filter);
        const QVector<int> excludeIdx = keyIndices(*layer, rule.exclude);

        // WindingFill y no OddEven: en MVT los anillos exteriores giran en un
        // sentido y los huecos en el otro, así dos polígonos que se solapan en
        // la misma regla no se «agujerean» entre sí.
        QPainterPath areas;
        areas.setFillRule(Qt::WindingFill);
        QPainterPath lines;
        QVector<QPointF> dots;

        for (const mvt::Feature &f : layer->features) {
            if (f.parts.isEmpty() || !rule.appliesTo(zoom, f.type)
                || !matches(*layer, f, rule, filterIdx, excludeIdx))
                continue;

            switch (f.type) {
            case mvt::GeomType::Polygon:
                for (const QVector<QPoint> &ring : f.parts) {
                    areas.moveTo(ring[0].x() * k, ring[0].y() * k);
                    for (int i = 1; i < ring.size(); ++i)
                        areas.lineTo(ring[i].x() * k, ring[i].y() * k);
                    areas.closeSubpath();
                }
                break;
            case mvt::GeomType::LineString:
                // Con rótulo, el nombre va a lo largo de cada tramo (una calle).
                if (!rule.labelFields.isEmpty() && canLabel) {
                    const QString text = labelText(*layer, f, rule.labelFields);
                    if (!text.isEmpty())
                        for (const QVector<QPoint> &part : f.parts) {
                            QVector<QPointF> px;
                            px.reserve(part.size());
                            for (const QPoint &q : part)
                                px.append(QPointF(q.x() * k, q.y() * k));
                            labels.append(LabelCandidate{ QPointF(), text, &rule, px });
                        }
                }
                if (!rule.line.isValid())
                    break;
                for (const QVector<QPoint> &part : f.parts) {
                    lines.moveTo(part[0].x() * k, part[0].y() * k);
                    for (int i = 1; i < part.size(); ++i)
                        lines.lineTo(part[i].x() * k, part[i].y() * k);
                }
                break;
            case mvt::GeomType::Point:
                for (const QPoint &pt : f.parts.first()) {
                    // Solo los de dentro: los del margen son de la tesela vecina.
                    if (pt.x() < 0 || pt.y() < 0 || pt.x() >= layer->extent
                        || pt.y() >= layer->extent)
                        continue;
                    const QPointF at(pt.x() * k, pt.y() * k);
                    if (!rule.labelFields.isEmpty()) {
                        if (!canLabel)
                            continue;
                        const QString text = labelText(*layer, f, rule.labelFields);
                        if (!text.isEmpty())
                            labels.append(LabelCandidate{ at, text, &rule, {} });
                    } else if (rule.pointRadius > 0) {
                        dots.append(at);
                    }
                }
                break;
            case mvt::GeomType::Unknown:
                break;
            }
        }

        const double width = rule.widthAt(zoom);
        if (!areas.isEmpty()) {
            if (rule.fill.isValid())
                p.fillPath(areas, rule.fill);
            if (rule.line.isValid())
                p.strokePath(areas, strokePen(rule, width));
        }
        if (!lines.isEmpty() && rule.line.isValid())
            p.strokePath(lines, strokePen(rule, width));
        if (!dots.isEmpty()) {
            for (const QPointF &d : dots)
                drawMarker(p, d, rule);
        }
    }

    drawLabels(p, labels, sizePx);
    p.end();
    return img;
}

// Atajo: decodificar y pintar. Una tesela vacía (sin capas) da solo el fondo.
QImage VectorTileRenderer::render(const QByteArray &bytes, int zoom, int sizePx,
                                  QString *error) const
{
    mvt::Tile tile;
    if (!mvt::decode(bytes, &tile, error))
        return QImage();
    return render(tile, zoom, sizePx);
}

// Atajo con la tesela entera (para la máscara de tierra).
QImage VectorTileRenderer::render(const QByteArray &bytes, const TileKey &key, int sizePx,
                                  QString *error) const
{
    mvt::Tile tile;
    if (!mvt::decode(bytes, &tile, error))
        return QImage();
    return render(tile, key, sizePx);
}

} // namespace libmapa
