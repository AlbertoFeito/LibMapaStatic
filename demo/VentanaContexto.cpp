// Menú del clic derecho: acciones según lo que haya bajo el cursor (objetivo,
// grupo de objetivos, anillos o marcación, entidad o el mapa sin nada).
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

#include "libmapa/Coordinates.h"

#include <QClipboard>
#include <QGuiApplication>

using namespace libmapa;

// Añade al menú una cabecera (acción desactivada en negrita) que dice sobre
// qué se va a actuar. addSection no vale: en el estilo de Windows no pinta
// el texto.
static void cabecera(QMenu *menu, const QString &texto)
{
    QAction *a = menu->addAction(texto);
    QFont f = a->font();
    f.setBold(true);
    a->setFont(f);
    a->setEnabled(false);
}

// Recuadro que contiene todos los puntos (para encuadrar entidades y grupos),
// con un margen para que lo encuadrado no toque el borde. false si no hay
// ningún punto válido.
static bool recuadro(const QVector<QGeoCoordinate> &puntos, QGeoCoordinate *no, QGeoCoordinate *se)
{
    double n = -90, s = 90, e = -180, o = 180;
    bool alguno = false;
    for (const QGeoCoordinate &c : puntos) {
        if (!c.isValid())
            continue;
        alguno = true;
        n = std::max(n, c.latitude());
        s = std::min(s, c.latitude());
        e = std::max(e, c.longitude());
        o = std::min(o, c.longitude());
    }
    if (!alguno)
        return false;
    const double mLat = std::max((n - s) * 0.15, 0.002), mLon = std::max((e - o) * 0.15, 0.002);
    *no = QGeoCoordinate(std::min(n + mLat, 85.0), o - mLon);
    *se = QGeoCoordinate(std::max(s - mLat, -85.0), e + mLon);
    return true;
}

// Punto de entrada: monta el menú con una sección por cada cosa que haya bajo
// el cursor, en orden de prioridad (objetivo o grupo, anillos/marcación,
// entidad) y, al final, las acciones sobre el punto del mapa (en un submenú
// si ya hay otras secciones, para que el menú no crezca demasiado).
void Ventana::menuContextual(const MapHit &hit, const QPoint &global)
{
    QMenu menu(this);
    bool algo = false;
    if (hit.targetId >= 0) {
        menuObjetivo(&menu, hit.targetId);
        algo = true;
    } else if (!hit.clusterTargets.isEmpty()) {
        menuGrupo(&menu, hit.clusterTargets);
        algo = true;
    }
    if (hit.rangeMarkId >= 0) {
        if (algo)
            menu.addSeparator();
        menuMarca(&menu, hit.rangeMarkId);
        algo = true;
    }
    // La entidad, solo si no hay un objetivo encima (lo habitual es querer
    // actuar sobre lo que se ve arriba; la entidad sigue a un clic de distancia).
    if (hit.featureId >= 0 && hit.targetId < 0 && hit.clusterTargets.isEmpty()) {
        if (algo)
            menu.addSeparator();
        menuEntidad(&menu, hit.featureId);
        algo = true;
    }
    if (algo) {
        menu.addSeparator();
        QMenu *aqui = menu.addMenu(tr("En este punto del mapa"));
        menuPunto(aqui, hit.coordinate);
    } else {
        menuPunto(&menu, hit.coordinate);
    }
    menu.exec(global);
}

// Acciones sobre un objetivo móvil: seleccionar, centrar, anillos, marcación,
// CPA con el seleccionado, filtrar por su tipo, exportar su historial, quitar.
void Ventana::menuObjetivo(QMenu *menu, qint64 id)
{
    const auto t = m_mapa->target(id);
    if (!t)
        return;
    // El rótulo puede tener varias líneas ("UAV 9\nBat 79%"): en el menú, la primera.
    const QString nombre = t->label.isEmpty() ? tr("Objetivo %1").arg(id)
                                              : t->label.section(QLatin1Char('\n'), 0, 0);
    cabecera(menu, t->kind.isEmpty() ? nombre : tr("%1 (%2)").arg(nombre, t->kind));

    connect(menu->addAction(tr("Seleccionar (ver su traza)")), &QAction::triggered, this,
            [this, id] { m_mapa->setSelectedTarget(id); });
    connect(menu->addAction(tr("Centrar en el objetivo")), &QAction::triggered, this, [this, id] {
        if (const auto o = m_mapa->target(id))
            m_mapa->setCenter(o->position);
    });
    menu->addSeparator();

    // Anillos que lo siguen: rápidos (con los últimos ajustes) o con el diálogo.
    connect(menu->addAction(tr("Anillos de distancia en él")), &QAction::triggered, this, [this, id] {
        RangeRings r = m_anillos;
        r.targetId = id;
        r.name.clear();
        m_mapa->addRangeRings(r);
        statusBar()->showMessage(tr("Anillos alrededor del objetivo %1 (lo siguen)").arg(id), 4000);
    });
    connect(menu->addAction(tr("Anillos de distancia en él...")), &QAction::triggered, this, [this, id] {
        m_mapa->setSelectedTarget(id);
        ponerAnillos();
    });
    connect(menu->addAction(tr("Marcación desde él...")), &QAction::triggered, this, [this, id] {
        m_ebl = BearingLine();
        m_ebl.fromTarget = id;
        iniciarPickMedida(Pick::DestinoEbl,
                          tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
    });

    // Con otro objetivo seleccionado: marcación y CPA entre los dos.
    const qint64 sel = m_mapa->selectedTarget();
    const auto otroT = sel >= 0 && sel != id ? m_mapa->target(sel) : std::nullopt;
    if (otroT) {
        const QString otro = otroT->label.isEmpty() ? tr("objetivo %1").arg(sel)
                                                    : otroT->label.section(QLatin1Char('\n'), 0, 0);
        connect(menu->addAction(tr("Marcación desde %1 hasta él").arg(otro)), &QAction::triggered,
                this, [this, sel, id] {
                    BearingLine l;
                    l.fromTarget = sel;
                    l.toTarget = id;
                    m_mapa->addBearingLine(l);
                });
        connect(menu->addAction(tr("CPA/TCPA con %1").arg(otro)), &QAction::triggered, this,
                [this, sel, id, otro, nombre] {
                    const CpaResult c = m_mapa->targetCpa(sel, id);
                    if (!c.valid) {
                        statusBar()->showMessage(tr("Sin velocidad conocida: no se puede calcular el CPA"), 5000);
                        return;
                    }
                    QMessageBox::information(this, tr("CPA / TCPA"),
                        c.timeSeconds < 0
                            ? tr("%1 y %2 ya se están alejando.\nDistancia ahora: %3")
                                  .arg(otro, nombre, textoDistancia(c.currentDistanceMeters))
                            : tr("%1 y %2:\nmáximo acercamiento %3 dentro de %4 min.\nDistancia ahora: %5")
                                  .arg(otro, nombre, textoDistancia(c.distanceMeters))
                                  .arg(c.timeSeconds / 60.0, 0, 'f', 1)
                                  .arg(textoDistancia(c.currentDistanceMeters)));
                });
    }
    menu->addSeparator();

    // Filtro rápido por su tipo (y quitar el filtro si lo hay).
    if (!t->kind.isEmpty()) {
        const QString tipo = t->kind;
        connect(menu->addAction(tr("Mostrar solo objetivos «%1»").arg(tipo)), &QAction::triggered,
                this, [this, tipo] {
                    m_filtro = TargetFilter();
                    m_filtro.kinds << tipo;
                    m_filtro.dim = true;
                    m_mapa->setTargetFilter(m_filtro);
                    actualizarEstado();
                });
    }
    if (m_mapa->hasTargetFilter())
        connect(menu->addAction(tr("Quitar el filtro de objetivos")), &QAction::triggered, this, [this] {
            m_filtro = TargetFilter();
            m_mapa->clearTargetFilter();
            actualizarEstado();
        });
    connect(menu->addAction(tr("Exportar su historial a GPX...")), &QAction::triggered, this,
            [this, id, nombre] {
                const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar historial"),
                    dirTrabajo() + QLatin1Char('/') + nombre + QStringLiteral(".gpx"), tr("GPX (*.gpx)"));
                if (ruta.isEmpty())
                    return;
                QString error;
                if (!m_mapa->exportTargetHistory(ruta, { id }, &error)) {
                    QMessageBox::warning(this, tr("Exportar historial"),
                                         tr("No se pudo exportar:\n%1").arg(error));
                    return;
                }
                m_ultimoDir = QFileInfo(ruta).absolutePath();
                statusBar()->showMessage(tr("Historial de %1 exportado").arg(nombre), 4000);
            });
    connect(menu->addAction(tr("Copiar su posición")), &QAction::triggered, this,
            [this, pos = t->position] {
                QGuiApplication::clipboard()->setText(formatCoordinate(pos, m_formatoCoord));
            });
    menu->addSeparator();
    connect(menu->addAction(tr("Quitar el objetivo")), &QAction::triggered, this,
            [this, id] { m_mapa->removeTarget(id); });
}

// Acciones sobre un grupo de objetivos: ampliarlo o ir a uno de ellos.
void Ventana::menuGrupo(QMenu *menu, const QVector<qint64> &ids)
{
    cabecera(menu, tr("Grupo de %1 objetivos").arg(ids.size()));
    connect(menu->addAction(tr("Ampliar el grupo")), &QAction::triggered, this, [this, ids] {
        QVector<QGeoCoordinate> pos;
        for (const qint64 id : ids)
            if (const auto t = m_mapa->target(id))
                pos.append(t->position);
        QGeoCoordinate no, se;
        if (recuadro(pos, &no, &se))
            m_mapa->fitBounds(no, se);
    });
    // Los primeros, para ir directamente a uno.
    QMenu *sub = menu->addMenu(tr("Ir a un objetivo"));
    const int n = std::min(int(ids.size()), 25);
    for (int i = 0; i < n; ++i) {
        const qint64 id = ids.at(i);
        const auto t = m_mapa->target(id);
        if (!t)
            continue;
        const QString txt = t->label.isEmpty() ? tr("Objetivo %1").arg(id)
                                               : t->label.section(QLatin1Char('\n'), 0, 0);
        connect(sub->addAction(t->kind.isEmpty() ? txt : tr("%1 (%2)").arg(txt, t->kind)),
                &QAction::triggered, this, [this, id] {
                    if (const auto o = m_mapa->target(id)) {
                        m_mapa->setZoom(std::max(m_mapa->zoom(), 11));
                        m_mapa->setCenter(o->position);
                        m_mapa->setSelectedTarget(id);
                    }
                });
    }
    if (ids.size() > n)
        sub->addAction(tr("… y %1 más (amplía el grupo)").arg(ids.size() - n))->setEnabled(false);
}

// Acciones sobre unos anillos (separación automática o fija, radiales,
// quitar) o una marcación (quitar), y la lista de todos.
void Ventana::menuMarca(QMenu *menu, qint64 id)
{
    if (const auto r = m_mapa->rangeRings(id)) {
        const double paso = m_mapa->rangeRingsSpacing(id);
        cabecera(menu, tr("%1 · %2 anillos cada %3")
                           .arg(r->name.isEmpty() ? tr("Anillos") : r->name)
                           .arg(r->count).arg(textoDistancia(paso)));
        QAction *autom = menu->addAction(tr("Separación automática (según el zoom)"));
        autom->setCheckable(true);
        autom->setChecked(!(r->spacingMeters > 0.0));
        connect(autom, &QAction::toggled, this, [this, id, paso](bool on) {
            if (auto rr = m_mapa->rangeRings(id)) {
                // Al fijarla se queda la que se ve ahora.
                rr->spacingMeters = on ? 0.0 : paso;
                m_mapa->updateRangeRings(id, *rr);
            }
        });
        QMenu *rad = menu->addMenu(tr("Radiales"));
        for (const int g : { 0, 10, 15, 30, 45, 90 }) {
            QAction *a = rad->addAction(g == 0 ? tr("Sin radiales") : tr("Cada %1°").arg(g));
            a->setCheckable(true);
            a->setChecked(r->spokeEveryDeg == g);
            connect(a, &QAction::triggered, this, [this, id, g] {
                if (auto rr = m_mapa->rangeRings(id)) {
                    rr->spokeEveryDeg = g;
                    m_mapa->updateRangeRings(id, *rr);
                }
            });
        }
        connect(menu->addAction(tr("Quitar estos anillos")), &QAction::triggered, this,
                [this, id] { m_mapa->removeRangeMark(id); });
    } else if (m_mapa->bearingLine(id)) {
        double rumbo = 0.0, dist = 0.0;
        cabecera(menu, m_mapa->bearingLineMeasure(id, &rumbo, &dist)
                           ? tr("Marcación %1° · %2").arg(qRound(rumbo) % 360, 3, 10, QLatin1Char('0'))
                                                    .arg(textoDistancia(dist))
                           : tr("Marcación"));
        connect(menu->addAction(tr("Quitar esta marcación")), &QAction::triggered, this,
                [this, id] { m_mapa->removeRangeMark(id); });
    }
    connect(menu->addAction(tr("Anillos y marcaciones puestos...")), &QAction::triggered,
            this, &Ventana::gestionarMarcas);
}

// Acciones sobre una entidad: propiedades, encuadrar, editar, medir, vigilar
// como zona (polígonos), anillos en un punto y borrar (si su capa se edita).
void Ventana::menuEntidad(QMenu *menu, qint64 id)
{
    const auto f = m_mapa->feature(id);
    if (!f)
        return;
    const bool editable = capaEditable(f->layerId);
    const QString nombre = f->name.isEmpty() ? tr("Entidad %1").arg(id) : f->name;
    cabecera(menu, f->type.isEmpty() ? nombre : tr("%1 (%2)").arg(nombre, f->type));

    connect(menu->addAction(tr("Seleccionar y ver propiedades")), &QAction::triggered, this, [this, id] {
        m_mapa->selectFeature(id);
        if (m_dock)
            m_dock->show();
    });
    connect(menu->addAction(tr("Encuadrar")), &QAction::triggered, this, [this, id] {
        const auto e = m_mapa->feature(id);
        if (!e)
            return;
        if (e->kind == GeometryKind::Point) {
            m_mapa->setCenter(e->position());
            return;
        }
        QVector<QGeoCoordinate> todos;
        for (const auto &parte : e->outlines())
            todos += parte;
        QGeoCoordinate no, se;
        if (recuadro(todos, &no, &se))
            m_mapa->fitBounds(no, se);
    });
    if (editable && f->kind != GeometryKind::Point)
        connect(menu->addAction(tr("Editar vértices")), &QAction::triggered, this, [this, id] {
            m_mapa->selectFeature(id);
            if (m_accEditar)
                m_accEditar->trigger();
        });

    // Medida rápida: superficie y perímetro, o longitud.
    if (f->kind == GeometryKind::Polygon || f->kind == GeometryKind::Polyline) {
        connect(menu->addAction(tr("Medir")), &QAction::triggered, this, [this, id] {
            const auto e = m_mapa->feature(id);
            if (!e)
                return;
            const bool poli = e->kind == GeometryKind::Polygon;
            double area = 0.0, largo = 0.0;
            for (const auto &parte : e->outlines()) {
                if (poli)
                    area += polygonArea(parte);
                largo += pathLength(parte, poli);
            }
            const QString sup = area >= 1e6 ? tr("%1 km²").arg(area / 1e6, 0, 'f', 2)
                                            : tr("%1 ha").arg(area / 1e4, 0, 'f', 2);
            statusBar()->showMessage(poli ? tr("Superficie %1 · perímetro %2").arg(sup, textoDistancia(largo))
                                          : tr("Longitud %1").arg(textoDistancia(largo)), 10000);
        });
    }
    if (f->kind == GeometryKind::Polygon) {
        const bool vigilada = m_mapa->isZoneWatched(id);
        connect(menu->addAction(vigilada ? tr("Dejar de vigilar la zona") : tr("Vigilar como zona")),
                &QAction::triggered, this, [this, id] {
                    m_mapa->selectFeature(id);
                    alternarVigilancia();
                });
    }
    if (f->kind == GeometryKind::Point) {
        // Un punto puede ser un radar u observador: anillos fijos con su nombre.
        connect(menu->addAction(tr("Anillos de distancia en este punto")), &QAction::triggered, this,
                [this, pos = f->position(), nombre] {
                    RangeRings r = m_anillos;
                    r.targetId = -1;
                    r.center = pos;
                    r.name = nombre;
                    m_mapa->addRangeRings(r);
                });
    }
    if (editable) {
        menu->addSeparator();
        connect(menu->addAction(tr("Borrar")), &QAction::triggered, this, [this, id] {
            m_mapa->selectFeature(id);
            borrarSeleccion();
        });
    }
}

// Acciones sobre un punto cualquiera del mapa: copiar sus coordenadas, centrar,
// anillos y marcaciones desde ahí, un punto nuevo y los puntos A/B de la
// pestaña Elevación. La cabecera dice la coordenada y, si hay DEM, la cota.
void Ventana::menuPunto(QMenu *menu, const QGeoCoordinate &p)
{
    if (!p.isValid())
        return;
    QString titulo = formatCoordinate(p, m_formatoCoord);
    if (m_demActivo) {
        const double cota = m_mapa->elevationAt(p);
        if (!std::isnan(cota))
            titulo += tr(" · %1 m").arg(cota, 0, 'f', 0);
    }
    cabecera(menu, titulo);

    QMenu *copiar = menu->addMenu(tr("Copiar coordenadas"));
    const QList<QPair<QString, CoordinateFormat>> formatos = {
        { tr("Decimal"), CoordinateFormat::Decimal },
        { tr("Grados, minutos, segundos"), CoordinateFormat::DMS },
        { tr("UTM"), CoordinateFormat::UTM },
        { tr("MGRS"), CoordinateFormat::MGRS },
    };
    for (const auto &fm : formatos) {
        const QString txt = formatCoordinate(p, fm.second);
        connect(copiar->addAction(tr("%1: %2").arg(fm.first, txt)), &QAction::triggered, this,
                [txt] { QGuiApplication::clipboard()->setText(txt); });
    }
    connect(menu->addAction(tr("Centrar aquí")), &QAction::triggered, this,
            [this, p] { m_mapa->setCenter(p); });
    menu->addSeparator();

    connect(menu->addAction(tr("Anillos de distancia aquí")), &QAction::triggered, this, [this, p] {
        RangeRings r = m_anillos;
        r.targetId = -1;
        r.center = p;
        r.name.clear();
        m_mapa->addRangeRings(r);
    });
    connect(menu->addAction(tr("Anillos de distancia aquí...")), &QAction::triggered, this,
            [this, p] { ponerAnillos(p); });
    connect(menu->addAction(tr("Marcación desde aquí...")), &QAction::triggered, this, [this, p] {
        m_ebl = BearingLine();
        m_ebl.from = p;
        iniciarPickMedida(Pick::DestinoEbl,
                          tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
    });
    const qint64 sel = m_mapa->selectedTarget();
    if (sel >= 0 && m_mapa->target(sel))
        connect(menu->addAction(tr("Marcación del objetivo seleccionado hasta aquí")),
                &QAction::triggered, this, [this, sel, p] {
                    BearingLine l;
                    l.fromTarget = sel;
                    l.to = p;
                    if (m_mapa->addBearingLine(l) < 0)
                        statusBar()->showMessage(tr("El objetivo está justo aquí: no hay marcación"), 4000);
                });
    menu->addSeparator();

    // Un punto nuevo en la capa activa, con nombre.
    const bool editable = capaEditable(m_mapa->activeFeatureLayer());
    QAction *nuevo = menu->addAction(editable ? tr("Nuevo punto aquí...")
                                              : tr("Nuevo punto aquí (la capa activa no se edita)"));
    nuevo->setEnabled(editable);
    connect(nuevo, &QAction::triggered, this, [this, p] {
        bool ok = false;
        const QString nombre = QInputDialog::getText(this, tr("Nuevo punto"), tr("Nombre:"),
                                                     QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
        MapFeature f;
        f.kind = GeometryKind::Point;
        f.layerId = m_mapa->activeFeatureLayer();
        f.name = nombre.trimmed();
        f.geometry = { p };
        f.style = estiloActual();
        const qint64 id = m_mapa->addFeature(f);
        if (id >= 0) {
            marcarSucio();
            m_mapa->selectFeature(id);
        }
    });
    if (m_demActivo) {
        QMenu *elev = menu->addMenu(tr("Elevación"));
        for (const Pick cual : { Pick::A, Pick::B }) {
            connect(elev->addAction(cual == Pick::A ? tr("Fijar el punto A aquí") : tr("Fijar el punto B aquí")),
                    &QAction::triggered, this, [this, cual, p] {
                        moverPinCoord(cual, p);
                        if (m_tabs)
                            m_tabs->setCurrentIndex(m_tabs->count() - 1);
                    });
        }
    }
    if (m_mapa->activeTool() != MapTool::None) {
        menu->addSeparator();
        connect(menu->addAction(tr("Volver a navegar")), &QAction::triggered, this, &Ventana::volverANavegar);
    }
}
