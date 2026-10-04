#include "widget/TileLayer.h"

#include "core/Logging.h"
#include "geo/TileMatrix.h"

namespace libmapa {

TileLayer::TileLayer(QCustomPlot *parent, TileService *service)
    : QCPLayerable(parent)
    , m_service(service)
    // Azul mar por defecto. Las BD de este proyecto no guardan teselas de mar
    // por encima de los niveles gruesos, asi que al mirar oceano abierto puede
    // no haber ninguna imagen: el color de fondo es parte del mapa, no un
    // hueco. El codigo original dejaba ver el gris del widget.
    , m_noDataColor(QColor(0x9d, 0xd3, 0xdf))
{
    setAntialiased(false);   // las teselas son mapas de bits alineados
}

TileLayer::~TileLayer() = default;

// Guarda el plan de dibujo (que teselas pintar y con que respaldo) que preparo
// TilePlanner. No repinta: solo actualiza el estado; QCustomPlot llamara a draw().
void TileLayer::setPlan(const TilePlanner::Plan &plan)
{
    m_plan = plan;
}

// Imagen de una tesela concreta desde la cache del servicio (o nula si no esta).
QImage TileLayer::imageFor(const TileKey &key) const
{
    return m_service ? m_service->cache().take(key) : QImage();
}

// Gancho de QCustomPlot: aplica el hint de antialiasing de esta capa al pintor.
void TileLayer::applyDefaultAntialiasingHint(QCPPainter *painter) const
{
    applyAntialiasingHint(painter, mAntialiased, QCP::aeAll);
}

// Rectangulo EN PIXELES de pantalla que ocupa una tesela. Se calcula en
// coordenadas de EJE (x = longitud, y = ordenada de Mercator en grados), que son
// lineales en el indice de tesela, para que el cuadro salga exacto y sin
// deformar; pasar por la latitud encogeria/estiraria las teselas segun su fila.
QRectF TileLayer::screenRectFor(const TileKey &key) const
{
    QCustomPlot *plot = parentPlot();
    if (!plot)
        return {};

    // Los bordes de la tesela se calculan directamente en coordenadas de
    // EJE, que son las de la proyeccion: x = longitud, y = ordenada de
    // Mercator en grados. Ambas son lineales en el indice de tesela, asi que
    // el rectangulo sale exacto y del mismo tamano en toda la pantalla.
    //
    // Pasar por la latitud, como se hacia antes, deformaba el mapa: a zoom 3
    // la tesela superior abarca 5.88 grados de latitud y la central 40.98,
    // pero las dos miden 256 pixeles.
    const double lonWest = TileMatrix::tileXToLongitude(key.x, key.z);
    const double lonEast = TileMatrix::tileXToLongitude(key.x + 1, key.z);
    const double mercTop = TileMatrix::tileYToMercatorDegrees(key.y, key.z);
    const double mercBot = TileMatrix::tileYToMercatorDegrees(key.y + 1, key.z);

    const double left   = plot->xAxis->coordToPixel(lonWest);
    const double right  = plot->xAxis->coordToPixel(lonEast);
    const double top    = plot->yAxis->coordToPixel(mercTop);
    const double bottom = plot->yAxis->coordToPixel(mercBot);

    return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
}

// Pinta la capa de teselas. Primero rellena todo con el color "sin datos" (azul
// mar, que sobre oceano es el resultado correcto y no un hueco), y luego dibuja
// cada item del plan de gruesa a fina, de modo que las teselas exactas tapen a
// sus respaldos ampliados sin parpadeo. Reescala el rectangulo de origen si la
// imagen no mide lo que dice el descriptor, y opcionalmente dibuja la rejilla de
// depuracion. Guarda en m_lastDrawn cuantas teselas se pintaron.
void TileLayer::draw(QCPPainter *painter)
{
    m_lastDrawn = 0;
    if (!m_service || !parentPlot())
        return;

    // viewport() y no axisRect()->rect(): este ultimo solo tiene tamano tras
    // el primer recalculo de disposicion de QCustomPlot.
    const QRect clip = parentPlot()->viewport();
    painter->save();
    painter->setClipRect(clip);

    // Fondo primero: donde no llegue ninguna tesela quedara este color, que
    // sobre mar abierto es exactamente lo que se quiere ver.
    painter->fillRect(clip, m_noDataColor);

    // Las teselas se pintan escaladas sin suavizado entre pixeles vecinos
    // (SmoothPixmapTransform) porque un ancestro muy ampliado se ve mejor
    // interpolado que en bloques.
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    TileCache &cache = m_service->cache();

    // Los items vienen ordenados por zoom de origen ascendente, asi que los
    // ancestros muy ampliados quedan debajo y las teselas exactas encima.
    // Cuando llega una tesela nueva tapa a su sucedaneo sin parpadeo.
    for (const TileDrawItem &item : m_plan.items) {
        const QImage image = cache.take(item.source);
        if (image.isNull())
            continue;

        const QRectF dest = screenRectFor(item.slot);
        if (!dest.intersects(QRectF(clip)))
            continue;

        // sourceRect esta en pixeles de la imagen del origen. Para una tesela
        // exacta es la imagen entera; para un ancestro, el trozo que le toca.
        QRectF src = item.sourceRect;
        if (image.width() != m_service->activeDataset()->tileSize) {
            // La imagen no mide lo que dice el descriptor: se reescala el
            // rectangulo de origen en vez de dibujar mal.
            const double f = image.width()
                             / double(m_service->activeDataset()->tileSize);
            src = QRectF(src.x() * f, src.y() * f,
                         src.width() * f, src.height() * f);
        }

        painter->drawImage(dest, image, src);
        ++m_lastDrawn;

        if (m_debugGrid) {
            painter->setPen(QPen(item.isExact() ? Qt::darkGreen : Qt::red, 1));
            painter->drawRect(dest);
            painter->drawText(dest.adjusted(4, 4, -4, -4),
                              Qt::AlignLeft | Qt::AlignTop,
                              QStringLiteral("%1/%2/%3%4")
                                  .arg(item.slot.z)
                                  .arg(item.slot.x)
                                  .arg(item.slot.y)
                                  .arg(item.isExact()
                                           ? QString()
                                           : QStringLiteral(" <-%1")
                                                 .arg(item.ancestorDepth)));
        }
    }

    painter->restore();
}

} // namespace libmapa
