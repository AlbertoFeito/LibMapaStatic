# libmapa

[![CI](https://github.com/AlbertoFeito/LibMapaStatic/actions/workflows/ci.yml/badge.svg)](https://github.com/AlbertoFeito/LibMapaStatic/actions/workflows/ci.yml)

Librería Qt que dibuja mapas de teselas guardadas en SQLite, embebible como un
`QWidget` corriente. Refactorización de **LibMapaStatic**, la librería de mapas
de EstacionTerrena3.

Alterna entre cartografía **OSM** y **satelital** en caliente, lee las teselas
en un hilo aparte y rellena los huecos con teselas de nivel superior escaladas.

- Qt 5.14 / 5.15 / 6.x, MinGW / MSVC / GCC
- QCustomPlot como motor de dibujo, encapsulado: **no aparece en la cabecera pública**
- **Sin conexión**: todos los datos (teselas, elevación, capas fijas) van en un **paquete de datos** local, una carpeta con su manifiesto `mapa.json`. Internet solo se usa en las herramientas que preparan ese paquete
- 17 tests (14 sin QCustomPlot), sin avisos del compilador con `-Wall -Wextra -Wconversion -Wold-style-cast`
- Descarga las teselas que faltan de una fuente XYZ sin clave (`fill_tiles` / `fill_map`), reanudable, en paralelo, por rectángulo o polígono y con estimación de tamaño
- Elevación del terreno desde ficheros SRTM `.hgt` **o** una base de datos `.sqlitedb` empaquetable (cota bajo el cursor en `fill_map`)
- Entidades (puntos/líneas/polígonos) con **persistencia automática**: `MapConfig.featuresDbFile` guarda lo dibujado y lo recarga al abrir
- **Seguimiento de objetivos móviles** en tiempo real, **agnóstico del dominio**: cada objetivo lleva `kind` y un juego de `attributes` libres (AIS, ADS-B, telemetría…) que la app rellena y la librería no interpreta → vale igual para seguimiento naval, aéreo o de UAVs. **Simbología por hooks**: la app registra su juego de iconos (`setTargetSymbolProvider`) y la librería los coloca y los gira por el rumbo. **Escala a miles** (ADS-B regional) con culling por vista, nivel de detalle (presupuesto de etiquetas/trazas), declutter de etiquetas y decimación de traza

## Uso

```cpp
#include <libmapa/MapWidget.h>

libmapa::MapConfig cfg;
cfg.dataDir = QCoreApplication::applicationDirPath() + "/datos";   // carpeta con mapa.json

auto *mapa = new libmapa::MapWidget(cfg, this);
ui->contenedor->layout()->addWidget(mapa);

connect(botonSatelital, &QPushButton::clicked, mapa, [mapa]{
    mapa->setBaseLayerId("satelital");
});

connect(mapa, &libmapa::MapWidget::mouseMoved,
        this, [](const QGeoCoordinate &p){ /* ... */ });
```

Con esa línea de `dataDir` salen las capas base, la elevación, las capas fijas,
la BD de entidades del usuario y el punto de arranque. Lo que se rellene a mano
en `MapConfig` (`initialZoom`, `elevationDbFile`…) **manda sobre el paquete**.
La configuración clásica con `datasetsFile` sigue funcionando.

## El paquete de datos

Una carpeta con todo lo que el mapa necesita sin conexión y un manifiesto
`mapa.json` con **rutas relativas** a esa carpeta: se copia tal cual a otro PC
o junto a la aplicación. Plantilla: [`mapa.example.json`](mapa.example.json).

```json
{
  "format": "libmapa-package", "version": 2,
  "package":  { "id": "cuba", "name": "Cuba", "dataVersion": "2026.10",
                "bounds": { "north": 23.3, "west": -85.0, "south": 19.7, "east": -74.0 },
                "attribution": "© colaboradores de OpenStreetMap · …" },
  "start":    { "layer": "osm", "center": [21.5, -79.5], "zoom": 7 },
  "datasets": [ { "id": "osm", "filePath": "Cuba_OSM_CID3.sqlitedb", … }, … ],
  "elevation":{ "file": "cuba_dem.sqlitedb" },
  "overlays": [ { "id": "aguas", "file": "Aguas.geo", "style": { "lineColor": "#1565c0" } } ],
  "features": { "file": "entidades.db", "seed": "entidades_iniciales.db" }
}
```

| Bloque | Para qué |
|---|---|
| `package` | Qué es: id, nombre, versión de los datos, zona y **atribución** (`MapWidget::packageInfo()`) |
| `start` | Capa, centro y zoom de arranque |
| `datasets` | Las capas base, igual que en `datasets.json` (los campos omitidos toman su valor por defecto) |
| `elevation` | `"file"` (BD `.sqlitedb`) o `"dir"` (carpeta de `.hgt`) |
| `overlays` | Capas vectoriales **fijas** `.geo`: se cargan al abrir, bloqueadas, y **no** se guardan con las del usuario |
| `features` | BD de entidades del usuario. Una ruta relativa va a la carpeta de datos de la aplicación (`AppData/<app>/<package.id>/`), **no** a la del paquete, que puede ser de solo lectura. `seed` se copia ahí la primera vez |

Un fichero que falte (una capa, la elevación) no impide abrir el resto. Al
abrir, el widget hace una **comprobación rápida** (milisegundos) de lo que
dejaría el mapa en blanco sin explicación —un fichero que falta, una base que
no abre, imágenes que no se pueden decodificar porque falta el plugin de Qt— y
lo deja en `MapWidget::dataWarnings()` para que la app avise.

**Antes de distribuir**, `check_data` revisa el paquete entero: ficheros,
apertura, decodificación, zona y atribución, rutas fuera de la carpeta, y la
**cobertura por zoom** dentro de la zona. Sale con 1 si hay errores, para
usarlo en un script:

```bash
check_data Recursos              # informe completo (~0,2 s con 7 GB de datos)
check_data Recursos --quick      # sin cobertura por zoom
check_data Recursos --strict     # también falla con avisos
```

`probe_db --package` genera el manifiesto de partida:

```bash
probe_db --package --out Recursos/mapa.json --ref-bbox 23.3,-85.0,19.7,-74.0 \
         --id osm --file Recursos/Cuba_OSM_CID3.sqlitedb --name "Open Street Map" \
         --id satelital --file Recursos/Cuba_Satelital_CID3.sqlitedb --name "Satelital" \
         --dem Recursos/cuba_dem.sqlitedb --overlay Recursos/Aguas.geo
```

## Compilar

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/a/Qt
cmake --build build -j4
cd build && ctest
```

En Qt Creator basta con abrir el `CMakeLists.txt`.

### Usarla desde otra aplicación

> **Guía paso a paso, sin conocimientos previos:
> [`docs/DESPLIEGUE.md`](docs/DESPLIEGUE.md)**: compilar la librería,
> integrarla en una app qmake y llevarla a otro PC, con Qt 6 o con Qt 5.14.

**Con qmake (Qt 5.14 / 5.15 / 6.x, sin CMake):** `qmake/libmapa/libmapa.pro`
compila la librería (Release y Debug) y `make install` la deja en
`C:/libmapa/qt5` o `C:/libmapa/qt6` según el Qt. En el `.pro` de la app basta
una línea, que además elige la instalación del Qt del kit:

```qmake
include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)
```

`libmapa.pri` se para con un mensaje claro si la app y la librería son de Qt
distintos. Plantilla: [`examples/app_minima/app_minima.pro`](examples/app_minima/app_minima.pro).

**Con CMake:**

```bash
cmake --install build --prefix C:/libmapa
```

Instala las dos bibliotecas (estáticas: no hay DLL propia que repartir), las
cabeceras públicas, la configuración de CMake y las herramientas. En la app:

```cmake
find_package(libmapa REQUIRED)        # -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
target_link_libraries(miapp PRIVATE libmapa::widget)
```

[`examples/app_minima`](examples/app_minima) es la plantilla completa: una app
que solo usa `MapConfig::dataDir`. Con `add_subdirectory` también valen los
nombres `libmapa::core` / `libmapa::widget`.

### Llevarla a otro PC

```bat
herramientas\desplegar.bat <app.exe> <carpeta destino> [carpeta del paquete]
```

Copia el `.exe`, ejecuta `windeployqt` (sin los plugins de red, que el producto
no usa), **comprueba** los tres plugins sin los que el mapa sale en blanco
(`qwindows`, `qsqlite`, `qjpeg`) y, si se le pasa el paquete, lo revisa y copia
**solo** los ficheros que usa su `mapa.json` a `<destino>\datos` con
`check_data --export`. La carpeta resultante funciona en un PC sin Qt ni
internet. La librería tiene una guarda en CMake: si alguien le añade
`Qt Network`, la configuración se para.

Para una app de **Qt 5** (5.14 / 5.15) hay un segundo juego,
`herramientas\desplegar_qt5.bat`, con los mismos argumentos. El `windeployqt`
de Qt 5 no tiene `--skip-plugin-types`: este script quita después los plugins
de red (`bearer`, `generic`, `position`). Además, antes de empezar comprueba
que el `.exe` es de Qt 5 y está en Release, y al final avisa si algo de la
entrega sigue pidiendo `Qt5Network.dll`.

### QCustomPlot

**No se incluye en el repositorio: es GPL v3.** Ver
[`third_party/qcustomplot/LEEME.md`](third_party/qcustomplot/LEEME.md).

Copia `qcustomplot.h` y `qcustomplot.cpp` en `third_party/qcustomplot/`, o
apunta a la copia que ya tenga tu proyecto:

```bash
cmake -S . -B build -DLIBMAPA_QCP_DIR=/ruta/a/qcustomplot
```

Sin QCustomPlot, el núcleo compila igual y pasa sus tests; se quedan fuera el
widget, la aplicación `demo`, `render_map` y `fill_map`.

## Las bases de datos

Formato RMaps: una tabla `tiles` con columnas `(version, z, x, y, s, image)`.
**No van en el repositorio**, son gigabytes de datos.

Cada base tiene sus propias convenciones, y no son evidentes: la de OSM guarda
`z = zoomVerdadero + 1` y la satelital `z = 17 − zoomVerdadero`. En vez de
cablearlas, `probe_db` las detecta:

```bash
probe_db --id osm       --file Recursos/Cuba_OSM_CID3.sqlitedb \
         --id satelital --file Recursos/Cuba_Satelital_CID3.sqlitedb \
         --ref-bbox 23.3,-85.0,19.7,-74.0 \
         --out datasets.json
```

Detecta el mapeo de zoom, el esquema del eje Y (XYZ o TMS), el nivel de fondo
garantizado, el relleno típico y la extensión cubierta. Copia
`datasets.example.json` como plantilla si prefieres escribirlo a mano.

## Herramientas

| | |
|---|---|
| `probe_db` | Sondea las BD de teselas y genera `datasets.json`, o con `--package` el manifiesto `mapa.json` del paquete (rutas relativas) |
| `check_data` | Comprueba un paquete de datos antes de distribuirlo: ficheros, que abran, que sus imágenes se decodifiquen, cobertura por zoom en la zona y tamaño total |
| `bench_tiles` | Mide cobertura y tiempos de carga sobre las BD reales |
| `render_map` | Dibuja el mapa a PNG, sin abrir ninguna ventana (también un paquete entero con `--data`) |
| `vector_db` | Crea e inspecciona la BD de puntos, rutas y polígonos |
| `geo_to_tiles` | Rasteriza un fichero vectorial `.geo` a una base de teselas, para usarlo como capa base |
| `fill_tiles` | Descarga las teselas que faltan (o crea una base nueva) de una fuente XYZ sin clave |
| `fill_hgt` | Descarga ficheros de elevación SRTM `.hgt` (30 m) de AWS Skadi (sin clave) para una zona (autónomo: descomprime con **miniz**, sin zlib) |
| `dem_to_db` | Construye una base de datos de elevación (`.sqlitedb`) desde una carpeta de ficheros SRTM `.hgt` |
| `fill_map` | Lo mismo pero con mapa: marca el área (rectángulo o polígono), rango de zoom, barra de progreso y mancha de cobertura |
| `demo` | Aplicación de ejemplo: capas, dibujo/edición, cobertura por zoom, cota del terreno y persistencia automática |

`render_map --grid` marca cada tesela con su `z/x/y`: borde verde si es la
tesela propia, rojo si viene de un nivel superior escalado.

En `fill_map`, el botón **Rejilla** hace lo mismo sobre el mapa, y el botón
**Cobertura** (con selector de zoom) pinta una mancha fija con las zonas que ya
están en la BD a ese zoom, coloreada por completitud (ámbar→verde) y visible
aunque mires a un zoom menor. Además de **Seleccionar área** (rectángulo) está
**Polígono**: se marca clic a clic y descarga solo lo de dentro (en consola,
`fill_tiles --poly "lat,lon;lat,lon;..."`). La descarga va en **paralelo
limitado** (`--conns`/selector «Conex», 1..8) y, antes de confirmar, **estima el
tamaño en MB** con un muestreo rápido. La fuente por defecto es Esri «Clarity»
(sin clave); respeta los términos de uso de cada servidor.

Con `fill_map --dem <carpeta>` (ficheros `.hgt`) o `fill_map --dem-db <fichero>`
(base de datos `.sqlitedb`) —o el botón **DEM…**— la barra de estado muestra la
**cota del terreno bajo el cursor**. Detecta solo la resolución (90 m / 30 m) por
el tamaño del tile e interpola; sobre mar o sin dato muestra «—». La elevación es
parte de la librería (`MapWidget::elevationAt`), reutilizable desde cualquier app.

Para empaquetar la elevación en un solo fichero, el pipeline es
`fill_hgt --cuba --out carpeta` (baja 30 m de AWS Skadi sin clave, reanudable)
→ `dem_to_db carpeta --out dem.sqlitedb` (BD comprimida, ~30 % del tamaño) →
`fill_map --dem-db dem.sqlitedb`. El generador acepta también tus propios `.hgt`
de 90 m. La BD es ideal para distribuir dentro de una app.

## Referencia de comandos (argumentos por herramienta)

Opciones entre `[…]` opcionales; el resto, obligatorias. Los bbox son siempre
`latN,lonO,latS,lonE` (norte, oeste, sur, este). En `docs/arquitectura.html`
(y el PDF) está la tabla detallada de cada argumento.

```
probe_db     --id <id> --file <ruta.sqlitedb> [--name "…"] [--id … --file …]
             [--ref-bbox latN,lonO,latS,lonE] [--out datasets.json] [--no-test]
             [--package [--dem <dem.sqlitedb|carpeta>] [--overlay <f.geo>]…
                        [--features entidades.db]]          (genera mapa.json)

check_data   <carpeta_paquete | mapa.json> [--quick] [--max-zoom N] [--strict]
             [--export <carpeta>] [--verbose]
             (salida 0 = listo para distribuir, 1 = errores; --export copia
              solo los ficheros del paquete si no hay errores)

geo_to_tiles --in <f.geo> --out <salida.sqlitedb> --id <id> --name "<nombre>"
             [--minzoom N] [--maxzoom N] [--color #hex] [--width f] [--fill] [--bg #hex]

vector_db    --out <mapdata.db> [--dump]            (crea con datos de ejemplo)
             --file <mapdata.db> --dump             (solo inspecciona)

render_map   (--datasets <datasets.json> | --data <carpeta_paquete>) --out <mapa.png>
             [--layer id] [--center lat,lon] [--zoom N] [--size AnchoxAlto]
             [--wait ms] [--grid] [--features f.geo]

bench_tiles  --datasets <datasets.json>
             [--zoom N] [--width N] [--height N] [--center lat,lon] [--pans N]

fill_tiles   (--datasets <json> --id <id> | --new <fichero> [--id <id>] [--name "…"])
             (--bbox latN,lonO,latS,lonE | --poly "lat,lon;lat,lon;…")
             [--minzoom N] [--maxzoom N] [--url "…{z}/{y}/{x}…"]
             [--only-missing | --overwrite] [--rate N] [--conns 1..8]
             [--retries N] [--timeout ms] [--yes]

fill_map     [datasets.json] [--dem <carpeta_hgt>] [--dem-db <dem.sqlitedb>]
             (el resto —área, zoom, fuente, rejilla, cobertura— desde la ventana)

fill_hgt     (--cuba | --bbox latN,lonO,latS,lonE) --out <carpeta>
             [--url base] [--res 30]               (descarga SRTM 30 m, sin clave)

dem_to_db    <carpeta_hgt> --out <dem.sqlitedb> [--overwrite]

demo         [carpeta_paquete | mapa.json | datasets.json]
             [--dem <carpeta>] [--dem-db <db>] [--features <db>]
             (app de ejemplo: capas, dibujo, cobertura, cota, persistencia)
```

## Estructura

```
include/libmapa/     API pública: MapWidget, MapConfig, MapTypes, DataPackageInfo
src/
  core/              logging
  geo/               proyección Web Mercator, conversión geo <-> tesela
  db/                conexiones SQLite, esquema, repositorio vectorial
  tiles/             lectura, caché, planificación y carga de teselas
  dem/               elevación del terreno: ficheros SRTM .hgt o BD .sqlitedb
  io/                ficheros .geo, manifiesto del paquete (mapa.json) y su comprobación
  widget/            MapView (QCustomPlot), capas de dibujo (teselas,
                     entidades, objetivos, cobertura) y sus modelos
tests/               17 tests (14 sin QCustomPlot)
tools/               herramientas de línea de comandos (incl. fill_tiles / fill_map)
demo/                aplicación de ejemplo
examples/app_minima/ plantilla de producto que usa la librería instalada (CMake y qmake)
cmake/               libmapaConfig.cmake.in (para find_package)
qmake/libmapa/       compilar e instalar la librería solo con qmake (+ libmapa.pri)
qmake/*.pro          herramientas sueltas con qmake
herramientas/        desplegar.bat (Qt 6) y desplegar_qt5.bat (Qt 5): app + Qt + paquete
docs/DESPLIEGUE.md   guía paso a paso: integrar la librería y llevar la app a otro PC
docs/BITACORA.md     qué se encontró y por qué se decidió cada cosa
docs/arquitectura.html + .pdf   documento técnico (arquitectura, módulos, flujos)
```

## Estado

| Fase | |
|---|---|
| 0–1 | Infraestructura CMake y sonda de datasets |
| 2 | Acceso a teselas: conexiones persistentes, caché por capa |
| 3 | Motor asíncrono: hilo propio, cancelación, respaldo de tesela padre |
| 4 | `MapWidget`: navegación, capas, medición, zoom a área |
| 5 | Datos vectoriales: esquema relacional y repositorio |
| 6 | Entidades: puntos, polilíneas y polígonos, con capas, edición interactiva y deshacer/rehacer |
| 7 | Objetivos en movimiento (capa en tiempo real) y ficheros `.geo` como capas multi-parte |
| 8 | Vector pesado a teselas (`geo_to_tiles`); descarga de teselas que faltan (`fill_tiles`/`fill_map`), base nueva, auto-freno y mancha de cobertura |
| 9 | Descarga por polígono, en paralelo y con estimación de tamaño |
| 10 | Elevación del terreno: ficheros `.hgt` o BD `.sqlitedb` (`fill_hgt` → `dem_to_db`), cota bajo el cursor |
| 11 | Persistencia automática de entidades (`MapConfig.featuresDbFile`) y `demo` al día |
| 12 | **Paquete de datos sin conexión**: manifiesto `mapa.json`, `MapConfig.dataDir`, capas fijas, entidades del usuario en `AppData` |
| 13 | Comprobación del paquete: `check_data` (informe con cobertura por zoom) y `MapWidget::dataWarnings()` al abrir |
| 14 | Despliegue: `install()` + `find_package(libmapa)`, `examples/app_minima`, `desplegar.bat`, `check_data --export`, guarda contra Qt Network |
| 15 | Juego Qt 5: librería con qmake (`qmake/libmapa`, `libmapa.pri`), `desplegar_qt5.bat` y guía [`docs/DESPLIEGUE.md`](docs/DESPLIEGUE.md) |

El producto final trabaja **solo con datos locales**, y la librería ya se puede
usar desde otra aplicación y llevar a un PC sin Qt. Detalles y decisiones en
[`docs/BITACORA.md`](docs/BITACORA.md) §47–50.

## Licencia

Este proyecto se distribuye bajo la **GNU General Public License v3.0 (GPLv3)**,
la misma licencia que usa [QCustomPlot](https://www.qcustomplot.com/), el motor
de dibujo del que depende el widget. El texto completo está en el fichero
[`LICENSE`](LICENSE).

```
Copyright (C) 2026 Alberto Feito

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

QCustomPlot en sí **no se incluye** en el repositorio (ver
[`third_party/qcustomplot/LEEME.md`](third_party/qcustomplot/LEEME.md)); se
enlaza desde tu propia copia. El núcleo (`libmapa_core`), sus tests y las
herramientas que no usan el widget siguen compilando sin él.
