#ifndef LIBMAPA_WIDGET_TARGETLAYER_H_
#define LIBMAPA_WIDGET_TARGETLAYER_H_

#include "qcustomplot.h"

#include "libmapa/TargetSymbol.h"
#include "widget/TargetModel.h"

#include <QGeoCoordinate>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <functional>
#include <utility>

namespace libmapa {

/*!
 * \brief Dibuja TODOS los objetivos moviles y sus trazas en una sola capa.
 *
 * Es la capa DINAMICA que la Fase 6 dejaba prevista: va en su propia QCPLayer
 * en modo BUFFERED, asi que actualizar cientos de objetivos repinta solo esta
 * capa y no obliga a rehacer las teselas ni las entidades estaticas.
 *
 * A diferencia de FeatureLayer, NO cachea a un pixmap: su contenido cambia en
 * cada actualizacion. En cambio agrupa los avisos del modelo con un temporizador
 * para no repintar mas de lo util aunque lleguen decenas de posiciones por
 * segundo.
 */
class TargetLayer : public QCPLayerable
{
    Q_OBJECT

public:
    TargetLayer(QCustomPlot *parent, TargetModel *model);
    ~TargetLayer() override;

    //! Conversion de coordenada geografica a unidades de eje (la pone MapView).
    void setAxisMapper(std::function<QPointF(const QGeoCoordinate &)> toAxis);

    //! Tamano del simbolo del objetivo, en pixeles.
    void setSymbolSizePx(double px) { m_symbolPx = px; }

    //! Registra como dibuja la APP el simbolo de cada objetivo (icono por tipo o
    //! estado + rotacion por rumbo). Sin proveedor, se usa el galon por defecto.
    void setSymbolProvider(TargetSymbolProvider provider);

    //! Nivel de detalle para escalar a MILES de objetivos: si en un repintado
    //! hay mas objetivos visibles que \a maxLabels no se dibuja ninguna etiqueta
    //! (a esa densidad se solaparian en una mancha ilegible); igual con las
    //! trazas y \a maxTrails. El simbolo se dibuja siempre. Valores <= 0
    //! desactivan ese elemento. Por defecto 150 etiquetas y 400 trazas.
    void setDetailBudget(int maxLabels, int maxTrails);

    //! Objetivos dibujados en el ultimo repintado (los que caian en pantalla).
    int lastDrawnCount() const { return m_lastDrawn; }
    //! Etiquetas y trazas realmente dibujadas en el ultimo repintado (tras el
    //! nivel de detalle y el declutter). Util para tests y diagnostico.
    int lastLabelsDrawn() const { return m_lastLabels; }
    int lastTrailsDrawn() const { return m_lastTrails; }

protected:
    void applyDefaultAntialiasingHint(QCPPainter *painter) const override;
    void draw(QCPPainter *painter) override;

private slots:
    void programarRepintado();

private:
    QPointF screenPos(const QGeoCoordinate &c) const;
    // Dibuja un objetivo ya situado en 'pos' (en pantalla). drawTrail/drawLabel
    // los decide draw() segun el nivel de detalle; labelCells acumula las celdas
    // ya ocupadas por una etiqueta para no solaparlas (declutter).
    void drawTarget(QPainter *painter, const TargetModel::Entry &e,
                    const QPointF &pos, bool drawTrail, bool drawLabel,
                    QSet<qint64> &labelCells) const;

    TargetModel *m_model = nullptr;
    std::function<QPointF(const QGeoCoordinate &)> m_toAxis;
    TargetSymbolProvider m_symbolProvider;   //!< Lo pone la app; vacio = galon.

    double m_symbolPx = 7.0;
    int m_labelBudget = 150;         //!< Tope de etiquetas visibles (nivel detalle).
    int m_trailBudget = 400;         //!< Tope de trazas visibles (nivel detalle).
    QTimer m_repintar;               //!< Agrupa avisos: como mucho ~30 fps.
    mutable int m_lastDrawn = 0;
    mutable int m_lastLabels = 0;
    mutable int m_lastTrails = 0;
    // Buffers reusados entre repintados para no reservar memoria cada frame.
    mutable QVector<std::pair<const TargetModel::Entry *, QPointF>> m_visibles;
    mutable QSet<qint64> m_labelCells;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_TARGETLAYER_H_
