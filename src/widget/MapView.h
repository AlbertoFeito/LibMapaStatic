#ifndef LIBMAPA_WIDGET_MAPVIEW_H_
#define LIBMAPA_WIDGET_MAPVIEW_H_

#include "qcustomplot.h"

#include "libmapa/MapTypes.h"
#include "tiles/TileService.h"
#include "widget/CoverageLayer.h"
#include "widget/GraticuleLayer.h"
#include "widget/ScaleBarLayer.h"
#include "widget/HillshadeLayer.h"
#include "widget/LabelLayer.h"
#include "widget/FeatureLayer.h"
#include "widget/OverlayModel.h"
#include "widget/RangeLayer.h"
#include "widget/TargetLayer.h"
#include "widget/TargetModel.h"
#include "widget/TileLayer.h"

#include <QElapsedTimer>
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
    TargetModel *targetModel() const { return m_targetModel; }
    TargetLayer *targetLayer() const { return m_targetLayer; }
    CoverageLayer *coverageLayer() const { return m_coverageLayer; }
    GraticuleLayer *graticuleLayer() const { return m_graticuleLayer; }
    ScaleBarLayer *scaleBarLayer() const { return m_scaleBarLayer; }
    RangeLayer *rangeLayer() const { return m_rangeLayer; }
    HillshadeLayer *hillshadeLayer() const { return m_hillshadeLayer; }

    /*!
     * \brief Capa de teselas OVERLAY (mapa hibrido): un segundo juego de teselas,
     *        con fondo transparente, encima de la base y del relieve y debajo de
     *        las entidades. Su servicio es independiente del de la base: cambiar
     *        de base no lo toca. null = sin overlay.
     */
    void setOverlayService(TileService *service);
    TileService *overlayService() const { return m_overlayService; }
    TileLayer *overlayTileLayer() const { return m_overlayLayer; }

    QGeoCoordinate center() const;
    void setCenter(const QGeoCoordinate &center);

    int zoom() const { return m_zoom; }
    void setZoom(int zoom, const QPointF *anchorPx = nullptr);

    //! Tope de zoom por encima del recomendado del dataset (sobre-zoom). -1 (por
    //! defecto) = usar recommendedMaxZoom. Lo usan herramientas como fill_map para
    //! poder navegar/enmarcar niveles altos (z15/16) que se van a descargar.
    void setMaxZoomOverride(int zoom) { m_maxZoomOverride = zoom; }
    int maxZoomOverride() const { return m_maxZoomOverride; }

    void fitBounds(const QGeoCoordinate &northWest,
                   const QGeoCoordinate &southEast);

    QGeoCoordinate visibleNorthWest() const;
    QGeoCoordinate visibleSouthEast() const;

    QGeoCoordinate coordinateAt(const QPoint &pixel) const;

    //! Objetivo movil mas cercano al pixel dado dentro de \a tolPx pixeles, o -1
    //! si ninguno. Trabaja en pantalla (el simbolo tiene tamano en pixeles).
    qint64 targetAt(const QPoint &pixel, double tolPx = 14.0) const;

    //! Objetivo resaltado (-1 = ninguno). Lo dibuja TargetLayer con un halo y le
    //! fuerza la etiqueta aunque el nivel de detalle las haya apagado.
    void setSelectedTarget(qint64 id);
    qint64 selectedTarget() const { return m_selectedTarget; }

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

    //! Tope de vertices del trazado punto a punto (polilinea/poligono). −1 = sin
    //! limite. Al llegar al tope se ignoran los clics siguientes; el límite se
    //! guarda en la entidad (atributo \c max_vertices) para que la edicion
    //! posterior tambien lo respete. Sirve para un «area objetivo» de ≤10 puntos.
    void setDraftMaxVertices(int n) { m_draftMaxVertices = n; }
    int draftMaxVertices() const { return m_draftMaxVertices; }

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

    //! Oculta la marca de SelectArea / SelectPolygon, si la hay.
    void clearAreaSelection();

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
    /*!
     * \brief Cancela lo que la herramienta activa tenga a medias, sin cambiar de
     *        herramienta: el primer punto de Medir, el recuadro de area o la
     *        captura de un punto (que vuelve a Navegar y emite pickCancelled).
     *
     * Devuelve false si no habia nada. Lo llaman Esc y el clic derecho.
     */
    bool cancelOperation();
    //! Cierra el trazado en curso y crea la entidad. -1 si no era valida.
    qint64 finishDrawing();
    bool isDrawing() const { return m_drafting; }

signals:
    void zoomChanged(int zoom);
    void featureCreated(qint64 id);
    void featureClicked(qint64 id, const QGeoCoordinate &position);
    void targetClicked(qint64 id, const QGeoCoordinate &position);
    //! Clic sobre un grupo de objetivos (TargetClustering): sus ids.
    void targetClusterClicked(const QVector<qint64> &ids, const QGeoCoordinate &position);
    //! Arrastre (herramienta Navegar) de una entidad marcada \c draggable: al
    //! empezar, en cada movimiento (con la coordenada bajo el cursor) y al soltar.
    void featureDragStarted(qint64 id);
    void featureDragMovedTo(qint64 id, const QGeoCoordinate &position);
    //! Se solto el rotulo de un objetivo tras moverlo: offset final en pixeles.
    void targetLabelMoved(qint64 id, const QPointF &offsetPx);
    void featureDragFinished(qint64 id);
    void drawingCancelled();
    void centerChanged(const QGeoCoordinate &center);
    void mouseMovedTo(const QGeoCoordinate &position);
    void mapClicked(const QGeoCoordinate &position, Qt::MouseButton button);
    //! Clic derecho fuera de un trazado (o la tecla de menu): la app monta su menu
    //! contextual. No se emite si el clic derecho cancela algo a medias.
    void contextMenuRequested(const QPoint &pixel, const QPoint &globalPos);
    //! Clic izquierdo SIN arrastrar sobre un elemento (objetivo, entidad o marca)
    //! con la herramienta Navegar: la app puede mostrar una tarjeta con sus datos.
    void elementClicked(const QPoint &pixel, const QPoint &globalPos);
    //! La captura de un punto (herramienta PickPoint) se cancelo con Esc o con el
    //! clic derecho; la herramienta ya vuelve a Navegar.
    void pickCancelled();
    //! Una accion se rechazo (capa bloqueada, vertices minimos...): el texto
    //! explica por que, para mostrarlo al usuario.
    void interactionBlocked(const QString &reason);
    void measurementFinished(const libmapa::Measurement &measurement);
    void areaSelected(const QGeoCoordinate &northWest,
                      const QGeoCoordinate &southEast);
    //! Poligono cerrado con la herramienta SelectPolygon (vertices en orden).
    void polygonSelected(const QVector<QGeoCoordinate> &polygon);
    //! Poligono cerrado con la herramienta MeasureArea, con su medida.
    void areaMeasured(const libmapa::AreaMeasurement &measurement);
    void pointPicked(const QGeoCoordinate &position);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    //! Atiende un clic de la herramienta de medir. Se llama AL PULSAR.
    void handleMeasureClick(const QGeoCoordinate &donde);
    //! Desplaza la vista (pixeles; positivo = derecha/abajo), en el eje proyectado.
    void panByPixels(int dx, int dy);
    //! ¿Se puede editar la capa? (una que no existe aun cuenta como editable).
    bool capaEditable(const QString &layerId) const;
    //! Cursor segun lo que haya bajo el puntero (sin botones pulsados).
    void actualizarCursorHover(const QPoint &pos);

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
    TileService *m_overlayService = nullptr;
    TileLayer *m_overlayLayer = nullptr;
    QMetaObject::Connection m_overlayReady;
    OverlayModel *m_model = nullptr;
    FeatureLayer *m_featureLayer = nullptr;
    TargetModel *m_targetModel = nullptr;
    TargetLayer *m_targetLayer = nullptr;
    CoverageLayer *m_coverageLayer = nullptr;
    GraticuleLayer *m_graticuleLayer = nullptr;
    ScaleBarLayer *m_scaleBarLayer = nullptr;
    RangeLayer *m_rangeLayer = nullptr;       //!< Anillos y marcaciones (capa "targets").
    LabelLayer *m_labelLayer = nullptr;       //!< Rotulos, encima de todo (capa "targets").
    HillshadeLayer *m_hillshadeLayer = nullptr;

    int m_zoom = 10;
    int m_maxZoomOverride = -1;     //!< tope de sobre-zoom (-1 = recommendedMaxZoom)
    QGeoCoordinate m_center{23.1136, -82.3666};
    qint64 m_selectedTarget = -1;    //!< Objetivo resaltado (-1 = ninguno).

    MapTool m_tool = MapTool::None;
    bool m_dragging = false;            //!< pulsado para mover el mapa (aun puede ser un clic)
    Qt::MouseButton m_panButton = Qt::NoButton;
    bool m_dragMoved = false;           //!< el arrastre del mapa supero el umbral
    QPoint m_dragStartPx;
    QGeoCoordinate m_dragStartGeo;

    //! Umbral (pixeles) para que un pulsado deje de ser un clic y sea un arrastre.
    static constexpr int kClickSlopPx = 4;
    //! Lo que habia bajo el puntero al pulsar con Navegar y se decide al soltar.
    enum class Pend { Nada, Objetivo, Grupo, Entidad, Marca };
    Pend m_pend = Pend::Nada;
    qint64 m_pendId = -1;
    QVector<qint64> m_pendGrupo;
    bool m_pendDbl = false;             //!< el pulsado vino de un doble clic
    bool m_dblPress = false;            //!< (transitorio) mouseDoubleClickEvent -> mousePressEvent
    bool m_dragFeatureStarted = false;  //!< el arrastre de la entidad draggable ya empezo
    QPoint m_dragFeaturePx;
    QPoint m_editPressPx;
    bool m_editStarted = false;         //!< la edicion (vertice/entidad) ya empezo
    int m_hoverVertex = -1;             //!< tirador del vertice bajo el puntero (Editar)
    int m_wheelAccum = 0;               //!< giro de rueda acumulado (120 = un nivel)
    QElapsedTimer m_wheelClock;
    QElapsedTimer m_hoverClock;
    Qt::CursorShape m_hoverShape = Qt::ArrowCursor;
    bool m_hoverSet = false;

    // Arrastre de la ETIQUETA (entidad u objetivo) con la herramienta None: se
    // recuerda de quien es el rotulo, el pixel de inicio y su offset inicial; el
    // nuevo offset = inicial + (cursor - inicio). -1 = no se arrastra ninguno.
    qint64 m_dragLabelFeature = -1;
    qint64 m_dragLabelTarget = -1;
    qint64 m_dragLabelBearing = -1;      //!< Marcacion cuyo rotulo se arrastra.
    QPoint m_dragLabelStartPx;
    QPointF m_dragLabelStartOffset;
    //! Estado PROPIO del offset del objetivo al empezar el arrastre, para
    //! restaurarlo exacto si se cancela (soltar fuera del mapa).
    QPointF m_dragLabelStartRaw;
    bool m_dragLabelStartSet = false;

    //! Entidad (punto) marcada draggable que se arrastra con la herramienta None.
    qint64 m_dragFeature = -1;

    int m_draftMaxVertices = -1;     //!< Tope de vertices del trazado (−1 = sin).

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
    //! El borrador actual es un POLIGONO DE SELECCION (SelectPolygon), no una
    //! entidad: al cerrarlo se emite polygonSelected y no se crea nada.
    bool m_draftIsSelection = false;
    //! ...y ademas es una MEDIDA (MeasureArea): al cerrarlo se emite
    //! areaMeasured, y mientras se traza se rotula la superficie en vivo.
    bool m_selectionIsMeasure = false;
    //! Recalcula el rotulo de la medida con los vertices puestos mas, si es
    //! valido, el punto bajo el cursor (el lado que se esta por anadir).
    void updateAreaLabel(const QGeoCoordinate &cursor);

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
