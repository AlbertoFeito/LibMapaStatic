#ifndef LIBMAPA_WIDGET_HILLSHADELAYER_H_
#define LIBMAPA_WIDGET_HILLSHADELAYER_H_

#include "qcustomplot.h"

#include <QImage>
#include <QPainter>

namespace libmapa {

/*!
 * \brief Capa que dibuja un RELIEVE SOMBREADO (hillshade) sobre el mapa.
 *
 * Es una capa "tonta": solo guarda una imagen ya calculada (por \c MapWidget a
 * partir del DEM local, 100% sin conexion) y las coordenadas geograficas de sus
 * esquinas, y la pinta estirada entre los pixeles correspondientes. Como el eje X
 * es lineal en longitud y el Y en "grados de Mercator", y la imagen se calcula en
 * esas mismas unidades, el estirado lineal queda ALINEADO con las teselas y se
 * reproyecta solo al desplazar o hacer zoom (hasta que \c MapWidget la recalcula a
 * la nueva vista).
 */
class HillshadeLayer : public QCPLayerable
{
    Q_OBJECT

public:
    explicit HillshadeLayer(QCustomPlot *parent);
    ~HillshadeLayer() override;

    //! Fija la imagen y las esquinas geograficas (NO/SE) que cubre. \a mode es
    //! el modo de composicion (Multiply para el sombreado gris en gris sobre la
    //! base; SourceOver para el tintado por altura). \a opacity en [0,1].
    void setImage(const QImage &img, double lonWest, double lonEast,
                  double latNorth, double latSouth,
                  QPainter::CompositionMode mode, double opacity);
    //! Olvida la imagen (deja la capa en blanco).
    void clear();

    bool hasImage() const { return !m_img.isNull(); }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    QImage m_img;
    double m_lonW = 0.0, m_lonE = 0.0, m_latN = 0.0, m_latS = 0.0;
    double m_opacity = 0.6;
    QPainter::CompositionMode m_mode = QPainter::CompositionMode_Multiply;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_HILLSHADELAYER_H_
