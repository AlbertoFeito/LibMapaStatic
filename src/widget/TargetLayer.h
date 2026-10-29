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

    //! Objetivo resaltado (-1 = ninguno): se le dibuja un halo y se le fuerza la
    //! etiqueta aunque el nivel de detalle las haya apagado.
    void setSelected(qint64 id);

    //! Si el objetivo seleccionado dibuja su HISTORIAL COMPLETO desde el origen
    //! (donde se vio por primera vez, marcado con un cuadro y su hora). Activo
    //! por defecto.
    void setSelectedHistoryVisible(bool on);

    //! Una alerta de proximidad que dibujar: los dos objetivos, donde estara
    //! cada uno en el CPA y el rotulo ("CPA 0.30 NM en 4 min").
    struct ProximityLine {
        qint64 a = -1;
        qint64 b = -1;
        QGeoCoordinate cpaA;
        QGeoCoordinate cpaB;
        QString label;
    };
    //! Lineas de maximo acercamiento de los pares en alerta (vacio = ninguna).
    void setProximityLines(const QVector<ProximityLine> &lines);
    void setProximityColor(const QColor &c) { m_proximityColor = c; }

    /*!
     * \brief Objetivos que NO pasan el filtro de la app.
     *
     * Con \a dim false no se dibujan (ni se pueden pinchar: ver
     * isFilteredOut); con \a dim true se dibujan tenues, sin rotulo ni traza,
     * para que lo que si pasa destaque sin perder el contexto.
     */
    void setFilteredOut(const QSet<qint64> &ids, bool dim);
    //! Agrupacion de objetivos cercanos (ver TargetClustering).
    void setClustering(const TargetClustering &c);
    TargetClustering clustering() const { return m_clustering; }
    //! Objetivos del grupo dibujado bajo \a pixel en el ultimo repintado (vacio
    //! si no hay grupo ahi).
    QVector<qint64> clusterAt(const QPoint &pixel) const;
    //! Grupos dibujados en el ultimo repintado.
    int lastClusterCount() const { return int(m_clusterHits.size()); }
    //! ¿Esta el objetivo dentro de un grupo en el ultimo repintado? (entonces
    //! no se dibuja suelto ni se puede pinchar suelto).
    bool isClustered(qint64 id) const { return m_clustered.contains(id); }

    //! ¿Esta oculto por el filtro? (atenuado no cuenta: se ve y se puede pinchar).
    bool isFilteredOut(qint64 id) const { return !m_filterDim && m_filteredOut.contains(id); }
    bool isSelectedHistoryVisible() const { return m_historyVisible; }

    //! Nivel de detalle para escalar a MILES de objetivos: si en un repintado
    //! hay mas objetivos visibles que \a maxLabels no se dibuja ninguna etiqueta
    //! (a esa densidad se solaparian en una mancha ilegible); igual con las
    //! trazas y \a maxTrails. El simbolo se dibuja siempre. Valores <= 0
    //! desactivan ese elemento. Por defecto 150 etiquetas y 400 trazas.
    void setDetailBudget(int maxLabels, int maxTrails);

    //! Objetivo cuya CAJA DE ETIQUETA dibujada cae bajo el pixel, o -1 (la de
    //! encima gana). Solo considera etiquetas realmente pintadas en el ultimo
    //! frame (respeta nivel de detalle y declutter).
    qint64 labelAt(const QPoint &pixel) const;
    //! Caja (px) del rotulo de un objetivo en el ultimo frame, o nula si no se
    //! dibujo (oculto, sin texto, fuera de pantalla o descartado por detalle).
    QRectF labelRectOf(qint64 id) const;

    //! Aspecto comun de los rotulos (colocacion, fuente, columnas). Por defecto
    //! el de siempre.
    void setLabelFormat(const TargetLabelFormat &format);
    TargetLabelFormat labelFormat() const { return m_labelFormat; }
    //! Color del texto del rotulo del objetivo seleccionado cuando el segmento no
    //! trae color propio. Por defecto negro (como el resto del texto).
    void setLabelSelectionColor(const QColor &color);
    QColor labelSelectionColor() const { return m_labelSelectionColor; }

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
                    bool selected, QSet<qint64> &labelCells) const;
    // Historial completo del objetivo seleccionado y marca de su origen.
    void drawHistory(QPainter *painter, const TargetModel::Entry &e) const;
    void drawProximity(QPainter *painter) const;
    void drawCluster(QPainter *painter, const QPointF &center, int count) const;

    //! Lineas del rotulo de un objetivo: su \a labelContent, o su \a label plano
    //! partido por '\n' (un solo segmento por linea, sin color propio).
    static QVector<TargetLabelLine> labelLinesOf(const MapTarget &t);

    TargetModel *m_model = nullptr;
    TargetLabelFormat m_labelFormat;
    QColor m_labelSelectionColor = Qt::black;
    std::function<QPointF(const QGeoCoordinate &)> m_toAxis;
    TargetSymbolProvider m_symbolProvider;   //!< Lo pone la app; vacio = galon.

    double m_symbolPx = 7.0;
    int m_labelBudget = 150;         //!< Tope de etiquetas visibles (nivel detalle).
    int m_trailBudget = 400;         //!< Tope de trazas visibles (nivel detalle).
    qint64 m_selected = -1;          //!< Objetivo resaltado (-1 = ninguno).
    bool m_historyVisible = true;    //!< El seleccionado muestra su historial.
    QVector<ProximityLine> m_proximity;              //!< Pares en alerta CPA.
    QColor m_proximityColor = QColor(0xff, 0x3d, 0x00);
    QSet<qint64> m_filteredOut;                      //!< No pasan el filtro.
    TargetClustering m_clustering;
    //! Grupos del ultimo repintado, para el hit-test: centro, radio y miembros.
    struct ClusterHit {
        QPointF center;
        double radius = 0.0;
        QVector<qint64> ids;
    };
    QVector<ClusterHit> m_clusterHits;
    QSet<qint64> m_clustered;                        //!< Miembros de algun grupo.
    bool m_filterDim = false;                        //!< Atenuar en vez de ocultar.
    QTimer m_repintar;               //!< Agrupa avisos: como mucho ~30 fps.
    mutable int m_lastDrawn = 0;
    mutable int m_lastLabels = 0;
    mutable int m_lastTrails = 0;
    // Buffers reusados entre repintados para no reservar memoria cada frame.
    mutable QVector<std::pair<const TargetModel::Entry *, QPointF>> m_visibles;
    mutable QSet<qint64> m_labelCells;
    //! Cajas de etiqueta dibujadas en el ultimo frame (id -> rect en pixeles),
    //! para que labelAt() acierte justo donde se ve el rotulo.
    mutable QVector<std::pair<qint64, QRectF>> m_labelBoxes;
};

} // namespace libmapa

#endif // LIBMAPA_WIDGET_TARGETLAYER_H_
