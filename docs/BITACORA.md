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
