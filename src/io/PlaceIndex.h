#ifndef LIBMAPA_IO_PLACEINDEX_H_
#define LIBMAPA_IO_PLACEINDEX_H_

#include "libmapa/Places.h"

#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Buscador de lugares sobre una base de datos `lugares.sqlitedb`.
 *
 * Esquema (lo escribe `herramientas/osm_lugares_a_sqlitedb.py`):
 *
 *   lugar(id, nombre, busqueda, tipo, lat, lon, poblacion, peso)
 *
 * `busqueda` es el nombre normalizado (minúsculas, sin tildes); `peso` ordena los
 * resultados. Lee en solo lectura por el \c SqliteConnectionPool (una conexión por
 * hilo), así que \c search se puede llamar desde cualquier hilo.
 */
class PlaceIndex
{
public:
    PlaceIndex() = default;

    //! Fija la base de datos de lugares. Vacío = sin índice.
    void setDatabase(const QString &dbFile);
    QString database() const { return m_dbFile; }

    //! ¿Hay base de datos que se pueda abrir y con la tabla `lugar`?
    bool isUsable() const;

    /*!
     * \brief Lugares cuyo nombre contiene TODAS las palabras de \a text (sin
     *        distinguir tildes ni mayúsculas), mejores primero, como mucho \a limit.
     *
     * Van antes los que EMPIEZAN por lo escrito y, dentro de eso, los de más peso
     * (una ciudad antes que una aldea). Texto vacío o sin índice = lista vacía.
     */
    QVector<Place> search(const QString &text, int limit = 10) const;

    //! Minúsculas, sin tildes y con espacios simples: la forma en que se guarda
    //! `busqueda` y en que se compara lo que escribe la persona.
    static QString normalize(const QString &text);

private:
    QString m_dbFile;
    QString m_connId;   //!< Id para SqliteConnectionPool (único por instancia).
};

} // namespace libmapa

#endif // LIBMAPA_IO_PLACEINDEX_H_
