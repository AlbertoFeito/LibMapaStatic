#include "widget/LabelLayer.h"

#include "widget/FeatureLayer.h"
#include "widget/RangeLayer.h"

namespace libmapa {

// Crea la capa de rotulos. Las capas de las que pinta las gestiona MapView.
LabelLayer::LabelLayer(QCustomPlot *parent, FeatureLayer *features, RangeLayer *ranges)
    : QCPLayerable(parent)
    , m_features(features)
    , m_ranges(ranges)
{
}

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa.
void LabelLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Pinta primero los rotulos de las entidades y despues los de anillos y
// marcaciones, que quedan encima (son los que mas cambian con los objetivos).
void LabelLayer::draw(QCPPainter *painter)
{
    if (m_features)
        m_features->drawLabels(painter);
    if (m_ranges)
        m_ranges->drawLabels(painter);
}

} // namespace libmapa
