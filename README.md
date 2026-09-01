# libmapa

Librería Qt que dibuja mapas de teselas guardadas en SQLite, embebible como un
`QWidget` corriente. Refactorización de **LibMapaStatic**, la librería de mapas
de EstacionTerrena3.

Alterna entre cartografía **OSM** y **satelital** en caliente, lee las teselas
en un hilo aparte y rellena los huecos con teselas de nivel superior escaladas.

- Qt 5.14+ o Qt 6, MinGW / MSVC / GCC
- QCustomPlot como motor de dibujo, encapsulado: **no aparece en la cabecera pública**
- 10 tests (9 sin QCustomPlot), sin avisos del compilador con `-Wall -Wextra -Wconversion -Wold-style-cast`

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
| `demo` | Aplicación de ejemplo con selector de capa y herramientas |

`render_map --grid` marca cada tesela con su `z/x/y`: borde verde si es la
tesela propia, rojo si viene de un nivel superior escalado.

## Estructura

```
include/libmapa/     API pública: MapWidget, MapTypes, MapConfig
src/
  core/              logging
  geo/               proyección Web Mercator, conversión geo <-> tesela
  db/                conexiones SQLite, esquema, repositorio vectorial
  tiles/             lectura, caché, planificación y carga de teselas
  widget/            MapView y capa de teselas sobre QCustomPlot
tests/               10 tests (9 sin QCustomPlot)
tools/               herramientas de línea de comandos
docs/BITACORA.md     qué se encontró y por qué se decidió cada cosa
```

## Estado

| Fase | |
|---|---|
| 0–1 | Infraestructura CMake y sonda de datasets |
| 2 | Acceso a teselas: conexiones persistentes, caché por capa |
| 3 | Motor asíncrono: hilo propio, cancelación, respaldo de tesela padre |
| 4 | `MapWidget`: navegación, capas, medición, zoom a área |
| 5 | Datos vectoriales: esquema relacional y repositorio |
| 6 | *Pendiente*: overlays de puntos, vehículos, polígonos y rutas sobre el mapa |

Hasta la fase 5 inclusive, los datos vectoriales todavía no se dibujan: el
`MapWidget` aún no expone `addPoint()` ni `addVehicle()`. Eso llega en la 6.

## Licencia

**Sin definir todavía.** Depende de cómo se resuelva lo de QCustomPlot: si se
usa su versión GPL v3, este proyecto tendrá que ser GPLv3 también. Mientras no
haya un fichero `LICENSE`, el código queda como «todos los derechos
reservados».
