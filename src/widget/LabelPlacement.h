#ifndef LIBMAPA_LABELPLACEMENT_H_
#define LIBMAPA_LABELPLACEMENT_H_

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtMath>

#include <cmath>

#include "libmapa/MapFeature.h"

namespace libmapa {

/*!
 * \brief Esquina de la caja de un rotulo a la que se une la linea guia.
 *
 * Funciones puras, sin dibujar nada: asi se prueban con numeros sin crear un
 * mapa. Las usan FeatureLayer (rotulos de entidades) y TargetLayer (formulario
 * de los objetivos).
 */
enum class LabelCorner {
    BottomLeft,
    TopLeft,
    TopRight,
    BottomRight
};

// Esquina de la caja mas cercana al objetivo, segun el desplazamiento del rotulo
// en pixeles de PANTALLA (X a la derecha, Y hacia abajo). Se usa el azimut del
// desplazamiento medido desde el norte (arriba) en sentido horario: un rotulo
// arriba-izquierda (azimut 315) se une por su esquina inferior derecha, que es la
// que queda mas cerca del punto. En el norte exacto (0) y con desplazamiento nulo
// se elige la inferior derecha (desempate).
inline LabelCorner labelCornerFor(const QPointF &offsetPx)
{
    if (offsetPx.isNull())
        return LabelCorner::BottomRight;
    double azimut = qRadiansToDegrees(std::atan2(offsetPx.x(), -offsetPx.y()));
    if (azimut < 0.0)
        azimut += 360.0;

    if (azimut == 0.0)   return LabelCorner::BottomRight;
    if (azimut <= 90.0)  return LabelCorner::BottomLeft;
    if (azimut <= 180.0) return LabelCorner::TopLeft;
    if (azimut <= 270.0) return LabelCorner::TopRight;
    return LabelCorner::BottomRight;
}

// Punto de una caja al que se une la linea guia segun la esquina elegida.
inline QPointF labelCornerPoint(LabelCorner corner, const QRectF &box)
{
    switch (corner) {
    case LabelCorner::BottomLeft:  return box.bottomLeft();
    case LabelCorner::TopLeft:     return box.topLeft();
    case LabelCorner::TopRight:    return box.topRight();
    case LabelCorner::BottomRight: return box.bottomRight();
    }
    return box.center();
}

// Esquina superior izquierda de la caja de un rotulo respecto a su anclaje, para
// las posiciones predefinidas de LabelAnchor (todas menos Auto, que conserva el
// calculo clasico de quien llama). 'gap' es la separacion entre el anclaje y la
// caja. El desplazamiento libre del estilo se suma despues, fuera de aqui.
inline QPointF labelBoxOrigin(LabelAnchor anchor, const QSizeF &box, double gap)
{
    const double w = box.width();
    const double h = box.height();
    switch (anchor) {
    case LabelAnchor::Top:         return QPointF(-w / 2.0, -h - gap);
    case LabelAnchor::Bottom:      return QPointF(-w / 2.0, gap);
    case LabelAnchor::Left:        return QPointF(-w - gap, -h / 2.0);
    case LabelAnchor::Right:       return QPointF(gap, -h / 2.0);
    case LabelAnchor::TopLeft:     return QPointF(-w - gap, -h - gap);
    case LabelAnchor::TopRight:    return QPointF(gap, -h - gap);
    case LabelAnchor::BottomLeft:  return QPointF(-w - gap, gap);
    case LabelAnchor::BottomRight: return QPointF(gap, gap);
    case LabelAnchor::Center:      return QPointF(-w / 2.0, -h / 2.0);
    case LabelAnchor::Auto:        break;
    }
    return QPointF(0.0, 0.0);
}

} // namespace libmapa

#endif // LIBMAPA_LABELPLACEMENT_H_
