#ifndef LIBMAPA_TILES_TILELOADER_H_
#define LIBMAPA_TILES_TILELOADER_H_

#include "tiles/RMapsTileSource.h"
#include "tiles/TileDataset.h"
#include "tiles/TileKey.h"
#include "geo/TileMatrix.h"

#include <QAtomicInteger>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QVector>
#include <memory>

namespace libmapa {

//! Una tesela ya leida y decodificada, lista para la interfaz.
struct LoadedTile
{
    TileKey key;
    QImage image;
};

/*!
 * \brief Peticion de carga: una o varias rejillas, en orden de servicio.
 *
 * Se sirven EN ORDEN, y cada una emite su propia senal en cuanto esta lista.
 * Eso permite la carga progresiva: primero un nivel grueso, que llega casi
 * instantaneo y ya da cobertura completa por respaldo de tesela padre, y
 * despues el nivel de detalle, que lo va sustituyendo.
 *
 * Sin esto, al abrir la capa satelital sobre una zona nueva la pantalla se
 * quedaba al 30 %: el planificador solo puede recurrir a ancestros que ya
 * esten en cache, y en arranque en frio no hay ninguno.
 */
struct TileRequest
{
    QString datasetId;
    QVector<TileMatrix::TileRange> ranges;
    quint64 id = 0;
};

/*!
 * \brief Lee y decodifica teselas en un hilo aparte.
 *
 * Corrige la falla F-17: CMapaPlot::Cargar_Imagenes se ejecutaba en el hilo
 * de la interfaz, llamada desde wheelEvent y desde slot_actualiza_rangos. Con
 * ficheros de 1.4 y 3.4 GiB, cada movimiento del raton bloqueaba la ventana.
 *
 * Dos decisiones importantes:
 *
 * 1. Se decodifica AQUI, en el hilo trabajador, y se entrega QImage. QPixmap
 *    solo puede construirse en el hilo de la GUI, asi que decodificar alli
 *    devolveria el problema al sitio del que se queria sacar. La conversion
 *    QImage -> QPixmap se hace al montar el item y es mucho mas barata que
 *    descomprimir el PNG.
 *
 * 2. Cada peticion lleva un identificador creciente. Si al empezar a servirla
 *    ya hay otra mas reciente, se descarta. Sin esto, arrastrar el mapa
 *    encolaria decenas de peticiones y las teselas irian apareciendo con
 *    varios segundos de retraso, correspondientes a posiciones que el usuario
 *    ya abandono.
 *
 * El objeto se construye en el hilo de la GUI y se mueve al hilo trabajador
 * con moveToThread. Las conexiones SQLite se abren perezosamente DENTRO del
 * hilo trabajador, porque SQLite no permite compartirlas entre hilos.
 */
class TileLoader : public QObject
{
    Q_OBJECT

public:
    explicit TileLoader(QObject *parent = nullptr);
    ~TileLoader() override;

    /*!
     * \brief Marca como obsoleta cualquier peticion anterior a \a requestId.
     *
     * Se llama desde el hilo de la GUI; por eso el contador es atomico.
     */
    void invalidateBefore(quint64 requestId);

public slots:
    //! Define los datasets disponibles. No abre nada todavia.
    void configure(const QVector<libmapa::TileDataset> &datasets);

    //! Sirve las rejillas de la peticion, en orden, emitiendo una senal por
    //! cada una. Abandona en cuanto la peticion queda obsoleta.
    void load(const libmapa::TileRequest &request);

    //! Cierra las conexiones de este hilo. Llamar antes de parar el hilo.
    void releaseResources();

signals:
    //! Se emite una vez por rejilla servida. \a range permite al receptor
    //! saber que teselas NO vinieron, y anotarlas como inexistentes.
    void tilesLoaded(quint64 requestId, const QString &datasetId,
                     const libmapa::TileMatrix::TileRange &range,
                     const QVector<libmapa::LoadedTile> &tiles);
    void requestSkipped(quint64 requestId);
    void loadFailed(quint64 requestId, const QString &message);

private:
    RMapsTileSource *sourceFor(const QString &datasetId);
    bool isStale(quint64 requestId) const;

    QHash<QString, TileDataset> m_datasets;
    // shared_ptr y no unique_ptr: QHash exige que el valor sea copiable
    // para poder desasociarse (implicit sharing).
    QHash<QString, std::shared_ptr<RMapsTileSource>> m_sources;
    QAtomicInteger<quint64> m_newestRequest{0};
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::LoadedTile)
Q_DECLARE_METATYPE(libmapa::TileRequest)
Q_DECLARE_METATYPE(libmapa::TileMatrix::TileRange)
Q_DECLARE_METATYPE(QVector<libmapa::LoadedTile>)
Q_DECLARE_METATYPE(libmapa::TileDataset)
Q_DECLARE_METATYPE(QVector<libmapa::TileDataset>)

#endif // LIBMAPA_TILES_TILELOADER_H_
