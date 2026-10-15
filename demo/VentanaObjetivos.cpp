// Objetivos: seguimiento, zonas vigiladas, CPA, anillos, medidas, reproducción, simulación y filtro.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

using namespace libmapa;

// Prepara el seguimiento de objetivos: dibuja el juego de iconos (una vez) y
// registra la simbologia de la app. Demuestra el contrato de la libreria:
// ella es agnostica del dominio; la app elige el icono por 'kind' y por
// estado (un UAV con poca bateria va en rojo), y fija el nivel de detalle
// para escalar a miles. Un 'kind' sin icono cae al galon por defecto.
void Ventana::prepararSeguimiento()
{
    m_icoBuque   = iconoBuque(QColor(0x20, 0x6a, 0xd0));
    m_icoAereo   = iconoAereo(QColor(0x0c, 0x97, 0x8a));
    m_icoUav     = iconoUav(QColor(0x3c, 0xb0, 0x4a));
    m_icoUavBajo = iconoUav(QColor(0xd0, 0x3a, 0x2a));
    // Variantes de ALERTA: objetivo dentro de una zona vigilada.
    const QColor alerta(0xff, 0x17, 0x44);
    m_icoBuqueZona = iconoBuque(alerta);
    m_icoAereoZona = iconoAereo(alerta);
    m_icoUavZona   = iconoUav(alerta);

    m_mapa->setTargetSymbolProvider([this](const MapTarget &t) {
        TargetSymbol s;                         // icono nulo => galon
        // Dentro de una zona vigilada: icono rojo y algo mayor (la alerta
        // se VE en el mapa, no solo en la barra de estado).
        const bool enZona = !m_mapa->zonesOfTarget(t.id).isEmpty();
        if (t.kind == QLatin1String("buque"))
            s.icon = enZona ? m_icoBuqueZona : m_icoBuque;
        else if (t.kind == QLatin1String("aeronave"))
            s.icon = enZona ? m_icoAereoZona : m_icoAereo;
        else if (t.kind == QLatin1String("uav"))
            s.icon = enZona ? m_icoUavZona
                   : t.attributes.value(QStringLiteral("bateria")).toInt() < 20
                         ? m_icoUavBajo : m_icoUav;
        s.scale = enZona ? 1.3 : 1.0;
        s.rotateWithHeading = true;
        return s;
    });

    // Con miles de objetivos se dejan de rotular/trazar al amontonarse.
    m_mapa->setTargetDetailBudget(200, 600);

    // Clic sobre un objetivo -> la libreria lo resalta y emite targetClicked;
    // aqui mostramos sus datos de dominio (sus attributes) en la barra de
    // estado. Es el cierre del ciclo ver -> seleccionar -> consultar.
    // Alertas de zona: la librería avisa al cruzar el borde de una zona vigilada.
    connect(m_mapa, &MapWidget::targetEnteredZone, this, [this](qint64 obj, qint64 zona) {
        statusBar()->showMessage(textoAlertaZona(obj, false, zona, true), 6000);
        actualizarIndicadorAlertas();
    });
    connect(m_mapa, &MapWidget::targetLeftZone, this, [this](qint64 obj, qint64 zona) {
        statusBar()->showMessage(textoAlertaZona(obj, false, zona, false), 6000);
        actualizarIndicadorAlertas();
    });
    // Alertas de proximidad: aviso en la barra y recuento en el indicador.
    connect(m_mapa, &MapWidget::proximityAlertRaised, this,
            [this](qint64 a, qint64 b, double cpa, double tcpa) {
                statusBar()->showMessage(textoProximidad(a, b, cpa, tcpa), 8000);
                actualizarIndicadorAlertas();
            });
    connect(m_mapa, &MapWidget::proximityAlertCleared, this,
            [this](qint64, qint64) { actualizarIndicadorAlertas(); });
    // Los «Objetivo» que coloca el operador son ENTIDADES punto: su propia señal.
    connect(m_mapa, &MapWidget::featureEnteredZone, this, [this](qint64 ent, qint64 zona) {
        statusBar()->showMessage(textoAlertaZona(ent, true, zona, true), 8000);
        actualizarIndicadorAlertas();
    });
    connect(m_mapa, &MapWidget::featureLeftZone, this, [this](qint64 ent, qint64 zona) {
        statusBar()->showMessage(textoAlertaZona(ent, true, zona, false), 8000);
        actualizarIndicadorAlertas();
    });

    // Clic en un grupo de objetivos: encuadra a todos los que reúne (si
    // están casi en el mismo punto, amplía dos niveles sobre él).
    connect(m_mapa, &MapWidget::targetClusterClicked, this,
            [this](const QVector<qint64> &ids, const QGeoCoordinate &donde) {
                double n = -90, s = 90, w = 180, e = -180;
                for (qint64 id : ids)
                    if (const auto t = m_mapa->target(id)) {
                        n = std::max(n, t->position.latitude());
                        s = std::min(s, t->position.latitude());
                        w = std::min(w, t->position.longitude());
                        e = std::max(e, t->position.longitude());
                    }
                if (n - s > 1e-3 || e - w > 1e-3) {
                    const double mLat = (n - s) * 0.15 + 1e-3, mLon = (e - w) * 0.15 + 1e-3;
                    m_mapa->fitBounds(QGeoCoordinate(n + mLat, w - mLon),
                                      QGeoCoordinate(s - mLat, e + mLon));
                } else {
                    m_mapa->setCenter(donde);
                    m_mapa->setZoom(m_mapa->zoom() + 2);
                }
                statusBar()->showMessage(tr("Grupo de %1 objetivos").arg(ids.size()), 4000);
            });
    connect(m_mapa, &MapWidget::targetClicked, this,
            [this](qint64 id, const QGeoCoordinate &) {
                const auto t = m_mapa->target(id);
                if (!t)
                    return;
                QStringList datos;
                for (auto it = t->attributes.constBegin();
                     it != t->attributes.constEnd(); ++it)
                    datos << QStringLiteral("%1=%2")
                                 .arg(it.key(), it.value().toString());
                // Origen: donde y cuando se vio por primera vez (la
                // librería guarda el historial entero y, al estar
                // seleccionado, lo dibuja desde ese punto).
                const TargetTrackPoint o = m_mapa->targetOrigin(id);
                const QString origen = o.isValid()
                    ? tr("   origen %1 a las %2 (%3 puntos)")
                          .arg(formatCoordinate(o.position, m_formatoCoord),
                               o.time.toLocalTime().toString(QStringLiteral("HH:mm:ss")))
                          .arg(m_mapa->targetHistory(id).size())
                    : QString();
                statusBar()->showMessage(
                    tr("Objetivo %1 [%2]  %3%4")
                        .arg(id).arg(t->kind, datos.join(QStringLiteral("   ")), origen),
                    12000);
            });
}

// Respuesta a «Seleccionar área» / «Seleccionar polígono»: informa de la
// zona marcada (tamaño, extensión y qué objetivos y entidades hay dentro)
// y ofrece lo que se puede hacer con ella. Al cerrar, quita la marca y
// vuelve a Navegar.
void Ventana::zonaSeleccionada(const QVector<QGeoCoordinate> &zona, bool rectangulo)
{
    double s = 90, n = -90, w = 180, e = -180;
    for (const QGeoCoordinate &c : zona) {
        s = qMin(s, c.latitude());  n = qMax(n, c.latitude());
        w = qMin(w, c.longitude()); e = qMax(e, c.longitude());
    }
    const QGeoCoordinate nw(n, w), se(s, e);
    const double m2 = polygonArea(zona);
    const QString superficie = m2 >= 1e6
        ? tr("%1 km²").arg(m2 / 1e6, 0, 'f', m2 >= 1e8 ? 0 : 2)
        : tr("%1 ha").arg(m2 / 1e4, 0, 'f', 2);
    const double per = pathLength(zona, true);
    const QString perimetro = per >= 1000 ? tr("%1 km").arg(per / 1000, 0, 'f', 2)
                                          : tr("%1 m").arg(per, 0, 'f', 0);

    // Qué hay dentro: nombres de los primeros objetivos y entidades.
    const QVector<qint64> objs = m_mapa->targetsInArea(zona);
    const QVector<qint64> ents = m_mapa->featuresInArea(zona);
    const auto lista = [](const QStringList &l) {
        return l.size() <= 5 ? l.join(QStringLiteral(", "))
                             : QStringList(l.mid(0, 5)).join(QStringLiteral(", "))
                                   + QStringLiteral(", …");
    };
    QStringList nomObjs, nomEnts;
    for (qint64 id : objs) {
        const auto t = m_mapa->target(id);
        nomObjs << (t && !t->label.isEmpty() ? t->label.section(QLatin1Char('\n'), 0, 0)
                                             : tr("objetivo %1").arg(id));
    }
    for (qint64 id : ents) {
        const auto f = m_mapa->feature(id);
        nomEnts << (f && !f->name.isEmpty() ? f->name.section(QLatin1Char('\n'), 0, 0)
                                            : tr("entidad %1").arg(id));
    }

    QString txt = tr("<b>%1</b><br>Superficie: %2 · perímetro: %3<br>"
                     "NO: %4<br>SE: %5<br><br>")
        .arg(rectangulo ? tr("Área rectangular")
                        : tr("Polígono de %1 vértices").arg(zona.size()),
             superficie, perimetro,
             formatCoordinate(nw, m_formatoCoord).toHtmlEscaped(),
             formatCoordinate(se, m_formatoCoord).toHtmlEscaped());
    txt += tr("Objetivos dentro: <b>%1</b>").arg(objs.size());
    if (!nomObjs.isEmpty())
        txt += QStringLiteral(" (") + lista(nomObjs).toHtmlEscaped() + QStringLiteral(")");
    txt += tr("<br>Entidades dentro: <b>%1</b>").arg(ents.size());
    if (!nomEnts.isEmpty())
        txt += QStringLiteral(" (") + lista(nomEnts).toHtmlEscaped() + QStringLiteral(")");

    QMessageBox box(QMessageBox::Information, tr("Zona seleccionada"), txt,
                    QMessageBox::NoButton, this);
    box.setTextFormat(Qt::RichText);
    box.setInformativeText(
        tr("La zona marcada es temporal: no se guarda. Para conservarla, "
           "conviértela en un área de la capa activa."));
    QPushButton *bCrear = box.addButton(tr("Crear área"), QMessageBox::ActionRole);
    QPushButton *bVigilar = box.addButton(tr("Crear y vigilar"), QMessageBox::ActionRole);
    QPushButton *bExportar = box.addButton(tr("Exportar zona..."), QMessageBox::ActionRole);
    box.addButton(tr("Cerrar"), QMessageBox::RejectRole);
    bCrear->setToolTip(tr("Crea un polígono con esta forma en la capa activa."));
    bVigilar->setToolTip(tr("Crea el polígono y avisa cuando un objetivo entre o salga."));
    bExportar->setToolTip(tr("Encuadra la zona y la guarda como imagen o PDF."));
    box.exec();

    m_mapa->clearAreaSelection();
    volverANavegar();

    const QAbstractButton *pulsado = box.clickedButton();
    if (pulsado == bExportar) {
        m_mapa->fitBounds(nw, se);
        exportarVista(rectangulo ? tr("Área seleccionada") : tr("Zona seleccionada"));
        return;
    }
    if (pulsado != bCrear && pulsado != bVigilar)
        return;

    MapFeature f;
    f.kind = GeometryKind::Polygon;
    f.type = QStringLiteral("zona");
    f.name = tr("Zona %1").arg(++m_zonasCreadas);
    f.layerId = m_mapa->activeFeatureLayer();
    f.geometry = zona;
    f.style.lineColor = QColor(0xff, 0x6d, 0x00);
    f.style.lineWidth = 2.0;
    f.style.fillColor = QColor(0xff, 0x6d, 0x00, 40);
    const qint64 id = m_mapa->addFeature(f);
    if (id < 0) {
        statusBar()->showMessage(tr("No se pudo crear la zona"), 4000);
        return;
    }
    m_mapa->selectFeature(id);
    if (pulsado == bVigilar) {
        alternarVigilancia();                 // vigila la recién seleccionada
    } else {
        statusBar()->showMessage(tr("«%1» creada en la capa '%2'")
                                     .arg(f.name, f.layerId), 5000);
    }
}

// Abre un historial GPX y lo reproduce con la barra de reproducción. La
// simulación se para: mezclar objetivos simulados y reproducidos confunde.
void Ventana::abrirReproduccion()
{
    const QString ruta = QFileDialog::getOpenFileName(this, tr("Reproducir historial"),
        dirTrabajo(), tr("GPX (*.gpx)"));
    if (ruta.isEmpty())
        return;
    if (m_accSimular && m_accSimular->isChecked())
        m_accSimular->setChecked(false);
    if (!m_player) {
        m_player = new TrackPlayer(m_mapa, this);
        crearBarraReproduccion();
    }
    QString error;
    if (!m_player->loadFile(ruta, &error)) {
        QMessageBox::warning(this, tr("Reproducir historial"),
                             tr("No se pudo cargar:\n%1").arg(error));
        return;
    }
    m_ultimoDir = QFileInfo(ruta).absolutePath();
    m_player->setRoutesVisible(m_repRutas->isChecked());
    m_barraRep->show();
    m_accSalirRep->setEnabled(true);
    m_modoRep->show();
    statusBar()->showMessage(tr("%1 pistas de %2 a %3. Pulsa ▶ para reproducir; "
                                "Esc o «Salir de la reproducción» para terminar.")
        .arg(m_player->tracks().size())
        .arg(m_player->startTime().toLocalTime().toString(QStringLiteral("dd/MM HH:mm:ss")),
             m_player->endTime().toLocalTime().toString(QStringLiteral("dd/MM HH:mm:ss"))),
        8000);
}

// Barra de reproducción (abajo): reproducir/pausa, parar, deslizador de
// tiempo (mil pasos sobre el rango del fichero), hora actual, velocidad,
// rutas completas y cerrar. El deslizador manda al reproductor y el
// reproductor al deslizador (con las señales bloqueadas, para no rebotar).
void Ventana::crearBarraReproduccion()
{
    m_barraRep = new QToolBar(tr("Reproducción"), this);
    m_barraRep->setObjectName(QStringLiteral("barraReproduccion"));
    m_barraRep->setMovable(false);
    // Que no se pueda ocultar desde el menú contextual de las barras: se
    // quedaría la reproducción activa sin controles. Se sale con su botón.
    m_barraRep->toggleViewAction()->setEnabled(false);
    addToolBar(Qt::BottomToolBarArea, m_barraRep);

    auto *play = m_barraRep->addAction(style()->standardIcon(QStyle::SP_MediaPlay), tr("Reproducir"));
    play->setCheckable(true);
    connect(play, &QAction::triggered, this,
            [this](bool on) { on ? m_player->play() : m_player->pause(); });
    connect(m_player, &TrackPlayer::playingChanged, this, [this, play](bool on) {
        QSignalBlocker b(play);
        play->setChecked(on);
        play->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    auto *parar = m_barraRep->addAction(style()->standardIcon(QStyle::SP_MediaStop), tr("Al principio"));
    connect(parar, &QAction::triggered, m_player, &TrackPlayer::stop);

    m_repSlider = new QSlider(Qt::Horizontal, m_barraRep);
    m_repSlider->setRange(0, 1000);
    m_repSlider->setMinimumWidth(300);
    m_barraRep->addWidget(m_repSlider);
    connect(m_repSlider, &QSlider::valueChanged, this, [this](int v) {
        const qint64 total = m_player->startTime().msecsTo(m_player->endTime());
        m_player->seek(m_player->startTime().addMSecs(total * v / 1000));
    });

    m_repHora = new QLabel(m_barraRep);
    m_repHora->setMinimumWidth(170);
    m_repHora->setAlignment(Qt::AlignCenter);
    m_barraRep->addWidget(m_repHora);
    connect(m_player, &TrackPlayer::timeChanged, this, [this](const QDateTime &t) {
        m_repHora->setText(t.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")));
        const qint64 total = m_player->startTime().msecsTo(m_player->endTime());
        QSignalBlocker b(m_repSlider);
        m_repSlider->setValue(total > 0 ? int(m_player->startTime().msecsTo(t) * 1000 / total) : 0);
    });

    auto *vel = new QComboBox(m_barraRep);
    for (int f : { 1, 10, 60, 300, 1800 })
        vel->addItem(QStringLiteral("×%1").arg(f), f);
    vel->setCurrentIndex(2);                       // ×60: un minuto por segundo
    vel->setToolTip(tr("Velocidad respecto al tiempo real"));
    m_barraRep->addWidget(vel);
    connect(vel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, vel](int) { m_player->setSpeed(vel->currentData().toDouble()); });

    m_repRutas = new QCheckBox(tr("Rutas"), m_barraRep);
    m_repRutas->setChecked(true);
    m_repRutas->setToolTip(tr("Recorrido completo de cada pista, con su inicio y su fin"));
    m_barraRep->addWidget(m_repRutas);
    connect(m_repRutas, &QCheckBox::toggled, m_player, &TrackPlayer::setRoutesVisible);

    m_barraRep->addSeparator();
    auto *salir = new QToolButton(m_barraRep);
    salir->setDefaultAction(m_accSalirRep);
    salir->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_barraRep->addWidget(salir);
}

// Sale del modo reproducción: el reproductor quita sus objetivos y sus
// rutas (el mapa queda como antes de abrir el historial), se cierra la
// barra y se apaga el indicador. Sin reproducción activa no hace nada.
void Ventana::salirReproduccion()
{
    if (!m_player || !m_player->isLoaded())
        return;
    m_player->clear();
    m_barraRep->hide();
    m_modoRep->hide();
    m_accSalirRep->setEnabled(false);
    statusBar()->showMessage(tr("Reproducción cerrada"), 4000);
}

// Activa o apaga las alertas de proximidad. Al activarlas pide distancia (en
// millas náuticas) y horizonte (minutos); se recuerdan para la próxima vez.
void Ventana::configurarProximidad(bool activar)
{
    if (!activar) {
        m_mapa->setProximityAlert(0.0);
        actualizarIndicadorAlertas();
        statusBar()->showMessage(tr("Alertas de proximidad apagadas"), 4000);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Alertas de proximidad (CPA/TCPA)"));
    auto *form = new QFormLayout(&dlg);
    auto *distancia = new QDoubleSpinBox(&dlg);
    distancia->setRange(0.05, 50.0);
    distancia->setDecimals(2);
    distancia->setSingleStep(0.25);
    distancia->setSuffix(tr(" NM"));
    distancia->setValue(m_cpaMillas);
    auto *horizonte = new QSpinBox(&dlg);
    horizonte->setRange(1, 240);
    horizonte->setSuffix(tr(" min"));
    horizonte->setValue(m_cpaMinutos);
    auto *explica = new QLabel(tr("Avisar si dos objetivos van a pasar a menos de una "
                                  "distancia dentro de un tiempo, suponiendo que "
                                  "mantienen rumbo y velocidad."), &dlg);
    explica->setWordWrap(true);
    form->addRow(explica);
    form->addRow(tr("Distancia (CPA):"), distancia);
    form->addRow(tr("Horizonte (TCPA):"), horizonte);
    auto *botones = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(botones, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(botones, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(botones);
    if (dlg.exec() != QDialog::Accepted) {
        m_accProximidad->setChecked(m_mapa->proximityAlertDistance() > 0);
        return;
    }
    m_cpaMillas = distancia->value();
    m_cpaMinutos = horizonte->value();
    m_mapa->setProximityAlert(m_cpaMillas * 1852.0, m_cpaMinutos * 60.0);
    actualizarIndicadorAlertas();
    statusBar()->showMessage(
        tr("Alertas de proximidad: menos de %1 NM en %2 min — %3 pares en alerta")
            .arg(m_cpaMillas, 0, 'f', 2).arg(m_cpaMinutos)
            .arg(m_mapa->proximityAlerts().size()), 6000);
}

// Texto de una alerta de proximidad: los dos objetivos (primera línea de su
// etiqueta), a qué distancia pasarán y en cuánto tiempo.
QString Ventana::textoProximidad(qint64 a, qint64 b, double cpa, double tcpa) const
{
    const auto nombre = [this](qint64 id) {
        const auto t = m_mapa->target(id);
        return t && !t->label.isEmpty() ? t->label.section(QLatin1Char('\n'), 0, 0)
                                        : tr("Objetivo %1").arg(id);
    };
    if (tcpa < 0)
        return tr("⚠ Proximidad: %1 y %2 ya están a menos de %3 NM")
            .arg(nombre(a), nombre(b)).arg(m_cpaMillas, 0, 'f', 2);
    return tr("⚠ CPA: %1 y %2 pasarán a %3 NM dentro de %4 min")
        .arg(nombre(a), nombre(b)).arg(cpa / 1852.0, 0, 'f', 2)
        .arg(qRound(tcpa / 60.0));
}

// Pide separación, número de anillos y radiales, y los pone alrededor del
// objetivo seleccionado (lo siguen al moverse) o, si no hay, del punto que
// se pinche a continuación.
void Ventana::ponerAnillos()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Anillos de distancia"));
    auto *form = new QFormLayout(&dlg);
    auto *sep = new QDoubleSpinBox(&dlg);
    sep->setRange(0.05, 100.0);
    sep->setDecimals(2);
    sep->setSuffix(tr(" NM"));
    sep->setValue(m_anillos.spacingMeters / 1852.0);
    auto *num = new QSpinBox(&dlg);
    num->setRange(1, 20);
    num->setValue(m_anillos.count);
    auto *radiales = new QCheckBox(tr("Radiales cada 30°"), &dlg);
    radiales->setChecked(m_anillos.spokeEveryDeg > 0);
    const qint64 sel = m_mapa->selectedTarget();
    auto *donde = new QLabel(sel >= 0 ? tr("Centro: el objetivo seleccionado (lo siguen).")
                                      : tr("Centro: pincha en el mapa tras aceptar."), &dlg);
    donde->setWordWrap(true);
    form->addRow(donde);
    form->addRow(tr("Separación:"), sep);
    form->addRow(tr("Anillos:"), num);
    form->addRow(radiales);
    auto *botones = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(botones, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(botones, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(botones);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_anillos.spacingMeters = sep->value() * 1852.0;
    m_anillos.count = num->value();
    m_anillos.spokeEveryDeg = radiales->isChecked() ? 30 : 0;
    if (sel >= 0) {
        RangeRings r = m_anillos;
        r.targetId = sel;
        m_mapa->addRangeRings(r);
        statusBar()->showMessage(tr("Anillos alrededor del objetivo %1").arg(sel), 4000);
        return;
    }
    iniciarPickMedida(Pick::Anillos, tr("Pincha el centro de los anillos…"));
}

// Marcación: desde el objetivo seleccionado (o un punto que se pinche) hasta
// otro objetivo o punto que se pinche. Si el destino cae sobre un objetivo,
// la línea cuelga de él y se actualiza al moverse.
void Ventana::ponerMarcacion()
{
    m_ebl = BearingLine();
    const qint64 sel = m_mapa->selectedTarget();
    if (sel >= 0) {
        m_ebl.fromTarget = sel;
        iniciarPickMedida(Pick::DestinoEbl,
            tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
    } else {
        iniciarPickMedida(Pick::OrigenEbl,
            tr("Pincha el origen de la marcación (un objetivo o un punto)…"));
    }
}

// Arma la herramienta de elegir punto para una medida, con su aviso.
void Ventana::iniciarPickMedida(Pick cual, const QString &aviso)
{
    m_picking = cual;
    m_mapa->setActiveTool(MapTool::PickPoint);
    statusBar()->showMessage(aviso, 10000);
}

// Objetivo a menos de ~14 px del punto (con la resolución actual), o -1.
qint64 Ventana::objetivoCerca(const QGeoCoordinate &p) const
{
    const double tolerancia = 14.0 * std::max(1.0, m_mapa->metersPerPixel());
    qint64 mejor = -1;
    double dMejor = tolerancia;
    for (const MapTarget &t : m_mapa->targets()) {
        const double d = t.position.distanceTo(p);
        if (d <= dMejor) {
            dMejor = d;
            mejor = t.id;
        }
    }
    return mejor;
}

// Punto pinchado para anillos o marcación.
void Ventana::puntoDeMedida(const QGeoCoordinate &p)
{
    const Pick cual = m_picking;
    m_picking = Pick::Ninguno;
    m_mapa->setActiveTool(MapTool::None);
    if (m_accNavegar)
        m_accNavegar->setChecked(true);
    const qint64 obj = objetivoCerca(p);
    if (cual == Pick::Anillos) {
        RangeRings r = m_anillos;
        r.targetId = obj;
        r.center = p;
        m_mapa->addRangeRings(r);
        statusBar()->showMessage(obj >= 0 ? tr("Anillos alrededor del objetivo %1").arg(obj)
                                          : tr("Anillos puestos"), 4000);
    } else if (cual == Pick::OrigenEbl) {
        m_ebl.fromTarget = obj;
        m_ebl.from = p;
        iniciarPickMedida(Pick::DestinoEbl,
            tr("Pincha el destino de la marcación (un objetivo o un punto)…"));
    } else {
        m_ebl.toTarget = obj;
        m_ebl.to = p;
        const qint64 id = m_mapa->addBearingLine(m_ebl);
        double rumbo = 0.0, dist = 0.0;
        if (m_mapa->bearingLineMeasure(id, &rumbo, &dist))
            statusBar()->showMessage(tr("Marcación %1° · %2 NM")
                .arg(qRound(rumbo), 3, 10, QLatin1Char('0'))
                .arg(dist / 1852.0, 0, 'f', 2), 6000);
    }
}

// Vigila (o deja de vigilar) el polígono seleccionado como zona de alerta.
// Al empezar, la librería ya avisa de los objetivos que estén dentro.
void Ventana::alternarVigilancia()
{
    const qint64 id = m_mapa->selectedFeature();
    const auto f = id >= 0 ? m_mapa->feature(id) : std::nullopt;
    if (!f || f->kind != GeometryKind::Polygon) {
        statusBar()->showMessage(tr("Selecciona primero un polígono para vigilarlo"), 4000);
        return;
    }
    const QString nombre = f->name.isEmpty() ? tr("zona %1").arg(id) : f->name;
    if (m_mapa->isZoneWatched(id)) {
        m_mapa->unwatchZone(id);
        m_noVigilar.insert(id);           // no volver a vigilarla sola
        actualizarIndicadorAlertas();
        statusBar()->showMessage(tr("Se deja de vigilar «%1»").arg(nombre), 4000);
        return;
    }
    m_noVigilar.remove(id);
    m_mapa->watchZone(id);
    actualizarIndicadorAlertas();
    statusBar()->showMessage(
        tr("Vigilando «%1»: %2 objetivos dentro")
            .arg(nombre).arg(m_mapa->targetsInZone(id).size()), 5000);
}

// Texto de una alerta de zona para la barra de estado: qué objetivo (primera
// línea de su etiqueta), qué zona y cuántos quedan dentro.
// \a esEntidad: el que cruza es una entidad punto (un «Objetivo» colocado)
// y no un objetivo móvil. El recuento suma ambos.
QString Ventana::textoAlertaZona(qint64 quienId, bool esEntidad, qint64 zona, bool entra) const
{
    QString quien;
    if (esEntidad) {
        const auto e = m_mapa->feature(quienId);
        quien = e && !e->name.isEmpty() ? e->name.section(QLatin1Char('\n'), 0, 0)
                                        : tr("Entidad %1").arg(quienId);
    } else {
        const auto t = m_mapa->target(quienId);
        quien = t && !t->label.isEmpty() ? t->label.section(QLatin1Char('\n'), 0, 0)
                                         : tr("Objetivo %1").arg(quienId);
    }
    const auto f = m_mapa->feature(zona);
    const QString donde = f && !f->name.isEmpty()
        ? f->name.section(QLatin1Char('\n'), 0, 0) : tr("zona %1").arg(zona);
    const int dentro = int(m_mapa->targetsInZone(zona).size()
                           + m_mapa->featuresInZone(zona).size());
    return (entra ? tr("⚠ %1 ENTRA en «%2» (%3 dentro)")
                  : tr("%1 sale de «%2» (%3 dentro)"))
        .arg(quien, donde).arg(dentro);
}

// Indicador fijo de la barra de estado: cuántos objetivos (móviles o
// colocados) hay ahora dentro de alguna zona vigilada. Rojo si hay alguno.
void Ventana::actualizarIndicadorAlertas()
{
    if (!m_alertas)
        return;
    QSet<qint64> moviles, puntos;
    for (qint64 z : m_mapa->watchedZones()) {
        for (qint64 t : m_mapa->targetsInZone(z))
            moviles.insert(t);
        for (qint64 p : m_mapa->featuresInZone(z))
            puntos.insert(p);
    }
    const int n = int(moviles.size() + puntos.size());
    const int zonas = int(m_mapa->watchedZones().size());
    const int cpa = int(m_mapa->proximityAlerts().size());
    QStringList partes;
    if (zonas > 0)
        partes << (n == 0 ? tr("Zonas vigiladas: %1").arg(zonas) : tr("⚠ %1 en zonas").arg(n));
    if (m_mapa->proximityAlertDistance() > 0)
        partes << (cpa == 0 ? tr("CPA vigilado") : tr("⚠ %1 CPA").arg(cpa));
    m_alertas->setText(partes.join(QStringLiteral(" · ")));
    m_alertas->setStyleSheet(n + cpa > 0 ? QStringLiteral("QLabel { color:#fff; background:#d50032;"
                                                          " font-weight:bold; padding:0 6px; }")
                                         : QString());
}

// Vigila automáticamente toda «Área objetivo» (al crearla, cargarla o
// deshacer), salvo las que el usuario desactivó a mano.
void Ventana::vigilarAreasObjetivo()
{
    bool alguna = false;
    for (const MapFeature &f : m_mapa->featuresOfType(QStringLiteral("objetivo_area"))) {
        if (f.kind != GeometryKind::Polygon || m_noVigilar.contains(f.id)
            || m_mapa->isZoneWatched(f.id))
            continue;
        alguna = m_mapa->watchZone(f.id) || alguna;
    }
    if (alguna)
        actualizarIndicadorAlertas();
}

// Arranca o para la simulación de objetivos móviles.
void Ventana::alternarSimulacion(bool on)
{
    if (on) {
        salirReproduccion();          // en vivo y reproducción no se mezclan
        iniciarSimulacion(m_numObjetivos->value());
        m_simReloj->start();
    } else {
        m_simReloj->stop();
        m_mapa->clearTargets();
        m_simIds.clear();
        m_simVel.clear();
        statusBar()->showMessage(tr("Simulacion detenida"), 3000);
    }
    m_numObjetivos->setEnabled(!on);
}

// Crea \a n objetivos simulados (buques, aéreos y UAV) con rumbo y velocidad al azar.
void Ventana::iniciarSimulacion(int n)
{
    m_mapa->clearTargets();
    m_simIds.clear();
    m_simVel.clear();
    m_simHora = QDateTime::currentDateTimeUtc();
    m_simIds.reserve(n);
    m_simVel.reserve(n);

    auto *r = QRandomGenerator::global();
    // Tres clases de objetivo repartidas. Cada una lleva SUS datos en
    // attributes (la libreria no los interpreta) y su etiqueta se compone de
    // ellos: asi se ven las Fases 1 (datos) y 2 (icono por kind/estado).
    static const char *clases[] = { "buque", "aeronave", "uav" };

    for (int i = 0; i < n; ++i) {
        // Repartidos por el mar alrededor de Cuba.
        const double lat = 19.5 + r->bounded(4.5);       // 19.5 .. 24.0
        const double lon = -85.0 + r->bounded(11.0);     // -85 .. -74
        const double rumbo = r->bounded(360.0);
        const double velGrados = 0.002 + r->bounded(0.004);  // por paso

        const QString kind = QLatin1String(clases[i % 3]);
        MapTarget t;
        t.position = QGeoCoordinate(lat, lon);
        t.headingDeg = rumbo;
        t.kind = kind;

        if (kind == QLatin1String("buque")) {
            const int mmsi = 224000000 + r->bounded(999999);
            // La velocidad REAL a la que se moverá en la simulación (grados
            // por paso de kSegundosPorPaso), no un número al azar: así el
            // rótulo coincide con lo que estima la librería por la traza y
            // con lo que filtra «Filtrar objetivos» por velocidad.
            const int nudos = int(std::lround(velGrados * 111195.0 / kSegundosPorPaso
                                              * 3600.0 / 1852.0));
            t.speed = nudos;
            t.attributes.insert(QStringLiteral("mmsi"), mmsi);
            t.attributes.insert(QStringLiteral("eslora"), 40 + r->bounded(260));
            t.label = QStringLiteral("Buque %1\nMMSI %2\n%3 kn")
                          .arg(i + 1).arg(mmsi).arg(nudos);
            t.color = QColor(0x20, 0x6a, 0xd0);
        } else if (kind == QLatin1String("aeronave")) {
            const QString cs = QStringLiteral("CUB%1").arg(100 + r->bounded(900));
            const int fl = 80 + r->bounded(320);        // nivel de vuelo
            t.attributes.insert(QStringLiteral("callsign"), cs);
            t.attributes.insert(QStringLiteral("squawk"),
                                QStringLiteral("%1").arg(1000 + r->bounded(6000)));
            t.attributes.insert(QStringLiteral("fl"), fl);
            t.label = QStringLiteral("%1\nFL%2")
                          .arg(cs).arg(fl, 3, 10, QLatin1Char('0'));
            t.color = QColor(0x0c, 0x97, 0x8a);
        } else {                                         // uav
            const int bat = r->bounded(100);
            t.attributes.insert(QStringLiteral("bateria"), bat);
            t.attributes.insert(QStringLiteral("enlace"), 60 + r->bounded(40));
            t.label = QStringLiteral("UAV %1\nBat %2%").arg(i + 1).arg(bat);
            t.color = bat < 20 ? QColor(0xd0, 0x3a, 0x2a)
                               : QColor(0x3c, 0xb0, 0x4a);
        }

        const qint64 id = m_mapa->addTarget(t, m_simHora);
        m_simIds.append(id);

        const double rad = qDegreesToRadians(rumbo);
        // Componentes: x = variacion de longitud, y = variacion de latitud.
        m_simVel.append(QPointF(std::sin(rad) * velGrados,
                                std::cos(rad) * velGrados));
    }
    statusBar()->showMessage(tr("Simulando %1 objetivos").arg(n), 3000);
}

// Un paso de la simulación. Es un «time-lapse»: cada paso (100 ms reales)
// son kSegundosPorPaso de tiempo simulado, y esa hora se pasa a la
// librería. Así la velocidad que estima por la traza (para el CPA/TCPA) es
// la que se ve: 0.002–0.006° por paso son 7–22 m/s, y no kilómetros por
// segundo de reloj de pared.
void Ventana::pasoSimulacion()
{
    auto *r = QRandomGenerator::global();
    m_simHora = m_simHora.addSecs(kSegundosPorPaso);
    for (int i = 0; i < m_simIds.size(); ++i) {
        QPointF &v = m_simVel[i];
        const auto t = m_mapa->target(m_simIds[i]);
        if (!t)
            continue;

        double lat = t->position.latitude() + v.y();
        double lon = t->position.longitude() + v.x();

        // Rebote contra los limites del area para que no se escapen.
        if (lat < 19.0 || lat > 24.5) { v.setY(-v.y()); lat = t->position.latitude(); }
        if (lon < -86.0 || lon > -73.0) { v.setX(-v.x()); lon = t->position.longitude(); }

        // Un pequeno viraje aleatorio de vez en cuando.
        if (r->bounded(100) < 3) {
            const double giro = (r->bounded(2.0) - 1.0) * 0.2;
            const double c = std::cos(giro), s = std::sin(giro);
            const double nx = v.x() * c - v.y() * s;
            const double ny = v.x() * s + v.y() * c;
            v.setX(nx);
            v.setY(ny);
        }

        const double rumbo = qRadiansToDegrees(std::atan2(v.x(), v.y()));
        m_mapa->updateTarget(m_simIds[i], QGeoCoordinate(lat, lon), rumbo, m_simHora);
    }
    actualizarEstado();
}

// Diálogo del filtro de objetivos: tipos presentes (casillas), velocidad
// mínima y máxima en nudos (0 = sin límite), «solo en zonas vigiladas» y
// ocultar o atenuar el resto. «Quitar filtro» vuelve a mostrarlos todos.
void Ventana::filtrarObjetivos()
{
    QStringList tipos;
    for (const MapTarget &t : m_mapa->targets())
        if (!t.kind.isEmpty() && !tipos.contains(t.kind))
            tipos << t.kind;
    for (const QString &k : m_filtro.kinds)
        if (!tipos.contains(k))
            tipos << k;
    tipos.sort();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Filtrar objetivos"));
    auto *form = new QFormLayout(&dlg);
    auto *cajaTipos = new QGroupBox(tr("Tipos"), &dlg);
    auto *lTipos = new QVBoxLayout(cajaTipos);
    QList<QCheckBox *> marcas;
    for (const QString &k : tipos) {
        auto *c = new QCheckBox(k, cajaTipos);
        c->setChecked(m_filtro.kinds.isEmpty() || m_filtro.kinds.contains(k));
        lTipos->addWidget(c);
        marcas << c;
    }
    if (tipos.isEmpty())
        lTipos->addWidget(new QLabel(tr("(no hay objetivos)"), cajaTipos));
    form->addRow(cajaTipos);
    const auto nudos = [&dlg](double ms) {
        auto *sp = new QDoubleSpinBox(&dlg);
        sp->setRange(0.0, 2000.0);
        sp->setDecimals(1);
        sp->setSuffix(tr(" kn"));
        sp->setSpecialValueText(tr("sin límite"));
        sp->setValue(std::isnan(ms) ? 0.0 : ms * 3600.0 / 1852.0);
        return sp;
    };
    auto *vMin = nudos(m_filtro.minSpeed);
    auto *vMax = nudos(m_filtro.maxSpeed);
    form->addRow(tr("Velocidad mínima:"), vMin);
    form->addRow(tr("Velocidad máxima:"), vMax);
    auto *zonas = new QCheckBox(tr("Solo dentro de zonas vigiladas"), &dlg);
    zonas->setChecked(m_filtro.onlyInWatchedZones);
    form->addRow(zonas);
    auto *atenuar = new QCheckBox(tr("Atenuar el resto en vez de ocultarlo"), &dlg);
    atenuar->setChecked(m_filtro.dim);
    form->addRow(atenuar);
    auto *botones = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto *quitar = botones->addButton(tr("Quitar filtro"), QDialogButtonBox::ResetRole);
    connect(botones, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(botones, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    bool quitarFiltro = false;
    connect(quitar, &QPushButton::clicked, &dlg, [&] { quitarFiltro = true; dlg.accept(); });
    form->addRow(botones);
    if (dlg.exec() != QDialog::Accepted)
        return;

    if (quitarFiltro) {
        m_filtro = TargetFilter();
        m_mapa->clearTargetFilter();
        statusBar()->showMessage(tr("Filtro quitado: se ven todos los objetivos"), 4000);
        actualizarEstado();
        return;
    }
    TargetFilter f;
    bool todos = true;
    for (QCheckBox *c : marcas) {
        if (c->isChecked())
            f.kinds << c->text();
        else
            todos = false;
    }
    if (todos)
        f.kinds.clear();                       // todos marcados = no filtrar por tipo
    else if (f.kinds.isEmpty())
        f.kinds << QStringLiteral("\x01");    // ninguno marcado: no pasa ninguno
    const double kn = 1852.0 / 3600.0;
    if (vMin->value() > 0.0)
        f.minSpeed = vMin->value() * kn;
    if (vMax->value() > 0.0)
        f.maxSpeed = vMax->value() * kn;
    f.onlyInWatchedZones = zonas->isChecked();
    f.dim = atenuar->isChecked();
    m_filtro = f;
    m_mapa->setTargetFilter(f);
    actualizarEstado();
    const int total = m_mapa->targetCount();
    statusBar()->showMessage(m_mapa->hasTargetFilter()
        ? tr("Filtro aplicado: %1 de %2 objetivos").arg(total - m_mapa->filteredOutTargets().size()).arg(total)
        : tr("Sin filtro: se ven todos los objetivos"), 5000);
}
