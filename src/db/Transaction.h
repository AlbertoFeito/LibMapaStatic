#ifndef LIBMAPA_DB_TRANSACTION_H_
#define LIBMAPA_DB_TRANSACTION_H_

#include <QSqlDatabase>

namespace libmapa {

/*!
 * \brief Guard RAII de transaccion SQL.
 *
 * Corrige la falla F-14: CBDatos::guardarNuevoPoligono hacia N+1 INSERT
 * sueltos, y en SQLite cada INSERT fuera de transaccion es un commit con su
 * fsync. Guardar un poligono de 200 vertices eran 200 commits.
 *
 * Ademas garantiza el rollback si se sale por cualquier camino sin commit,
 * incluidos los "return" tempranos que el codigo original usaba en cada
 * comprobacion de error dejando la escritura a medias.
 *
 * Uso:
 *     Transaction tx(db);
 *     if (!tx.isActive()) return false;
 *     ... varios exec() ...
 *     return tx.commit();      // sin commit -> rollback automatico
 */
class Transaction
{
public:
    // Abre la transaccion al construirse. isActive() dice si se pudo abrir
    // (algunos motores/estados no lo permiten); no lanza excepciones.
    explicit Transaction(QSqlDatabase &db)
        : m_db(db), m_active(db.transaction())
    {
    }

    // Si la transaccion sigue abierta al destruirse (nadie llamo a commit ni a
    // rollback), deshace los cambios. Esto es lo que cubre los "return" tempranos.
    ~Transaction()
    {
        if (m_active && !m_finished)
            m_db.rollback();
    }

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    // ¿La transaccion se abrio correctamente? Comprobar antes de escribir.
    bool isActive() const { return m_active; }

    // Confirma los cambios. Devuelve false si no habia transaccion viva o si el
    // commit fallo; en cualquier caso la marca como terminada (no habra rollback).
    bool commit()
    {
        if (!m_active || m_finished)
            return false;
        m_finished = true;
        return m_db.commit();
    }

    // Deshace los cambios de forma explicita y marca la transaccion como
    // terminada, de modo que el destructor ya no vuelva a intentarlo.
    void rollback()
    {
        if (!m_active || m_finished)
            return;
        m_finished = true;
        m_db.rollback();
    }

private:
    QSqlDatabase &m_db;
    bool m_active = false;
    bool m_finished = false;
};

} // namespace libmapa

#endif // LIBMAPA_DB_TRANSACTION_H_
