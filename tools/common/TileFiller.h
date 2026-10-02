#ifndef LIBMAPA_TOOLS_TILEFILLER_H_
#define LIBMAPA_TOOLS_TILEFILLER_H_

#include "tiles/TileDataset.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QGeoCoordinate>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QSqlDatabase;
class QSqlQuery;
class QTimer;
QT_END_NAMESPACE

namespace libmapa {

/*!
 * \brief Motor de relleno de teselas: descarga las que faltan de una fuente XYZ
 *        (Esri satelite por defecto) y las inserta en la BD con la codificacion
 *        del dataset (columna z con zFactor/zOffset, esquema XYZ/TMS, columna s).
 *
 * Es asincrono y NO bloquea: se apoya en el bucle de eventos de Qt (un temporizador
 * marca el ritmo y cada respuesta encadena la siguiente). Sirve igual para la
 * herramienta de consola (con un QCoreApplication) que para la ventana con mapa
 * (con la barra de progreso viva). El mismo codigo -y por tanto la misma
 * codificacion probada- para las dos.
 *
 * Uso:
 *   TileFiller f;
 *   f.prepare(params, &err);         // abre BD y calcula lo que falta (sin red)
 *   // ...mostrar f.totalToDownload() y pedir confirmacion...
 *   connect(&f, &TileFiller::progress, ...);
 *   connect(&f, &TileFiller::finished, ...);
 *   f.start();                       // empieza la descarga
 */
class TileFiller : public QObject
{
    Q_OBJECT

public:
    struct Params {
        TileDataset ds;                 //!< Codificacion + ruta de la BD.
        double latN = 0, lonW = 0, latS = 0, lonE = 0;
        //! Opcional: si trae >=3 vertices, solo se descargan las teselas cuyo
        //! CENTRO cae dentro del poligono (el bbox se calcula de sus vertices).
        //! Vacio = rectangulo latN/lonW/latS/lonE.
        QVector<QGeoCoordinate> polygon;
        int minZoom = 0, maxZoom = 0;
        QString url;                    //!< Plantilla con {z}{x}{y}.
        QByteArray userAgent = "LibMapaStatic-fill/1.0";
        double rate = 2.0;              //!< Lanzamientos por segundo (tope suave).
        //! Peticiones EN VUELO a la vez (oculta la latencia). El tope de ritmo lo
        //! sigue marcando 'rate'; para ir mas rapido se sube 'rate'.
        int connections = 2;
        int retries = 3;
        int timeoutMs = 20000;
        bool overwrite = false;         //!< false = solo lo que falta.
        bool createSchema = false;      //!< true = crea la tabla si no existe
                                        //!< (para bases NUEVAS).
        //! Auto-freno: tras esta racha de fallos SEGUIDOS (la fuente esta
        //! limitando), pausa y reanuda. 0 = desactivado.
        int throttleAfter = 8;
        int maxPauseSec = 300;          //!< Tope de la pausa (backoff creciente).
    };

    struct Stats {
        qint64 downloaded = 0;
        qint64 existed = 0;
        qint64 notFound = 0;            //!< 404: el origen no tiene esa tesela.
        qint64 failed = 0;
    };

    explicit TileFiller(QObject *parent = nullptr);
    ~TileFiller() override;

    //! Abre la BD y calcula que teselas faltan por zoom. SIN red. false si algo
    //! falla (BD inexistente, dataset invalido); el motivo queda en \a error.
    bool prepare(const Params &params, QString *error);

    qint64 totalToDownload() const { return m_total; }
    qint64 totalCells() const { return m_cells; }
    //! (zoom, cuantas faltan) por cada nivel, para mostrar el desglose.
    QVector<QPair<int, qint64>> perZoomMissing() const { return m_perZoom; }

    Stats stats() const { return m_stats; }
    bool isRunning() const { return m_running; }

public slots:
    //! Empieza la descarga (debe haberse llamado prepare() con exito).
    void start();
    //! Corta: no se lanzan mas peticiones y se emite finished(cancelled=true).
    void cancel();

signals:
    //! Avance: teselas hechas de un total, y velocidad media (teselas/segundo).
    void progress(qint64 done, qint64 total, double tilesPerSec);
    //! Un nivel de zoom quedo terminado, con cuantas se anadieron.
    void zoomFinished(int zoom, qint64 added);
    //! Mensajes puntuales (fallos de una tesela), para un registro.
    void message(const QString &text);
    //! Auto-freno: la fuente parece estar limitando; se pausa \a pauseSeconds
    //! antes de reanudar (tras \a consecutiveFails fallos seguidos).
    void throttling(int pauseSeconds, qint64 consecutiveFails);
    //! Fin de todo. \a cancelled indica si se corto a mitad.
    void finished(const libmapa::TileFiller::Stats &stats, bool cancelled);

private:
    // Un nivel del plan: rango de teselas y las que ya existen (indices de
    // ALMACENAMIENTO, es decir con la Y ya convertida por el esquema).
    struct ZoomPlan {
        int z = 0;
        int storedZ = 0;
        int x0 = 0, x1 = 0, y0 = 0, y1 = 0;   // indices XYZ (logicos)
        QSet<QPair<int, int>> present;        // (x, storedY) ya en la BD
        qint64 added = 0;
    };

    // Estado de UNA tesela en vuelo (antes era el cursor global). Permite varias
    // peticiones concurrentes sin que se pisen las coordenadas.
    struct Pending {
        int zi = 0;                 // indice de nivel en m_plan
        int z = 0, storedZ = 0;
        int x = 0, y = 0;           // indices XYZ logicos
        int storedY = 0;            // Y de almacenamiento (segun esquema)
        int attempt = 0;            // reintentos ya hechos de ESTA tesela
    };

    void schedule();                //!< Lanza peticiones hasta llenar las conexiones.
    void launch(Pending p);         //!< Crea la peticion HTTP de una tesela.
    bool nextTile(Pending &out);    //!< Siguiente tesela que falta (avanza cursor).
    void onReplyDone(QNetworkReply *reply);
    void insertTile(const Pending &p, const QByteArray &image);
    void finish(bool cancelled);

    Params m_p;
    QString m_connName;
    QSqlDatabase *m_db = nullptr;    // conexion propia (puntero para el .h ligero)
    QSqlQuery *m_ins = nullptr;
    QNetworkAccessManager *m_nam = nullptr;

    // Peticiones EN VUELO (reply -> su tesela) y cola de reintentos ya listos.
    QHash<QNetworkReply *, Pending> m_active;
    QVector<Pending> m_retry;
    int m_pendingRetries = 0;       // reintentos esperando su backoff (aun no en m_retry)
    QHash<QNetworkReply *, QTimer *> m_timeouts;   // timeout por peticion

    QVector<ZoomPlan> m_plan;
    QVector<QPair<int, qint64>> m_perZoom;   // (zoom, faltan) para el desglose
    int m_zi = 0;                   // indice de zoom del cursor
    qint64 m_idxInZoom = 0;         // celda lineal dentro del nivel actual
    bool m_exhausted = false;       // el cursor ya recorrio todo el plan
    qint64 m_consecFails = 0;       // fallos definitivos SEGUIDOS (auto-freno)
    int m_pauseCount = 0;           // pausas ya hechas (backoff creciente)
    bool m_paused = false;          // en pausa por auto-freno

    qint64 m_total = 0, m_cells = 0, m_done = 0;
    Stats m_stats;
    bool m_running = false;
    bool m_cancelled = false;
    qint64 m_minIntervalMs = 0;
    qint64 m_lastLaunchMs = 0;      // ultimo lanzamiento (para el ritmo)
    QElapsedTimer m_clock;          // para la velocidad media y el ritmo
};

} // namespace libmapa

#endif // LIBMAPA_TOOLS_TILEFILLER_H_
