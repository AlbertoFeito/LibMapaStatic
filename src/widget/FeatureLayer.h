#ifndef LIBMAPA_WIDGET_FEATURELAYER_H_
#define LIBMAPA_WIDGET_FEATURELAYER_H_

#include "qcustomplot.h"

#include "libmapa/MapFeature.h"
#include "libmapa/MapTypes.h"
#include "widget/OverlayModel.h"

#include <QPixmap>
#include <QSet>
#include <functional>

namespace libmapa {

/*!
 * \brief Dibuja TODAS las entidades del modelo en una sola capa.
 *
 * Igual que TileLayer: un unico QCPLayerable, no un QCPItemEllipse mas un
 * QCPItemText por entidad. CMapaPlot creaba y destruia esos objetos en cada
 * repintado; con quinientas entidades son mil quinientos QObject por frame.
 *
 * ESTA CAPA ES PARA LO ESTATICO: puntos, poligonos y areas de interes, que
 * cambian solo cuando alguien los edita. Por eso el resultado se guarda en un
 * pixmap y se reutiliza mientras nada cambie. Los objetivos en movimiento
 * llegan a decenas por segundo e iran en su propia capa: si compartieran
 * esta, cada actualizacion de un buque obligaria a redibujar todas las zonas.
 */
class FeatureLayer : public QCPLayerable
{
    Q_OBJECT

public:
    FeatureLayer(QCustomPlot *parent, OverlayModel *model);
    ~FeatureLayer() override;

    //! Conversion de coordenada geografica a unidades de eje. La pone MapView,
    //! que es quien sabe que el eje Y va en grados de Mercator.
    void setAxisMapper(std::function<QPointF(const QGeoCoordinate &)> toAxis);

    //! Invalida el pixmap cacheado. Lo llama el modelo al cambiar algo.
    void markDirty();

    /*!
     * \brief Geometria en curso de trazado, dibujada por encima de todo.
     *
     * No entra en el modelo hasta que se cierra: mientras se traza no es una
     * entidad todavia, y meterla en el modelo obligaria a limpiarla si el
     * usuario cancela. Se pasa nullptr para dejar de dibujarla.
     */
    void setDraft(const MapFeature *draft);
    //! Rotulo del borrador (la medida en vivo de MeasureArea): junto al cursor
    //! mientras se traza, o en el centro del poligono ya cerrado. Vacio = nada.
    void setDraftLabel(const QString &text);

    //! Punto que sigue al raton mientras se traza, en pixeles. Se dibuja como
    //! el lado que se esta a punto de anadir.
    void setDraftCursor(const QPoint &pixel, bool visible);

    //! Entidad bajo un punto de la pantalla, o -1. La mas cercana gana.
    //! Con \a incluirNoSeleccionables = false (lo que usa el raton para elegir,
    //! arrastrar y poner el cursor) se saltan las capas con \c LayerInfo::selectable
    //! falso (las fijas del paquete); con true se miran todas: es la consulta «que
    //! hay debajo», para un tooltip o una lectura de cota, sin elegir nada.
    //! Con \a subIndex distinto de null recibe la PARTE de la entidad que cae bajo el
    //! pixel si la entidad es multi-parte (-1 si no lo es o no hay entidad).
    qint64 featureAt(const QPoint &pixel, double tolerancePx = 8.0,
                     bool incluirNoSeleccionables = false, int *subIndex = nullptr) const;

    //! Entidad cuya CAJA DE ETIQUETA cae bajo el pixel, o -1 (la de encima gana).
    //! Permite seleccionar/arrastrar el rotulo por separado de la geometria.
    qint64 labelAt(const QPoint &pixel) const;
    //! Caja (px) del rotulo de una entidad como se ve ahora; nula si no tiene.
    QRectF labelRectOf(qint64 id) const;
    //! Caja (px) del rotulo PROPIO de la parte \a part de una entidad multi-parte
    //! (su \a PartInfo::name); nula si no tiene.
    QRectF partLabelRectOf(qint64 id, int part) const;
    //! Fuente del rotulo: la del mapa, con el tamano propio si el estilo lo fija.
    static QFont labelFontFor(const MapFeature &f, const QFont &base);
    //! Pinta las etiquetas de todas las entidades visibles sobre \a painter, en
    //! coordenadas de pantalla. Las llama LabelLayer (encima de los objetivos);
    //! la propia capa ya no las pinta.
    void drawLabels(QPainter *painter) const;

    /*!
     * \brief Vertice bajo un punto de la pantalla.
     * \return indice del vertice, o -1.
     */
    int vertexAt(qint64 featureId, const QPoint &pixel,
                 double tolerancePx = 8.0) const;

    /*!
     * \brief Lado mas cercano a un punto de la pantalla.
     * \return indice del vertice donde EMPIEZA el lado, o -1.
     *
     * Sirve para insertar un vertice en el sitio correcto: al hacer doble
     * clic sobre un lado, el vertice nuevo va entre sus dos extremos, no al
     * final de la lista.
     */
    int segmentAt(const MapFeature &feature, const QPoint &pixel,
                  double tolerancePx = 10.0) const;

    /*!
     * \brief Entidades "en alerta": se dibujan con un resalte del color de
     *        alerta por debajo (aro alrededor de un punto, borde ancho y velo en
     *        un poligono). Lo usa MapWidget para las alertas de zona.
     */
    void setAlerted(const QSet<qint64> &ids);
    void setAlertColor(const QColor &color);
    QColor alertColor() const { return m_alertColor; }

    //! Que lineas marcan su inicio y su fin (ver MapWidget::setRouteEndpoints).
    void setRouteEndpoints(RouteEndpoints mode);
    RouteEndpoints routeEndpoints() const { return m_endpoints; }

    //! Entidades dibujadas en el ultimo repintado.
    int lastDrawnCount() const { return m_lastDrawn; }
    //! Veces que se reutilizo el pixmap en vez de volver a dibujar.
    int cacheHits() const { return m_cacheHits; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    void drawFeature(QPainter *painter, const MapFeature &f, bool selected) const;
    void drawAlert(QPainter *painter, const MapFeature &f) const;
    void drawEndpoints(QPainter *painter, const MapFeature &f, bool selected) const;
    //! Dibuja UNA parte (un punto de ella) de una entidad Point.
    void drawPointPart(QPainter *painter, const MapFeature &f, int partIndex,
                       const QVector<QGeoCoordinate> &part, bool selected) const;
    //! Dibuja UNA parte de una entidad Polyline/Polygon.
    void drawPathPart(QPainter *painter, const MapFeature &f, int partIndex,
                      const QVector<QGeoCoordinate> &part, bool selected) const;
    void drawVertices(QPainter *painter, const MapFeature &f) const;
    void drawDraft(QPainter *painter) const;
    void drawLabel(QPainter *painter, const MapFeature &f,
                   const QPointF &anchor) const;
    //! Punto (en pixeles) del que CUELGA la etiqueta: el vertice de anclaje
    //! (style.labelAnchorVertex), o el punto, o el centro de la geometria.
    QPointF labelAnchorScreen(const MapFeature &f) const;
    //! Caja (en pixeles) de la etiqueta: anclaje + hueco por defecto + offset,
    //! con el tamano del texto (multilinea por '\n'). La comparten dibujo y
    //! hit-test para que coincidan exactamente.
    QRectF labelBoxFor(const MapFeature &f, const QPointF &base,
                       const QFontMetricsF &fm) const;
    //! Caja de un rotulo de texto colgado de \a base: lo comun al rotulo de la entidad y
    //! al de cada una de sus partes.
    static QRectF labelBoxForText(const QString &texto, LabelAnchor ancla, bool junar,
                                  double radioPx, const QPointF &offset,
                                  const QPointF &base, const QFontMetricsF &fm);
    //! Pinta los rotulos propios de las partes con nombre de una entidad multi-parte.
    void drawPartLabels(QPainter *painter, const MapFeature &f) const;
    //! Caja del rotulo de la parte \a part: su texto, colgado de su punto (el punto, o
    //! el centro de la caja de la parte) por el lado que diga su PartInfo o el estilo.
    QRectF partLabelBox(const MapFeature &f, int part, const QFontMetricsF &fm,
                        QPointF *anchor = nullptr) const;
    //! Punto (px) del que cuelga el rotulo de la parte \a part.
    QPointF partAnchorScreen(const MapFeature &f, int part) const;

    QPointF screenPos(const QGeoCoordinate &c) const;
    QPolygonF screenPolygon(const MapFeature &f) const;
    QPolygonF screenPolygonOf(const QVector<QGeoCoordinate> &pts) const;

    //! ¿La entidad cae (toca) el rectangulo visible, en coordenadas de EJE?
    //! Recorte barato (sin proyectar a pixeles): salta las fuera de pantalla.
    bool featureInView(const MapFeature &f, const QRectF &axisRect) const;
    //! Rectangulo visible actual en coordenadas de eje, con un margen relativo.
    QRectF visibleAxisRect(double marginFrac = 0.08) const;

    //! Distancia de un punto a un segmento, en pixeles.
    static double distanceToSegment(const QPointF &p, const QPointF &a,
                                    const QPointF &b);

    OverlayModel *m_model = nullptr;
    std::function<QPointF(const QGeoCoordinate &)> m_toAxis;

    // Cache del dibujo. Se invalida al cambiar el modelo o la vista.
    mutable QPixmap m_cache;
    mutable bool m_dirty = true;
    mutable QRect m_cacheViewport;
    mutable QCPRange m_cacheX, m_cacheY;
    mutable int m_lastDrawn = 0;
    mutable int m_cacheHits = 0;

    // Alertas de zona: ids a resaltar y su color.
    QSet<qint64> m_alerted;
    QColor m_alertColor = QColor(0xff, 0x17, 0x44);
    RouteEndpoints m_endpoints = RouteEndpoints::Routes;

    // El trazo en curso NO va al pixmap cacheado: cambia con cada movimiento
    // del raton y obligaria a redibujar todas las entidades cada vez.
    MapFeature m_draft;
    bool m_hasDraft = false;
    QPoint m_draftCursor;
    bool m_draftCursorVisible = false;
    QString m_draftLabel;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_FEATURELAYER_H_
