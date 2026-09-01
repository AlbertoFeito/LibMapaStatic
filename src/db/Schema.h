#ifndef LIBMAPA_DB_SCHEMA_H_
#define LIBMAPA_DB_SCHEMA_H_

#include <QStringList>

namespace libmapa {
namespace schema {

//! Version actual del esquema. Cada incremento anade un bloque a migrations().
constexpr int kCurrentVersion = 1;

/*!
 * \brief Sentencias que llevan la BD de la version \a from a kCurrentVersion.
 *
 * Sustituye al esquema original, cuyos problemas estan verificados uno a uno
 * contra cbdatosmapa.cpp y contra el propio SQLite:
 *
 *  1. "NOT nullptr" NO es SQL valido. Un Find&Replace de NULL por nullptr
 *     arraso las cadenas SQL, y sqlite3 responde:
 *
 *         near "nullptr": syntax error
 *
 *     Como el codigo hace "if (Consulta.prepare(Crea)) Consulta.exec();", el
 *     prepare falla en silencio y el exec ni se intenta. Es decir: en una
 *     instalacion nueva las tablas NO se crean. La aplicacion solo funciona
 *     sobre ficheros .sig heredados de antes del reemplazo.
 *
 *  2. "no_punto KEY INTEGER" no declara una clave primaria: SQLite lo lee
 *     como una columna llamada no_punto de tipo "KEY INTEGER", con pk=0.
 *     Se quiso escribir PRIMARY KEY.
 *
 *  3. Una tabla por entidad: trayectorias_<nombre>, poligono_<nombre>,
 *     Ruta_<fecha>. Con 500 buques son 500 tablas, sin indices, sin JOINs
 *     posibles y con DDL construido a partir de texto del usuario.
 *
 *  4. Fechas como TEXT "dd/MM/yyyy hh:mm:ss", imposibles de comparar en SQL.
 *     De ahi que el filtro "del ultimo dia" fuera aquella condicion con
 *     qAbs(dia-dia)<=1 && (qAbs(mes-mes)<=1 || mes==11).
 *
 *  5. Sin claves foraneas: al borrar un punto, su tabla trayectorias_<nombre>
 *     quedaba huerfana para siempre.
 */
QStringList migrations(int from);

//! Nombre de la tabla que guarda la version del esquema.
inline const char *versionTable() { return "schema_version"; }

} // namespace schema
} // namespace libmapa

#endif // LIBMAPA_DB_SCHEMA_H_
