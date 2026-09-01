#ifndef LIBMAPA_WIDGET_TILELAYER_H_
#define LIBMAPA_WIDGET_TILELAYER_H_

#include "qcustomplot.h"

#include "tiles/TilePlanner.h"
#include "tiles/TileService.h"

#include <QColor>

namespace libmapa {

/*!
 * \brief Dibuja TODAS las teselas del mapa en una sola capa de QCustomPlot.
 *
 * Es un QCPLayerable, no una coleccion de QCPItemPixmap. La diferencia con el
 * codigo original es de fondo, no de estilo:
 *
 *   CMapaPlot::Cargar_Imagenes hacia, en cada movimiento del mapa:
 *
 *       foreach (QCPItemPixmap* pix, ListIMG) removeItem(pix);
 *       ListIMG.clear();
 *       ... y volvia a crear un QCPItemPixmap por cada tesela visible
 *
 *   Con 30 a 60 teselas en pantalla eso son decenas de QObject creados y
 *   destruidos por frame, cada uno con su registro en el QCustomPlot, sus
 *   conexiones y su recalculo de disposicion. Ademas se volvia a llamar a
 *   QPixmap::loadFromData sobre cada una (falla F-15).
 *
 * Aqui hay UN solo objeto para toda la vida del widget. En cada repintado
 * recorre el plan que le da TilePlanner y hace un drawImage por tesela, con
 * su rectangulo de origen. No se crea ni se destruye nada.
 *
 * El respaldo de tesela padre sale gratis: TileDrawItem ya trae el
 * rectangulo de origen dentro de la imagen del ancestro, que es justo el
 * tercer argumento de QPainter::drawImage.
 */
class TileLayer : public QCPLayerable
{
    Q_OBJECT

public:
    TileLayer(QCustomPlot *parent, TileService *service);
    ~TileLayer() override;

    //! Plan de dibujo vigente. Lo recalcula MapView cuando cambia la vista.
    void setPlan(const TilePlanner::Plan &plan);
    const TilePlanner::Plan &plan() const { return m_plan; }

    //! Color para las celdas sin ninguna imagen disponible.
    void setNoDataColor(const QColor &color) { m_noDataColor = color; }
    QColor noDataColor() const { return m_noDataColor; }

    //! Rejilla de depuracion sobre cada tesela, con su z/x/y.
    void setDebugGridVisible(bool on) { m_debugGrid = on; }
    bool debugGridVisible() const { return m_debugGrid; }

    //! Imagen que se usaria para una tesela, de la cache de la capa ACTIVA.
    //! Expuesto para poder verificar que cada capa tiene la suya.
    QImage imageFor(const TileKey &key) const;

    //! Teselas realmente dibujadas en el ultimo repintado.
    int lastDrawnCount() const { return m_lastDrawn; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private:
    //! Rectangulo en pixeles del widget que ocupa una tesela.
    QRectF screenRectFor(const TileKey &key) const;

    TileService *m_service = nullptr;
    TilePlanner::Plan m_plan;
    QColor m_noDataColor;
    bool m_debugGrid = false;
    int m_lastDrawn = 0;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_TILELAYER_H_
