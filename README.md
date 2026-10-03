# libmapa

Librería Qt que dibuja mapas de teselas guardadas en SQLite, embebible como un
`QWidget` corriente. Refactorización de **LibMapaStatic**, la librería de mapas
de EstacionTerrena3.

Alterna entre cartografía **OSM** y **satelital** en caliente, lee las teselas
en un hilo aparte y rellena los huecos con teselas de nivel superior escaladas.

- Qt 5.14 / 5.15 / 6.x, MinGW / MSVC / GCC
- QCustomPlot como motor de dibujo, encapsulado: **no aparece en la cabecera pública**
- 15 tests (12 sin QCustomPlot), sin avisos del compilador con `-Wall -Wextra -Wconversion -Wold-style-cast`
- Descarga las teselas que faltan de una fuente XYZ sin clave (`fill_tiles` / `fill_map`), reanudable, en paralelo, por rectángulo o polígono y con estimación de tamaño
- Elevación del terreno desde ficheros SRTM `.hgt` **o** una base de datos `.sqlitedb` empaquetable (cota bajo el cursor en `fill_map`)

## Uso

```cpp
#include <libmapa/MapWidget.h>

libmapa::MapConfig cfg;
cfg.datasetsFile  = QDir::currentPath() + "/datasets.json";
cfg.initialCenter = QGeoCoordinate(23.1136, -82.3666);
cfg.initialZoom   = 11;

auto *mapa = new libmapa::MapWidget(cfg, this);
ui->contenedor->layout()->addWidget(mapa);

connect(botonSatelital, &QPushButton::clicked, mapa, [mapa]{
    mapa->setBaseLayerId("satelital");
});

connect(mapa, &libmapa::MapWidget::mouseMoved,
        this, [](const QGeoCoordinate &p){ /* ... */ });
```

## Compilar

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/a/Qt
cmake --build build -j4
cd build && ctest
```

En Qt Creator basta con abrir el `CMakeLists.txt`.

### QCustomPlot

**No se incluye en el repositorio: es GPL v3.** Ver
[`third_party/qcustomplot/LEEME.md`](third_party/qcustomplot/LEEME.md).

Copia `qcustomplot.h` y `qcustomplot.cpp` en `third_party/qcustomplot/`, o
apunta a la copia que ya tenga tu proyecto:

```bash
cmake -S . -B build -DLIBMAPA_QCP_DIR=/ruta/a/qcustomplot
```

Sin QCustomPlot, el núcleo compila igual y pasa sus tests; se quedan fuera el
widget, la aplicación `demo` y `render_map`.

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
| `probe_db` | Sondea las BD de teselas y genera `datasets.json` |
| `bench_tiles` | Mide cobertura y tiempos de carga sobre las BD reales |
| `render_map` | Dibuja el mapa a PNG, sin abrir ninguna ventana |
| `vector_db` | Crea e inspecciona la BD de puntos, rutas y polígonos |
| `fill_tiles` | Descarga las teselas que faltan (o crea una base nueva) de una fuente XYZ sin clave |
| `fill_hgt` | Descarga ficheros de elevación SRTM `.hgt` (30 m) de AWS Skadi (sin clave) para una zona (autónomo: descomprime con **miniz**, sin zlib) |
| `dem_to_db` | Construye una base de datos de elevación (`.sqlitedb`) desde una carpeta de ficheros SRTM `.hgt` |
| `fill_map` | Lo mismo pero con mapa: marca el área (rectángulo o polígono), rango de zoom, barra de progreso y mancha de cobertura |
| `demo` | Aplicación de ejemplo con selector de capa y herramientas |

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

## Estructura

```
include/libmapa/     API pública: MapWidget, MapTypes, MapConfig
src/
  core/              logging
  geo/               proyección Web Mercator, conversión geo <-> tesela
  db/                conexiones SQLite, esquema, repositorio vectorial
  tiles/             lectura, caché, planificación y carga de teselas
  widget/            MapView y capa de teselas sobre QCustomPlot
tests/               15 tests (12 sin QCustomPlot)
tools/               herramientas de línea de comandos (incl. fill_tiles / fill_map)
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

Las entidades se dibujan, se editan y se **deshacen/rehacen** con el ratón, pero
todavía **no se guardan solas**: enlazar el `MapWidget` con el
`VectorRepository` (persistencia automática) es el principal pendiente. La hoja
de ruta completa está en [`docs/BITACORA.md`](docs/BITACORA.md) §35.

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
