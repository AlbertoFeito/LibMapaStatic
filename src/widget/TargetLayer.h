#ifndef LIBMAPA_WIDGET_TARGETLAYER_H_
#define LIBMAPA_WIDGET_TARGETLAYER_H_

#include "qcustomplot.h"

#include "widget/TargetModel.h"

#include <QGeoCoordinate>
#include <QTimer>
#include <functional>

namespace libmapa {

/*!
 * \brief Dibuja TODOS los objetivos moviles y sus trazas en una sola capa.
 *
 * Es la capa DINAMICA que la Fase 6 dejaba prevista: va en su propia QCPLayer
 * en modo BUFFERED, asi que actualizar cientos de objetivos repinta solo esta
 * capa y no obliga a rehacer las teselas ni las entidades estaticas.
 *
 * A diferencia de FeatureLayer, NO cachea a un pixmap: su contenido cambia en
 * cada actualizacion. En cambio agrupa los avisos del modelo con un temporizador
 * para no repintar mas de lo util aunque lleguen decenas de posiciones por
 * segundo.
 */
class TargetLayer : public QCPLayerable
{
    Q_OBJECT

public:
    TargetLayer(QCustomPlot *parent, TargetModel *model);
    ~TargetLayer() override;

    //! Conversion de coordenada geografica a unidades de eje (la pone MapView).
    void setAxisMapper(std::function<QPointF(const QGeoCoordinate &)> toAxis);

    //! Tamano del simbolo del objetivo, en pixeles.
    void setSymbolSizePx(double px) { m_symbolPx = px; }

    //! Objetivos dibujados en el ultimo repintado (los que caian en pantalla).
    int lastDrawnCount() const { return m_lastDrawn; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private slots:
    void programarRepintado();

private:
    QPointF screenPos(const QGeoCoordinate &c) const;
    void drawTarget(QPainter *painter, const TargetModel::Entry &e,
                    const QRect &area) const;

    TargetModel *m_model = nullptr;
    std::function<QPointF(const QGeoCoordinate &)> m_toAxis;

    double m_symbolPx = 7.0;
    QTimer m_repintar;               //!< Agrupa avisos: como mucho ~30 fps.
    mutable int m_lastDrawn = 0;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_TARGETLAYER_H_
