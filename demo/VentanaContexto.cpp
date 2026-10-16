// Menú del clic derecho: acciones según lo que haya bajo el cursor.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).
//
// Modelo (ver también «Elementos, anillos y marcaciones» en VentanaObjetivos.cpp):
//  - Elementos: objetivo móvil, punto del usuario (radar, observador…), zona o
//    línea, y grupo de objetivos. Cada uno tiene SU menú.
//  - Anillos y marcaciones NO son elementos aparte: cuelgan de un objetivo o de
//    un punto y se manejan desde él. Una marcación PARTE de un elemento y llega
//    a otro elemento o a cualquier lugar del mapa; no se parte de un lugar
//    vacío. Un punto nuevo con anillos es un solo elemento.
//  - Un lugar vacío del mapa solo ofrece lo que tiene sentido ahí: copiar sus
//    coordenadas, centrar, crear un punto (con o sin anillos), una marcación
//    hasta él desde el elemento seleccionado y analizar la elevación en él.
//  - Se muestra UN solo elemento, el de más prioridad bajo el cursor: objetivo,
//    grupo, entidad (puntos del usuario, zonas, líneas), y solo si no hay nada
//    de eso, la marca (anillos o marcación) sobre la que se hizo clic.

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

// ¿Es la entidad \a id un punto del usuario (radar, observador, hito…)? No lo
// son los pines ni los resultados de Elevación (capas elev_*).
static bool esPuntoDelUsuario(const MapWidget *mapa, qint64 id)
{
    const auto f = id >= 0 ? mapa->feature(id) : std::nullopt;
    return f && f->kind == GeometryKind::Point && !f->layerId.startsWith(QLatin1String("elev_"));
}

// Punto de entrada. Elige el elemento de más prioridad bajo el cursor y monta
// su menú; después, las acciones del lugar (en un submenú si hay un elemento,
// y reducidas a copiar/centrar si el elemento es un objetivo o un punto: ahí
// no tiene sentido crear otro punto ni analizar «el lugar»).
void Ventana::menuContextual(const MapHit &hit, const QPoint &global)
{
    QMenu menu(this);
    bool hayElemento = true;
    if (hit.targetId >= 0)
        menuObjetivo(&menu, hit.targetId);
    else if (!hit.clusterTargets.isEmpty())
        menuGrupo(&menu, hit.clusterTargets);
    else if (hit.vertexFeature >= 0)
        menuEntidad(&menu, hit.vertexFeature, hit.vertexIndex);     // sobre un tirador de vértice
    else if (hit.featureId >= 0)
        menuEntidad(&menu, hit.featureId);
    else if (hit.rangeMarkId >= 0)
        menuMarca(&menu, hit.rangeMarkId);
    else
        hayElemento = false;

    const bool elementoPuntual = hit.targetId >= 0 || !hit.clusterTargets.isEmpty()
                                 || esPuntoDelUsuario(m_mapa, hit.featureId);
    if (hayElemento) {
        menu.addSeparator();
        QMenu *aqui = menu.addMenu(tr("Este lugar del mapa"));
        menuPunto(aqui, hit.coordinate, elementoPuntual);
    } else {
        menuPunto(&menu, hit.coordinate, false);
    }
    menu.exec(global);
}

// Submenú «Anillos de distancia» de un objetivo o de un punto. Si aún no tiene,
// permite ponerlos (con los últimos ajustes o eligiéndolos); si ya los tiene,
// solo cambiarlos o quitarlos: un dueño, unos anillos.
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
                                        : tr("el punto «%1»").arg(nombrePunto(featureId));
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
    connect(menu->addAction(tr("Quitar los anillos")), &QAction::triggered, this,
            [this, id] { m_mapa->removeRangeMark(id); });
}

// Submenú «Marcación» de un objetivo o de un punto: la marcación PARTE de un
// elemento y llega a otro elemento o a cualquier lugar del mapa que se pinche;
// también directamente hasta el objetivo o el punto seleccionados. Se pueden
// quitar las que ya salen o llegan a este elemento.
void Ventana::submenuMarcacion(QMenu *menu, qint64 targetId, qint64 featureId)
{
    QMenu *sub = menu->addMenu(tr("Marcación"));
    connect(sub->addAction(tr("Hasta un objetivo, punto o lugar (pinchar)...")), &QAction::triggered, this,
            [this, targetId, featureId] {
                m_ebl = BearingLine();
                m_ebl.fromTarget = targetId;
                m_ebl.fromFeature = targetId >= 0 ? -1 : featureId;
                iniciarPickMedida(Pick::DestinoEbl,
                                  tr("Pincha el destino de la marcación: un objetivo, un punto o cualquier lugar (Esc cancela)…"));
            });
    // Otros elementos ya seleccionados: marcación directa.
    const qint64 selT = m_mapa->selectedTarget();
    if (selT >= 0 && selT != targetId && m_mapa->target(selT))
        connect(sub->addAction(tr("Hasta %1 (seleccionado)").arg(nombreObjetivo(selT))), &QAction::triggered,
                this, [this, targetId, featureId, selT] { ponerMarcacionEntre(targetId, featureId, selT, -1); });
    const qint64 selP = puntoSeleccionado();
    if (selP >= 0 && !(targetId < 0 && selP == featureId))
        connect(sub->addAction(tr("Hasta «%1» (seleccionado)").arg(nombrePunto(selP))), &QAction::triggered,
                this, [this, targetId, featureId, selP] { ponerMarcacionEntre(targetId, featureId, -1, selP); });
    // Las que ya tiene este elemento, para quitarlas.
    const QVector<qint64> existentes = marcacionesDe(targetId, featureId);
    if (!existentes.isEmpty())
        sub->addSeparator();
    for (const qint64 id : existentes) {
        const auto l = m_mapa->bearingLine(id);
        if (!l)
            continue;
        const bool salgo = targetId >= 0 ? l->fromTarget == targetId
                                         : (l->fromTarget < 0 && l->fromFeature == featureId);
        const QString otro = salgo ? nombreExtremo(l->toTarget, l->toFeature)
                                   : nombreExtremo(l->fromTarget, l->fromFeature);
        connect(sub->addAction(tr("Quitar la marcación con %1").arg(otro)), &QAction::triggered, this,
                [this, id] { m_mapa->removeRangeMark(id); });
    }
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

// Si hay un punto del usuario a menos de ~12 px de \a p, su posición exacta
// (así un análisis hecho «aquí», junto a un radar u observador, parte de él y
// no de un punto casi igual); si no, \a p.
QGeoCoordinate Ventana::ajustarAPunto(const QGeoCoordinate &p) const
{
    const qint64 id = puntoEn(p);
    return id >= 0 ? posicionDe(-1, id) : p;
}

// Submenú «Elevación» (solo con DEM): los análisis que salen de \a p (perfil,
// viewshed, picos), la visión desde A hasta él, y fijarlo como A o como B.
// \a delLugar dice si \a p es un lugar del mapa («aquí») o un elemento («él»).
// Abre la pestaña Elevación con el análisis puesto, para que se vea qué se ha
// calculado y con qué datos; y B solo se muestra en «Visión A→B», a la que se
// pasa al fijarlo.
void Ventana::submenuElevacion(QMenu *menu, const QGeoCoordinate &p, bool delLugar)
{
    if (!m_demActivo || !p.isValid())
        return;
    QMenu *elev = menu->addMenu(tr("Elevación"));
    connect(elev->addAction(delLugar ? tr("Perfil desde aquí") : tr("Perfil desde él")),
            &QAction::triggered, this, [this, p] {
                moverPinCoord(Pick::A, ajustarAPunto(p));
                irAElevacion(0);
                analizarPerfil();
            });
    connect(elev->addAction(delLugar ? tr("Visión desde A hasta aquí") : tr("Visión desde A hasta él")),
            &QAction::triggered, this, [this, p] {
                moverPinCoord(Pick::B, ajustarAPunto(p));
                irAElevacion(1);
                analizarVision();
            });
    connect(elev->addAction(delLugar ? tr("Viewshed (zona visible) desde aquí")
                                     : tr("Viewshed (zona visible) desde él")),
            &QAction::triggered, this, [this, p] {
                moverPinCoord(Pick::A, ajustarAPunto(p));
                irAElevacion(2);
                analizarViewshed();
            });
    connect(elev->addAction(delLugar ? tr("10 picos más altos alrededor de aquí")
                                     : tr("10 picos más altos alrededor de él")),
            &QAction::triggered, this, [this, p] {
                moverPinCoord(Pick::A, ajustarAPunto(p));
                irAElevacion(3);
                analizarPicosAltos();
            });
    elev->addSeparator();
    connect(elev->addAction(delLugar ? tr("Fijar aquí el punto A") : tr("Usar como observador (punto A)")),
            &QAction::triggered, this, [this, p] {
                moverPinCoord(Pick::A, ajustarAPunto(p));
                irAElevacion(-1);
            });
    connect(elev->addAction(delLugar ? tr("Fijar aquí el punto B (visión A→B)")
                                     : tr("Usar como punto B (visión A→B)")),
            &QAction::triggered, this, [this, p] {
                moverPinCoord(Pick::B, ajustarAPunto(p));
                irAElevacion(1);              // B solo lo usa la visión: se pasa a ella
            });
}

// Acciones sobre un objetivo móvil: seleccionar, centrar, sus anillos y
// marcaciones, CPA con otro objetivo seleccionado, elevación en su posición,
// filtrar por su tipo, exportar su historial y quitarlo (con sus anillos y
// marcaciones).
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
    submenuMarcacion(menu, id, -1);

    // CPA/TCPA con otro objetivo seleccionado.
    const qint64 sel = m_mapa->selectedTarget();
    if (sel >= 0 && sel != id && m_mapa->target(sel)) {
        const QString otro = nombreObjetivo(sel);
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
    submenuElevacion(menu, t->position, false);
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
        quitarMarcasDe(id, -1);            // sus anillos y marcaciones se van con él
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
        if (recuadroDe(pos, &no, &se))
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

// Clic sobre una marca (la línea de un anillo o de una marcación): si son los
// anillos de un objetivo o de un punto, el menú es el de ese elemento (ahí se
// manejan); una marcación se puede quitar; y unos anillos sin dueño (solo vía
// la API) tienen sus opciones y pueden darse un punto en el centro. Siempre, la
// lista de todos.
void Ventana::menuMarca(QMenu *menu, qint64 id)
{
    if (const auto r = m_mapa->rangeRings(id)) {
        if (r->targetId >= 0 && m_mapa->target(r->targetId)) {
            menuObjetivo(menu, r->targetId);
            return;
        }
        if (r->featureId >= 0 && m_mapa->feature(r->featureId)) {
            menuEntidad(menu, r->featureId);
            return;
        }
        cabecera(menu, r->name.isEmpty() ? tr("Anillos de distancia") : tr("Anillos «%1»").arg(r->name));
        opcionesAnillos(menu, id);
        menu->addSeparator();
        QAction *conPunto = menu->addAction(tr("Poner un punto en su centro (radar, observador)..."));
        conPunto->setEnabled(!capaParaPuntos().isEmpty());
        connect(conPunto, &QAction::triggered, this, [this, c = r->center] { nuevoPunto(c); });
    } else if (const auto l = m_mapa->bearingLine(id)) {
        double rumbo = 0.0, dist = 0.0;
        const QString de = l->fromTarget >= 0 || l->fromFeature >= 0
            ? nombreExtremo(l->fromTarget, l->fromFeature) : formatCoordinate(l->from, m_formatoCoord);
        const QString a = l->toTarget >= 0 || l->toFeature >= 0
            ? nombreExtremo(l->toTarget, l->toFeature) : formatCoordinate(l->to, m_formatoCoord);
        cabecera(menu, m_mapa->bearingLineMeasure(id, &rumbo, &dist)
                           ? tr("Marcación %1 → %2 · %3° · %4").arg(de, a)
                                 .arg(qRound(rumbo) % 360, 3, 10, QLatin1Char('0')).arg(textoDistancia(dist))
                           : tr("Marcación %1 → %2").arg(de, a));
        connect(menu->addAction(tr("Quitar la marcación")), &QAction::triggered, this,
                [this, id] { m_mapa->removeRangeMark(id); });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Ver todos los anillos y marcaciones...")), &QAction::triggered,
            this, &Ventana::gestionarMarcas);
}

// Acciones sobre una entidad. Un PUNTO del usuario (radar, observador…) es un
// elemento como un objetivo: sus anillos, sus marcaciones, la elevación desde
// él y borrarlo (con lo que cuelga de él). Una zona o una línea: encuadrar,
// editar vértices, medir y vigilar como zona. Los pines A/B y los resultados de
// Elevación tienen un menú propio, más corto.
void Ventana::menuEntidad(QMenu *menu, qint64 id, int vertice)
{
    const auto f = m_mapa->feature(id);
    if (!f)
        return;
    const QString nombre = f->name.isEmpty() ? tr("Entidad %1").arg(id) : f->name;

    // Pines A/B de la pestaña Elevación: no son entidades del usuario (se
    // mueven arrastrándolos o desde la pestaña).
    if (f->layerId == kCapaPuntos) {
        const bool esB = f->name == tr("B");
        cabecera(menu, esB ? tr("Punto B (visión A→B)") : tr("Punto A (observador del análisis)"));
        connect(menu->addAction(tr("Ver la pestaña Elevación")), &QAction::triggered, this,
                [this, esB] { irAElevacion(esB ? 1 : -1); });
        submenuElevacion(menu, f->position(), false);
        return;
    }
    // Resultados de un análisis (picos, visión, viewshed): se pueden centrar y
    // analizar desde ellos, pero no son del usuario.
    if (f->layerId.startsWith(QLatin1String("elev_"))) {
        cabecera(menu, nombre);
        if (f->kind == GeometryKind::Point) {
            connect(menu->addAction(tr("Centrar")), &QAction::triggered, this,
                    [this, pos = f->position()] { m_mapa->setCenter(pos); });
            submenuElevacion(menu, f->position(), false);
        }
        return;
    }

    const bool editable = capaEditable(f->layerId);
    cabecera(menu, f->type.isEmpty() ? nombre : tr("%1 (%2)").arg(nombre, f->type));
    // Sobre un tirador de vértice de la entidad seleccionada: borrarlo (una zona
    // no baja de 3 vértices ni una línea de 2).
    if (vertice >= 0 && f->kind != GeometryKind::Point) {
        const int minimo = f->kind == GeometryKind::Polygon ? 3 : 2;
        const bool puede = editable && f->geometry.size() > minimo;
        QAction *borrarV = menu->addAction(puede ? tr("Borrar este vértice")
                                                 : tr("Borrar este vértice (no puede quedar con menos de %1)").arg(minimo));
        borrarV->setEnabled(puede);
        connect(borrarV, &QAction::triggered, this, [this, id, vertice] {
            if (m_mapa->removeVertex(id, vertice))
                marcarSucio();
        });
        menu->addSeparator();
    }
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
                if (recuadroDe(todos, &no, &se))
                    m_mapa->fitBounds(no, se);
            });

    if (f->kind == GeometryKind::Point) {
        menu->addSeparator();
        submenuAnillos(menu, -1, id);
        submenuMarcacion(menu, -1, id);
        submenuElevacion(menu, f->position(), false);
    } else {
        if (editable)
            connect(menu->addAction(tr("Editar vértices")), &QAction::triggered, this, [this, id] {
                m_mapa->selectFeature(id);
                if (m_accEditar)
                    m_accEditar->trigger();
            });
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
        if (f->kind == GeometryKind::Polygon) {
            const bool vigilada = m_mapa->isZoneWatched(id);
            connect(menu->addAction(vigilada ? tr("Dejar de vigilar la zona") : tr("Vigilar como zona")),
                    &QAction::triggered, this, [this, id] {
                        m_mapa->selectFeature(id);
                        alternarVigilancia();
                    });
        }
    }
    if (editable) {
        menu->addSeparator();
        connect(menu->addAction(f->kind == GeometryKind::Point ? tr("Borrar el punto (con sus anillos y marcaciones)")
                                                               : tr("Borrar")),
                &QAction::triggered, this, [this, id] {
                    m_mapa->selectFeature(id);
                    borrarSeleccion();     // lo que cuelga de un punto se quita al borrarlo
                });
    }
}

// Acciones sobre un lugar del mapa: copiar sus coordenadas, centrar y, si no
// hay ya un objetivo o un punto (\a enElemento), crear un punto (con o sin
// anillos) y analizar la elevación. NO hay anillos ni marcación «aquí»: unos
// anillos necesitan un dueño y una marcación, dos elementos; para un radar u
// observador se crea primero el punto. La cabecera dice la coordenada y, si hay
// DEM, la cota.
void Ventana::menuPunto(QMenu *menu, const QGeoCoordinate &p, bool enElemento)
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

    if (!enElemento) {
        menu->addSeparator();
        const bool hayCapa = !capaParaPuntos().isEmpty();
        QAction *nuevo = menu->addAction(hayCapa ? tr("Nuevo punto aquí...")
                                                 : tr("Nuevo punto aquí (no hay capa editable)"));
        nuevo->setEnabled(hayCapa);
        connect(nuevo, &QAction::triggered, this, [this, p] { nuevoPunto(p); });
        QAction *conAnillos = menu->addAction(hayCapa ? tr("Nuevo punto con anillos aquí...")
                                                      : tr("Nuevo punto con anillos aquí (no hay capa editable)"));
        conAnillos->setEnabled(hayCapa);
        connect(conAnillos, &QAction::triggered, this, [this, p] {
            RangeRings r;
            r.center = p;                  // un punto nuevo en estas coordenadas
            ponerAnillos(r);
        });
        // Marcación hasta este lugar: parte de un elemento ya seleccionado
        // (no se puede partir de un lugar vacío).
        const qint64 selT = m_mapa->selectedTarget() >= 0 && m_mapa->target(m_mapa->selectedTarget())
                                ? m_mapa->selectedTarget() : -1;
        const qint64 selP = puntoSeleccionado();
        if (selT >= 0)
            connect(menu->addAction(tr("Marcación desde %1 hasta aquí").arg(nombreObjetivo(selT))),
                    &QAction::triggered, this, [this, selT, p] { ponerMarcacionEntre(selT, -1, -1, -1, p); });
        if (selP >= 0)
            connect(menu->addAction(tr("Marcación desde «%1» hasta aquí").arg(nombrePunto(selP))),
                    &QAction::triggered, this, [this, selP, p] { ponerMarcacionEntre(-1, selP, -1, -1, p); });
        if (selT < 0 && selP < 0)
            menu->addAction(tr("Marcación hasta aquí (selecciona antes un objetivo o un punto)"))->setEnabled(false);
        submenuElevacion(menu, p, true);
    }
    if (m_mapa->activeTool() != MapTool::None) {
        menu->addSeparator();
        connect(menu->addAction(tr("Volver a navegar")), &QAction::triggered, this, &Ventana::volverANavegar);
    }
}

// Anillos SIN dueño (solo se crean por la API) cuyo centro está en \a p (a menos
// de 12 px a la escala actual), o -1. Así un punto puesto ahí se queda con ellos.
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

// Crea un punto con nombre en \a p (capa activa, o la primera editable). Si ahí
// está el centro de unos anillos sueltos, el punto se pone EXACTAMENTE en ese
// centro y pasa a ser su dueño: es un solo elemento. Si no se da nombre, toma
// el de los anillos o «Punto N».
void Ventana::nuevoPunto(const QGeoCoordinate &p)
{
    const qint64 anillos = anillosSueltosEn(p);
    const auto r = anillos >= 0 ? m_mapa->rangeRings(anillos) : std::nullopt;
    bool ok = false;
    const QString nombre = QInputDialog::getText(this, tr("Nuevo punto"),
        r ? tr("Nombre (se queda con los anillos de este centro):") : tr("Nombre (radar, observador…):"),
        QLineEdit::Normal, r ? r->name : QString(), &ok);
    if (!ok)
        return;
    const qint64 id = crearPunto(r ? r->center : p, nombre);
    if (id < 0)
        return;
    if (r) {
        RangeRings suyos = *r;
        suyos.featureId = id;
        suyos.name.clear();                // el rótulo ya lo pone el punto
        m_mapa->updateRangeRings(anillos, suyos);
        statusBar()->showMessage(tr("El punto «%1» se queda con los anillos").arg(nombrePunto(id)), 4000);
    }
}
