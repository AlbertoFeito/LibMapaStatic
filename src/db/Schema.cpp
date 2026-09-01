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

    return sql;
}

} // namespace schema
} // namespace libmapa
