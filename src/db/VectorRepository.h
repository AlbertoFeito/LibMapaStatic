#ifndef LIBMAPA_DB_VECTORREPOSITORY_H_
#define LIBMAPA_DB_VECTORREPOSITORY_H_

#include "libmapa/MapTypes.h"

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <optional>

namespace libmapa {

/*!
 * \brief Acceso a los datos vectoriales: puntos, vehiculos, poligonos, rutas.
 *
 * Sustituye a CBDatos. Diferencias de fondo:
 *
 *  - Tipos de VALOR en la interfaz, no QList<void*>. El original devolvia
 *    punteros sin tipo y el llamador hacia ((CBarco*)p)->... sin comprobar
 *    nada; si la tabla no era la esperada, comportamiento indefinido.
 *
 *  - CERO DDL en tiempo de ejecucion. No se crean tablas a partir del nombre
 *    que escriba el usuario.
 *
 *  - Toda escritura compuesta va en una transaccion. Guardar un poligono de
 *    200 vertices eran 200 commits con su fsync cada uno.
 *
 *  - Los errores se propagan: cada operacion devuelve si salio bien y emite
 *    errorOccurred con el detalle. El original hacia
 *    "if (Consulta.prepare(x)) Consulta.exec();" y descartaba el resultado.
 */
class VectorRepository : public QObject
{
    Q_OBJECT

public:
    explicit VectorRepository(QObject *parent = nullptr);
    ~VectorRepository() override;

    //! Abre el fichero y aplica las migraciones pendientes.
    bool open(const QString &filePath);
    bool isOpen() const;
    void close();

    QString filePath() const { return m_filePath; }
    int schemaVersion() const;
    QString lastError() const { return m_lastError; }

    // --- Puntos ----------------------------------------------------------
    std::optional<qint64> insertPoint(const MapPoint &point);
    bool updatePoint(const MapPoint &point);
    bool removePoint(qint64 id);
    QVector<MapPoint> loadPoints() const;
    std::optional<MapPoint> findPointByName(const QString &name) const;

    // --- Vehiculos -------------------------------------------------------
    //! Inserta el punto y su fila de vehiculo en una sola transaccion.
    std::optional<qint64> insertVehicle(const MapVehicle &vehicle);
    bool updateVehicle(const MapVehicle &vehicle);
    QVector<MapVehicle> loadVehicles() const;

    /*!
     * \brief Buques AIS actualizados desde \a sinceUtcMs.
     *
     * Con las fechas guardadas como enteros esto es un WHERE normal. En el
     * original, las fechas eran TEXT "dd/MM/yyyy hh:mm:ss" y el filtro del
     * ultimo dia se hacia comparando dia y mes a mano en C++.
     */
    QVector<MapVehicle> loadVesselsUpdatedSince(qint64 sinceUtcMs) const;

    // --- Trayectorias ----------------------------------------------------
    bool appendTrack(qint64 pointId, const TrackSample &sample);
    bool appendTrackBatch(qint64 pointId, const QVector<TrackSample> &samples);
    QVector<TrackSample> loadTrack(qint64 pointId, int limit = 500) const;
    //! Deja solo las \a keep muestras mas recientes.
    bool pruneTrack(qint64 pointId, int keep);

    // --- Poligonos -------------------------------------------------------
    std::optional<qint64> insertPolygon(const MapPolygon &polygon);
    bool removePolygon(qint64 id);
    QVector<MapPolygon> loadPolygons() const;

    // --- Rutas -----------------------------------------------------------
    std::optional<qint64> insertRoute(const MapRoute &route);
    bool removeRoute(qint64 id);
    QVector<MapRoute> loadRoutes() const;

signals:
    void errorOccurred(const QString &context, const QString &message);

private:
    bool migrate();
    bool fail(const QString &context, const QString &message) const;
    QSqlDatabase db() const;

    QString m_filePath;
    QString m_connectionId;
    mutable QString m_lastError;
    bool m_open = false;
};

} // namespace libmapa

#endif // LIBMAPA_DB_VECTORREPOSITORY_H_
