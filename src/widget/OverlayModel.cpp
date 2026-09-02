#include "widget/OverlayModel.h"

#include "core/Logging.h"

#include <algorithm>

namespace libmapa {

OverlayModel::OverlayModel(QObject *parent)
    : QObject(parent)
{
    LayerInfo porDefecto;
    porDefecto.id = defaultLayerId();
    porDefecto.displayName = tr("General");
    m_layers.insert(porDefecto.id, porDefecto);
}

// ------------------------------------------------------ deshacer/rehacer --

OverlayModel::Snapshot OverlayModel::snapshot() const
{
    Snapshot s;
    s.features = m_features;
    s.layers = m_layers;
    s.nextId = m_nextId;
    return s;
}

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

void OverlayModel::endUndoGroup()
{
    if (m_groupDepth > 0)
        --m_groupDepth;
}

bool OverlayModel::undo()
{
    if (m_undo.isEmpty())
        return false;
    m_redo.append(snapshot());
    const Snapshot s = m_undo.takeLast();
    restore(s);
    return true;
}

bool OverlayModel::redo()
{
    if (m_redo.isEmpty())
        return false;
    m_undo.append(snapshot());
    const Snapshot s = m_redo.takeLast();
    restore(s);
    return true;
}

void OverlayModel::clearUndoHistory()
{
    m_undo.clear();
    m_redo.clear();
}

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

bool OverlayModel::hasLayer(const QString &id) const
{
    return m_layers.contains(id);
}

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

bool OverlayModel::setLayerEditable(const QString &id, bool editable)
{
    auto it = m_layers.find(id);
    if (it == m_layers.end())
        return false;
    it->editable = editable;
    emit layersChanged();
    return true;
}

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
    emit changed();
    return feature.id;
}

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
    emit changed();
    return true;
}

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
        emit changed();
    }
}

void OverlayModel::clear()
{
    if (!m_features.isEmpty())
        pushUndo();
    m_features.clear();
    if (m_selected != -1) {
        m_selected = -1;
        emit selectionChanged(-1);
    }
    emit changed();
}

std::optional<MapFeature> OverlayModel::feature(qint64 id) const
{
    auto it = m_features.constFind(id);
    if (it == m_features.constEnd())
        return std::nullopt;
    return it.value();
}

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

QVector<MapFeature> OverlayModel::featuresInLayer(const QString &layerId) const
{
    QVector<MapFeature> out;
    for (const MapFeature &f : features())
        if (f.layerId == layerId)
            out.append(f);
    return out;
}

QVector<MapFeature> OverlayModel::featuresOfType(const QString &type) const
{
    QVector<MapFeature> out;
    for (const MapFeature &f : features())
        if (f.type == type)
            out.append(f);
    return out;
}

// ------------------------------------------------------------- geometria --

bool OverlayModel::moveVertex(qint64 id, int index, const QGeoCoordinate &to)
{
    auto it = m_features.find(id);
    if (it == m_features.end() || !to.isValid())
        return false;
    if (index < 0 || index >= it->geometry.size())
        return false;

    pushUndo();
    it->geometry[index] = to;
    emit featureUpdated(id);
    emit changed();
    return true;
}

bool OverlayModel::insertVertex(qint64 id, int index, const QGeoCoordinate &at)
{
    auto it = m_features.find(id);
    if (it == m_features.end() || !at.isValid())
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

bool OverlayModel::removeVertex(qint64 id, int index)
{
    auto it = m_features.find(id);
    if (it == m_features.end())
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

bool OverlayModel::moveFeature(qint64 id, double deltaLat, double deltaLon)
{
    auto it = m_features.find(id);
    if (it == m_features.end())
        return false;

    QVector<QGeoCoordinate> nueva;
    nueva.reserve(it->geometry.size());
    for (const QGeoCoordinate &c : it->geometry) {
        const QGeoCoordinate movido(c.latitude() + deltaLat,
                                    c.longitude() + deltaLon);
        // Un desplazamiento que saque la geometria del mundo se rechaza
        // entera, no a medias: QGeoCoordinate se marcaria invalida y
        // devolveria NaN sin avisar.
        if (!movido.isValid())
            return false;
        nueva.append(movido);
    }

    pushUndo();
    it->geometry = nueva;
    emit featureUpdated(id);
    emit changed();
    return true;
}

// -------------------------------------------------------------- seleccion --

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

void OverlayModel::clearSelection()
{
    setSelected(-1);
}

} // namespace libmapa
