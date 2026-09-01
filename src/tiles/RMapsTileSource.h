#ifndef LIBMAPA_TILES_RMAPSTILESOURCE_H_
#define LIBMAPA_TILES_RMAPSTILESOURCE_H_

#include "tiles/ITileSource.h"

// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que
// hay que pedirlo explicitamente: sin esto, bindValue() no compila en
// Qt 5.14 porque QVariant es un tipo incompleto y no hay conversion
// posible desde int, QString ni QByteArray.
#include <QVariant>
#include <QSqlQuery>
#include <memory>

namespace libmapa {

/*!
 * \brief Fuente de teselas sobre el formato RMaps / BigPlanet.
 *
 * Es el formato real de las dos BD del proyecto, segun OSMSchema.txt:
 *
 *     CREATE TABLE tiles (version INTEGER, z INTEGER, x INTEGER,
 *                         y INTEGER, s INTEGER, image BLOB);
 *     CREATE INDEX IND ON tiles (z, x, y, s);
 *
 * Detalles que el codigo original desaprovechaba:
 *
 *  - El indice es (z,x,y,s) pero las consultas nunca filtraban por 's', asi
 *    que SQLite solo podia usar el prefijo del indice. Aqui 's' entra
 *    siempre en el WHERE.
 *
 *  - Se hacia "SELECT *", que trae 'version' e 's' ademas del BLOB. Aqui se
 *    piden solo las columnas necesarias, y en available() ni siquiera el BLOB.
 *
 *  - Los resultados se leian por posicion fija (value(1), value(2), value(3),
 *    value(5)). Aqui se resuelven por nombre una sola vez (falla F-13).
 *
 *  - Las sentencias se reconstruian como cadenas concatenadas en cada
 *    llamada. Aqui se preparan una vez y se reutilizan con bindValue.
 */
class RMapsTileSource : public ITileSource
{
public:
    explicit RMapsTileSource(TileDataset dataset);
    ~RMapsTileSource() override;

    const TileDataset &dataset() const override { return m_ds; }

    //! Abre la conexion y prepara las sentencias. Debe llamarse en el hilo
    //! que vaya a usar la fuente (las conexiones SQLite no se comparten).
    bool open();
    bool isOpen() const { return m_open; }

    QByteArray fetch(const TileKey &key) override;

    QHash<TileKey, QByteArray> fetchRange(int z,
                                          int xMin, int xMax,
                                          int yMin, int yMax) override;

    QSet<TileKey> available(int z,
                            int xMin, int xMax,
                            int yMin, int yMax) override;

    qint64 tileCount(int z) override;

    //! Ultimo error registrado, para diagnostico.
    QString lastError() const { return m_lastError; }

private:
    //! Construye el WHERE comun, incluyendo 's' solo si la columna existe.
    QString whereClause() const;
    void bindCommon(QSqlQuery &q, int storedZ,
                    int xMin, int xMax, int yMinStored, int yMaxStored) const;

    //! Traduce el rango de y logico (XYZ) al rango de y almacenado.
    //! En TMS el orden se invierte, por eso hay que recalcular min y max.
    void storageYRange(int z, int yMin, int yMax,
                       int *outMin, int *outMax) const;

    TileDataset m_ds;
    bool m_open = false;
    QString m_lastError;

    // Las QSqlQuery se guardan preparadas. Son punteros porque QSqlQuery no
    // es copiable ni reasignable de forma segura una vez preparada.
    std::unique_ptr<QSqlQuery> m_qSingle;
    std::unique_ptr<QSqlQuery> m_qRange;
    std::unique_ptr<QSqlQuery> m_qAvailable;
    std::unique_ptr<QSqlQuery> m_qCount;
};

} // namespace libmapa

#endif // LIBMAPA_TILES_RMAPSTILESOURCE_H_
