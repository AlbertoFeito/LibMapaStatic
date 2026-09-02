#!/usr/bin/env bash
# ===================================================================
#  ejecutar_bench.sh  -  busca bench_tiles y lo lanza
#
#  Uso:
#     ./ejecutar_bench.sh
#     ./ejecutar_bench.sh /ruta/a/datasets.json
#
#  Genera bench15.txt y bench16.txt junto a este script.
# ===================================================================

set -u

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/../.." && pwd)"

EXE="$(find "$RAIZ" -type f -name bench_tiles -perm -u+x 2>/dev/null | head -1)"

if [ -z "$EXE" ]; then
    cat <<'EOF'

 ERROR: no se encontro el ejecutable bench_tiles.

 Probablemente no se ha compilado todavia. bench_tiles se anadio en la
 fase 3, asi que una carpeta de compilacion creada con un paquete
 anterior no lo incluye. Solucion:

     cmake -S . -B build
     cmake --build build -j4

EOF
    exit 1
fi

JSON="${1:-}"
if [ -z "$JSON" ] && [ -f "$RAIZ/datasets.json" ]; then
    JSON="$RAIZ/datasets.json"
fi
if [ -z "$JSON" ]; then
    JSON="$(find "$RAIZ" -type f -name datasets.json 2>/dev/null | head -1)"
fi

if [ -z "$JSON" ]; then
    echo " ERROR: no se encontro datasets.json."
    echo " Paselo como argumento: ./ejecutar_bench.sh /ruta/datasets.json"
    exit 1
fi

echo " Ejecutable: $EXE"
echo " Datasets  : $JSON"
echo

# Sin servidor grafico hace falta la plataforma offscreen.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

echo " Midiendo zoom 15 ..."
"$EXE" --datasets "$JSON" --zoom 15 --pans 40 > "$AQUI/bench15.txt" 2>&1

echo " Midiendo zoom 16 ..."
"$EXE" --datasets "$JSON" --zoom 16 --pans 40 > "$AQUI/bench16.txt" 2>&1

echo
echo " Listo. Ficheros generados:"
echo "    $AQUI/bench15.txt"
echo "    $AQUI/bench16.txt"
