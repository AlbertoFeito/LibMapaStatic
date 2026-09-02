#ifndef LIBMAPA_MAPWIDGET_H_
#define LIBMAPA_MAPWIDGET_H_

#include "libmapa/MapFeature.h"
#include "libmapa/MapTypes.h"
#include "libmapa/libmapa_export.h"

#include <QGeoCoordinate>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <memory>
#include <optional>

namespace libmapa {

//! Ajustes de arranque del widget.
struct MapConfig
{
    /*!
     * \brief Ruta al datasets.json generado por probe_db.
     *
     * Ahi estan las convenciones de cada BD: el mapeo de zoom, el esquema del
     * eje Y, el nivel de fondo garantizado y el relleno tipico. Nada de eso
     * se cablea en el codigo.
     */
    QString datasetsFile;

    QString initialLayerId;                    //!< Vacio = el primero del JSON.
    QGeoCoordinate initialCenter{23.1136, -82.3666};
    int initialZoom = 10;

    int cacheMiB = 128;         //!< Memoria para teselas ya decodificadas.
    int debounceMs = 80;        //!< Agrupacion de peticiones al arrastrar.

    //! Color de las zonas sin ninguna tesela. Azul mar por defecto: las BD no
    //! guardan teselas de oceano abierto y ese color ES el mapa correcto.
    QColor noDataColor = QColor(0x9d, 0xd3, 0xdf);
};

/*!
 * \brief Widget de mapa embebible.
 *
 * Se usa asi, y esto es todo lo que hace falta:
 *
 *     libmapa::MapConfig cfg;
 *     cfg.datasetsFile = QDir::currentPath() + "/datasets.json";
 *
 *     auto *mapa = new libmapa::MapWidget(cfg, this);
 *     ui->contenedor->layout()->addWidget(mapa);
 *
 *     connect(ui->btnSat, &QPushButton::clicked, mapa, [mapa]{
 *         mapa->setBaseLayerId("satelital");
 *     });
 *
 * El motor de dibujo es QCustomPlot, pero NO aparece en esta cabecera. En el
 * codigo original, cmapaplot.h hacia #include "qcustomplot.h" y CMapaPlot
 * heredaba publicamente de QCustomPlot: cualquier proyecto que usara la
 * libreria se tragaba 300 KB de cabecera y veia doscientos metodos que no
 * deberia tocar. Aqui QCustomPlot queda dentro del PIMPL.
 *
 * Quien quiera acceso directo al QCustomPlot interno para dibujar sus propias
 * capas puede pedirlo con customPlot(), pero es una salida de emergencia
 * explicita, no la via normal.
 */
class LIBMAPA_EXPORT MapWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString baseLayerId READ baseLayerId WRITE setBaseLayerId
                   NOTIFY baseLayerChanged)
    Q_PROPERTY(int zoom READ zoom WRITE setZoom NOTIFY zoomChanged)

public:
    explicit MapWidget(const MapConfig &config, QWidget *parent = nullptr);
    ~MapWidget() override;

    //! false si no se pudo abrir ninguna base de datos de mapas.
    bool isReady() const;
    QString lastError() const;

    // --- Capa base -------------------------------------------------------
    QVector<BaseLayerInfo> availableBaseLayers() const;
    QString baseLayerId() const;
    bool setBaseLayerId(const QString &id);

    // --- Navegacion ------------------------------------------------------
    QGeoCoordinate center() const;
    void setCenter(const QGeoCoordinate &center);

    int zoom() const;
    void setZoom(int zoom);
    int minZoom() const;
    int maxZoom() const;

    void zoomIn();
    void zoomOut();
    void fitBounds(const QGeoCoordinate &northWest,
                   const QGeoCoordinate &southEast);

    //! Extension visible ahora mismo.
    QGeoCoordinate visibleNorthWest() const;
    QGeoCoordinate visibleSouthEast() const;

    // --- Capas de entidades ----------------------------------------------
    /*!
     * \brief Crea una capa. Si ya existe, no hace nada y devuelve false.
     *
     * "Pueden existir varias de cada uno" se resuelve creando las capas que
     * hagan falta: una para zonas prohibidas, otra para puntos de interes,
     * otra para areas de vigilancia. La libreria no impone ninguna.
     */
    bool addFeatureLayer(const QString &id, const QString &displayName = QString(),
                         int zOrder = 0);
    bool removeFeatureLayer(const QString &id);
    QVector<LayerInfo> featureLayers() const;
    bool setFeatureLayerVisible(const QString &id, bool visible);
    bool setFeatureLayerZOrder(const QString &id, int zOrder);

    // --- Entidades -------------------------------------------------------
    //! Devuelve el identificador asignado, o -1 si la geometria no es valida.
    qint64 addFeature(const MapFeature &feature);
    bool updateFeature(const MapFeature &feature);
    bool removeFeature(qint64 id);
    void clearFeatureLayer(const QString &layerId);
    void clearFeatures();

    std::optional<MapFeature> feature(qint64 id) const;
    QVector<MapFeature> features() const;
    QVector<MapFeature> featuresInLayer(const QString &layerId) const;
    //! Filtra por la etiqueta de dominio que puso la aplicacion.
    QVector<MapFeature> featuresOfType(const QString &type) const;
    int featureCount() const;

    // --- Edicion de geometria --------------------------------------------
    bool moveVertex(qint64 id, int index, const QGeoCoordinate &to);
    bool insertVertex(qint64 id, int index, const QGeoCoordinate &at);
    bool removeVertex(qint64 id, int index);
    bool moveFeature(qint64 id, double deltaLat, double deltaLon);

    // --- Seleccion -------------------------------------------------------
    qint64 selectedFeature() const;
    void selectFeature(qint64 id);
    void clearSelection();

    //! Entidad bajo un punto de la pantalla, o -1.
    qint64 featureAt(const QPoint &pixel, double tolerancePx = 8.0) const;

    // --- Herramientas ----------------------------------------------------
    MapTool activeTool() const;
    void setActiveTool(MapTool tool);

    //! Capa donde van las entidades que se creen con el raton.
    void setActiveFeatureLayer(const QString &id);
    QString activeFeatureLayer() const;

    //! Estilo y tipo de dominio de las entidades que se creen a partir de ahora.
    void setDraftStyle(const FeatureStyle &style);
    void setDraftType(const QString &type);

    //! true mientras hay una geometria a medio trazar.
    bool isDrawing() const;
    //! Cierra el trazado en curso. Devuelve el identificador, o -1.
    qint64 finishDrawing();
    //! Descarta el trazado en curso.
    bool cancelDrawing();

    // --- Diagnostico -----------------------------------------------------
    //! Porcentaje de la pantalla resuelto con teselas propias (no ancestros).
    double exactCoverage() const;
    void setDebugGridVisible(bool visible);

    /*!
     * \brief Geografico -> coordenadas de los ejes del QCustomPlot interno.
     *
     * IMPRESCINDIBLE para cualquier overlay dibujado con coordenadas de eje
     * (QCPCurve, QCPItemEllipse, QCPItemLine...). El eje X va en grados de
     * longitud, pero el Y NO va en grados de latitud: va en "grados de
     * Mercator", porque el eje de QCustomPlot es lineal y la proyeccion no lo
     * es. Pasarle la latitud directamente deforma el mapa: a 23 grados el
     * desfase es de 0.64 y a 60 de 15.5.
     *
     *     QCPItemEllipse *punto = new QCPItemEllipse(plot);
     *     const QPointF c = mapa->toAxisCoords(coordenada);
     *     punto->topLeft->setCoords(c.x() - r, c.y() + r);
     *     punto->bottomRight->setCoords(c.x() + r, c.y() - r);
     */
    QPointF toAxisCoords(const QGeoCoordinate &position) const;
    QGeoCoordinate fromAxisCoords(const QPointF &axisPoint) const;

    /*!
     * \brief QCustomPlot interno, para dibujar capas propias.
     *
     * Devuelve QWidget* a proposito: quien lo necesite hara el cast y
     * asumira la dependencia en SU codigo, sin imponersela a los demas.
     */
    QWidget *customPlot() const;

signals:
    void baseLayerChanged(const QString &id);
    void featureAdded(qint64 id);
    void featureUpdated(qint64 id);
    void featureRemoved(qint64 id);
    void featureSelected(qint64 id);      //!< -1 al deseleccionar
    void featureLayersChanged();
    //! Emitida al crear una entidad con el raton.
    void featureCreated(qint64 id);
    void drawingCancelled();
    void zoomChanged(int zoom);
    void centerChanged(const QGeoCoordinate &center);
    void mouseMoved(const QGeoCoordinate &position);
    void clicked(const QGeoCoordinate &position, Qt::MouseButton button);
    void measurementFinished(const libmapa::Measurement &measurement);
    void areaSelected(const QGeoCoordinate &northWest,
                      const QGeoCoordinate &southEast);
    void pointPicked(const QGeoCoordinate &position);
    void errorOccurred(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;   //!< Firma correcta (F-18)

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace libmapa

#endif // LIBMAPA_MAPWIDGET_H_
