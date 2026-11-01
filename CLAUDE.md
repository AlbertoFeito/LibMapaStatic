# CLAUDE.md — convenciones del proyecto libmapa / LibMapaStatic

Guía para cualquier sesión de Claude Code que trabaje en este repo. Son las
reglas que ya venimos aplicando; respétalas siempre.

## Rama y commits
- **Una rama por tarea, nunca `main`.** Cada trabajo nuevo sale de `main` actualizado
  en una rama `claude/<tema>` (p. ej. `claude/app-medidor-campo`):
  `git fetch origin main && git checkout -B claude/<tema> origin/main`.
  Si la sesión ya trae una rama asignada, esa es la que se usa. No se reutiliza una
  rama cuyo PR ya se fusionó: el trabajo siguiente es una rama nueva desde `main`.
- Cada rama acaba en un **PR en borrador contra `main`**; el usuario lo prueba, lo marca
  como listo y lo fusiona. Claude **nunca fusiona**. Evita apilar una rama sobre otra
  sin fusionar; si hace falta, dilo en el PR con el orden de fusión (así se hizo en #18 a #21).
- **Registro de ramas:** al abrir una rama nueva, añade su línea a la tabla de
  `docs/RAMAS.md` (nombre, PR y para qué sirve); al fusionarse, no se borra.
- Antes de commitear, si puede haber otra sesión, `git fetch` y ponte al día.
- Mensajes de commit en español, claros, explicando el porqué.
- **Cierra cada commit** con la línea de atribución
  `Co-Authored-By: Claude <modelo> <noreply@anthropic.com>`, poniendo el nombre
  del modelo con el que trabaja la sesión. Las sesiones en la nube
  añaden además su línea `Claude-Session: <url de la sesión>`.
- Fuera de esa línea final del commit, **nunca** pongas un identificador de
  modelo en commits, PRs, comentarios de código ni ningún artefacto del repo.
- `git push -u origin claude/<tema>`; si el push da un 503
  transitorio del servicio de credenciales, reintenta con backoff (2,4,8,16 s).

## Ramas y PR
El registro de ramas está en **`docs/RAMAS.md`** (ya no va aquí, para que `CLAUDE.md` cargue ligero).
Ábrelo solo para añadir la línea de la rama nueva (al final de la tabla); no hace falta leerlo entero.

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
- **Dos juegos de despliegue** (guía en `docs/DESPLIEGUE.md`): Qt 6 con
  `herramientas/desplegar.bat`, y Qt 5 con `herramientas/desplegar_qt5.bat`.
  No mezcles uno con otro ni cambies el de Qt 6 para cubrir Qt 5.
- La librería se compila también **con qmake** (`qmake/libmapa/core|widget`).
  Si añades o quitas un `.cpp` (o una cabecera con `Q_OBJECT`) en
  `CMakeLists.txt`, haz lo mismo en `core.pro` o `widget.pro`. Lo mismo con
  `libtelemetria/CMakeLists.txt` y `qmake/libtelemetria/telemetria.pro`.

## QCustomPlot
- Es GPLv3 y **está en `.gitignore`**: no se commitea. Versión usada: **2.1.1**.
- Si falta (`third_party/qcustomplot/qcustomplot.{h,cpp}`), descárgala:
  `curl -fsSL https://www.qcustomplot.com/release/2.1.1/QCustomPlot-source.tar.gz`
  y copia `qcustomplot.h` y `qcustomplot.cpp` a `third_party/qcustomplot/`.
- Sin QCustomPlot, el núcleo (`libmapa_core`) compila y pasa sus tests; se queda
  fuera el widget, `fill_map`, `demo` y `render_map`.

## miniz (descompresor gzip de `fill_hgt` y de las teselas vectoriales)
- `third_party/miniz/miniz.{c,h}` es **dominio público** y **SÍ se commitea**
  (al revés que QCustomPlot). Lo usan `fill_hgt` (`.hgt.gz`) y el núcleo
  (`src/vector/Gzip.cpp`, teselas MVT en gzip de los MBTiles vectoriales), sin
  depender de zlib externa → compila en cualquier sitio (incl. Qt MinGW). En el
  núcleo va dentro de `libmapa_core` (CMake y `core.pro`), con su cabecera privada.
- Por eso `project()` habilita **C** además de CXX; a `miniz.c` se le pone `-w`
  (es de terceros) y los avisos solo-C++ se limitan a CXX con generator expressions.

## libtelemetria (biblioteca aparte)
- Decodificador/codificador de **NMEA 0183 y AIS** en `libtelemetria/` (namespace `telemetria`, target
  `telemetria::telemetria`). **Solo Qt Core**: ni red, ni Gui, ni mapa, ni E/S (se le dan bytes y devuelve datos);
  su `CMakeLists.txt` se detiene si alguien le añade esas dependencias. La red y el mapa los pone la aplicación.
- Se instala en el **mismo prefijo** que libmapa (`find_package(libtelemetria)`; con qmake, `telemetria.pri` junto a
  `libmapa.pri`; `qmake/libmapa/libmapa.pro` compila e instala las dos).
- Todo decodificador nuevo se prueba con un **dato publicado por una fuente independiente** (una frase con su suma de
  comprobación, un mensaje AIS de la documentación de gpsd...) además de con la ida y vuelta del codificador propio, que
  no demuestra nada por sí sola.

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
- Compila y pasa **todos los tests** (33 ahora, 3 de ellos de libtelemetria; 30 sin QCustomPlot): `QT_QPA_PLATFORM=offscreen ctest` (sin pantalla
  en el contenedor). Un push verde vale más que tres especulativos.
- Contenedor nuevo sin Qt: **`herramientas/preparar_sesion.sh`** instala Qt 6 por apt
  (`qt6-base-dev qt6-positioning-dev`) y recupera QCustomPlot (arriba); es repetible.
  Con `--qt5` instala además Qt 5.14.2 con aqtinstall en `~/Qt` (compila contra
  `-DCMAKE_PREFIX_PATH=~/Qt/5.14.2/gcc_64 -DCMAKE_CXX_FLAGS='-include limits'`; lo segundo evita el fallo de `qfloat16.h` con GCC moderno). Necesita que la sesión permita los dominios
  `www.qcustomplot.com` (QCustomPlot) y `download.qt.io` (solo `--qt5`): se añaden en
  claude.ai > Settings > Capabilities, y puede hacer falta una sesión nueva.
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
- **Ideas aparcadas** (habladas con el usuario y dejadas para otro momento):
  `docs/PENDIENTES.md`. Míralo antes de proponer trabajo nuevo; al hacer una, bórrala
  de ahí y cuéntala en la bitácora.

## Entorno
Se trabaja en dos sitios; comprueba en cuál estás antes de seguir las
instrucciones de build y PDF.

**Contenedor en la nube** (Linux)
- Efímero: lo no commiteado+pusheado se pierde.
- Salida HTTPS por proxy preconfigurado.
- Las instrucciones de arriba (`apt-get`, `/opt/pw-browsers/`, `offscreen`)
  son para este entorno.

**PC local** (Windows 10, `D:\QtPro\LibMapaStatic`)
- Qt **6.11.2 MinGW 64** (`C:/Qt/6.11.2/mingw_64`, compilador
  `C:/Qt/Tools/mingw1310_64`), proyecto abierto en **Qt Creator**.
- Builds de Qt Creator en `build/Desktop_Qt_6_11_2_MinGW_64_bit_{Debug,Release}`.
  QCustomPlot ya está copiada en `third_party/qcustomplot/` (gitignored).
- Tests: con `C:/Qt/6.11.2/mingw_64/bin` y `C:/Qt/Tools/mingw1310_64/bin` en el
  `PATH` (para las DLL), `ctest` dentro de la carpeta de build (no hace falta
  `offscreen`: hay pantalla).
- PDF con el Chrome de Windows:
  `"C:\Program Files\Google\Chrome\Application\chrome.exe" --headless --disable-gpu
  --no-pdf-header-footer --print-to-pdf=docs\LibMapaStatic_Documentacion.pdf
  docs\arquitectura.html` (ruta absoluta en `--print-to-pdf` si falla).
- Aquí no se pierde nada al cerrar, pero haz igualmente commit+push: la nube
  parte del remoto.
