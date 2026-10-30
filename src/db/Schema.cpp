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

    if (from < 3) {
        // v3: desplazamiento de la ETIQUETA respecto a su anclaje, en pixeles.
        // Permite arrastrar el rotulo y que, al ser relativo, siga a la entidad.
        // ALTER ADD COLUMN con DEFAULT 0 es seguro y no reescribe las filas: una
        // BD v2 (p. ej. una mascara generada por script) se abre y se migra sola.
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN label_dx REAL NOT NULL DEFAULT 0");
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN label_dy REAL NOT NULL DEFAULT 0");
        // Vertice al que se ancla la etiqueta (−1 = automatico; 0 = primer punto,
        // para un area objetivo). Asi el anclaje sobrevive guardar/cargar.
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN label_anchor INTEGER NOT NULL DEFAULT -1");
    }

    if (from < 4) {
        // v4: lado del anclaje en el que cuelga el rotulo (LabelAnchor; 0 = Auto,
        // el comportamiento de siempre) y tamano propio de su fuente en puntos
        // (0 = la fuente del mapa). Con DEFAULT 0 una BD v3 se migra sola y sus
        // entidades se ven exactamente igual que antes.
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN label_pos INTEGER NOT NULL DEFAULT 0");
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN label_fuente INTEGER NOT NULL DEFAULT 0");
    }

    if (from < 5) {
        // v5: circulos y arcos geodesicos (GeometryKind::Circle y Arc). El vertice
        // guardado es el centro; el radio va en metros y los azimuts del arco en
        // grados. Con DEFAULT 0 una BD anterior se migra sola: sus entidades no son
        // redondas y estas columnas no se leen.
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN radio_m REAL NOT NULL DEFAULT 0");
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN arco_ini REAL NOT NULL DEFAULT 0");
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN arco_fin REAL NOT NULL DEFAULT 0");
    }

    if (from < 6) {
        // v6: datos propios de cada PARTE de una entidad multi-parte (MapFeature::partsInfo):
        // nombre, color (ARGB; NULL = el del estilo) y lado del rotulo. Una fila por parte
        // con algun dato; las entidades sin partsInfo no tienen filas. Se borran con su
        // entidad, como sus vertices.
        sql << QStringLiteral(
            "CREATE TABLE entidad_parte ("
            "  entidad_id INTEGER NOT NULL"
            "             REFERENCES entidad(id) ON DELETE CASCADE,"
            "  parte      INTEGER NOT NULL,"
            "  nombre     TEXT    NOT NULL DEFAULT '',"
            "  color      INTEGER,"
            "  label_pos  INTEGER NOT NULL DEFAULT 0,"
            "  PRIMARY KEY (entidad_id, parte)"
            ") WITHOUT ROWID");
    }

    if (from < 7) {
        // v7: un Arc puede ser un SECTOR (cuna cerrada hasta el centro, un area): MapFeature::
        // arcSector. Con DEFAULT 0 los arcos ya guardados siguen siendo solo la linea.
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN arco_sector INTEGER NOT NULL DEFAULT 0");
    }

    if (from < 8) {
        // v8: dueno de un circulo o sector (MapFeature::originTargetId y originFeatureId). -1 =
        // sin dueno, asi que lo ya guardado sigue con su centro fijo.
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN origen_objetivo INTEGER NOT NULL DEFAULT -1");
        sql << QStringLiteral(
            "ALTER TABLE entidad ADD COLUMN origen_entidad INTEGER NOT NULL DEFAULT -1");
    }

    return sql;
}

} // namespace schema
} // namespace libmapa
