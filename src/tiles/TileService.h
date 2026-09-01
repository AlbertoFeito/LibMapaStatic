#ifndef LIBMAPA_TILES_TILESERVICE_H_
#define LIBMAPA_TILES_TILESERVICE_H_

#include "tiles/TileCache.h"
#include "tiles/TileDataset.h"
#include "tiles/TileLoader.h"
#include "tiles/TilePlanner.h"
#include "geo/TileMatrix.h"

#include <QGeoCoordinate>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <map>
#include <memory>
#include <map>
#include <memory>

namespace libmapa {

/*!
 * \brief Punto unico de entrada al motor de teselas, para el hilo de la GUI.
 *
 * Reune el hilo trabajador, la cache y el planificador. Quien lo usa nunca ve
 * SQL, ni hilos, ni el mapeo de zoom de cada BD.
 *
 * Alternar OSM <-> Satelital es setActiveDataset(): no hay que reconstruir
 * nada. En el codigo original ese cambio pasaba por Cargar_Base_Datos_Map con
 * el tipo cruzado a mano y por dos QSqlDatabase que compartian el mismo
 * connectionName, de ahi su inestabilidad.
 */
class TileService : public QObject
{
    Q_OBJECT

public:
    explicit TileService(QObject *parent = nullptr);
    ~TileService() override;

    //! Registra los datasets y arranca el hilo. Devuelve false si no hay ninguno valido.
    /*!
     * \brief Registra los datasets y arranca el hilo.
     * \param cacheMiB memoria maxima para teselas decodificadas.
     *
     * Se mide en MiB y no en numero de teselas porque el coste en RAM no
     * depende de lo que pese la tesela en disco: una de 256x256 en RGB32
     * ocupa 256 KiB decodificada, tanto si su PNG media 1.2 KiB (media de la
     * BD de OSM) como 10.6 KiB (media de la satelital).
     */
    bool start(const QVector<TileDataset> &datasets, int cacheMiB = 128);

    //! Carga los datasets desde un datasets.json. \a baseDir resuelve rutas relativas.
    static QVector<TileDataset> loadDatasets(const QString &jsonPath,
                                             QString *error = nullptr);

    QStringList datasetIds() const;
    const TileDataset *dataset(const QString &id) const;
    const TileDataset *activeDataset() const;

    //! Cambia de capa base. Conserva la cache de la capa anterior.
    bool setActiveDataset(const QString &id);
    QString activeDatasetId() const { return m_activeId; }

    /*!
     * \brief Pide las teselas que cubren un rectangulo geografico.
     *
     * Las peticiones se agrupan con un pequeno retardo: mientras el usuario
     * arrastra el mapa llegan decenas de eventos por segundo y no tiene
     * sentido lanzar una consulta por cada uno.
     *
     * \return identificador de la peticion (0 si no habia nada que pedir).
     */
    quint64 requestViewport(const QGeoCoordinate &northWest,
                            const QGeoCoordinate &southEast,
                            int zoom,
                            int marginTiles = 1);

    //! Plan de dibujo para el estado ACTUAL de la cache. No bloquea nunca.
    TilePlanner::Plan planFor(const QGeoCoordinate &northWest,
                              const QGeoCoordinate &southEast,
                              int zoom,
                              int marginTiles = 0) const;

    //! Nivel de zoom que mejor encaja con el ancho del widget.
    int bestZoomFor(double spanLongitudeDeg, int viewportWidthPx) const;

    /*!
     * \brief Cache de la capa ACTIVA.
     *
     * Hay una cache por dataset, no una compartida. Las claves de tesela son
     * (z, x, y) y nada mas, asi que la tesela 10/285/447 de OSM y la 10/285/447
     * de la satelital colisionaban: al alternar capas se veian teselas
     * mezcladas de las dos. Y era peor de lo que parece, porque el registro de
     * ausencias tambien se compartia: las teselas de mar que no existen en la
     * satelital hacian que en OSM ni se pidieran.
     */
    TileCache &cache();
    const TileCache &cache() const;

    //! Cache de un dataset concreto, creandola si hace falta.
    TileCache &cacheFor(const QString &datasetId);

    int debounceMs() const { return m_debounceMs; }
    void setDebounceMs(int ms) { m_debounceMs = ms; }

    /*!
     * \brief Cuantos niveles gruesos se precargan como respaldo.
     *
     * No basta con precargar UN nivel: si ese tambien tiene huecos, el
     * respaldo falla igual. Es exactamente lo que pasa con la capa satelital,
     * cuyo relleno solo llega al 100 % en los niveles muy gruesos:
     *
     *     z 15 -> 32 %   z 12 -> 36 %   z 10 -> 51 %
     *     z  9 -> 65 %   z  8 -> 72 %   z  7 -> 90 %   z 6 -> 100 %
     *
     * Por eso se carga una ESCALERA: z-2, z-4, z-6... Cada peldano tiene 4^n
     * veces menos teselas que el anterior, asi que los tres juntos suman una
     * fraccion despreciable del nivel de detalle, y garantizan que el
     * planificador siempre encuentre un ancestro con imagen.
     *
     * 0 lo desactiva.
     */
    /*!
     * \brief Peldanos de escalera cuando se fijan a mano. -1 = automatico.
     *
     * En automatico solo se precarga el peldano MAS GRUESO, que es barato y
     * ya cubre la pantalla entera por respaldo. Los intermedios se piden
     * despues, y solo si al llegar el detalle quedan huecos de verdad.
     *
     * El motivo esta medido sobre la BD satelital real, zoom 16 sobre La
     * Habana: los peldanos intermedios costaron 420 ms de los 1088 totales
     * y no taparon ni un hueco, porque el detalle vino completo. El relleno
     * global del 32 % es un promedio enganoso: sobre ciudad esta al 100 % y
     * los huecos estan en el mar.
     */
    int fallbackLevels() const { return m_fallbackLevels; }
    void setFallbackLevels(int n) { m_fallbackLevels = qBound(-1, n, 5); }

    //! Peldanos que se usarian ahora mismo, ya resueltos.
    int effectiveFallbackLevels() const;

    int fallbackStep() const { return m_fallbackStep; }
    void setFallbackStep(int levels) { m_fallbackStep = qBound(1, levels, 4); }

signals:
    //! Han llegado teselas nuevas a la cache: toca repintar. Se emite una vez
    //! por nivel, asi que con carga progresiva llega primero el grueso.
    void tilesReady(quint64 requestId, int zoom, int newTiles);
    void activeDatasetChanged(const QString &id);
    void errorOccurred(const QString &message);

private:
    //! Pide peldanos intermedios si, con el detalle ya cargado, quedan huecos.
    void repairGapsIfNeeded();

private slots:
    void onTilesLoaded(quint64 requestId, const QString &datasetId,
                       const libmapa::TileMatrix::TileRange &range,
                       const QVector<libmapa::LoadedTile> &tiles);
    void onLoadFailed(quint64 requestId, const QString &message);
    void flushPendingRequest();

private:
    QThread m_thread;
    TileLoader *m_loader = nullptr;      // vive en m_thread
    // Una cache por dataset. std::map y no QHash: QHash exige que el valor
    // sea copiable para poder desasociarse, y TileCache tiene un QMutex.
    mutable std::map<QString, std::unique_ptr<TileCache>> m_caches;
    qint64 m_cacheBytesPerDataset = 128LL * 1024 * 1024;
    TilePlanner m_planner;
    TileMatrix m_matrix;

    QHash<QString, TileDataset> m_datasets;
    QString m_activeId;

    QTimer m_debounce;
    int m_debounceMs = 80;
    int m_fallbackLevels = -1;   // -1 = automatico segun el relleno
    int m_fallbackStep = 2;

    TileRequest m_pending;
    bool m_hasPending = false;

    int m_targetZoom = 0;
    //! Ultimo viewport pedido, para poder evaluar los huecos al recibirlo.
    QGeoCoordinate m_lastNorthWest;
    QGeoCoordinate m_lastSouthEast;
    int m_lastMargin = 0;
    quint64 m_repairedFor = 0;     //!< peticion para la que ya se reparo

    quint64 m_nextRequestId = 1;
    bool m_started = false;
};

} // namespace libmapa

#endif // LIBMAPA_TILES_TILESERVICE_H_
