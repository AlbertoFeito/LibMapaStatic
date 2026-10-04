#ifndef LIBMAPA_TARGETSYMBOL_H_
#define LIBMAPA_TARGETSYMBOL_H_

#include "libmapa/MapTarget.h"

#include <QPixmap>
#include <QPointF>
#include <functional>

namespace libmapa {

/*!
 * \brief Como dibujar el simbolo de un objetivo: lo decide la APLICACION.
 *
 * La libreria es agnostica del dominio (no sabe de barcos, aeronaves ni UAVs),
 * asi que el JUEGO de iconos lo trae la app. Para cada objetivo visible la app
 * devuelve este TargetSymbol; la libreria lo coloca sobre la posicion, lo gira
 * segun el rumbo si se pide y lo escala. Un icono nulo => la libreria usa su
 * simbolo por defecto (un galon), de modo que activar un proveedor que solo
 * decora algunos tipos deja el resto con el simbolo generico.
 */
struct TargetSymbol
{
    //! Icono a dibujar. Si es nulo, la libreria usa su simbolo por defecto.
    QPixmap icon;

    //! Si true, la libreria gira el icono segun MapTarget::headingDeg (0 = norte,
    //! horario). Para iconos sin orientacion (un circulo, un cuadrado) ponlo a
    //! false y se dibuja derecho.
    bool rotateWithHeading = true;

    //! Factor sobre el tamano natural del icono (1.0 = tal cual).
    double scale = 1.0;

    //! Punto del icono, en fraccion 0..1, que cae sobre la posicion del objetivo.
    //! (0.5, 0.5) centra el icono; (0.5, 1.0) ancla su base (util para un "pin").
    QPointF anchor = QPointF(0.5, 0.5);
};

/*!
 * \brief Funcion que la app registra para elegir el simbolo de cada objetivo.
 *
 * Recibe el objetivo COMPLETO (incluidos kind y attributes), asi que la app
 * puede elegir icono por tipo, por estado o por cualquier dato que ella misma
 * haya colgado. La libreria la llama al dibujar, una vez por objetivo visible:
 * debe ser rapida y, si cachea iconos, mejor.
 */
using TargetSymbolProvider = std::function<TargetSymbol(const MapTarget &)>;

} // namespace libmapa

#endif // LIBMAPA_TARGETSYMBOL_H_
