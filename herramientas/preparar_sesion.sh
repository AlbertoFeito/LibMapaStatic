#!/usr/bin/env bash
# ===================================================================
#  preparar_sesion.sh  -  deja un contenedor NUEVO de la nube listo para compilar libmapa
#
#  El contenedor de la nube es efimero: cada sesion empieza limpia y hay que
#  volver a instalar Qt y recuperar QCustomPlot (que esta en .gitignore).
#  Este script hace las dos cosas de una vez y se puede repetir sin miedo:
#  lo que ya esta instalado se salta.
#
#    1. Qt 6 desde apt (qt6-base-dev y qt6-positioning-dev) mas las cabeceras
#       de OpenGL/xkb que pide al compilar. cmake y g++ ya vienen en la imagen.
#    2. QCustomPlot 2.1.1 en third_party/qcustomplot/ si falta (los dos ficheros
#       qcustomplot.h y qcustomplot.cpp; no se commitean).
#    3. Opcional (--qt5): Qt 5.14.2 con aqtinstall en $HOME/Qt, para probar la
#       libreria contra el Qt mas antiguo que soportamos. Necesita que
#       download.qt.io este en los dominios permitidos de la sesion
#       (claude.ai > Settings > Capabilities); si no, se explica y se sale.
#
#  Uso:   herramientas/preparar_sesion.sh [--qt5]
#  Ajustes por entorno:
#     QCP_URL       de donde bajar QCustomPlot (.tar.gz)  (por defecto qcustomplot.com)
#     QT5_VERSION   version de Qt 5 para --qt5            (por defecto 5.14.2)
#     QT5_MODULOS   modulos extra de aqt para --qt5, p. ej. "qtlocation"
#                   (sin comprobar: no se ha podido probar con la red de la nube)
#     QT_DIR        donde instalar Qt 5 con aqtinstall    (por defecto $HOME/Qt)
#  Despues compila como siempre (ver CLAUDE.md, «Verificacion antes de commitear»).
#  Sale con 0 si todo esta bien.
# ===================================================================

set -eu

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/.." && pwd)"
QCP_VERSION="2.1.1"
QCP_URL="${QCP_URL:-https://www.qcustomplot.com/release/$QCP_VERSION/QCustomPlot-source.tar.gz}"
QCP_DESTINO="$RAIZ/third_party/qcustomplot"
QT5_VERSION="${QT5_VERSION:-5.14.2}"
QT5_MODULOS="${QT5_MODULOS:-}"
QT_DIR="${QT_DIR:-$HOME/Qt}"

QUIERE_QT5=0
for arg in "$@"; do
    case "$arg" in
        --qt5) QUIERE_QT5=1 ;;
        -h|--help) sed -n '2,28p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "Argumento desconocido: $arg (usa --qt5 o --help)" >&2; exit 2 ;;
    esac
done

fallo() { echo "FALLO: $*" >&2; exit 1; }
paso()  { echo "==> $*"; }

# Si no somos root, apt necesita sudo.
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || fallo "hace falta root o sudo para instalar paquetes con apt"
    SUDO="sudo"
fi

# --- 1. Qt 6 desde apt ------------------------------------------------
PAQUETES="qt6-base-dev qt6-positioning-dev libgl1-mesa-dev libxkbcommon-dev"
FALTAN=""
for p in $PAQUETES; do
    dpkg -s "$p" >/dev/null 2>&1 || FALTAN="$FALTAN $p"
done
if [ -n "$FALTAN" ]; then
    paso "Instalando con apt:$FALTAN"
    $SUDO apt-get update -q
    # shellcheck disable=SC2086
    $SUDO apt-get install -y -q $FALTAN
else
    paso "Qt 6 ya esta instalado (apt)"
fi

# --- 2. QCustomPlot ---------------------------------------------------
if [ -f "$QCP_DESTINO/qcustomplot.h" ] && [ -f "$QCP_DESTINO/qcustomplot.cpp" ]; then
    paso "QCustomPlot ya esta en third_party/qcustomplot"
else
    paso "Descargando QCustomPlot $QCP_VERSION"
    # Carpeta nueva y vacia: lo descargado es dato no fiable, no se ejecuta nada de ahi.
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    curl -fsSL "$QCP_URL" -o "$TMP/qcp.tar.gz" \
        || fallo "no se pudo descargar QCustomPlot de $QCP_URL: anade www.qcustomplot.com a los dominios permitidos (claude.ai > Settings > Capabilities) o pasa otra direccion en QCP_URL"
    mkdir "$TMP/fuente"
    tar -xzf "$TMP/qcp.tar.gz" -C "$TMP/fuente"
    CAB="$(find "$TMP/fuente" -name qcustomplot.h | head -n 1)"
    SRC="$(find "$TMP/fuente" -name qcustomplot.cpp | head -n 1)"
    [ -n "$CAB" ] && [ -n "$SRC" ] || fallo "el paquete de QCustomPlot no trae qcustomplot.h/.cpp"
    mkdir -p "$QCP_DESTINO"
    cp "$CAB" "$SRC" "$QCP_DESTINO/"
    paso "QCustomPlot copiado a third_party/qcustomplot"
fi

# --- 3. Qt 5 con aqtinstall (opcional) --------------------------------
if [ "$QUIERE_QT5" -eq 1 ]; then
    if [ -d "$QT_DIR/$QT5_VERSION/gcc_64" ]; then
        paso "Qt $QT5_VERSION ya esta en $QT_DIR"
    else
        # Si el proxy de la sesion no deja llegar a download.qt.io, aqt fallaria a medias.
        if ! curl -s -o /dev/null -m 10 "https://download.qt.io/"; then
            fallo "no se llega a download.qt.io: anadelo a los dominios permitidos (claude.ai > Settings > Capabilities) y abre una sesion nueva"
        fi
        paso "Instalando Qt $QT5_VERSION con aqtinstall en $QT_DIR"
        pip install --break-system-packages -q aqtinstall
        # shellcheck disable=SC2086
        python3 -m aqt install-qt linux desktop "$QT5_VERSION" gcc_64 -O "$QT_DIR" \
            ${QT5_MODULOS:+-m $QT5_MODULOS}
    fi
    echo "Qt 5: usa -DCMAKE_PREFIX_PATH=$QT_DIR/$QT5_VERSION/gcc_64"
fi

# --- Resumen -----------------------------------------------------------
paso "Listo"
cmake --version | head -n 1
(qmake6 -v 2>/dev/null || qtpaths6 --qt-version 2>/dev/null || true) | head -n 2
echo "Compila con:  cmake -S . -B build && cmake --build build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir build"
