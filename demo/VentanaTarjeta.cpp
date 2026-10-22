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
#include <functional>

#include <QCheckBox>
#include <QCursor>
#include <QDialog>
#include <QHash>
#include <QFrame>
#include <QLabel>
#include <QTextBrowser>
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

namespace {

// Qué es una entidad vectorial, en español y legible: categoría ("Calle"), título
// ("Obispo" o, sin nombre, su tipo: "Carretera principal") y detalles sueltos
// ("sentido único", "puente", "altura 12 m"). Traduce lo habitual de OpenMapTiles;
// lo que no conoce lo deja tal cual en vez de esconderlo.
struct DescripcionVectorial
{
    QString categoria;
    QString titulo;
    QStringList detalles;
};

// Traducción de un valor de "class" (o "subclass"), según la capa en que aparece:
// "residential" es una zona en landuse y una calle en transportation.
QString traducirClase(const QString &capa, const QString &clase)
{
    static const QHash<QString, QString> t = {
        { "transportation:motorway", QObject::tr("Autopista") },
        { "transportation:trunk", QObject::tr("Vía troncal") },
        { "transportation:primary", QObject::tr("Carretera principal") },
        { "transportation:secondary", QObject::tr("Carretera secundaria") },
        { "transportation:tertiary", QObject::tr("Carretera terciaria") },
        { "transportation:minor", QObject::tr("Calle") },
        { "transportation:residential", QObject::tr("Calle residencial") },
        { "transportation:service", QObject::tr("Vía de servicio") },
        { "transportation:track", QObject::tr("Camino") },
        { "transportation:path", QObject::tr("Sendero") },
        { "transportation:pedestrian", QObject::tr("Calle peatonal") },
        { "transportation:rail", QObject::tr("Ferrocarril") },
        { "landuse:residential", QObject::tr("Zona residencial") },
        { "landuse:commercial", QObject::tr("Zona comercial") },
        { "landuse:industrial", QObject::tr("Zona industrial") },
        { "landuse:cemetery", QObject::tr("Cementerio") },
        { "landuse:school", QObject::tr("Centro educativo") },
        { "landuse:hospital", QObject::tr("Hospital") },
        { "landuse:military", QObject::tr("Zona militar") },
        { "landuse:railway", QObject::tr("Zona ferroviaria") },
        { "landuse:pitch", QObject::tr("Campo deportivo") },
        { "landuse:playground", QObject::tr("Parque infantil") },
        { "landcover:grass", QObject::tr("Hierba") },
        { "landcover:wood", QObject::tr("Bosque") },
        { "landcover:farmland", QObject::tr("Cultivos") },
        { "landcover:wetland", QObject::tr("Humedal") },
        { "landcover:sand", QObject::tr("Arena") },
        { "landcover:rock", QObject::tr("Roca") },
        { "water:river", QObject::tr("Río") },
        { "water:lake", QObject::tr("Lago") },
        { "water:ocean", QObject::tr("Mar") },
        { "water:pond", QObject::tr("Estanque") },
        { "waterway:river", QObject::tr("Río") },
        { "waterway:stream", QObject::tr("Arroyo") },
        { "waterway:canal", QObject::tr("Canal") },
        { "poi:hospital", QObject::tr("Hospital") },
        { "poi:doctors", QObject::tr("Consultorio") },
        { "poi:pharmacy", QObject::tr("Farmacia") },
        { "poi:police", QObject::tr("Policía") },
        { "poi:fire_station", QObject::tr("Bomberos") },
        { "poi:fuel", QObject::tr("Gasolinera") },
        { "poi:bank", QObject::tr("Banco") },
        { "poi:atm", QObject::tr("Cajero") },
        { "poi:school", QObject::tr("Escuela") },
        { "poi:college", QObject::tr("Universidad") },
        { "poi:lodging", QObject::tr("Alojamiento") },
        { "poi:restaurant", QObject::tr("Restaurante") },
        { "poi:cafe", QObject::tr("Cafetería") },
        { "poi:bar", QObject::tr("Bar") },
        { "poi:bus", QObject::tr("Parada de autobús") },
        { "poi:place_of_worship", QObject::tr("Lugar de culto") },
        { "poi:theatre", QObject::tr("Teatro") },
        { "poi:cinema", QObject::tr("Cine") },
        { "poi:stadium", QObject::tr("Estadio") },
        { "poi:post", QObject::tr("Correos") },
        { "poi:shop", QObject::tr("Tienda") },
        { "poi:grocery", QObject::tr("Mercado") },
        { "place:city", QObject::tr("Ciudad") },
        { "place:town", QObject::tr("Pueblo") },
        { "place:village", QObject::tr("Aldea") },
        { "place:suburb", QObject::tr("Barrio") },
        { "place:neighbourhood", QObject::tr("Reparto") },
        { "place:island", QObject::tr("Isla") },
    };
    return t.value(capa + QLatin1Char(':') + clase);
}

// Lo que dice una entidad de sí misma: ver DescripcionVectorial.
DescripcionVectorial describirVectorial(const VectorFeatureInfo &h)
{
    static const QHash<QString, QString> capas = {
        { "transportation", QObject::tr("Vía") }, { "transportation_name", QObject::tr("Vía") },
        { "building", QObject::tr("Edificio") }, { "landuse", QObject::tr("Uso del suelo") },
        { "landcover", QObject::tr("Terreno") }, { "water", QObject::tr("Agua") },
        { "waterway", QObject::tr("Agua") }, { "park", QObject::tr("Parque") },
        { "place", QObject::tr("Lugar") }, { "poi", QObject::tr("Punto de interés") },
        { "boundary", QObject::tr("Límite") }, { "aeroway", QObject::tr("Aeropuerto") },
        { "mountain_peak", QObject::tr("Cima") }, { "housenumber", QObject::tr("Portal") },
    };
    DescripcionVectorial d;
    d.categoria = capas.value(h.layer, h.layer);
    const QString clase = h.attributes.value(QStringLiteral("class")).toString();
    const QString traducida = traducirClase(h.layer, clase);
    const QString tipo = !traducida.isEmpty() ? traducida : clase;
    d.titulo = h.name();
    if (d.titulo.isEmpty())
        d.titulo = !tipo.isEmpty() ? tipo : (h.layer == QLatin1String("building") ? QObject::tr("Edificio") : d.categoria);
    else if (!tipo.isEmpty() && tipo != d.titulo)
        d.detalles << tipo.toLower();

    const auto txt = [&h](const char *k) { return h.attributes.value(QLatin1String(k)).toString(); };
    if (txt("oneway") == QLatin1String("1"))
        d.detalles << QObject::tr("sentido único");
    if (txt("brunnel") == QLatin1String("bridge"))
        d.detalles << QObject::tr("puente");
    else if (txt("brunnel") == QLatin1String("tunnel"))
        d.detalles << QObject::tr("túnel");
    else if (txt("brunnel") == QLatin1String("ford"))
        d.detalles << QObject::tr("vado");
    if (!txt("surface").isEmpty())
        d.detalles << QObject::tr("firme %1").arg(txt("surface"));
    if (!txt("render_height").isEmpty())
        d.detalles << QObject::tr("altura %1 m").arg(txt("render_height"));
    if (!txt("ele").isEmpty())
        d.detalles << QObject::tr("altitud %1 m").arg(txt("ele"));
    if (!txt("admin_level").isEmpty())
        d.detalles << QObject::tr("nivel administrativo %1").arg(txt("admin_level"));
    return d;
}

} // namespace

namespace {

// Una línea de «¿Qué hay aquí?»: la entidad más cercana de un grupo de tramos
// iguales (misma capa, tipo, nombre y geometría) y cuántos tramos eran.
struct LineaVectorial
{
    VectorFeatureInfo h;
    int tramos = 1;
};

// Clave de agrupación: dos tramos de la misma calle son «la misma cosa».
QString claveVectorial(const VectorFeatureInfo &h)
{
    return h.layer + QLatin1Char('|') + h.attributes.value(QStringLiteral("class")).toString()
           + QLatin1Char('|') + h.name() + QLatin1Char('|') + h.geometry;
}

// Agrupa los tramos repetidos conservando el orden (y por tanto el más cercano primero).
QVector<LineaVectorial> agruparVectorial(const QVector<VectorFeatureInfo> &hits)
{
    QVector<LineaVectorial> lineas;
    for (const VectorFeatureInfo &h : hits) {
        bool repetida = false;
        for (LineaVectorial &l : lineas) {
            if (claveVectorial(l.h) == claveVectorial(h)) {
                ++l.tramos;
                repetida = true;
                break;
            }
        }
        if (!repetida)
            lineas.append({ h, 1 });
    }
    return lineas;
}

// HTML de las líneas, en los bloques «Aquí» (lo que contiene el punto) y «Cerca».
// Con \a maximo > 0 solo las primeras \a maximo líneas; \a resto (si no es null) recibe
// cuántas quedaron fuera. \a crudos añade los atributos del MBTiles tal cual.
QString htmlVectorial(const QVector<LineaVectorial> &lineas, bool crudos, int maximo, int *resto,
                      const std::function<QString(double)> &distancia)
{
    QString html;
    int puestas = 0;
    const auto bloque = [&](const QString &cabecera, bool dentro) {
        QString cuerpo;
        for (const LineaVectorial &l : lineas) {
            if ((l.h.distanceM < 0.5) != dentro)
                continue;
            if (maximo > 0 && puestas >= maximo)
                continue;
            ++puestas;
            const DescripcionVectorial d = describirVectorial(l.h);
            QStringList sub{ d.categoria.toLower() };
            sub << d.detalles;
            if (l.tramos > 1)
                sub << QObject::tr("%1 tramos").arg(l.tramos);
            if (!dentro)
                sub << QObject::tr("a %1").arg(distancia(l.h.distanceM));
            cuerpo += QStringLiteral("<p style='margin:0 0 7px 0'><b>%1</b><br>"
                                     "<span style='color:gray'>%2</span>")
                          .arg(d.titulo.toHtmlEscaped(), sub.join(QStringLiteral(" · ")).toHtmlEscaped());
            if (crudos) {
                QStringList kv;
                for (auto it = l.h.attributes.constBegin(); it != l.h.attributes.constEnd(); ++it)
                    if (!it.key().startsWith(QLatin1String("name:")))
                        kv << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
                cuerpo += QStringLiteral("<br><span style='color:gray;font-size:small'>%1</span>")
                              .arg(kv.join(QStringLiteral("  ")).toHtmlEscaped());
            }
            cuerpo += QStringLiteral("</p>");
        }
        if (!cuerpo.isEmpty())
            html += QStringLiteral("<h4 style='margin:8px 0 4px 0'>%1</h4>").arg(cabecera.toHtmlEscaped()) + cuerpo;
    };
    bloque(QObject::tr("Aquí"), true);
    bloque(QObject::tr("Cerca"), false);
    if (resto)
        *resto = int(lineas.size()) - puestas;
    return html;
}

} // namespace

// «¿Qué hay aquí?» completo: lo que el MBTiles vectorial dice del punto, en lenguaje
// llano, en una ventana con todas las líneas. Los atributos crudos del MBTiles
// (claves en inglés) quedan tras una casilla, para quien los quiera.
void Ventana::queHayAqui(const QGeoCoordinate &p)
{
    const QVector<LineaVectorial> lineas = agruparVectorial(m_mapa->vectorFeaturesAt(p));
    const std::function<QString(double)> distancia = [this](double m) { return textoDistancia(m); };

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("¿Qué hay aquí?"));
    auto *caja = new QVBoxLayout(dlg);
    auto *coord = new QLabel(formatCoordinate(p, m_formatoCoord).toHtmlEscaped(), dlg);
    coord->setTextInteractionFlags(Qt::TextSelectableByMouse);
    caja->addWidget(coord);
    auto *txt = new QTextBrowser(dlg);
    txt->setFrameShape(QFrame::NoFrame);
    txt->setMinimumSize(400, 260);
    caja->addWidget(txt, 1);
    auto *crudos = new QCheckBox(tr("Mostrar todos los atributos del mapa"), dlg);
    caja->addWidget(crudos);
    auto *cerrar = new QPushButton(tr("Cerrar"), dlg);
    connect(cerrar, &QPushButton::clicked, dlg, &QDialog::close);
    caja->addWidget(cerrar, 0, Qt::AlignRight);

    // Se vuelve a componer al marcar o desmarcar la casilla.
    const auto componer = [txt, lineas, crudos, distancia] {
        QString html = htmlVectorial(lineas, crudos->isChecked(), 0, nullptr, distancia);
        if (html.isEmpty())
            html = QObject::tr("<p>No hay nada vectorial cerca de este punto. Hace falta un MBTiles "
                               "vectorial como base o como capa híbrida.</p>");
        txt->setHtml(html);
    };
    connect(crudos, &QCheckBox::toggled, dlg, componer);
    componer();
    dlg->show();
}

// Clic izquierdo en un lugar vacío del mapa: una tarjeta pequeña junto al cursor con
// lo más relevante del punto (hasta 4 líneas) y un botón «Todo…» para la ventana
// completa; se cierra sola a los 5 s o al alejar el cursor. Si no hay nada vectorial cerca (o la base no es vectorial) no sale nada:
// un clic en el vacío no debe abrir cosas sin motivo.
void Ventana::tarjetaQueHayAqui(const QGeoCoordinate &p, const QPoint &global)
{
    const QVector<LineaVectorial> lineas = agruparVectorial(m_mapa->vectorFeaturesAt(p));
    if (lineas.isEmpty())
        return;
    int resto = 0;
    const QString html = htmlVectorial(lineas, false, 4, &resto,
                                       [this](double m) { return textoDistancia(m); });

    auto *tarjeta = new QFrame(this, Qt::Popup);
    tarjeta->setAttribute(Qt::WA_DeleteOnClose);
    tarjeta->setFrameShape(QFrame::StyledPanel);
    auto *caja = new QVBoxLayout(tarjeta);
    auto *cuerpo = new QLabel(html, tarjeta);
    cuerpo->setTextFormat(Qt::RichText);
    caja->addWidget(cuerpo);
    auto *todo = new QPushButton(resto > 0 ? tr("Todo… (%1 más)").arg(resto) : tr("Todo…"), tarjeta);
    todo->setAutoDefault(false);
    connect(todo, &QPushButton::clicked, this, [this, tarjeta, p] {
        tarjeta->close();
        QTimer::singleShot(0, this, [this, p] { queHayAqui(p); });
    });
    caja->addWidget(todo, 0, Qt::AlignRight);

    // Junto al cursor, sin salirse de la pantalla (igual que la tarjeta de los elementos).
    tarjeta->adjustSize();
    QPoint pos = global + QPoint(12, 12);
    if (const QScreen *pantalla = QGuiApplication::screenAt(global)) {
        const QRect area = pantalla->availableGeometry();
        pos.setX(std::max(area.left(), std::min(pos.x(), area.right() - tarjeta->width())));
        pos.setY(std::max(area.top(), std::min(pos.y(), area.bottom() - tarjeta->height())));
    }
    tarjeta->move(pos);
    tarjeta->show();

    // Se cierra sola a los 5 s, y en cuanto el cursor se aleja de ella. Un Popup tiene
    // el ratón capturado, así que no llega un «leave» fiable: se vigila la posición del
    // cursor. El margen de 30 px cubre el desplazamiento con que sale junto al cursor
    // (12 px) y el temblor del ratón; los temporizadores son hijos de la tarjeta y
    // mueren con ella.
    auto *plazo = new QTimer(tarjeta);
    plazo->setSingleShot(true);
    connect(plazo, &QTimer::timeout, tarjeta, &QWidget::close);
    plazo->start(5000);
    auto *vigia = new QTimer(tarjeta);
    connect(vigia, &QTimer::timeout, tarjeta, [tarjeta] {
        if (!tarjeta->frameGeometry().adjusted(-30, -30, 30, 30).contains(QCursor::pos()))
            tarjeta->close();
    });
    vigia->start(100);
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
        "<tr><td><b>Clic</b></td><td>tarjeta del elemento (datos y acciones rápidas); en vacío deselecciona y, con mapa vectorial, tarjeta «¿Qué hay aquí?»</td></tr>"
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
