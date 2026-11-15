#ifndef LIBMAPA_WIDGET_LABELPAINT_H_
#define LIBMAPA_WIDGET_LABELPAINT_H_

#include "libmapa/MapTypes.h"

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace libmapa {

/*!
 * \brief Pintado comun de los rotulos del mapa: caja, borde, halo y texto.
 *
 * Todas las capas que rotulan (entidades, anillos y marcaciones, cuadricula,
 * alertas de proximidad) pasan por aqui, para que el fondo, el borde y el halo
 * se configuren igual en todas y no haya un blanco fijo repartido por el codigo.
 */
namespace LabelPaint {

//! Color efectivo: \a propio si es valido y, si no, \a porDefecto. Es la regla de
//! LabelBoxStyle («invalido = como siempre»).
inline QColor elegir(const QColor &propio, const QColor &porDefecto)
{
    return propio.isValid() ? propio : porDefecto;
}

// Pinta la caja de un rotulo: fondo redondeado y, si se pide, borde. Un fondo
// totalmente transparente y sin borde no pinta nada (rotulo «sin caja»).
inline void caja(QPainter *painter, const QRectF &rect, const QColor &fondo,
                 const QColor &borde, double grosorBorde, double radio)
{
    const bool hayBorde = borde.isValid() && borde.alpha() > 0 && grosorBorde > 0.0;
    const bool hayFondo = fondo.isValid() && fondo.alpha() > 0;
    if (!hayBorde && !hayFondo)
        return;
    painter->setPen(hayBorde ? QPen(borde, grosorBorde) : QPen(Qt::NoPen));
    painter->setBrush(hayFondo ? QBrush(fondo) : QBrush(Qt::NoBrush));
    if (radio > 0.0)
        painter->drawRoundedRect(rect, radio, radio);
    else
        painter->drawRect(rect);
}

// Pinta el texto CENTRADO en la caja (una o varias lineas), con un halo alrededor
// si \a halo es valido. El halo es el contorno del texto trazado con \a grosorHalo
// pixeles a cada lado (un QPainterPath, no varias pasadas desplazadas: asi el
// grosor es el que se pide). El texto se pinta encima con drawText, como siempre.
inline void texto(QPainter *painter, const QRectF &rect, const QString &txt,
                  const QColor &color, const QColor &halo, double grosorHalo)
{
    if (halo.isValid() && halo.alpha() > 0 && grosorHalo > 0.0 && !txt.isEmpty()) {
        const QFontMetricsF fm(painter->font());
        const QStringList lineas = txt.split(QLatin1Char('\n'));
        const double alto = fm.height() * double(lineas.size());
        double y = rect.center().y() - alto / 2.0 + fm.ascent();
        QPainterPath contorno;
        for (const QString &l : lineas) {
            const double x = rect.center().x() - fm.horizontalAdvance(l) / 2.0;
            contorno.addText(QPointF(x, y), painter->font(), l);
            y += fm.height();
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen pen(halo, grosorHalo * 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(contorno);
        painter->restore();
    }
    painter->setPen(QPen(color));
    painter->drawText(rect, Qt::AlignCenter, txt);
}

} // namespace LabelPaint
} // namespace libmapa

#endif // LIBMAPA_WIDGET_LABELPAINT_H_
