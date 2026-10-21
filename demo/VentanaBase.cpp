// Añadir una capa base (un .mbtiles o una base de teselas .sqlitedb) con el mapa
// abierto: sin editar datasets.json ni reiniciar. Es solo la interfaz; la lectura
// y el sondeo del fichero los hace MapWidget::addBaseLayer.

#include "Ventana.h"

#include <QFileDialog>
#include <QMessageBox>

// Pone la capa en el menú Ver -> Capa base (como opción de radio) y la conecta.
void Ventana::registrarCapaBase(const BaseLayerInfo &c)
{
    QAction *a = m_menuBase->addAction(c.displayName.isEmpty() ? c.id : c.displayName);
    a->setCheckable(true);
    a->setData(c.id);
    a->setChecked(c.id == m_mapa->baseLayerId());
    m_grupoBase->addAction(a);
    connect(a, &QAction::triggered, this, [this, a] {
        const QString id = a->data().toString();
        m_mapa->setBaseLayerId(id);
        if (m_capasBase) m_capasBase->setCurrentIndex(m_capasBase->findData(id));
    });
}

// Pide un MBTiles vectorial y lo pone encima de la base (mapa híbrido). El panel
// lo muestra en «Overlays» con sus grupos (calles, lugares, POI...).
void Ventana::alPonerHibrido()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("MBTiles vectorial (pbf)"), QString(), tr("MBTiles (*.mbtiles);;Todos (*)"));
    if (file.isEmpty())
        return;
    QString error;
    if (!m_mapa->setVectorOverlay(file, &error)) {
        QMessageBox::warning(this, tr("Mapa híbrido"),
                             tr("No se pudo poner el overlay:\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Mapa híbrido: %1 encima de la capa base").arg(file), 6000);
}

// Quita el overlay del mapa híbrido.
void Ventana::alQuitarHibrido()
{
    m_mapa->clearVectorOverlay();
}

// Pide un fichero de teselas, lo añade como capa base y la activa. Si no es una
// base de teselas válida, dice por qué.
void Ventana::alAnadirCapaBase()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Fichero de teselas"), QString(),
        tr("Teselas (*.mbtiles *.sqlitedb *.db);;MBTiles (*.mbtiles);;Todos (*)"));
    if (file.isEmpty())
        return;

    QString error;
    const QString id = m_mapa->addBaseLayer(file, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Añadir capa base"),
                             tr("No se pudo añadir la capa:\n%1").arg(error));
        return;
    }

    // Recoge los datos de la capa nueva para el menú y el desplegable de la barra.
    for (const BaseLayerInfo &c : m_mapa->availableBaseLayers()) {
        if (c.id != id)
            continue;
        if (m_capasBase)
            m_capasBase->addItem(c.displayName.isEmpty() ? c.id : c.displayName, c.id);
        registrarCapaBase(c);
        break;
    }
    m_mapa->setBaseLayerId(id);
    if (m_capasBase)
        m_capasBase->setCurrentIndex(m_capasBase->findData(id));
    pedirReconstruir();                        // el árbol de capas incluye la nueva
    statusBar()->showMessage(tr("Capa base añadida: %1").arg(id), 6000);
}
