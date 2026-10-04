#ifndef LIBMAPA_DB_VECTORREPOSITORY_H_
#define LIBMAPA_DB_VECTORREPOSITORY_H_

#include "libmapa/MapFeature.h"
#include "libmapa/MapTypes.h"

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <optional>

namespace libmapa {

/*!
 * \brief Persistencia de las entidades de dibujo (puntos, polilineas y
 *        poligonos) y sus capas, en SQLite.
 *
 * Es el almacen que usa MapWidget para guardar y recargar lo que el usuario
 * dibuja (ver `saveFeaturesTo`/`loadFeaturesFrom`). Una sola tabla de
 * entidades: la geometria va aparte y el resto del dominio en 'atributos',
 * como JSON, de modo que la libreria no conoce ningun dominio concreto.
 *
 * Diferencias de fondo respecto al CBDatos original:
 *
 *  - Tipos de VALOR en la interfaz (MapFeature), no QList<void*> con castes a
 *    mano en cada uso.
 *  - CERO DDL en tiempo de ejecucion: no se crean tablas a partir del nombre
 *    que escriba el usuario.
 *  - Toda escritura compuesta va en una transaccion (un .geo de 200 vertices
 *    era un fsync por vertice).
 *  - Los errores se propagan: cada operacion devuelve si salio bien y emite
 *    errorOccurred con el detalle.
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

    /*!
     * \name Entidades de dibujo
     *
     * Puntos, poligonos y areas de interes tal y como los maneja el widget.
     * Una sola tabla para todos: la geometria va aparte y el resto de campos
     * del dominio en 'atributos', como JSON. Un tipo nuevo de zona no obliga
     * a migrar el esquema.
     */
    //@{
    //! Guarda o actualiza. Si feature.id es valido y existe, actualiza.
    std::optional<qint64> saveFeature(const MapFeature &feature);
    //! Guarda un conjunto entero en UNA transaccion.
    bool saveFeatures(const QVector<MapFeature> &features);
    bool removeFeatureRow(qint64 id);
    QVector<MapFeature> loadFeatures() const;
    QVector<MapFeature> loadFeaturesInLayer(const QString &layerId) const;
    //! Borra todas las entidades. Para volcar el estado completo del widget.
    bool clearFeatures();

    bool saveLayer(const LayerInfo &layer);
    QVector<LayerInfo> loadLayers() const;
    //@}

signals:
    void errorOccurred(const QString &context, const QString &message);

private:
    /*!
     * \brief Escribe una entidad SIN abrir transaccion.
     *
     * Existe porque SQLite no admite transacciones anidadas: si saveFeatures
     * abre una y luego llama a saveFeature, que abre otra, la interior falla.
     * Cada punto de entrada publico abre la suya y ambos usan esta.
     */
    std::optional<qint64> writeFeature(QSqlDatabase &database,
                                       const MapFeature &feature);

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
