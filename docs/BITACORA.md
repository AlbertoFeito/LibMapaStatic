# Bitácora técnica de la refactorización

Registro de lo que se encontró y por qué se decidió cada cosa, fase por fase.
No es documentación de uso: para eso está el `README.md` de la raíz.

Está escrito en orden cronológico. Casi todos los hallazgos salieron de medir
contra las bases de datos reales (4,9 GiB de teselas sobre Cuba), no de leer
el código: varias decisiones de diseño se revisaron después de que las
mediciones contradijeran lo que se había supuesto.

---

# libmapa — Fases 0, 1 y 2

Refactorización de LibMapaStatic. Este paquete contiene el núcleo sin
interfaz gráfica (datos, geodesia, teselas), sus tests y la herramienta
`probe_db`.

**Estado verificado:** compila y pasa los 5 tests con **Qt 5.15** y **Qt 6.4**,
con `-Wall -Wextra -Wconversion -Wold-style-cast -Woverloaded-virtual` y sin
warnings propios.

---

## 1. Lo primero que tienes que hacer

Ejecutar `probe_db` contra tus dos `.sqlitedb` reales. Todo lo demás depende de
saber con certeza qué contienen. Salta directamente a la sección
[3. Ejecutar probe_db](#3-ejecutar-probe_db) si ya tienes el proyecto compilado.

---

## 2. Requisitos

| | |
|---|---|
| Qt | 5.15 o 6.x, con los módulos **Core, Gui, Sql, Positioning** (y **Test** para los tests) |
| Compilador | MSVC 2019+, MinGW 8+, o GCC/Clang con C++17 |
| CMake | 3.16 o superior (viene con Qt Creator) |

En Windows, si instalaste Qt con el instalador oficial, ya tienes todo.
Comprueba que en el *Qt Maintenance Tool* esté marcado **Qt Positioning**;
si no, añádelo — es el módulo de `QGeoCoordinate`, que tu proyecto ya usa.

---

## 3. Compilar

### Opción A — Qt Creator (la más cómoda)

1. `Archivo` → `Abrir archivo o proyecto…`
2. Selecciona **`CMakeLists.txt`** de la raíz de `libmapa/`.
3. Elige el kit (Qt 5.15 o Qt 6, da igual) y pulsa `Configurar proyecto`.
4. `Compilar` → `Compilar todo` (Ctrl+Shift+B).
5. Para lanzar los tests: pestaña `Pruebas` (o `Herramientas` → `Tests`) →
   `Ejecutar todos los tests`.

Los binarios quedan en la carpeta de compilación, dentro de la raíz del build.

### Opción B — Línea de comandos

**Windows (Símbolo del sistema de Qt / MSVC):**

```bat
cd libmapa
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.5.3\msvc2019_64
cmake --build build --config Release
cd build
ctest -C Release --output-on-failure
```

Ajusta `CMAKE_PREFIX_PATH` a tu instalación. Con MinGW añade
`-G "MinGW Makefiles"`.

**Linux / macOS:**

```bash
cd libmapa
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
cd build && ctest --output-on-failure
```

### Opción C — qmake, si no quieres tocar CMake

Solo compila `probe_db`, que es lo urgente. Abre con Qt Creator el fichero:

```
libmapa/qmake/probe_db.pro
```

Pulsa Ejecutar. El binario queda en `libmapa/bin/`.

### Salida esperada de los tests

```
1/5 Test #1: tst_tilematrix ...................   Passed
2/5 Test #2: tst_connectionpool ...............   Passed
3/5 Test #3: tst_tilesource ...................   Passed
4/5 Test #4: tst_probe ........................   Passed
5/5 Test #5: tst_tilecache ....................   Passed

100% tests passed, 0 tests failed out of 5
```

Si algo falla aquí, **páralo y mándame la salida** antes de seguir: significa
que hay una diferencia de entorno que conviene resolver antes que nada.

---

## 4. Ejecutar probe_db

Este es el paso que necesito de ti. Sitúate donde esté el ejecutable y lanza:

**Windows:**

```bat
probe_db.exe ^
  --id osm       --file "D:\Mapas\Recursos\Cuba_OSM_CID3.sqlitedb" ^
  --id satelital --file "D:\Mapas\Recursos\Cuba_Satelital_CID3.sqlitedb" ^
  --ref-bbox 23.3,-85.0,19.7,-74.0 ^
  --out datasets.json > informe.txt 2>&1
```

**Linux / macOS:**

```bash
./probe_db \
  --id osm       --file Recursos/Cuba_OSM_CID3.sqlitedb \
  --id satelital --file Recursos/Cuba_Satelital_CID3.sqlitedb \
  --ref-bbox 23.3,-85.0,19.7,-74.0 \
  --out datasets.json > informe.txt 2>&1
```

Ajusta las rutas a las tuyas. Las BD **se abren en solo lectura**: es imposible
que la herramienta las modifique o corrompa.

`--ref-bbox` es `latNorte,lonOeste,latSur,lonEste`. El valor de arriba
corresponde a Cuba con margen. **No lo omitas:** sin él, el offset absoluto de
zoom y el esquema del eje Y no se pueden determinar con datos y la herramienta
tiene que asumir convenciones.

### Qué mándame

Los dos ficheros: **`informe.txt`** y **`datasets.json`**. Con eso ataco la
Fase 3 sobre parámetros reales.

### Ejemplo de salida (con una BD sintética que imita la tuya)

```
==========================================================
 DATASET: satelital
==========================================================
 Fichero      : /tmp/fake_sat.sqlitedb
 Tamano       : 0.4 MiB (466944 bytes)
 page_size    : 4096
 Tabla        : tiles
 Columnas     : z=z x=x y=y image=image s=s (valor 0)
 Teselas      : 2020
 Tamano tesela: 64 px

 MAPEO DE ZOOM
   storedZ = -1 * logicalZ + 17
   zoom logico disponible: [6 .. 11]
   motivo: z=6 tiene 1472 teselas y z=11 tiene 6; el nivel de mas detalle
           es z=6 -> z INVERTIDO; offset absoluto = 17 (minimo geometrico 16,
           elegido por solape en longitud)

 ESQUEMA EJE Y: XYZ
   motivo: Solapamiento con la referencia lat [19.700, 23.300]:
           XYZ=3.600 grados, TMS=0.000 grados -> XYZ

 EXTENSION CUBIERTA
   NO: 23.40276, -85.07813
   SE: 19.64259, -73.82813

 NIVELES
   zLog  zBD      teselas        x[min..max]        y[min..max]
   ----  ----  ----------  -----------------  -----------------
      6    11           6             16..18             27..28
     ...
     11     6        1472           540..603           887..909

 VERIFICACION DE LECTURA
   z=9  rejilla x[140..144] y[222..226]
   pedidas 25, obtenidas 25 en 0 ms
   segunda lectura (cache SQLite caliente): 0 ms, 25 teselas
```

### Cómo leer el informe

- **`pedidas N, obtenidas N`** → el descriptor detectado es correcto.
- **`obtenidas 0`** → el mapeo de zoom o el esquema del eje Y están mal.
  Mándamelo igualmente: el informe trae los datos que necesito para
  diagnosticarlo.
- **`AVISOS`** → léelos. Suelen indicar que algo no se pudo determinar con
  certeza.
- Con tus BD reales de 1.4 GB, la primera lectura tardará más que la segunda.
  Esa diferencia es justo el efecto que hoy pierdes al abrir y cerrar la BD en
  cada movimiento del mapa.

---

## 5. Qué contiene el paquete

```
libmapa/
├── CMakeLists.txt
├── README.md                        ← este fichero
├── qmake/probe_db.pro               ← alternativa sin CMake
├── include/libmapa/
│   └── libmapa_export.h
├── src/
│   ├── core/Logging.{h,cpp}
│   ├── geo/
│   │   ├── WebMercator.{h,cpp}      ← proyeccion, firmas honestas  (F-19)
│   │   └── TileMatrix.{h,cpp}       ← geo<->tesela, fuente unica   (F-7)
│   ├── db/
│   │   ├── SqliteConnectionPool.{h,cpp}   ← conexiones persistentes (F-1,2,3)
│   │   └── Transaction.h            ← guard RAII                   (F-14)
│   └── tiles/
│       ├── TileDataset.{h,cpp}      ← descriptor por BD        (F-4,5,6)
│       ├── TileKey.h                ← clave hashable               (F-16)
│       ├── ITileSource.h            ← interfaz de origen           (F-5)
│       ├── RMapsTileSource.{h,cpp}  ← lector del formato real  (F-8,13)
│       ├── TileDatasetProbe.{h,cpp} ← autodeteccion               (F-4)
│       └── TileCache.{h,cpp}        ← LRU de imagenes             (F-15)
├── tests/
│   ├── SyntheticTileDb.h            ← genera BD de prueba
│   ├── tst_tilematrix.cpp
│   ├── tst_connectionpool.cpp
│   ├── tst_tilesource.cpp
│   ├── tst_probe.cpp
│   └── tst_tilecache.cpp
└── tools/probe_db/main.cpp
```

Los códigos `F-n` remiten a la numeración de fallas del documento
`PLAN_REFACTORIZACION.md`.

---

## 6. Notas sobre lo que se corrigió

**El pool de conexiones.** Tu `CBDatos` registraba `bd_Sat` y `bd_Osm` con el
mismo `connectionName` `"Mapas"`. Al comprobarlo con un test resultó ser peor
de lo esperado: Qt no solo sustituye la conexión, sino que **invalida el objeto
anterior** — `bdSat.connectionName()` pasa a devolver cadena vacía y cualquier
consulta por él falla. Eso explica la inestabilidad al alternar capas.
Está reproducido en `tst_connectionpool.cpp::demonstrateOriginalCollisionBug`.

También se confirmó que `QSqlDatabase::removeDatabase("QSQLITE")` es un no-op
silencioso: `"QSQLITE"` es el nombre del *driver*, no el de la conexión.

**El offset de zoom.** Mi primera versión de la sonda normalizaba el zoom
invertido a 0, y los tests la cazaron. Está mal: en el formato RMaps, `z` es un
nivel de reducción, pero los índices `x`/`y` siguen en la rejilla del zoom
**verdadero**. Con la normalización, un `x=310` almacenado quedaba asociado a un
nivel lógico 4, donde solo caben 16 teselas por lado → cero filas, mapa en
blanco. Ahora la sonda calcula el offset absoluto acotándolo por geometría y
afinándolo por solape en longitud contra `--ref-bbox`.

**La caché.** Medido en esta máquina: decodificar 300 teselas 5 veces son
607 ms; con caché, 162 ms. Un factor de 3,7. Es el coste que hoy pagas en cada
movimiento del mapa, en el hilo de la interfaz.

---

## 7. Si algo va mal

**`Could NOT find Qt6Positioning` (o Qt5Positioning)**
Falta el módulo. Abre el *Qt Maintenance Tool* y añade **Qt Positioning**.

**`QSQLITE driver not loaded`**
Falta el plugin de SQLite. En Windows suele bastar con ejecutar desde Qt
Creator, que ya configura el `PATH`. Desde consola, añade
`C:\Qt\<version>\<kit>\bin` al `PATH`.

**Los tests fallan en `tst_tilecache::avoidsRepeatedDecoding`**
Ese test compara tiempos y en una máquina muy cargada podría dar un falso
negativo. No es grave; los demás son los que importan.

**Compilando en Linux sin servidor gráfico**
Antepón `QT_QPA_PLATFORM=offscreen` a los comandos de test.

Para ver el log interno con detalle:

```
QT_LOGGING_RULES="libmapa.*.debug=true" ./probe_db ...
```

---

## 8. Después de esto

Con `datasets.json` en la mano, la Fase 3 es mecánica: `TileLoader` en hilo
propio con cancelación por `requestId`, y `MapView` reutilizando los
`QCPItemPixmap` en vez de destruirlos y recrearlos en cada frame. Luego la
Fase 4 es ya el `MapWidget` público con `setBaseLayer()` para alternar
OSM ↔ Satelital en caliente.

---

## 9. Resultados del sondeo de las BD reales (agosto 2026)

### Hallazgo principal: las dos BD usan convenciones de zoom distintas

| BD | Tamaño | Convención | Zoom verdadero |
|---|---|---|---|
| `Cuba_OSM_CID3.sqlitedb` | 1 454 MiB | `z = zoomVerdadero + 1` | 3 .. 16 |
| `Cuba_Satelital_CID3.sqlitedb` | 3 438 MiB | `z = 17 - zoomVerdadero` | 1 .. 18 |

La primera versión de la sonda dio `zOffset = 0` para OSM porque solo buscaba
el offset en el caso invertido. Con ese descriptor el mapa se situaba en
**lat 71 N, lon −133** (Territorios del Noroeste, Canadá). Su propio aviso lo
delató: *"Ninguna interpretación solapa con la extensión de referencia"*.

Esto explica además la falla **F-7**: en el código original,
`ValidaZoomActivo` calculaba los índices con `ZoomValido - 1` mientras
`Cargar_BD_IMG` consultaba `WHERE z = Zoom_Level` sin el `-1`. No era un
descuadre, era la compensación —no documentada— de esta convención.

Los descriptores correctos están en **`datasets.json`** (raíz del proyecto).

### Cobertura por nivel

**OSM: densidad prácticamente total** hasta zoom verdadero 15 (884 447 teselas,
100 % del rectángulo). El nivel 16 solo cubre una franja de 18 columnas
(x 16900..16917), así que `maxZoom` se fija en **15**.

**Satelital: cobertura solo de tierra.** Se estabiliza en torno al 32–33 %
entre los niveles 12 y 15, que es aproximadamente la proporción entre la
superficie de Cuba y su rectángulo envolvente — es decir, no hay teselas de
mar. A partir de ahí cae en picado:

| zoom | teselas | relleno |
|---|---|---|
| 13 | 7 122 | 33,4 % |
| 14 | 27 462 | 32,4 % |
| 15 | 107 696 | 32,0 % |
| 16 | 87 923 | **8,0 %** |
| 17 | 65 431 | **1,6 %** |
| 18 | 32 209 | **0,23 %** |

Los niveles 16 a 18 solo tienen zonas concretas. **Consecuencia para la
Fase 3:** el render *tiene* que tolerar teselas ausentes dibujando la del
nivel padre escalada. No es un caso excepcional, es lo normal en satelital.
La verificación de lectura ya lo mostró: 19 de 25 teselas en el nivel 10.

### Anomalías en los niveles finales (detectadas en la segunda pasada)

Las dos BD tienen su último nivel a medio poblar, y calcular la extensión
desde él daba resultados falsos:

| BD | Nivel | Teselas | Problema |
|---|---|---|---|
| OSM | 16 | 23 407 | Franja de **0,099°** en lon −87,17: mar abierto al oeste de Cuba. El 0,7 % del ancho del nivel de referencia. |
| Satelital | 17 | 65 431 | Densidad del **5,0 %** de la del nivel de referencia. |
| Satelital | 18 | 32 209 | Densidad del **0,7 %**. |

Por eso el informe reportaba para OSM una extensión de lon −87,17 a −87,07:
la de esa franja, no la de la base de datos.

**Correcciones aplicadas a la sonda:**

- La extensión sale ahora del **nivel de referencia** (el más poblado), no del
  más profundo.
- El esquema XYZ/TMS se decide por **votación ponderada** entre todos los
  niveles, no mirando solo el último.
- Cada nivel muestra su propia extensión en el informe.
- Se añade `recommendedMaxZoom`, que recorta el zoom cuando un nivel es una
  franja estrecha o está demasiado disperso. `maxZoom` conserva el valor real.

Extensiones correctas, medidas sobre el nivel 15 de cada BD:

| BD | Longitud | Latitud |
|---|---|---|
| OSM | −87,1655 .. −72,5317 | 18,3754 .. 25,1453 |
| Satelital | −84,9902 .. −74,1248 | 19,8184 .. 23,2918 |

### Corroboración con DatosZoom.txt

El fichero `DatosZoom.txt` del proyecto original confirma por una vía
independiente el mapeo de zoom de OSM: sus filas van de **4 a 17** (los
valores de `ZoomValido`), y `ValidaZoomActivo` calculaba los índices de tesela
con `ZoomValido - 1`, es decir zoom verdadero **3..16** — exactamente el rango
que detectó la sonda a partir del solape en longitud.

### Otros datos

- `page_size = 1024` en ambas, como se anticipó: valor antiguo que provoca
  encadenamiento de páginas de desbordamiento con BLOBs de ~10 KiB. No se
  puede cambiar sin reescribir las BD, pero se compensa con `cache_size` y
  `mmap_size`, ya configurados en el pool.
- Tesela media: **10 KiB**, 256 px, en ambas.
- Primera lectura de una rejilla 5×5: 55 ms (OSM) y 43 ms (satelital).
  Segunda lectura con la caché de SQLite caliente: **0 ms**. Esa diferencia es
  exactamente lo que hoy se tira a la basura al abrir y cerrar la BD en cada
  movimiento del mapa.
- La columna `s` vale 2 686 452 en OSM y 0 en satelital. Como 2 686 452 no
  parece un discriminador, la sonda ahora **verifica** que filtrar por ella no
  pierda teselas (compara `COUNT(*)` con y sin filtro) y lo desactiva sola si
  las perdiera.

---

## 10. Fase 3 — Motor de teselas asíncrono

### Qué se añadió

| Componente | Qué resuelve |
|---|---|
| `TilePlanner` | Respaldo de tesela padre: decide qué dibujar en cada hueco cuando la tesela exacta no está |
| `TileLoader` | Lectura y decodificación en hilo aparte, con cancelación de peticiones obsoletas (F-17) |
| `TileService` | Fachada para el hilo de la GUI: hilo + caché + planificador + alternancia de capas |
| `bench_tiles` | Banco de pruebas que mide cobertura y tiempos contra BD reales |

### El respaldo de tesela padre no era opcional

El sondeo dejó claro que la capa satelital solo guarda teselas de tierra
(~32 % de relleno). Medido con `bench_tiles` sobre una réplica con la densidad
real por nivel, un viewport de 1280×800 al zoom 12:

```
 COBERTURA EN PANTALLA
   huecos de la rejilla : 30
   con tesela exacta    : 11
   con respaldo de padre: 19
   sin nada que dibujar : 0
   cobertura total      : 100.0 %
   (sin respaldo, la pantalla estaria al 36.7 %)
```

Sin respaldo, **dos tercios de la pantalla quedarían en blanco**. Es lo que
hace hoy el código original: dibuja las teselas que encuentra y deja el resto
vacío.

### Dos correcciones que solo aparecieron al medir

**Un nivel de respaldo no basta.** La primera versión precargaba únicamente
`z-2`. Pero si ese nivel también tiene huecos, el respaldo falla igual — y en
la BD satelital el relleno solo llega al 100 % en los niveles muy gruesos
(z 12 → 36 %, z 10 → 51 %, z 8 → 72 %, z 6 → 100 %). La medición dio 36,7 %
de cobertura, sin mejora alguna.

La solución es una **escalera**: se precargan `z-2`, `z-4` y `z-6`. Cada
peldaño tiene 4ⁿ veces menos teselas que el anterior, así que los tres juntos
son una fracción despreciable del nivel de detalle, y garantizan que el
planificador siempre encuentre un ancestro con imagen.

**La profundidad de búsqueda se quedaba corta.** Con la escalera llegando a
`z-6`, el `maxAncestorDepth` por defecto de 5 dejaba fuera de alcance el
peldaño más grueso. Se calcula ahora a partir de la propia escalera.

### Carga progresiva

Las peticiones se sirven en orden, de grueso a fino, y cada nivel emite su
señal en cuanto está listo:

```
 CARGA EN FRIO
   requestViewport devolvio en 0 ms      <- el hilo de la GUI no se bloquea
   nivel grueso de respaldo tras 7 ms    <- pantalla ya al 100 %
   nivel de detalle tras 18 ms
```

### Cancelación de peticiones obsoletas

Arrastrar el mapa genera decenas de eventos por segundo. Cada petición lleva
un identificador creciente; el hilo trabajador comprueba si ha quedado
obsoleta antes de abrir la BD, después de la consulta y cada 16 teselas
durante la decodificación.

```
 ARRASTRE SIMULADO
   peticiones lanzadas : 40
   lecturas servidas   : 4
   descartadas por obsoletas: 36
```

Sin esto, las teselas irían apareciendo con segundos de retraso,
correspondientes a posiciones que el usuario ya abandonó.

### Alternar OSM ↔ Satelital

```cpp
libmapa::TileService svc;
svc.start(libmapa::TileService::loadDatasets("datasets.json"));

svc.setActiveDataset("satelital");   // en caliente, sin reconstruir nada
```

La caché **no se vacía** al cambiar: las claves llevan el zoom y el dataset se
consulta por separado, así que volver a la capa anterior es instantáneo.

### Uso de bench_tiles

Lo más cómodo es el script, que localiza el ejecutable solo:

**Windows:** doble clic en `herramientas\ejecutar_bench.bat`, o desde consola:
```bat
herramientas\ejecutar_bench.bat
herramientas\ejecutar_bench.bat D:\ruta\a\datasets.json
```

**Linux / macOS:**
```bash
./herramientas/ejecutar_bench.sh
```

Genera `bench15.txt` y `bench16.txt` junto al script.

A mano, si prefieres:
```
bench_tiles --datasets datasets.json --zoom 12 --pans 40
```

**`bench_tiles` se añadió en la fase 3.** Si tu carpeta de compilación se creó
con un paquete anterior, CMake no sabe que existe y el ejecutable no aparece.
En Qt Creator: `Compilar → Ejecutar CMake`, luego `Compilar → Recompilar todo`.

Con tus BD reales dirá exactamente qué porcentaje de pantalla se resuelve con
teselas propias y cuánto por respaldo, en cada capa y a cada zoom.

---

## 11. Validación de la Fase 3 contra las BD reales

`probe_db` ejecutado sobre los 4,9 GiB reales confirmó **todas** las
predicciones, al dígito:

| | Predicho | Medido |
|---|---|---|
| OSM `zOffset` | 1 | 1 |
| OSM zoom | 3..16, recomendado 15 | 3..16, recomendado 15 |
| OSM extensión | −87,1655..−72,5317 | −87,16553..−72,53174 |
| Satelital `zOffset` | 17 | 17 |
| Satelital zoom | 1..18, recomendado 16 | 1..18, recomendado 16 |
| Satelital extensión | −84,9902..−74,1248 | −84,99023..−74,12476 |

El relleno por nivel de la capa satelital coincide con el que se había
supuesto en las réplicas sintéticas, con menos de un 0,3 % de diferencia:

```
z= 6  supuesto 100.0%   real 100.0%      z=11  supuesto  40.5%   real  40.5%
z= 7  supuesto  90.0%   real  90.0%      z=12  supuesto  36.0%   real  36.0%
z= 8  supuesto  72.0%   real  72.2%      z=13  supuesto  33.4%   real  33.4%
z= 9  supuesto  65.0%   real  65.2%      z=14  supuesto  32.4%   real  32.4%
z=10  supuesto  51.0%   real  51.0%      z=15  supuesto  32.0%   real  32.0%
```

**La verificación del filtro por `s` cumplió su función.** El valor 2 686 452
resultaba sospechoso, pero la comprobación empírica confirmó que es constante:
884 447 de 884 447 teselas en OSM y 107 696 de 107 696 en satelital. El filtro
se activa y el índice `(z,x,y,s)` se usa completo.

### Hallazgo nuevo: el tamaño en disco no predice el coste en RAM

| BD | Tesela media en disco | Muestra de 25 cerca de La Habana |
|---|---|---|
| OSM | **1,24 KiB** | 10,0 KiB |
| Satelital | 10,6 KiB | 10,5 KiB |

La media real de OSM es ocho veces menor que la de la muestra: la mayoría de
sus teselas son de mar, casi vacías y muy comprimibles. Pero **una vez
decodificada, cualquier tesela de 256×256 en RGB32 ocupa 256 KiB en RAM**, dé
igual lo que pesara comprimida.

Por eso la caché se acotaba mal: contar teselas no dice nada sobre la memoria
consumida. Ahora el límite se expresa en MiB y el coste de cada entrada se
mide sobre la imagen decodificada.

### El fallo que destapó ese cambio

Al acotar por memoria, un test empezó a fallar con **88 huecos en blanco**:
las cientos de teselas de detalle expulsaban por LRU los pocos niveles
gruesos de la escalera de respaldo. La escalera se autodestruía.

La caché tiene ahora un **área protegida** con presupuesto propio (una cuarta
parte del total). Los niveles de respaldo viven ahí y sobreviven a cualquier
avalancha de detalle. Verificado en `pinnedTilesSurviveDetailFlood`: tras
insertar 200 teselas de detalle en una caché de 8, las 4 protegidas siguen
intactas.

Con un viewport de pantalla real (1280×800 al zoom 11) y 16 MiB de caché:

```
56 teselas (16 protegidas), 24 exactas, 0 por respaldo, 0 huecos
```

---

## 12. Nota de portabilidad: Qt 5.14 y `QVariant`

`QSqlQuery::bindValue()` recibe un `QVariant`, pero **`qsqlquery.h` solo
declara `QVariant` en Qt 5** (`class QVariant;`), mientras que en Qt 6 lo
incluye de verdad:

```
Qt5:  50: class QVariant;
Qt6:  10: #include <QtCore/qvariant.h>
```

Con Qt 5, si ninguna otra cabecera arrastra `qvariant.h`, `QVariant` queda
como tipo incompleto y no existe conversión posible desde `int`, `QString`
ni `QByteArray`. El compilador dice:

```
error: no matching function for call to
       'QSqlQuery::bindValue(QString, const int&)'
```

Compilaba en Qt 5.15 y Qt 6 por casualidad — otras cabeceras cerraban la
cadena de inclusiones. En **Qt 5.14** esa cadena es distinta y falla.

Corregido añadiendo `#include <QVariant>` explícito en los seis ficheros que
usan `bindValue()` o `QSqlQuery::value()`. Es la clase de dependencia
implícita que solo aparece al cambiar de versión.

---

## 13. Tres optimizaciones que solo aparecieron al medir sobre las BD reales

`bench_tiles` sobre la BD de OSM, zoom 15, viewport 1280×800:

```
 z= 9   9 teselas   lectura  28 ms   decodificacion  88 ms
 z=11   9 teselas   lectura  25 ms   decodificacion  10 ms
 z=13  16 teselas   lectura  20 ms   decodificacion  17 ms
 z=15  56 teselas   lectura  97 ms   decodificacion  58 ms
```

### 1. La escalera de respaldo era contraproducente en OSM

Escalera (z9, z11, z13): **188 ms**. Nivel de detalle por sí solo: **155 ms**.
La escalera *más que duplicaba* el tiempo hasta ver el detalle — y en OSM es
inútil, porque su relleno es del 100 % y no hay ningún hueco que tapar.

Corregido con `TileDataset::typicalFill`, que mide la sonda. La escalera se
dimensiona sola:

| Relleno | Peldaños |
|---|---|
| ≥ 95 % (OSM) | 1 — solo para cubrir latencia |
| ≥ 60 % | 2 |
| < 60 % (satelital, 32 %) | 3 |

### 2. Se re-leía y re-decodificaba lo que ya estaba en memoria

```
z9  peticion 2 : lectura 0 ms, decodificacion 6 ms   <- ya estaba en cache
z11 peticion 2 : lectura 0 ms, decodificacion 7 ms
```

Cada movimiento del mapa reconsultaba la escalera entera, que por definición
cambia poco. Ahora una rejilla ya resuelta no se pide.

### 3. Las teselas inexistentes se pedían eternamente

El más grave de los tres. Una rejilla con huecos nunca se consideraba
resuelta, así que se volvía a consultar en cada movimiento. **En la capa
satelital, donde el 68 % de las teselas no existe, eso significa reconsultar
3,4 GiB una y otra vez para no encontrar nada.**

La caché lleva ahora un registro de ausencias, acotado y LRU. El resultado:

```
 SEGUNDA CARGA: 0 ms, no se consulto la base de datos
   (todo resuelto en memoria: 41 teselas y 52 ausencias anotadas)
```

### Una regresión que la medición cazó

Al hacer automáticos los peldaños, `m_fallbackLevels` pasó a valer −1, y el
cálculo de la profundidad máxima de búsqueda daba `qMax(5, -1) = 5`. El
peldaño más grueso quedaba fuera de alcance y la cobertura de la satelital
caía del 100 % al 93,3 %. Es exactamente el mismo fallo corregido antes,
reintroducido por un cambio en apariencia inocuo — y visible solo porque el
banco de pruebas mide la cobertura en cada ejecución.

### Dato colateral: decodificar cuesta tanto como leer

Alrededor de **1,05 ms por tesela**, muy estable entre niveles. Con 56 teselas
en pantalla son ~58 ms por repintado. En el hilo de la interfaz eso es
precisamente el tirón que hoy se nota al mover el mapa.

---

## 14. Portabilidad: `#pragma once` y rutas de inclusión

Compilaba en GCC/Linux con Qt 5.15 y Qt 6, y fallaba en MinGW con Qt 5.14 con
errores en cascada del tipo:

```
TileMatrix.h:48: error: 'TileKey' does not name a type
TileMatrix.cpp:48: error: request for member 'y' in 'key',
                          which is of non-class type 'const int'
bench_tiles/main.cpp:27: error: unknown type name 'TileService'
```

**Causa:** la misma cabecera se alcanzaba por hasta cuatro grafías distintas.
`src/tiles/TileKey.h` se incluía como `"TileKey.h"`, `"../tiles/TileKey.h"`,
`"tiles/TileKey.h"` y `"../../src/tiles/TileKey.h"`.

`#pragma once` deduplica comparando el fichero al que apunta cada ruta. GCC en
Linux lo resuelve bien; **MinGW no siempre reconoce que dos rutas escritas de
forma distinta son el mismo fichero**, así que la cabecera se procesaba dos
veces o a medias.

**Corregido por dos vías, ambas necesarias:**

1. **Guardas de inclusión reales** en las 16 cabeceras
   (`#ifndef LIBMAPA_TILES_TILEKEY_H_`). A diferencia de `#pragma once`, no
   dependen de que el compilador reconozca la identidad del fichero.
2. **Una sola grafía por ruta**, siempre relativa a `src/`:
   `"tiles/TileKey.h"`, `"geo/TileMatrix.h"`, `"core/Logging.h"`. `src/` ya
   era directorio de inclusión, así que vale desde cualquier punto del árbol,
   incluidas las herramientas.

### Y un test nuevo para que no vuelva a pasar

`tst_headers_selfcontained` compila un `.cpp` por cabecera que **no incluye
nada más**. Una cabecera que solo compila si antes se incluyó otra funciona
por casualidad, según el orden que imponga cada `.cpp`, y estalla al cambiar
de compilador. Los ficheros se generan en `tests/selfcontained/` y CMake los
recoge con un `GLOB`, así que una cabecera nueva entra sola en la comprobación.

Ahora son **8 tests**, no 7.

---

## 15. Medición sobre las BD reales, zoom 15 y 16

### La escalera automática funcionó

OSM al zoom 15, tiempo hasta tener el detalle:

| | |
|---|---|
| Escalera fija de 3 peldaños | 359 ms |
| Escalera automática, 1 peldaño (`typicalFill` = 1,00) | **92 ms** |

**3,9× más rápido.**

### Predicción confirmada: OSM z16 está vacío sobre Cuba

```
 osm, zoom 16
   con tesela exacta    : 0
   con respaldo de padre: 24
   cobertura total      : 100.0 %
   (sin respaldo, la pantalla estaria al 0.0 %)
   ... 48 ausencias anotadas
```

El nivel 16 de OSM es la franja de 0,099° en mar abierto, así que sobre La
Habana no hay ni una tesela. `recommendedMaxZoom = 15` era correcto, y el
respaldo mantiene la pantalla utilizable aunque se fuerce el zoom.

### El `typicalFill` global es un promedio engañoso

Satelital al zoom 16 sobre La Habana: **24 de 24 exactas**. El relleno global
del 32 % está dominado por el mar; sobre ciudad la cobertura es completa.
Resultado: los peldaños intermedios costaron 420 ms de los 1088 totales **sin
tapar ni un hueco**.

```
 z=10  lectura   75 ms   peldaño 1  -> cobertura 100 % ya a los 87 ms
 z=12  lectura  150 ms   peldaño 2  -> no tapo nada
 z=14  lectura  270 ms   peldaño 3  -> no tapo nada
 z=16  lectura  485 ms   DETALLE    -> 24 de 24 exactas
```

**Corregido: la escalera es ahora por demanda.** Se precarga solo el peldaño
más grueso, que es barato y ya cubre la pantalla entera. Los intermedios se
piden después, y únicamente si al llegar el detalle quedan huecos reales.
Medido tras el cambio: 2 niveles servidos en vez de 4, misma cobertura.

### El cuello de botella es el disco, no la CPU

Satelital, zoom 16, carga en frío:

| | | |
|---|---|---|
| Lectura de la BD | 980 ms | **91 %** |
| Decodificación | 101 ms | 9 % |

Coste por tesela leída en frío:

| BD | ms/tesela |
|---|---|
| OSM (1,4 GiB, tesela media 1,24 KiB) | 0,00 – 0,07 |
| Satelital (3,4 GiB, tesela media 10,6 KiB) | 1,31 – 13,50 |

Los ~10 ms/tesela de la satelital son del orden del tiempo de búsqueda de un
disco mecánico. **Si las BD están en un disco duro, moverlas a un SSD daría
más mejora que cualquier optimización de código.**

Mientras tanto, `mmap_size` se ajusta ahora al tamaño real del fichero (hasta
1 GiB en compilaciones de 64 bits) en lugar de un valor fijo de 256 MiB.

---

## 16. Cobertura solo sobre tierra: mar abierto

Las dos BD guardan teselas **solo donde hay tierra**, salvo en los niveles muy
gruesos. Verificado en el informe de la sonda:

| Satelital | Relleno | Extensión |
|---|---|---|
| z 1 – 5 | 100 % | −180..180 — **mundo entero, con mar** |
| z 6 | 100 % | −90..−73 — solo el recuadro de Cuba |
| z 9 | 65 % | ya faltan teselas de mar |
| z 15 | 32 % | solo tierra |

OSM sí tiene teselas de mar dentro de su recuadro: por eso su tesela media
pesa 1,24 KiB, son PNG azules casi vacíos. Fuera de ese recuadro (Florida,
Jamaica) no hay nada a ningún nivel.

### Tres fallos que esto destapó

**1. La escalera era relativa, no absoluta.** Desde z15 el peldaño más grueso
era z9 (65 % de relleno, con huecos de mar), y la búsqueda de ancestros se
topaba con un límite de 7 niveles. Los únicos niveles con mar son el 1 al 5,
así que **eran inalcanzables: mirar mar abierto dejaba la pantalla vacía.**

**2. `repairGapsIfNeeded` malgastaba consultas** pidiendo peldaños intermedios
sobre zonas donde tampoco hay nada.

**3. Sin nada que dibujar no se dibujaba nada**, por accidente y no por
decisión.

### Corregido

- **`TileDataset::baseZoom`**: nivel de fondo garantizado, el más grueso con
  cobertura completa que abarque la extensión del dataset. La sonda lo detecta
  sola. Satelital → z1, OSM → z3.
- Ese nivel **se carga siempre** y se queda anclado: cuesta una o dos teselas.
- La búsqueda de ancestros llega ahora **hasta `baseZoom`**, no hasta un tope
  fijo de 7.
- Si ni el nivel de fondo tiene teselas ahí, es zona fuera de la BD: **no se
  piden más niveles**, se anota y se deja de consultar.

### Verificado con dos tests

```
Mar abierto: 0 exactas, 42 resueltas con el nivel de cobertura mundial, 0 huecos
Zona sin cobertura: 3 lecturas y ninguna repeticion; 33 ausencias anotadas
```

El primero fabrica una BD como la satelital real (niveles gruesos mundiales,
detalle solo sobre Cuba) y mira mar abierto al norte de la isla: la pantalla se
resuelve entera con el nivel de fondo, siete niveles más arriba.

### Lo que queda para la Fase 4

Cuando no existe **ninguna** tesela a ningún nivel (Tokio, por ejemplo), el
plan devuelve huecos y no se dibuja nada. Eso es correcto, pero el color de
fondo debe ser una decisión explícita del widget — azul mar para la capa
satelital — y no el gris que traiga por defecto.

---

## 17. Refinamiento por calidad, no solo por huecos

`bench_tiles` reporta ahora la **profundidad** de cada respaldo. Un ancestro a
profundidad *d* se amplía 2^d veces:

| Salto | Ampliación | Región de origen |
|---|---|---|
| z3 → z15 | 4 096× | 0,0625 px |
| z1 → z15 | 16 384× | 0,0156 px |
| z1 → z16 | 32 768× | 0,0078 px |

A esas escalas la tesela de fondo **es un color plano**. Tapa el hueco, pero no
es un mapa.

### El fallo que esto destapó

`repairGapsIfNeeded` solo se disparaba con `emptyCount > 0`. Medido con el
desglose nuevo:

```
   respaldo por profundidad:
     17 huecos con un ancestro 6 niveles arriba (ampliado 64x)
```

Formalmente la cobertura era del 100 % y la reparación **no se activaba**,
aunque 17 de 24 celdas fueran un color plano.

**Corregido:** se refina también cuando hay respaldos por encima de
`kMaxAcceptableDepth` (3 niveles, 8×), no solo cuando faltan del todo.
Verificado en `blurryFallbackTriggersRefinement`:

```
Refinamiento: el respaldo paso de 6 niveles (ampliado 64x) a 2 (4x)
```

### Aviso sobre las mediciones de tiempo

En la última corrida la satelital al zoom 16 pasó de 980 ms a 2 ms de lectura.
**Casi todo es mérito de la caché del sistema operativo**, no del código: el
fichero ya estaba en memoria tras las corridas anteriores. Lo atribuible al
cambio es el número de consultas, de 4 a 3, que en disco frío eran 420 ms
tirados en niveles que no tapaban nada.

Para medir en frío hace falta reiniciar, o probar sobre una zona del mapa que
no se haya visitado en esa sesión.

---

## 18. Validación final de la Fase 3: los dos desenlaces del refinamiento

Corridas manuales a zoom 12 y 13 sobre las BD reales.

### Zoom 13 — el refinamiento acierta

```
z13 detalle: 48 pedidas, 40 obtenidas  -> 8 respaldos demasiado ampliados
reparacion : z9 (12/12) y z11 (20/20), ambos COMPLETOS
resultado  : 24 exactas, 0 por respaldo, 0 huecos
```

### Zoom 12 — el refinamiento no puede hacer nada, y está bien

```
z12 detalle: 56 pedidas, 38 obtenidas  -> 18 respaldos demasiado ampliados
reparacion : z8 (9/12) y z10 (12/16), incompletos
resultado  : 23 exactas, 7 por respaldo a 6 niveles (z6), 0 huecos
```

Para esos 7 huecos no existe ancestro en z8 ni en z10: se resuelven con z6, a
64×. **Y es correcto: son mar.** La BD satelital no guarda teselas de mar por
encima de los niveles gruesos, así que un azul uniforme es exactamente la
imagen buena. El aviso del banco se reformuló para no dar a entender lo
contrario.

### Números de lectura honestos

Al arrastrar hacia zonas no visitadas, con el fichero fuera de la caché del SO:

| | ms/tesela |
|---|---|
| OSM z13 (48 teselas en 149 ms) | 3,10 |
| Satelital z12 (56 en 83 ms) | 1,48 |

Muy lejos de los 0,00–0,07 ms/tesela que salían con la caché caliente. Esa es
la cifra realista para dimensionar la interfaz.

### Estado

El motor de teselas queda cerrado y validado contra los 4,9 GiB reales, en los
dos esquemas de zoom, con y sin huecos, con y sin refinamiento posible.

---

## 19. Fase 4 — El widget

### Decisión de diseño: QCustomPlot se conserva

Se mantiene QCustomPlot como motor de dibujo, por una razón de peso: es la
herramienta que ya se domina y con la que se va a mantener el proyecto. Pero
queda **encerrado dentro del PIMPL**.

### Cómo se usa

```cpp
libmapa::MapConfig cfg;
cfg.datasetsFile = QDir::currentPath() + "/datasets.json";
cfg.initialCenter = QGeoCoordinate(23.1136, -82.3666);
cfg.initialZoom = 11;

auto *mapa = new libmapa::MapWidget(cfg, this);
ui->contenedor->layout()->addWidget(mapa);

connect(ui->btnSat, &QPushButton::clicked, mapa, [mapa]{
    mapa->setBaseLayerId("satelital");
});
```

`MapWidget.h` **no incluye `qcustomplot.h`**. En el código original,
`cmapaplot.h` sí lo hacía y `CMapaPlot` heredaba públicamente de
`QCustomPlot`: cualquier proyecto que usara la librería se tragaba 300 KB de
cabecera y veía doscientos métodos que no debería tocar.

`customPlot()` devuelve `QWidget*` a propósito. Quien necesite acceso directo
hace el cast en *su* código y asume la dependencia ahí, sin imponérsela a los
demás.

### Una sola capa de teselas, no un item por tesela

`TileLayer` es un único `QCPLayerable` que pinta todas las teselas en su
`draw()`. El original hacía, en cada movimiento del mapa:

```cpp
foreach (QCPItemPixmap* pix, ListIMG) removeItem(pix);
ListIMG.clear();
// ... y volvía a crear un QCPItemPixmap por cada tesela visible
```

Con 30–60 teselas en pantalla eso son decenas de `QObject` creados y
destruidos por frame. El test `usesASingleTileLayer` mueve el mapa diez veces
y comprueba que `itemCount()` no cambia.

El respaldo de tesela padre sale gratis: `TileDrawItem` ya trae el rectángulo
de origen, que es justo el tercer argumento de `QPainter::drawImage`.

### Tres fallos de geometría que los tests no detectaban

Los tests pasaban antes de arreglarlos, porque medían coherencia interna y no
el resultado del dibujo. Aparecieron al renderizar de verdad a PNG:

**1. Los layouts de Qt no reparten geometría con el widget oculto.** El área
de dibujo medía 100×30 en vez de 640×480 y todo el cálculo de zoom salía mal.
No es solo cosa de tests: le pasa a cualquiera que construya el mapa, lo
redimensione y lo consulte sin volver al bucle de eventos.

**2. `resizeEvent` no se entrega a widgets ocultos.** Depender de él era
frágil. Hay ahora un `syncGeometry()` al principio de cada método público —
un par de comparaciones — y desaparece la dependencia del orden de eventos.
La sincronización es perezosa: ocurre al entrar por la API, no en el instante
del `resize()`.

**3. `axisRect()->rect()` es 0×0 hasta el primer repintado.** QCustomPlot
calcula esa geometría durante el `replot`. `MapView::ensureLayout()` la fuerza.

### Verificación con imágenes, no solo con aserciones

`render_map` dibuja el mapa a PNG sin abrir ninguna ventana:

```
render_map --datasets datasets.json --out mapa.png \
           --layer satelital --center 23.1136,-82.3666 --zoom 13 --grid
```

Con `--grid` cada tesela lleva su `z/x/y` y un borde: verde si es exacta, rojo
si viene de un ancestro. Fue lo que destapó los tres fallos anteriores.

Medido sobre la capa satelital al zoom 12, con huecos reales:

```
celdas: 9  (exactas 2, respaldo 7, vacias 0)
items dibujados: 9
```

### Tipos públicos

`MapTypes.h` define tipos de **valor**, no una jerarquía de punteros:
`MapPoint`, `MapVehicle`, `MapPolygon`, `MapRoute`. Los datos AIS van por
**composición** (`MapVehicle::ais`), no por herencia: en el original `CBarco`
añadía unos cincuenta getters que `CAvion` no tenía ni podía usar.

### QCustomPlot: cuál se usa

CMake lo busca, por este orden:

1. `-DLIBMAPA_QCP_DIR=/ruta/a/qcustomplot`
2. `third_party/qcustomplot/` (viene la 2.1.1 incluida)
3. La variable de entorno `QCUSTOMPLOT_DIR`

**Usa la tuya** si tu proyecto ya tiene una versión concreta. Se trata como
cabecera de sistema, así que sus 25 avisos con nuestro juego de `-W...` no
tapan los nuestros.

Sin QCustomPlot el núcleo compila igual y sus 8 tests siguen pasando: la
librería sirve para leer teselas aunque no se quiera el widget.

**Estado: 9 tests, 0 warnings, Qt5 y Qt6.**

---

## 20. Tres fallos encontrados probando `demo.exe`

Los tests pasaban con los tres presentes: medían coherencia interna, no el
resultado en pantalla.

### 1. Bloqueo al arrastrar a zoom bajo

A zoom 3 el viewport abarca **225 grados de longitud**, así que basta arrastrar
un poco para que el centro se salga de [−180, 180].

`QGeoCoordinate` con longitud fuera de ese rango es **inválido**, y sus
`latitude()` y `longitude()` devuelven NaN. Ese NaN llegaba a los rangos de los
ejes y QCustomPlot se colgaba generando marcas.

**Corregido** con `MapView::normalized()`: la longitud da la vuelta al mundo,
la latitud se recorta al límite de Mercator y los NaN se sustituyen por cero.
Se aplica en cada entrada al sistema de coordenadas.

### 2. Estiramiento vertical

El eje Y iba en **grados de latitud**. Pero el eje de QCustomPlot es lineal y
Mercator no lo es:

| Latitud real | y de Mercator | Desfase |
|---|---|---|
| 10° | 10,051 | +0,051 |
| 23° | 23,644 | **+0,644** |
| 40° | 43,712 | **+3,712** |
| 60° | 75,456 | **+15,456** |

A zoom 13 el viewport abarca décimas de grado y no se nota. A zoom 3 abarca
decenas y el mapa se deforma por completo.

**Corregido:** el eje Y va ahora en *grados de Mercator*
(`TileMatrix::latitudeToAxisY`). En esas unidades el mundo mide 360 en ambos
ejes, así que la misma fórmula sirve para los dos y las teselas salen cuadradas
sin corregir la relación de aspecto. Verificado: a zoom 4 y latitud 45, las 12
teselas miden 256×256 px exactos.

El arrastre también se calcula ahora en unidades de eje. Restar latitudes hacía
que el arrastre se acelerase hacia los polos.

> **Importante para los overlays.** Si dibujas con coordenadas de eje
> (`QCPCurve`, `QCPItemEllipse`...), **no le pases la latitud directamente**.
> Usa `MapWidget::toAxisCoords(coordenada)` y `fromAxisCoords(punto)`.

### 3. Las dos capas compartían caché

`TileKey` lleva solo `z/x/y`, y las dos BD usan los **mismos índices lógicos
para la misma geografía**. Con una caché compartida, al cambiar de capa se
veían teselas de la otra. Este README llegó a afirmar que conservar la caché
era seguro: era falso.

**Corregido:** una caché por capa, creada bajo demanda. El presupuesto de
`cacheMiB` se reparte entre ellas, con un mínimo de 32 MiB cada una. Las
teselas que llegan tarde van a la caché de **su** dataset, no a la de la capa
activa, así que un cambio de capa a mitad de carga no contamina la nueva.

Volver a la capa anterior sigue siendo instantáneo, y ahora además es correcto.

### El generador de pruebas también estaba mal

El test de cambio de capa no podía detectar el fallo porque las dos BD
sintéticas producían **píxeles idénticos** para el mismo `z/x/y`.
`SyntheticSpec::colorSeed` las hace distinguibles.

**Estado: 9 tests (18 casos en el del widget), 0 warnings, Qt5 y Qt6.**

---

## 21. Correcciones tras probar la Fase 4 en Windows

### Las dos capas se mezclaban en pantalla

Con "satelital" seleccionado aparecían teselas de OSM: la `11/553/888` con
calles y rótulos junto a la `11/552/888` de fotografía aérea. Y el estado
decía "100 % propias", porque las teselas *sí* estaban en caché — de la base
de datos equivocada.

**Causa:** una sola `TileCache` compartida por todo el servicio. La clave de
tesela es `(z, x, y)` y nada más, así que la `11/553/888` de OSM y la de la
satelital son la misma entrada. Ambas BD cubren Cuba, de modo que la colisión
es sistemática, no un caso raro.

Era peor de lo que parece: el registro de ausencias también se compartía, así
que las teselas de mar que no existen en la satelital hacían que en OSM ni
siquiera se pidieran.

**Corregido:** una caché por dataset. `TileService` mantiene
`std::map<QString, std::unique_ptr<TileCache>>` y las crea bajo demanda en
`cacheFor(datasetId)`; `cache()` devuelve la del dataset activo.
`onTilesLoaded` usa `cacheFor(datasetId)`, así que las teselas que llegan
tarde tras un cambio de capa van a la caché correcta.

Se usa `std::map` y no `QHash` porque `QHash` exige que el valor sea copiable
para poder desasociarse, y `TileCache` contiene un `QMutex`.

### Dónde se crea la caché

| | |
|---|---|
| Declaración | `TileService.h`, miembro `m_caches` |
| Creación | `TileService::cacheFor()`, perezosa, al pedir la primera tesela |
| Tamaño | `m_cacheBytesPerDataset`, fijado en `start()` desde `MapConfig::cacheMiB` |
| Uso en el dibujo | `TileLayer::draw()` → `m_service->cache()` (la del dataset activo) |

El presupuesto es **por dataset**: con `cacheMiB = 128` y dos capas, el techo
real son 256 MiB si se usan las dos.

### La aplicación se colgaba al arrastrar en zoom 3

A zoom 3 la pantalla abarca unos 225 grados de longitud, así que un arrastre
corto saca el centro de `[-180, 180]`.

**`QGeoCoordinate` no recorta: se marca inválida y devuelve NaN en
`latitude()` *y* en `longitude()`.** Ese NaN llegaba a los rangos de los ejes,
y `QCPAxis::setupTickVectors` solo retorna pronto si marcas, etiquetas y
rejilla están las tres apagadas — no bastaba con `setVisible(false)`. Con un
rango NaN el generador de marcas no termina.

**Corregido en tres sitios:**

- `MapView::normalized()` da la vuelta a la longitud y recorta la latitud.
  Se aplica en `applyZoomToAxes`, en `coordinateAt` y en `visibleNorthWest` /
  `visibleSouthEast`, que construían la coordenada directamente del eje.
- Marcas, etiquetas y rejilla desactivadas en los cuatro ejes, así el
  generador no llega a ejecutarse nunca.
- El centro se limita para que la vista no se salga del mundo. Esto elimina
  además las bandas vacías que se veían arriba y a los lados.

**Efecto visible:** a zoom 3 con 1280 px se ven 225 grados, así que el centro
solo puede moverse entre −67,5 y 67,5. Pedir −79,5 devuelve −67,5. Es
deliberado: la alternativa sería repetir el mundo horizontalmente, que no
aporta nada con dos BD que solo cubren Cuba.

### Menor

La demo mostraba "50 %% propias": un `%%` de más en el `QStringLiteral`.

**Estado: 9 tests, 0 warnings, Qt5 y Qt6.**

---

## 22. Los marcadores de las herramientas caían por debajo del cursor

Al medir distancia o al trazar el rectángulo de zoom a área, el item aparecía
unos píxeles más abajo del punto donde se había hecho clic.

**Causa:** el eje Y del mapa va en **grados de Mercator**, no de latitud. Los
items se colocaban con

```cpp
m_measureLine->start->setCoords(donde.longitude(), donde.latitude());
```

pasándole la latitud a un eje que espera Mercator. Ya existía el helper
`toAxis()` que hace la conversión, y no se estaba usando.

**El desfase depende del zoom y de la latitud**, y por eso es engañoso:

| latitud | error en grados | px a z3 | px a z11 | px a z15 |
|---|---|---|---|---|
| 10 | 0,05 | 0,3 | 75 | 1 192 |
| 23 | 0,64 | **3,7** | 938 | 15 002 |
| 45 | 5,50 | 31 | 8 008 | 128 136 |
| 60 | 15,46 | 88 | 22 510 | 360 154 |

Sobre Cuba a zoom 3 son unos pocos píxeles — apenas se nota, que es como se
detectó. A zoom 11 el marcador se habría ido a casi mil píxeles, fuera de la
pantalla.

Corregido usando `toAxis()` en las cinco llamadas a `setCoords`.

### Test que lo fija

`toolItemsLandUnderTheCursor` envía clics reales a varios zooms y latitudes y
comprueba, con `pixelPosition()`, que el item queda a menos de 1,5 px del
punto pulsado. Revirtiendo la corrección, el test falla con el mensaje
*"el marcador quedo 8.99 px por debajo del clic"* — el mismo síntoma
observado.

Es la clase de error que vuelve en cuanto se añada un item nuevo, así que
conviene tenerlo cubierto: **cualquier overlay que se coloque por coordenadas
de eje tiene que pasar por `toAxis()`.** Es la regla a recordar para las
fases 5 y 6, donde entran puntos, vehículos, polígonos y rutas.

**Estado: 9 tests, 0 warnings, Qt5 y Qt6.**

---

## 23. Desfase del cursor en las herramientas

Al medir distancia o hacer zoom a área, el punto quedaba unos píxeles por
debajo del clic.

### Lo que NO era

La conversión píxel → coordenada → píxel es **exacta**. Medido en
`toolsLandExactlyUnderTheCursor`, que pulsa en tres puntos repartidos por la
pantalla y compara dónde queda el item:

```
click en QPoint(120,90)  -> item en QPointF(120,90)   desfase 0  5.2e-12
click en QPoint(500,350) -> item en QPointF(500,350)  desfase 0 -5.2e-12
click en QPoint(880,610) -> item en QPointF(880,610)  desfase 0  0
```

El eje Y va en grados de Mercator, no de latitud, y `toAxis()`/`fromAxis()` ya
hacen la conversión en los dos sentidos. Si se le pasara la latitud a secas el
desfase sería enorme: a zoom 11 sobre Cuba, casi mil píxeles.

### Lo que sí es, casi con seguridad

**El escalado de pantalla de Windows.** Con el zoom del sistema al 125 % o
150 %, y sin declarar conciencia de alta densidad, Qt 5 entrega las
coordenadas del ratón en una escala y dibuja en otra. El resultado es un
desfase proporcional a la distancia al origen: pequeño arriba y creciente
hacia abajo, que es justo el síntoma descrito.

La demo activa ahora `AA_EnableHighDpiScaling` y `AA_UseHighDpiPixmaps` en
Qt 5 (en Qt 6 ya es el comportamiento por defecto), y muestra en la barra de
estado el `devicePixelRatio` al hacer clic, para poder confirmarlo.

**Si tu escalado está al 100 % el desfase tiene otra causa** y hace falta el
dato concreto: cuántos píxeles, a qué zoom, y si crece hacia abajo de la
pantalla o es constante.

---

## 24. La marca caía unos píxeles por debajo del clic

Al medir distancias y al hacer zoom a área, el punto quedaba varios píxeles
por debajo de donde se pinchaba.

### No era la conversión de coordenadas

Lo primero fue medir el viaje de ida y vuelta
`píxel → geográfico → unidades de eje → píxel`, en cuatro niveles de zoom y
seis puntos de la pantalla:

```
Desfase maximo: 0 px en X, 3.1e-11 px en Y
```

Exacto. El error estaba en otro sitio.

### Era el momento de capturar el punto

La herramienta de medir capturaba el punto en `mouseReleaseEvent`. Entre
pulsar y soltar, el ratón se mueve unos píxeles — y casi siempre hacia abajo,
al levantar el dedo. De ahí que la marca cayera justo por debajo del clic.

**Corregido:** el punto se ancla en `mousePressEvent`, que es donde el usuario
apunta. Lo mismo para `PickPoint`. `AreaZoom` ya anclaba la primera esquina al
pulsar.

Se añade además un círculo rojo en el punto de anclaje, para que se vea de
inmediato si cae donde toca. Su radio está en píxeles (`ptAbsolute`), así que
mide igual a cualquier zoom y no se deforma con la latitud.

### Verificado

`toolsLandExactlyUnderTheCursor` pulsa en tres puntos repartidos por la
pantalla y suelta el botón **9 píxeles más abajo** cada vez:

```
click en (120, 90)  -> item en (120, 90)   desfase 0, 5e-12
click en (500, 350) -> item en (500, 350)  desfase 0, -5e-12
click en (880, 610) -> item en (880, 610)  desfase 0, 0
```

Con el código anterior el desfase habría sido de 9 píxeles en los tres.

**Estado: 9 tests (27 casos), 0 warnings, Qt5 y Qt6.**

---

## 25. Fase 5 — Capa de datos vectoriales

### Lo que se encontró en el esquema original

Verificado contra `cbdatosmapa.cpp` y contra el propio SQLite, no de memoria.

**1. El DDL es un error de sintaxis.** Un *Find & Replace* de `NULL` por
`nullptr` arrasó las cadenas SQL:

```sql
CREATE TABLE IF NOT EXISTS puntos (no_punto KEY INTEGER NOT nullptr UNIQUE, ...)
```

```
sqlite3: near "nullptr": syntax error
```

Como el código hace `if (Consulta.prepare(Crea)) Consulta.exec();`, el
`prepare` falla en silencio y el `exec` ni se intenta. **En una instalación
nueva las tablas no se crean.** La aplicación solo funciona sobre ficheros
`.sig` heredados de antes del reemplazo.

**2. `no_punto KEY INTEGER` no declara ninguna clave primaria.** SQLite lo lee
como una columna de tipo `"KEY INTEGER"`, con `pk=0`. Se quiso escribir
`PRIMARY KEY`. Verificado con `PRAGMA table_info`.

**3. Los índices de columna al leer puntos están descuadrados.** La tabla tiene
ocho columnas y `cargarPuntos` las lee como si fueran siete: se comentó la
línea que leía `tipo` sin corregir los índices siguientes.

| `value(i)` | Columna real | Se usa como |
|---|---|---|
| 3 | `tipo` | símbolo → `loadFromData` falla, punto sin icono |
| 4 | `simbolo` | latitud → 0 |
| 5 | `latitud` | longitud → el punto aparece en otro sitio |
| 6 | `longitud` | fecha → texto sin sentido |

**4. Una tabla por entidad:** `trayectorias_<nombre>`, `poligono_<nombre>`,
`Ruta_<fecha>`. Con 500 buques, 500 tablas, y DDL construido con texto del
usuario.

**5. Fechas como `TEXT` `"dd/MM/yyyy hh:mm:ss"`,** imposibles de comparar en
SQL. De ahí que el filtro «del último día» fuese aquella condición con
`qAbs(dia-dia)<=1 && (qAbs(mes-mes)<=1 || mes==11)`.

**6. Sin claves foráneas:** al borrar un punto, su `trayectorias_<nombre>`
quedaba huérfana para siempre.

### El esquema nuevo

Diez tablas, ninguna creada en tiempo de ejecución: `punto`, `vehiculo`,
`buque_ais`, `trayectoria`, `poligono`, `poligono_vertice`, `ruta`,
`ruta_punto`, `schema_version` y el índice de AIS.

- **Los datos AIS van por composición**, en su propia tabla enlazada al punto.
  En el original, `CBarco` heredaba de `CVehiculo` y añadía ~50 getters que
  `CAvion` heredaba sin poder usar.
- **`ON DELETE CASCADE`** en todo lo que cuelga de un punto.
- **Tiempos como enteros** (epoch en ms): «buques del último día» es un `WHERE`.
- **`WITHOUT ROWID`** en las tablas de detalle, con clave compuesta.

### VectorRepository

Tipos de valor, no `QList<void*>`. Errores propagados con `errorOccurred`, no
descartados. Lectura **por nombre de columna**, así el descuadre del punto 3 no
puede repetirse aunque cambie el orden.

### Medido, no afirmado

```
500 filas -> 258 ms sueltas, 1 ms en una transaccion
Tablas tras guardar de todo: 10 -> las mismas, ninguna creada en tiempo de ejecucion
```

En esta máquina, con disco rápido y sin `fsync` real, el factor es 258. En un
disco mecánico será mayor.

### Un efecto de que las restricciones ahora existan

Una `QString` por defecto es **nula**, y `QSqlQuery` la enlaza como `NULL`.
Contra `descripcion TEXT NOT NULL DEFAULT ''` eso es una violación, no un
valor por defecto: el `DEFAULT` solo actúa si la columna se **omite**. Con el
`NOT nullptr` inerte del original esto nunca se notaba.

**Estado: 10 tests (22 casos en el repositorio), 0 warnings.**

---

## 26. Qué se puede ver de la Fase 5

**En pantalla, nada.** Es capa de datos, y el `demo` todavía no la usa: el
`MapWidget` aún no tiene `addPoint()` ni `addVehicle()` — eso llega en la
Fase 6, con los overlays. Lo que sí se puede hacer es inspeccionar el
resultado.

### 1. Los tests

```
ctest --output-on-failure
```

10 tests. `tst_vectorrepository` trae 22 casos, y tres de ellos **demuestran
los fallos del esquema original** en vez de describirlos:

| Test | Qué demuestra |
|---|---|
| `originalDdlIsASyntaxError` | El `CREATE TABLE` con `NOT nullptr` no compila; la tabla no se crea |
| `originalPrimaryKeyIsNotAKey` | `PRAGMA table_info` da `pk=0` y tipo `"KEY INTEGER"` |
| `originalColumnIndicesAreShifted` | El desfase de columnas al leer puntos |

### 2. La herramienta `vector_db`

Crea una base de datos de ejemplo y la vuelca:

```
vector_db --out mapdata.db
vector_db --file mapdata.db --dump
```

Y como es un SQLite normal, se puede abrir con **DB Browser for SQLite** para
mirarla por dentro.

Salida real:

```
 tablas (9): buque_ais, poligono, poligono_vertice, punto, ruta, ruta_punto,
             schema_version, trayectoria, vehiculo

 Restricciones de la tabla 'punto':
   id                INTEGER            PRIMARY KEY
   nombre            TEXT      NOT NULL
   latitud           REAL      NOT NULL
   ...

 Claves foraneas declaradas:
   buque_ais.punto_id        -> punto.id     ON DELETE CASCADE
   trayectoria.punto_id      -> punto.id     ON DELETE CASCADE
   vehiculo.punto_id         -> punto.id     ON DELETE CASCADE
   poligono_vertice.poligono_id -> poligono.id  ON DELETE CASCADE
   ruta_punto.ruta_id        -> ruta.id      ON DELETE CASCADE

 Vehiculos (2):
   [5] CU-T1234         aereo   rumbo 270  vel 850   (sin AIS)
   [6] Rio Almendares   naval   rumbo 0    vel 12    AIS mmsi=323456789 ...
        trayectoria: 500 muestras
```

Tres cosas que merece la pena mirar ahí:

- **`PRIMARY KEY` y `NOT NULL` aparecen de verdad.** En el esquema original no
  existían: el `NOT nullptr` impedía crear la tabla, y `KEY INTEGER` era un
  nombre de tipo.
- **Cinco claves foráneas con `ON DELETE CASCADE`.** Borrar un punto se lleva
  su vehículo, su AIS y sus 500 muestras de trayectoria.
- **El avión no tiene fila en `buque_ais`.** Los datos AIS van por composición.

### 3. La comprobación que puedes hacer en tu aplicación actual

Renombra `Recursos/puntosBD.sig`, arranca `EstacionTerrena3` y guarda un punto
nuevo. Si al reiniciar no está, es el fallo del DDL: las tablas nunca llegan a
crearse y la aplicación solo funciona sobre ficheros heredados.

---

## 27. Fase 6 — Entidades sobre el mapa

### El modelo: geometría más atributos, no una clase por concepto

En el código original había `CPunto`, `CVehiculo`, `CAvion` y `CBarco`, con
herencia, porque el dominio decía que eran cuatro cosas distintas. Acabó en un
`CBarco` con unos cincuenta getters de AIS que `CAvion` heredaba sin poder
usar, y en una jerarquía que había que tocar cada vez que aparecía un concepto
nuevo.

`MapFeature` tiene **tres geometrías** (`Point`, `Polyline`, `Polygon`) más un
`type` que es una etiqueta libre y un `QVariantMap attributes`. La librería no
interpreta ninguno de los dos:

```cpp
zona.type = "zona_prohibida";
zona.attributes["techo_m"] = 120;
zona.attributes["vigencia"] = "2026-09-01";
```

Un concepto nuevo del dominio no obliga a tocar la librería ni a migrar nada.

### El reparto

**Dentro de la librería:** dibujar geometrías, detectar qué hay bajo el
cursor, crear y editar, capas con visibilidad y orden, conversión de
coordenadas.

**Fuera, en la aplicación:** qué significa cada tipo, las reglas de
validación, el catálogo de iconos, y de dónde salen los datos.

### Estático y dinámico van en capas separadas

`FeatureLayer` dibuja a un pixmap y lo reutiliza mientras nada cambie. Es lo
que permitirá que los objetivos en movimiento, en su propia capa, no obliguen
a redibujar las zonas: con 500 objetivos y 50 zonas, compartir capa
significaría redibujarlo todo decenas de veces por segundo.

El trazo en curso se pinta **fuera** del pixmap, porque cambia con cada
movimiento del ratón.

### Herramientas interactivas

| Herramienta | |
|---|---|
| `DrawPoint` | un clic crea un punto |
| `DrawPolyline` / `DrawPolygon` | clic a clic; doble clic o clic derecho cierra |
| `EditFeature` | clic selecciona, arrastrar un tirador mueve el vértice, arrastrar el interior mueve la figura, doble clic sobre un lado inserta un vértice |

Teclado: `Esc` cancela, `Retroceso` deshace el último vértice, `Supr` borra la
entidad seleccionada, `Intro` cierra el trazado.

Detalles que importan:

- **La geometría en curso no entra en el modelo** hasta que se cierra. Si
  entrara, cancelar obligaría a limpiarla.
- **Los tiradores tienen prioridad** sobre la entidad al pulsar, porque caen
  encima de ella.
- **Cambiar de herramienta descarta el trazo a medias.** Dejarlo vivo hacía
  que reapareciera al volver a la herramienta.
- **El resalte y el radio de los puntos van en píxeles**, no en grados: no se
  deforman con el zoom ni con la latitud.
- `removeVertex` se niega a dejar un polígono con dos vértices, y
  `moveFeature` rechaza el desplazamiento **entero** si sacaría la geometría
  del mundo — el mismo `QGeoCoordinate` devolviendo NaN que colgaba la
  aplicación al arrastrar en zoom 3.

### Verificación

`tst_overlaymodel` prueba el modelo sin ventanas (19 casos). `tst_mapwidget`
simula pulsaciones y arrastres reales (35 casos): trazar un polígono clic a
clic, arrastrar un vértice concreto y comprobar que los demás no se mueven,
desplazar la figura entera y comprobar que todos los vértices se desplazan lo
mismo.

`render_map --features` dibuja un ejemplo completo a PNG.

**Estado: 11 tests, 0 avisos, Qt5 y Qt6.**

### Pendiente

Deshacer y rehacer, enlace con el `VectorRepository` para guardar y cargar, y
la capa dinámica de objetivos en movimiento.

---

## 28. El demo como banco de pruebas, y el vaciado observable

Sobre la base de la Fase 6 se añadieron **deshacer/rehacer** (instantáneas del
modelo, agrupables con `beginUndoGroup`/`endUndoGroup`) y **persistencia** en
SQLite (`MapWidget::saveFeaturesTo`/`loadFeaturesFrom`, sobre `setContents`,
que emite una sola señal para toda la carga). El `demo` pasó a ejercitar TODA
esa API: capas, dibujo, edición, propiedades, atributos de dominio, estilo,
deshacer/rehacer y guardar/cargar.

### El panel se refresca solo, no a mano

El primer banco de pruebas refrescaba el panel llamando a `actualizarPanelCapas()`
en cada manejador. El patrón es frágil: se le escapaba el botón **"Vaciar"** (no
refrescaba) y **cualquier edición hecha sobre el mapa** (borrar con `Supr`,
mover vértices) tampoco, porque no pasaba por un manejador del panel.

Ahora el panel se reconstruye **solo a partir de las señales** del `MapWidget`
(`featureAdded`, `featureRemoved`, `featureUpdated`, `featureLayersChanged`),
así que da igual de dónde venga el cambio. Eso destapó un hueco en el modelo:
`clearLayer()` y `clear()` emitían solo el `changed()` interno, que `MapWidget`
no reexpone, de modo que un vaciado era **invisible** para cualquier panel.

**Corregido en `OverlayModel`:** un vaciado es un borrado en lote, así que emite
`featureRemoved` entidad a entidad, igual que `removeFeature`, más `layersChanged`
para los contadores. `addFeature`/`removeFeature` también emiten `layersChanged`.
Como `restore()` (deshacer/rehacer) y `setContents()` (cargar) ya emitían
`layersChanged`, ahora **todas** las rutas refrescan el panel. Las N señales de
un vaciado o una carga se agrupan en una sola reconstrucción con un `QTimer`.
`tst_overlaymodel` gana `notifiesObserversOnClear`.

### Guardar/cargar

Rediseñado: se recuerda el **fichero actual** (en el título de la ventana) con
**Guardar** / **Guardar como…**, y **Abrir** avisa antes de reemplazar el
trabajo en curso (la carga es deshacible) y reporta los errores. Antes no había
aviso de fallo ni concepto de fichero abierto.

### Borrado sin choques

`Supr` borra la entidad seleccionada **solo con el panel enfocado**
(`WidgetWithChildrenShortcut`), para no pisar el `Supr` del editor, que sobre el
mapa borra vértices. Las herramientas (navegar, medir, zoom, dibujo, editar)
van en un único grupo excluyente.

**Estado: 11 tests, 0 avisos, compilado y probado en Qt 5.15 y Qt 6.4.**

---

## 29. Fase 7 — Ficheros .geo y objetivos moviles

### Ficheros .geo como capas

El formato `.geo` es una linea por vertice, `longitud,latitud,` (OJO: la
longitud primero), terminada en `0.0,0.0`. Un anillo cerrado repite el primer
vertice al final.

**Un `.geo` puede llevar VARIOS trazados**, separados por `0.0,0.0`: `0.0,0.0`
es un **separador**, no un simple fin de fichero. Un mismo fichero va desde un
anillo (las aguas) hasta decenas de polilineas (los `corredores` son parejas de
lineas; `ejercitos` son 39 divisiones administrativas).

`readGeoFile()` (en el nucleo, `include/libmapa/GeoFile.h`) devuelve **un
`GeoPath` por trazado** (`{points, closed}`), saltando lineas en blanco o mal
formadas y avisando por `error` si no se puede abrir.
`MapWidget::loadGeoAsLayer()` crea una entidad por trazado en la misma capa:
**poligono** si cierra, **punto** si es un solo vertice, **polilinea** en los
demas casos; devuelve la lista de identificadores. El `Aguas.geo` (aguas
jurisdiccionales) es un anillo de 140 vertices (139 tras quitar el de cierre).

El primer intento se paraba en el **primer** `0.0,0.0` y solo cargaba un
segmento: por eso `corredores` salia con una sola linea. Corregido tratando
`0.0,0.0` como separador y no como terminador.

### Geometria multi-parte: un fichero, una entidad

Un `.geo` entero es **una sola entidad multi-parte**, no una entidad por
trazado. `MapFeature` gana `QVector<QVector<QGeoCoordinate>> parts`: vacio =
una sola parte (se usa `geometry`); con elementos, la entidad es multi-parte y
todas las partes comparten tipo, estilo, nombre y atributos. `loadGeoAsLayer()`
crea **un** poligono multi-parte si todos los trazados cierran, o una polilinea
multi-parte si no.

- **Dibujo y seleccion** recorren `outlines()` (las partes, o `geometry`):
  `FeatureLayer` pinta y detecta bajo el cursor parte a parte.
- **Edicion**: mover la entidad entera desplaza todas las partes; editar
  vertices sueltos se rechaza en multi-parte (no se sabria que parte tocar).
- **Persistencia**: `entidad_vertice` gana una columna `parte`; guardar escribe
  una fila por vertice agrupada por parte, y cargar reconstruye las partes. Los
  ficheros del esquema anterior (sin `parte`) se detectan y se leen como una
  sola parte.

### Objetivos: etiqueta multilinea y opciones de traza

- La **etiqueta** de un objetivo admite varias lineas (`\n`): un parametro por
  linea (nombre, rumbo, velocidad...). `TargetLayer` las dibuja apiladas, cada
  una con su halo.
- La **traza** tiene opciones: `setTargetTrailLength(n)` con `n < 0` = toda
  (ilimitada), `0` = sin traza, `n > 0` = las ultimas N (10, 100, 500...). El
  demo lo expone en un desplegable.

### Objetivos moviles: la capa dinamica

La Fase 6 dejaba prevista una capa aparte para lo que se mueve, y aqui esta.
Tres piezas, separadas igual que las entidades estaticas:

- **`MapTarget`** (publico): posicion, rumbo, velocidad, **etiqueta de texto**,
  color. La identidad la pone la aplicacion (pista, MMSI...).
- **`TargetModel`**: guarda los objetivos y su **traza** (las ultimas N
  posiciones, acotada), sin dibujar. `upsert` da de alta; `update(id, pos,
  rumbo)` es la via rapida del tiempo real y anade el punto a la traza.
- **`TargetLayer`**: los dibuja todos en un unico `QCPLayerable` —traza,
  simbolo orientado por el rumbo y etiqueta con halo—.

Lo que hace que 250+ objetivos vayan fluidos: la capa vive en su propia
`QCPLayer` en modo **`lmBuffered`**, asi que actualizar posiciones repinta
**solo esa capa** y recompone, sin rehacer teselas ni entidades estaticas. Y
los avisos del modelo se **agrupan con un temporizador** (~30 fps): aunque
lleguen decenas de posiciones por segundo, no se repinta de mas. Fuera de
pantalla los objetivos se descartan (culling).

API en `MapWidget`: `addTarget`, `updateTarget`, `setTargetLabel`,
`removeTarget`, `clearTargets`, `target`, `targets`, `targetCount`,
`setTargetTrailLength`, `setTargetsVisible`.

### Verificacion

`tst_geofile` lee el `aguas.geo` real (un anillo), los `corredores` (6
polilineas) y `ejercitos` (39 segmentos), comprobando que NO se para en el
primer separador. `tst_targetmodel`
prueba altas, actualizaciones, poda de la traza y **250 objetivos** con 20
actualizaciones cada uno. `tst_mapwidget` carga un `.geo` como poligono y
**dibuja 250 objetivos** forzando el render con `grab()`. El `demo` gana
"Cargar .geo..." y un simulador de objetivos (250 por defecto) con traza y
etiqueta, moviendose en tiempo real.

**Estado: 13 tests (incluye `.geo` multi-trazado, entidad multi-parte con
guardar/cargar, y opciones de traza), 0 avisos, compilado y probado en Qt 5.15
y Qt 6.4.**

---

## 30. Vector pesado como capa base: `geo_to_tiles`

Un `.geo`/`.xyz` de Cuba con **~381.000 vertices** (uno solo de sus trazados
tiene 198.238) dibujado como entidad vector arrastra la aplicacion: hay que
recorrer y pintar cientos de miles de puntos en cada frame. La solucion no es
optimizar ese dibujo, sino **cambiar de representacion**: rasterizar el vector
a un **piramide de teselas** y servirlo con el motor de mapa que ya existe,
igual que OSM o el satelital. Asi solo se pintan los 256x256 visibles, cacheados.

La herramienta `geo_to_tiles` hace esa conversion:

```
geo_to_tiles --in Cuba.geo --out Cuba_Vector.sqlitedb \
             --id costas --name "Costas de Cuba" --minzoom 4 --maxzoom 12
```

- Lee `.geo` (longitud,latitud) y `.xyz` (metros Web Mercator): el formato se
  detecta por la magnitud. `0.0,0.0` separa trazados.
- Escribe un SQLite en el formato RMaps/XYZ que la libreria ya consume (tabla
  `tiles(x,y,z,s,image)`, esquema XYZ, `zFactor=1`), y **imprime el bloque
  para pegar en `datasets.json`**: la capa aparece como una base mas.

Dos cosas hacen que genere en **segundos** y no en minutos:

1. **Decimado sub-pixel** por zoom: a bajo zoom cientos de miles de vertices
   colapsan a los pocos que se distinguen.
2. **Bucketing de segmentos**: cada segmento se reparte a las teselas que
   cruza su caja, de modo que el trazado gigante de la costa aporta a cada
   tesela solo su tramo, en vez de redibujarse entero en todas. Sin esto, ese
   unico trazado de 198k puntos se pintaba completo en cada una de las ~1.300
   teselas.

El `Cuba.geo` completo (z4-12, 1.373 teselas) se convierte en ~7 segundos.

La DB generada NO usa columna `s` (se declara `hasSColumn:false`): en teselas
propias no significa nada y arrastraba un error facil —un `sValue` mal copiado
en `datasets.json` (p. ej. el de `osm`) hacia que la consulta filtrara `AND
s = <valor>` y **no devolviera ninguna tesela** aunque la fuente abriera bien.
Sin columna `s`, ese filtro no existe.

**Estado: 13 tests + la herramienta `geo_to_tiles`, 0 avisos, Qt 5.15 y Qt 6.4.**

---

## 31. Qt moderno: fuera 5.7, objetivo 5.14 / 5.15 / 6.x

Un compañero intentó compilar en Qt 5.7 con MinGW 5.3 y el proyecto reventaba.
En vez de arrastrar compatibilidad con un Qt de 2016, se decidió **abandonar
5.7** y fijar el objetivo en **Qt 5.14 / 5.15 / 6.x** (MinGW, MSVC, GCC), que es
lo que usan de verdad. Se eliminó de todo el repo cualquier rastro de 5.7.

Dos cosas concretas que salieron de ahí:

- Se **quitó el flag `-Wnull-dereference`**. No es de 5.7, pero su activación
  producía falsos positivos dentro de las cabeceras de Qt y de QCustomPlot
  (inalcanzables para nosotros) que inundaban la salida del compilador del
  compañero. Un flag que solo avisa de código que no es tuyo no aporta.
- Qt5 solo **declara** `QVariant` en `qsqlquery.h` (Qt6 sí lo incluye). Sin un
  `#include <QVariant>` explícito, `bindValue()` no compila en 5.14 porque
  `QVariant` es tipo incompleto. Se añadió donde hacía falta.

**Estado: 13 tests verdes en Qt 5.15 y Qt 6.4, sin avisos propios.**

---

## 32. Rellenar los huecos: `fill_tiles`, `fill_map` y el motor `TileFiller`

La capa satelital tiene `typicalFill ≈ 0.32`: dos tercios de la rejilla están
vacíos. Hacía falta **descargar las teselas que faltan**, y el usuario pidió la
vía fácil: **gratis y sin API key**. Resultado: un motor común y dos carcasas.

- **`TileFiller`** (`tools/common`) es un `QObject` **asíncrono** que no bloquea:
  se apoya en el bucle de eventos (un temporizador marca el ritmo, cada respuesta
  encadena la siguiente). El mismo motor —y la misma codificación probada— sirve
  para la consola (`fill_tiles`) y para la ventana con mapa (`fill_map`).
- **Fuente por defecto: Esri "Clarity"** (World Imagery), sin clave. Se eligió
  porque, de las fuentes sin clave probadas, es la que mejor **casa en color**
  con la base satelital de Google del usuario. Plantilla `{z}/{x}/{y}`, editable.
- **`prepare()`** abre la BD y cuenta cuántas faltan por zoom **sin red**, para
  poder avisar del total y pedir confirmación antes de una descarga enorme.
  **`start()`** descarga e **inserta con la codificación exacta de la base**
  (`storedZ`, Y según esquema, columna `s`). Es **reanudable**: solo baja lo que
  falta, así que se corta y se relanza sin repetir.
- **Orden de descarga** (`advanceCursor`): por nivel de zoom de menor a mayor
  (termina un nivel antes de pasar al siguiente); dentro de cada nivel, columna
  a columna de oeste a este y cada columna de norte a sur; saltando lo que ya
  existe. Barrido sistemático del bbox, no espiral desde el centro.
- **Base nueva** (`--new` / botón "Nueva base…"): crea un `.sqlitedb` desde cero
  con codificación **limpia** (XYZ, `z` = z lógico, sin columna `s`) y, al
  terminar, imprime el bloque listo para pegar en `datasets.json`.
- **Auto-freno**: si se acumulan fallos SEGUIDOS (la fuente está limitando), se
  pausa con backoff creciente (30→60→120→…→300 s) y se reanuda al primer éxito,
  en vez de insistir hasta que bloqueen la IP.
- **Diagnóstico TLS**: en Windows faltaba OpenSSL y toda descarga HTTPS fallaba
  en silencio. Se avisa al arrancar si `QSslSocket::supportsSsl()` es falso
  (qué DLLs copiar), y se dejó de leer el cuerpo de respuestas con error, que
  provocaba el cosmético `QIODevice::read: device not open`.

Sobre OSM: el servidor oficial **prohíbe** la descarga masiva de teselas; por eso
la fuente por defecto es satélite de Esri, no OSM.

**Estado: dos herramientas nuevas (consola y ventana) sobre un motor común;
13 tests verdes, Qt 5.15 y Qt 6.4.**

---

## 33. Ver qué falta en la base: rejilla y mancha de cobertura

Descargar a ciegas no dice qué tienes. Dos ayudas visuales, de menos a más útil:

- **Rejilla** (`TileLayer::setDebugGridVisible`, la misma de `render_map --grid`):
  dibuja el borde de cada tesela con su `z/x/y`, verde si es propia y rojo si se
  está viendo con un ancestro escalado. Pero es **efímera** y solo muestra el
  **zoom actual** bajo la vista.
- **Mancha de cobertura** (`CoverageLayer`): lo que de verdad hacía falta. Fija
  un **zoom objetivo** (p.ej. 14) y pinta, sobre el mapa, qué zonas de ese zoom
  están en la BD —**visible aunque estés mirando a z9**—, coloreadas por
  **completitud** (ámbar = a medias, verde = llena).

Lo delicado era no traer un millón de filas para pintarla. Se resolvió con
**`RMapsTileSource::coverageHistogram(z, shift)`**: una sola consulta
`GROUP BY (x>>shift, y>>shift)` que agrega las teselas presentes del nivel a una
rejilla gruesa (zoom resumen = objetivo − 3). Agrupar por `y_almacenada>>shift`
es correcto para la completitud tanto en XYZ como en TMS, porque un bloque de
`2^shift` valores de Y almacenada contiene exactamente los `4^shift` hijos de la
celda resumen. Se expone por la fachada `MapWidget`
(`setCoverageVisible/Zoom`, `refreshCoverage`), como el resto de capas.

La mancha se **refresca sola mientras descargas** si está encendida (limitado a
una vez cada ~2.5 s, que la consulta es un `GROUP BY`), y el estado exacto final
lo deja el refresco de `finished`. El botón quedó solo como mostrar/ocultar.

De paso, al empezar una descarga se **sale del modo "seleccionar área"** para
poder desplazar el mapa (deshabilitar el botón no cambiaba la herramienta activa).

**Estado: capa nueva `CoverageLayer` + consulta de cobertura; 13 tests verdes,
Qt 6.4.**

---

## 34. Documentación: comentarios por función, PDF y convenciones

- **Comentario `//` en español encima de CADA función** de toda la librería y las
  herramientas (módulos geo, tiles, db, io, core, widget y tools): qué hace y por
  qué, no lo obvio de la firma. El header documenta el API; el `.cpp`, la
  implementación. Es la norma del proyecto para todo código nuevo.
- **`docs/arquitectura.html` + PDF** (`docs/LibMapaStatic_Documentacion.pdf`):
  documento técnico con arquitectura por capas, módulos, flujos de interacción,
  codificación de teselas, herramientas y el motor de descarga. El PDF se genera
  con **Chromium headless** (no hay pandoc/weasyprint en el entorno):
  `chrome --headless --print-to-pdf=... arquitectura.html`.
- **`Doxyfile`** para generar la referencia del API a partir de las cabeceras.
- **`CLAUDE.md`** (raíz) y la skill **`.claude/skills/libmapa-docs`** fijan las
  convenciones aprendidas (rama y atribución de commits, estilo de comentarios,
  Qt 5.14/5.15/6.x, QCustomPlot 2.1.1 gitignored, fachada, codificación de
  teselas, build + 13 tests *offscreen* antes de commitear, y este mismo flujo
  de documentación) para que cualquier sesión futura las cumpla.

**Estado: documentación al día; sin cambios de código, no requiere build.**

---

## 35. Qué falta — hoja de ruta

> **Superada:** los puntos 1 (persistencia, §44) y 4 (elevación, §39–42) ya
> están hechos. La hoja de ruta vigente está en §46.

Lo que la librería **todavía no tiene**, por prioridad. Es una lista de trabajo,
no una promesa de orden.

**Alta (funcionalidad central incompleta):**

1. **Persistencia automática de entidades.** Se dibujan y editan en el mapa pero
   **no se guardan solas**: falta enlazar `MapWidget` con `VectorRepository`
   (altas/bajas/cambios → BD, y recarga al abrir). Ya lo reconoce el README.
2. **Objetivos/vehículos en vivo desde la BD.** `VectorRepository` tiene
   `vehiculo`/`buque_ais`/`trayectoria` y existe `TargetModel`/`TargetLayer`,
   pero no están conectados: la capa de datos y la de tiempo real van por
   separado.
3. **Rutas interactivas.** La BD guarda `ruta`/`ruta_punto`, pero no hay
   herramienta ni capa para dibujarlas/editarlas en el mapa.

**Media:**

4. **Altura del terreno (DEM).** No existe (ni almacenamiento, ni consulta por
   coordenada, ni relieve). Opción que encaja: teselas Terrarium/Terrain-RGB
   (`z/x/y` PNG) descargables con `fill_tiles`, más un decodificador RGB→metros.
5. **Fusionar bases regionales** (`merge_tiles`): unir varias `.sqlitedb` en una
   (relevante para bajar OSM por países y unificar).
6. **Medición de área/perímetro.** Hoy solo hay distancia entre dos puntos.
7. **Tests del código nuevo.** `TileFiller` (descarga) y la cobertura no tienen
   tests; los 13 actuales no los cubren.

**Baja (acabado cartográfico):**

8. Barra de escala, flecha norte, cuadrícula de coordenadas y leyenda.
9. Búsqueda por lugar/coordenada (geocoding).
10. **Soporte vectorial OSM** (`.pbf`/MVT): el tema grande en pausa. Hoy todo es
    ráster; servir vector requeriría un decodificador MVT y un renderizador de
    estilo nuevos (ver el análisis del `.txt` de OSM).
11. Publicar la referencia Doxygen e internacionalización (cadenas en español
    sin ficheros `.ts`).

**Estado: la librería cubre el ciclo ver→navegar→dibujar→descargar; cerrar
dibujar→guardar→recargar y conectar objetivos/rutas con la BD es el siguiente
salto natural.**

---

## 36. Acercarnos a SAS.Planet (1/4): selección por polígono

Comparando la descarga con **SAS.Planet**, nuestra herramienta era sólida e
integrada pero le faltaban cuatro cosas: selección por polígono, descarga en
paralelo, estimación de tamaño y elevación del terreno. Se abordan una a una;
esta es la primera.

Hasta ahora solo se podía marcar un **rectángulo** (`SelectArea`), y bajar un
bbox sobre una costa o una isla desperdicia muchas teselas de mar. Ahora se
puede marcar un **polígono** y descargar **solo lo de dentro**.

- `MapTool::SelectPolygon`: se marca clic a clic (doble clic o Enter lo cierra),
  reutilizando el borrador de `FeatureLayer` que ya dibujaba los polígonos de
  entidad —no se crea ninguna entidad, es una selección transitoria—. Emite
  `polygonSelected(QVector<QGeoCoordinate>)` y deja el contorno visible.
- El filtro es `GeoMath::pointInPolygon` (ray-casting sobre lon/lat), puesto en
  el núcleo para poder **probarlo** (test en `tst_tilematrix`). No corrige la
  distorsión de la proyección, pero para elegir qué teselas bajar en un área del
  tamaño de un país sobra.
- `TileFiller` admite `Params::polygon`: el **bbox de barrido** sale de los
  vértices y, tesela a tesela, se descarta la que tenga su **centro fuera** del
  polígono —tanto al contar en `prepare()` como al descargar en `advanceCursor()`—.
- En las herramientas: `fill_tiles --poly "lat,lon;lat,lon;..."` (alternativa a
  `--bbox`) y, en `fill_map`, el botón **"Polígono"** (excluyente con
  "Seleccionar área"); marcar un rectángulo o escribir un bbox anula el polígono
  y viceversa. La mancha de cobertura sigue usando el bbox.

Decisión: el polígono es una **selección**, no una entidad del mapa; por eso se
reaprovecha el borrador (líneas, cierre, tiradores) sin tocar el `OverlayModel`.

**Estado: 13 tests verdes (con un caso nuevo `pointInPolygonBasic` dentro de
`tst_tilematrix`), Qt 6.4.**

---

## 37. Acercarnos a SAS.Planet (2/4): descarga en paralelo limitada

`TileFiller` bajaba **una tesela cada vez**: lanzaba una petición y, al volver,
encadenaba la siguiente. Con la latencia de un servidor remoto eso deja la
conexión parada entre tesela y tesela. SAS.Planet usa muchos hilos; aquí basta
con tener **unas pocas peticiones en vuelo a la vez** (sin hilos nuevos: todo en
el bucle de eventos).

El cambio de fondo fue sacar el estado de "qué tesela" de variables **globales**
(`m_cx/m_cy/m_curStoredY/m_attempt/m_reply`) a una `struct Pending` **por
petición**, guardada en `m_active` (`QHash<QNetworkReply*,Pending>`). Así varias
peticiones conviven sin pisarse las coordenadas.

- `advanceCursor()` → **`nextTile(Pending&)`**: produce la siguiente tesela que
  falta (cursor global de nivel+celda, con el filtro de polígono/present); marca
  `m_exhausted` al acabar.
- `pump()` → **`schedule()`**: mientras no esté cancelado ni en pausa y queden
  huecos (`m_active.size() < connections`), lanza —de la cola de reintentos
  primero, si no de `nextTile()`— respetando el **ritmo** (`--rate`, tope de
  lanzamientos/seg; si es pronto se re-arma solo). `launch()` crea la petición,
  su timeout propio y la registra.
- `onReplyDone(reply)`: busca su `Pending`, clasifica igual que antes e **inserta
  con las coords de esa `Pending`**. Un fallo con reintentos vuelve a la cola tras
  su backoff (contados en `m_pendingRetries` para no terminar antes de tiempo). El
  **auto-freno** y las estadísticas son compartidos; al dispararse pone
  `m_paused` y reanuda tras la pausa. Termina cuando no hay nada en vuelo, ni
  reintentos, ni cursor.

Decisión de semántica: `--rate` sigue siendo el **tope suave** de lanzamientos
por segundo (para no abusar de la fuente) y `--conns` (por defecto 2, máx 8) las
**peticiones simultáneas** que ocultan la latencia. Para ir realmente rápido se
sube `--rate`; el auto-freno y la **reanudabilidad** quedan intactos.

Verificado end-to-end sobre Esri Clarity: 20/20 teselas con `--conns 3` (progreso
a ráfagas), 0 al reanudar, y con `--poly` + `--conns 2` el triángulo baja solo 3
de las 20 del bbox.

En las herramientas: `fill_tiles --conns N` y, en `fill_map`, el selector
**"Conex"** (1..8).

**Estado: 13 tests verdes; descarga en paralelo verificada contra la fuente
real, Qt 6.4.**

## 38. Acercarnos a SAS.Planet (3/4): estimación de tamaño (MB)

Antes de confirmar sabíamos **cuántas** teselas íbamos a bajar, pero no **cuánto
ocupaban**. SAS.Planet enseña un tamaño aproximado; aquí añadimos lo mismo con un
**muestreo pequeño y asíncrono**, sin descargar todo ni escribir la BD.

Nuevo método `TileFiller::estimateSize(int samples = 12)`:

- `collectSamples(n)`: recorre los niveles del plan (sin tocar el cursor real ni
  emitir `zoomFinished`) y reparte las `n` muestras entre los zooms. En cada nivel
  salta con un **stride** por el rango de teselas para no coger todas del mismo
  rincón, aplicando el mismo filtro de **polígono/present** que la descarga (solo
  muestrea teselas que de verdad faltan). Devuelve una lista de `Pending` ligeras
  (basta z/x/y para construir la URL).
- Un mini-descargador propio (`m_sampNam` independiente del de la descarga real,
  con su `QSet` de peticiones en vuelo y su cap = `connections`) que **solo mide
  los bytes** de cada respuesta y **no inserta nada** en la BD. Las muestras se
  vuelven a bajar luego en `start()`; es poca cosa.
- Al volver todas, promedia los bytes de las que salieron bien y emite
  **`sizeEstimated(double avgKiB, qint64 estBytesTotal, int sampled)`** con
  `estBytesTotal ≈ media · totalToDownload()`. Si no logra muestrear nada (sin
  red, o todo presente), cae a una **heurística de ~20 KiB/tesela** y marca
  `sampled = 0` para que la interfaz avise de que es aproximado.

Es un paso **previo** y opcional: no interfiere con `start()` (su estado de red es
aparte) y se llama entre `prepare()` y `start()`.

Integración en las herramientas:

- `fill_tiles`: tras `prepare()` y antes del prompt, lanza `estimateSize()` y
  espera el `sizeEstimated` con un `QEventLoop` local (la consola puede bloquear);
  imprime `Tamano estimado: ~X MB (media Y KiB/tesela, muestreo de N)`.
- `fill_map`: igual, pero el `QEventLoop` es un **bucle anidado** (como un diálogo
  modal: la ventana sigue viva mientras se muestrea) y el resultado se añade a la
  confirmación: `Se descargaran N teselas (~X MB).`; si el muestreo falló se marca
  "aprox.".

Verificado sobre Esri Clarity en un área pequeña (zoom 14–16, 145 teselas): el
estimado fue **~3.6 MB** (media 25.4 KiB, muestreo de 12) y la BD real quedó en
**3.50 MB** — mismo orden, diferencia ~3 % (y la BD incluye el propio formato
SQLite, así que la suma de imágenes casa aún mejor).

**Estado: 13 tests verdes; estimación verificada contra la descarga real, Qt 6.4.**

## 39. Acercarnos a SAS.Planet (4/4): elevación del terreno (HGT/SRTM)

La librería manejaba teselas raster pero no sabía nada de la **altura del
terreno**. SAS.Planet puede mostrar la cota del punto; aquí añadimos lo mismo
leyendo ficheros **SRTM `.hgt`** locales (el usuario ya tiene los de 90 m de Cuba
en su PC; para las pruebas en el contenedor usé los de 30 m de AWS Skadi, mismo
formato).

El `.hgt` es un formato crudo sin cabecera: una rejilla **cuadrada** de muestras
`int16` **big-endian** que cubre un tile de 1°×1°. El nombre da la esquina
suroeste (`N19W077.hgt` = de 19N a 20N y de 77O a 76O). La fila 0 es el borde
**norte** y la columna 0 el **oeste**. La resolución no está escrita en ningún
sitio: se **deduce del tamaño** del fichero (`lado = isqrt(bytes/2)` → 1201 = 90 m,
3601 = 30 m).

Nuevo módulo de núcleo **`src/dem/HgtElevation`** (sin widgets, en
`libmapa_core`):
- `setDirectory(dir)` / `elevationAt(QGeoCoordinate) → double` (metros, o
  **NaN** si no hay dato: tile ausente, fuera de la carpeta, o hueco SRTM).
- Localiza el tile por el `floor` de lat/lon, lo carga con una **cache LRU**
  pequeña (no releer el disco al mover el ratón), autodetecta el lado, lee las
  muestras con `qFromBigEndian<qint16>` e **interpola bilinealmente** entre los 4
  nodos que rodean el punto. Si alguno es el valor de hueco (`-32768`) → NaN: no
  se inventa terreno. No había `isqrt` ni lector big-endian reutilizable, así que
  el módulo trae los suyos.

Se expone por la **fachada** `MapWidget`: nuevo `MapConfig.elevationDir`,
`MapWidget::elevationAt(coord)` y `setElevationDir(dir)` (para cambiarla en
caliente). En **`fill_map`**: opción `--dem <carpeta>`, un botón **"DEM…"** que
abre el selector de carpeta, y una etiqueta en la barra de estado que muestra la
**cota bajo el cursor** (conectada a `MapWidget::mouseMoved`); "—" cuando no hay
dato.

Prueba automática (`tst_hgtelevation`, el test nº 14): sin meter un `.hgt` real
(decenas de MB) en el repo, escribe `.hgt` **sintéticos** pequeños en un temporal
(lados 7 y 5, rampa conocida, con un hueco) y comprueba el valor exacto en un
nodo, la **autodetección** del lado, la **interpolación** bilineal, el hueco→NaN
y el tile ausente→NaN.

Prueba manual sobre el tile real de 30 m `N19W077` (Sierra Maestra): el barrido
de la zona da **1970.8 m** en 19.99N, 76.836O — el **Pico Turquino** (cumbre real
1974 m; SRTM 30 m lee ~1971). Confirma orientación norte/oeste, descodificación
big-endian y bilineal correctas.

**Estado: 14 tests verdes (13 + `tst_hgtelevation`); lectura HGT verificada
contra un tile SRTM real, Qt 6.4.**

## 40. Elevación en base de datos (1/3): interfaz y lector SQLite

La elevación por ficheros `.hgt` sueltos (§39) está bien para consultar en el PC,
pero el usuario va a **empaquetar la elevación dentro de una app**: para eso
quiere **un solo fichero** portable, no cientos de `.hgt`. Se decidió guardarla en
una **base de datos SQLite**, coherente con cómo el proyecto ya guarda las
teselas. Este paso añade **leer** de esa BD; generarla y descargar vienen después.

Para no duplicar la matemática, se separa *de dónde salen las muestras* de *cómo
se interpola*, con una interfaz al estilo de `ITileSource`:

- **`IElevationSource`** (`src/dem/IElevationSource.h`): interfaz pura, un único
  método `double elevationAt(QGeoCoordinate) const` con el contrato NaN.
- **`GridElevation`** (`src/dem/GridElevation.{h,cpp}`): base abstracta que
  concentra TODO lo común de una fuente SRTM en rejilla —`struct Tile`, la **caché
  LRU**, `sampleAt`, `isqrtExact`, el valor de hueco y la **interpolación
  bilineal** con su contrato NaN— y deja un único hueco por implementar:
  `virtual bool loadTile(latFloor, lonFloor, data, side)`.
- **`HgtElevation`** ahora **hereda de `GridElevation`**: solo implementa
  `loadTile` leyendo el fichero `.hgt`. Su comportamiento público no cambia, así
  que `tst_hgtelevation` sigue en verde sin tocarlo.
- **`SqliteElevation`** (`src/dem/SqliteElevation.{h,cpp}`): implementa `loadTile`
  consultando la BD y descomprimiendo el blob. Abre la BD **en solo lectura por
  hilo** con `SqliteConnectionPool` (ya fija `QSQLITE_OPEN_READONLY` y
  `busy_timeout`).

**Esquema de la BD** (lo fija el lector; lo escribirá `dem_to_db`):
```sql
CREATE TABLE dem_tiles (lat INTEGER, lon INTEGER, side INTEGER, data BLOB,
                        PRIMARY KEY(lat,lon));
CREATE TABLE dem_meta  (key TEXT PRIMARY KEY, value TEXT);
```
`data` son **las MISMAS muestras** `int16` big-endian que el `.hgt`, solo que
`qCompress`-adas (el terreno comprime bien y el mar casi a cero). Al leer,
`qUncompress` reproduce el `Tile` exacto → la bilineal es la misma → **idéntica
cota** que el lector de ficheros. (`qCompress`/`qUncompress`, de QtCore, no se
usaban aún en el repo.)

**Fachada:** `MapConfig` gana `elevationDbFile` (prioritaria sobre
`elevationDir`); el miembro de la `Impl` pasa a `std::unique_ptr<IElevationSource>`
y el constructor elige la implementación; nuevos `MapWidget::setElevationDb()` y
`setElevationDir()` cambian el origen en caliente. En **`fill_map`**: opción
`--dem-db <fichero>` y el botón **DEM…** pasa a un menú (carpeta `.hgt` **o** BD
`.sqlitedb`).

**Test nº 15 (`tst_sqliteelevation`)**: construye en un temporal el mismo tile
sintético como `.hgt` y como BD (blob `qCompress`-ado) y comprueba que
`SqliteElevation` da **exactamente lo mismo** que `HgtElevation` en varios puntos
(nodo, intermedio bilineal), más hueco→NaN y tile ausente→NaN.

**Estado: 15 tests verdes; lector de BD verificado contra el lector de ficheros
(misma cota). Faltan las herramientas `dem_to_db` y `fill_hgt` (siguientes pasos).**

## 41. Elevación en base de datos (2/3): generador `dem_to_db`

Con el lector de BD ya hecho (§40), falta **construir** esa base de datos. Nueva
herramienta de consola **`dem_to_db`** (solo `libmapa_core`, patrón de
`geo_to_tiles`):

```
dem_to_db <carpeta_hgt> --out cuba_dem.sqlitedb [--overwrite]
```

Recorre los `.hgt` de la carpeta, saca `(lat,lon)` del nombre
(`N19W077` → 19, −77, con una `QRegularExpression`), detecta el lado por el
tamaño, **comprime** las muestras con `qCompress` nivel 9 e inserta cada tile en
`dem_tiles` dentro de una `Transaction` (una sola, rápida). Escribe `dem_meta`
(resolución, fuente, nº de tiles, fecha) y un resumen con el ahorro de tamaño.
Avisa y salta ficheros con nombre o tamaño raros, sin abortar. **Sin red y sin
gzip**: trabaja sobre `.hgt` ya descomprimidos, así que vale tal cual para los
90 m del usuario.

Verificado con el tile real de 30 m `N19W077` (24,7 MB): la BD queda en **7,5 MB
(30 % del crudo)** y `SqliteElevation` lee de ella **exactamente** lo mismo que el
lector de ficheros (diff 0,0 en todos los puntos; Pico Turquino **1970.8 m**). A
90 m la relación es parecida, así que una BD de Cuba entera ronda las pocas
decenas de MB.

En CMake se añade como `dem_to_db` (como `vector_db`); hay `.pro` equivalente
(`qmake/dem_to_db.pro`, solo QtCore+QtSql, usa el header `Transaction.h`).

**Estado: 15 tests verdes; `dem_to_db` verificado (BD = ficheros, 30 % de tamaño).
Falta el descargador `fill_hgt` (último paso).**

## 42. Elevación en base de datos (3/3): descargador `fill_hgt`

Último paso del pipeline: conseguir los `.hgt` sin tenerlos ya. Nueva herramienta
de consola **`fill_hgt`** que baja tiles SRTM de **30 m** de **AWS Skadi** (sin
clave), para una zona:

```
fill_hgt --cuba --out carpeta
fill_hgt --bbox latN,lonO,latS,lonE --out carpeta
```

Recorre los tiles de 1°×1° del bbox (o el preset `--cuba`), y por cada uno que
**falte** baja `…/skadi/N19/N19W077.hgt.gz`, lo **descomprime al vuelo con zlib**
(`inflateInit2` en modo gzip, en memoria, sin ficheros temporales) y valida que
el `.hgt` resultante es un cuadrado perfecto de `int16`. Es **reanudable** (salta
los `.hgt` que ya están), reintenta los fallos de red con backoff, y **salta los
404** (tiles de mar abierto que la fuente no tiene) sin contarlos como error.

Es la única pieza que estrena una dependencia (**zlib**), porque Skadi solo sirve
`.hgt.gz`; el núcleo y `dem_to_db` siguen sin zlib (trabajan sobre `.hgt` ya
descomprimidos). En CMake se añade con `find_package(ZLIB)` y enlace `ZLIB::ZLIB`;
`.pro` equivalente con `LIBS += -lz`.

Verificado de punta a punta: `fill_hgt --bbox 21,-77.9,19.1,-76.1` baja 6 tiles
reales de 30 m (0 fallidos), la segunda pasada no baja nada (reanudable),
`dem_to_db` genera la BD (37,6 MB vs 148 MB crudos) y `SqliteElevation` da el
**Pico Turquino a 1970.8 m** y NaN fuera de cobertura. Pipeline completo:
**`fill_hgt` → `dem_to_db` → `fill_map --dem-db`**.

**Estado: 15 tests verdes; pipeline DEM-en-BD cerrado (descarga 30 m → BD
comprimida → consulta), verificado contra datos reales, Qt 6.4.**

> **Corrección (zlib → miniz, `fill_hgt` autónomo):** la primera versión usaba
> `find_package(ZLIB REQUIRED)`, que **abortaba toda** la configuración de CMake
> donde no hubiera zlib de desarrollo (p.ej. **Qt MinGW en Windows**:
> *"Could NOT find ZLIB"*). La solución definitiva **elimina la dependencia de
> zlib**: se vendoriza **miniz** (descompresor DEFLATE en un solo fichero,
> **dominio público**, en `third_party/miniz/`, commiteado —a diferencia de
> QCustomPlot—). `fill_hgt` descomprime el gzip parseando a mano la cabecera
> (RFC 1952) y usando `tinfl` de miniz, así que **compila en cualquier sitio sin
> instalar nada**. Detalles: se habilita el lenguaje **C** en `project()` (miniz
> es C; su `extern "C"` enlaza con el `main.cpp` en C++), se le aplica `-w` (es de
> terceros) y los tres avisos solo-C++ (`-Woverloaded-virtual`,
> `-Wnon-virtual-dtor`, `-Wold-style-cast`) se limitan a CXX con generator
> expressions. Verificado: la salida de miniz es **byte a byte idéntica** a la de
> zlib (mismo md5 del `.hgt`). (De paso, un `-Wconversion` latente en
> `GeoMath::pointInPolygon` —`int(poly.size())`— que solo salía en build limpio.)

## 43. Documentación: referencia de comandos por herramienta

Para que quede claro cómo se invoca cada aplicación, se añade una **referencia
completa de comando + argumentos de entrada** de las diez herramientas
(`probe_db`, `geo_to_tiles`, `vector_db`, `render_map`, `bench_tiles`,
`fill_tiles`, `fill_map`, `fill_hgt`, `dem_to_db`, `demo`):

- En **`README.md`**, un bloque "Referencia de comandos" con la sintaxis de cada
  una (opciones `[…]` opcionales; bbox siempre `latN,lonO,latS,lonE`).
- En **`docs/arquitectura.html`** (§11) + **PDF**, una tabla por herramienta
  explicando cada argumento, además de las secciones que ya había de `fill_tiles`
  y `fill_map`, y la síntesis de línea de comandos de `fill_map`.

Los argumentos se tomaron directamente del parseo real de cada `main.cpp` (no
inventados). Cambio solo de documentación: no toca el build ni los tests.

**Estado: 15 tests verdes (sin cambios de código); documentación de comandos al día.**

## 44. Persistencia automática de entidades

Hasta ahora las entidades (puntos/líneas/polígonos) se **dibujaban y editaban**
pero **no se guardaban solas**: solo había un guardado/carga manual
(`saveFeaturesTo`/`loadFeaturesFrom`). Se cierra el ciclo *dibujar → **guardar
solo** → recargar al abrir*, como capacidad de la **librería** (cualquier app la
hereda con solo configurarla).

Diseño — **autosave por volcado completo con antirebote** (reutiliza lo ya
probado, sin refactors arriesgados):

- `MapConfig.featuresDbFile`: si viene, el `MapWidget` **carga** esa BD de
  entidades al abrir y **guarda solo** lo que se dibuje/edite/borre. Vacío =
  apagado. También se puede encender en caliente con
  `MapWidget::setFeaturesDbFile(ruta)`; `saveFeaturesNow()` fuerza un guardado
  (p.ej. al cerrar la app).
- El guardado escucha las señales del modelo (`featureAdded`/`featureUpdated`/
  `featureRemoved`/`layersChanged` — cubren el 100 % de cambios, tanto por ratón
  como por API) y **rearranca un `QTimer` de antirebote (~500 ms)**: una ráfaga
  (arrastrar un vértice emite muchos `featureUpdated`) se agrupa en **un solo**
  volcado (`saveFeaturesTo(featuresDbFile)`: borra y reescribe).
- **Sin bucles**: `loadFeaturesFrom` usa `setContents`, que **no** reemite
  `featureAdded` por entidad; además la carga inicial se hace con un guard
  (`suppressAutosave`) para que su `layersChanged` no programe un guardado.

Por qué volcado completo y no incremental: `VectorRepository::writeFeature`
**siempre hace INSERT** (no UPSERT) e ignora `f.id`, y el id del `OverlayModel`
(contador en memoria) ≠ id de la tabla `entidad`. Un guardado incremental por
`featureUpdated` **duplicaría filas**. El volcado completo con antirebote es
correcto y simple para un mapa de trabajo (cientos de entidades). El UPSERT real
por id queda como mejora futura si algún día hay miles.

Los **objetivos móviles** (TargetModel) NO se persisten: llegan a decenas por
segundo y no tiene sentido escribirlos en disco.

Test (nº 15, `tst_mapwidget`, 3 casos nuevos): con un `featuresDbFile` en un
temporal, (1) añadir + `saveFeaturesNow()` deja la entidad en la BD (leída con un
`VectorRepository` aparte); (2) un segundo `MapWidget` con ese fichero
**autocarga** (`featureCount()==2`); (3) solo añadir y dejar correr el bucle de
eventos — el **antirebote** dispara el guardado solo (`QTest::qWait(900)`).

**Estado: 15 tests verdes (tst_mapwidget con 3 casos nuevos); persistencia
automática verificada, sin avisos, Qt 6.4.**

## 45. `demo`: cobertura por zoom, cota del terreno y persistencia

La app `demo` es la vitrina de la librería, pero se había quedado atrás: no
mostraba ni la **mancha de cobertura** ni la **cota del terreno** (vivían solo en
`fill_map`), ni usaba la **persistencia automática** recién añadida. Se llevan a
`demo` reutilizando exactamente la misma API pública (sin tocar el build: `demo`
ya enlaza `libmapa_widget`, que trae `CoverageLayer` y el DEM; no necesita
`Qt::Network`, que es solo para descargar).

- **Cobertura:** acción «Cobertura» (checkable) + selector de zoom en la barra de
  mapa → `setCoverageZoom` + `setCoverageVisible` (igual que en `fill_map`). Pinta
  qué zonas del zoom elegido ya están en la BD, visible aunque mires a otro zoom.
- **Cota:** botón **DEM…** con menú (carpeta `.hgt` / BD `.sqlitedb`) → `setElevationDir`/
  `setElevationDb`; la cota bajo el cursor sale en la barra de estado (junto a las
  coordenadas que ya había), "—" si no hay dato. También por CLI: `--dem`/`--dem-db`.
- **Persistencia automática:** opción `--features <db>` que rellena
  `MapConfig.featuresDbFile` → lo que dibujes se **guarda solo** y se recarga al
  abrir (los botones manuales Guardar/Abrir siguen para exportar a otro fichero).

Así `demo` demuestra de un vistazo lo último hecho: ver cobertura, consultar
altura y persistir entidades, sin la parte de descarga (esa sigue en `fill_map`).
Cambio de aplicación de ejemplo (no de librería): los 15 tests no se tocan.

**Estado: 15 tests verdes; `demo` enriquecido (cobertura + cota + persistencia),
build sin avisos, Qt 6.4.**

## 46. Repaso de la documentación y hoja de ruta actualizada

Una revisión completa de los documentos frente al código encontró que se habían
ido quedando atrás respecto a lo construido en §36–45. Ninguna incoherencia
afectaba al build, pero varias **contradecían** el estado real:

- **README:** la sección «Estado» seguía diciendo que las entidades «no se
  guardan solas» justo después de §44; faltaba `geo_to_tiles` en la tabla de
  herramientas; la «Estructura» no listaba `src/dem/` ni `src/io/` y describía
  `widget/` como «MapView y capa de teselas»; la nota «sin QCustomPlot» omitía
  `fill_map`; y la tabla de fases terminaba en la 8. Corregido todo, con las
  fases 9–11 (descarga avanzada, elevación, persistencia).
- **§35** proponía como pendiente lo que ya está hecho (persistencia, DEM) y
  hablaba de 13 tests. Se marca como superada y se sustituye por la lista de
  abajo, en vez de reescribirla: la bitácora es cronológica.
- **Skill `libmapa-docs`:** decía «13 tests» (son 15).
- **Comentarios de `fill_hgt`** (`main.cpp` y `qmake/fill_hgt.pro`): aún decían
  que descomprime con zlib; desde la corrección de §42 es miniz.
- **`CLAUDE.md` y la skill** solo describían el contenedor Linux en la nube. Se
  añade el entorno local (Windows, Qt 6.11.2 MinGW, Qt Creator): dónde está el
  build, cómo correr los tests y cómo generar el PDF con el Chrome/Edge de
  Windows.
- **Atribución:** `CLAUDE.md` prohibía poner un identificador de modelo en el
  repo y a la vez exigía una línea `Co-Authored-By` que lo lleva. Se aclara que
  esa línea final del commit es la **única** excepción, y se actualiza.
- `.qtcreator/` (configuración local de Qt Creator) va al `.gitignore`.

**Hoja de ruta vigente** (sustituye a §35):

*Alta:*
1. **Objetivos en vivo desde la BD:** conectar `vehiculo`/`buque_ais`/
   `trayectoria` de `VectorRepository` con `TargetModel`/`TargetLayer`.
2. **Rutas interactivas:** la BD guarda `ruta`/`ruta_punto`, pero no hay
   herramienta ni capa para dibujarlas o editarlas.
3. **Tests del código sin cubrir:** `TileFiller` (descarga) y `CoverageLayer`.

*Media:*
4. **Guardado incremental por id (UPSERT)** en `VectorRepository`, para que la
   persistencia de §44 escale a miles de entidades sin volcado completo.
5. **Fusionar bases regionales** (`merge_tiles`).
6. **Medición de área y perímetro** (hoy solo distancia entre dos puntos).
7. **Relieve a partir del DEM** (sombreado), ahora que ya hay elevación.

*Baja:*
8. Barra de escala, flecha norte, cuadrícula de coordenadas y leyenda.
9. Búsqueda por lugar/coordenada (geocoding).
10. Soporte vectorial OSM (`.pbf`/MVT).
11. Publicar la referencia Doxygen e internacionalización (`.ts`).

**Estado: cambio solo de documentación y comentarios, no requiere build;
15 tests verdes.**

## 47. Paquete de datos sin conexión: `mapa.json` y `MapConfig.dataDir`

**Cambio de rumbo.** Se aclaró el uso real: el producto final **no usa
internet**; solo trabaja con datos locales que viajan con la aplicación.
Internet queda para las herramientas que **preparan** esos datos (`fill_tiles`,
`fill_map`, `fill_hgt`). Eso reordena las prioridades: lo importante deja de ser
«leer más fuentes» (servidor de teselas, MBTiles) y pasa a ser que el conjunto de
datos **se instale, se encuentre y funcione** sin sorpresas. La hoja de ruta de
§46 queda en segundo plano; su punto 1 (objetivos desde la BD) se aparca porque
nadie lo necesita todavía: las tablas `vehiculo`/`buque_ais`/`trayectoria`
vienen copiadas del EstacionTerrena original (§25), no de un requisito.

**Lo que lo hacía difícil:**

- `probe_db` escribía **rutas absolutas** (`D:/QtPro/Recursos/...`): el
  `datasets.json` no servía en otro PC aunque el lector ya aceptaba relativas.
- Los datos estaban **repartidos** en tres ajustes de `MapConfig`
  (`datasetsFile`, `elevationDbFile`, `featuresDbFile`) sin idea de conjunto.
- La BD de entidades necesita **escribir**, y una app instalada en
  `Program Files` no puede escribir en su propia carpeta.
- Nada describía el conjunto: ni versión, ni zona, ni atribución.

**El paquete.** Una carpeta con todo y un manifiesto `mapa.json` (formato
`libmapa-package`, versión 2) con rutas **relativas** a ella:

- `package`: id, nombre, versión de los datos, fecha, zona (`bounds`) y
  **atribución** (OSM la exige; la app la lee con `MapWidget::packageInfo()`).
- `start`: capa, centro y zoom de arranque (la app ya no cablea La Habana).
- `datasets`: lo mismo que `datasets.json`; los campos omitidos toman su valor
  por defecto, así que ya no hace falta repetir `colZ`, `tableName`…
- `elevation`: `file` (BD) o `dir` (`.hgt`).
- `overlays`: capas vectoriales **fijas** `.geo` con su estilo.
- `features`: la BD de entidades del usuario y, opcional, una `seed` de partida.

Las claves van **en inglés**, como las del `datasets.json` que ya existía. Un
`datasets.json` versión 1 sigue valiendo (es un paquete con solo capas base), y
un manifiesto de una versión **futura** se rechaza con un mensaje claro en vez
de abrirse a medias.

**Decisiones:**

- **Un solo ajuste:** `MapConfig::dataDir`. Lo que la app rellene a mano **gana**
  al paquete. Para poder distinguir «no puesto» de «puesto», `initialCenter`
  pasa a ser inválido por defecto e `initialZoom` a −1; si nadie los pone se usan
  los de siempre (La Habana, 10), así que ninguna app existente cambia.
- **Lo que falta avisa, no rompe:** una capa base, la elevación o un `.geo`
  ausentes se anotan como aviso (log) y el resto del mapa funciona. Solo es
  fatal no tener manifiesto, que no sea JSON, otro formato/versión, o cero
  datasets.
- **Entidades fuera del paquete:** una ruta relativa en `features.file` se
  resuelve contra `AppDataLocation/<package.id>/` (escribible), **nunca** contra
  la carpeta del paquete. El id separa paquetes distintos. La `seed` se copia
  la primera vez y se le devuelven los permisos de escritura (la copia hereda el
  solo-lectura del original instalado). Nunca se pisa lo ya guardado.
- **Capas fijas que no se duplican:** se cargan en el `OverlayModel` como el
  resto (reutilizando `loadGeoAsLayer`), pero el widget recuerda sus ids
  (`fixedLayers`) y `saveFeaturesTo` las **salta**: si se guardaran, en cada
  arranque aparecerían dos veces. Quedan bloqueadas (capa no editable, entidad
  no seleccionable) y su carga no entra en el historial de deshacer. Como
  `setContents` reemplaza todo el modelo, `loadFeaturesFrom` las vuelve a poner,
  dentro del mismo grupo de deshacer.
- El lector (`src/io/DataPackage`) va en el **núcleo** (sin widgets) para
  probarlo sin pantalla; lo público es solo `DataPackageInfo`
  (`include/libmapa/DataPackage.h`), sin exponer `TileDataset`.

**Herramientas:** `probe_db` escribe las rutas **relativas a la carpeta del
`--out`** y, con `--package` (+ `--dem`, `--overlay`, `--features`), genera el
manifiesto completo con la zona del `--ref-bbox` y la fecha de hoy.
`render_map --data <carpeta>` y `demo <carpeta>` abren un paquete entero.

**El paquete real** `D:\QtPro\Recursos\mapa.json` (plantilla en el repo:
`mapa.example.json`) lleva los **cuatro** datasets que hay en `Recursos`
(`Cuba_OSM_CID3`, `Cuba_Satelital_CID3`, `Nueva_Clarity`, `Cuba_Vector`), la
elevación `cuba_dem.sqlitedb` y los cuatro `.geo` como capas fijas (`Aguas`,
`FIR`, `Corredores`, `Ejercitos`). Se respetaron los ajustes hechos a mano en el
`datasets.json` anterior, que la sonda no reproduce (zoom mínimo/fondo 3 en
`satelital`, 3/4 en `clarity`, `typicalFill` 1.0 en `costas`).
`render_map --data D:\QtPro\Recursos` lo dibuja entero con una sola opción.

**Tests:** nuevo `tst_datapackage` (todos los bloques, carpeta o fichero,
valores por defecto, versión 1, formato/versión ajenos, avisos por ficheros que
faltan, entidades en `AppData` con copia de la semilla) y tres casos en
`tst_mapwidget` (abrir solo con `dataDir`; la capa fija ni se guarda ni se
duplica al reabrir; lo manual gana al paquete; error claro si falta).

**Siguiente (pasos 2 y 3 del plan):** comprobar un paquete antes de
distribuirlo (ficheros, apertura, zonas y zooms cubiertos) y desplegar la
librería en otra app sin sorpresas (`install()`/`find_package`, plugins
`qsqlite`/imágenes, y un test que vigile que la librería no enlaza `Qt Network`).

**Estado: 16 tests verdes (13 sin QCustomPlot), sin avisos, Qt 6.11.2 MinGW.**

## 48. Comprobar el paquete: `check_data` y `MapWidget::dataWarnings()`

Con datos solo locales, lo que falte en el paquete **no se puede descargar
después**. Y lo que falla al instalar en otro PC falla en silencio: si falta el
plugin de imagen `qjpeg` (lo más típico al copiar una app Qt sin
`windeployqt`), las bases abren pero ninguna tesela se decodifica, y el mapa sale
**en blanco sin ningún mensaje**. Hacía falta decirlo antes de distribuir y
también al abrir.

**`PackageCheck`** (`src/io/`, núcleo sin widgets) recorre el paquete y anota
hallazgos con gravedad: **Error** si una parte del mapa no se dibujará
(fichero ausente, base que no abre o sin teselas, imágenes que no se decodifican,
BD de elevación inválida, `.geo` ilegible, ids repetidos, sin driver `QSQLITE`),
**Warning** si funciona pero hay algo que arreglar (sin atribución, sin zona,
capa de arranque inexistente, un fichero **fuera** de la carpeta que no viajará,
un nivel declarado sin ninguna tesela en la zona) e **Info** para el informe.
No escribe nada; la BD de entidades del usuario ni se abre (abrirla con
`VectorRepository` podría migrar su esquema).

Decisiones:

- **Decodificar una tesela de verdad**, no solo abrir la BD: es la única forma de
  detectar el plugin que falta. Se lee una tesela cualquiera del nivel de fondo
  (`RMapsTileSource::anyTile`, `LIMIT 1`) y se pasa por `QImageReader`. Si falla,
  el formato se identifica por los primeros bytes (sin el plugin, Qt ni siquiera
  lo reconoce) para que el mensaje diga **cuál** falta.
- **Cobertura por zoom con `COUNT(*)`** (`RMapsTileSource::countInRange`, mismo
  `WHERE` que la lectura, incluida `s`), no trayendo las claves: usa el índice
  `(z,x,y,s)`. Sobre el paquete real (6,7 GiB, cuatro capas, hasta z16) el informe
  completo tarda **~0,2 s**.
- **Dos modos.** El completo (cobertura) es para `check_data`. El rápido lo hace
  `MapWidget` al abrir un paquete (unos ms) y lo deja en `dataWarnings()`, además
  del log; el mapa **arranca igual** con lo que funcione y la aplicación decide si
  avisar (`demo` muestra un diálogo). Que falte una capa no debe impedir usar las
  demás.
- En `DataPackage::load` lo que falta era solo un aviso (para poder abrir el
  resto); en la comprobación es un **error** con su gravedad real.

**`check_data <paquete> [--quick] [--max-zoom N] [--strict]`** imprime una ficha
por capa (tamaño, formato, cobertura por zoom) y los hallazgos; sale con 1 si hay
errores (o avisos, con `--strict`) para usarlo en un script de empaquetado.

Lo que dice del paquete real `D:\QtPro\Recursos`: **0 errores, 0 avisos**;
OSM completo al 100 % hasta z15 (incluye mar); satelital completo hasta z12 y
~31 % desde z13 (solo tierra), **6,2 % a z16**; Clarity completo hasta z13 y 54,5 %
a z14; costas, solo la franja costera (5 % a z14, como corresponde). El dato de
z16 sugiere que `recommendedMaxZoom` de la satelital debería ser 15: a 16 casi
todo se vería ampliado desde el nivel anterior.

**Tests:** nuevo `tst_packagecheck` (paquete correcto sin hallazgos; cobertura
exacta en la zona; modo rápido sin cobertura; ficheros ausentes como errores;
BLOB que parece JPEG y no lo es → error que nombra el formato; BD de elevación
inválida; fuera de la carpeta / sin atribución / arranque inexistente como
avisos; nivel vacío en la zona; sin manifiesto) y un caso en `tst_mapwidget`
(paquete incompleto: arranca y `dataWarnings()` nombra lo que falta).

**Estado: 17 tests verdes (14 sin QCustomPlot), sin avisos, Qt 6.11.2 MinGW.**

## 49. Desplegar: `find_package(libmapa)`, `desplegar.bat` y nada de red

Con el paquete de datos resuelto (§47–48) faltaba lo otro: que una aplicación
**de fuera** use la librería sin copiar sus fuentes, y que esa aplicación se
pueda llevar a un PC **sin Qt y sin internet**.

**Instalación y `find_package`.** El CMake gana reglas de instalación
(`cmake --install build --prefix C:/libmapa`): las dos bibliotecas, solo las
cabeceras **públicas** (`include/libmapa`; `src/` es interno), un
`libmapaConfig.cmake` (desde `cmake/libmapaConfig.cmake.in`) que busca las
mismas dependencias de Qt con las que se compiló (5 o 6; Widgets/PrintSupport
solo si hay widget), y las herramientas. Los targets se ven como
`libmapa::core` y `libmapa::widget`, también con `add_subdirectory` (alias).
Las bibliotecas siguen siendo **estáticas**: no hay una DLL propia que
repartir ni versiones que casar. Se renombraron a `libmapa_core.a` /
`libmapa_widget.a` (salía `liblibmapa_core.a`).

`examples/app_minima` es la plantilla de producto: un proyecto aparte que solo
hace `find_package(libmapa)` y `cfg.dataDir = <exe>/datos`. Con `--comprobar`
no abre ventana: dice si el mapa arranca y lista `dataWarnings()`, y sale con 0
si todo está bien. Verificado: compilado contra la instalación (sin acceso a
las fuentes), abre el paquete real con 4 capas y 0 avisos.

**`herramientas/desplegar.bat <app.exe> <destino> [paquete]`:** copia el
`.exe`, ejecuta `windeployqt` y **comprueba** los tres plugins sin los que el
mapa falla en silencio (`platforms/qwindows`, `sqldrivers/qsqlite`,
`imageformats/qjpeg`), copiándolos a mano si faltan. Con el paquete, llama a
`check_data --quick --export <destino>\datos`.

**`check_data --export <carpeta>`:** si no hay errores, copia el `mapa.json` y
**solo** los ficheros que referencia (`DataPackage::files()`), respetando sus
rutas relativas. `Recursos` tiene además PDF, iconos y estilos que el mapa no
usa: copiar la carpeta entera los arrastraría. Es reanudable (lo ya copiado
con el mismo tamaño se salta), se niega si algún fichero queda fuera de la
carpeta, y al final comprueba la copia. `check_data` deja de mostrar el
registro interno de la librería salvo con `--verbose`.

**Sin red, verificado.** `objdump` sobre los binarios: la librería, `demo` y
`render_map` **no importan `Qt6Network.dll`** (solo `fill_map`, herramienta de
descarga, como debe ser); `Qt6Positioning` solo depende de `Qt6Core`. Pero
`windeployqt` **sí** copiaba `Qt6Network.dll`: la arrastraban plugins que el
mapa no usa (`tls`, `networkinformation`, `generic`) y el de posición `nmea`
pedía además `Qt6SerialPort`. Se excluyen con `--skip-plugin-types`; la
entrega ya no lleva nada de red. Y una guarda en CMake para el futuro: si
`libmapa_core` o `libmapa_widget` enlazan algo con `Network`, la configuración
se para con un mensaje claro.

**Prueba de despliegue:** `app_minima` desplegada y ejecutada con un `PATH`
reducido a `C:\Windows` (sin Qt) → arranca, 4 capas, 0 avisos. Quitando
`qjpeg.dll` de la copia → arranca igual y `dataWarnings()` dice que
**satelital y clarity** (JPEG) no se pueden decodificar por falta del plugin,
mientras OSM y costas (PNG) siguen funcionando. Es exactamente el fallo que
antes dejaba el mapa en blanco sin explicación.

(Un tropiezo de la prueba: lanzar la copia con `-platform offscreen` aborta,
porque `windeployqt` solo despliega el plugin de plataforma de Windows. No es un
fallo del despliegue; en un PC real no se pide `offscreen`.)

**Estado: 17 tests verdes (14 sin QCustomPlot), sin avisos, Qt 6.11.2 MinGW;
instalación, `find_package` y despliegue verificados de punta a punta.**

## 50. Segundo juego de despliegue: Qt 5.14 con qmake, y una guía para cualquiera

Un compañero trabaja con **Qt 5.14**, y el despliegue de §49 solo servía para
Qt 6. Fallaba en tres puntos:

- **`desplegar.bat`** tiene fijos Qt 6.11.2 y MinGW 13.1, y usa
  `windeployqt --skip-plugin-types`. Esa opción no existe en el `windeployqt`
  de Qt 5, que se pararía con «opción desconocida».
- **Las bibliotecas estáticas dependen del Qt y del compilador.** Un `.a`
  compilado con GCC 13 contra Qt 6 no enlaza en una app de Qt 5.14 con
  MinGW 7.3. Hay que compilar la librería en el PC del compañero.
- **`find_package` necesita CMake**, y alguien con Qt 5.14 y qmake no tiene
  por qué tenerlo. Además, la app de prueba de esta máquina también es de qmake,
  y hasta ahora se integraba copiando líneas a mano en su `.pro`.

Se decidió **no tocar** el despliegue de Qt 6, que ya funciona y está
verificado, y añadir un segundo juego con ficheros nuevos:

**`qmake/libmapa/` (librería con qmake).** `libmapa.pro` (subdirs) compila
`core`, `widget` y `check_data` sin CMake, con Qt 5.14, 5.15 o 6.x. Las
bibliotecas salen en **Release y Debug** (`mapa_core` / `mapa_cored`). En
Qt 5 MinGW, una app Debug usa otras DLL (`Qt5Cored.dll`), y mezclarla con una
biblioteca compilada contra las de Release cargaría dos copias de Qt. `make
install` las deja por defecto en `C:/libmapa/qt5` o `C:/libmapa/qt6`, para
poder tener las dos en el mismo PC, con la misma forma que la instalación de
CMake (`include/`, `lib/`, `bin/check_data.exe`, `share/libmapa/`). Instala
también el script de despliegue que corresponde a ese Qt. A los `.bat`
instalados hay que ponerles `CONFIG += nostrip`, porque si no qmake les
pasa `strip` como a un ejecutable.

**`libmapa.pri` (integración en una línea).** Tiene el mismo papel que el
`libmapaConfig.cmake`: `QT +=`, `DEFINES`, `INCLUDEPATH` y `LIBS` en el orden
correcto (el widget antes que el núcleo), las versiones `d` en Debug, y
`PRE_TARGETDEPS` para volver a enlazar si se reinstala la librería. Con
`include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)`, la misma línea elige
la instalación del Qt del kit. Junto a él se instala `libmapa_qt.pri`, con el
Qt con el que se compiló (lo genera `write_file`). Si la app es de otro Qt,
qmake se para con un mensaje que lo explica, en lugar de dar cientos de
errores de enlace. `examples/app_minima/app_minima.pro` es la plantilla qmake.

**`herramientas/desplegar_qt5.bat`.** Tiene los mismos argumentos y pasos que
`desplegar.bat`, con estas diferencias:

1. Antes de copiar nada, mira qué DLL de Qt importa el `.exe` (`findstr` sobre
   el binario):
   - `Qt5Core.dll`: Qt 5 Release, se sigue.
   - `Qt5Cored.dll`: Debug, se para y pide Release.
   - `Qt6Core.dll`: se para y remite a `desplegar.bat`.
2. Los plugins de red no se pueden excluir en `windeployqt`, así que se borran
   después:
   - `bearer` y `generic` (TUIO), que arrastran `Qt5Network`;
   - `position`, que arrastra `Qt5SerialPort`. El mapa no lee GPS: no hay
     ningún `QGeoPositionInfoSource` en el código.
3. Al final, recorre la entrega buscando quién importa todavía
   `Qt5Network.dll`, y **avisa** sin pararse. Sobre la entrega de Qt 6 esta
   búsqueda no da falsos positivos. Sin quitar esos plugins, en Qt 6 sí detecta
   `tls` y `networkinformation`, lo que demuestra que funciona.
4. Comprueba que el `check_data` que va a usar también es de Qt 5. Uno de Qt 6
   no arrancaría con las DLL de Qt 5.
5. Qt y MinGW por defecto: `C:\Qt\5.14.2\mingw73_64` y
   `C:\Qt\Tools\mingw730_64`. Se cambian con `QTDIR` y `MINGW_BIN`, por ejemplo
   para 5.15.2 con MinGW 8.1.

**`docs/DESPLIEGUE.md`.** Es una guía para alguien sin experiencia, con un
glosario, una tabla para elegir el juego, pasos numerados con lo que se debe
ver en cada uno, y una tabla de problemas frecuentes. Esa tabla recoge errores
reales de esta sesión:

- lanzar el `.bat` desde Git Bash, que se come las `\`;
- pensar que la copia se ha parado, porque Windows reserva el tamaño final del
  fichero desde el primer momento.

**Verificado en este PC (Qt 6.11.2):**

- `libmapa.pro` compila e instala, siguiendo la guía al pie de la letra en la
  consola «Qt 6.11.2 (MinGW 13.1.0 64-bit)».
- `app_minima.pro` enlaza en Release y en Debug contra la instalación y abre el
  paquete real: 4 capas, 0 avisos.
- Saltan las tres guardas de qmake: falta la instalación, Qt distinto y falta
  QCustomPlot.
- Con el `desplegar.bat` sin modificar sobre esa instalación, la copia
  funciona con un `PATH` sin Qt y no lleva DLL de red.
- De `desplegar_qt5.bat` se han probado:
  - sus dos guardas reales: no hay Qt 5 instalado, y la app es de Qt 6;
  - el resto de su lógica, en una copia adaptada a Qt 6 y con un paquete
    pequeño: plugins, aviso de red, `check_data` y copia del paquete.

**No verificado:** no hay ningún Qt 5.14 en esta máquina. Falta compilar y
desplegar con un Qt 5.14 real en el PC del compañero. El código de la librería
ya tenía en cuenta Qt 5 (§31), pero desde §36 solo se ha compilado con Qt 6.

**Estado: sin cambios en el código de la librería (siguen valiendo los 17 tests de §49); juego Qt 6
por qmake verificado de punta a punta con Qt 6.11.2 MinGW; juego Qt 5
preparado y pendiente de probar con Qt 5.14.**

## 51. El juego Qt 5 compila con un Qt 5 real, y la guía queda clara de punta a punta

§50 dejó el juego Qt 5 «preparado pero sin probar con un Qt 5 real». Esta
sesión lo ha probado hasta donde se puede sin Windows, y ha repasado
`docs/DESPLIEGUE.md` para que no falte ningún paso.

**El juego Qt 5 compila e instala con un Qt 5 de verdad.** En el contenedor se
instaló Qt 5.15.13 (`qtbase5-dev qtpositioning5-dev qtbase5-dev-tools`) y se
siguió el paso 2 de la guía con ese Qt:

- `qmake -qt=5 qmake/libmapa/libmapa.pro PREFIX=…` genera los Makefile sin que
  salte la guarda de «Qt 5.14 o posterior».
- `make` compila `mapa_core`/`mapa_cored` y `mapa_widget`/`mapa_widgetd` (las
  cuatro `.a`) limpio.
- `make install` deja la instalación con la forma esperada: `include/libmapa/`,
  `lib/*.a`, `bin/check_data`, `libmapa.pri`, `share/libmapa/desplegar_qt5.bat`
  y, sobre todo, `libmapa_qt.pri` con `LIBMAPA_QT_MAJOR = 5` y
  `LIBMAPA_QT_VERSION = 5.15.13` escrito por `write_file` al instalar.
- `examples/app_minima/app_minima.pro`, compilado con el mismo Qt 5 contra esa
  instalación, enlaza y **arranca** (sin pantalla, con `offscreen`).
- `check_data` de esa instalación funciona (uso y error ante un `mapa.json`
  ausente).

Con eso, de todo el juego Qt 5 solo queda sin ejecutar en un Windows real el
propio `desplegar_qt5.bat`, porque usa `windeployqt`, que no existe en Linux.
El resto de su lógica ya se había validado en §50 sobre una copia adaptada.
De paso se comprobó que el camino de CMake (`find_package(libmapa)`) sigue
bien bajo Qt 6, y que los 17 tests de §49 siguen verdes.

**`docs/DESPLIEGUE.md` más claro.** Era una petición explícita: que la guía
quede bien clara con todos sus pasos. Cambios:

- Un **resumen de los pasos (0–6) de un vistazo** al principio, con la nota de
  qué se hace una sola vez (0, 1, 2) y qué en cada entrega (3–6). Así se ve el
  mapa del proceso antes de entrar en el detalle.
- El **aviso del juego Qt 5** ya no dice «sin probar»: explica qué está
  verificado (compila, instala, el ejemplo enlaza y arranca, `check_data`, con
  Qt 5.15; el código es el mismo para 5.14) y acota lo único pendiente de un
  Windows real (el `windeployqt` de `desplegar_qt5.bat`).
- En el paso 1 (requisitos), una línea para quien **aún no tenga `mapa.json`**:
  se genera con `probe_db --package` (detalle en el README).

Es un cambio **solo de documentación** (`.md`), así que no necesita build ni
tests, y `DESPLIEGUE.md` no es `arquitectura.html`: tampoco hay que regenerar
el PDF.

**Estado: sin cambios en el código ni en los 17 tests; juego Qt 6 verificado de
punta a punta (Qt 6.11.2); juego Qt 5 verificado hasta compilar, instalar,
enlazar el ejemplo y `check_data` con un Qt 5.15 real, pendiente solo de
`desplegar_qt5.bat` en un Windows con Qt 5.14; guía de despliegue repasada.**

## 52. El juego Qt 6, verificado en el PC real del usuario (no solo en el contenedor)

Hasta §51 el juego **Qt 6** estaba verificado «en este PC» entendiendo por eso
el **contenedor Linux** (build con CMake y qmake, 17 tests). Faltaba el único
escenario que importa de verdad para entregar: **Windows real con Qt 6.11.2
MinGW**. El usuario lo ha recorrido ahora, paso a paso siguiendo
`docs/DESPLIEGUE.md`, y ha funcionado de punta a punta:

- **Instalar la librería (paso 2).** En la consola «Qt 6.11.2 (MinGW 13.1.0
  64-bit)»: `qmake ..\qmake\libmapa\libmapa.pro`, `mingw32-make -j4` y
  `mingw32-make install`. Quedó en `C:\libmapa\qt6` con la forma esperada: las
  cuatro `.a` (Release y Debug), `include\libmapa\*.h`, `bin\check_data.exe`,
  `libmapa.pri`, `libmapa_qt.pri` y `share\libmapa\desplegar.bat`.
- **Usar la librería desde una app (paso 3).** Abrió `examples/app_minima` con
  el mismo kit Qt 6; enlazó contra la instalación por la única línea
  `include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)`, sin tocar nada más.
  Apuntado a su paquete `D:\QtPro\Recursos` (pasado como **argumento de
  ejecución**), dibujó el mapa.
- **Release + desplegar (pasos 4–5).** Recompiló en Release y lanzó
  `desplegar.bat <app_minima.exe> D:\Entrega D:\QtPro\Recursos` desde `cmd`:
  terminó con `Listo: D:\Entrega` (el `.exe`, las DLL de Qt sin las de red, los
  plugins y el paquete de datos filtrado por `check_data`).

Con esto, del lado **Qt 6** ya no queda nada «no verificado en Windows real».

**Un detalle que la guía no recogía.** Al ejecutar el ejemplo **sin argumento**
salió `libmapa.render: "No se encuentra el manifiesto …\datos\mapa.json"`.
Parece un error, pero es justo lo contrario: demuestra que la librería **está
enlazada y corriendo** (es ella quien emite el mensaje); lo único que falta es
decirle **dónde están los datos**. Sin argumento busca una carpeta `datos`
junto al `.exe` —que en el build no existe— mientras que `app_minima` toma esa
carpeta como argumento (`app_minima <carpeta>`), que en Qt Creator se pone en
*Proyectos → Ejecución → Argumentos de la línea de órdenes*. Confundir ese
mensaje con un fallo de instalación es fácil, así que se documenta.

**Afinado de `docs/DESPLIEGUE.md`** (solo documentación):

- Nuevo apartado **3.3 «Probar con el ejemplo `app_minima`»**: cómo verlo con
  datos sin escribir código, pasando la carpeta del paquete como argumento de
  ejecución; el antiguo «Comprobar que compila» pasa a 3.4. En 3.2 se aclara que
  editar `cfg.dataDir` es para **tu** app, no para el ejemplo.
- La aclaración del mensaje de `mapa.json` queda en 3.3 y la fila de *Problemas
  frecuentes* se amplía con los tres casos (ejemplo en Qt Creator / tu app / la
  entrega).
- Nota en el paso 2: para **reinstalar** tras un cambio basta
  `mingw32-make -j4 && mingw32-make install` dentro de `build-qt6`; el `qmake`
  solo se repite si se borra esa carpeta.
- Aviso al principio del paso 1 para quien **clona el repo**: QCustomPlot y el
  paquete de datos **no vienen en git** (licencia y tamaño), hay que
  conseguirlos aparte; el resto sí llega con el `git clone`. Pensado para el
  compañero que montará el juego Qt 5 en su PC.
- Nuevo `docs/RESUMEN_QT5.md`: la versión corta del juego Qt 5 (clonar →
  compilar/instalar → probar el ejemplo → desplegar), para que el compañero la
  tenga con el `git clone` sin depender de un reenvío por chat. `DESPLIEGUE.md`
  enlaza a él desde el resumen de pasos.

Cambio **solo de documentación** (`.md`): no toca código ni tests, y
`DESPLIEGUE.md` no es `arquitectura.html`, así que no hay que regenerar el PDF.

**Estado: sin cambios en el código ni en los 17 tests; juego Qt 6 verificado de
punta a punta también en Windows real (Qt 6.11.2 MinGW): instalar, usar desde
`app_minima` y desplegar con `desplegar.bat`; juego Qt 5 como en §51 (pendiente
solo de `desplegar_qt5.bat` en un Windows con Qt 5.14); guía con el paso 3
afinado.**

## 53. Integración continua: compilar y pasar los 17 tests en cada push/PR (Qt 6 y Qt 5)

De cara a pasar a `main`, el repo no tenía **CI**: nada verificaba de forma
automática que compila y pasa los tests, y menos aún el objetivo **multi-Qt**
(5.14/5.15/6.x), que hasta ahora solo se comprobaba a mano. Se añade
`.github/workflows/ci.yml` (GitHub Actions):

- **Matriz de dos jobs en paralelo**, uno con **Qt 6** y otro con **Qt 5**, en
  `ubuntu-latest`. Cada job instala **solo su Qt** por `apt`
  (`qt6-base-dev …` / `qtbase5-dev …` + `*-positioning-dev` y el driver
  `*sql*-sqlite`), de modo que `find_package(QT NAMES Qt6 Qt5 …)` del
  `CMakeLists.txt` detecta la versión sin ambigüedad.
- **QCustomPlot 2.1.1** no está en git (GPLv3): el workflow lo **descarga** de
  `qcustomplot.com` y copia `qcustomplot.{h,cpp}` a `third_party/qcustomplot/`,
  como indica `CLAUDE.md`. Así compilan también el widget y sus 3 tests (17 en
  total; sin QCustomPlot serían 14).
- Compila en Release y corre `ctest` con `QT_QPA_PLATFORM=offscreen` (el runner
  no tiene pantalla, igual que el contenedor).
- Dispara en push a `main` y a ramas `claude/**`, en PR hacia `main` y a mano
  (`workflow_dispatch`). `concurrency` cancela runs superados de la misma rama.

También se añade el **badge de CI** al principio del `README.md`. Es el primer
fichero bajo `.github/`; no cambia el código de la librería (siguen los 17
tests) y no toca `arquitectura.html` (sin PDF que regenerar).

**Estado: CI en marcha (Qt 6 y Qt 5, 17 tests offscreen); sin cambios en el
código de la librería; queda, para `main`, validar `desplegar_qt5.bat` en un
Windows con Qt 5.14 real y abrir el PR de la rama con su resumen.**

## 54. Objetivo móvil extensible: `kind` + `attributes` (Fase 1 del alcance)

Tras acordar el **alcance** de la librería (motor de representación de objetos
móviles sobre mapa offline, agnóstico del dominio, para apps de seguimiento
naval/aéreo/UAV), esta es la primera fase. El objetivo: que un mismo `MapTarget`
sirva a cualquier dominio sin que la librería conozca su semántica.

Hallazgo que facilitó todo: la fachada **ya era agnóstica** (`MapTarget` y
`addTarget/updateTarget/...` no sabían de barcos). El dominio naval (`vehiculo`,
`buque_ais`) vive solo en el esquema SQLite legado, aparte del motor de tracks.
Así que la Fase 1 fue aditiva, sin tocar el dominio.

Cambios:

- `include/libmapa/MapTarget.h`: dos campos nuevos. `QString kind` (clase que la
  app asigna: "buque", "aeronave", "uav"…, para elegir símbolo o filtrar) y
  `QVariantMap attributes` (datos libres del objetivo: mmsi/imo para AIS,
  callsign/squawk para ADS-B, batería/enlace para un UAV). La librería los lleva
  y los devuelve **tal cual, sin interpretarlos**. Se incluye `<QVariant>` (no
  `<QVariantMap>`, que como cabecera suelta no existe en Qt 5.14; el typedef
  viene de `<QVariant>`).
- `src/widget/TargetModel.{h,cpp}`: `upsert` ya guardaba el `MapTarget` completo,
  así que `kind`/`attributes` viajan sin cambios y la vía rápida `update` (solo
  posición/rumbo) **no los pierde**. Añadidos `setAttribute(id, clave, valor)` y
  `attribute(id, clave)` para colgar/leer datos de dominio en caliente.
- `include/libmapa/MapWidget.h` + `src/widget/MapWidget.cpp`: reenvíos finos
  `setTargetAttribute` / `targetAttribute`, en línea con `setTargetLabel`.
- `tests/tst_targetmodel.cpp`: caso nuevo `carriesKindAndAttributes` (upsert con
  kind+atributos, que `update` los conserva, set/get en caliente, e id/clave
  inexistentes → QVariant inválido / false). Siguen los **17 tests** en verde,
  sin warnings (`-Wall -Wextra -Wconversion -Wold-style-cast`).
- Docs: `arquitectura.html` (descripción de `MapTarget` y fila de API) + **PDF
  regenerado**; README con una línea de "seguimiento agnóstico del dominio".

Es cambio de **API público** (solo aditivo: no rompe nada existente). Fases que
siguen: simbología por hooks (2), escala a miles (3), corte limpio del dominio
legado (4) y ejemplo de seguimiento + contrato público (5).

**Estado: 17 tests en verde (Qt 6 local; el CI los repite en Qt 6 y Qt 5);
`MapTarget` ya es extensible por la app. Siguiente: Fase 2 (simbología por
hooks).**
