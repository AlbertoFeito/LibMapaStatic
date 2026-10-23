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
  «Ramas y PR» de más abajo (nombre, PR y para qué sirve); al fusionarse, no se borra.
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
Historial de las ramas de trabajo (fusionadas en `main`, salvo la última si su PR sigue abierto). Añade aquí la siguiente.

| Rama | PR | Para qué |
|---|---|---|
| `refactor-libmapa` | #1 | Refactor de libmapa |
| `claude/sharp-goodall-dt7hh5` | #2–#7 | Elevación, descarga de teselas, paquete de datos sin conexión, seguimiento de objetivos, análisis de elevación y demo |
| `claude/fervent-mccarthy-v9rl6d` | #8–#14 | Alertas, anillos y marcaciones, medir superficie, GeoTIFF/MBTiles, búsqueda de lugares, capa base en caliente (rama larga anterior) |
| `claude/apps-ejemplo` | #15, #17 | Contrato de instalación (`probar_instalacion.sh`, `examples/` en bloque, CI); #17 lleva a `main` lo de #16 |
| `claude/etiquetas-arriba` | #16 | Rótulos encima de todo (`LabelLayer`) y rótulo de marcación movible |
| `claude/app-visor-gpx` | #18 | Ejemplo `visor_gpx` |
| `claude/app-editor-rutas` | #19 | Ejemplo `editor_rutas` |
| `claude/app-buscador-lugares` | #20 | Ejemplo `buscador_lugares` |
| `claude/overlays-fijos` | #21 | Capas fijas del paquete ocultas y no seleccionables |
| `claude/app-medidor-campo` | #22 | Ejemplo `medidor_campo` |
| `claude/claude-md-ramas` | #23 | Regla de ramas y registro en `CLAUDE.md` |
| `claude/app-monitor-trafico` | #24 | Ejemplo `monitor_trafico` (tabla de contactos, perdidos, alertas y glosario) |
| `claude/app-vigilancia-zonas` | #25 | Ejemplo `vigilancia_zonas` (zonas con reglas de aviso, historial de eventos, CSV y GeoJSON) |
| `claude/app-analisis-terreno` | #26 | Ejemplo `analisis_terreno` (línea de vista, perfil, zona visible y terreno de demostración) |
| `claude/app-cliente-telemetria` | #27 | `libtelemetria` (biblioteca aparte: decodificador NMEA 0183 y AIS) y los ejemplos `emisor_nmea` y `cliente_telemetria` |
| `claude/docs-quitar-mundo` | #28 | Quita de `docs/MIS_RUTAS.md` la nota pendiente de la capa `mundo` |
| `claude/cliente-tcp-serie` | #29 | `cliente_telemetria` por TCP (cliente, con reconexión) y por puerto serie (opcional); `emisor_nmea --tcp` |
| `claude/sweet-cray-74nz88` | #30 | MBTiles vectoriales (pbf/MVT): `src/vector/` (gzip, decodificador, estilo, renderizador), `vector_tile` |
| `claude/sweet-cray-74nz88` (otra vez, desde `main`) | #31 | Mapa híbrido: el MBTiles vectorial encima de cualquier base, nombres de calles y capas conmutables |
| `claude/stoic-clarke-rn7r7f` | #32 | «¿Qué hay aquí?»: `MapWidget::vectorFeaturesAt`, ventana y tarjeta de la demo y `vector_tile --at` |
| `claude/stoic-clarke-rn7r7f` (otra vez, desde `main`) | #33 | Puntos de interés por tema en los estilos por defecto (cinco grupos apagados, con `pointShape`) |
| `claude/stoic-clarke-rn7r7f` (y otra vez, desde `main`) | #34 | Buscar sitios (POI) por nombre o tipo: `MapWidget::searchPointsOfInterest` y el buscador de la demo |
| `claude/stoic-clarke-rn7r7f` (y otra más, desde `main`) | #35 | Sacar los POI de una zona como entidades: `MapWidget::importPointsOfInterest` y «Sitios de la zona…» en la demo |
| `claude/buscar-calles` | #36 | Búsqueda de calles sin conexión: `MapWidget::searchStreets`, resaltado en la demo |
| `claude/rutas-sin-conexion` | #37 | Rutas en coche sin conexión por la red vial de las teselas: `MapWidget::findRoute`, «Ruta (coche)» en la demo y `vector_tile --route` |
| `claude/app-rutas-tierra` | #38 | Ejemplo `rutas_tierra` (rutas por tierra con caminos de tierra y red de demostración) y `RouteResult::legs` |
| `claude/inspiring-wright-u0smtv` | #41 | Los 27 tipos AIS en `libtelemetria` (aportación del fork `randoloc`), catálogo de tipos en `emisor_nmea` y estaciones fijas en `cliente_telemetria` |
| `claude/vigilar-randy` | (borrador) | `herramientas/vigilar_randy.sh`: avisa cuando cambia `docs/Randy.md` en el remoto (para correr dos veces al día) |

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
- Compila y pasa **todos los tests** (31 ahora, 3 de ellos de libtelemetria; 28 sin QCustomPlot): `QT_QPA_PLATFORM=offscreen ctest` (sin pantalla
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
