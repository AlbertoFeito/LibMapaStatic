#ifndef LIBMAPA_WIDGET_OVERLAYMODEL_H_
#define LIBMAPA_WIDGET_OVERLAYMODEL_H_

#include "libmapa/MapFeature.h"

#include <QHash>
#include <QObject>
#include <QVector>
#include <optional>

namespace libmapa {

/*!
 * \brief Guarda las entidades y las capas. Sin dibujar nada.
 *
 * Separado de la capa de dibujo a proposito: asi se puede probar toda la
 * gestion de entidades sin ventanas, y el dibujo solo tiene que preocuparse
 * de pintar lo que el modelo le diga.
 *
 * Los identificadores son enteros que asigna el modelo. La aplicacion nunca
 * maneja punteros a entidades, que era el problema de QList<CPunto*> y
 * QList<void*> en el codigo original: cualquiera podia quedarse un puntero y
 * usarlo despues de que la lista lo hubiera borrado.
 */
class OverlayModel : public QObject
{
    Q_OBJECT

public:
    explicit OverlayModel(QObject *parent = nullptr);

    // --- Capas -----------------------------------------------------------
    bool addLayer(const QString &id, const QString &displayName = QString(),
                  int zOrder = 0);
    bool removeLayer(const QString &id);
    bool hasLayer(const QString &id) const;

    //! Capas ordenadas por zOrder ascendente: la ultima se pinta encima.
    QVector<LayerInfo> layers() const;
    std::optional<LayerInfo> layer(const QString &id) const;

    bool setLayerVisible(const QString &id, bool visible);
    bool setLayerEditable(const QString &id, bool editable);
    bool setLayerZOrder(const QString &id, int z);

    // --- Entidades -------------------------------------------------------
    /*!
     * \brief Anade una entidad y devuelve su identificador.
     *
     * Si \a feature.layerId esta vacio va a la capa por defecto. Si la capa
     * no existe, se crea: es mas util que fallar por un nombre nuevo.
     */
    qint64 addFeature(MapFeature feature);

    bool updateFeature(const MapFeature &feature);
    bool removeFeature(qint64 id);
    void clearLayer(const QString &layerId);
    void clear();

    std::optional<MapFeature> feature(qint64 id) const;
    QVector<MapFeature> features() const;
    QVector<MapFeature> featuresInLayer(const QString &layerId) const;
    QVector<MapFeature> featuresOfType(const QString &type) const;
    int count() const { return static_cast<int>(m_features.size()); }

    // --- Geometria, para editar ------------------------------------------
    bool moveVertex(qint64 id, int index, const QGeoCoordinate &to);
    bool insertVertex(qint64 id, int index, const QGeoCoordinate &at);
    bool removeVertex(qint64 id, int index);
    bool moveFeature(qint64 id, double deltaLat, double deltaLon);

    // --- Deshacer y rehacer ----------------------------------------------
    /*!
     * \brief Marca el inicio de una accion deshacible.
     *
     * Se guarda una instantanea del estado completo. Con centenares de
     * entidades el coste es despreciable frente a la alternativa: un sistema
     * de comandos con su inverso para cada operacion, que hay que escribir y
     * mantener para cada tipo de edicion, y donde un inverso mal calculado
     * corrompe los datos sin avisar.
     *
     * Las operaciones que modifican el modelo la llaman solas. Se expone para
     * poder agrupar varias en un solo paso: arrastrar un vertice genera
     * decenas de moveVertex y debe deshacerse de una vez.
     */
    void beginUndoGroup();
    void endUndoGroup();

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool undo();
    bool redo();
    void clearUndoHistory();
    int undoDepth() const { return static_cast<int>(m_undo.size()); }

    // --- Carga masiva ----------------------------------------------------
    /*!
     * \brief Sustituye TODO el contenido de una vez.
     *
     * Emite una sola senal 'changed' en vez de una por entidad, que con
     * cientos de ellas dispararia otros tantos repintados.
     */
    void setContents(const QVector<MapFeature> &features,
                     const QVector<LayerInfo> &layers = {});

    // --- Seleccion -------------------------------------------------------
    void setSelected(qint64 id);
    void clearSelection();
    qint64 selectedId() const { return m_selected; }

    //! Nombre de la capa a la que van las entidades sin capa indicada.
    static QString defaultLayerId() { return QStringLiteral("default"); }

signals:
    //! Algo cambio y hay que repintar. Una sola senal: al que dibuja no le
    //! importa si fue un alta, una edicion o un cambio de visibilidad.
    void changed();

    void featureAdded(qint64 id);
    void featureUpdated(qint64 id);
    void featureRemoved(qint64 id);
    void selectionChanged(qint64 id);
    void layersChanged();

private:
    QHash<qint64, MapFeature> m_features;
    QHash<QString, LayerInfo> m_layers;
    qint64 m_nextId = 1;
    qint64 m_selected = -1;

    struct Snapshot {
        QHash<qint64, MapFeature> features;
        QHash<QString, LayerInfo> layers;
        qint64 nextId = 1;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &s);
    void pushUndo();

    QVector<Snapshot> m_undo;
    QVector<Snapshot> m_redo;
    int m_groupDepth = 0;
    int m_maxUndo = 50;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_OVERLAYMODEL_H_
