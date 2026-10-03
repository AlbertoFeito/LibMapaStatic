# CLAUDE.md — convenciones del proyecto libmapa / LibMapaStatic

Guía para cualquier sesión de Claude Code que trabaje en este repo. Son las
reglas que ya venimos aplicando; respétalas siempre.

## Rama y commits
- **Trabaja SIEMPRE en la rama `claude/sharp-goodall-dt7hh5`.** Nunca commitees a
  `main`. Si el checkout arranca en `main` (contenedor nuevo), recupera la rama:
  `git fetch origin claude/sharp-goodall-dt7hh5 && git reset --hard origin/claude/sharp-goodall-dt7hh5`.
- Antes de commitear, si puede haber otra sesión, `git fetch` y ponte al día.
- Mensajes de commit en español, claros, explicando el porqué.
- **Cierra cada commit** con estas dos líneas (atribución):
  ```
  Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01X8UgEjA9yqurw5eZkjXYF1
  ```
- **Nunca** pongas un identificador de modelo en commits, PRs, comentarios de
  código ni ningún artefacto del repo. Solo en el chat.
- `git push -u origin claude/sharp-goodall-dt7hh5`; si el push da un 503
  transitorio del servicio de credenciales, reintenta con backoff (2,4,8,16 s).

## Estilo de código
- C++17. Sigue el estilo del fichero que edites (nombres, densidad de comentarios).
- **Comentario `//` en español ENCIMA de CADA función**, explicando qué hace y,
  cuando importa, por qué (no repetir lo obvio de la firma). Es la norma de todo
  el código; el header documenta el API público, el `.cpp` la implementación.
- Código nuevo = código documentado así desde el primer commit.

## Qt y compilador
- Objetivo: **Qt 5.14 / 5.15 / 6.x** (MinGW, MSVC, GCC). **Nada de Qt 5.7.**
- No uses flags específicos de compilador frágiles (p.ej. `-Wnull-dereference`):
  dan falsos positivos dentro de las cabeceras de Qt/QCustomPlot.
- Qt5 solo declara `QVariant` en `qsqlquery.h`: incluye `<QVariant>` explícito
  donde uses `bindValue`.

## QCustomPlot
- Es GPLv3 y **está en `.gitignore`**: no se commitea. Versión usada: **2.1.1**.
- Si falta (`third_party/qcustomplot/qcustomplot.{h,cpp}`), descárgala:
  `curl -fsSL https://www.qcustomplot.com/release/2.1.1/QCustomPlot-source.tar.gz`
  y copia `qcustomplot.h` y `qcustomplot.cpp` a `third_party/qcustomplot/`.
- Sin QCustomPlot, el núcleo (`libmapa_core`) compila y pasa sus tests; se queda
  fuera el widget, `fill_map`, `demo` y `render_map`.

## miniz (descompresor gzip de `fill_hgt`)
- `third_party/miniz/miniz.{c,h}` es **dominio público** y **SÍ se commitea**
  (al revés que QCustomPlot). Lo usa solo `fill_hgt` para descomprimir `.hgt.gz`
  sin depender de zlib externa → compila en cualquier sitio (incl. Qt MinGW).
- Por eso `project()` habilita **C** además de CXX; a `miniz.c` se le pone `-w`
  (es de terceros) y los avisos solo-C++ se limitan a CXX con generator expressions.

## Arquitectura (respétala)
- **Fachada:** `MapWidget` (API pública en `include/libmapa/`) oculta QCustomPlot.
  Una capa de dibujo nueva es un `QCPLayerable` que vive en `src/widget/` y se
  expone por `MapWidget`, como `TileLayer`/`FeatureLayer`/`TargetLayer`/`CoverageLayer`.
- **Teselas:** una sola fuente de verdad en `geo/TileMatrix`. Codificación por
  base: `storedZ = zFactor·z + zOffset`, esquema XYZ/TMS (`toStorageY`/`fromStorageY`),
  columna opcional `s`. Todo descrito en `datasets.json`; `probe_db` lo detecta.
- **SQLite:** conexión por hilo vía `SqliteConnectionPool` (las conexiones no se
  comparten entre hilos). Lectura concurrente con `busy_timeout`.

## Descarga de teselas
- Motor común `tools/common/TileFiller` para consola (`fill_tiles`) y ventana
  (`fill_map`). Reanudable (solo baja lo que falta), con auto-freno ante
  limitación de la fuente y diagnóstico TLS.
- Fuente por defecto: **Esri "Clarity"**, sin API key. Respeta los términos de
  uso de cada servidor. **Nunca** descargues masivamente del OSM oficial
  (`tile.openstreetmap.org`): está prohibido.

## Verificación antes de commitear código
- Compila y pasa **todos los tests** (15 ahora): `QT_QPA_PLATFORM=offscreen ctest` (sin pantalla
  en el contenedor). Un push verde vale más que tres especulativos.
- Contenedor nuevo sin Qt: `sudo apt-get update && sudo apt-get install -y
  qt6-base-dev qt6-positioning-dev`, y recupera QCustomPlot (arriba).
- Cambios solo de documentación (`.md`, `.html`, skills) no necesitan build.

## Documentación (obligatoria con cada feature)
- Registra lo hecho en **`docs/BITACORA.md`** (secciones numeradas, en orden
  cronológico: añade la siguiente `## N. ...`). Explica el porqué, no solo el qué.
- Si cambia la arquitectura o el API, actualiza **`docs/arquitectura.html`** y
  **regenera el PDF** `docs/LibMapaStatic_Documentacion.pdf` con Chromium headless:
  `<chrome> --headless --no-sandbox --disable-gpu --no-pdf-header-footer
  --print-to-pdf=docs/LibMapaStatic_Documentacion.pdf docs/arquitectura.html`
  (Chromium está en `/opt/pw-browsers/`).
- Mantén el **README** al día (nº de tests, herramientas, estado).

## Entorno
- Contenedor en la nube, efímero: lo no commiteado+pusheado se pierde.
- Salida HTTPS por proxy preconfigurado.
