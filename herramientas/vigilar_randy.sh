#!/usr/bin/env bash
# ===================================================================
#  vigilar_randy.sh  -  avisa cuando docs/Randy.md cambia en el remoto
#
#  Trae el remoto y compara la version de docs/Randy.md que hay en su rama
#  (por omision origin/main) con la ultima que este script vio. Si cambio,
#  enseña los commits que lo tocaron y las diferencias, y guarda la nueva
#  version como la vista. No toca tu rama ni tu arbol de trabajo: solo hace
#  `git fetch` y lee del repositorio.
#
#  Que "ultima vista" se guarda en .git/vigilar_randy.estado (no se commitea).
#  La primera vez solo la apunta y no avisa de nada.
#
#  Uso:    herramientas/vigilar_randy.sh [--remoto=origin] [--rama=main]
#                                        [--fichero=docs/Randy.md] [--sin-guardar]
#    --sin-guardar   mira pero no apunta la version: sirve para probar, o para
#                    seguir viendo el aviso hasta que lo hayas leido.
#  Sale con:  0 sin cambios (o primera vez),  10 el fichero cambio,
#             11 el fichero ya no existe en el remoto,  1 error (sin red, etc.).
#
#  Dos veces al dia (a las 9:00 y a las 18:00). Cambia /ruta/al/repo:
#    Linux/macOS (crontab -e):
#      0 9,18 * * *  cd /ruta/al/repo && herramientas/vigilar_randy.sh >> .git/vigilar_randy.log 2>&1
#    Windows (Programador de tareas, con Git Bash instalado; una tarea a las 9:00 y otra a las 18:00):
#      "C:\Program Files\Git\bin\bash.exe" -lc "cd /d/QtPro/LibMapaStatic && herramientas/vigilar_randy.sh"
#  Con cron, el codigo 10 no avisa por si solo: lee .git/vigilar_randy.log, o encadena
#  tu aviso (correo, notificacion...) tras `herramientas/vigilar_randy.sh || ...`.
# ===================================================================
set -u

REMOTO=origin
RAMA=main
FICHERO=docs/Randy.md
GUARDAR=1
for a in "$@"; do
    case "$a" in
        --remoto=*)     REMOTO="${a#*=}" ;;
        --rama=*)       RAMA="${a#*=}" ;;
        --fichero=*)    FICHERO="${a#*=}" ;;
        --sin-guardar)  GUARDAR=0 ;;
        -h|--help)      sed -n '2,28p' "$0"; exit 0 ;;
        *) echo "opcion desconocida: $a (usa --help)" >&2; exit 1 ;;
    esac
done

# Siempre desde la raiz del repositorio, ejecutes el script desde donde lo ejecutes.
RAIZ="$(git -C "$(dirname "$0")" rev-parse --show-toplevel 2>/dev/null)" || { echo "esto no es un repositorio git" >&2; exit 1; }
cd "$RAIZ" || exit 1
ESTADO="$(git rev-parse --git-dir)/vigilar_randy.estado"
AHORA="$(date '+%Y-%m-%d %H:%M')"

# --- 1. Traer el remoto (solo la rama que se vigila) -------------------
if ! git fetch --quiet "$REMOTO" "$RAMA" 2>/dev/null; then
    echo "[$AHORA] ERROR: no se pudo traer $REMOTO/$RAMA (¿sin red?)" >&2
    exit 1
fi
REF="FETCH_HEAD"

# --- 2. La version actual del fichero en el remoto ---------------------
if ! NUEVA="$(git rev-parse --verify --quiet "$REF:$FICHERO")"; then
    echo "[$AHORA] $FICHERO ya no existe en $REMOTO/$RAMA"
    exit 11
fi

# --- 3. Compararla con la ultima vista ---------------------------------
if [ ! -f "$ESTADO" ]; then
    [ "$GUARDAR" = 1 ] && echo "$NUEVA" > "$ESTADO"
    echo "[$AHORA] primera vez: $FICHERO apuntado ($NUEVA) en $REMOTO/$RAMA; desde ahora avisare de sus cambios"
    exit 0
fi
VISTA="$(cat "$ESTADO")"
if [ "$NUEVA" = "$VISTA" ]; then
    echo "[$AHORA] sin cambios en $FICHERO ($REMOTO/$RAMA)"
    exit 0
fi

# --- 4. Cambio: que commits lo tocaron y que cambio --------------------
echo "[$AHORA] $FICHERO HA CAMBIADO en $REMOTO/$RAMA"
echo "  antes: $VISTA"
echo "  ahora: $NUEVA"
echo
echo "Commits que lo tocaron (los mas recientes primero):"
git log --format='  %h %ad %an: %s' --date=short -n 10 "$REF" -- "$FICHERO"
echo
if git cat-file -e "$VISTA" 2>/dev/null; then
    echo "Diferencias (+ lo añadido, - lo quitado):"
    git diff --no-color --unified=2 "$VISTA" "$NUEVA" | tail -n +5
else
    # La version anterior ya no esta en el repositorio local (se limpio): se enseña la nueva.
    echo "(la version anterior ya no esta en este clon; se muestra el fichero entero)"
    git show "$NUEVA"
fi

[ "$GUARDAR" = 1 ] && echo "$NUEVA" > "$ESTADO"
exit 10
