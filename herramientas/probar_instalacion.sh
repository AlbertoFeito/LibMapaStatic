#!/usr/bin/env bash
# ===================================================================
#  probar_instalacion.sh  -  comprueba que libmapa INSTALADA sirve a una app
#
#  Es el contrato que usan los ejemplos y cualquier producto: libmapa se
#  instala una vez y las apps la encuentran con find_package, sin tocar las
#  fuentes de la libreria. Este script:
#    1. mira que el prefijo tenga lo que se promete (config, bibliotecas,
#       cabeceras, herramientas);
#    2. fabrica un paquete de datos minimo (teselas de 1 px y, si esta en el repo,
#       el indice de lugares de prueba; sin red);
#    3. compila TODOS los ejemplos (examples/CMakeLists.txt) contra el prefijo;
#    4. arranca app_minima, visor_gpx, editor_rutas y buscador_lugares con
#       --comprobar, y check_data, sobre ese paquete.
#
#  Uso:   herramientas/probar_instalacion.sh <prefijo de instalacion> [carpeta de trabajo]
#  Antes: cmake --install <build> --prefix <prefijo>
#  Sin pantalla (CI):  QT_QPA_PLATFORM=offscreen herramientas/probar_instalacion.sh ...
#  Qt fuera de las rutas del sistema: CMAKE_PREFIX_PATH_EXTRA=/ruta/a/Qt
#  Sale con 0 si todo esta bien.
# ===================================================================

set -eu

PREFIJO="${1:-}"
if [ -z "$PREFIJO" ] || [ ! -d "$PREFIJO" ]; then
    echo "Uso: $0 <prefijo de instalacion> [carpeta de trabajo]" >&2
    exit 2
fi
PREFIJO="$(cd "$PREFIJO" && pwd)"
AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/.." && pwd)"
TRABAJO="${2:-$(mktemp -d)}"
mkdir -p "$TRABAJO"
TRABAJO="$(cd "$TRABAJO" && pwd)"

fallo() { echo "FALLO: $*" >&2; exit 1; }

# --- 1. Lo que la instalacion promete ---------------------------------
echo "== 1/4 Contenido de $PREFIJO"
for f in lib/cmake/libmapa/libmapaConfig.cmake \
         lib/cmake/libmapa/libmapaConfigVersion.cmake \
         lib/cmake/libmapa/libmapaTargets.cmake \
         include/libmapa/MapWidget.h \
         bin/check_data \
         share/libmapa/mapa.example.json; do
    [ -e "$PREFIJO/$f" ] || fallo "falta $f en el prefijo"
done
ls "$PREFIJO"/lib/libmapa_core.* >/dev/null 2>&1   || fallo "falta libmapa_core"
ls "$PREFIJO"/lib/libmapa_widget.* >/dev/null 2>&1 || fallo "falta libmapa_widget"

# --- 2. Paquete de datos minimo ----------------------------------------
echo "== 2/4 Paquete de datos minimo"
PAQ="$TRABAJO/paquete"
mkdir -p "$PAQ"
python3 - "$PAQ" "$RAIZ" <<'PY'
import base64, json, os, shutil, sqlite3, sys
d = sys.argv[1]
raiz = sys.argv[2]
# PNG de 1x1 pixel: basta para que la base de teselas sea valida.
png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")
db = os.path.join(d, "base.sqlitedb")
if os.path.exists(db):
    os.remove(db)
c = sqlite3.connect(db)
c.execute("CREATE TABLE tiles (version INTEGER, z INTEGER, x INTEGER, y INTEGER, image BLOB)")
c.execute("CREATE INDEX IND ON tiles (z, x, y)")
for z in range(0, 3):
    for x in range(2 ** z):
        for y in range(2 ** z):
            c.execute("INSERT INTO tiles VALUES (1,?,?,?,?)", (z, x, y, png))
c.commit(); c.close()
# Indice de lugares de prueba (12 lugares inventados del repo), si esta.
lugares = os.path.join(raiz, "tests", "data", "places", "lugares_prueba.sqlitedb")
places = {}
if os.path.exists(lugares):
    shutil.copy(lugares, os.path.join(d, "lugares.sqlitedb"))
    places = {"file": "lugares.sqlitedb"}
paquete = {
    "format": "libmapa-package", "version": 2,
    "package": {"id": "prueba", "name": "Paquete de prueba",
                "bounds": {"north": 85, "west": -180, "south": -85, "east": 180},
                "attribution": "Datos de prueba generados por probar_instalacion.sh"},
    "start": {"layer": "base", "center": [0, 0], "zoom": 1},
    "datasets": [{"id": "base", "displayName": "Base", "filePath": "base.sqlitedb",
                  "zFactor": 1, "zOffset": 0, "scheme": "XYZ",
                  "minZoom": 0, "maxZoom": 2, "baseZoom": 0, "hasSColumn": False}],
}
if places:
    paquete["places"] = places
json.dump(paquete, open(os.path.join(d, "mapa.json"), "w"), indent=2)
PY

# --- 3. Todos los ejemplos contra el prefijo ---------------------------
echo "== 3/4 Compilar los ejemplos contra $PREFIJO"
PREFIX_PATH="$PREFIJO${CMAKE_PREFIX_PATH_EXTRA:+;$CMAKE_PREFIX_PATH_EXTRA}"
cmake -S "$RAIZ/examples" -B "$TRABAJO/ejemplos" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PREFIX_PATH" >/dev/null || fallo "cmake no configura los ejemplos"
cmake --build "$TRABAJO/ejemplos" -j"$(nproc 2>/dev/null || echo 2)" >/dev/null \
    || fallo "los ejemplos no compilan contra la instalacion"

# --- 4. Funciona de verdad ---------------------------------------------
echo "== 4/4 Arrancar los ejemplos con --comprobar y check_data"
APP="$(find "$TRABAJO/ejemplos" -type f -name app_minima -perm -u+x | head -1)"
[ -n "$APP" ] || fallo "no se encuentra app_minima compilada"
"$APP" "$PAQ" --comprobar || fallo "app_minima --comprobar no sale con 0"
"$PREFIJO/bin/check_data" "$PAQ" --quick || fallo "check_data ve problemas en el paquete"

# visor_gpx lee su GPX de ejemplo: entidades, longitud y horas.
GPX_EJEMPLO="$RAIZ/examples/visor_gpx/ruta_ejemplo.gpx"
VISOR="$(find "$TRABAJO/ejemplos" -type f -name visor_gpx -perm -u+x | head -1)"
[ -n "$VISOR" ] || fallo "no se encuentra visor_gpx compilado"
"$VISOR" "$PAQ" "$GPX_EJEMPLO" --comprobar || fallo "visor_gpx --comprobar no sale con 0"

# editor_rutas crea una ruta, la guarda a GPX y la vuelve a leer: debe ser la misma.
EDITOR="$(find "$TRABAJO/ejemplos" -type f -name editor_rutas -perm -u+x | head -1)"
[ -n "$EDITOR" ] || fallo "no se encuentra editor_rutas compilado"
"$EDITOR" "$PAQ" --comprobar || fallo "editor_rutas --comprobar no sale con 0"

# buscador_lugares: busca en el indice de lugares y lee una coordenada MGRS.
BUSCADOR="$(find "$TRABAJO/ejemplos" -type f -name buscador_lugares -perm -u+x | head -1)"
[ -n "$BUSCADOR" ] || fallo "no se encuentra buscador_lugares compilado"
"$BUSCADOR" "$PAQ" --comprobar || fallo "buscador_lugares --comprobar no sale con 0"

echo "OK: la instalacion en $PREFIJO sirve a los ejemplos"
