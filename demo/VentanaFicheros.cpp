// Ficheros: guardar/abrir entidades, importar/exportar vectorial, exportar vista e historial GPX.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).

#include "Ventana.h"

using namespace libmapa;

// Guarda las entidades en el fichero de trabajo; si no hay, pregunta dónde (Guardar como).
void Ventana::guardar()
{
    if (m_archivoActual.isEmpty()) {
        guardarComo();
        return;
    }
    if (m_mapa->saveFeaturesTo(m_archivoActual)) {
        m_sucio = false;
        actualizarTitulo();
        statusBar()->showMessage(tr("Guardadas %1 entidades en %2")
            .arg(m_mapa->featureCount()).arg(m_archivoActual), 6000);
    } else {
        QMessageBox::warning(this, tr("Guardar"),
            tr("No se pudo guardar en:\n%1").arg(m_archivoActual));
    }
}

// Pide un fichero nuevo y guarda ahí las entidades; pasa a ser el de trabajo.
void Ventana::guardarComo()
{
    const QString base = m_archivoActual.isEmpty()
        ? dirTrabajo() + QStringLiteral("/entidades.db") : m_archivoActual;
    const QString ruta = QFileDialog::getSaveFileName(this,
        tr("Guardar entidades"), base, tr("Base de datos SQLite (*.db)"));
    if (ruta.isEmpty())
        return;
    if (m_mapa->saveFeaturesTo(ruta)) {
        m_archivoActual = ruta;
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        m_sucio = false;
        actualizarTitulo();
        statusBar()->showMessage(tr("Guardadas %1 entidades en %2")
            .arg(m_mapa->featureCount()).arg(ruta), 6000);
    } else {
        QMessageBox::warning(this, tr("Guardar como"),
            tr("No se pudo guardar en:\n%1").arg(ruta));
    }
}

// Abre un fichero de entidades (pregunta antes si hay cambios sin guardar).
void Ventana::abrir()
{
    // Cargar sustituye TODO. Con guardado explícito, primero se ofrece
    // guardar lo pendiente (y se puede cancelar la apertura).
    if (!confirmarDescartar())
        return;

    const QString ruta = QFileDialog::getOpenFileName(this,
        tr("Abrir entidades"), dirTrabajo(),
        tr("Base de datos SQLite (*.db)"));
    if (ruta.isEmpty())
        return;

    if (m_mapa->loadFeaturesFrom(ruta)) {
        m_archivoActual = ruta;
        m_ultimoDir = QFileInfo(ruta).absolutePath();
        m_sucio = false;
        actualizarTitulo();
        // El panel se refresca solo: loadFeaturesFrom -> setContents emite
        // layersChanged. No hace falta reconstruir a mano.
        statusBar()->showMessage(
            tr("Cargadas %1 entidades desde %2")
                .arg(m_mapa->featureCount()).arg(ruta), 6000);
    } else {
        QMessageBox::warning(this, tr("Abrir"),
            tr("No se pudo abrir el fichero:\n%1").arg(ruta));
    }
}

// Carpeta inicial de los diálogos de fichero: la última usada o la actual.
QString Ventana::dirTrabajo() const
{
    return m_ultimoDir.isEmpty() ? QDir::currentPath() : m_ultimoDir;
}

// Título de la ventana con el fichero de trabajo y un * si hay cambios sin guardar.
void Ventana::actualizarTitulo()
{
    const QString f = m_archivoActual.isEmpty()
        ? tr("(sin guardar)") : QFileInfo(m_archivoActual).fileName();
    // Un asterisco delante marca que hay cambios sin guardar (explícito).
    const QString marca = m_sucio ? QStringLiteral("*") : QString();
    const DataPackageInfo paquete = m_mapa ? m_mapa->packageInfo() : DataPackageInfo();
    const QString datos = paquete.isValid()
        ? tr("  -  datos: %1 %2").arg(paquete.name, paquete.dataVersion) : QString();
    setWindowTitle(tr("libmapa - demostracion  -  %1%2%3").arg(marca, f, datos));
}

// Marca el documento como modificado (guardado explícito): refresca el
// título con el asterisco. Lo llaman las ediciones de entidades de usuario;
// las capas temporales (análisis, A/B) NO lo llaman, por eso no ensucian.
void Ventana::marcarSucio()
{
    if (m_sucio)
        return;
    m_sucio = true;
    actualizarTitulo();
}

// Pregunta si guardar los cambios pendientes. Devuelve false solo si el
// usuario cancela (para abortar el cierre o la apertura de otro fichero).
bool Ventana::confirmarDescartar()
{
    if (!m_sucio)
        return true;
    const auto r = QMessageBox::question(this, tr("Cambios sin guardar"),
        tr("Hay entidades sin guardar. ¿Quieres guardarlas?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save) {
        guardar();
        return !m_sucio;      // si el guardado se canceló/falló, no sigas
    }
    return true;              // Descartar
}

// Al cerrar la ventana, ofrece guardar lo pendiente (guardado explícito).
void Ventana::closeEvent(QCloseEvent *e)
{
    if (confirmarDescartar())
        e->accept();
    else
        e->ignore();
}

// Carga un fichero .geo de contornos como capa fija nueva.
void Ventana::cargarGeo()
{
    const QString ruta = QFileDialog::getOpenFileName(this,
        tr("Cargar fichero .geo"), dirTrabajo(),
        tr("Contornos (*.geo);;Todos (*)"));
    if (ruta.isEmpty())
        return;

    const QString id = QFileInfo(ruta).completeBaseName().toLower();
    FeatureStyle estilo;
    estilo.lineColor = QColor(0x00, 0x69, 0x94);
    estilo.fillColor = QColor(0x00, 0x69, 0x94, 40);

    QString error;
    const qint64 fid = m_mapa->loadGeoAsLayer(ruta, id,
        QFileInfo(ruta).completeBaseName(), estilo, &error);
    if (fid < 0) {
        QMessageBox::warning(this, tr("Cargar .geo"),
            tr("No se pudo cargar:\n%1").arg(error));
        return;
    }
    m_ultimoDir = QFileInfo(ruta).absolutePath();
    m_mapa->setActiveFeatureLayer(id);
    marcarSucio();               // el .geo entra como entidad guardable
    statusBar()->showMessage(tr("Cargado %1 como una entidad en la capa '%2'")
        .arg(QFileInfo(ruta).fileName(), id), 5000);
}

// Pide una coordenada (decimal, GMS, UTM o MGRS), centra el mapa en ella y
// la muestra en todos los formatos, como comprobación cruzada.
void Ventana::irACoordenada()
{
    bool aceptado = false;
    const QString txt = QInputDialog::getText(this, tr("Ir a coordenada"),
        tr("Coordenada (p. ej. 23.1136, -82.3666 · 17Q LF 60056 56750 · "
           "17Q 360057 2556751 · 23°06'49\"N 82°22'00\"W):"),
        QLineEdit::Normal, QString(), &aceptado);
    if (!aceptado || txt.trimmed().isEmpty())
        return;
    bool ok = false;
    const QGeoCoordinate c = parseCoordinate(txt, &ok);
    if (!ok) {
        QMessageBox::warning(this, tr("Ir a coordenada"),
                             tr("No se reconoce «%1» como coordenada.").arg(txt));
        return;
    }
    m_mapa->setCenter(c);
    statusBar()->showMessage(tr("%1  ·  %2  ·  %3")
        .arg(formatCoordinate(c, CoordinateFormat::DMS),
             formatCoordinate(c, CoordinateFormat::UTM),
             formatCoordinate(c, CoordinateFormat::MGRS)), 15000);
}

// Importa un GeoJSON/KML/GPX como capa nueva (id = nombre del fichero). Cada
// entidad del fichero entra con su nombre, atributos y estilo.
void Ventana::importarVectorial()
{
    const QString ruta = QFileDialog::getOpenFileName(this,
        tr("Importar entidades"), dirTrabajo(),
        tr("Vectorial (*.geojson *.json *.kml *.gpx);;Todos (*)"));
    if (ruta.isEmpty())
        return;
    const QString id = QFileInfo(ruta).completeBaseName().toLower();
    QString error;
    const qint64 fid = m_mapa->importVectorFile(ruta, id,
        QFileInfo(ruta).completeBaseName(), &error);
    if (fid < 0) {
        QMessageBox::warning(this, tr("Importar"), tr("No se pudo importar:\n%1").arg(error));
        return;
    }
    m_ultimoDir = QFileInfo(ruta).absolutePath();
    m_mapa->setActiveFeatureLayer(id);
    marcarSucio();
    statusBar()->showMessage(tr("Importadas %1 entidades en la capa '%2'")
        .arg(m_mapa->featuresInLayer(id).size()).arg(id), 5000);
}

// Exporta la capa activa; el formato lo decide la extension elegida.
void Ventana::exportarVectorial()
{
    const QString capa = m_mapa->activeFeatureLayer();
    const QString ruta = QFileDialog::getSaveFileName(this,
        tr("Exportar capa '%1'").arg(capa), dirTrabajo() + QLatin1Char('/') + capa + QStringLiteral(".geojson"),
        tr("GeoJSON (*.geojson);;KML (*.kml);;GPX (*.gpx)"));
    if (ruta.isEmpty())
        return;
    QString error;
    if (!m_mapa->exportVectorFile(ruta, capa, &error)) {
        QMessageBox::warning(this, tr("Exportar"), tr("No se pudo exportar:\n%1").arg(error));
        return;
    }
    m_ultimoDir = QFileInfo(ruta).absolutePath();
    statusBar()->showMessage(tr("Capa '%1' exportada a %2")
        .arg(capa, QFileInfo(ruta).fileName()), 5000);
}

// Exporta la vista actual a imagen o PDF. Pide el fichero y un título
// opcional; el pie lleva extensión, fecha, escala (PDF) y la atribución
// de la base, como exige el proveedor de la imagen.
void Ventana::exportarVista(const QString &tituloSugerido)
{
    QString filtro;
    const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar vista"),
        dirTrabajo() + QStringLiteral("/mapa.pdf"),
        tr("PDF (*.pdf);;Imagen PNG (*.png);;Imagen JPEG (*.jpg)"), &filtro);
    if (ruta.isEmpty())
        return;
    bool ok = false;
    const QString titulo = QInputDialog::getText(this, tr("Exportar vista"),
        tr("Título (opcional):"), QLineEdit::Normal, tituloSugerido, &ok);
    if (!ok)
        return;
    MapExportOptions o;
    o.title = titulo;
    o.attribution = tr("Base: %1").arg(m_capasBase ? m_capasBase->currentText() : QString());
    o.imageScale = 2.0;                       // imagen nítida para informes
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool bien = m_mapa->exportView(ruta, o, &error);
    QApplication::restoreOverrideCursor();
    if (!bien) {
        QMessageBox::warning(this, tr("Exportar vista"), tr("No se pudo exportar:\n%1").arg(error));
        return;
    }
    m_ultimoDir = QFileInfo(ruta).absolutePath();
    statusBar()->showMessage(tr("Vista exportada a %1").arg(QFileInfo(ruta).fileName()), 5000);
}

// Exporta el historial completo de todos los objetivos a GPX con horas.
void Ventana::exportarHistorialGpx()
{
    if (m_mapa->targets().isEmpty()) {
        statusBar()->showMessage(tr("No hay objetivos (Datos → Simular objetivos)"), 4000);
        return;
    }
    const QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar historial de objetivos"),
        dirTrabajo() + QStringLiteral("/historial_objetivos.gpx"), tr("GPX (*.gpx)"));
    if (ruta.isEmpty())
        return;
    QString error;
    if (!m_mapa->exportTargetHistory(ruta, {}, &error)) {
        QMessageBox::warning(this, tr("Exportar historial"), tr("No se pudo exportar:\n%1").arg(error));
        return;
    }
    m_ultimoDir = QFileInfo(ruta).absolutePath();
    statusBar()->showMessage(tr("Historial de %1 objetivos exportado a %2")
        .arg(m_mapa->targets().size()).arg(QFileInfo(ruta).fileName()), 5000);
}
