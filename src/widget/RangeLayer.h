#ifndef LIBMAPA_WIDGET_RANGELAYER_H_
#define LIBMAPA_WIDGET_RANGELAYER_H_

#include "libmapa/MapTypes.h"
#include "qcustomplot.h"
#include "widget/TargetModel.h"

#include <QGeoCoordinate>
#include <QMap>
#include <functional>

namespace libmapa {

/*!
 * \brief Anillos de distancia (VRM) y lineas de marcacion (EBL).
 *
 * Herramientas de medida que se quedan en el mapa. Cada una puede colgar de un
 * objetivo movil (centro de los anillos, origen o destino de la marcacion) y
 * lo sigue: por eso vive en la misma capa BUFFERED que TargetLayer y, al moverse
 * los objetivos, se repinta con ellos sin rehacer teselas ni entidades. Se
 * dibuja antes que los objetivos, que quedan encima.
 *
 * Los anillos son geodesicos (MapDecor::geodesicCircle) y los rotulos usan las
 * unidades de la escala grafica.
 */
class RangeLayer : public QCPLayerable
{
    Q_OBJECT

public:
    RangeLayer(QCustomPlot *parent, TargetModel *targets);
    ~RangeLayer() override;

    //! Coordenada geografica -> coordenada de eje (Mercator), la de MapView.
    void setAxisMapper(std::function<QPointF(const QGeoCoordinate &)> toAxis);
    void setUnits(ScaleUnits units) { m_units = units; }
    //! Posicion de una entidad punto por su id (invalida si no existe), para
    //! los anillos con RangeRings::featureId. La pone MapView.
    void setFeatureResolver(std::function<QGeoCoordinate(qint64)> resolver);
    //! Una entidad ha cambiado: si hay anillos en ella, se repintan.
    void featureChanged(qint64 id);

    qint64 addRings(const RangeRings &rings);
    bool updateRings(qint64 id, const RangeRings &rings);
    qint64 addBearing(const BearingLine &line);
    bool updateBearing(qint64 id, const BearingLine &line);
    bool remove(qint64 id);
    void clear();
    QVector<qint64> ids() const;
    //! Copia de unos anillos / una marcacion por id. false si no es de ese tipo.
    bool rings(qint64 id, RangeRings *out) const;
    bool bearing(qint64 id, BearingLine *out) const;
    /*!
     * \brief Separacion con la que se dibujan ahora unos anillos (m).
     *
     * La fija, o la automatica para el zoom actual si \c spacingMeters es 0.
     * 0 si no existen o no se pueden resolver (sin vista, objetivo perdido).
     */
    double spacing(qint64 id) const;
    /*!
     * \brief Anillos o marcacion bajo el pixel (a menos de \a tolPx de un
     *        anillo, de su centro o de la linea), -1 si nada.
     *
     * Las marcaciones, que se dibujan encima, ganan a los anillos; entre
     * iguales, la ultima anadida.
     */
    qint64 markAt(const QPointF &pixel, double tolPx = 6.0) const;
    /*!
     * \brief Marcacion cuyo ROTULO cae bajo el pixel (el ultimo pintado gana), o -1.
     *
     * Usa las cajas de la ultima pasada de dibujo, para acertar justo donde se ve
     * el rotulo.
     */
    qint64 bearingLabelAt(const QPointF &pixel) const;
    //! Pixel donde esta el raton, para destacar el rotulo de anillo o de radial que tiene debajo
    //! (los demas son translucidos). Repinta solo si cambia el rotulo destacado. (-1,-1) = ninguno.
    void setHoverPixel(const QPointF &pixel);
    //! Desplaza el rotulo de una marcacion (pixeles, relativo a su centro). false
    //! si no existe esa marcacion.
    bool setBearingLabelOffset(qint64 id, const QPointF &offsetPx);
    /*!
     * \brief Pinta los rotulos acumulados por la ultima pasada de \ref draw.
     *
     * Rotulos de anillos (distancia, nombre) y de marcaciones, con la linea guia
     * de los desplazados. Lo llama LabelLayer, por encima de objetivos y entidades.
     */
    void drawLabels(QCPPainter *painter) const;
    bool isEmpty() const { return m_rings.isEmpty() && m_bearings.isEmpty(); }

    /*!
     * \brief Extremos de una marcacion ya resueltos (objetivos incluidos).
     *
     * false si no existe, o si su origen o su destino no se pueden resolver
     * ahora (un objetivo que ya no esta).
     */
    bool bearingEnds(qint64 id, QGeoCoordinate *from, QGeoCoordinate *to) const;

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    QPointF screenPos(const QGeoCoordinate &c) const;
    QGeoCoordinate resolve(const QGeoCoordinate &fixed, qint64 targetId) const;
    QGeoCoordinate centro(const RangeRings &r) const;
    QGeoCoordinate extremo(const QGeoCoordinate &fijo, qint64 targetId, qint64 featureId) const;
    bool ends(const BearingLine &l, QGeoCoordinate *from, QGeoCoordinate *to) const;
    static bool degenerada(const BearingLine &l);
    double pasoEfectivo(const RangeRings &r, const QGeoCoordinate &c) const;
    QRectF vista() const;
    QPointF dentroDeVista(const QPointF &ideal, const QPolygonF &curva) const;
    QPointF bordeDeVista(const QPointF &desde, const QPointF &hasta) const;
    void drawRings(QCPPainter *painter, const RangeRings &r) const;
    void drawBearing(QCPPainter *painter, qint64 id, const BearingLine &l) const;
    //! Anota un rotulo para pintarlo despues, encima de todo (drawLabels).
    void label(const QPointF &anchor, const QString &txt, const QColor &color,
               const QPointF &offset = QPointF(), qint64 bearingId = -1,
               double opacity = 1.0) const;
    void repintar();

    TargetModel *m_targets = nullptr;
    std::function<QPointF(const QGeoCoordinate &)> m_toAxis;
    std::function<QGeoCoordinate(qint64)> m_featurePos;
    ScaleUnits m_units = ScaleUnits::Metric;
    qint64 m_nextId = 1;
    QMap<qint64, RangeRings> m_rings;        //!< Ordenados: dibujo estable.
    QMap<qint64, BearingLine> m_bearings;

    //! Rotulo pendiente de pintar: lo anota draw() y lo pinta drawLabels().
    struct PendingLabel {
        QPointF anchor;       //!< Punto al que cuelga (centro de la caja si no hay offset).
        QString text;
        QColor color;
        QPointF offset;       //!< Desplazamiento en pixeles respecto a \a anchor.
        qint64 bearingId;     //!< Marcacion a la que pertenece, o -1 (anillos).
        double opacity = 1.0; //!< < 1 = rotulo suave (anillo o radial): translucido y destaca bajo el raton.
    };
    mutable QVector<PendingLabel> m_pending;
    //! Cajas de los rotulos suaves de la ultima pasada y la que tiene el raton encima.
    mutable QVector<QRectF> m_softBoxes;
    QRectF m_hoverBox;
    QPointF m_hoverPx = QPointF(-1.0, -1.0);
    //! Cajas de los rotulos de marcacion de la ultima pasada, para bearingLabelAt.
    mutable QVector<std::pair<qint64, QRectF>> m_bearingLabelBoxes;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_RANGELAYER_H_
