#include "db/Schema.h"

namespace libmapa {
namespace schema {

QStringList migrations(int from)
{
    QStringList sql;

    if (from < 1) {
        sql << QStringLiteral(
            "CREATE TABLE punto ("
            "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  nombre       TEXT    NOT NULL UNIQUE,"
            "  descripcion  TEXT    NOT NULL DEFAULT '',"
            "  tipo         INTEGER NOT NULL DEFAULT 0,"
            "  simbolo      BLOB,"
            "  color_rgb    INTEGER NOT NULL DEFAULT 0,"
            "  etiqueta_visible INTEGER NOT NULL DEFAULT 1,"
            "  latitud      REAL    NOT NULL,"
            "  longitud     REAL    NOT NULL,"
            "  altura       REAL    NOT NULL DEFAULT 0,"
            // Epoch en milisegundos, no texto: comparable en SQL e indexable.
            "  creado_utc   INTEGER NOT NULL"
            ")");

        sql << QStringLiteral("CREATE INDEX idx_punto_tipo ON punto(tipo)");

        // Los datos de vehiculo cuelgan del punto en vez de duplicarlo.
        sql << QStringLiteral(
            "CREATE TABLE vehiculo ("
            "  punto_id     INTEGER PRIMARY KEY"
            "               REFERENCES punto(id) ON DELETE CASCADE,"
            "  clase        INTEGER NOT NULL,"          // 0 terrestre 1 aereo 2 naval
            "  color_traza  INTEGER NOT NULL DEFAULT 0,"
            "  traza_visible INTEGER NOT NULL DEFAULT 1,"
            "  traza_max_puntos INTEGER NOT NULL DEFAULT 500,"
            "  rumbo        REAL NOT NULL DEFAULT 0,"
            "  velocidad    REAL NOT NULL DEFAULT 0"
            ")");

        // AIS por COMPOSICION: solo existe fila si el vehiculo es naval y
        // tiene datos AIS. En el original, CBarco heredaba de CVehiculo y
        // anadia unos cincuenta getters que CAvion no podia usar.
        sql << QStringLiteral(
            "CREATE TABLE buque_ais ("
            "  punto_id     INTEGER PRIMARY KEY"
            "               REFERENCES punto(id) ON DELETE CASCADE,"
            "  mmsi         TEXT NOT NULL,"
            "  imo          TEXT NOT NULL DEFAULT '',"
            "  callsign     TEXT NOT NULL DEFAULT '',"
            "  nombre_buque TEXT NOT NULL DEFAULT '',"
            "  tipo_embarcacion TEXT NOT NULL DEFAULT '',"
            "  bandera      TEXT NOT NULL DEFAULT '',"
            "  destino      TEXT NOT NULL DEFAULT '',"
            "  estado_navegacion TEXT NOT NULL DEFAULT '',"
            "  eslora       REAL NOT NULL DEFAULT 0,"
            "  manga        REAL NOT NULL DEFAULT 0,"
            "  calado       REAL NOT NULL DEFAULT 0,"
            "  rumbo        REAL NOT NULL DEFAULT 0,"
            "  curso        REAL NOT NULL DEFAULT 0,"
            "  actualizado_utc INTEGER NOT NULL DEFAULT 0"
            ")");

        sql << QStringLiteral("CREATE UNIQUE INDEX idx_ais_mmsi ON buque_ais(mmsi)");
        sql << QStringLiteral(
            "CREATE INDEX idx_ais_actualizado ON buque_ais(actualizado_utc)");

        // UNA tabla de trayectorias, no una por punto.
        sql << QStringLiteral(
            "CREATE TABLE trayectoria ("
            "  punto_id  INTEGER NOT NULL REFERENCES punto(id) ON DELETE CASCADE,"
            "  t_utc     INTEGER NOT NULL,"
            "  latitud   REAL NOT NULL,"
            "  longitud  REAL NOT NULL,"
            "  altura    REAL NOT NULL DEFAULT 0,"
            "  rumbo     REAL NOT NULL DEFAULT 0,"
            "  velocidad REAL NOT NULL DEFAULT 0,"
            "  PRIMARY KEY (punto_id, t_utc)"
            ") WITHOUT ROWID");

        sql << QStringLiteral(
            "CREATE TABLE poligono ("
            "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  nombre       TEXT NOT NULL UNIQUE,"
            "  color_linea  INTEGER NOT NULL DEFAULT 0,"
            "  color_relleno INTEGER NOT NULL DEFAULT 0,"
            "  relleno      INTEGER NOT NULL DEFAULT 1,"
            "  creado_utc   INTEGER NOT NULL"
            ")");

        sql << QStringLiteral(
            "CREATE TABLE poligono_vertice ("
            "  poligono_id INTEGER NOT NULL"
            "              REFERENCES poligono(id) ON DELETE CASCADE,"
            "  orden       INTEGER NOT NULL,"
            "  latitud     REAL NOT NULL,"
            "  longitud    REAL NOT NULL,"
            "  PRIMARY KEY (poligono_id, orden)"
            ") WITHOUT ROWID");

        sql << QStringLiteral(
            "CREATE TABLE ruta ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  nombre     TEXT NOT NULL UNIQUE,"
            "  color_rgb  INTEGER NOT NULL DEFAULT 0,"
            "  creado_utc INTEGER NOT NULL"
            ")");

        // 'pos' se guardaba como TEXT con la coordenada dentro; aqui son dos
        // numeros, que se pueden comparar y ordenar.
        sql << QStringLiteral(
            "CREATE TABLE ruta_punto ("
            "  ruta_id     INTEGER NOT NULL REFERENCES ruta(id) ON DELETE CASCADE,"
            "  orden       INTEGER NOT NULL,"
            "  prioridad   INTEGER NOT NULL DEFAULT 0,"
            "  descripcion TEXT NOT NULL DEFAULT '',"
            "  latitud     REAL NOT NULL,"
            "  longitud    REAL NOT NULL,"
            "  radio_m     REAL NOT NULL DEFAULT 20,"
            "  PRIMARY KEY (ruta_id, orden)"
            ") WITHOUT ROWID");
    }

    if (from < 2) {
        // Entidades genericas de la capa de dibujo. Deliberadamente NO hay
        // una tabla por concepto del dominio: la geometria va aparte y todo
        // lo demas en 'atributos', como JSON. Asi un tipo nuevo de zona o de
        // ruta no obliga a migrar el esquema.
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

        sql << QStringLiteral(
            "CREATE TABLE entidad_vertice ("
            "  entidad_id INTEGER NOT NULL"
            "             REFERENCES entidad(id) ON DELETE CASCADE,"
            "  orden      INTEGER NOT NULL,"
            "  latitud    REAL NOT NULL,"
            "  longitud   REAL NOT NULL,"
            "  PRIMARY KEY (entidad_id, orden)"
            ") WITHOUT ROWID");

        // Las capas, para conservar visibilidad y orden de dibujo.
        sql << QStringLiteral(
            "CREATE TABLE capa ("
            "  id       TEXT PRIMARY KEY,"
            "  nombre   TEXT NOT NULL DEFAULT '',"
            "  visible  INTEGER NOT NULL DEFAULT 1,"
            "  editable INTEGER NOT NULL DEFAULT 1,"
            "  z_orden  INTEGER NOT NULL DEFAULT 0"
            ")");
    }

    if (from < 2) {
        // -------------------------------------------------------------
        // Entidades ESTATICAS: puntos, poligonales y poligonos que el
        // usuario dibuja y edita.
        //
        // Van en tablas propias, separadas de punto/vehiculo/buque_ais.
        // No es duplicacion: son cosas distintas con ciclos de vida
        // distintos. Una zona prohibida se dibuja una vez y se edita a
        // mano; un objetivo AIS se actualiza varias veces por segundo y
        // arrastra una trayectoria. Meterlos en la misma tabla obligaria
        // a que cada actualizacion de posicion tocara la misma tabla que
        // las zonas.
        // -------------------------------------------------------------
        sql << QStringLiteral(
            "CREATE TABLE feature ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  capa        TEXT    NOT NULL,"
            "  geometria   INTEGER NOT NULL,"     // 0 punto 1 polilinea 2 poligono
            // Etiqueta de dominio. La libreria no la interpreta: la pone y
            // la filtra la aplicacion.
            "  tipo        TEXT    NOT NULL DEFAULT '',"
            "  nombre      TEXT    NOT NULL DEFAULT '',"
            "  descripcion TEXT    NOT NULL DEFAULT '',"
            "  color_linea    INTEGER NOT NULL DEFAULT 0,"
            "  color_relleno  INTEGER NOT NULL DEFAULT 0,"
            "  ancho_linea    REAL    NOT NULL DEFAULT 2,"
            "  estilo_linea   INTEGER NOT NULL DEFAULT 1,"
            "  radio_px       REAL    NOT NULL DEFAULT 6,"
            "  etiqueta_visible INTEGER NOT NULL DEFAULT 1,"
            "  visible     INTEGER NOT NULL DEFAULT 1,"
            "  icono       BLOB,"
            // Los atributos de dominio se guardan como JSON: son abiertos
            // por definicion, y una tabla clave-valor obligaria a un JOIN
            // por atributo sin ganar nada.
            "  atributos   TEXT    NOT NULL DEFAULT '{}',"
            "  creado_utc  INTEGER NOT NULL"
            ")");

        sql << QStringLiteral("CREATE INDEX idx_feature_capa ON feature(capa)");
        sql << QStringLiteral("CREATE INDEX idx_feature_tipo ON feature(tipo)");

        sql << QStringLiteral(
            "CREATE TABLE feature_vertice ("
            "  feature_id INTEGER NOT NULL"
            "             REFERENCES feature(id) ON DELETE CASCADE,"
            "  orden      INTEGER NOT NULL,"
            "  latitud    REAL NOT NULL,"
            "  longitud   REAL NOT NULL,"
            "  PRIMARY KEY (feature_id, orden)"
            ") WITHOUT ROWID");

        // Las capas se guardan aparte: una capa vacia tambien debe
        // sobrevivir, con su visibilidad y su orden de dibujo.
        sql << QStringLiteral(
            "CREATE TABLE feature_capa ("
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
