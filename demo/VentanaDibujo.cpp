// Alta de entidades: diálogo, herramienta de dibujo y vuelta a Navegar.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

using namespace libmapa;

// Borra las capas de resultados de elevación y restablece los pines A/B.
// (Era una lambda del botón «Limpiar»; ahora es un slot reutilizable.)
void Ventana::limpiarAnalisis()
{
    quitarResultadosAnalisis();
    marcarPuntosActivos();
    statusBar()->showMessage(tr("Análisis de elevación limpiado"), 3000);
}

// Quita del mapa y del panel TODO lo que dejó el análisis anterior: la línea de
// visión, la zona visible, los picos, su texto de resultado y la ventana del
// perfil. Se llama al limpiar y al CAMBIAR de análisis: cada análisis solo
// prepara su propia capa, así que sin esto el resultado del anterior (p. ej. la
// recta de visibilidad de «Visión A→B») se quedaba pintado bajo el nuevo.
void Ventana::quitarResultadosAnalisis()
{
    m_mapa->removeFeatureLayer(kCapaVision);
    m_mapa->removeFeatureLayer(kCapaViewshed);
    m_mapa->removeFeatureLayer(kCapaPicos);
    m_picoIds.clear(); m_picoBase.clear(); m_picoHover = -1;
    if (m_resultado)
        m_resultado->clear();
    if (m_perfilWin)
        m_perfilWin->hide();
    m_ultimo = Analisis::Ninguno;
}

// Al iniciar, los overlays del paquete arrancan OCULTOS (no cargan el
// sistema). El operador los enciende desde el panel de capas.
void Ventana::ocultarOverlaysAlIniciar()
{
    // Los overlays del paquete se cargan como capas NO editables; las capas
    // de usuario sí lo son. Ocultamos las no editables (los overlays).
    for (const LayerInfo &capa : m_mapa->featureLayers())
        if (!capa.editable)
            m_mapa->setFeatureLayerVisible(capa.id, false);
}

// Repone el borrador por defecto (sin tipo de dominio, sin tope de
// vértices, estilo por defecto). Lo usa volverANavegar al desarmar el alta.
void Ventana::reponerBorrador()
{
    m_mapa->setDraftType(QString());
    m_mapa->setDraftMaxVertices(-1);
    m_mapa->setDraftStyle(FeatureStyle());
}

// Herramientas genéricas (Punto/Línea/Polígono): abre el diálogo de alta
// con el estilo que haya configurado el usuario en el panel.
void Ventana::nuevaEntidadGenerica()
{
    configurarNuevaEntidad(QString(), estiloActual(), -1, tr("Entidad"));
}

// Modo "Objetivo": punto con type "objetivo" y etiqueta visible; su rótulo
// (y el punto) se arrastran. Pasa por el diálogo de alta.
void Ventana::modoObjetivo()
{
    FeatureStyle s;
    s.labelVisible = true;
    s.pointRadiusPx = 7.0;
    s.lineColor = QColor(0xff, 0x8f, 0x00);
    s.fillColor = QColor(0xff, 0x8f, 0x00, 160);
    configurarNuevaEntidad(QStringLiteral("objetivo"), s, -1, tr("Objetivo"));
}

// Modo "Área objetivo": polígono de <=10 vértices (type "objetivo_area")
// con la etiqueta colgada de su PRIMER punto (labelAnchorVertex=0).
void Ventana::modoAreaObjetivo()
{
    FeatureStyle s;
    s.labelVisible = true;
    s.labelAnchorVertex = 0;
    s.lineColor = QColor(0xc6, 0x28, 0x28);
    s.fillColor = QColor(0xc6, 0x28, 0x28, 40);
    s.lineWidth = 2.0;
    configurarNuevaEntidad(QStringLiteral("objetivo_area"), s, 10, tr("Área"));
}

// Vuelve a la herramienta Navegar y desarma el alta de entidad en curso.
void Ventana::volverANavegar()
{
    m_esperandoDibujo = false;
    m_nombrePendiente.clear();
    reponerBorrador();
    if (m_accNavegar)
        m_accNavegar->setChecked(true);
    m_mapa->setActiveTool(MapTool::None);
}

// Flujo de alta de una entidad: pregunta nombre/capa/colores y, al aceptar,
// configura el borrador y deja que el usuario dibuje; al terminar el trazo
// (señal featureAdded → alEntidadCreada) se le pone el nombre y se vuelve a
// Navegar. Se DIFIERE con un singleShot(0) para que el diálogo salga después
// de que el grupo de acciones fije la herramienta de dibujo; así, al
// cancelar, volverANavegar no queda pisado por esa activación automática.
void Ventana::configurarNuevaEntidad(const QString &tipo, FeatureStyle base,
                            int maxVert, const QString &nombreSug)
{
    QTimer::singleShot(0, this, [this, tipo, base, maxVert, nombreSug]() mutable {
        QString nombre, capa;
        QColor cl = base.lineColor, cr = base.fillColor;
        double ancho = base.lineWidth > 0 ? base.lineWidth : 1.5;
        if (!dialogoNuevaEntidad(nombreSug, &nombre, &capa, &cl, &cr, &ancho)) {
            volverANavegar();
            return;
        }
        if (capa.isEmpty())
            capa = m_mapa->activeFeatureLayer();
        // addFeatureLayer no-op si la capa ya existe; la crea si es nueva.
        m_mapa->addFeatureLayer(capa, capa,
            static_cast<int>(m_mapa->featureLayers().size()) * 10);
        m_mapa->setActiveFeatureLayer(capa);

        base.lineColor = cl;
        base.fillColor = cr;
        base.lineWidth = ancho;
        m_mapa->setDraftType(tipo);
        m_mapa->setDraftStyle(base);
        m_mapa->setDraftMaxVertices(maxVert);

        m_nombrePendiente = nombre;
        m_esperandoDibujo = true;
        statusBar()->showMessage(
            tr("Dibuja la entidad en el mapa; al terminar vuelve a Navegar."),
            6000);
    });
}

// Diálogo modal de alta: nombre, capa (editable: se puede teclear una
// nueva) y colores de borde/relleno + ancho. Devuelve true si se aceptó.
bool Ventana::dialogoNuevaEntidad(const QString &nombreSug, QString *nombre,
                         QString *capa, QColor *linea, QColor *relleno,
                         double *ancho)
{
    QDialog d(this);
    d.setWindowTitle(tr("Nueva entidad"));
    auto *form = new QFormLayout(&d);

    auto *edNombre = new QLineEdit(nombreSug, &d);
    form->addRow(tr("Nombre:"), edNombre);

    auto *cbCapa = new QComboBox(&d);
    cbCapa->setEditable(true);           // permite teclear una capa nueva
    for (const LayerInfo &c : m_mapa->featureLayers())
        if (c.editable)
            cbCapa->addItem(c.displayName.isEmpty() ? c.id : c.displayName, c.id);
    const QString activa = m_mapa->activeFeatureLayer();
    if (const int i = cbCapa->findData(activa); i >= 0)
        cbCapa->setCurrentIndex(i);
    else if (!activa.isEmpty())
        cbCapa->setCurrentText(activa);
    form->addRow(tr("Capa:"), cbCapa);

    QColor cl = *linea, cr = *relleno;
    auto *btnL = new QPushButton(&d);
    auto *btnR = new QPushButton(&d);
    pintarBotonColor(btnL, cl);
    pintarBotonColor(btnR, cr);
    connect(btnL, &QPushButton::clicked, &d, [this, &d, &cl, btnL] {
        const QColor c = QColorDialog::getColor(cl, &d, tr("Color de borde"),
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) { cl = c; pintarBotonColor(btnL, c); }
    });
    connect(btnR, &QPushButton::clicked, &d, [this, &d, &cr, btnR] {
        const QColor c = QColorDialog::getColor(cr, &d, tr("Color de relleno"),
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) { cr = c; pintarBotonColor(btnR, c); }
    });
    form->addRow(tr("Color de borde:"), btnL);
    form->addRow(tr("Color de relleno:"), btnR);

    auto *spAncho = new QDoubleSpinBox(&d);
    spAncho->setRange(0.5, 12.0);
    spAncho->setSingleStep(0.5);
    spAncho->setValue(*ancho);
    form->addRow(tr("Ancho de línea:"), spAncho);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);

    if (d.exec() != QDialog::Accepted)
        return false;

    *nombre = edNombre->text().trimmed();
    // Si el texto de la capa coincide con un ítem, usa su id (data); si el
    // usuario tecleó algo nuevo, ese texto ES el id de la capa nueva.
    // OJO: en un combo editable currentData()/currentIndex() NO cambian al
    // teclear texto nuevo (siguen en la capa preseleccionada), así que se
    // resuelve por TEXTO: si coincide con un ítem, se usa su id (data); si
    // el usuario tecleó algo nuevo, ese texto ES el id de la capa nueva.
    const QString texto = cbCapa->currentText().trimmed();
    const int i = cbCapa->findText(texto);
    *capa = (i >= 0) ? cbCapa->itemData(i).toString() : texto;
    *linea = cl;
    *relleno = cr;
    *ancho = spAncho->value();
    return true;
}

// Llega por featureAdded: si hay un alta en curso, pone el nombre elegido a
// la entidad recién dibujada, la deja seleccionada y vuelve a Navegar.
void Ventana::alEntidadCreada(qint64 id)
{
    if (!m_esperandoDibujo)
        return;
    m_esperandoDibujo = false;
    if (!m_nombrePendiente.isEmpty()) {
        if (auto f = m_mapa->feature(id)) {
            f->name = m_nombrePendiente;
            m_mapa->updateFeature(*f);
        }
    }
    m_nombrePendiente.clear();
    marcarSucio();               // entidad nueva: hay algo que guardar
    volverANavegar();
    m_mapa->selectFeature(id);   // deja ver sus datos en el panel
}

// Si la entidad seleccionada es un área objetivo, lista sus coordenadas en
// la barra de estado (demuestra el acceso a los puntos vía feature()).
void Ventana::mostrarCoordsArea(qint64 id)
{
    const auto f = m_mapa->feature(id);
    if (!f || f->type != QLatin1String("objetivo_area"))
        return;
    QStringList pts;
    for (const QGeoCoordinate &c : f->geometry)
        pts << QStringLiteral("%1,%2")
                   .arg(c.latitude(), 0, 'f', 5).arg(c.longitude(), 0, 'f', 5);
    statusBar()->showMessage(
        tr("Área (%1 pts): %2").arg(f->geometry.size()).arg(pts.join(QStringLiteral("  "))),
        8000);
}
