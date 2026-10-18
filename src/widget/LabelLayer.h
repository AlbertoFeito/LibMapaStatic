#ifndef LIBMAPA_WIDGET_LABELLAYER_H_
#define LIBMAPA_WIDGET_LABELLAYER_H_

#include "qcustomplot.h"

namespace libmapa {

class FeatureLayer;
class RangeLayer;

/*!
 * \brief Capa de ROTULOS: pinta encima de todo el texto que acompana al mapa.
 *
 * Las entidades, los anillos y las marcaciones se dibujan cada uno en su capa,
 * y los objetivos moviles encima de ellos; si cada uno pintara tambien su
 * texto, una linea de marcacion o un anillo taparian el nombre de una zona. Por
 * eso los rotulos de entidades (\ref FeatureLayer::drawLabels) y los de anillos
 * y marcaciones (\ref RangeLayer::drawLabels) se pintan aqui, en la MISMA capa
 * buffered que los objetivos pero creada la ultima, de modo que queda por encima
 * de todo y se repinta con ellos.
 *
 * No guarda estado: solo dibuja lo que le piden las otras capas.
 */
class LabelLayer : public QCPLayerable
{
    Q_OBJECT

public:
    LabelLayer(QCustomPlot *parent, FeatureLayer *features, RangeLayer *ranges);

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    FeatureLayer *m_features = nullptr;
    RangeLayer *m_ranges = nullptr;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_LABELLAYER_H_
