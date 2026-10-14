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

    qint64 addRings(const RangeRings &rings);
    bool updateRings(qint64 id, const RangeRings &rings);
    qint64 addBearing(const BearingLine &line);
    bool updateBearing(qint64 id, const BearingLine &line);
    bool remove(qint64 id);
    void clear();
    QVector<qint64> ids() const;
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
    bool ends(const BearingLine &l, QGeoCoordinate *from, QGeoCoordinate *to) const;
    void drawRings(QCPPainter *painter, const RangeRings &r) const;
    void drawBearing(QCPPainter *painter, const BearingLine &l) const;
    void label(QCPPainter *painter, const QPointF &center, const QString &txt,
               const QColor &color) const;
    void repintar();

    TargetModel *m_targets = nullptr;
    std::function<QPointF(const QGeoCoordinate &)> m_toAxis;
    ScaleUnits m_units = ScaleUnits::Metric;
    qint64 m_nextId = 1;
    QMap<qint64, RangeRings> m_rings;        //!< Ordenados: dibujo estable.
    QMap<qint64, BearingLine> m_bearings;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_RANGELAYER_H_
