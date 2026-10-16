// Tarjeta del clic izquierdo y ayuda de gestos del ratón.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).
//
// Clic izquierdo sobre un elemento = TARJETA: un popup con sus datos y las
// acciones más habituales (centrar, anillos, marcación…). El menú completo sigue
// en el clic derecho (VentanaContexto.cpp) y la tarjeta lo ofrece con «Más…».
// Se muestra UN solo elemento, con la misma prioridad que el menú: objetivo,
// entidad (punto, zona, línea) y, si no hay nada de eso, la marca pinchada.

#include "Ventana.h"

#include "libmapa/Coordinates.h"

#include <QApplication>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

using namespace libmapa;

namespace {

// Una fila «Etiqueta: valor» de la tarjeta (el valor admite HTML ya escapado).
QString fila(const QString &etiqueta, const QString &valorHtml)
{
    return QStringLiteral("<tr><td style='color:#666;padding-right:8px'>%1</td><td>%2</td></tr>")
        .arg(etiqueta.toHtmlEscaped(), valorHtml);
}

// Filas con los atributos libres de un elemento (como mucho 6, para que la
// tarjeta no crezca sin límite).
QString filasAtributos(const QVariantMap &atributos)
{
    QString html;
    int n = 0;
    for (auto it = atributos.constBegin(); it != atributos.constEnd() && n < 6; ++it, ++n)
        html += fila(it.key(), it.value().toString().toHtmlEscaped());
    return html;
}

// Acción de la tarjeta: texto del botón y qué hacer al pulsarlo.
struct Accion
{
    QString texto;
    std::function<void()> hacer;
};

} // namespace

// Monta y muestra la tarjeta del elemento bajo el cursor en \a global. Los
// botones cierran la tarjeta ANTES de actuar (con un singleShot): varias acciones
// abren diálogos o esperan un clic, y no deben hacerlo bajo un popup abierto.
void Ventana::mostrarTarjeta(const MapHit &hit, const QPoint &global)
{
    QString titulo;
    QString filas;
    QVector<Accion> acciones;

    const auto fmt = [this](const QGeoCoordinate &c) { return formatCoordinate(c, m_formatoCoord).toHtmlEscaped(); };

    if (hit.targetId >= 0 && m_mapa->target(hit.targetId)) {
        const qint64 id = hit.targetId;
        const MapTarget t = *m_mapa->target(id);
        titulo = t.kind.isEmpty() ? nombreObjetivo(id) : tr("%1 (%2)").arg(nombreObjetivo(id), t.kind);
        filas += fila(tr("Posición"), fmt(t.position));
        filas += fila(tr("Rumbo"), tr("%1°").arg(qRound(t.headingDeg) % 360, 3, 10, QLatin1Char('0')));
        if (t.speed > 0.0)
            filas += fila(tr("Velocidad"), tr("%1 kn").arg(t.speed * 3600.0 / 1852.0, 0, 'f', 1));
        filas += filasAtributos(t.attributes);
        const qint64 anillos = anillosDe(id, -1);
        if (anillos >= 0)
            filas += fila(tr("Anillos"), tr("sí (%1)").arg(m_mapa->rangeRings(anillos) ? m_mapa->rangeRings(anillos)->count : 0));
        const int marcas = int(marcacionesDe(id, -1).size());
        if (marcas > 0)
            filas += fila(tr("Marcaciones"), QString::number(marcas));
        acciones.append({ tr("Centrar"), [this, id] {
                              if (const auto o = m_mapa->target(id))
                                  m_mapa->setCenter(o->position);
                          } });
        if (anillos >= 0)
            acciones.append({ tr("Quitar anillos"), [this, anillos] { m_mapa->removeRangeMark(anillos); } });
        else
            acciones.append({ tr("Anillos"), [this, id] {
                                  RangeRings r = m_anillos;
                                  r.targetId = id;
                                  r.featureId = -1;
                                  r.name.clear();
                                  ponerAnillosA(r, tr("el objetivo %1").arg(nombreObjetivo(id)));
                              } });
        acciones.append({ tr("Marcación…"), [this, id] {
                              m_ebl = BearingLine();
                              m_ebl.fromTarget = id;
                              iniciarPickMedida(Pick::DestinoEbl,
                                                tr("Pincha el destino de la marcación: un objetivo, un punto o cualquier lugar (Esc cancela)…"));
                          } });
    } else if (hit.featureId >= 0 && m_mapa->feature(hit.featureId)) {
        const qint64 id = hit.featureId;
        const MapFeature f = *m_mapa->feature(id);
        const QString nombre = f.name.isEmpty() ? tr("Entidad %1").arg(id) : f.name;
        titulo = f.type.isEmpty() ? nombre : tr("%1 (%2)").arg(nombre, f.type);
        const bool pin = f.layerId == kCapaPuntos || f.layerId.startsWith(QLatin1String("elev_"));
        if (f.kind == GeometryKind::Point) {
            filas += fila(tr("Capa"), f.layerId.toHtmlEscaped());
            filas += fila(tr("Posición"), fmt(f.position()));
            if (m_demActivo) {
                const double cota = m_mapa->elevationAt(f.position());
                if (!std::isnan(cota))
                    filas += fila(tr("Cota"), tr("%1 m").arg(cota, 0, 'f', 0));
            }
            if (!f.description.isEmpty())
                filas += fila(tr("Nota"), f.description.toHtmlEscaped());
            filas += filasAtributos(f.attributes);
            const qint64 anillos = pin ? -1 : anillosDe(-1, id);
            if (anillos >= 0)
                filas += fila(tr("Anillos"), tr("sí (%1)").arg(m_mapa->rangeRings(anillos) ? m_mapa->rangeRings(anillos)->count : 0));
            const int marcas = pin ? 0 : int(marcacionesDe(-1, id).size());
            if (marcas > 0)
                filas += fila(tr("Marcaciones"), QString::number(marcas));
            acciones.append({ tr("Centrar"), [this, pos = f.position()] { m_mapa->setCenter(pos); } });
            if (!pin) {
                if (anillos >= 0)
                    acciones.append({ tr("Quitar anillos"), [this, anillos] { m_mapa->removeRangeMark(anillos); } });
                else
                    acciones.append({ tr("Anillos"), [this, id] {
                                          RangeRings r = m_anillos;
                                          r.targetId = -1;
                                          r.featureId = id;
                                          r.name.clear();
                                          ponerAnillosA(r, tr("el punto «%1»").arg(nombrePunto(id)));
                                      } });
                acciones.append({ tr("Marcación…"), [this, id] {
                                      m_ebl = BearingLine();
                                      m_ebl.fromFeature = id;
                                      iniciarPickMedida(Pick::DestinoEbl,
                                                        tr("Pincha el destino de la marcación: un objetivo, un punto o cualquier lugar (Esc cancela)…"));
                                  } });
            }
        } else {
            double area = 0.0, largo = 0.0;
            int vertices = 0;
            const bool poli = f.kind == GeometryKind::Polygon;
            for (const auto &parte : f.outlines()) {
                vertices += int(parte.size());
                if (poli)
                    area += polygonArea(parte);
                largo += pathLength(parte, poli);
            }
            filas += fila(tr("Capa"), f.layerId.toHtmlEscaped());
            filas += fila(tr("Vértices"), QString::number(vertices));
            if (poli) {
                filas += fila(tr("Superficie"), area >= 1e6 ? tr("%1 km²").arg(area / 1e6, 0, 'f', 2)
                                                            : tr("%1 ha").arg(area / 1e4, 0, 'f', 2));
                filas += fila(tr("Perímetro"), textoDistancia(largo).toHtmlEscaped());
                if (m_mapa->isZoneWatched(id))
                    filas += fila(tr("Vigilada"), tr("sí · %1 objetivos dentro").arg(int(m_mapa->targetsInZone(id).size())));
            } else {
                filas += fila(tr("Longitud"), textoDistancia(largo).toHtmlEscaped());
            }
            filas += filasAtributos(f.attributes);
            acciones.append({ tr("Encuadrar"), [this, f] {
                                  QVector<QGeoCoordinate> todos;
                                  for (const auto &parte : f.outlines())
                                      todos += parte;
                                  QGeoCoordinate no, se;
                                  if (recuadroDe(todos, &no, &se))
                                      m_mapa->fitBounds(no, se);
                              } });
            if (poli)
                acciones.append({ m_mapa->isZoneWatched(id) ? tr("Dejar de vigilar") : tr("Vigilar zona"), [this, id] {
                                      m_mapa->selectFeature(id);
                                      alternarVigilancia();
                                  } });
        }
    } else if (hit.rangeMarkId >= 0) {
        const qint64 id = hit.rangeMarkId;
        if (const auto r = m_mapa->rangeRings(id)) {
            titulo = r->name.isEmpty() ? tr("Anillos de distancia") : tr("Anillos «%1»").arg(r->name);
            filas += fila(tr("Centro"), fmt(r->center));
            filas += fila(tr("Anillos"), QString::number(r->count));
            filas += fila(tr("Separación"), textoDistancia(m_mapa->rangeRingsSpacing(id)).toHtmlEscaped());
            acciones.append({ tr("Cambiar…"), [this, id] { ponerAnillos(RangeRings(), id); } });
        } else if (const auto l = m_mapa->bearingLine(id)) {
            const QString de = l->fromTarget >= 0 || l->fromFeature >= 0
                ? nombreExtremo(l->fromTarget, l->fromFeature) : formatCoordinate(l->from, m_formatoCoord);
            const QString a = l->toTarget >= 0 || l->toFeature >= 0
                ? nombreExtremo(l->toTarget, l->toFeature) : formatCoordinate(l->to, m_formatoCoord);
            titulo = tr("Marcación");
            filas += fila(tr("De"), de.toHtmlEscaped());
            filas += fila(tr("A"), a.toHtmlEscaped());
            double rumbo = 0.0, dist = 0.0;
            if (m_mapa->bearingLineMeasure(id, &rumbo, &dist)) {
                filas += fila(tr("Rumbo"), tr("%1°").arg(qRound(rumbo) % 360, 3, 10, QLatin1Char('0')));
                filas += fila(tr("Distancia"), textoDistancia(dist).toHtmlEscaped());
            }
        }
        acciones.append({ tr("Quitar"), [this, id] { m_mapa->removeRangeMark(id); } });
    } else {
        return;                         // nada que mostrar: ni grupos ni lugares vacíos
    }
    acciones.append({ tr("Más…"), [this, hit, global] { menuContextual(hit, global); } });

    auto *tarjeta = new QFrame(this, Qt::Popup);
    tarjeta->setAttribute(Qt::WA_DeleteOnClose);
    tarjeta->setFrameShape(QFrame::StyledPanel);
    auto *caja = new QVBoxLayout(tarjeta);
    auto *cab = new QLabel(QStringLiteral("<b>%1</b>").arg(titulo.toHtmlEscaped()), tarjeta);
    caja->addWidget(cab);
    auto *cuerpo = new QLabel(QStringLiteral("<table>%1</table>").arg(filas), tarjeta);
    cuerpo->setTextFormat(Qt::RichText);
    caja->addWidget(cuerpo);
    auto *botones = new QHBoxLayout;
    for (const Accion &ac : acciones) {
        auto *b = new QPushButton(ac.texto, tarjeta);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, tarjeta, hacer = ac.hacer] {
            tarjeta->close();
            QTimer::singleShot(0, this, hacer);
        });
        botones->addWidget(b);
    }
    caja->addLayout(botones);

    // Junto al cursor, sin salirse de la pantalla.
    tarjeta->adjustSize();
    QPoint pos = global + QPoint(12, 12);
    if (const QScreen *pantalla = QGuiApplication::screenAt(global)) {
        const QRect area = pantalla->availableGeometry();
        pos.setX(std::max(area.left(), std::min(pos.x(), area.right() - tarjeta->width())));
        pos.setY(std::max(area.top(), std::min(pos.y(), area.bottom() - tarjeta->height())));
    }
    tarjeta->move(pos);
    tarjeta->show();
}

// Diálogo con todos los gestos del ratón y del teclado sobre el mapa.
void Ventana::mostrarAyudaRaton()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Ratón y teclado"));
    auto *caja = new QVBoxLayout(&dlg);
    auto *txt = new QLabel(&dlg);
    txt->setTextFormat(Qt::RichText);
    txt->setText(tr(
        "<h3>Navegar</h3><table cellspacing='4'>"
        "<tr><td><b>Arrastrar</b></td><td>desplazar el mapa (también sobre un objetivo)</td></tr>"
        "<tr><td><b>Botón central</b></td><td>desplazar con cualquier herramienta</td></tr>"
        "<tr><td><b>Rueda</b></td><td>zoom hacia el cursor (los toques finos se acumulan)</td></tr>"
        "<tr><td><b>Doble clic</b></td><td>acercar un nivel en un lugar vacío</td></tr>"
        "<tr><td><b>Clic</b></td><td>tarjeta del elemento (datos y acciones rápidas); en vacío deselecciona</td></tr>"
        "<tr><td><b>Clic derecho</b></td><td>menú completo del elemento o del lugar</td></tr>"
        "<tr><td><b>Flechas · + / −</b></td><td>desplazar · zoom</td></tr>"
        "</table>"
        "<h3>Dibujar y medir</h3><table cellspacing='4'>"
        "<tr><td><b>Clic</b></td><td>añadir un vértice o punto</td></tr>"
        "<tr><td><b>Doble clic · Intro</b></td><td>terminar la figura</td></tr>"
        "<tr><td><b>Esc · clic derecho</b></td><td>cancelar la operación en curso</td></tr>"
        "<tr><td><b>Retroceso</b></td><td>quitar el último vértice</td></tr>"
        "</table>"
        "<h3>Editar</h3><table cellspacing='4'>"
        "<tr><td><b>Arrastrar un tirador</b></td><td>mover un vértice (hay un margen de 4 px contra el temblor)</td></tr>"
        "<tr><td><b>Ctrl + clic</b></td><td>añadir un vértice en el lado pinchado</td></tr>"
        "<tr><td><b>Clic derecho en un tirador</b></td><td>borrar ese vértice</td></tr>"
        "<tr><td><b>Supr</b></td><td>borrar la selección</td></tr>"
        "<tr><td><b>Capas bloqueadas</b></td><td>no se editan: la barra de estado lo explica</td></tr>"
        "</table>"
        "<h3>Teclado</h3><table cellspacing='4'>"
        "<tr><td><b>Tecla de menú · Mayús+F10</b></td><td>menú contextual en el centro del mapa</td></tr>"
        "</table>"));
    caja->addWidget(txt);
    auto *cerrar = new QPushButton(tr("Cerrar"), &dlg);
    connect(cerrar, &QPushButton::clicked, &dlg, &QDialog::accept);
    caja->addWidget(cerrar, 0, Qt::AlignRight);
    dlg.exec();
}
