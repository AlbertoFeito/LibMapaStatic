// Pestaña Elevación: perfil, visión, viewshed, picos y captura de puntos del mapa.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

#include "qcustomplot.h"      // ya compilado dentro de libmapa_widget; aquí solo se usa

using namespace libmapa;

// Pestaña lateral para PROBAR los tres calculos sobre el DEM activo: perfil de
// una ruta, linea de vision entre dos puntos y viewshed 360. La libreria
// devuelve los datos; aqui se dibujan (ventana flotante del perfil; linea de
// vision y zona de visibilidad como entidades sobre el mapa).
QWidget *Ventana::construirTabElevacion()
{
    auto *tab = new QWidget;
    auto *caja = new QVBoxLayout(tab);
    caja->setContentsMargins(8, 8, 8, 8);

    // Selector de análisis: cada uno pide SOLO sus datos (las demás filas se
    // ocultan). Así la entrada no es fija: cambia según lo que vayas a calcular.
    m_tipoAnalisis = new QComboBox(tab);
    m_tipoAnalisis->addItems(QStringList{ tr("Perfil"), tr("Visión A→B"),
                                          tr("Viewshed"), tr("10 picos altos") });
    {
        auto *fc = new QFormLayout;
        fc->addRow(tr("Análisis:"), m_tipoAnalisis);
        caja->addLayout(fc);
    }

    // Crea una fila [etiqueta + contenido] que se puede mostrar/ocultar entera.
    auto crearFila = [&](const QString &etiqueta, QWidget *contenido, QLabel **outLbl) {
        auto *lbl = new QLabel(etiqueta, tab);
        lbl->setMinimumWidth(96);
        auto *h = new QHBoxLayout; h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(lbl, 0); h->addWidget(contenido, 1);
        auto *w = new QWidget(tab); w->setLayout(h);
        if (outLbl) *outLbl = lbl;
        caja->addWidget(w);
        return w;
    };

    // Contenido de un punto: lat/lon + botón «Mapa» (pin del color del punto).
    auto contenidoPunto = [&](QDoubleSpinBox *&lat, QDoubleSpinBox *&lon, Pick cual) {
        lat = new QDoubleSpinBox(tab);
        lat->setRange(-90.0, 90.0); lat->setDecimals(5); lat->setSingleStep(0.001);
        lat->setToolTip(tr("Latitud"));
        lon = new QDoubleSpinBox(tab);
        lon->setRange(-180.0, 180.0); lon->setDecimals(5); lon->setSingleStep(0.001);
        lon->setToolTip(tr("Longitud"));
        auto *pick = new QPushButton(tr("Mapa"), tab);
        pick->setIcon(QIcon(pinPixmap(cual == Pick::A ? m_colorA : m_colorB)));
        pick->setToolTip(tr("Fija este punto con el siguiente clic en el mapa."));
        connect(pick, &QPushButton::clicked, this, [this, cual] { iniciarPick(cual); });
        connect(lat, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { actualizarInfoPuntos(); });
        connect(lon, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { actualizarInfoPuntos(); });
        auto *h = new QHBoxLayout; h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(lat, 1); h->addWidget(lon, 1); h->addWidget(pick, 0);
        auto *w = new QWidget(tab); w->setLayout(h);
        return w;
    };

    m_altA = new QDoubleSpinBox(tab); m_altA->setRange(0.0, 20000.0); m_altA->setValue(10.0); m_altA->setSuffix(tr(" m"));
    m_altB = new QDoubleSpinBox(tab); m_altB->setRange(0.0, 20000.0); m_altB->setValue(10.0); m_altB->setSuffix(tr(" m"));
    m_rumbo = new QDoubleSpinBox(tab); m_rumbo->setRange(0.0, 359.9); m_rumbo->setDecimals(1); m_rumbo->setSuffix(tr(" °")); m_rumbo->setValue(180.0); m_rumbo->setWrapping(true);
    m_alcanceKm = new QDoubleSpinBox(tab); m_alcanceKm->setRange(1.0, 400.0); m_alcanceKm->setValue(40.0); m_alcanceKm->setSuffix(tr(" km"));
    m_picosRadioKm = new QDoubleSpinBox(tab); m_picosRadioKm->setRange(1.0, 50.0); m_picosRadioKm->setValue(10.0); m_picosRadioKm->setSuffix(tr(" km"));
    m_picosSepM = new QSpinBox(tab); m_picosSepM->setRange(50, 5000); m_picosSepM->setValue(800); m_picosSepM->setSingleStep(50); m_picosSepM->setSuffix(tr(" m"));

    m_rowPosA   = crearFila(tr("Posición A:"),  contenidoPunto(m_latA, m_lonA, Pick::A), &m_lblPosA);
    m_rowPosB   = crearFila(tr("Posición B:"),  contenidoPunto(m_latB, m_lonB, Pick::B), nullptr);
    m_rowAzimut = crearFila(tr("Azimut:"),       m_rumbo,        nullptr);
    m_rowDist   = crearFila(tr("Distancia:"),    m_alcanceKm,    &m_lblDist);
    m_rowAnt1   = crearFila(tr("Antena 1:"),     m_altA,         &m_lblAnt1);
    m_rowAnt2   = crearFila(tr("Antena 2:"),     m_altB,         &m_lblAnt2);
    m_rowRadio  = crearFila(tr("Radio:"),        m_picosRadioKm, nullptr);
    m_rowSep    = crearFila(tr("Separación:"),   m_picosSepM,    nullptr);

    // Lectura: cota del terreno y, en Visión, rumbo/distancia A→B.
    m_infoPuntos = new QLabel(tab);
    m_infoPuntos->setTextFormat(Qt::RichText);
    m_infoPuntos->setWordWrap(true);
    caja->addWidget(m_infoPuntos);

    // Puntos A y B por defecto: centro del mapa y 20 km al este (así la Visión
    // A→B arranca con una línea válida sin tener que pinchar primero).
    const QGeoCoordinate c = m_mapa->center();
    if (c.isValid()) {
        m_latA->setValue(c.latitude()); m_lonA->setValue(c.longitude());
        const QGeoCoordinate b = c.atDistanceAndAzimuth(20000.0, 90.0);
        m_latB->setValue(b.latitude()); m_lonB->setValue(b.longitude());
    }
    actualizarInfoPuntos();
    connect(m_mapa, &MapWidget::pointPicked, this,
            [this](const QGeoCoordinate &p) { onPointPicked(p); });
    // El filtro va sobre la MapView (el QCustomPlot interno) para interceptar
    // el clic ANTES de que lo use para desplazar: así se arrastran los pines.
    m_plotObj = m_mapa->customPlot();
    if (m_plotObj)
        m_plotObj->installEventFilter(this);

    // Las capas de análisis y los pines A/B son TEMPORALES: viven solo
    // durante la sesión y NO se guardan con las entidades del usuario.
    for (const QString &id : {kCapaPuntos, kCapaVision, kCapaViewshed, kCapaPicos})
        m_mapa->setFeatureLayerTransient(id);

    m_marComo0 = new QCheckBox(tr("Mar / sin dato = 0 m"), tab);
    m_marComo0->setChecked(true);
    m_marComo0->setToolTip(tr("Trata los huecos del DEM (mar, fuera de cobertura) "
                              "como cota 0, para analizar objetivos sobre el mar."));
    m_curvatura = new QCheckBox(tr("Curvatura 4/3"), tab);
    m_curvatura->setChecked(true);
    m_curvatura->setToolTip(tr("Con curvatura, el mar llano se oculta tras el "
                               "horizonte geométrico. Desactívala para analizar "
                               "solo el enmascaramiento por terreno (sobre mar sin "
                               "obstáculos se ve hasta el alcance máximo)."));
    {
        auto *h = new QHBoxLayout; h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_marComo0); h->addWidget(m_curvatura);
        m_rowChecks = new QWidget(tab); m_rowChecks->setLayout(h);
        caja->addWidget(m_rowChecks);
    }

    // Botones: «Calcular» lanza el análisis del selector; «Limpiar» borra.
    auto *filaBtn = new QHBoxLayout;
    auto *bCalcular = new QPushButton(tr("Calcular"), tab);
    auto *bLimpiar = new QPushButton(tr("Limpiar"), tab);
    filaBtn->addWidget(bCalcular, 1); filaBtn->addWidget(bLimpiar, 0);
    caja->addLayout(filaBtn);

    connect(bCalcular, &QPushButton::clicked, this, [this] {
        switch (m_tipoAnalisis->currentIndex()) {
        case 0: analizarPerfil();     break;
        case 1: analizarVision();     break;
        case 2: analizarViewshed();   break;
        case 3: analizarPicosAltos(); break;
        }
    });
    // Mismo slot que el menú «Elevación → Limpiar análisis» (sin duplicar).
    connect(bLimpiar, &QPushButton::clicked, this, &Ventana::limpiarAnalisis);
    // Los checks re-aplican al instante el último análisis.
    connect(m_marComo0, &QCheckBox::toggled, this, [this] { reejecutar(); });
    connect(m_curvatura, &QCheckBox::toggled, this, [this] { reejecutar(); });
    // Al cambiar de análisis, se muestran solo sus campos y se actualizan pines.
    connect(m_tipoAnalisis, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) { actualizarEntradaAnalisis(i); });
    actualizarEntradaAnalisis(0);   // estado inicial (Perfil)

    // --- Relieve sombreado (hillshade) en vivo, desde el DEM local ---------
    auto *grupoHs = new QGroupBox(tr("Relieve sombreado (hillshade)"), tab);
    grupoHs->setCheckable(true);
    grupoHs->setChecked(false);
    auto *fHs = new QFormLayout(grupoHs);
    auto *az = new QDoubleSpinBox(grupoHs);
    az->setRange(0.0, 359.0); az->setValue(315.0); az->setSuffix(tr(" °")); az->setWrapping(true);
    auto *alt = new QDoubleSpinBox(grupoHs);
    alt->setRange(1.0, 89.0); alt->setValue(45.0); alt->setSuffix(tr(" °"));
    auto *filaSol = new QHBoxLayout; filaSol->setContentsMargins(0, 0, 0, 0);
    filaSol->addWidget(az, 1); filaSol->addWidget(alt, 1);
    auto *wSol = new QWidget(grupoHs); wSol->setLayout(filaSol);
    fHs->addRow(tr("Sol (azim/alt):"), wSol);
    auto *inten = new QSpinBox(grupoHs);
    inten->setRange(0, 100); inten->setValue(60); inten->setSuffix(tr(" %"));
    fHs->addRow(tr("Intensidad:"), inten);
    auto *exag = new QDoubleSpinBox(grupoHs);
    exag->setRange(1.0, 6.0); exag->setValue(2.0); exag->setSingleStep(0.5); exag->setSuffix(tr("×"));
    fHs->addRow(tr("Exageración:"), exag);
    auto *color = new QCheckBox(tr("Tintar por altura"), grupoHs);
    fHs->addRow(QString(), color);
    caja->addWidget(grupoHs);

    grupoHs->setToolTip(tr("Sombreado del terreno calculado en vivo del DEM local (sin conexión). "
                           "Se recalcula al mover la vista. A vista muy general (p. ej. Cuba "
                           "entera) no se dibuja: acércate para verlo."));
    m_mapa->setHillshadeSun(az->value(), alt->value());
    m_mapa->setHillshadeOpacity(inten->value() / 100.0);
    m_mapa->setHillshadeExaggeration(exag->value());
    connect(grupoHs, &QGroupBox::toggled, this, [this](bool on) { m_mapa->setHillshadeVisible(on); });
    auto aplicarSol = [this, az, alt] { m_mapa->setHillshadeSun(az->value(), alt->value()); };
    connect(az, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [aplicarSol](double) { aplicarSol(); });
    connect(alt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [aplicarSol](double) { aplicarSol(); });
    connect(inten, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { m_mapa->setHillshadeOpacity(v / 100.0); });
    connect(exag, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_mapa->setHillshadeExaggeration(v); });
    connect(color, &QCheckBox::toggled, this, [this](bool on) { m_mapa->setHillshadeColored(on); });

    m_resultado = new QLabel(tab);
    m_resultado->setWordWrap(true);
    m_resultado->setTextFormat(Qt::RichText);
    m_resultado->setMinimumHeight(60);
    m_resultado->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    caja->addWidget(m_resultado);

    auto *ayuda = new QLabel(
        tr("<span style='color:#777'>Elige el <b>Análisis</b> arriba: solo pide sus datos. "
           "Fija las posiciones con «Mapa» (el cursor toma el pin; azul=A, rojo=B) o "
           "escríbelas. <b>Perfil</b>: posición + azimut + distancia. <b>Visión A→B</b>: "
           "posiciones 1 y 2 + antenas. <b>Viewshed</b>: observador + antena + alcance + "
           "altura objetivo. <b>10 picos</b>: posición + radio/separación. Pulsa "
           "<b>Calcular</b>. El perfil se abre en una ventana aparte (arrastrar = "
           "desplazar, rueda = zoom).</span>"), tab);
    ayuda->setWordWrap(true);
    caja->addWidget(ayuda);
    caja->addStretch(1);
    return tab;
}

// Re-ejecuta el último análisis (p. ej. al cambiar un check). Nada si ninguno.
void Ventana::reejecutar()
{
    switch (m_ultimo) {
    case Analisis::Perfil:   analizarPerfil();   break;
    case Analisis::Vision:   analizarVision();   break;
    case Analisis::Viewshed: analizarViewshed(); break;
    case Analisis::Picos:    analizarPicosAltos(); break;
    case Analisis::Ninguno:  break;
    }
}

// Abre (o reutiliza) la ventana flotante del perfil y la actualiza. Con
// \a conVision dibuja la línea de visión recta entre antenas (cimas absolutas
// \a zA en A y \a zB en B, a distancia \a D) y marca el obstáculo \a critD;
// \a maxInicial fija la ventana inicial en X.
void Ventana::mostrarPerfil(const ElevationProfile &p, const QString &resumen,
                   double maxInicial, bool conVision,
                   double zA, double zB, double D,
                   double critD, bool bloqueado)
{
    if (!m_perfilWin) {
        m_perfilWin = new QDialog(this);
        m_perfilWin->setWindowTitle(tr("Perfil del terreno"));
        m_perfilWin->resize(960, 500);
        auto *lay = new QVBoxLayout(m_perfilWin);
        m_perfilInfo = new QLabel(m_perfilWin);
        m_perfilInfo->setWordWrap(true);
        lay->addWidget(m_perfilInfo);
        // Gráfica con QCustomPlot: arrastre y zoom (rueda) en ambos ejes.
        m_plot = new QCustomPlot(m_perfilWin);
        m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend);
        m_plot->legend->setVisible(true);
        m_plot->xAxis->setLabel(tr("Distancia (m)"));
        m_plot->yAxis->setLabel(tr("Altura (m)"));
        lay->addWidget(m_plot, 1);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, m_perfilWin);
        connect(bb, &QDialogButtonBox::rejected, m_perfilWin, &QDialog::hide);
        lay->addWidget(bb);
    }
    m_perfilInfo->setText(resumen);
    pintarPerfil(p, conVision, zA, zB, D, critD, bloqueado, maxInicial);
    m_perfilWin->show();
    m_perfilWin->raise();
    m_perfilWin->activateWindow();
}

// Dibuja el perfil en el QCustomPlot al estilo DVD_potencial: terreno CRUDO
// (verde, relleno hasta la curva de curvatura), curva de curvatura de la Tierra
// (−d²/2kR), y —si hay visión— la recta de visibilidad A→B, los mástiles de
// antena, el obstáculo y la línea de sombra hasta el final.
void Ventana::pintarPerfil(const ElevationProfile &p, bool conVision,
                  double zA, double zB, double D, double critD, bool bloqueado,
                  double maxInicialM)
{
    m_plot->clearGraphs();
    m_plot->clearItems();
    if (p.samples.size() < 2) { m_plot->replot(); return; }

    const bool curv = curvaturaOn();
    const double k = 4.0 / 3.0, R = 6371000.0;
    auto caida = [&](double d) { return curv ? (d * d) / (2.0 * k * R) : 0.0; };

    // Modelo al estilo DVD: el eje Y refleja la CURVATURA de la Tierra. Todo se
    // hunde con la distancia restando caida(d)=d²/2kR. Así el NIVEL DEL MAR es
    // una sola línea que baja con la distancia (y = −caida(d)); el mar NO se
    // rellena, es esa línea. El terreno sobre el mar (cota ≤ 0: mar o batimetría)
    // se trata como superficie 0 y RIELA sobre esa línea; la tierra firme (cota
    // > 0) va a su altura, también hundida. terreno dibujado = max(cota,0) −
    // caida(d); nivel del mar = −caida(d).
    auto superf = [](double e) { return std::isnan(e) ? 0.0 : qMax(e, 0.0); };
    QVector<double> dx, mar, terr, objetivo;
    double dFin = 0.0, yTop = -1e18, yBot = 1e18;
    for (const ElevationSample &s : p.samples) {
        const double d = s.distanceM;
        const double nivelMar = -caida(d);                 // mar hundido por curvatura
        const double yt = superf(s.elevation) - caida(d);  // terreno (mar = nivel del mar)
        dx << d; mar << nivelMar; terr << yt;
        if (conVision) objetivo << yt + m_altB->value();
        dFin = d;
        yTop = qMax(yTop, yt); yBot = qMin(yBot, nivelMar);
    }
    if (dx.size() < 2) { m_plot->replot(); return; }

    // Nivel del mar (una línea azul fina que baja con la curvatura). El mar es
    // SOLO esta línea: no se rellena.
    QCPGraph *gMar = m_plot->addGraph();
    gMar->setName(tr("Nivel del mar"));
    gMar->setPen(QPen(QColor(0x1e, 0x88, 0xe5), 1));
    gMar->setData(dx, mar);

    // Terreno (verde), relleno por canal hasta el nivel del mar = el cuerpo de
    // tierra sobre el agua. Donde hay mar, terreno = nivel del mar → relleno nulo.
    QCPGraph *gTerr = m_plot->addGraph();
    gTerr->setName(tr("Perfil del terreno"));
    gTerr->setPen(QPen(QColor(0x1b, 0x5e, 0x20), 1.4));
    gTerr->setBrush(QBrush(QColor(0x2e, 0x7d, 0x32, 90)));
    gTerr->setChannelFillGraph(gMar);
    gTerr->setData(dx, terr);

    // Terreno-superficie hundido a una distancia d (para mástiles y obstáculo).
    auto terrPlot = [&](double d) {
        return superf(terrEn(p, d)) - caida(d);
    };
    if (conVision && D > 0.0) {
        const double zBp = zB - caida(D);        // cima del objetivo, ya hundida
        // Recta de visibilidad A→B (naranja): recta en el plano hundido, de la
        // cima de antena en A a la del objetivo en B (que riela sobre el mar).
        QCPGraph *gVis = m_plot->addGraph();
        gVis->setName(tr("Recta de visibilidad"));
        gVis->setPen(QPen(QColor(0xef, 0x6c, 0x00), 2, Qt::DashLine));
        gVis->setData({0.0, D}, {zA, zBp});
        yTop = qMax(yTop, qMax(zA, zBp));

        // Objetivo a Alt2 sobre el terreno (ya hundido).
        QCPGraph *gObj = m_plot->addGraph();
        gObj->setName(tr("Objetivo a %1 m sobre el terreno").arg(m_altB->value(), 0, 'f', 0));
        gObj->setPen(QPen(QColor(0x15, 0x65, 0xc0), 1, Qt::DotLine));
        gObj->setData(dx, objetivo);

        // Mástiles de antena en A y B (del terreno hundido a la cima).
        mastil(0.0, terrPlot(0.0), zA);
        mastil(D, terrPlot(D), zBp);

        // ÁNGULO DE CIERRE y picos de la silueta. Desde el observador en (0, zA),
        // la tangente del ángulo de cierre a una muestra es (y − zA)/d; cada
        // muestra que supera el máximo acumulado es un PICO que eleva el horizonte.
        // El de mayor ángulo hasta B es el que DE VERDAD tapa el objetivo.
        QVector<double> picoD, picoY;
        double domD = -1.0, domY = 0.0;
        double maxTan = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < dx.size(); ++i) {
            if (dx[i] <= 1.0) continue;
            const double tang = (terr[i] - zA) / dx[i];
            if (tang > maxTan) {                       // nuevo máximo → pico
                maxTan = tang;
                picoD << dx[i]; picoY << terr[i];
                if (dx[i] <= D) { domD = dx[i]; domY = terr[i]; }   // dominante hasta B
            }
        }
        if (domD < 0.0 && critD >= 0.0) {              // sin pico hasta B: corte más justo
            domD = critD; domY = terrPlot(critD);
        }

        // Tracers (círculos) en los picos de la silueta.
        QCPGraph *gPicos = m_plot->addGraph();
        gPicos->setName(tr("Picos (ángulo de cierre)"));
        gPicos->setLineStyle(QCPGraph::lsNone);
        gPicos->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle,
                                                QColor(0x37, 0x47, 0x4f), 6));
        gPicos->setData(picoD, picoY);

        // Recta tangente del observador por el PICO DOMINANTE (el que tapa),
        // EXTENDIDA hasta la distancia máxima: es la línea de cierre que limita
        // la visibilidad (lo que queda por debajo detrás del pico está oculto).
        // Rojo si bloquea, morado si no.
        if (domD > 0.0) {
            const QColor c = bloqueado ? QColor(0xc6, 0x28, 0x28)
                                       : QColor(0x6a, 0x1b, 0x9a);
            const double pend = (domY - zA) / domD;   // pendiente del ángulo de cierre
            const double yFin = zA + pend * dFin;     // extendida hasta el final
            QCPGraph *gCierre = m_plot->addGraph();
            gCierre->setName(bloqueado ? tr("Recta al pico que tapa")
                                       : tr("Recta al pico dominante"));
            gCierre->setPen(QPen(c, 1.8));
            gCierre->setData({0.0, dFin}, {zA, yFin});
            marcaObstaculo(domD, domY, c);            // tracer en el pico dominante
            yTop = qMax(yTop, qMax(domY, yFin));
        }
    }

    const double margen = qMax(10.0, (yTop - yBot) * 0.08);
    m_plot->yAxis->setRange(yBot - margen, yTop + margen);
    const double xMax = (maxInicialM > 0.0) ? qMin(maxInicialM, dFin) : dFin;
    m_plot->xAxis->setRange(0.0, xMax > 0.0 ? xMax : dFin);
    m_plot->replot();
}

// Cota del terreno (interpolada) a distancia d del perfil, o NaN.
double Ventana::terrEn(const ElevationProfile &p, double d)
{
    const auto &s = p.samples;
    for (int i = 1; i < s.size(); ++i) {
        if (s[i].distanceM >= d) {
            const double e0 = s[i - 1].elevation, e1 = s[i].elevation;
            if (std::isnan(e0) || std::isnan(e1)) return std::numeric_limits<double>::quiet_NaN();
            const double d0 = s[i - 1].distanceM, d1 = s[i].distanceM;
            const double t = (d1 > d0) ? (d - d0) / (d1 - d0) : 0.0;
            return e0 + (e1 - e0) * t;
        }
    }
    return s.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : s.last().elevation;
}

// Mástil de antena: línea vertical del terreno a la cima, con un punto arriba.
void Ventana::mastil(double d, double base, double cima)
{
    if (std::isnan(base)) base = cima;
    auto *l = new QCPItemLine(m_plot);
    l->setPen(QPen(QColor(0x33, 0x33, 0x33), 2));
    l->start->setCoords(d, base);
    l->end->setCoords(d, cima);
}

// Marca del pico/obstáculo: punto sobre el terreno (rojo por defecto).
void Ventana::marcaObstaculo(double d, double cota, const QColor &c)
{
    if (std::isnan(cota)) return;
    auto *t = new QCPItemTracer(m_plot);
    t->setStyle(QCPItemTracer::tsCircle);
    t->setPen(QPen(c));
    t->setBrush(c);
    t->setSize(8);
    t->position->setCoords(d, cota);
}

// Comprueba que hay DEM; si no, avisa y devuelve false.
bool Ventana::exigirDem()
{
    if (m_demActivo)
        return true;
    statusBar()->showMessage(
        tr("No hay elevación activa: carga un DEM con «DEM…» primero."), 5000);
    return false;
}

// Dibuja un pin (gota de mapa) del color dado. Sirve de icono del botón, de
// cursor al capturar y de marca fija del punto sobre el mapa. La punta queda
// abajo-centro (el «hotspot» del cursor).
QPixmap Ventana::pinPixmap(const QColor &c)
{
    const int w = 24, h = 32;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    const double r = 9.0, cx = w / 2.0, cy = r + 1.0;
    path.addEllipse(QPointF(cx, cy), r, r);          // cabeza
    path.moveTo(cx - 6.0, cy + 4.0);                 // punta hacia abajo
    path.lineTo(cx, h - 1.0);
    path.lineTo(cx + 6.0, cy + 4.0);
    path.closeSubpath();
    g.setPen(QPen(Qt::white, 1.5));
    g.setBrush(c);
    g.drawPath(path.simplified());
    g.setBrush(Qt::white);                           // ojal central
    g.setPen(Qt::NoPen);
    g.drawEllipse(QPointF(cx, cy), 3.2, 3.2);
    return pm;
}

// Icono de un pico: disco del color dado con borde blanco y un punto central
// blanco. \a hl (resaltado por hover) lo agranda y le añade un halo amarillo.
QPixmap Ventana::picoIcon(const QColor &c, bool hl)
{
    const int s = hl ? 26 : 18;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const double cx = s / 2.0, cy = s / 2.0;
    if (hl) {                                   // halo
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(0xff, 0xee, 0x58, 180));
        g.drawEllipse(QPointF(cx, cy), s / 2.0 - 1.0, s / 2.0 - 1.0);
    }
    g.setPen(QPen(Qt::white, 2.0));
    g.setBrush(c);
    g.drawEllipse(QPointF(cx, cy), hl ? 8.0 : 7.0, hl ? 8.0 : 7.0);
    g.setPen(Qt::NoPen);
    g.setBrush(Qt::white);                      // punto central
    g.drawEllipse(QPointF(cx, cy), 2.4, 2.4);
    return pm;
}

// Paleta de 10 colores distintos para los picos (1..10).
const QVector<QColor> &Ventana::paletaPicos()
{
    static const QVector<QColor> p = {
        QColor(0xe5, 0x39, 0x35), QColor(0x8e, 0x24, 0xaa),
        QColor(0x34, 0x49, 0xab), QColor(0x03, 0x9b, 0xe5),
        QColor(0x00, 0x89, 0x7b), QColor(0x7c, 0xb3, 0x42),
        QColor(0xf9, 0xa8, 0x25), QColor(0xfb, 0x8c, 0x00),
        QColor(0x6d, 0x4c, 0x41), QColor(0x54, 0x6e, 0x7a)
    };
    return p;
}

// Resalta el pico cuyo marcador está bajo el cursor (y restaura el anterior).
// \a id es lo que devuelve featureAt; si no es un pico, solo restaura.
void Ventana::resaltarPico(qint64 id)
{
    if (!m_picoIds.contains(id)) id = -1;      // fuera de un pico
    if (id == m_picoHover) return;
    auto reponer = [this](qint64 x) {          // vuelve al icono base
        if (x >= 0 && m_picoBase.contains(x))
            m_mapa->updateFeature(m_picoBase.value(x));
    };
    reponer(m_picoHover);
    if (id >= 0 && m_picoBase.contains(id)) {  // aplica el icono resaltado
        MapFeature f = m_picoBase.value(id);
        f.style.icon = picoIcon(f.style.labelColor, /*hl=*/true);
        m_mapa->updateFeature(f);
    }
    m_picoHover = id;
}

// Filtro de eventos del mapa: al mover el ratón, resalta el pico que haya bajo
// el cursor (si hay picos dibujados).
// Texto de tooltip de una entidad: nombre, tipo y cota si los trae.
QString Ventana::tooltipDe(const MapFeature &f)
{
    QStringList lin;
    if (!f.name.isEmpty())
        lin << QStringLiteral("<b>") + f.name.toHtmlEscaped() + QStringLiteral("</b>");
    const QVariant cota = f.attributes.value(QStringLiteral("cota"));
    if (cota.isValid())
        lin << QString::number(qRound(cota.toDouble())) + QStringLiteral(" m");
    if (!f.type.isEmpty())
        lin << QStringLiteral("<span style='color:gray'>") + f.type.toHtmlEscaped()
                   + QStringLiteral("</span>");
    return lin.join(QStringLiteral("<br>"));
}

// Intercepta el ratón sobre la MapView: tooltip de la entidad bajo el cursor
// (TODA entidad, no solo curvas), resalte de picos por hover, y ARRASTRE de
// los pines A/B (su punta sigue al cursor sobre el terreno).
bool Ventana::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != m_plotObj)
        return QMainWindow::eventFilter(obj, ev);

    const QEvent::Type t = ev->type();

    // Esc cancela una captura de punto en curso (anillos, marcación, A o B):
    // la herramienta de pinchar no tenía otra salida que elegir «Navegar».
    if (t == QEvent::KeyPress && static_cast<QKeyEvent *>(ev)->key() == Qt::Key_Escape
        && m_picking != Pick::Ninguno) {
        cancelarCaptura();
        return true;
    }

    // El arrastre de los pines A/B ya NO se hace aquí: lo gestiona la librería
    // (entidad marcada draggable + señales featureDrag*, ver construirPanel).

    // --- hover: resalte de picos + tooltip de cualquier entidad ---
    if (t == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(ev);
        const qint64 id = m_mapa->featureAt(me->pos(), 8.0);
        if (!m_picoIds.isEmpty())
            resaltarPico(id);
        QString tip;
        if (id >= 0)
            if (const auto f = m_mapa->feature(id))
                tip = tooltipDe(*f);
        if (!tip.isEmpty())
            QToolTip::showText(QCursor::pos(), tip, m_mapa);   // global, Qt5/Qt6
        else
            QToolTip::hideText();
    }
    return QMainWindow::eventFilter(obj, ev);
}

// Vuelca una coordenada (arrastre de un pin A/B) en sus campos lat/lon, que
// redibujan el pin y la lectura al vuelo. La coordenada la da la librería por
// la señal featureDragMovedTo.
void Ventana::moverPinCoord(Pick cual, const QGeoCoordinate &g)
{
    if (!g.isValid()) return;
    QDoubleSpinBox *lat = (cual == Pick::A) ? m_latA : m_latB;
    QDoubleSpinBox *lon = (cual == Pick::A) ? m_lonA : m_lonB;
    lat->setValue(g.latitude());
    lon->setValue(g.longitude());         // dispara actualizarInfoPuntos()->marcarPuntosActivos()
}

// Arranca la captura de un punto (A o B) con el siguiente clic en el mapa.
// Cambia el cursor del mapa al pin del color del punto (azul A, rojo B).
void Ventana::iniciarPick(Pick cual)
{
    m_picking = cual;
    m_mapa->setActiveTool(MapTool::PickPoint);
    const QPixmap pm = pinPixmap(cual == Pick::A ? m_colorA : m_colorB);
    m_mapa->setCursor(QCursor(pm, pm.width() / 2, pm.height() - 1));  // punta = hotspot
    statusBar()->showMessage(
        tr("Pincha en el mapa para fijar el punto %1…")
            .arg(cual == Pick::A ? tr("A") : tr("B")), 8000);
}

// Recibe el clic capturado: vuelca la coordenada en los campos del punto en
// curso, restaura el cursor y re-ejecuta el último análisis para verlo al vuelo.
void Ventana::onPointPicked(const QGeoCoordinate &p)
{
    if (m_picking == Pick::Ninguno) return;
    if (m_picking == Pick::Anillos || m_picking == Pick::OrigenEbl
        || m_picking == Pick::DestinoEbl) {
        puntoDeMedida(p);
        return;
    }
    // Junto a un punto del usuario (radar, observador), el punto A/B es ese punto
    // exacto: así el análisis y sus anillos parten del mismo sitio.
    const QGeoCoordinate q = ajustarAPunto(p);
    QDoubleSpinBox *lat = (m_picking == Pick::A) ? m_latA : m_latB;
    QDoubleSpinBox *lon = (m_picking == Pick::A) ? m_lonA : m_lonB;
    lat->setValue(q.latitude());
    lon->setValue(q.longitude());
    const Pick cual = m_picking;
    m_picking = Pick::Ninguno;
    m_mapa->setActiveTool(MapTool::None);
    m_mapa->unsetCursor();
    statusBar()->showMessage(
        tr("Punto %1 fijado en %2, %3")
            .arg(cual == Pick::A ? tr("A") : tr("B"))
            .arg(q.latitude(), 0, 'f', 5).arg(q.longitude(), 0, 'f', 5), 4000);
    reejecutar();     // refresca el análisis con el nuevo punto
}

// Actualiza la lectura de los puntos: cota del terreno en A y B (bajo cada
// punto seleccionado), y rumbo y distancia de A a B. Se llama al cambiar
// cualquier campo lat/lon.
void Ventana::actualizarInfoPuntos()
{
    if (!m_infoPuntos) return;
    auto cota = [this](const QGeoCoordinate &q) {
        const double e = m_mapa ? m_mapa->elevationAt(q) : std::numeric_limits<double>::quiet_NaN();
        return std::isnan(e) ? QStringLiteral("—") : QString::number(e, 'f', 0) + tr(" m");
    };
    const bool vision = m_tipoAnalisis && m_tipoAnalisis->currentIndex() == 1;
    const QGeoCoordinate a = puntoA();
    QString t = tr("<span style='color:%1'>●</span> Cota: <b>%2</b>")
                    .arg(m_colorA.name(), cota(a));
    if (vision) {                       // en Visión, también B y el rumbo/distancia
        const QGeoCoordinate b = puntoB();
        const double d = a.isValid() && b.isValid() ? a.distanceTo(b) : 0.0;
        const double az = a.isValid() && b.isValid() ? a.azimuthTo(b) : 0.0;
        t += tr(" &nbsp; <span style='color:%1'>●</span> Cota B: <b>%2</b><br>"
                "A→B: <b>%3 km</b> · rumbo <b>%4°</b>")
                 .arg(m_colorB.name(), cota(b))
                 .arg(d / 1000.0, 0, 'f', 2).arg(az, 0, 'f', 0);
    }
    m_infoPuntos->setText(t);
    marcarPuntosActivos();   // los pines siguen a los puntos (al pinchar o escribir)
}

// Muestra/oculta los campos de entrada según el análisis elegido y relabela los
// comunes (un observador/posición se llama distinto en cada caso).
void Ventana::actualizarEntradaAnalisis(int idx)
{
    const bool perfil = idx == 0, vision = idx == 1, viewshed = idx == 2, picos = idx == 3;
    if (m_rowPosA) m_rowPosA->setVisible(true);
    if (m_rowPosB) m_rowPosB->setVisible(vision);
    if (m_rowAzimut) m_rowAzimut->setVisible(perfil);
    if (m_rowDist) m_rowDist->setVisible(perfil || viewshed);
    if (m_rowAnt1) m_rowAnt1->setVisible(vision || viewshed);
    if (m_rowAnt2) m_rowAnt2->setVisible(vision || viewshed);
    if (m_rowRadio) m_rowRadio->setVisible(picos);
    if (m_rowSep) m_rowSep->setVisible(picos);
    if (m_rowChecks) m_rowChecks->setVisible(!picos);
    if (m_lblPosA) m_lblPosA->setText(vision ? tr("Posición 1 (A):")
                                    : viewshed ? tr("Observador:")
                                    : picos ? tr("Posición:")
                                            : tr("Posición A:"));
    if (m_lblDist) m_lblDist->setText(viewshed ? tr("Alcance:") : tr("Distancia:"));
    if (m_lblAnt1) m_lblAnt1->setText(viewshed ? tr("Antena obs.:") : tr("Antena 1 (Alt1):"));
    if (m_lblAnt2) m_lblAnt2->setText(viewshed ? tr("Alt. objetivo:") : tr("Antena 2 (Alt2):"));
    actualizarInfoPuntos();   // redibuja los pines activos y la lectura
}

// Dibuja el/los pin(es) del ANÁLISIS ACTIVO: siempre la posición A (azul) y, solo
// en Visión A→B, también B (rojo). Así el punto seleccionado se ve sin analizar,
// pero no sobran pines (B no aparece en perfil/viewshed/picos).
void Ventana::marcarPuntosActivos()
{
    if (!m_mapa) return;
    prepararCapa(kCapaPuntos, tr("Puntos"), 60);
    auto pin = [this](const QGeoCoordinate &p, const QColor &c, const QString &txt) {
        if (!p.isValid()) return;
        MapFeature f;
        f.layerId = kCapaPuntos;
        f.kind = GeometryKind::Point;
        f.type = QStringLiteral("punto_ab");
        f.name = txt;
        f.geometry = { p };
        f.style.icon = pinPixmap(c);
        f.style.iconAnchor = QPointF(0.5, 31.0 / 32.0);  // la PUNTA marca el punto
        f.style.labelVisible = true;
        f.style.labelColor = c;
        f.selectable = true;
        f.draggable = true;         // la librería lo arrastra con «Navegar»
        m_mapa->addFeature(f);
    };
    pin(puntoA(), m_colorA, tr("A"));
    if (m_tipoAnalisis && m_tipoAnalisis->currentIndex() == 1)   // solo Visión usa B
        pin(puntoB(), m_colorB, tr("B"));
}

// (Re)crea vacía una capa donde volcar un resultado de análisis.
void Ventana::prepararCapa(const QString &id, const QString &nombre, int z)
{
    if (!m_mapa->addFeatureLayer(id, nombre, z))
        m_mapa->clearFeatureLayer(id);
}

// Parámetros de elevación con la opción «mar/sin dato = 0 m» de la pestaña.
ElevationProfileParams Ventana::paramsPerfil() const
{
    ElevationProfileParams pp;
    if (m_marComo0 && m_marComo0->isChecked()) pp.voidElevation = 0.0;
    return pp;
}

// Cota que se usa donde no hay dato: 0 m si «mar como 0» está marcado, NaN si no.
double Ventana::voidElev() const
{
    return (m_marComo0 && m_marComo0->isChecked())
               ? 0.0 : std::numeric_limits<double>::quiet_NaN();
}

// Perfil del terreno a lo largo de la ruta seleccionada -> ventana flotante.
void Ventana::analizarPerfil()
{
    if (!exigirDem())
        return;
    // Perfil RADIAL desde A por el rumbo indicado, hasta el alcance.
    const QGeoCoordinate a = puntoA();
    if (!a.isValid()) {
        statusBar()->showMessage(tr("Punto A no válido (fíjalo en el mapa o escríbelo)."), 5000);
        return;
    }
    marcarPuntosActivos();              // pin azul fijo en A
    const double alcanceM = m_alcanceKm->value() * 1000.0;
    const QGeoCoordinate fin = a.atDistanceAndAzimuth(alcanceM, m_rumbo->value());
    // El perfil se DIBUJA con la cota cruda (muestra la batimetría en azul); el
    // check «Mar = 0» solo afecta al ANÁLISIS de visibilidad, no a la gráfica.
    const ElevationProfile p = m_mapa->elevationProfile({ a, fin }, ElevationProfileParams());
    if (!p.isValid() || std::isnan(p.maxElevation)) {
        statusBar()->showMessage(
            tr("El perfil no tiene cota en el DEM activo (fuera de cobertura)."),
            5000);
        return;
    }
    const QString resumen =
        tr("<b>Perfil</b> · rumbo %1° · %2 km · mín %3 m · máx %4 m · subida +%5 m · bajada −%6 m")
            .arg(m_rumbo->value(), 0, 'f', 0)
            .arg(p.totalDistanceM / 1000.0, 0, 'f', 2)
            .arg(p.minElevation, 0, 'f', 0).arg(p.maxElevation, 0, 'f', 0)
            .arg(p.gain, 0, 'f', 0).arg(p.loss, 0, 'f', 0);
    if (m_resultado) m_resultado->setText(resumen);
    m_ultimo = Analisis::Perfil;
    mostrarPerfil(p, resumen);           // perfil radial: sin visión ni techo 5000
}

// Línea de visión de A (Alt1) a B (Alt2), ambos de los campos de la pestaña.
void Ventana::analizarVision()
{
    if (!exigirDem())
        return;
    const QGeoCoordinate a = puntoA();
    const QGeoCoordinate b = puntoB();
    if (!a.isValid() || !b.isValid() || a == b) {
        statusBar()->showMessage(
            tr("Fija los puntos A y B (distintos) en el mapa o escríbelos."), 5000);
        return;
    }
    marcarPuntosActivos();               // pines fijos azul (A) y rojo (B)
    LineOfSightParams lp;
    lp.voidElevation = voidElev();
    lp.curvature = curvaturaOn();
    const LineOfSightResult v =
        m_mapa->lineOfSight(a, b, m_altA->value(), m_altB->value(), lp);
    if (!v.isValid()) {
        statusBar()->showMessage(
            tr("No se pudo calcular la visión (falta cota en A o B)."), 5000);
        return;
    }

    prepararCapa(kCapaVision, tr("Análisis: visión"), 50);
    const QColor azul(0x15, 0x65, 0xc0);        // VISIBLE (llega la vista)
    const QColor rojo(0xc6, 0x28, 0x28);        // OCULTO (tras el obstáculo)

    if (v.clear || !v.blockPosition.isValid()) {
        // Visión directa: toda la línea en azul.
        MapFeature linea;
        linea.layerId = kCapaVision;
        linea.kind = GeometryKind::Polyline;
        linea.type = QStringLiteral("linea_vision");
        linea.name = tr("Visión directa");
        linea.geometry = { a, b };
        linea.style.lineColor = azul;
        linea.style.lineWidth = 3.0;
        linea.selectable = false;
        m_mapa->addFeature(linea);
    } else {
        // Bloqueada: tramo VISIBLE (azul) hasta el obstáculo y tramo OCULTO
        // (rojo, discontinuo) por detrás, para que se vea dónde corta la vista.
        MapFeature visible;
        visible.layerId = kCapaVision;
        visible.kind = GeometryKind::Polyline;
        visible.type = QStringLiteral("vision_visible");
        visible.name = tr("Visible");
        visible.geometry = { a, v.blockPosition };
        visible.style.lineColor = azul;
        visible.style.lineWidth = 3.0;
        visible.style.labelVisible = false;
        visible.selectable = false;
        m_mapa->addFeature(visible);

        MapFeature oculto;
        oculto.layerId = kCapaVision;
        oculto.kind = GeometryKind::Polyline;
        oculto.type = QStringLiteral("vision_oculta");
        oculto.name = tr("Oculto");
        oculto.geometry = { v.blockPosition, b };
        oculto.style.lineColor = rojo;
        oculto.style.lineWidth = 2.0;
        oculto.style.lineStyle = Qt::DashLine;
        oculto.style.labelVisible = false;
        oculto.selectable = false;
        m_mapa->addFeature(oculto);

        MapFeature corte;
        corte.layerId = kCapaVision;
        corte.kind = GeometryKind::Point;
        corte.type = QStringLiteral("obstaculo");
        corte.name = tr("Obstáculo a %1 km (falta %2 m)")
                         .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                         .arg(-v.clearanceM, 0, 'f', 0);
        corte.geometry = { v.blockPosition };
        corte.style.lineColor = rojo;
        corte.style.pointRadiusPx = 7.0;
        corte.selectable = false;
        m_mapa->addFeature(corte);
    }

    const QString resumen =
        v.clear
            ? tr("<b>Visión DIRECTA</b> · %1 km · holgura mínima %2 m · paso más "
                 "justo a %3 km. <span style='color:#777'>Naranja = línea de visión "
                 "(Alt1 %4 m en A, Alt2 %5 m en B).</span>")
                  .arg(v.totalDistanceM / 1000.0, 0, 'f', 2)
                  .arg(v.clearanceM, 0, 'f', 0)
                  .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                  .arg(m_altA->value(), 0, 'f', 0).arg(m_altB->value(), 0, 'f', 0)
            : tr("<b>BLOQUEADA</b> · obstáculo a %1 km de %2 km · faltan %3 m de "
                 "altura. <span style='color:#777'>Azul = visible, rojo = oculto; "
                 "naranja = línea de visión.</span>")
                  .arg(v.blockDistanceM / 1000.0, 0, 'f', 2)
                  .arg(v.totalDistanceM / 1000.0, 0, 'f', 2)
                  .arg(-v.clearanceM, 0, 'f', 0);
    if (m_resultado) m_resultado->setText(resumen);
    statusBar()->showMessage(QString(resumen).remove(QRegularExpression(
                                 QStringLiteral("<[^>]*>"))), 9000);

    // Perfil con la LÍNEA DE VISIÓN y el obstáculo, en la ventana flotante. El
    // perfil se extiende hasta ALCANCE + 5 km por el rumbo A→B (para ver todo el
    // terreno y poder arrastrar); la vista inicial se centra en [0, alcance].
    const double D = v.totalDistanceM;                  // distancia A→B
    const double alcanceM = m_alcanceKm->value() * 1000.0;
    const double largo = qMax(alcanceM + 5000.0, D + 5000.0);
    const QGeoCoordinate fin = a.atDistanceAndAzimuth(largo, a.azimuthTo(b));
    // Perfil CRUDO para dibujar (batimetría en azul). La visibilidad (v, arriba)
    // ya se calculó con «Mar = 0» clampeando el fondo marino a la superficie.
    const ElevationProfile perfil = m_mapa->elevationProfile({ a, fin }, ElevationProfileParams());
    if (!perfil.isValid() || std::isnan(perfil.maxElevation))
        return;

    // Terreno (ya con «mar = 0» si procede) en A (primera muestra) y en B.
    const double tA = perfil.samples.first().elevation;
    double tB = std::numeric_limits<double>::quiet_NaN();
    for (int i = 1; i < perfil.samples.size(); ++i) {
        if (perfil.samples[i].distanceM >= D) {
            const ElevationSample &s0 = perfil.samples[i - 1];
            const ElevationSample &s1 = perfil.samples[i];
            if (!std::isnan(s0.elevation) && !std::isnan(s1.elevation)) {
                const double t = (s1.distanceM > s0.distanceM)
                    ? (D - s0.distanceM) / (s1.distanceM - s0.distanceM) : 0.0;
                tB = s0.elevation + (s1.elevation - s0.elevation) * t;
            }
            break;
        }
    }
    m_ultimo = Analisis::Vision;
    // La recta de visibilidad busca el objetivo SOBRE EL MAR: si un extremo no
    // tiene dato (NaN) o cae bajo el nivel del mar (mar o batimetría negativa),
    // la base de la antena se asienta en la SUPERFICIE (0), no en el fondo
    // marino. Los objetivos están por encima del nivel del mar (p. ej. buques),
    // así que su mástil arranca en 0, no a −profundidad.
    const double baseA = std::isnan(tA) ? 0.0 : qMax(tA, 0.0);
    const double baseB = std::isnan(tB) ? 0.0 : qMax(tB, 0.0);
    if (D > 0.0) {
        const double zA = baseA + m_altA->value();   // cima de antena en A (abs)
        const double zB = baseB + m_altB->value();   // cima de antena en B (abs)
        mostrarPerfil(perfil, resumen, alcanceM, true, zA, zB, D,
                      v.blockDistanceM, !v.clear);
    } else {
        mostrarPerfil(perfil, resumen, alcanceM);
    }
}

// Viewshed 360° desde el vértice de la entidad seleccionada (o el centro del
// mapa): dibuja el polígono de la zona visible para un objetivo a Alt2.
void Ventana::analizarViewshed()
{
    if (!exigirDem())
        return;
    const QGeoCoordinate origen = puntoA();
    if (!origen.isValid()) {
        statusBar()->showMessage(tr("Punto (A) no válido (fíjalo en el mapa o escríbelo)."), 5000);
        return;
    }
    marcarPuntosActivos();              // pin azul fijo en A (observador)

    ViewshedParams vp;
    vp.observerHeight = m_altA->value();
    vp.targetHeight = m_altB->value();
    vp.maxRangeM = m_alcanceKm->value() * 1000.0;
    vp.voidElevation = voidElev();      // mar / sin dato = 0 (objetivos en el mar)
    vp.curvature = curvaturaOn();       // sin curvatura: mar sin obstáculos = alcance

    // Barra de progreso cancelable: el viewshed de 360° a largo alcance (hasta
    // 400 km) puede tardar. Solo aparece si pasa de ~0,4 s (alcances cortos no
    // la ven). El callback corre en este mismo hilo, así que procesa eventos
    // para repintar y atender «Cancelar».
    QProgressDialog prog(tr("Calculando viewshed (%1 km)…")
                             .arg(m_alcanceKm->value(), 0, 'f', 0),
                         tr("Cancelar"), 0, 360, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(400);
    auto progreso = [&prog](int done, int total) -> bool {
        prog.setMaximum(total);
        prog.setValue(done);
        QApplication::processEvents();
        return !prog.wasCanceled();
    };

    QElapsedTimer reloj;
    reloj.start();
    const Viewshed vs = m_mapa->viewshed(origen, vp, progreso);
    const qint64 ms = reloj.elapsed();
    const bool cancelado = prog.wasCanceled();
    prog.reset();
    if (!vs.isValid()) {
        statusBar()->showMessage(
            cancelado ? tr("Viewshed cancelado.")
                      : tr("No hay cota en el origen del viewshed (fuera de cobertura)."),
            5000);
        return;
    }
    m_ultimo = Analisis::Viewshed;

    // Zona de visibilidad REAL: una cuña por cada TRAMO de cada rayo, como
    // partes de UNA entidad por color. AZUL = visible, AMARILLO = oculto; lo
    // que no tiene dato (y el check de mar está apagado) se deja sin pintar.
    // Coincide, azimut a azimut, con la Visión A→B.
    const double half = (vp.azimuthStepDeg > 0.0 ? vp.azimuthStepDeg : 1.0) / 2.0;
    auto cunasDe = [&](const QVector<VisibleRange> &rs, double az) {
        QVector<QVector<QGeoCoordinate>> out;
        for (const VisibleRange &vr : rs) {
            if (vr.endM - vr.startM < 1.0) continue;
            const double d0 = qMax(vr.startM, 1.0);
            out.append({
                origen.atDistanceAndAzimuth(d0,      az - half),
                origen.atDistanceAndAzimuth(vr.endM, az - half),
                origen.atDistanceAndAzimuth(vr.endM, az + half),
                origen.atDistanceAndAzimuth(d0,      az + half)
            });
        }
        return out;
    };
    QVector<QVector<QGeoCoordinate>> cunasVis, cunasOcu;
    double maxKm = 0.0;
    for (const ViewshedRay &r : vs.rays) {
        cunasVis += cunasDe(r.visibleRanges, r.azimuthDeg);
        cunasOcu += cunasDe(r.hiddenRanges, r.azimuthDeg);
        for (const VisibleRange &vr : r.visibleRanges)
            maxKm = qMax(maxKm, vr.endM / 1000.0);
    }

    prepararCapa(kCapaViewshed, tr("Análisis: viewshed"), 40);
    auto pintarZona = [&](const QVector<QVector<QGeoCoordinate>> &cunas,
                          const QString &tipo, const QString &nombre,
                          const QColor &fill) {
        if (cunas.isEmpty()) return;
        MapFeature zona;
        zona.layerId = kCapaViewshed;
        zona.kind = GeometryKind::Polygon;
        zona.type = tipo;
        zona.name = nombre;
        zona.parts = cunas;
        zona.geometry = cunas.first();
        zona.style.lineColor = QColor(0, 0, 0, 0);   // sin borde por cuña
        zona.style.fillColor = fill;
        zona.style.labelVisible = false;
        zona.selectable = false;
        m_mapa->addFeature(zona);
    };
    // Oculto primero (debajo), visible encima.
    pintarZona(cunasOcu, QStringLiteral("zona_oculta"), tr("Oculto"),
               QColor(0xff, 0xc1, 0x07, 90));                 // amarillo
    pintarZona(cunasVis, QStringLiteral("zona_visibilidad"),
               tr("Visible a %1 m").arg(vp.targetHeight, 0, 'f', 0),
               QColor(0x42, 0xa5, 0xf5, 80));                 // azul

    MapFeature centro;
    centro.layerId = kCapaViewshed;
    centro.kind = GeometryKind::Point;
    centro.type = QStringLiteral("observador");
    centro.name = tr("Observador");
    centro.geometry = { origen };
    centro.style.lineColor = QColor(0x0d, 0x47, 0xa1);
    centro.selectable = false;
    m_mapa->addFeature(centro);

    const QString resumen =
        tr("<b>Viewshed</b> · %1 rayos · obs %2 m, obj %3 m · alcance máx visible "
           "%4 km · %5 ms. <span style='color:#777'>Azul = visible, amarillo = "
           "oculto.</span>")
            .arg(vs.rays.size())
            .arg(vp.observerHeight, 0, 'f', 0).arg(vp.targetHeight, 0, 'f', 0)
            .arg(maxKm, 0, 'f', 1).arg(ms);
    if (m_resultado) m_resultado->setText(resumen);
    statusBar()->showMessage(QString(resumen).remove(QRegularExpression(
                                 QStringLiteral("<[^>]*>"))), 9000);
}

// Marca los 10 puntos MÁS ALTOS del terreno en un radio de 10 km desde A.
// Muestrea una rejilla (paso 100 m) dentro del círculo, ordena por cota y toma
// los 10 más altos exigiendo una separación mínima (para que sean cumbres
// distintas y no 10 muestras de la misma loma). Los pinta numerados sobre el
// mapa y los lista (cota · distancia · rumbo) en el panel.
void Ventana::analizarPicosAltos()
{
    if (!exigirDem())
        return;
    const QGeoCoordinate a = puntoA();
    if (!a.isValid()) {
        statusBar()->showMessage(tr("Punto A no válido (fíjalo en el mapa o escríbelo)."), 5000);
        return;
    }
    m_ultimo = Analisis::Picos;
    marcarPuntosActivos();              // pin azul fijo en A (centro)
    m_picoIds.clear(); m_picoBase.clear(); m_picoHover = -1;

    const double R = m_picosRadioKm->value() * 1000.0;
    const double sepMin = double(m_picosSepM->value());
    // La rejilla LOCALIZA cumbres; su paso se escala con el radio para acotar el
    // coste (~500×500 muestras). La EXACTITUD la da luego el refinamiento fino.
    const double paso = qMax(30.0, (2.0 * R) / 500.0);
    const double radioKm = m_picosRadioKm->value();

    struct Pico { QGeoCoordinate p; double ele; double dist; double az; };
    auto cotaEn = [&](const QGeoCoordinate &q) { return m_mapa->elevationAt(q); };

    // Barra de progreso cancelable (radios grandes tardan).
    const int filas = int((2.0 * R) / paso) + 1;
    QProgressDialog prog(tr("Buscando cumbres en %1 km…").arg(radioKm, 0, 'f', 0),
                         tr("Cancelar"), 0, filas, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(400);

    QVector<Pico> cand;
    cand.reserve(qsizetype(filas) * qsizetype(filas) / 2);
    int fila = 0;
    for (double dy = -R; dy <= R; dy += paso, ++fila) {
        for (double dx = -R; dx <= R; dx += paso) {
            const double dd = std::hypot(dx, dy);
            if (dd > R) continue;
            const double az = (dd < 1.0) ? 0.0
                : std::fmod(qRadiansToDegrees(std::atan2(dx, dy)) + 360.0, 360.0);
            const QGeoCoordinate q = a.atDistanceAndAzimuth(dd, az);
            const double e = cotaEn(q);
            if (std::isnan(e)) continue;
            cand.push_back({ q, e, dd, az });
        }
        prog.setValue(fila);
        QApplication::processEvents();
        if (prog.wasCanceled()) { statusBar()->showMessage(tr("Búsqueda de picos cancelada."), 4000); return; }
    }
    prog.reset();
    if (cand.isEmpty()) {
        statusBar()->showMessage(tr("No hay cota en %1 km alrededor de A (fuera de cobertura).").arg(radioKm,0,'f',0), 5000);
        return;
    }
    std::sort(cand.begin(), cand.end(),
              [](const Pico &x, const Pico &y) { return x.ele > y.ele; });

    // Top-10 con separación mínima sobre la rejilla gruesa.
    QVector<Pico> top;
    for (const Pico &c : cand) {
        bool lejos = true;
        for (const Pico &t : top)
            if (c.p.distanceTo(t.p) < sepMin) { lejos = false; break; }
        if (lejos) { top.push_back(c); if (top.size() >= 10) break; }
    }

    // Refinamiento fino: alrededor de cada cumbre, ventana ±150 m a paso 10 m
    // para clavar la cota exacta (la rejilla gruesa se salta el nodo del pico).
    auto refinar = [&](Pico t) {
        const double win = qMax(150.0, paso), st = 10.0;   // cubre ≥ una celda
        Pico best = t;
        for (double dy = -win; dy <= win; dy += st)
            for (double dx = -win; dx <= win; dx += st) {
                const double dd = std::hypot(dx, dy);
                if (dd > win) continue;
                const double az = (dd < 1.0) ? 0.0
                    : std::fmod(qRadiansToDegrees(std::atan2(dx, dy)) + 360.0, 360.0);
                const QGeoCoordinate q = t.p.atDistanceAndAzimuth(dd, az);
                const double e = cotaEn(q);
                if (!std::isnan(e) && e > best.ele) {
                    best.ele = e; best.p = q;
                    best.dist = a.distanceTo(q); best.az = a.azimuthTo(q);
                }
            }
        return best;
    };
    for (Pico &t : top) t = refinar(t);
    std::sort(top.begin(), top.end(),
              [](const Pico &x, const Pico &y) { return x.ele > y.ele; });

    // Dibuja los picos numerados, cada uno de un color distinto (disco con
    // punto central), y guarda su estilo base para el resaltado por hover.
    prepararCapa(kCapaPicos, tr("Picos más altos"), 55);
    QString lista = tr("<b>%1 puntos más altos (radio %2 km)</b><br>")
                        .arg(top.size()).arg(radioKm, 0, 'f', 0);
    for (int i = 0; i < top.size(); ++i) {
        const Pico &t = top[i];
        const QColor c = paletaPicos().at(i % paletaPicos().size());
        MapFeature f;
        f.layerId = kCapaPicos;
        f.kind = GeometryKind::Point;
        f.type = QStringLiteral("pico_alto");
        f.name = QString::number(i + 1);
        f.geometry = { t.p };
        f.style.icon = picoIcon(c, /*hl=*/false);
        f.style.labelColor = c;             // (reutilizado por resaltarPico)
        f.selectable = false;
        const qint64 id = m_mapa->addFeature(f);
        if (id >= 0) {
            m_picoIds.push_back(id);
            if (auto fg = m_mapa->feature(id)) m_picoBase.insert(id, *fg);
        }
        lista += tr("<span style='color:%1'>%2.</span> <b>%3 m</b> · %4 km · rumbo %5°<br>")
                     .arg(c.name()).arg(i + 1).arg(t.ele, 0, 'f', 0)
                     .arg(t.dist / 1000.0, 0, 'f', 2).arg(t.az, 0, 'f', 0);
    }
    if (m_resultado) m_resultado->setText(lista);
    statusBar()->showMessage(
        tr("%1 picos en %2 km; el más alto a %3 m.")
            .arg(top.size()).arg(radioKm, 0, 'f', 0)
            .arg(top.isEmpty() ? 0.0 : top.first().ele, 0, 'f', 0), 8000);
}
