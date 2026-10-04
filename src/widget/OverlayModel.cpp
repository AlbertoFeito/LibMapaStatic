#include "widget/OverlayModel.h"

#include "core/Logging.h"

#include <algorithm>

namespace libmapa {

// Arranca con una unica capa "General" por defecto, para que siempre exista un
// destino valido donde caer las entidades que no indican capa.
OverlayModel::OverlayModel(QObject *parent)
    : QObject(parent)
{
    LayerInfo porDefecto;
    porDefecto.id = defaultLayerId();
    porDefecto.displayName = tr("General");
    m_layers.insert(porDefecto.id, porDefecto);
}

// ------------------------------------------------------ deshacer/rehacer --

// Captura el estado completo (entidades, capas y contador de id) para la pila de
// deshacer. Es una copia por valor: barata frente al coste de repintar el mapa.
OverlayModel::Snapshot OverlayModel::snapshot() const
{
    Snapshot s;
    s.features = m_features;
    s.layers = m_layers;
    s.nextId = m_nextId;
    return s;
}

// Reemplaza el estado por el de una instantanea (usado por undo/redo). Si la
// seleccion actual ya no existe en el estado restaurado, la limpia y avisa. Emite
// una sola tanda de senales para repintar una vez.
void OverlayModel::restore(const Snapshot &s)
{
    m_features = s.features;
    m_layers = s.layers;
    m_nextId = s.nextId;

    // La seleccion puede apuntar a algo que ya no existe.
    if (m_selected >= 0 && !m_features.contains(m_selected)) {
        m_selected = -1;
        emit selectionChanged(-1);
    }
    emit layersChanged();
    emit changed();
}

// Apila el estado actual antes de un cambio. Dentro de un grupo (arrastre) no
// hace nada: la instantanea la puso beginUndoGroup. Limita el tamano de la pila y
// vacia la de rehacer, porque un cambio nuevo invalida cualquier "rehacer".
void OverlayModel::pushUndo()
{
    // Dentro de un grupo solo cuenta la instantanea inicial: arrastrar un
    // vertice genera decenas de moveVertex y debe deshacerse de una vez.
    if (m_groupDepth > 0)
        return;

    m_undo.append(snapshot());
    if (m_undo.size() > m_maxUndo)
        m_undo.removeFirst();

    // Cualquier cambio nuevo invalida la pila de rehacer.
    m_redo.clear();
}

// Abre un grupo de deshacer: toma UNA instantanea y a partir de aqui los pushUndo
// intermedios se ignoran, de modo que todo el gesto (p.ej. arrastrar un vertice,
// decenas de moveVertex) se deshaga como una sola operacion. Reentrante (cuenta).
void OverlayModel::beginUndoGroup()
{
    if (m_groupDepth == 0) {
        m_undo.append(snapshot());
        if (m_undo.size() > m_maxUndo)
            m_undo.removeFirst();
        m_redo.clear();
    }
    ++m_groupDepth;
}

// Cierra el grupo de deshacer abierto por beginUndoGroup (decrementa el contador).
void OverlayModel::endUndoGroup()
{
    if (m_groupDepth > 0)
        --m_groupDepth;
}

// Deshace el ultimo cambio: guarda el estado actual en la pila de rehacer y
// restaura la ultima instantanea de la de deshacer. false si no hay nada que
// deshacer.
bool OverlayModel::undo()
{
    if (m_undo.isEmpty())
        return false;
    m_redo.append(snapshot());
    const Snapshot s = m_undo.takeLast();
    restore(s);
    return true;
}

// Rehace el ultimo cambio deshecho: simetrico de undo() (guarda en deshacer y
// restaura de rehacer). false si no hay nada que rehacer.
bool OverlayModel::redo()
{
    if (m_redo.isEmpty())
        return false;
    m_undo.append(snapshot());
    const Snapshot s = m_redo.takeLast();
    restore(s);
    return true;
}

// Vacia ambas pilas (deshacer y rehacer). Se usa tras cargar un fichero: el
// estado recien abierto es el punto de partida, sin historial previo.
void OverlayModel::clearUndoHistory()
{
    m_undo.clear();
    m_redo.clear();
}

// Sustituye TODO el contenido (entidades y capas) de una vez, p.ej. al abrir un
// documento. Reintroduce la capa "General" por defecto, crea las capas que
// falten, asigna ids a las entidades sin uno y emite una sola tanda de senales
// (una por entidad seria un repintado por entidad).
void OverlayModel::setContents(const QVector<MapFeature> &features,
                               const QVector<LayerInfo> &layers)
{
    pushUndo();

    m_features.clear();
    m_layers.clear();

    LayerInfo porDefecto;
    porDefecto.id = defaultLayerId();
    porDefecto.displayName = tr("General");
    m_layers.insert(porDefecto.id, porDefecto);

    for (const LayerInfo &c : layers)
        if (!c.id.isEmpty())
            m_layers.insert(c.id, c);

    qint64 maxId = 0;
    for (MapFeature f : features) {
        if (!f.isValid())
            continue;
        if (f.layerId.isEmpty())
            f.layerId = defaultLayerId();
        if (!m_layers.contains(f.layerId)) {
            LayerInfo c;
            c.id = f.layerId;
            c.displayName = f.layerId;
            m_layers.insert(c.id, c);
        }
        if (f.id <= 0)
            f.id = ++maxId;
        maxId = qMax(maxId, f.id);
        m_features.insert(f.id, f);
    }
    m_nextId = maxId + 1;

    if (m_selected >= 0 && !m_features.contains(m_selected)) {
        m_selected = -1;
        emit selectionChanged(-1);
    }

    // Una sola senal para toda la carga: con cientos de entidades, emitir una
    // por cada una dispararia otros tantos repintados.
    emit layersChanged();
    emit changed();
}

// ------------------------------------------------------------------ capas --

// Crea una capa nueva (id unico). false si el id esta vacio o ya existe. El
// nombre visible cae al id si se deja en blanco.
bool OverlayModel::addLayer(const QString &id, const QString &displayName,
                            int zOrder)
{
    if (id.isEmpty() || m_layers.contains(id))
        return false;

    LayerInfo capa;
    capa.id = id;
    capa.displayName = displayName.isEmpty() ? id : displayName;
    capa.zOrder = zOrder;
    m_layers.insert(id, capa);

    emit layersChanged();
    emit changed();
    return true;
}

// Borra una capa y todas sus entidades. No se permite borrar la capa "General"
// por defecto. false si no existe.
bool OverlayModel::removeLayer(const QString &id)
{
    if (id == defaultLayerId() || !m_layers.contains(id))
        return false;

    clearLayer(id);
    m_layers.remove(id);
    emit layersChanged();
    emit changed();
    return true;
}

// ¿Existe una capa con ese id?
bool OverlayModel::hasLayer(const QString &id) const
{
    return m_layers.contains(id);
}

// Todas las capas, cada una con su numero de entidades calculado al vuelo, y
// ORDENADAS por zOrder (y por id como desempate, para un resultado reproducible
// que no dependa del orden interno del hash).
QVector<LayerInfo> OverlayModel::layers() const
{
    QVector<LayerInfo> out;
    out.reserve(m_layers.size());
    for (auto it = m_layers.constBegin(); it != m_layers.constEnd(); ++it) {
        LayerInfo capa = it.value();
        capa.featureCount = 0;
        for (const MapFeature &f : m_features)
            if (f.layerId == capa.id)
                ++capa.featureCount;
        out.append(capa);
    }

    // Orden de dibujo. Con el mismo zOrder se ordena por id, para que el
    // resultado no dependa del orden interno del QHash y sea reproducible.
    std::sort(out.begin(), out.end(), [](const LayerInfo &a, const LayerInfo &b) {
        return a.zOrder != b.zOrder ? a.zOrder < b.zOrder : a.id < b.id;
    });
    return out;
}

// Una capa por id (con su contador de entidades al dia), o nullopt si no existe.
std::optional<LayerInfo> OverlayModel::layer(const QString &id) const
{
    auto it = m_layers.constFind(id);
    if (it == m_layers.constEnd())
        return std::nullopt;

    LayerInfo capa = it.value();
    capa.featureCount = 0;
    for (const MapFeature &f : m_features)
        if (f.layerId == id)
            ++capa.featureCount;
    return capa;
}

// Muestra u oculta una capa entera. No hace nada (pero devuelve true) si ya
// estaba en ese estado. false si la capa no existe.
bool OverlayModel::setLayerVisible(const QString &id, bool visible)
{
    auto it = m_layers.find(id);
    if (it == m_layers.end())
        return false;
    if (it->visible == visible)
        return true;
    it->visible = visible;
    emit layersChanged();
    emit changed();
    return true;
}

// Marca una capa como editable o bloqueada (afecta a la interaccion, no al
// dibujo, por eso no emite changed()). false si no existe.
bool OverlayModel::setLayerEditable(const QString &id, bool editable)
{
    auto it = m_layers.find(id);
    if (it == m_layers.end())
        return false;
    it->editable = editable;
    emit layersChanged();
    return true;
}

// Cambia el orden Z (de pintado) de una capa. false si no existe.
bool OverlayModel::setLayerZOrder(const QString &id, int z)
{
    auto it = m_layers.find(id);
    if (it == m_layers.end())
        return false;
    it->zOrder = z;
    emit layersChanged();
    emit changed();
    return true;
}

// -------------------------------------------------------------- entidades --

// Anade una entidad nueva: valida su geometria, la asigna a su capa (creandola si
// no existia) y le da un id nuevo. Devuelve el id, o -1 si la geometria no es
// valida. Registra un paso de deshacer y avisa a la vista.
qint64 OverlayModel::addFeature(MapFeature feature)
{
    if (!feature.isValid()) {
        qCWarning(lcMapaRender)
            << "Entidad descartada: geometria invalida para" << feature.name;
        return -1;
    }

    if (feature.layerId.isEmpty())
        feature.layerId = defaultLayerId();

    // Una capa nueva se crea sola. Fallar porque el nombre no existia todavia
    // obligaria a declararlas todas por adelantado, sin ganar nada.
    if (!m_layers.contains(feature.layerId))
        addLayer(feature.layerId);

    pushUndo();
    feature.id = m_nextId++;
    m_features.insert(feature.id, feature);

    emit featureAdded(feature.id);
    emit layersChanged();          // el contador de su capa subio en uno
    emit changed();
    return feature.id;
}

// Reemplaza una entidad existente (por id) por una version nueva. Conserva la
// capa anterior si la nueva no trae ninguna. false si el id no existe o la
// geometria no es valida.
bool OverlayModel::updateFeature(const MapFeature &feature)
{
    if (feature.id < 0 || !m_features.contains(feature.id))
        return false;
    if (!feature.isValid()) {
        qCWarning(lcMapaRender)
            << "Actualizacion descartada: geometria invalida en la entidad"
            << feature.id;
        return false;
    }

    pushUndo();
    MapFeature copia = feature;
    if (copia.layerId.isEmpty())
        copia.layerId = m_features.value(feature.id).layerId;
    if (!m_layers.contains(copia.layerId))
        addLayer(copia.layerId);

    m_features.insert(copia.id, copia);
    emit featureUpdated(copia.id);
    emit changed();
    return true;
}

// Borra una entidad por id. Si estaba seleccionada, limpia la seleccion. false si
// no existe.
bool OverlayModel::removeFeature(qint64 id)
{
    if (!m_features.contains(id))
        return false;
    pushUndo();
    m_features.remove(id);
    if (m_selected == id) {
        m_selected = -1;
        emit selectionChanged(-1);
    }
    emit featureRemoved(id);
    emit layersChanged();          // el contador de su capa bajo en uno
    emit changed();
    return true;
}

// Borra todas las entidades de una capa (pero conserva la capa). Emite un
// featureRemoved por entidad ademas de changed(), para que un panel de capas
// pueda mantener su lista al dia sin volver a sondear el modelo.
void OverlayModel::clearLayer(const QString &layerId)
{
    QVector<qint64> aBorrar;
    for (auto it = m_features.constBegin(); it != m_features.constEnd(); ++it)
        if (it.value().layerId == layerId)
            aBorrar.append(it.key());

    if (!aBorrar.isEmpty())
        pushUndo();
    for (qint64 id : aBorrar)
        m_features.remove(id);

    if (!aBorrar.isEmpty()) {
        if (aBorrar.contains(m_selected)) {
            m_selected = -1;
            emit selectionChanged(-1);
        }
        // Un vaciado es un borrado en lote: se avisa entidad a entidad, igual
        // que removeFeature, para que quien escuche pueda mantener su lista al
        // dia sin sondear el modelo. Sin esto, un panel de capas no se enteraba
        // de "Vaciar" porque solo se emitia changed().
        for (qint64 id : aBorrar)
            emit featureRemoved(id);
        emit layersChanged();      // los contadores por capa cambiaron
        emit changed();
    }
}

// Borra TODAS las entidades de todas las capas (las capas se conservan). Como
// clearLayer, avisa entidad a entidad para que los oyentes se pongan al dia.
void OverlayModel::clear()
{
    if (m_features.isEmpty())
        return;

    QVector<qint64> aBorrar;
    aBorrar.reserve(m_features.size());
    for (auto it = m_features.constBegin(); it != m_features.constEnd(); ++it)
        aBorrar.append(it.key());

    pushUndo();
    m_features.clear();
    if (m_selected != -1) {
        m_selected = -1;
        emit selectionChanged(-1);
    }
    for (qint64 id : aBorrar)
        emit featureRemoved(id);
    emit layersChanged();          // todos los contadores quedan a cero
    emit changed();
}

// Una entidad por id, o nullopt si no existe.
std::optional<MapFeature> OverlayModel::feature(qint64 id) const
{
    auto it = m_features.constFind(id);
    if (it == m_features.constEnd())
        return std::nullopt;
    return it.value();
}

// Todas las entidades, ordenadas por id (orden estable de creacion) para que la
// vista las pinte de forma reproducible.
QVector<MapFeature> OverlayModel::features() const
{
    QVector<MapFeature> out;
    out.reserve(m_features.size());
    for (const MapFeature &f : m_features)
        out.append(f);
    std::sort(out.begin(), out.end(),
              [](const MapFeature &a, const MapFeature &b) { return a.id < b.id; });
    return out;
}

// Entidades de una capa concreta (mismo orden estable que features()).
QVector<MapFeature> OverlayModel::featuresInLayer(const QString &layerId) const
{
    QVector<MapFeature> out;
    for (const MapFeature &f : features())
        if (f.layerId == layerId)
            out.append(f);
    return out;
}

// Entidades de un 'tipo' de dominio concreto (etiqueta que la libreria no
// interpreta; la pone y filtra la aplicacion).
QVector<MapFeature> OverlayModel::featuresOfType(const QString &type) const
{
    QVector<MapFeature> out;
    for (const MapFeature &f : features())
        if (f.type == type)
            out.append(f);
    return out;
}

// ------------------------------------------------------------- geometria --

// Mueve un vertice suelto de una entidad a una nueva coordenada. Solo geometrias
// de una parte (en una multi-parte no se sabria a que parte pertenece el indice).
// false si el id/indice no es valido, la entidad es multi-parte o la coord no vale.
bool OverlayModel::moveVertex(qint64 id, int index, const QGeoCoordinate &to)
{
    auto it = m_features.find(id);
    if (it == m_features.end() || !to.isValid())
        return false;
    // La edicion de vertices sueltos es de una sola parte: en una entidad
    // multi-parte (un .geo entero) no se sabria que parte tocar.
    if (it->isMultiPart())
        return false;
    if (index < 0 || index >= it->geometry.size())
        return false;

    pushUndo();
    it->geometry[index] = to;
    emit featureUpdated(id);
    emit changed();
    return true;
}

// Inserta un vertice nuevo en la posicion 'index' de una entidad (para prolongar
// o subdividir una linea/poligono). No aplica a puntos ni a multi-parte. false si
// algo no cuadra.
bool OverlayModel::insertVertex(qint64 id, int index, const QGeoCoordinate &at)
{
    auto it = m_features.find(id);
    if (it == m_features.end() || !at.isValid())
        return false;
    if (it->isMultiPart())
        return false;
    if (it->kind == GeometryKind::Point)
        return false;                       // un punto tiene un solo vertice
    if (index < 0 || index > it->geometry.size())
        return false;

    pushUndo();
    it->geometry.insert(index, at);
    emit featureUpdated(id);
    emit changed();
    return true;
}

// Elimina un vertice de una entidad, pero NUNCA hasta degenerarla (un poligono no
// puede quedar con menos de 3 vertices, etc.): para eso esta removeFeature. No
// aplica a multi-parte. false si el borrado no es valido.
bool OverlayModel::removeVertex(qint64 id, int index)
{
    auto it = m_features.find(id);
    if (it == m_features.end())
        return false;
    if (it->isMultiPart())
        return false;
    if (index < 0 || index >= it->geometry.size())
        return false;

    // No se deja degenerar la geometria: un poligono de dos vertices no es un
    // poligono. Quien quiera eliminarlo del todo tiene removeFeature.
    if (it->geometry.size() <= it->minimumVertices())
        return false;

    pushUndo();
    it->geometry.remove(index);
    emit featureUpdated(id);
    emit changed();
    return true;
}

// Desplaza una entidad ENTERA (todos sus vertices, y cada parte si es
// multi-parte) sumando un delta de lat/lon. Es todo-o-nada: si el movimiento
// sacaria algun vertice fuera del mundo, se rechaza sin tocar nada (no se deja la
// geometria a medias con coordenadas NaN). false si el id no existe o hay
// desbordamiento.
bool OverlayModel::moveFeature(qint64 id, double deltaLat, double deltaLon)
{
    auto it = m_features.find(id);
    if (it == m_features.end())
        return false;

    // Desplaza TODA la geometria (geometry y, si es multi-parte, cada parte).
    // Un desplazamiento que saque cualquier vertice del mundo se rechaza
    // entero, no a medias: QGeoCoordinate se marcaria invalida y devolveria
    // NaN sin avisar.
    const auto desplazar =
        [&](const QVector<QGeoCoordinate> &origen,
            QVector<QGeoCoordinate> &destino) -> bool {
        destino.clear();
        destino.reserve(origen.size());
        for (const QGeoCoordinate &c : origen) {
            const QGeoCoordinate movido(c.latitude() + deltaLat,
                                        c.longitude() + deltaLon);
            if (!movido.isValid())
                return false;
            destino.append(movido);
        }
        return true;
    };

    QVector<QGeoCoordinate> nuevaGeom;
    if (!desplazar(it->geometry, nuevaGeom))
        return false;

    QVector<QVector<QGeoCoordinate>> nuevasPartes;
    nuevasPartes.reserve(it->parts.size());
    for (const QVector<QGeoCoordinate> &parte : it->parts) {
        QVector<QGeoCoordinate> movida;
        if (!desplazar(parte, movida))
            return false;
        nuevasPartes.append(movida);
    }

    pushUndo();
    it->geometry = nuevaGeom;
    it->parts = nuevasPartes;
    emit featureUpdated(id);
    emit changed();
    return true;
}

// -------------------------------------------------------------- seleccion --

// Selecciona una entidad por id (o -1 para no seleccionar nada). No hace nada si
// ya estaba seleccionada, ni si el id no existe. Emite selectionChanged/changed.
void OverlayModel::setSelected(qint64 id)
{
    if (m_selected == id)
        return;
    if (id >= 0 && !m_features.contains(id))
        return;
    m_selected = id;
    emit selectionChanged(m_selected);
    emit changed();
}

// Quita la seleccion actual (equivale a setSelected(-1)).
void OverlayModel::clearSelection()
{
    setSelected(-1);
}

} // namespace libmapa
