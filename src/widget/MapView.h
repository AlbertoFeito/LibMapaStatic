#ifndef LIBMAPA_WIDGET_MAPVIEW_H_
#define LIBMAPA_WIDGET_MAPVIEW_H_

#include "qcustomplot.h"

#include "libmapa/MapTypes.h"
#include "tiles/TileService.h"
#include "widget/FeatureLayer.h"
#include "widget/OverlayModel.h"
#include "widget/TileLayer.h"

#include <QGeoCoordinate>

namespace libmapa {

/*!
 * \brief QCustomPlot configurado como mapa. Vive dentro del PIMPL.
 *
 * Los ejes trabajan en grados: x = longitud, y = latitud. Eso permite que
 * cualquier overlay que ya use coordenadas de eje (los QCPCurve de rutas, los
 * QCPItemEllipse de puntos) siga funcionando sin cambios.
 *
 * Lo que NO se hace aqui, a diferencia del original:
 *
 *   - No se crea un QCPItemPixmap por tesela en cada frame. Hay una sola
 *     TileLayer que las pinta todas (falla F-15).
 *   - No se lee la base de datos desde el hilo de la interfaz. Se pide a
 *     TileService y se repinta cuando avisa (falla F-17).
 *   - resizeEvent no recarga capas ni ficheros: solo recalcula el viewport.
 *   - La relacion de aspecto se calcula, no se busca en una tabla de
 *     resoluciones cableada a mano (falla F-20).
 */
class MapView : public QCustomPlot
{
    Q_OBJECT

public:
    explicit MapView(TileService *service, QWidget *parent = nullptr);
    ~MapView() override;

    TileLayer *tileLayer() const { return m_tileLayer; }
    FeatureLayer *featureLayer() const { return m_featureLayer; }
    OverlayModel *overlayModel() const { return m_model; }

    QGeoCoordinate center() const;
    void setCenter(const QGeoCoordinate &center);

    int zoom() const { return m_zoom; }
    void setZoom(int zoom, const QPointF *anchorPx = nullptr);

    void fitBounds(const QGeoCoordinate &northWest,
                   const QGeoCoordinate &southEast);

    QGeoCoordinate visibleNorthWest() const;
    QGeoCoordinate visibleSouthEast() const;

    QGeoCoordinate coordinateAt(const QPoint &pixel) const;

    //! Geografico <-> coordenadas de eje (lon, mercatorY). Las necesita
    //! cualquier overlay que se dibuje con coordenadas de eje.
    static QPointF toAxis(const QGeoCoordinate &c);
    static QGeoCoordinate fromAxis(const QPointF &p);

    //! Recorta longitud a [-180,180] y latitud al limite de Mercator, y
    //! sustituye NaN por 0. Sin esto, un QGeoCoordinate invalido llena los
    //! ejes de NaN y QCustomPlot se cuelga.
    static QGeoCoordinate normalized(const QGeoCoordinate &c);

    /*!
     * \brief Conversion entre coordenadas geograficas y las del eje.
     *
     * El eje x es la longitud en grados; el eje y NO es la latitud, sino la
     * ordenada de Mercator en grados. Cualquier overlay que se dibuje con
     * coordenadas de eje tiene que pasar por aqui.
     */
    static QPointF toAxisCoords(const QGeoCoordinate &coord);
    static QGeoCoordinate fromAxisCoords(const QPointF &axisPoint);

    MapTool activeTool() const { return m_tool; }
    void setActiveTool(MapTool tool);

    /*!
     * \brief Pone al dia el viewport y la disposicion sin esperar eventos.
     *
     * QCustomPlot actualiza su viewport en resizeEvent y la geometria del
     * axisRect durante el repintado. Ambas cosas son diferidas, asi que
     * inmediatamente despues de un setGeometry las conversiones
     * coordenada<->pixel usarian todavia el tamano viejo.
     */
    void ensureLayout();

    //! Rehace el plan de dibujo con lo que haya en cache y repinta.
    void refreshPlan();

    //! Pide al servicio las teselas del viewport actual.
    void requestVisibleTiles();

    //! Capa donde se crean las entidades nuevas.
    void setActiveFeatureLayer(const QString &id) { m_activeLayer = id; }
    QString activeFeatureLayer() const { return m_activeLayer; }

    //! Estilo de las entidades que se creen a partir de ahora.
    void setDraftStyle(const FeatureStyle &style) { m_draftStyle = style; }
    FeatureStyle draftStyle() const { return m_draftStyle; }

    //! Tipo de dominio que se pone a las entidades nuevas.
    void setDraftType(const QString &type) { m_draftType = type; }

    //! Cancela el trazado en curso. Devuelve false si no habia ninguno.
    bool cancelDrawing();
    //! Cierra el trazado en curso y crea la entidad. -1 si no era valida.
    qint64 finishDrawing();
    bool isDrawing() const { return m_drafting; }

signals:
    void zoomChanged(int zoom);
    void featureCreated(qint64 id);
    void featureClicked(qint64 id, const QGeoCoordinate &position);
    void drawingCancelled();
    void centerChanged(const QGeoCoordinate &center);
    void mouseMovedTo(const QGeoCoordinate &position);
    void mapClicked(const QGeoCoordinate &position, Qt::MouseButton button);
    void measurementFinished(const libmapa::Measurement &measurement);
    void areaSelected(const QGeoCoordinate &northWest,
                      const QGeoCoordinate &southEast);
    void pointPicked(const QGeoCoordinate &position);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    //! Atiende un clic de la herramienta de medir. Se llama AL PULSAR.
    void handleMeasureClick(const QGeoCoordinate &donde);

private slots:
    void onTilesReady(quint64 requestId, int zoom, int newTiles);

private:
    //! Ajusta el rango de los ejes para que el zoom actual encaje en el widget.
    void applyZoomToAxes(const QGeoCoordinate &center);

    //! Relacion de aspecto real del area de dibujo. Sustituye a las setenta
    //! lineas de "if (ancho==1280 && alto==1024) ..." del original.
    double aspectRatio() const;

    /*!
     * \brief Area de dibujo en pixeles, valida incluso antes del primer
     *        repintado.
     *
     * axisRect()->rect() solo tiene tamano despues de que QCustomPlot haya
     * recalculado su disposicion, cosa que ocurre en el primer replot. Antes
     * de eso devuelve 0x0, y todo el calculo de zoom sale mal. Con el widget
     * sin mostrar puede no ocurrir nunca. Por eso se usa viewport(), que
     * QCustomPlot actualiza en su resizeEvent, con rect() como ultimo recurso.
     */
    QRect plotArea() const;

    TileService *m_service = nullptr;
    TileLayer *m_tileLayer = nullptr;
    OverlayModel *m_model = nullptr;
    FeatureLayer *m_featureLayer = nullptr;

    int m_zoom = 10;
    QGeoCoordinate m_center{23.1136, -82.3666};

    MapTool m_tool = MapTool::None;
    bool m_dragging = false;
    QPoint m_dragStartPx;
    QGeoCoordinate m_dragStartGeo;

    // Estado de las herramientas de dos clics.
    bool m_toolFirstPointSet = false;
    QGeoCoordinate m_toolFirstPoint;
    QCPItemLine *m_measureLine = nullptr;

    // --- Trazado y edicion de entidades ---------------------------------
    QString m_activeLayer = QStringLiteral("default");
    QString m_draftType;
    FeatureStyle m_draftStyle;

    MapFeature m_draft;
    bool m_drafting = false;

    //! Vertice que se esta arrastrando, o -1.
    int m_editVertex = -1;
    //! Se esta moviendo la entidad entera.
    bool m_movingFeature = false;
    QGeoCoordinate m_lastEditPos;
    QCPItemEllipse *m_measureStart = nullptr;
    QCPItemRect *m_areaRect = nullptr;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_MAPVIEW_H_
