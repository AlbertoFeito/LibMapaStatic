#include "db/Schema.h"

namespace libmapa {
namespace schema {

// Devuelve, EN ORDEN, las sentencias DDL necesarias para llevar una BD desde la
// version 'from' a la actual. Cada bloque "if (from < N)" agrupa la migracion que
// sube a la version N: si la BD ya esta en la version N esos CREATE se omiten, de
// modo que aplicar la lista completa es seguro tanto en una BD nueva (from = 0)
// como en una que ya paso migraciones anteriores. El llamador ejecuta todas estas
// sentencias dentro de una unica transaccion y actualiza user_version al final.
QStringList migrations(int from)
{
    QStringList sql;

    if (from < 2) {
        // Entidades de dibujo: puntos, polilineas y poligonos que el usuario
        // dibuja y edita, en UNA tabla. Deliberadamente NO hay una tabla por
        // concepto del dominio: la geometria va aparte y todo lo demas en
        // 'atributos', como JSON. Asi un tipo nuevo de zona o de ruta no
        // obliga a migrar el esquema, y la libreria no sabe de dominios.
        sql << QStringLiteral(
            "CREATE TABLE entidad ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  capa        TEXT    NOT NULL,"
            "  tipo        TEXT    NOT NULL DEFAULT '',"
            "  geometria   INTEGER NOT NULL,"     // 0 punto 1 polilinea 2 poligono
            "  nombre      TEXT    NOT NULL DEFAULT '',"
            "  descripcion TEXT    NOT NULL DEFAULT '',"
            "  color_linea    INTEGER NOT NULL DEFAULT 0,"
            "  color_relleno  INTEGER NOT NULL DEFAULT 0,"
            "  ancho_linea    REAL    NOT NULL DEFAULT 2,"
            "  estilo_linea   INTEGER NOT NULL DEFAULT 1,"
            "  radio_px       REAL    NOT NULL DEFAULT 6,"
            "  etiqueta_visible INTEGER NOT NULL DEFAULT 1,"
            "  visible     INTEGER NOT NULL DEFAULT 1,"
            "  simbolo     BLOB,"
            "  atributos   TEXT    NOT NULL DEFAULT '{}',"
            "  creado_utc  INTEGER NOT NULL"
            ")");

        sql << QStringLiteral("CREATE INDEX idx_entidad_capa ON entidad(capa)");
        sql << QStringLiteral("CREATE INDEX idx_entidad_tipo ON entidad(tipo)");

        // 'parte' permite geometrias multi-parte (un .geo entero como una sola
        // entidad). Las de una parte usan parte 0.
        sql << QStringLiteral(
            "CREATE TABLE entidad_vertice ("
            "  entidad_id INTEGER NOT NULL"
            "             REFERENCES entidad(id) ON DELETE CASCADE,"
            "  parte      INTEGER NOT NULL DEFAULT 0,"
            "  orden      INTEGER NOT NULL,"
            "  latitud    REAL NOT NULL,"
            "  longitud   REAL NOT NULL,"
            "  PRIMARY KEY (entidad_id, parte, orden)"
            ") WITHOUT ROWID");

        // Las capas, para conservar visibilidad y orden de dibujo (una capa
        // vacia tambien debe sobrevivir).
        sql << QStringLiteral(
            "CREATE TABLE capa ("
            "  id       TEXT PRIMARY KEY,"
            "  nombre   TEXT NOT NULL DEFAULT '',"
            "  visible  INTEGER NOT NULL DEFAULT 1,"
            "  editable INTEGER NOT NULL DEFAULT 1,"
            "  z_orden  INTEGER NOT NULL DEFAULT 0"
            ")");
    }

    return sql;
}

} // namespace schema
} // namespace libmapa
