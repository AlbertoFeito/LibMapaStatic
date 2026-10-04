#ifndef LIBMAPA_WIDGET_COVERAGELAYER_H_
#define LIBMAPA_WIDGET_COVERAGELAYER_H_

#include "qcustomplot.h"

#include <QColor>
#include <QVector>

namespace libmapa {

/*!
 * \brief Capa que dibuja una "mancha de cobertura" traslucida sobre el mapa.
 *
 * A diferencia de la rejilla de depuracion de TileLayer -que solo marca lo que
 * cae bajo la vista al zoom actual-, esta capa muestra, de forma FIJA, que
 * zonas de un zoom OBJETIVO ya estan en la base de datos, agregadas a una
 * rejilla mas gruesa (zoom RESUMEN) para que se vean aunque se este mirando a un
 * zoom mucho menor.
 *
 * Cada celda se colorea por COMPLETITUD: ambar = pocas teselas presentes de las
 * que caben, verde = llena. El dato lo calcula MapWidget con
 * RMapsTileSource::coverageHistogram; aqui solo se pinta. Como las celdas se
 * proyectan con las mismas formulas que las teselas (TileMatrix), la mancha se
 * reproyecta sola al desplazar o hacer zoom, sin recalcular nada.
 */
class CoverageLayer : public QCPLayerable
{
    Q_OBJECT

public:
    //! Celda resumen ya en coordenadas LOGICAS (sx, sy) del zoom resumen, con la
    //! fraccion [0,1] de teselas del zoom objetivo presentes.
    struct Cell { int sx = 0; int sy = 0; float frac = 0.0f; };

    explicit CoverageLayer(QCustomPlot *parent);
    ~CoverageLayer() override;

    //! Fija los datos a pintar: zoom objetivo (para la leyenda), zoom resumen
    //! (al que estan los indices de las celdas) y las celdas con su fraccion.
    void setData(int targetZoom, int summaryZoom, const QVector<Cell> &cells);
    //! Olvida los datos (deja la capa en blanco).
    void clearData();

    int targetZoom() const { return m_zt; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    //! Rectangulo en pixeles de una celda resumen (misma proyeccion que las
    //! teselas, pero al zoom resumen).
    QRectF screenRectFor(int sx, int sy, int z) const;

    int m_zt = -1;
    int m_zs = -1;
    QVector<Cell> m_cells;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_COVERAGELAYER_H_
