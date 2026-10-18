// Buscador de lugares de la demo: un campo en la barra principal que, al escribir,
// propone lugares (ciudades, aeropuertos, bahías...) del índice sin conexión del
// paquete, y al elegir uno centra el mapa en él. Sin índice, el campo se ve
// desactivado y dice por qué.

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
    m_buscador->setPlaceholderText(tr("Buscar lugar…  (Ctrl+K)"));

    m_sugerencias = new QStringListModel(this);
    auto *completer = new QCompleter(m_sugerencias, this);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_buscador->setCompleter(completer);

    barra->addSeparator();
    barra->addWidget(m_buscador);

    connect(m_buscador, &QLineEdit::textEdited, this, &Ventana::buscarLugares);
    connect(m_buscador, &QLineEdit::returnPressed, this, [this] {
        if (!m_lugaresVistos.isEmpty())
            irALugar(m_lugaresVistos.first());
    });
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated), this,
            [this](const QString &texto) {
                const auto i = m_sugerencias->stringList().indexOf(texto);
                if (i >= 0 && i < m_lugaresVistos.size())
                    irALugar(m_lugaresVistos.at(i));
            });
    actualizarBuscador();
}

// Activa o desactiva el campo según haya índice de lugares.
void Ventana::actualizarBuscador()
{
    if (!m_buscador)
        return;
    const bool hay = m_mapa->hasPlaces();
    m_buscador->setEnabled(hay);
    m_buscador->setToolTip(hay ? tr("Escribe un nombre (sin tildes vale) y elige un lugar.")
                               : tr("No hay índice de lugares. Añade \"places\" al mapa.json "
                                    "(se genera con osm_lugares_a_sqlitedb.py)."));
}

// Busca lugares con lo escrito y rellena el desplegable con «nombre — tipo».
void Ventana::buscarLugares(const QString &texto)
{
    m_lugaresVistos = m_mapa->searchPlaces(texto, 12);
    QStringList filas;
    for (const Place &p : m_lugaresVistos)
        filas << QStringLiteral("%1 — %2").arg(p.name, p.typeLabel());
    m_sugerencias->setStringList(filas);
    if (!filas.isEmpty() && m_buscador->completer())
        m_buscador->completer()->complete();
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

// Ctrl+K: lleva el foco al buscador con el texto seleccionado.
void Ventana::enfocarBuscador()
{
    if (!m_buscador || !m_buscador->isEnabled()) {
        statusBar()->showMessage(tr("No hay índice de lugares en este paquete."), 5000);
        return;
    }
    m_buscador->setFocus();
    m_buscador->selectAll();
}
