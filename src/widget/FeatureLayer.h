#ifndef LIBMAPA_WIDGET_FEATURELAYER_H_
#define LIBMAPA_WIDGET_FEATURELAYER_H_

#include "qcustomplot.h"

#include "libmapa/MapFeature.h"
#include "widget/OverlayModel.h"

#include <QPixmap>
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

    //! Punto que sigue al raton mientras se traza, en pixeles. Se dibuja como
    //! el lado que se esta a punto de anadir.
    void setDraftCursor(const QPoint &pixel, bool visible);

    //! Entidad bajo un punto de la pantalla, o -1. La mas cercana gana.
    qint64 featureAt(const QPoint &pixel, double tolerancePx = 8.0) const;

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

    //! Entidades dibujadas en el ultimo repintado.
    int lastDrawnCount() const { return m_lastDrawn; }
    //! Veces que se reutilizo el pixmap en vez de volver a dibujar.
    int cacheHits() const { return m_cacheHits; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    void drawFeature(QPainter *painter, const MapFeature &f, bool selected) const;
    void drawPoint(QPainter *painter, const MapFeature &f, bool selected) const;
    void drawPath(QPainter *painter, const MapFeature &f, bool selected) const;
    void drawVertices(QPainter *painter, const MapFeature &f) const;
    void drawDraft(QPainter *painter) const;
    void drawLabel(QPainter *painter, const MapFeature &f,
                   const QPointF &anchor) const;

    QPointF screenPos(const QGeoCoordinate &c) const;
    QPolygonF screenPolygon(const MapFeature &f) const;

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

    // El trazo en curso NO va al pixmap cacheado: cambia con cada movimiento
    // del raton y obligaria a redibujar todas las entidades cada vez.
    MapFeature m_draft;
    bool m_hasDraft = false;
    QPoint m_draftCursor;
    bool m_draftCursorVisible = false;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_FEATURELAYER_H_
