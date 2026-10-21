#ifndef LIBMAPA_VECTOR_FEATUREQUERY_H_
#define LIBMAPA_VECTOR_FEATUREQUERY_H_

#include "tiles/TileKey.h"
#include "vector/MvtTile.h"

#include <QString>
#include <QVariantMap>
#include <QVector>

namespace libmapa {

/*!
 * \brief «Qué hay aquí»: las entidades de una tesela vectorial cerca de un punto.
 *
 * Es la consulta inversa al pintado: en vez de convertir la tesela en píxeles,
 * busca qué calle, edificio, parque o lugar cae bajo un punto (o a pocos metros)
 * y devuelve sus atributos (`class`, `name`, `surface`, `render_height`…).
 */
namespace featurequery {

//! Una entidad encontrada.
struct Hit
{
    QString layer;                       //!< Capa de la tesela: "transportation", "poi"…
    mvt::GeomType type = mvt::GeomType::Unknown;
    double distanceM = 0.0;              //!< 0 si el punto cae DENTRO de un polígono.
    quint64 id = 0;                      //!< Id de la entidad en la tesela (0 si no tiene).
    QVariantMap attributes;              //!< Todas sus propiedades, por nombre.
};

/*!
 * \brief Entidades de \a tile (que es la tesela \a key) a menos de \a radiusM
 *        metros de (\a lat, \a lon).
 *
 * Ordenadas de más a menos relevantes: primero los polígonos que contienen el
 * punto, luego por distancia; a igualdad, puntos antes que líneas y líneas antes
 * que polígonos (lo más específico arriba). Las capas sin nombre útil para el
 * usuario (p. ej. la máscara de agua) se devuelven igual: filtrar es cosa del
 * que llama.
 */
QVector<Hit> query(const mvt::Tile &tile, const TileKey &key, double lat, double lon,
                   double radiusM);

} // namespace featurequery

} // namespace libmapa

#endif // LIBMAPA_VECTOR_FEATUREQUERY_H_
