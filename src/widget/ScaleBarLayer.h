#ifndef LIBMAPA_WIDGET_SCALEBARLAYER_H_
#define LIBMAPA_WIDGET_SCALEBARLAYER_H_

#include "libmapa/MapTypes.h"
#include "qcustomplot.h"

namespace libmapa {

/*!
 * \brief Escala grafica fija en la esquina inferior izquierda.
 *
 * Una barra de longitud redonda (1-2-5 x 10^n, ver MapDecor::scaleBar) en
 * metros/kilometros o en millas nauticas. La resolucion se mide en el CENTRO
 * de la vista con la distancia geodesica real entre dos pixeles: en Mercator la
 * escala cambia con la latitud, y el centro es donde mira el operador.
 *
 * Encima de todas las capas: es un elemento de la pantalla, no del mapa.
 * Oculta por defecto (MapWidget::setScaleBarVisible).
 */
class ScaleBarLayer : public QCPLayerable
{
    Q_OBJECT

public:
    explicit ScaleBarLayer(QCustomPlot *parent);
    ~ScaleBarLayer() override;

    void setUnits(ScaleUnits units) { m_units = units; }
    ScaleUnits units() const { return m_units; }

    //! Metros por pixel en el centro de la vista (0 si no se puede medir).
    //! Publico para poder comprobarlo en los tests.
    double metersPerPixelAtCenter() const;

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    ScaleUnits m_units = ScaleUnits::Metric;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_SCALEBARLAYER_H_
