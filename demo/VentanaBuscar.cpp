// Buscador de la demo: un campo en la barra principal que, al escribir, propone lugares
// (ciudades, aeropuertos, bahías...) del índice sin conexión del paquete y puntos de
// interés (farmacias, hospitales, gasolineras...) y calles de las teselas vectoriales
// cercanas al centro del mapa, y al elegir uno centra el mapa en él (una calle se encuadra
// entera y se resalta). Sin índice ni mapa
// vectorial, el campo se ve desactivado y dice por qué.

#include "Ventana.h"

#include <QCompleter>
#include <QLineEdit>
#include <QStringListModel>

// Crea el campo de búsqueda al final de la barra \a barra y lo conecta.
//
// El desplegable NO filtra por prefijo (UnfilteredPopupCompletion): lo que
// propone es lo que encontró el índice, y "guantanamo" debe sugerir
// "Guantánamo" aunque no empiece igual. Elegir una sugerencia, o pulsar Intro,
// lleva al primer resultado.
void Ventana::crearBuscador(QToolBar *barra)
{
    m_buscador = new QLineEdit(this);
    m_buscador->setClearButtonEnabled(true);
    m_buscador->setMinimumWidth(190);
    m_buscador->setMaximumWidth(260);
    m_buscador->setPlaceholderText(tr("Buscar lugar, sitio o calle…  (Ctrl+K)"));

    m_sugerencias = new QStringListModel(this);
    auto *completer = new QCompleter(m_sugerencias, this);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_buscador->setCompleter(completer);

    barra->addSeparator();
    barra->addWidget(m_buscador);

    connect(m_buscador, &QLineEdit::textEdited, this, &Ventana::buscarLugares);
    connect(m_buscador, &QLineEdit::returnPressed, this, [this] { irAResultado(0); });
    // Al vaciar el campo se quita el resalte de la calle elegida.
    connect(m_buscador, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.isEmpty())
            quitarResaltadoCalle();
    });
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated), this,
            [this](const QString &texto) { irAResultado(int(m_sugerencias->stringList().indexOf(texto))); });
    // El buscador de sitios depende del mapa vectorial: se reevalúa al cambiar de base o de capa híbrida.
    connect(m_mapa, &MapWidget::baseLayerChanged, this, [this] { actualizarBuscador(); });
    connect(m_mapa, &MapWidget::vectorOverlayChanged, this, [this] { actualizarBuscador(); });
    actualizarBuscador();
}

// Activa o desactiva el campo según haya índice de lugares o mapa vectorial donde
// buscar sitios.
void Ventana::actualizarBuscador()
{
    if (!m_buscador)
        return;
    const bool lugares = m_mapa->hasPlaces();
    const bool sitios = m_mapa->canSearchPointsOfInterest();
    m_buscador->setEnabled(lugares || sitios);
    if (lugares && sitios)
        m_buscador->setToolTip(tr("Escribe un nombre (sin tildes vale) o un tipo («farmacia»): "
                                  "busca lugares, sitios y calles cerca del centro del mapa."));
    else if (sitios)
        m_buscador->setToolTip(tr("Escribe un nombre o un tipo («farmacia») para buscar sitios y "
                                  "calles cerca del centro del mapa. No hay índice de lugares."));
    else if (lugares)
        m_buscador->setToolTip(tr("Escribe un nombre (sin tildes vale) y elige un lugar. Para "
                                  "buscar sitios (farmacias, hospitales…) hace falta un mapa vectorial."));
    else
        m_buscador->setToolTip(tr("No hay índice de lugares ni mapa vectorial. Añade \"places\" o "
                                  "un MBTiles vectorial al mapa.json."));
}

// Busca con lo escrito y rellena el desplegable: primero los lugares («nombre — tipo»),
// luego los sitios cercanos («nombre — tipo · a 1,2 km»).
void Ventana::buscarLugares(const QString &texto)
{
    const bool sitios = m_mapa->canSearchPointsOfInterest();
    m_lugaresVistos = m_mapa->searchPlaces(texto, sitios ? 5 : 12);
    m_poisVistos = sitios ? m_mapa->searchPointsOfInterest(texto, 5) : QVector<PointOfInterest>();
    m_callesVistas = m_mapa->canSearchStreets() ? m_mapa->searchStreets(texto, 5) : QVector<Street>();
    QStringList filas;
    for (const Place &p : m_lugaresVistos)
        filas << QStringLiteral("%1 — %2").arg(p.name, p.typeLabel());
    for (const PointOfInterest &p : m_poisVistos)
        filas << tr("%1 — %2 · a %3").arg(p.displayName(), p.typeLabel(), textoDistancia(p.distanceM));
    for (const Street &c : m_callesVistas)
        filas << tr("%1 — %2 · a %3").arg(c.name, c.typeLabel(), textoDistancia(c.distanceM));
    m_sugerencias->setStringList(filas);
    if (!filas.isEmpty() && m_buscador->completer())
        m_buscador->completer()->complete();
}

// Va al resultado número \a indice del desplegable: los lugares primero, luego los sitios y
// al final las calles.
void Ventana::irAResultado(int indice)
{
    if (indice < 0)
        return;
    if (indice < m_lugaresVistos.size())
        irALugar(m_lugaresVistos.at(indice));
    else if (indice - m_lugaresVistos.size() < m_poisVistos.size())
        irAPoi(m_poisVistos.at(indice - m_lugaresVistos.size()));
    else if (indice - m_lugaresVistos.size() - m_poisVistos.size() < m_callesVistas.size())
        irACalle(m_callesVistas.at(indice - m_lugaresVistos.size() - m_poisVistos.size()));
}

// Centra el mapa en el lugar elegido y lo dice en la barra de estado.
void Ventana::irALugar(const Place &lugar)
{
    m_mapa->goToPlace(lugar);
    statusBar()->showMessage(
        tr("%1 (%2): %3").arg(lugar.name, lugar.typeLabel(),
                              formatCoordinate(lugar.coordinate, m_formatoCoord)), 6000);
    m_buscador->clearFocus();
}

// Centra el mapa en el sitio elegido y lo dice en la barra de estado.
void Ventana::irAPoi(const PointOfInterest &poi)
{
    m_mapa->goToPointOfInterest(poi);
    statusBar()->showMessage(
        tr("%1 (%2): %3").arg(poi.displayName(), poi.typeLabel(),
                              formatCoordinate(poi.coordinate, m_formatoCoord)), 6000);
    m_buscador->clearFocus();
}

// Capa temporal donde se resalta la calle elegida.
static const char *kCapaCalle = "busqueda_calle";

// Quita el resalte de la calle (si lo hay).
void Ventana::quitarResaltadoCalle()
{
    m_mapa->clearFeatureLayer(QLatin1String(kCapaCalle));
}

// Resalta la calle en el mapa (una línea gruesa naranja, en una capa temporal que no se guarda
// ni se puede elegir con el ratón), la encuadra entera y lo dice en la barra de estado.
void Ventana::irACalle(const Street &calle)
{
    const QString capa = QLatin1String(kCapaCalle);
    if (m_mapa->addFeatureLayer(capa, tr("Búsqueda"), 50)) {
        m_mapa->setFeatureLayerTransient(capa, true);
        m_mapa->setFeatureLayerSelectable(capa, false);
    }
    m_mapa->clearFeatureLayer(capa);
    MapFeature f;
    f.kind = GeometryKind::Polyline;
    f.layerId = capa;
    f.type = QStringLiteral("calle");
    f.name = calle.name;
    f.geometry = calle.parts.value(0);
    if (calle.parts.size() > 1)
        f.parts = calle.parts;
    f.style.lineColor = QColor(0xff, 0x6d, 0x00, 210);
    f.style.lineWidth = 6.0;
    f.style.labelVisible = false;
    f.selectable = false;
    m_mapa->addFeature(f);
    m_mapa->goToStreet(calle);
    statusBar()->showMessage(
        tr("%1 (%2): a %3 del centro").arg(calle.name, calle.typeLabel(), textoDistancia(calle.distanceM)), 6000);
    m_buscador->clearFocus();
}

// Ctrl+K: lleva el foco al buscador con el texto seleccionado.
void Ventana::enfocarBuscador()
{
    if (!m_buscador || !m_buscador->isEnabled()) {
        statusBar()->showMessage(tr("No hay índice de lugares ni mapa vectorial donde buscar."), 5000);
        return;
    }
    m_buscador->setFocus();
    m_buscador->selectAll();
}
