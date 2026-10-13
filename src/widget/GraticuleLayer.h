#ifndef LIBMAPA_WIDGET_GRATICULELAYER_H_
#define LIBMAPA_WIDGET_GRATICULELAYER_H_

#include "qcustomplot.h"

#include <QColor>

namespace libmapa {

/*!
 * \brief Cuadricula de coordenadas: meridianos y paralelos con su rotulo.
 *
 * Elige sola la separacion segun lo que se ve (MapDecor::graticuleStep), en
 * pasos de carta (grados, 30', 10', 1', 30"...), y rotula cada linea en el
 * borde: las longitudes arriba y las latitudes a la izquierda, con la precision
 * que pide el paso (23°30'N). Los paralelos se dibujan en la misma proyeccion
 * que las teselas (eje Y en grados de Mercator), asi que caen donde deben.
 *
 * Va por encima de las entidades y por debajo de los objetivos: orienta sin
 * tapar lo que se mueve. Oculta por defecto (MapWidget::setGraticuleVisible).
 */
class GraticuleLayer : public QCPLayerable
{
    Q_OBJECT

public:
    explicit GraticuleLayer(QCustomPlot *parent);
    ~GraticuleLayer() override;

    //! Color de las lineas y del texto de los rotulos.
    void setColor(const QColor &color) { m_color = color; }
    QColor color() const { return m_color; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    QColor m_color = QColor(20, 40, 60, 150);
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_GRATICULELAYER_H_
