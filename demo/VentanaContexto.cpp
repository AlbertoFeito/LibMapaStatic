// Menú del clic derecho: acciones según lo que haya bajo el cursor (objetivo,
// grupo de objetivos, punto o entidad, anillos o marcación, o el mapa).
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

// Punto de entrada. Se muestra UN solo elemento, el de más prioridad bajo el
// cursor: objetivo, grupo de objetivos, entidad (los puntos que pone el
// usuario), y solo si no hay nada de eso, unos anillos o una marcación. Los
// anillos de un objetivo o de un punto van dentro de su menú, no aparte. Al
// final, las acciones del lugar del mapa (en un submenú si ya hay un elemento).
void Ventana::menuContextual(const MapHit &hit, const QPoint &global)
{
    QMenu menu(this);
    bool algo = true;
    if (hit.targetId >= 0)
        menuObjetivo(&menu, hit.targetId);
    else if (!hit.clusterTargets.isEmpty())
        menuGrupo(&menu, hit.clusterTargets);
    else if (hit.featureId >= 0)
        menuEntidad(&menu, hit.featureId);
    else if (hit.rangeMarkId >= 0)
        menuMarca(&menu, hit.rangeMarkId);
    else
        algo = false;

    // ¿El clic cae en el centro de unos anillos? Entonces no se ofrece poner
    // otros encima.
    const bool enAnillos = hit.rangeMarkId >= 0 && m_mapa->rangeRings(hit.rangeMarkId).has_value();

    if (algo) {
        menu.addSeparator();
        QMenu *aqui = menu.addMenu(tr("Este lugar del mapa"));
        menuPunto(aqui, hit.coordinate, enAnillos);
    } else {
        menuPunto(&menu, hit.coordinate, enAnillos);
    }
    menu.exec(global);
}

// Submenú «Anillos de distancia» de un objetivo o de una entidad punto. Si
// aún no tiene, permite ponerlos (automáticos o eligiendo las opciones); si
// ya los tiene, solo cambiarlos o quitarlos: un dueño, unos anillos.
void Ventana::submenuAnillos(QMenu *menu, qint64 targetId, qint64 featureId)
{
    QMenu *sub = menu->addMenu(tr("Anillos de distancia"));
    const qint64 ya = anillosDe(targetId, featureId);
    if (ya >= 0) {
        opcionesAnillos(sub, ya);
        return;
    }
    RangeRings r = m_anillos;
    r.targetId = targetId;
    r.featureId = targetId >= 0 ? -1 : featureId;
    r.name.clear();
    const QString quien = targetId >= 0 ? tr("el objetivo %1").arg(nombreObjetivo(targetId))
                                        : tr("el punto");
    connect(sub->addAction(tr("Poner con los últimos ajustes")), &QAction::triggered, this,
            [this, r, quien] { ponerAnillosA(r, quien); });
    connect(sub->addAction(tr("Poner eligiendo las opciones...")), &QAction::triggered, this,
            [this, r] { ponerAnillos(r); });
}

// Opciones de unos anillos ya puestos: separación automática sí/no, radiales,
// cambiar con el diálogo y quitar.
void Ventana::opcionesAnillos(QMenu *menu, qint64 id)
{
    const auto r = m_mapa->rangeRings(id);
    if (!r)
        return;
    const double paso = m_mapa->rangeRingsSpacing(id);
    QAction *info = menu->addAction(tr("%1 anillos cada %2").arg(r->count).arg(textoDistancia(paso)));
    info->setEnabled(false);
    QAction *autom = menu->addAction(tr("Separación automática (según el zoom)"));
    autom->setCheckable(true);
    autom->setChecked(!(r->spacingMeters > 0.0));
    connect(autom, &QAction::toggled, this, [this, id, paso](bool on) {
        if (auto rr = m_mapa->rangeRings(id)) {
            rr->spacingMeters = on ? 0.0 : paso;      // al fijarla se queda la que se ve
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
    connect(menu->addAction(tr("Cambiar...")), &QAction::triggered, this,
            [this, id] { ponerAnillos(RangeRings(), id); });
    connect(menu->addAction(tr("Quitar")), &QAction::triggered, this,
            [this, id] { m_mapa->removeRangeMark(id); });
}

// Muestra la pestaña Elevación (y el panel) con el análisis \a analisis
// elegido (0 perfil, 1 visión A→B, 2 viewshed, 3 picos; -1 deja el que hay).
// Cambiar el análisis redibuja los pines: B solo se ve en Visión.
void Ventana::irAElevacion(int analisis)
{
    if (m_dock) {
        m_dock->show();
        m_dock->raise();
    }
    if (m_tabs)
        m_tabs->setCurrentIndex(m_tabs->count() - 1);
    if (analisis >= 0 && m_tipoAnalisis)
        m_tipoAnalisis->setCurrentIndex(analisis);
}

// Submenú «Elevación» para un punto: los análisis que salen de él (perfil,
// viewshed, picos), la visión desde A hasta él, y fijarlo como A o como B.
// Abre la pestaña Elevación con el análisis puesto, para que se vea qué se ha
// calculado y con qué datos.
void Ventana::submenuElevacion(QMenu *menu, const QGeoCoordinate &p)
{
    if (!m_demActivo || !p.isValid())
        return;
    QMenu *elev = menu->addMenu(tr("Elevación"));
    connect(elev->addAction(tr("Perfil desde aquí")), &QAction::triggered, this, [this, p] {
        moverPinCoord(Pick::A, p);
        irAElevacion(0);
        analizarPerfil();
    });
    connect(elev->addAction(tr("Visión desde A hasta aquí")), &QAction::triggered, this, [this, p] {
        moverPinCoord(Pick::B, p);
        irAElevacion(1);
        analizarVision();
    });
    connect(elev->addAction(tr("Viewshed (zona visible) desde aquí")), &QAction::triggered, this,
            [this, p] {
                moverPinCoord(Pick::A, p);
                irAElevacion(2);
                analizarViewshed();
            });
    connect(elev->addAction(tr("10 picos más altos alrededor")), &QAction::triggered, this, [this, p] {
        moverPinCoord(Pick::A, p);
        irAElevacion(3);
        analizarPicosAltos();
    });
    elev->addSeparator();
    connect(elev->addAction(tr("Fijar aquí el punto A")), &QAction::triggered, this, [this, p] {
        moverPinCoord(Pick::A, p);
        irAElevacion(-1);
    });
    connect(elev->addAction(tr("Fijar aquí el punto B (visión A→B)")), &QAction::triggered, this,
            [this, p] {
                moverPinCoord(Pick::B, p);
                irAElevacion(1);              // B solo lo usa la visión: se pasa a ella
            });
}

// Acciones sobre un objetivo móvil: seleccionar, centrar, sus anillos, una
// marcación, CPA con el seleccionado, filtrar por su tipo, exportar su
// historial, elevación en su posición y quitarlo.
void Ventana::menuObjetivo(QMenu *menu, qint64 id)
{
    const auto t = m_mapa->target(id);
    if (!t)
        return;
    const QString nombre = nombreObjetivo(id);
    cabecera(menu, t->kind.isEmpty() ? nombre : tr("%1 (%2)").arg(nombre, t->kind));

    connect(menu->addAction(tr("Seleccionar (ver su traza)")), &QAction::triggered, this,
            [this, id] { m_mapa->setSelectedTarget(id); });
    connect(menu->addAction(tr("Centrar en el objetivo")), &QAction::triggered, this, [this, id] {
        if (const auto o = m_mapa->target(id))
            m_mapa->setCenter(o->position);
    });
    menu->addSeparator();
    submenuAnillos(menu, id, -1);
    connect(menu->addAction(tr("Marcación desde él...")), &QAction::triggered, this, [this, id] {
        m_ebl = BearingLine();
        m_ebl.fromTarget = id;
        iniciarPickMedida(Pick::DestinoEbl,
                          tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
    });

    // Con otro objetivo seleccionado: marcación y CPA entre los dos.
    const qint64 sel = m_mapa->selectedTarget();
    if (sel >= 0 && sel != id && m_mapa->target(sel)) {
        const QString otro = nombreObjetivo(sel);
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
    submenuElevacion(menu, t->position);
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
    connect(menu->addAction(tr("Quitar el objetivo")), &QAction::triggered, this, [this, id] {
        const qint64 anillos = anillosDe(id, -1);       // sus anillos se van con él
        if (anillos >= 0)
            m_mapa->removeRangeMark(anillos);
        m_mapa->removeTarget(id);
    });
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
        const QString txt = nombreObjetivo(id);
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

// Acciones sobre unos anillos sueltos (en un lugar fijo, sin dueño) o una
// marcación, y la lista de todos. Los anillos de un objetivo o de un punto se
// manejan desde el menú de su dueño.
void Ventana::menuMarca(QMenu *menu, qint64 id)
{
    if (const auto r = m_mapa->rangeRings(id)) {
        if (r->targetId >= 0 && m_mapa->target(r->targetId)) {
            menuObjetivo(menu, r->targetId);     // son de un objetivo: su menú
            return;
        }
        if (r->featureId >= 0 && m_mapa->feature(r->featureId)) {
            menuEntidad(menu, r->featureId);     // son de un punto: su menú
            return;
        }
        cabecera(menu, r->name.isEmpty() ? tr("Anillos de distancia") : tr("Anillos «%1»").arg(r->name));
        opcionesAnillos(menu, id);
        // Sin dueño: un punto en su centro (radar, observador) se los queda y
        // pasan a ser un solo elemento.
        menu->addSeparator();
        QAction *conPunto = menu->addAction(tr("Poner un punto en su centro (radar, observador)..."));
        conPunto->setEnabled(capaEditable(m_mapa->activeFeatureLayer()));
        connect(conPunto, &QAction::triggered, this, [this, c = r->center] { nuevoPunto(c); });
    } else if (m_mapa->bearingLine(id)) {
        double rumbo = 0.0, dist = 0.0;
        cabecera(menu, m_mapa->bearingLineMeasure(id, &rumbo, &dist)
                           ? tr("Marcación %1° · %2").arg(qRound(rumbo) % 360, 3, 10, QLatin1Char('0'))
                                                    .arg(textoDistancia(dist))
                           : tr("Marcación"));
        connect(menu->addAction(tr("Quitar la marcación")), &QAction::triggered, this,
                [this, id] { m_mapa->removeRangeMark(id); });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Ver todos los anillos y marcaciones...")), &QAction::triggered,
            this, &Ventana::gestionarMarcas);
}

// Acciones sobre una entidad: propiedades, encuadrar, editar, medir, vigilar
// como zona (polígonos); en un punto, sus anillos y la elevación; y borrar
// (si su capa se edita).
void Ventana::menuEntidad(QMenu *menu, qint64 id)
{
    const auto f = m_mapa->feature(id);
    if (!f)
        return;
    // Los pines A/B de la pestaña Elevación no son entidades del usuario: su
    // menú es el de la elevación (se mueven arrastrándolos o desde la pestaña).
    if (f->layerId == kCapaPuntos) {
        const bool esB = f->name == tr("B");
        cabecera(menu, esB ? tr("Punto B (visión A→B)") : tr("Punto A (elevación)"));
        connect(menu->addAction(tr("Ver la pestaña Elevación")), &QAction::triggered, this,
                [this, esB] { irAElevacion(esB ? 1 : -1); });
        submenuElevacion(menu, f->position());
        return;
    }
    const bool editable = capaEditable(f->layerId);
    const QString nombre = f->name.isEmpty() ? tr("Entidad %1").arg(id) : f->name;
    cabecera(menu, f->type.isEmpty() ? nombre : tr("%1 (%2)").arg(nombre, f->type));

    connect(menu->addAction(tr("Seleccionar y ver propiedades")), &QAction::triggered, this, [this, id] {
        m_mapa->selectFeature(id);
        if (m_dock)
            m_dock->show();
        if (m_tabs)
            m_tabs->setCurrentIndex(0);
    });
    connect(menu->addAction(f->kind == GeometryKind::Point ? tr("Centrar") : tr("Encuadrar")),
            &QAction::triggered, this, [this, id] {
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

    if (f->kind == GeometryKind::Point) {
        // Un punto puede ser un radar u observador: sus anillos y su elevación.
        menu->addSeparator();
        submenuAnillos(menu, -1, id);
        submenuElevacion(menu, f->position());
    } else {
        // Medida rápida: superficie y perímetro, o longitud.
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
    if (editable) {
        menu->addSeparator();
        connect(menu->addAction(tr("Borrar")), &QAction::triggered, this, [this, id] {
            m_mapa->selectFeature(id);
            borrarSeleccion();                   // sus anillos se quitan al borrarla
        });
    }
}

// Acciones sobre un lugar del mapa: copiar sus coordenadas, centrar, anillos
// (salvo que ya haya unos centrados ahí: \a enAnillos), marcación, un punto
// nuevo y la elevación. La cabecera dice la coordenada y, si hay DEM, la cota.
void Ventana::menuPunto(QMenu *menu, const QGeoCoordinate &p, bool enAnillos)
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

    // Un punto con nombre: lo recomendable para un radar u observador, que
    // luego tiene sus anillos y su elevación desde su propio menú.
    const bool editable = capaEditable(m_mapa->activeFeatureLayer());
    QAction *nuevo = menu->addAction(editable ? tr("Nuevo punto aquí...")
                                              : tr("Nuevo punto aquí (la capa activa no se edita)"));
    nuevo->setEnabled(editable);
    connect(nuevo, &QAction::triggered, this, [this, p] { nuevoPunto(p); });
    if (!enAnillos) {
        QMenu *sub = menu->addMenu(tr("Anillos de distancia"));
        connect(sub->addAction(tr("Poner aquí con los últimos ajustes")), &QAction::triggered, this,
                [this, p] {
                    RangeRings r = m_anillos;
                    r.targetId = -1;
                    r.featureId = -1;
                    r.center = p;
                    r.name.clear();
                    m_mapa->addRangeRings(r);
                });
        connect(sub->addAction(tr("Poner aquí eligiendo las opciones...")), &QAction::triggered, this,
                [this, p] {
                    RangeRings r;
                    r.center = p;
                    ponerAnillos(r);
                });
    }
    QMenu *marc = menu->addMenu(tr("Marcación"));
    connect(marc->addAction(tr("Desde aquí hasta otro punto u objetivo...")), &QAction::triggered,
            this, [this, p] {
                m_ebl = BearingLine();
                m_ebl.from = p;
                iniciarPickMedida(Pick::DestinoEbl,
                                  tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
            });
    const qint64 sel = m_mapa->selectedTarget();
    if (sel >= 0 && m_mapa->target(sel))
        connect(marc->addAction(tr("Desde %1 hasta aquí").arg(nombreObjetivo(sel))),
                &QAction::triggered, this, [this, sel, p] {
                    BearingLine l;
                    l.fromTarget = sel;
                    l.to = p;
                    if (m_mapa->addBearingLine(l) < 0)
                        statusBar()->showMessage(tr("El objetivo está justo aquí: no hay marcación"), 4000);
                });
    submenuElevacion(menu, p);
    if (m_mapa->activeTool() != MapTool::None) {
        menu->addSeparator();
        connect(menu->addAction(tr("Volver a navegar")), &QAction::triggered, this, &Ventana::volverANavegar);
    }
}

// Anillos SIN dueño cuyo centro está en \a p (a menos de 12 px a la escala
// actual), o -1. Así un punto puesto ahí se queda con ellos.
qint64 Ventana::anillosSueltosEn(const QGeoCoordinate &p) const
{
    const double tolerancia = 12.0 * std::max(1.0, m_mapa->metersPerPixel());
    qint64 mejor = -1;
    double dMejor = tolerancia;
    for (const qint64 id : m_mapa->rangeMarks()) {
        const auto r = m_mapa->rangeRings(id);
        if (!r || r->targetId >= 0 || r->featureId >= 0 || !r->center.isValid())
            continue;
        const double d = r->center.distanceTo(p);
        if (d <= dMejor) {
            dMejor = d;
            mejor = id;
        }
    }
    return mejor;
}

// Crea un punto con nombre en \a p en la capa activa. Si ahí está el centro de
// unos anillos sueltos, el punto se pone EXACTAMENTE en ese centro y pasa a
// ser su dueño (los anillos lo siguen y son un solo elemento con él); si no
// se le da nombre, toma el de los anillos.
void Ventana::nuevoPunto(const QGeoCoordinate &p)
{
    const qint64 anillos = anillosSueltosEn(p);
    const auto r = anillos >= 0 ? m_mapa->rangeRings(anillos) : std::nullopt;
    bool ok = false;
    const QString nombre = QInputDialog::getText(this, tr("Nuevo punto"),
        r ? tr("Nombre (se queda con los anillos de este centro):") : tr("Nombre:"),
        QLineEdit::Normal, r ? r->name : QString(), &ok);
    if (!ok)
        return;
    MapFeature f;
    f.kind = GeometryKind::Point;
    f.layerId = m_mapa->activeFeatureLayer();
    f.name = nombre.trimmed();
    f.geometry = { r ? r->center : p };
    f.style = estiloActual();
    if (r)
        f.style.labelVisible = true;       // el nombre que rotulaban los anillos lo rotula él
    const qint64 id = m_mapa->addFeature(f);
    if (id < 0)
        return;
    if (r) {
        RangeRings suyos = *r;
        suyos.featureId = id;
        suyos.name.clear();                // el rótulo ya lo pone el punto
        m_mapa->updateRangeRings(anillos, suyos);
        statusBar()->showMessage(tr("El punto «%1» se queda con los anillos").arg(f.name), 4000);
    }
    marcarSucio();
    m_mapa->selectFeature(id);
}
