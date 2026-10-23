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

## 55. Simbología por hooks: la app trae los iconos (Fase 2 del alcance)

Hasta ahora la capa de objetivos pintaba un símbolo fijo (un galón girado por el
rumbo). Para que la librería siga siendo **agnóstica del dominio** pero cada app
(naval, aérea, UAV) muestre sus propios iconos, la simbología pasa a ser un
**hook**: la app registra un proveedor y la librería se limita a colocar y girar
lo que ese proveedor devuelva.

Cambios:

- Nueva cabecera pública `include/libmapa/TargetSymbol.h`: `struct TargetSymbol`
  (`QPixmap icon`; `bool rotateWithHeading`; `double scale`; `QPointF anchor`) y
  el typedef `TargetSymbolProvider = std::function<TargetSymbol(const MapTarget&)>`.
  El proveedor recibe el objetivo COMPLETO (con `kind`/`attributes`), así que la
  app elige icono por tipo, por estado o por cualquier dato suyo. Icono nulo =>
  la librería usa el galón por defecto (un proveedor puede decorar solo algunos
  tipos y dejar el resto genérico).
- `src/widget/TargetLayer.{h,cpp}`: guarda el proveedor (`setSymbolProvider`) y,
  en `drawTarget`, si devuelve un icono lo dibuja (trasladado a la posición,
  girado por el rumbo si se pide, escalado y anclado; respeta el
  `devicePixelRatio` para HiDPI); si no, cae al galón de antes.
- `include/libmapa/MapWidget.{h,cpp}`: reenvío `setTargetSymbolProvider`, que lo
  pasa a la capa de objetivos vía `MapView::targetLayer()`.
- `tests/tst_mapwidget.cpp`: caso `usesTargetSymbolProvider` (coloca un objetivo
  en el centro, registra un proveedor que cuenta invocaciones y da un icono por
  `kind`, fuerza el render con `grab()` y comprueba que el proveedor se llamó con
  el objetivo completo). 17 tests en verde, sin warnings.
- Docs: `arquitectura.html` (bullet de objetivos, fila de API y de `TargetLayer`)
  + PDF regenerado; README con la simbología por hooks.

API **aditivo**: sin proveedor, el comportamiento es el de antes. No se añadió
ningún `.cpp` (la cabecera nueva es header-only), así que CMake y qmake no
cambian.

**Estado: 17 tests en verde; la app ya puede traer su juego de iconos. Siguiente:
Fase 3 (escala a miles: culling por vista, nivel de detalle y poda de traza, con
un banco que lo mida).**

## 56. Escala a miles de objetivos: culling, nivel de detalle y declutter (Fase 3)

El alcance fija **miles** de móviles (ADS-B regional). El dibujo ya hacía culling
por objetivo, pero a esa densidad lo caro y lo ilegible son las **etiquetas**
(cada una con 8 trazos de halo) y las **trazas** (una polilínea por objetivo).
`TargetLayer` se reescribe en dos pasadas:

- **Pasada 1 (culling):** recorre el modelo una vez y recoge en un buffer
  reusado solo los objetivos cuya posición cae en pantalla (con margen de 256
  px) junto con su posición ya proyectada.
- **Pasada 2 (dibujo) con nivel de detalle:** según cuántos quedaron visibles,
  decide si dibujar etiquetas y trazas. Por encima de un **presupuesto**
  (`setTargetDetailBudget`, por defecto 150 etiquetas / 400 trazas) se apagan
  (el símbolo se dibuja siempre). Además:
  - **Declutter de etiquetas:** una sola etiqueta por celda de pantalla (~40 px),
    para que no se amontonen; el resto se omite.
  - **Decimación de traza:** se saltan los puntos a menos de 2 px del último
    dibujado (una traza larga al alejar el zoom son muchos puntos pegados).
- Contadores `lastLabelsDrawn()`/`lastTrailsDrawn()` para diagnóstico y test.

Fachada: `MapWidget::setTargetDetailBudget(maxLabels, maxTrails)`.

Banco/medida: en vez de una herramienta aparte, el test
`tst_mapwidget::scalesToThousandsOfTargets` monta **3025 objetivos** en el área
visible (con etiqueta y traza), fuerza el repintado de la capa (BUFFERED, vía
`QCPLayer::replot`) y comprueba de forma **determinista** el nivel de detalle:
con presupuesto bajo, 0 etiquetas y 0 trazas; con presupuesto holgado, vuelven
pero el declutter deja muchas menos etiquetas (~108) que objetivos (3025). Deja
en el log el tiempo del render (del orden de ~20 ms en el contenedor). 17 tests
en verde, sin warnings (se corrigió un `-Wconversion` de `size()`).

API **aditivo**; la cabecera de símbolos sigue siendo header-only, así que CMake
y qmake no cambian.

**Estado: 17 tests en verde; el motor de objetivos escala a miles con nivel de
detalle. Con esto el motor de seguimiento está completo (Fases 1–3). Siguiente:
Fase 4 (corte limpio del dominio naval legado) o Fase 5 (ejemplo de seguimiento
+ contrato público).**

## 57. Ejemplo de seguimiento en `demo` y contrato público (Fase 5)

Las Fases 1–3 dejaron el motor de seguimiento completo pero **invisible**: nada
en el repo lo usaba. Esta fase lo hace demostrable enriqueciendo el `demo` (que
ya tenía un simulador de objetivos) y documentando cómo se usa. **Solo cambia la
app de ejemplo y la documentación; la librería y su API no se tocan.**

En `demo/main.cpp`:

- El simulador crea objetivos de tres clases con `kind` **"buque" / "aeronave" /
  "uav"**, cada uno con sus **attributes** de dominio (buque: `mmsi`, `eslora`;
  aeronave: `callsign`, `squawk`, `fl`; uav: `bateria`, `enlace`). La etiqueta se
  compone de esos atributos → se ven en pantalla (Fase 1).
- `prepararSeguimiento()` dibuja el **juego de iconos en código** (sin ficheros:
  un casco, una silueta de avión y un cuadricóptero, con `QPainter` → `QPixmap`)
  y registra `setTargetSymbolProvider`: elige el icono por `kind` y, para un UAV
  con `bateria` < 20, una variante **en rojo** → "icono por estado" (Fase 2). Un
  `kind` sin icono caería al galón por defecto.
- Fija `setTargetDetailBudget(200, 600)`; el spinbox ya llegaba a 5000, así que
  subir la cantidad enseña el nivel de detalle a miles (Fase 3).

Documentación del **contrato público**: nueva sección «Seguimiento de objetivos
móviles» en el README con el patrón mínimo (proveedor de símbolos, alta con
`kind`+`attributes`, actualización desde el feed, presupuesto de detalle) y la
frase clave —la librería **representa**; los protocolos (AIS/ADS-B/MAVLink) y el
control los pone la app—; una línea en `arquitectura.html` + **PDF regenerado**.

Verificación: `demo` compila; los **17 tests** siguen en verde (la librería no
cambió), sin warnings. La validación visual la hace el usuario en su PC
(arrancar `demo` → simular → buques/aeronaves/UAVs con su icono y datos).

Fuera de alcance (anotado): "clic en un objetivo → panel con sus datos" pediría
hit-testing de objetivos en la librería (hoy la selección es solo de entidades);
los atributos se ven por ahora vía la etiqueta.

**Estado: motor de seguimiento completo y demostrado en `demo` (Fases 1–5
hechas); 17 tests en verde. Queda, cuando toque, la Fase 4 (corte limpio del
esquema naval legado), que encaja mejor al reimplementar Estación Terrena.**

## 58. Hit-testing de objetivos: clic en un objetivo → sus datos

Cerraba el ciclo "ver → seleccionar → consultar": se podían ver los objetivos
pero no **pinchar uno y leer sus `attributes`**. Es lo que piden las apps de
seguimiento (seleccionar un contacto y ver su ficha). Se añade como capacidad de
la librería, reusando el patrón de `featureClicked`.

- `MapView`: `targetAt(pixel, tolPx=14)` proyecta cada objetivo a pantalla (misma
  transformación que `TargetLayer`) y devuelve el más cercano dentro de la
  tolerancia, o -1. En `mousePressEvent`, caso `MapTool::None`: si el clic cae
  sobre un objetivo, lo **selecciona** y emite `targetClicked(id, pos)` **sin**
  iniciar el arrastre; sobre mapa vacío, arrastra como siempre. `setSelectedTarget`
  guarda el id y lo pasa a la capa.
- `TargetLayer`: `setSelected(id)`; el objetivo resaltado se dibuja con un **halo**
  ámbar y se le **fuerza la etiqueta** aunque el nivel de detalle o el declutter
  las hayan apagado (para poder leer su ficha en zona densa).
- `MapWidget`: reenvíos `targetAt`, `setSelectedTarget`, `selectedTarget` y la
  señal `targetClicked(id, pos)`.
- `demo`: al recibir `targetClicked`, muestra `kind` + `attributes` del objetivo
  en la barra de estado (el resaltado lo pone ya la librería).
- `tests/tst_mapwidget.cpp`: caso `selectsTargetByClick` (targetAt acierta encima
  y da -1 lejos; un clic emite `targetClicked` con el id correcto y deja
  `selectedTarget()` fijado). 17 tests en verde, sin warnings.
- Docs: README (sección de seguimiento) y `arquitectura.html` + PDF.

API **aditivo**. Sin cambios en CMake/qmake (solo edición de ficheros ya
listados).

**Estado: ciclo de seguimiento completo (ver, simbología, escala, y ahora
selección/consulta por clic); 17 tests en verde. Pendiente opcional: Fase 4
(corte limpio del dominio naval legado) y consolidar la rama en `main`.**

## 59. Corte limpio del modelo vectorial legado (Fase 4)

El esquema SQLite arrastraba, de la migración desde el CBDatos original, un
**modelo vectorial legado** completo: `punto`, `vehiculo`, `buque_ais`,
`trayectoria`, `poligono`/`poligono_vertice`, `ruta`/`ruta_punto`, y además unas
tablas `feature`/`feature_vertice`/`feature_capa` **huérfanas** (se creaban pero
nadie las usaba). Al explorarlo se confirmó que **nada de la librería viva lo
usa**: `MapWidget` persiste las entidades en las tablas `entidad`/`capa` a través
de `VectorRepository`; esos tipos y métodos legados solo los ejercían el tool
`vector_db` y su test. El usuario pidió el corte limpio **total**.

Cambios:

- **`src/db/Schema.cpp`:** `migrations()` crea ahora solo `entidad`,
  `entidad_vertice` y `capa` (lo que de verdad usa la persistencia de entidades).
  Fuera todo el modelo naval/CBDatos y las tablas `feature*` huérfanas.
  `kCurrentVersion` se mantiene en 2, así que las BD de entidades existentes
  siguen válidas.
- **`src/db/VectorRepository.{h,cpp}`:** se eliminan las APIs de Puntos,
  Vehículos, Trayectorias, Polígonos y Rutas (`insertPoint`, `insertVehicle`,
  `appendTrack`, `insertPolygon`, `insertRoute`, …). Queda solo el almacén de
  entidades de dibujo y capas (`saveFeature(s)`, `loadFeatures`,
  `removeFeatureRow`, `clearFeatures`, `saveLayer`, `loadLayers`) que usa
  `MapWidget`. Doc de la clase actualizada.
- **`include/libmapa/MapTypes.h`:** se retiran los structs `MapPoint`,
  `VehicleKind`, `AisData`, `MapVehicle`, `TrackSample`, `MapPolygon`,
  `MapRoutePoint`, `MapRoute` y sus `Q_DECLARE_METATYPE`. Quedan `BaseLayer`,
  `MapTool`, `BaseLayerInfo` y `Measurement`.
- **Tool `vector_db`:** eliminado (`tools/vector_db/` + su `add_executable` y su
  entrada en la lista de instalación de CMake). Era la única app que tocaba el
  modelo legado.
- **`tests/tst_vectorrepository.cpp`:** reescrito para cubrir solo la API que
  queda (esquema/versión, idempotencia, guardar/cargar/borrar/limpiar Features,
  filtrar por capa, multi-parte, seguridad ante comillas/inyección, capas,
  geometría inválida, cero DDL en runtime, propagación de errores y la
  comparación transacción-vs-commits-sueltos). Siguen **17 tests** en verde, sin
  warnings.
- **Docs:** README (fuera `vector_db` de la tabla y de la referencia de
  comandos) y `arquitectura.html` (fila de tipos, tabla y sección de
  herramientas) + PDF.

Qt y qmake sin cambios: no se retiró ningún `.cpp` del núcleo
(`Schema.cpp`/`VectorRepository.cpp` siguen, más pequeños), y `vector_db` no
tenía proyecto qmake.

**Estado: VectorRepository es ya solo el almacén de entidades que usa MapWidget;
esquema sin modelo legado; 17 tests en verde. Con esto, el alcance acordado de la
librería (Fases 1–5 + hit-testing + corte limpio) queda cerrado.**

## 60. Análisis de elevación: perfil de una ruta (Fase A)

Con el seguimiento cerrado, el usuario pidió **cálculos de elevación** sobre el
DEM que la librería ya tiene (`IElevationSource`, bilineal, hueco→NaN). Son tres
capacidades, de visibilidad **directa** (nada de radar: sin horizonte radioeléctrico
`4.12√h`, sin haz, sin factores empíricos): **A)** perfil del terreno a lo largo de
una ruta, **B)** visibilidad punto a punto con altura de antenas y curvatura 4/3, y
**C)** viewshed 360° (perfil + ángulo de cierre + zona de visibilidad a una altura).
Se revisó `DVD_potencial` como referencia física y se confirmó que su constante de
curvatura `d²/17e6` es `d²/(2·k·R)` con radio efectivo **k=4/3** y R=6371 km — la
misma física que usarán B y C. Esta sección cubre la **Fase A**.

Decisión de alcance de A: el **perfil de ruta** es la cota del terreno EN EL LUGAR,
no desde un punto; por eso **no** aplica curvatura (es altura real, para dibujar el
corte o medir desniveles). La curvatura entra solo en los cálculos de visibilidad
(B y C). Muestreo configurable, **por defecto 30 m**: como `elevationAt` es bilineal,
muestrear a 30 m sobre un DEM de 90 m interpola sin coste extra de datos.

Cambios:

- **`include/libmapa/Elevation.h` (cabecera pública nueva):** tipos de resultado
  `ElevationSample{distanceM, position, elevation}` (NaN = sin dato),
  `ElevationProfile{samples, totalDistanceM, min/maxElevation, gain, loss}` y los
  parámetros `ElevationProfileParams{stepMeters=30}`. Se instala por el glob de
  `include/libmapa/*.h`.
- **`src/dem/ElevationAnalysis.{h,cpp}` (núcleo nuevo):** función libre
  `elevationProfile(const IElevationSource&, path, params)`. Camina la polilínea
  vértice a vértice con `QGeoCoordinate::atDistanceAndAzimuth` y `GeoMath`,
  muestreando a paso uniforme a lo largo de TODA la ruta (lleva un "resto" entre
  tramos para no reiniciar el paso en cada vértice) e incluye siempre el último
  punto exacto. Estadísticas (min/máx/ganancia/pérdida) que ignoran las muestras
  sin dato. Con menos de dos vértices válidos → perfil vacío.
- **Fachada `MapWidget`:** reenvío fino `elevationProfile(path, params)` que usa el
  origen DEM configurado (`d->elevation`); sin origen, perfil vacío.
- **`tests/tst_elevationanalysis.cpp` (nuevo, test 18):** `.hgt` sintético N19W077
  de lado 7 en rampa (`col·100+row`) con un hueco en el nodo (1,1). Comprueba el
  perfil a lo largo de una línea (muestras cada 30 m, distancia creciente, cuesta
  arriba = solo ganancia, extremos exactos que coinciden con `elevationAt`), que la
  distancia total de una ruta multi-tramo suma los tramos, y el tratamiento de
  huecos→NaN (una línea que arranca en la banda bilineal del hueco y sale a terreno
  con dato: hay muestras NaN y, aun así, estadísticas de las válidas) y de rutas
  degeneradas / sin origen DEM (geometría válida pero cotas NaN).
- **Build:** `src/dem/ElevationAnalysis.cpp` añadido a `libmapa_core` en
  `CMakeLists.txt` y, en espejo, en `qmake/libmapa/core/core.pro`;
  `libmapa_add_test(tst_elevationanalysis)`.
- **Docs:** README (sección «Análisis de elevación», recuento 17→18, hito 16) y
  `arquitectura.html` + PDF.

**Estado: perfil de ruta disponible en el núcleo y por la fachada; 18 tests en
verde, sin warnings. Siguientes: Fase B (visibilidad punto a punto) y Fase C
(viewshed 360°).**

## 61. Análisis de elevación: visibilidad punto a punto (Fase B)

Segunda capacidad: la **línea de visión** entre dos puntos con **altura de antena
en cada extremo**. A diferencia del perfil de ruta (altura real del terreno), aquí
**sí** se corrige el abombamiento de la Tierra, porque lo que se decide es si una
recta en el espacio queda despejada.

Física (la misma de `DVD_potencial`, revisada en la fase de diseño): el
abombamiento de la superficie sobre la cuerda recta A–B en un punto intermedio que
dista `d1` de un extremo y `d2` del otro es `d1·d2/(2·k·R)`, con R=6371 km y radio
efectivo **k=4/3** (refracción estándar; k=1 = geométrico puro). Equivale a la
constante `d²/17e6` de DVD. Se **suma al terreno** y se compara con la recta entre
las cimas de antena: `holgura = recta − (terreno + abombamiento)`. El mínimo de esa
holgura a lo largo del trayecto decide la visión y marca el punto crítico.

Cambios:

- **`include/libmapa/Elevation.h`:** tipos nuevos `LineOfSightParams`
  (`stepMeters=30`, `curvature=true`, `k=4/3`, `earthRadiusM`) y
  `LineOfSightResult` (`clear`, `clearanceM` —holgura mínima, negativa = cuánto
  falta—, `blockPosition`/`blockDistanceM` —punto crítico—, `isValid()`).
- **`src/dem/ElevationAnalysis.{h,cpp}`:** función libre
  `lineOfSight(src, a, b, antennaA, antennaB, params)` y el helper interno
  `abombamiento(d1, d2, k, R)` (que reutilizará el viewshed). Camina el rayo
  geodésico A→B con `atDistanceAndAzimuth`, salta las muestras sin dato (un hueco
  no afirma nada), y devuelve resultado inválido si falta la cota de un extremo
  (no se puede anclar la recta).
- **Fachada `MapWidget`:** `lineOfSight(a, b, antennaA=0, antennaB=0, params)`
  sobre el origen DEM configurado (inválido si no hay origen).
- **`tests/tst_elevationanalysis.cpp`:** tres casos nuevos (el ejecutable sigue
  siendo uno, con más slots): una colina central que bloquea a ras de suelo y se
  despeja al subir las antenas; terreno plano a cota 0 donde la única obstrucción
  es la curvatura, comprobando que la holgura mínima = −D²/(8·k·R) con k=4/3 (y que
  desactivar la curvatura deja la vista justo a ras); y casos inválidos (sin origen
  DEM, o un extremo sobre un hueco SRTM).
- **Docs:** README (visibilidad en la sección «Análisis de elevación», hito 17),
  `arquitectura.html` (8c) + PDF.

No cambian ni CMake ni qmake: no se añadió ningún `.cpp` (todo va en el
`ElevationAnalysis.cpp` ya listado), y los casos nuevos son slots del test
existente, así que **siguen 18 tests** en verde, sin warnings.

**Estado: visibilidad punto a punto disponible en el núcleo y por la fachada.
Siguiente: Fase C (viewshed 360° — perfil + ángulo de cierre + zona de
visibilidad a una altura).**

## 62. Análisis de elevación: viewshed 360° (Fase C)

Tercera y última capacidad: el **viewshed** desde un punto. Por cada azimut (0..360
a paso configurable, 1° = 360 rayos) se camina el rayo geodésico y se calcula el
**ángulo de cierre** del terreno, el **horizonte acumulado** con sus **picos** (la
silueta), y hasta dónde se ve un **objetivo a una altura H** (la zona de visibilidad
directa, el polígono ZVD que pedía el usuario). Cierra el alcance de análisis de
elevación acordado tras revisar `DVD_potencial`.

Física (reutiliza la de la Fase B). Observador en el origen con plano horizontal
tangente; para un punto a distancia `d`: caída de la Tierra bajo la tangente
`d²/(2·k·R)` (= `abombamiento(d, d, k, R)`, envuelto en el helper `caida`), altura
respecto al observador `y = (terreno + [H] − zObs) − caída`, y **ángulo de cierre**
`atan2(y, d)` en grados con su tangente (no la aproximación de ángulo pequeño
`3440·Δh/d` de DVD; usamos `atan2`). El horizonte es el máximo acumulado del ángulo
del terreno; un objetivo a altura H es visible mientras su ángulo supere ese
horizonte acumulado hasta esa distancia. Es visibilidad **directa**, no radar: el
horizonte geométrico (`≈4.12·√h`) **emerge** de la geometría; no se codifica la
fórmula. El test lo confirma: sobre terreno plano el alcance con curvatura sale
26040 m frente a los 26069 m de `sqrt(2·k·R)·(√hObs+√H)`.

Cambios:

- **`include/libmapa/Elevation.h`:** tipos nuevos `ViewshedParams` (`stepMeters=30`,
  `azimuthStepDeg=1`, `maxRangeM=50000`, `observerHeight`, `targetHeight`,
  `curvature`, `k=4/3`, `earthRadiusM`, `keepProfiles=false`), `ClosingAnglePeak`
  (`distanceM`, `position`, `elevation`, `angleDeg`, `tangent`), `ViewshedRay`
  (`azimuthDeg`, `visibilityReachM`, `horizonDeg`, `peaks`, `profile`) y `Viewshed`
  (`origin`, alturas, `rays`, `isValid()`).
- **`src/dem/ElevationAnalysis.{h,cpp}`:** función libre
  `computeViewshed(src, origin, params)` (+ helper interno `rayoViewshed` por azimut
  y `caida(d,k,R)`). El objetivo de un punto se comprueba contra el horizonte de lo
  MÁS cercano (antes de incorporar el terreno de ese mismo `d`, que no se tapa a sí
  mismo); con H=0 es el viewshed del propio terreno. Huecos/fuera de cobertura se
  saltan. **Memoria acotada:** con `keepProfiles=false` (defecto) cada rayo guarda
  solo picos y escalares (360 rayos → trivial); `keepProfiles=true` llena el perfil
  por rayo.
- **Fachada `MapWidget`:** `viewshed(origin, params)` sobre el origen DEM
  configurado (inválido si no hay origen).
- **`tests/tst_elevationanalysis.cpp`:** cuatro casos nuevos (slots del test
  existente): colina al este → pico de la silueta a distancia conocida y horizonte
  mucho mayor que hacia el oeste (plano); el alcance de visibilidad crece con la
  altura del objetivo; terreno plano → alcance finito que coincide con el horizonte
  geométrico (y, sin curvatura, hasta el alcance máximo); casos inválidos. Un
  `qDebug` registra el tiempo de un viewshed 360° (≈136 ms a 30 m, 50 km). Siguen
  **18 tests** en verde, sin warnings.
- **Docs:** README (viewshed en «Análisis de elevación», hito 18),
  `arquitectura.html` (8c) + PDF.

Sin cambios en CMake ni qmake: todo va en `ElevationAnalysis.cpp` y los casos son
slots del test existente.

**Estado: las tres capacidades de análisis de elevación (perfil de ruta, visibilidad
punto a punto, viewshed 360°) están en el núcleo y por la fachada, verdes (18 tests),
solo visibilidad directa. Queda cerrado lo pedido tras revisar `DVD_potencial`.**

## 63. El `demo` prueba el análisis de elevación

Las tres capacidades (Fases A–C) estaban en la librería pero **invisibles**: el
`demo` solo mostraba la cota bajo el cursor. Se le añade una barra **Elevación**
para probarlas, **sin quitar nada** de lo que ya había (es cambio solo de la app de
ejemplo; no toca la librería, su API ni los tests).

Qué se añadió (en `demo/main.cpp`):

- **Barra «Elevación»** (`construirBarraElevacion`), con dos alturas `Alt1`/`Alt2`
  (antena A/B para la visión; observador/objetivo para el viewshed) y un `Alcance`
  (radio del viewshed), más cuatro acciones:
  - **Perfil:** sobre la línea/polígono seleccionado llama a
    `MapWidget::elevationProfile` y abre un diálogo con el **corte del terreno**
    dibujado (clase `PerfilWidget`, QPainter) y las estadísticas (distancia,
    mín/máx, subida/bajada). Las muestras sin dato dejan hueco.
  - **Visión A→B:** entre el 1º y el último vértice de la línea seleccionada llama a
    `MapWidget::lineOfSight` con `Alt1`/`Alt2`, y dibuja el resultado como entidades
    en la capa «Análisis: visión»: la recta en verde (hay visión) o roja (bloqueada)
    y un punto en el obstáculo; la barra de estado da holgura/distancia.
  - **Viewshed:** desde el vértice de la entidad seleccionada (o el centro del mapa)
    llama a `MapWidget::viewshed` y dibuja el **polígono de la zona de visibilidad**
    (ZVD) a la altura `Alt2` en la capa «Análisis: viewshed», más el punto del
    observador; informa de alcance medio/máx y del tiempo de cálculo.
  - **Limpiar análisis:** quita esas dos capas.
- Las acciones avisan si no hay DEM activo o si falta selección/cobertura; los
  resultados van a capas propias que se recrean vacías en cada cálculo (así no se
  mezclan con las entidades del usuario).

Verificación: compila `demo` sin warnings; los **18 tests** siguen en verde (la
librería no cambió). La validación visual la hace el usuario en su PC (en el
contenedor no hay paquete de datos). Docs: README (fila del `demo` y sección
«Análisis de elevación»), `arquitectura.html` (8c) + PDF.

## 64. Viewshed: zona de visibilidad REAL (con huecos), consistente con la Visión

Al probar el `demo`, el usuario vio que la **línea verde «Visión directa»** (Visión
A→B) llegaba a un punto que quedaba **fuera** del polígono azul de la ZVD, y señaló
—con razón— que no pueden contradecirse en un azimut.

Diagnóstico: **no era física distinta**. El test de visibilidad del viewshed por
muestra es, desarrollado con `caida(d)=d²/(2kR)`, **exactamente** la condición de
`lineOfSight` (con `abombamiento(d1,d2)=d1·d2/(2kR)`); coinciden punto a punto. La
discrepancia venía de una decisión de representación: `visibilityReachM` era el
alcance **contiguo** (se cortaba en el PRIMER obstáculo), así que un punto que se ve
de verdad pero está tras una vaguada oculta quedaba fuera de la estrella, aunque
`lineOfSight` lo diera visible.

Corrección (el usuario eligió «zona real con huecos»):

- **`include/libmapa/Elevation.h`:** tipo nuevo `VisibleRange{startM,endM}` y campo
  `QVector<VisibleRange> visibleRanges` en `ViewshedRay`: TODOS los tramos visibles
  del rayo (la zona real; tras una loma, el terreno de más allá vuelve a verse como
  una bolsa). `visibilityReachM` se conserva como el primer tramo contiguo (útil
  para un polígono simple), documentado como tal.
- **`src/dem/ElevationAnalysis.cpp` (`rayoViewshed`):** además del alcance contiguo,
  abre/cierra tramos visibles según el MISMO test por muestra (los huecos y los
  NaN cierran el tramo). El primer tramo que arranca en la 1ª muestra incluye el
  origen; los demás empiezan en su muestra exacta.
- **`tests/tst_elevationanalysis.cpp` (test 19 en slots):** `viewshedMatchesLineOfSight`
  sobre un tile con loma + vaguada + pico: para CADA distancia del rayo comprueba
  que estar en un tramo visible del viewshed == `lineOfSight(origen, punto).clear`
  (saltando el roce, holgura ≈ 0, por el muestreo), y confirma que hay zona oculta
  y una **bolsa visible** detrás (≥ 2 tramos). Garantiza la consistencia y evita
  regresiones. Siguen 18 ejecutables de test (más slots), todos en verde.
- **`demo/main.cpp` (`analizarViewshed`):** la ZVD se dibuja ahora como la zona
  **real**: una cuña por cada tramo visible de cada rayo, todas como partes de UNA
  entidad (polígono multiparte), de modo que los huecos se ven como entrantes y la
  zona concuerda con la Visión A→B. La barra de estado informa de nº de tramos y
  distancia máxima visible.
- **Docs:** README y `arquitectura.html` (8c) + PDF: `visibleRanges` vs
  `visibilityReachM` y la nota de consistencia viewshed↔`lineOfSight`.

**Estado: la ZVD del viewshed es la zona de visibilidad real (con huecos) y
concuerda, azimut a azimut, con la línea de visión; 18 tests en verde, sin
warnings.**

## 65. Visión A→B: tramo visible/oculto en dos colores y distancia del obstáculo

Probando la Visión A→B, el usuario pidió más detalle (distancia del obstáculo) y
que quedara claro qué color es visible. Mejora SOLO del `demo` (`analizarVision`),
sin tocar la librería:

- Si hay visión directa, la línea va entera en **azul**.
- Si está bloqueada, se parte en el obstáculo (`blockPosition`): el tramo
  observador→obstáculo en **azul** (hasta aquí llega la vista) y el tramo
  obstáculo→B en **rojo discontinuo** (oculto por detrás). El punto del obstáculo
  lleva en su etiqueta la **distancia** y cuántos metros falta de altura
  (`blockDistanceM`, `−clearanceM`).
- La barra de estado lo resume: en directa, distancia, holgura mínima y dónde está
  el paso más justo; en bloqueada, obstáculo a X km de Y km, metros que faltan y la
  leyenda «azul = visible, rojo = oculto».

Convención de color coherente con el viewshed (azul = visible). Compila sin
warnings; la librería no cambia, así que **siguen 18 tests** en verde. README al
día; sin cambios de API, no se regenera el PDF.

## 66. Visión A→B: el obstáculo y la línea de visión SOBRE el perfil

El usuario pidió ver el obstáculo en el perfil como referencia, y planteó —con
razón— que si Alt1 y Alt2 son iguales en la Visión A→B y en el viewshed, los datos
deben coincidir. Lo son: el viewshed es la Visión A→B hecha a 360° para un objetivo
a Alt2 (misma condición por muestra, ya con test). La coincidencia exige el **mismo
observador**: el viewshed pone Alt1 en el origen y Alt2 en el objetivo, así que hay
que lanzarlo desde el mismo punto A (en el demo, el 1er vértice de la entidad
seleccionada). Con Alt1=Alt2 `lineOfSight` es simétrica (da igual la dirección);
quedan como únicas fuentes de diferencia el muestreo de azimut a 1° justo en el roce
y, antes del arreglo de `visibleRanges`, la ZVD contigua.

Para verlo, mejora SOLO del `demo` (`PerfilWidget` y `analizarVision`):

- **`PerfilWidget`** admite `setVision(vista, critD, bloqueado)`: superpone la
  **línea de visión** (recta entre antenas bajada por la curvatura, `recta−bulge`
  con k=4/3, en naranja) y marca el **obstáculo / paso más justo** (línea vertical
  + punto sobre el terreno + distancia; rojo si bloqueado, azul si no). El rango
  vertical pasa a abarcar terreno y línea de visión.
- **`analizarVision`** calcula el perfil de la línea A→B, monta esa línea de visión
  muestra a muestra y abre el perfil con todo marcado, además de dibujar en el mapa.
  Así se ve dónde el terreno corta la recta y a qué distancia.

Compila sin warnings; la librería no cambia, **siguen 18 tests** en verde. Sin
cambios de API; no se regenera el PDF.

## 67. Pestaña de elevación, perfil interactivo y viewshed azul/amarillo (mar=0)

El usuario pidió: mover el análisis de elevación a una **pestaña lateral**, un
**perfil interactivo**, y que el viewshed **sombree de amarillo lo no visible**,
tratando el **mar/sin dato como 0 m** (objetivos en el mar). Casi todo es del
`demo`; dos añadidos a la librería (API aditiva, compatible).

Librería (`include/libmapa/Elevation.h`, `src/dem/ElevationAnalysis.cpp`):
- **`voidElevation`** en `ElevationProfileParams`, `LineOfSightParams` y
  `ViewshedParams`: cota con la que sustituir los huecos del DEM (NaN = saltar,
  por defecto; 0 = mar). El helper interno `cota()` la aplica en el perfil, la
  línea de visión y el viewshed, así un objetivo sobre el mar se analiza hasta el
  alcance en vez de cortarse en la costa.
- **`hiddenRanges`** en `ViewshedRay`: los tramos NO visibles (con dato); junto a
  `visibleRanges` parten el rayo cubierto, sin solape. Para sombrear lo oculto.
- Tests nuevos: `viewshedSeaAsVoidElevation` (un tile todo hueco, inválido sin la
  opción, válido como mar con `voidElevation=0`) y `viewshedVisibleHiddenPartition`
  (en cada muestra con dato el objetivo está en EXACTAMENTE uno de los dos). **18
  tests** en verde, sin warnings.

Demo (`demo/main.cpp`):
- **Pestaña «Elevación»** (`construirTabElevacion`): `QTabWidget` en el dock con
  «Capas» y «Elevación»; esta última lleva Alt1/Alt2/Alcance, los botones
  (Perfil/Visión A→B/Viewshed/Limpiar), el check «Mar / sin dato = 0 m», una
  lectura (`m_resultado`) y una leyenda. Se quitó la barra superior de elevación.
- **`PerfilWidget` interactivo:** ejes y rejilla con ticks «bonitos», **zoom** con
  la rueda sobre la distancia, **arrastre** para desplazar, **doble clic** para
  restablecer y un **cursor** con la lectura (distancia, cota, altura de la visión,
  holgura), leyenda, y `setProfile`/`setVision`. Se abre en una **ventana flotante
  reutilizable** (`mostrarPerfil`), no modal, que Perfil y Visión A→B actualizan.
- **Viewshed azul/amarillo:** cuñas **azules** de `visibleRanges` y **amarillas**
  de `hiddenRanges` (dos entidades multiparte); con `voidElevation=0` el mar entra
  como objetivo. La Visión A→B usa el mismo `voidElevation` para seguir coherente.

Docs: `Elevation.h` (comentarios), README, `arquitectura.html` (8c) + **PDF**.

## 68. Perfil: mástiles, +5 km tras B, línea de sombra y check de curvatura

Afinados del `demo` tras probar (solo `demo/main.cpp`, sin tocar la librería):

- **Perfil +5 km tras B:** la Visión A→B calcula el perfil de `{A, B+5 km}` (mismo
  rumbo) para ver el terreno detrás del objetivo; la recta de visión sigue yendo
  solo de A a B (no se extrapola).
- **Mástiles de antena:** en el perfil se dibuja, en A y B, la línea vertical del
  terreno a la cima de la antena (Alt1/Alt2) con su punto y etiqueta.
- **Línea de sombra (dead ground):** si bloquea, un rayo rojo desde la antena de A
  que roza el obstáculo y sigue hasta el final del perfil; por debajo, tras el
  obstáculo, el terreno queda oculto.
- **Check «Curvatura 4/3»:** con curvatura (por defecto) el mar llano se oculta tras
  el horizonte geométrico; al desactivarla, el análisis es solo de enmascaramiento
  por terreno y sobre mar sin obstáculos se ve hasta el alcance máximo. Se aplica a
  la línea de visión, al viewshed y a la recta dibujada en el perfil.

Compila sin warnings; la librería no cambia, **18 tests** en verde. README al día;
sin cambios de API, no se regenera el PDF.

## 69. Perfil con curvatura de la Tierra y checks que re-aplican

Más afinados del `demo` tras probar (solo `demo/main.cpp`; la librería no cambia):

- **Checks que re-aplican al instante:** al marcar/desmarcar «Mar = 0» o
  «Curvatura 4/3» se vuelve a ejecutar el último análisis (perfil/visión/viewshed)
  sin pulsar otra vez el botón (`enum Analisis m_ultimo` + `reejecutar()`).
- **Perfil que refleja la curvatura:** `PerfilWidget` reescrito a coordenadas
  proyectadas `cota − caida(d)` (con `caida=d²/(2kR)` si hay curvatura): el terreno
  se **hunde con la distancia**, se dibuja una **curva azul fina** del nivel del mar
  y la **línea de visión queda recta** entre las cimas de antena (equivalente, punto
  a punto, a la representación anterior). Nueva API del widget:
  `setVision(zA, zB, D, critD, bloqueado)` (cimas absolutas), `setCurvatura(on,k,R)`,
  `setTechoSobreObjetivo(m)` y `setProfile(p, maxInicial)`.
- **Mayor área:** el perfil de la Visión A→B llega hasta **alcance + 5 km** (datos),
  con vista inicial `[0, alcance]` para **arrastrar desde el inicio**; en vertical
  muestra hasta **5000 m por encima del objetivo**. Mástiles de antena, línea de
  sombra y cursor (lectura en cotas absolutas) se mantienen.

Compila sin warnings; **18 tests** en verde. README y BITÁCORA al día; sin cambios de
API, no se regenera el PDF.

## 70. Perfil del `demo` con QCustomPlot, al estilo de DVD_potencial

El perfil hecho a mano «seguía sin funcionar» (arrastre/zoom/curvatura). Revisado
`DVD_potencial/graficaperfil.cpp`: usa **QCustomPlot** (arrastre y zoom reales con
`setInteractions(iRangeDrag|iRangeZoom)`, leyenda) y dibuja el **terreno crudo**
(verde, relleno por canal hasta) una **curva de curvatura** aparte `−d²/17e6`, más la
recta de visibilidad y curvas de altura. LibMapaStatic **ya trae QCustomPlot** (se
compila dentro de `libmapa_widget`), así que se sustituye el widget a mano del `demo`
por un QCustomPlot con esa misma representación.

- **CMake (target `demo`):** se enlaza `Qt::PrintSupport` y se añade el include de
  QCustomPlot como `SYSTEM` (los símbolos ya están en `libmapa_widget`). El `demo`
  sigue construyéndose solo cuando hay QCustomPlot, como antes.
- **`demo/main.cpp`:** se elimina la clase `PerfilWidget` (QWidget a mano) y la ventana
  flotante embebe un `QCustomPlot`. `pintarPerfil(...)` dibuja: curva de **curvatura**
  (`curvOn ? −d²/2kR : 0`), **terreno** crudo verde con `setChannelFillGraph` hasta la
  curvatura, y en la Visión A→B la **recta de visibilidad** A→B, la **curva del objetivo
  a Alt2**, los **mástiles** (`QCPItemLine`), el **obstáculo** (`QCPItemTracer`) y la
  **línea de sombra** hasta el final. Arrastre y zoom (rueda) en ambos ejes; rango X
  inicial `[0, alcance]` con datos hasta `alcance+5 km`; Y auto-ajustado al dato con
  margen (se quita el techo fijo de 5000 m que aplastaba el relieve). Se mantienen los
  checks «Mar=0»/«Curvatura 4/3» que re-aplican al instante.

Reconfigurar CMake (`cmake -S . -B build`). Compila sin warnings; la librería no
cambia, **18 tests** en verde. README y BITÁCORA al día; sin cambios de API, no se
regenera el PDF.

## 71. Perfil: agua azul bajo el nivel del mar y recta de visibilidad sobre el mar

Probando sobre el dataset «Cuba 2026.10», el usuario vio que la barra de estado marcaba
**−1891 m** con el cursor en mar abierto al norte de Mariel. Esa lectura es la cota cruda
bajo el cursor (`m_mapa->elevationAt`, sin aplicar el check «Mar=0»): que salga negativa
sobre el mar confirma que **este dataset trae batimetría** (profundidad del fondo; el
talud al norte de Cuba cae a ~−1800 m hacia el Estrecho de Florida). El lector del DEM ya
dejaba pasar los negativos (solo `−32768` es hueco), así que no hubo que tocar la
librería; basta aprovecharlo en el `demo`. Dos cambios, **solo en `demo/main.cpp`**:

- **Agua azul en el perfil (`pintarPerfil`).** Se añade una línea de **nivel del mar** en
  `y=0` (azul discontinua) y una **columna de agua** azul translúcida: un graph con
  `min(terreno, 0)` relleno por canal hasta una línea constante 0. Donde la tierra está
  sobre el mar ambos coinciden (sin relleno); donde la cota es < 0 (mar con batimetría o
  depresión) se rellena entre el terreno y 0 → se ve la diferencia tierra/agua. El cuerpo
  verde del terreno sigue yendo hasta la curva de curvatura (tierra bajo el agua).
- **Recta de visibilidad sobre el mar (`analizarVision` + mástiles).** Los objetivos son
  por ahora **sobre el nivel del mar** (buques): si un extremo no tiene dato (NaN) o cae
  bajo 0 (mar/batimetría), la base de la antena se asienta en la **superficie (0)**, no en
  el fondo marino (`base = isnan ? 0 : max(cota,0)`). Así la recta «busca» el objetivo en
  la superficie y el mástil arranca en 0, en vez de a −profundidad. (Una depresión de
  tierra firme bajo 0 se trata igual en el `demo` por simplicidad; distinguir mar de
  tierra firme negativa necesitaría una máscara de costa, fuera de alcance.)

Nota: sin batimetría (SRTM puro) el mar abierto está en 0 o es hueco, así que el azul solo
aparece donde hay cota real < 0; con un DEM batimétrico (GEBCO/ETOPO) el mismo lector
mostraría la profundidad. Compila sin warnings; la librería no cambia, **18 tests** en
verde. README y BITÁCORA al día; sin cambios de API, no se regenera el PDF.

## 72. «Mar = 0» como suelo: el obstáculo no puede estar en el fondo del mar

Al probar la Visión A→B sobre batimetría (objetivo a 500 m a 34 km sobre mar hondo), la
recta naranja llegaba bien a la superficie, pero el **obstáculo rojo aparecía a 5 km bajo
el agua (~−50 m)** y la línea de sombra se hundía al fondo marino (−850 m). Imposible: el
lecho marino no puede tapar una visual entre dos objetivos sobre la superficie.

**Causa.** El `demo` ya **dibujaba** la recta clampeada a la superficie (§71), pero
`lineOfSight` de la **librería** calculaba el bloqueo contra el terreno **crudo**: con
batimetría, el extremo B se anclaba en el fondo (−800 m + Alt2) y la recta «caía» al
agua, de modo que la costa la tapaba falsamente. El check «Mar = 0» solo sustituía los
huecos (NaN), no la batimetría negativa **con dato**.

**Arreglo (semántica del API).** `voidElevation` finito pasa a ser el **suelo** del
análisis en el helper `cota()` de `ElevationAnalysis.cpp`: sustituye los huecos por él y
**sube a él cualquier cota por debajo** (la batimetría). Con `voidElevation = 0`, sobre
el mar la superficie que cuenta es el nivel del agua (0), no el fondo — ni los objetivos
flotan en el lecho ni el lecho tapa una visual. Afecta a los tres cálculos (perfil, línea
de visión y viewshed) por igual; con `voidElevation = NaN` (crudo) nada cambia (negativos
de tierra firme incluidos).

- **`src/dem/ElevationAnalysis.cpp`:** `cota()` → `isnan(voidElev) ? t : (isnan(t) ?
  voidElev : max(t, voidElev))`.
- **`include/libmapa/Elevation.h`:** comentarios de `voidElevation` en los tres structs
  de parámetros (ahora «suelo»).
- **`demo/main.cpp`:** el **perfil se dibuja siempre crudo** (`ElevationProfileParams{}`)
  para ver la batimetría en azul; el **análisis** (`lineOfSight`/viewshed) usa `voidElev()`
  (0 con el check). El obstáculo sobre el perfil se sitúa en la superficie (`max(ct,0)` con
  el check) para no pintarse bajo el agua, coherente con el análisis.
- **Test** `lineOfSightSeaFloorClampedToSurface`: tile con costa (+100) al oeste y mar
  profundo (−1000) al este. En crudo, la recta a un objetivo a 500 m se hunde y la costa la
  bloquea (`!clear`); con `voidElevation = 0` el objetivo está en superficie y **se ve**
  (`clear`). Comprueba además que el perfil crudo muestra la batimetría (mín < −500) y que
  con mar=0 el perfil se clampea (mín = 0, máx = 100).

Compila sin warnings; **18 tests** en verde (un slot nuevo del mismo ejecutable). README,
BITÁCORA y `arquitectura.html` al día; **cambia la semántica del API → se regenera el PDF**.

## 73. Perfil al estilo DVD: curvatura en el eje Y, el mar es solo una línea

Con el relleno azul (§71) el perfil quedaba feo: la batimetría (−1891 m) dominaba la
gráfica con una gran mancha azul, y el objetivo sobre el mar no reflejaba la curvatura.
Revisando a fondo `DVD_potencial/graficaperfil.cpp` (`graph(0)` terreno relleno por canal
hasta `graph(1)` curvatura; `curva = −d²/17e6`; y donde no hay terreno, **`altu = curva`**
→ el terreno SIGUE la línea del mar), se reescribe `pintarPerfil` con ese modelo. Cambio
**solo del `demo`**.

- **El eje Y refleja la curvatura de la Tierra:** todo se hunde con la distancia restando
  `caída(d) = d²/2kR` (k=4/3). El **nivel del mar es una sola línea** `y = −caída(d)` que
  baja con la distancia; **el mar no se rellena**, es esa línea (antes se rellenaba de
  azul: eliminado).
- **Terreno:** `max(cota, 0) − caída(d)`. La tierra firme (cota > 0) va a su altura,
  hundida; el mar y la batimetría (cota ≤ 0) se tratan como superficie 0 y **rielan sobre
  la línea del mar** (relleno verde por canal hasta ella → nulo sobre el mar). Ya no se
  dibuja la batimetría: el usuario pidió el mar como una línea, no como relleno.
- **Objetivo sobre el mar:** a `Alt2` por encima de la línea del mar (hundida); «el punto
  va sobre la línea del mar con curvatura».
- **Recta de visibilidad:** recta en el plano hundido, de la cima de antena en A
  `(0, zA)` a la del objetivo en B `(D, zB − caída(D))`, que es justo el punto del objetivo
  rielando sobre el mar. Mástiles y obstáculo se sitúan sobre la superficie hundida.

Compila sin warnings; la librería no cambia, **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API, no se regenera el PDF.

## 74. Perfil: ángulo de cierre, picos de la silueta y recta al pico dominante

El usuario pidió reflejar en el perfil el **ángulo de cierre** al estilo DVD: una recta
directa entre los puntos, tracers en los **picos máximos** y una recta hasta el **pico
dominante**, el que de verdad provoca la no visibilidad. En DVD, los tracers se ponen en
cada punto de ángulo de cierre sobre el terreno y la recta de visibilidad se dibuja como
`tangente·d + posición` (recta desde el observador con la pendiente del ángulo de cierre
dominante). Se replica en `pintarPerfil` (solo `demo`):

- **Recta directa A→B** (naranja): se mantiene, es la línea de visión entre A y B.
- **Ángulo de cierre:** desde el observador en `(0, zA)`, la tangente a cada muestra es
  `(y − zA)/d` (en el plano ya hundido por la curvatura). Cada muestra que supera el
  **máximo acumulado** es un **pico** que eleva el horizonte.
- **Tracers (círculos) en los picos** de la silueta (`gPicos`, scatter sin línea).
- **Recta al pico dominante** (`gCierre`): recta tangente del observador `(0, zA)` por el
  pico de mayor ángulo hasta B `(domD, domY)`, **extendida con esa pendiente hasta la
  distancia máxima** `dFin` (lo que queda por debajo detrás del pico está oculto). Es la
  línea de cierre que limita la visibilidad. **Roja si bloquea, morada si no**; el pico se
  marca con un tracer del mismo color. Si no hay pico hasta B, cae al corte más justo
  (`critD`) de `lineOfSight`.

Compila sin warnings; la librería no cambia, **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API, no se regenera el PDF.

## 75. Entrada de puntos en la pestaña Elevación (pick en el mapa / lat-lon)

Antes el análisis tomaba la geometría de la entidad SELECCIONADA en el mapa (una línea
para perfil/visión, un vértice para el viewshed). Ahora los puntos se introducen en la
propia pestaña, por análisis. Cambio **solo del `demo`**.

- **Punto A** y **Punto B**: cada uno con campos **lat/lon** editables y un botón
  **«📍 Mapa»** que captura el siguiente clic. Se aprovecha el API público del widget:
  `setActiveTool(MapTool::PickPoint)` + señal `pointPicked(coord)`. Como `PickPoint` no se
  auto-resetea, tras capturar se vuelve a `MapTool::None` y se re-ejecuta el último
  análisis para verlo al vuelo. A y B arrancan en el centro del mapa y 20 km al este.
- **Perfil** (radial): A + **rumbo** (azimut, 0=N) + **alcance**; perfil del terreno desde
  A por ese rumbo hasta el alcance (`a.atDistanceAndAzimuth`).
- **Visión A→B**: A (Alt1) y B (Alt2) de los campos; se exige A≠B.
- **Viewshed**: origen = A; observador Alt1, objetivo Alt2, alcance.
- Nuevos campos: `m_latA/m_lonA`, `m_latB/m_lonB`, `m_rumbo`; enum `Pick{Ninguno,A,B}` y
  `m_picking`. Se elimina `entidadSeleccionada()` (ya no se usa). Los botones y la ayuda
  del tab describen el nuevo flujo.

Compila sin warnings; la librería no cambia, **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API, no se regenera el PDF.

## 76. Alcances de 400 km, progreso cancelable del viewshed y sobre-zoom en fill_map

Tres mejoras para ir preparando escenarios grandes.

**1. Rangos largos (demo).** El alcance (viewshed/perfil) sube a **400 km**; las alturas
de antena/objetivo ya llegaban a **20 km** (0–20000 m). El viewshed, el perfil radial y la
visión A→B operan sin cambios a esos alcances (caminan varias teselas del DEM).

**2. Progreso cancelable del viewshed (librería + demo).** Un viewshed de 360° a 400 km
son millones de muestras y puede tardar. Se añade un **callback de progreso** opcional al
API:
- `include/libmapa/Elevation.h`: `using ViewshedProgress = std::function<bool(int done,
  int total)>;`.
- `computeViewshed(src, origin, params, progress = {})`: invoca `progress(hecho, total)`
  tras cada azimut; si devuelve `false`, **cancela** y devuelve un viewshed vacío.
- `MapWidget::viewshed(origin, params, progress = {})`: reenvío.
- `demo`: `analizarViewshed` muestra un `QProgressDialog` **cancelable** (modal, aparece
  solo si pasa de ~0,4 s); el callback procesa eventos y propaga «Cancelar».
- Test `viewshedProgressAndCancel`: el callback se llama una vez por rayo hasta
  `(total,total)`; devolver `false` al 5º rayo deja el resultado inválido.

**3. Sobre-zoom en fill_map (librería + fill_map).** El mapa recortaba el zoom a
`recommendedMaxZoom` del dataset (p. ej. 14), así que no se podían **navegar ni enmarcar**
niveles altos (z15/16) para descargarlos. Se añade:
- `MapView::setMaxZoomOverride(int)` / `MapWidget::setMaxZoomOverride(int)`: tope de
  sobre-zoom por encima del recomendado (−1 lo restaura). Se aplica en `setZoom` y
  `fitBounds`.
- `fill_map`: `m_mapa->setMaxZoomOverride(19)` al arrancar; así la vista previa llega a
  z19 para enmarcar. Los spinboxes de descarga ya llegaban a z22 (sin recorte), de modo
  que **z15/z16 se descargan cuando se desee**.

Compila sin warnings; **18 tests** en verde (slot nuevo). README, BITÁCORA y
`arquitectura.html` al día; **cambia el API público (callback de progreso, sobre-zoom) →
se regenera el PDF**.

## 77. Todas las herramientas de teselas leen el paquete mapa.json

El usuario tenía su configuración en `D:\QtPro\recursos\mapa.json` (paquete v2, con
`clarity` a maxZoom 16), pero `fill_map` mostraba `clarity z[3..14]`: estaba leyendo un
`datasets.json` suelto de su directorio de trabajo, no el paquete. `fill_map`,
`fill_tiles` y `bench_tiles` solo entendían `datasets.json`; el `demo`, `render_map` y
`check_data` ya aceptaban el paquete. Se unifica: **todas usan `mapa.json`**.

- **`fill_map`** (GUI): el constructor pasa a `(origen, esPaquete, demDir, demDb)`. Con un
  paquete, `cfg.dataDir = origen` (la vista previa lee capas, elevación y overlays del
  paquete); los datasets para la descarga se leen con `DataPackage::load(origen)`, que
  acepta tanto un `mapa.json` (rutas resueltas a absolutas) como un `datasets.json` plano.
  `main` detecta paquete (carpeta o fichero `mapa.json`) y, sin argumento, prefiere
  `mapa.json` del directorio actual y si no `datasets.json`.
- **`fill_tiles`** / **`bench_tiles`** (consola): su lector ya tomaba el array `datasets`
  (común a `mapa.json` y `datasets.json`) resolviendo rutas junto al JSON; se añade que, sin
  `--datasets`, usen `mapa.json` del directorio actual (si no, `datasets.json`).

Así, ejecutando las herramientas desde la carpeta de recursos, todas ven el mismo
`mapa.json` y los mismos zooms. (El «z14» era la config vieja; con el paquete, `clarity`
llega a z16 en vista y cobertura, y la descarga a z15/16 funciona como ya hacía.)

Compila sin warnings; la librería no cambia, **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API, no se regenera el PDF.

## 78. Pestaña Elevación: cota de A/B, rumbo/distancia, pines de color y «10 picos»

Mejoras pedidas para el panel lateral de elevación del `demo` (solo `demo/main.cpp`).

- **Cota de los puntos.** Debajo de los campos, una lectura muestra la **cota del terreno
  en A y en B** (`elevationAt` bajo cada punto) y el **rumbo y la distancia A→B**
  (`QGeoCoordinate::azimuthTo`/`distanceTo`). Se recalcula al cambiar cualquier lat/lon.
- **Cursor y pines de color.** Un pin se dibuja con `QPainter` (`pinPixmap(color)`) y se usa
  como **icono del botón «Mapa»**, como **cursor** al capturar (punta = hotspot) y como
  **marca fija** del punto. Azul = A, rojo = B. Al pulsar «Mapa» el cursor del mapa toma el
  pin del color del punto; al fijarlo se restaura. Durante cada análisis, A (y B en la
  Visión) quedan marcados con su pin sobre el mapa (capa `elev_puntos`).
- **Herramienta «10 picos (10 km)».** Nuevo botón: muestrea una rejilla de 100 m dentro de
  un radio de **10 km** desde A, descarta el mar/sin dato (NaN), ordena por cota y toma los
  **10 puntos más altos** exigiendo una separación mínima de 800 m (cumbres distintas, no la
  misma loma). Los pinta **numerados** (naranja, capa `elev_picos`) y los lista en el panel
  con **cota · distancia · rumbo**. Reutiliza `MapWidget::elevationAt`; no toca la librería.
- `FeatureStyle::icon` (QPixmap) permite el pin como icono de punto; `Limpiar` borra también
  `elev_puntos` y `elev_picos`; el enum de análisis gana `Picos` para re-aplicar al vuelo.

Compila sin warnings; la librería no cambia, **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API, no se regenera el PDF.

## 79. «10 picos»: exactitud (refinamiento), radio/separación configurables, color y hover

Probando sobre el Pico Turquino (1974 m), la herramienta daba 1972 m: la rejilla de 100 m
se saltaba la cima exacta. Se mejora en varios frentes (solo `demo`).

- **Exactitud.** La rejilla ahora **localiza** cumbres y su paso se **escala con el radio**
  `paso = max(30, 2R/500)` (~500×500 muestras, coste acotado a cualquier radio). Cada
  cumbre del top-10 se **refina**: ventana `±max(150, paso)` a paso **10 m**, quedándose
  con la cota máxima real (clava el nodo del pico que la rejilla gruesa se saltaba). Tras
  refinar se reordena por cota.
- **Radio y separación configurables.** Dos controles en la pestaña: **radio** (1–50 km,
  por defecto 10) y **separación mínima** entre cumbres (50–5000 m, por defecto 800).
- **Progreso.** Barra cancelable (`QProgressDialog`) durante el barrido de la rejilla
  (radios grandes tardan); aparece solo si pasa de ~0,4 s.
- **Color por pico + punto central.** Cada pico se dibuja con un **color distinto** (paleta
  de 10) como disco con borde blanco y **punto central**, con el número en su color. Icono
  dibujado con `QPainter` (`picoIcon`).
- **Resaltado por hover.** Un `eventFilter` sobre el mapa usa `MapWidget::featureAt(píxel)`;
  al pasar el cursor por un pico, se agranda con un halo amarillo (`updateFeature`) y se
  restaura al salir. Se guardan los ids y el estilo base de cada pico.

Compila sin warnings; la librería no cambia, **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API, no se regenera el PDF.

## 80. Relieve sombreado (hillshade) en vivo, calculado del DEM local (offline)

Nueva capa de **relieve sombreado** sobre el mapa, calculada EN VIVO a partir del DEM
local del paquete (100% sin conexión, como pidió el usuario). Toca la librería (capa + API)
y el demo (controles).

- **`src/widget/HillshadeLayer.{h,cpp}`** (nuevo `QCPLayerable`, con `Q_OBJECT`): capa
  «tonta» que guarda una imagen ya calculada + las esquinas geográficas y la pinta estirada
  entre sus píxeles (eje X = longitud, Y = grados de Mercator → alineada con la base). Va en
  una capa propia **encima de las teselas y debajo de las entidades**. Dibuja en *Multiply*
  (gris sobre la base) o *SourceOver* (tintado por altura), con opacidad.
- **`MapWidget`**: calcula la imagen en `refreshHillshade()` muestreando el DEM
  (`elevationAt`) en una rejilla submuestreada del viewport (lado máx 420 px); filas
  lineales en grados de Mercator (inverso de `WebMercator`) para alinear. Por píxel:
  pendiente/orientación por diferencias centrales, sombreado Lambert
  `cos(zenit)cos(pend)+sin(zenit)sin(pend)cos(azSol−orient)` con **exageración** `zFactor`;
  sin dato → transparente. Gris (fuerza mezclada hacia blanco + *Multiply*) o color
  **hipsométrico** (rampa por altura × sombreado). Se **recalcula sola** al mover la vista
  (antirebote 180 ms). API: `setHillshadeVisible/Sun/Opacity/Exaggeration/Colored`,
  `isHillshadeVisible`, `refreshHillshade`.
- **`MapView`**: crea la capa `hillshade` entre `tiles` y `features`; `hillshadeLayer()`.
- **`demo`**: grupo «Relieve sombreado» en la pestaña Elevación (checkable) con sol
  (azimut/altura), intensidad, exageración y «tintar por altura».
- **CMake + qmake**: añadido `HillshadeLayer.{cpp,h}` a `libmapa_widget` (mirror).
- **Test** `hillshadeApiTogglesSafely` (tst_mapwidget): la API conmuta y acepta parámetros
  sin colgar (sin DEM, la capa queda vacía).

Compila sin warnings; **18 tests** en verde. README, BITÁCORA y `arquitectura.html` al día;
**API pública nueva → se regenera el PDF**.

## 81. Pines automáticos al seleccionar punto; freno del hillshade por área

Dos ajustes tras probar (solo `demo` salvo el freno, que va en `MapWidget`).

- **El pin aparece al seleccionar/escribir el punto.** Antes los pines A/B solo se
  dibujaban al lanzar un análisis. Ahora `marcarPuntosAB()` dibuja SIEMPRE A (azul) y B
  (rojo) y se llama desde `actualizarInfoPuntos()` (que ya se dispara al pinchar «Mapa» o
  editar lat/lon), así el icono sigue a los puntos sin necesidad de analizar.
- **Hillshade: freno por área (no congelar a vista general).** El coste del hillshade lo
  domina cargar teselas del DEM, que crece con el área de la vista; con Cuba entera (z9) la
  GUI se quedaba «sin responder». `refreshHillshade` ahora **no calcula** si la vista abarca
  más de ~2° de ancho o alto (deja la capa en blanco): es una capa de detalle, se ve al
  acercar. Se baja también el lado máx. de la imagen a 360 px. (Para relieve a escala de
  país, el camino serían tiles de hillshade horneados, offline, en el paquete.)

Compila sin warnings; **18 tests** en verde. README y BITÁCORA al día; sin cambios de firma
de API, no se regenera el PDF.

## 82. dem_to_hillshade: hornear el relieve sombreado a teselas (offline)

Complemento de la capa de hillshade en vivo (§80), que a vista general se desactiva por
coste (§81). Nueva herramienta de consola **`dem_to_hillshade`** (gemela de `geo_to_tiles`):
hornea el relieve a una **pirámide de teselas PNG** en un `.sqlitedb` XYZ que viaja en el
`mapa.json` como una capa base más.

- **Beneficios:** fluido a CUALQUIER zoom (incluida Cuba entera), no necesita el DEM ni CPU
  en runtime, calidad consistente, y reutiliza el motor de teselas (cero código nuevo de
  runtime). Coste: espacio en disco, pregeneración una vez, y el **sol/exageración quedan
  fijos** (para cambiarlos al vuelo está la capa en vivo).
- **Cálculo:** abre el DEM (carpeta `.hgt` → `HgtElevation`, o `.sqlitedb` →
  `SqliteElevation`); por cada tesela z/x/y de la bbox calcula una rejilla de cotas 258×258
  (con halo de 1 px para que no haya costuras), y por píxel el sombreado Lambert con
  diferencias centrales y exageración. Metros/píxel por Web Mercator (conforme: igual en x e
  y). Gris opaco en tierra; **sin dato → transparente** (el mar deja ver el fondo). PNG en
  `tiles(x,y,z,image)`. Imprime el bloque de dataset listo para pegar.
- **Uso:** `dem_to_hillshade --in <hgt|dem.sqlitedb> --out relieve.sqlitedb --cuba
  [--minzoom 6 --maxzoom 13] [--sun-az 315 --sun-alt 45 --exag 2] [--overwrite]`.
- **Build:** `add_executable(dem_to_hillshade …)` con `libmapa_core` + Qt Gui (QImage), y en
  la lista de `install`. No añade `.cpp` de librería → sin cambios en qmake de core/widget
  (como `geo_to_tiles`, que tampoco tiene `.pro` propio).

Probado con un `.hgt` sintético (una colina): genera teselas PNG válidas con sombreado real.
Compila sin warnings; **18 tests** en verde. README, BITÁCORA y `arquitectura.html` al día;
**herramienta nueva → se regenera el PDF**.

## 83. Entrada por análisis (selector) y pines solo del análisis activo

El usuario pidió que los puntos A/B NO estén siempre visibles y que la entrada de cada
análisis pida SOLO sus datos (no un panel de campos fijos). Rediseño de la pestaña
Elevación (solo `demo`).

- **Selector + campos contextuales.** Un `QComboBox` «Análisis» (Perfil / Visión A→B /
  Viewshed / 10 picos) y un único botón **«Calcular»** sustituyen a los cuatro botones.
  Cada campo vive en una fila que se **muestra/oculta** según el análisis (sin
  `QFormLayout::setRowVisible`, que es Qt 6.4+: cada fila es un `QWidget` propio). Se
  relabelan los comunes: la posición A es «Posición A» / «Posición 1» / «Observador» /
  «Posición»; la distancia es «Distancia» o «Alcance»; las antenas «Antena 1/2» o «Antena
  obs. / Altura objetivo». `actualizarEntradaAnalisis(idx)` hace el show/hide + relabel.
  - Perfil: posición + azimut + distancia. Visión: posiciones 1 y 2 + antenas 1/2.
    Viewshed: observador + antena + alcance + altura objetivo. Picos: posición + radio/sep.
- **Pines solo del análisis activo.** `marcarPuntosActivos()` dibuja el pin de la posición
  A siempre y el de B **solo en Visión**; se actualiza al cambiar de análisis, al pinchar o
  al escribir. Así el icono aparece al seleccionar el punto (sin analizar) pero no sobran
  pines (B no aparece en perfil/viewshed/picos). «Limpiar» borra los overlays de análisis
  pero mantiene los pines de entrada.
- **Lectura contextual.** La cota se muestra de la posición activa; en Visión, además la de
  B y el rumbo/distancia A→B.

Los slots de cálculo (`analizarPerfil/Vision/Viewshed/PicosAltos`) no cambian: ya leían de
los mismos spinboxes; «Calcular» despacha según el selector. Compila sin warnings, **18
tests** en verde. README y BITÁCORA al día; sin cambios de API, no se regenera el PDF.

## 84. dem_to_hillshade: realce de contraste (el relieve salía lavado)

Al hornear el relieve de Cuba (§82) y cargarlo como capa base, el usuario vio que «no se ve
bien»: salía un gris plano, de bajo contraste. La causa: se escribía el sombreado Lambert
crudo (`v = hs·255`). El terreno **llano** vale `cz = sin(sunAlt)` (a 45°, ~0.707 → gris
claro) y casi todas las laderas caen cerca de ese valor, así que el rango útil se agolpaba en
una franja estrecha → imagen lavada.

- **Estirado tonal (solo `tools/dem_to_hillshade`).** Tras calcular `hs ∈ [0,1]`, se aplica
  un realce lineal que **ancla el llano a un gris claro fijo** (`grisLlano = 0.72`) y abre el
  rango `contrast` veces a su alrededor: `t = clamp(grisLlano + (hs − sin(sunAlt))·contrast,
  0, 1)`, y `v = 255·t`. Así las laderas en sombra se oscurecen de verdad y las soleadas
  aclaran, **sin salirse del gris**. El llano queda claro (destaca el relieve bajo las capas
  de encima); `contrast = 1` deja el sombreado casi tal cual, `> 1` realza (defecto **2.2**).
- **Nuevo flag `--contrast <k>`** (defecto 2.2) para graduarlo al hornear.
- **Nuevo flag `--sea-level <m>`** (por defecto desactivado): toda cota `≤ m` se deja
  **transparente** en vez de sombrearla. Imprescindible con DEM que traen **batimetría** (el
  «Cuba 2026.10» tiene fondo marino con cotas negativas): sin esto, `dem_to_hillshade`
  sombreaba el fondo del mar y la **costa se perdía** en el gris. Con `--sea-level 0` el mar
  queda transparente (se ve la capa base de debajo) y la línea de costa sale limpia. El resto
  del pipeline (rejilla 258×258 con halo, PNG XYZ; teselas 100 % mar se omiten) no cambia.
- **Costa sin ribete.** Al calcular la pendiente de un píxel de tierra, los vecinos **sin dato
  o de mar** (`≤ seaLevel`) se sustituyen por la cota del centro. Así la costa no computa un
  acantilado artificial tierra→fondo-marino (que dejaba un ribete claro/oscuro de 1 px) y
  sombrea por su propia pendiente suave de tierra.

Validado horneando crops reales de SRTM 30 m (bajados con `fill_hgt`) de La Habana (llano →
gris claro) y la Sierra Maestra (relieve fuerte): con `--contrast 2.2 --sea-level 0` el relieve
sale nítido y el mar transparente. Sin cambios en la librería ni en el API; compila sin
warnings y **18 tests** en verde. README y BITÁCORA al día; la herramienta ya existía (solo
flags opcionales nuevos) → no se regenera el PDF.

## 85. dem_to_hillshade: `--colored` (tinte hipsométrico + batimetría)

El usuario preguntó por qué gris y no color por altura. El gris se mantiene por defecto (neutro
bajo las capas temáticas de color), pero se añade un modo **«mapa físico»** opcional.

- **Nuevo flag `--colored`.** En vez de gris, cada píxel toma un **color hipsométrico** según
  su cota y se **modula por el sombreado** para conservar la forma 3D. La rampa (función
  `rampaColor`, interpolación lineal entre paradas):
  - **Tierra (cota ≥ 0):** verde costa → verde claro → amarillo-tierra → tostado → marrón →
    cumbres claras (0 … 2000 m).
  - **Mar (cota < 0):** azul **batimétrico** por profundidad (orilla clara → azul profundo;
    0 … −6000 m). Aprovecha que el DEM «Cuba 2026.10» trae fondo marino con cotas negativas.
  - **Sombreado:** el color se multiplica por un factor `0.45 + 0.65·t` (t = tono del relieve),
    comprimido para que el color siga vivo en el llano y solo oscurezca sombras / aclare laderas
    al sol.
- **Interacción con `--sea-level`:** para **ver** la batimetría en azul, NO pases `--sea-level`
  (las cotas negativas se pintan). Si lo pasas, el mar queda transparente y manda la máscara.

Validado con crops reales (Sierra Maestra: verde→marrón, Turquino en blanco, mar transparente)
y con un DEM **sintético** con gradiente tierra→mar profundo que recorre toda la rampa (verde
costa → azul claro → azul profundo), confirmando la batimetría. Solo cambia la herramienta;
librería y API intactos, compila sin warnings y **18 tests** en verde. README y BITÁCORA al
día; sin cambios de API → no se regenera el PDF.

## 86. dem_to_hillshade: más verde en llanuras y `--water-level` (bahías con agua)

Probando `--colored` sobre Cuba, el usuario pidió (1) **más verde en las llanuras** (salían
oliváceas) y (2) que la **Bahía de la Habana se vea con agua** (salía verde como tierra).

- **Diagnóstico de la bahía.** Muestreando el SRTM: la bahía da **~8–12 m** (positivo: el DEM
  la rellena como tierra baja, no la excava) y su textura/pendiente es **igual** que la de la
  tierra (la planitud NO la distingue). Pero hay ventana de cota útil: bahía ~10 m frente a las
  llanuras de alrededor ~48 m.
- **`--water-level <m>` (nuevo, solo con `--colored`, defecto 0).** Toda cota `≤ m` se pinta
  como **agua** (rampa azul; las positivas bajas salen azul claro de orilla). Con `0` solo el
  mar real (`<0`) es agua; subiendo a **~8–12** entran las bahías/lagunas que el DEM rellenó con
  cota baja positiva, **sin** inundar las llanuras (>15 m siguen verdes). Comparado 0/8/15 en la
  Bahía de la Habana: **8** es el equilibrio (bahía con agua, llanuras verdes); 15 ya salpica de
  azul los valles costeros.
- **Rampa de tierra más verde.** Se rebaja el amarilleo: verde sostenido hasta ~400 m
  (0:(120,170,95) → 150:(150,185,110) → 400:(180,200,120)) y solo después amarillo-tierra
  (800), tostado (1200), marrón (1600) y cumbres claras (2000). Las llanuras bajas quedan
  claramente verdes.

Solo cambia la herramienta; librería y API intactos, compila sin warnings y **18 tests** en
verde. README y BITÁCORA al día; sin cambios de API → no se regenera el PDF.

## 87. Curvas de nivel (isohipsas): núcleo `computeContours` + `dem_to_contours`

Siguiente capacidad de trabajo con alturas: **curvas de nivel** como **capa vectorial** (se
activa/desactiva, nítida a cualquier zoom, con lectura de cota al seleccionar), en vez de
raster. Se hace en el núcleo (testeable y reutilizable) + herramienta, como el análisis de
elevación.

- **Núcleo `src/dem/Contours.{h,cpp}`** (cabecera pública `include/libmapa/Contours.h`):
  `computeContours(src, params)` por **marching squares** sobre una rejilla muestreada del DEM.
  Por cada celda y cada nivel que la cruza (cota = `base + k·interval`) emite 1–2 segmentos
  (con los dos cruces saddle resueltos) y luego **encadena** los segmentos de cada nivel en
  polilíneas: el grafo usa como nodo la **clave exacta de la arista** de rejilla donde cae el
  cruce, así dos segmentos que comparten cruce se unen sin tolerancias de coma flotante (salen
  cadenas abiertas y bucles cerrados). **Streaming por filas**: solo dos filas de nodos en
  memoria a la vez (una rejilla de país entero no cabría). `ContourParams{bbox, interval, base,
  stepMeters, minLengthMeters}`; `ContourLine{elevation, points}`. Celdas con algún vértice sin
  dato (NaN) se saltan.
- **Herramienta `tools/dem_to_contours`**: abre el DEM, calcula y vuelca cada curva como
  entidad **polilínea** (`VectorRepository`, mismo esquema `entidad`/`entidad_vertice` que lee
  la app) en una capa; `atributos.cota` lleva la altura. Las **curvas índice** (múltiplos de
  `--index`, p. ej. 500 m) salen más gruesas, con color más oscuro y **etiqueta** de cota.
  Args: `--in --out (--cuba|--bbox) [--interval 100] [--index 500] [--step 150]
  [--min-length 500] [--layer curvas] [--overwrite]`.
- **Test `tst_contours`** (18→**19**): DEM sintéticos — **cono** (anillos cerrados concéntricos
  de radio `R·(1−L/H)`, decreciente con la cota), **plano** (ninguna curva) y **rampa** (niveles
  esperados, curvas casi verticales).
- **Build:** `Contours.cpp` a `libmapa_core` (CMake + `core.pro`); target `dem_to_contours`
  (+ install). La cabecera pública se instala por el glob de `include/`.

Validado end-to-end: curvas reales de la Sierra Maestra (intervalo 100 m, índice 500 m
etiquetado) con **curvas batimétricas** azules en el mar (el SRTM de AWS Skadi trae batimetría).
Compila sin warnings, **19 tests** en verde. README, BITÁCORA y `arquitectura.html` al día;
**API pública nueva → se regenera el PDF**.

- **Recorte por cota (`--min-level` / `--max-level`).** `ContourParams` gana `minLevel`/`maxLevel`
  (por defecto ±∞): no se generan curvas fuera de ese rango. `--min-level 0` recorta al **nivel
  del mar** y omite las curvas batimétricas negativas (verificado: cotas 0–1900 m, cero negativas).
  Test `minLevelClipsBelow` (un cono que baja de 0: sin recorte hay curvas negativas; con
  `minLevel=0`, ninguna). Sigue en **19 tests**; `arquitectura.html` y PDF al día.
- **Progreso al calcular (`ContourProgress`).** Para país entero el muestreo de la rejilla son
  ~20 M de cotas (1–4 min) y la herramienta parecía colgada. `computeContours` acepta un
  callback opcional `ContourProgress = function<bool(hecho,total)>` invocado **por fila**
  (devolver `false` cancela); `dem_to_contours` imprime el porcentaje en la misma línea. API
  pública ampliada → `arquitectura.html` y PDF al día. 19 tests en verde.

## 88. Overlays del paquete desde una BD vectorial (curvas como capa fija del mapa.json)

Al generar las curvas (10 079 entidades) surgió la pregunta de **cómo meterlas en el
`mapa.json`**. Las curvas son una BD de entidades (`VectorRepository`), pero `overlays` solo
cargaba `.geo` (`loadGeoAsLayer`: un trazado, un estilo, sin etiquetas) y `features` es el
**único** fichero editable del usuario. No había hueco limpio para una capa vectorial de solo
lectura.

- **`MapWidget::loadFeaturesAsLayer(db, layerId, name)` (nuevo).** Abre una BD de entidades y
  **añade** sus entidades como una capa más (sin `setContents`, al revés que `loadFeaturesFrom`),
  conservando **estilo, etiqueta y atributos por entidad**. Pensada para capas fijas del paquete.
- **`overlays` acepta `.sqlitedb`.** `loadOverlays` distingue por extensión: `.geo` → `loadGeoAsLayer`
  (decoración fija, no seleccionable); `.sqlitedb`/`.db` → `loadFeaturesAsLayer`. La capa queda
  **no editable** pero sus entidades **seleccionables** (así al pinchar una curva se lee su cota).
  No hubo que tocar `DataPackage` (el overlay ya tenía `id`/`file`/`name`/`zOrder`).
- Así, la entrada en `mapa.json` para las curvas es un `overlays` normal apuntando al `.sqlitedb`.
  También sirve para cargar `Cuba_vector` (u otra BD) como capa fija.
- **Test** `tst_mapwidget::loadsFeaturesDbAsFixedLayer`: añade (no reemplaza) una entidad propia +
  2 curvas; comprueba conteo, atributos (`cota` 500/600) y estilo por entidad (1 con etiqueta).

Compila sin warnings, **19 tests** en verde. README, BITÁCORA, `arquitectura.html` y PDF al día.

- **Fix: `PackageCheck` también valida los overlays `.sqlitedb`.** Al cargar el paquete real, el
  chequeo (`PackageCheck`) seguía leyendo **todo** overlay con `readGeoFile`, así que con el
  overlay de curvas `.sqlitedb` saltaba un aviso falso *«no contiene trazados válidos»* y un
  diluvio de *«línea N no es un vértice»* (interpretaba el binario SQLite como texto). Ahora
  `PackageCheck` ramifica por extensión igual que la carga: `.sqlitedb`/`.db` se valida como **BD
  vectorial** contando las filas de `entidad` (sin cargarlas todas); `.geo` sigue con `readGeoFile`.
  Test `tst_packagecheck::vectorDbOverlayValidates`. 19 tests en verde.

## 89. Zoom mínimo/máximo por capa (las curvas solo al acercar)

Cargadas las 10 094 curvas como overlay, a vista de país (z8) eran un amasijo de etiquetas
(«0 m / 500 m» por todas partes, el «0 m» pegado a cada cayo), feo y algo lento, y **siempre
visibles**. Las curvas son **detalle**: deben salir solo al acercar.

- **`LayerInfo.minZoom`/`maxZoom`** (−1 = sin límite). `FeatureLayer` consulta el zoom actual
  (vía `MapView::zoom()`) y **omite** las capas fuera de su rango, tanto al **dibujar** como al
  **seleccionar** (`featureAt`). El caché del `FeatureLayer` ya se invalida al cambiar el zoom
  (el rango de ejes cambia), así que la puerta se aplica sola.
- **Fachada:** `MapWidget::setFeatureLayerZoomRange(id, minZoom, maxZoom)` y setter en
  `OverlayModel`. En el paquete, el overlay acepta **`minZoom`/`maxZoom`** (los lee `DataPackage`
  y los aplica `loadOverlays`).
- Para las curvas, en el `mapa.json`: `"minZoom": 11` → invisibles hasta z11, limpio a vista
  general y más rápido. (El contorno «0 m» conviene quitarlo en el horneado con `--min-level 1`,
  que ya lo dibuja la capa de costas.)
- Test `tst_mapwidget::loadsFeaturesDbAsFixedLayer` ampliado (guarda min/max en la `LayerInfo`).

Compila sin warnings, **19 tests** en verde. README, BITÁCORA, `arquitectura.html` y PDF al día.

## 90. Recorte por viewport en `FeatureLayer` (pan fluido con muchas entidades)

Con las 10 094 curvas cargadas, mover el mapa iba algo lento: `FeatureLayer` reproyectaba y
dibujaba **todas** las entidades en cada refresco, aunque a z11+ solo caben unas pocas.

- **`FeatureLayer::featureInView(f, rectEjes)`**: antes de dibujar (y antes de consultar en
  `featureAt`), se descarta la entidad cuya caja envolvente **no toca** el rectángulo visible,
  trabajando en **coordenadas de eje** (sin `coordToPixel` ni construir el trazado). Camino
  rápido: si un vértice cae dentro, se dibuja ya; si no, se compara la caja.
- **Solape AABB a mano** (no `QRectF::intersects`): una línea perfectamente horizontal/vertical
  tiene caja degenerada (alto/ancho 0) y `QRectF::intersects` la daría por **no** solapada,
  recortándola mal aunque cruce la vista. El test `viewportCullKeepsCrossingFeatures` (una línea
  con los extremos fuera de pantalla que pasa por el centro) cazó justo ese caso.
- No cambia el API público (método privado del layer). 19 tests en verde (uno nuevo).

Resultado: el *pan* vuelve a ir fluido; solo se proyecta y pinta lo que se ve.

## 91. Curvas: etiquetas solo en las cimas (espaciadas) y cota por tooltip

Con las curvas de todo el país, etiquetar TODA curva índice llenaba el mapa de «500 m / 1000 m»
repetidos. Mejor: etiquetar solo las **cimas**, espaciadas, y leer el resto al vuelo.

- **`dem_to_contours --labels peaks|all|none` (defecto `peaks`).** En `peaks` solo se etiqueta un
  **anillo de cima** por cumbre: curva índice **cerrada** y **pequeña** (envolvente ≤ `--peak-span`,
  6 km por defecto), quedándose con la **más alta** y separando las etiquetas al menos `--label-sep`
  (8 km) — una greedy por cota descendente. `all` = toda curva índice (comportamiento anterior);
  `none` = ninguna. Probado en la Sierra Maestra: de 25 índice → **4 etiquetas** (500/1000/1500),
  cada una en su cima. Solo cambia la herramienta.
- **Tooltip de cota en el `demo`.** El `eventFilter` del mapa, al pasar el ratón, consulta
  `featureAt` y si la entidad trae el atributo `cota` (las curvas lo llevan) muestra
  «`<cota> m`» en un `QToolTip`. Así se lee la altura de **cualquier** curva, no solo de las
  etiquetadas. Reutiliza `MapWidget::featureAt`/`feature`; sin cambios de API.

19 tests en verde (la librería no cambia). README y BITÁCORA al día; sin regenerar PDF.

## 92. Interacción del demo: tooltips, lista↔mapa, análisis temporal, pines arrastrables y con punta

Bloque de usabilidad pedido antes de cerrar la fase. Toca la librería (aditivo) y el `demo`.

- **Icono anclado por un punto (`FeatureStyle::iconAnchor`, normalizado).** `(0.5,0.5)` = centro
  (por defecto); `(0.5,1.0)` = pie-centro. `FeatureLayer` dibuja el pixmap respecto a ese ancla
  (respetando la dpr) y el **hit-test de un punto con icono** usa TODO el rectángulo del pixmap
  anclado (no solo la punta), para poder **agarrar** el pin. Los pines A/B del demo usan
  `(0.5, 31/32)`: su **punta** marca el lugar exacto. Test `iconAnchorHitTestCoversPin`.
- **Capa temporal (`MapWidget::setFeatureLayerTransient`).** Marca una capa para **excluirla del
  guardado** (como las fijas del paquete). El demo marca las de análisis (`elev_*`) y los pines
  A/B: son **temporales**, no se guardan ni reaparecen. Test `persistSkipsTransientLayers`.
- **Tooltip de cualquier entidad (demo).** Al pasar el ratón, `featureAt`+`feature` arman un
  tooltip con **nombre, cota y tipo** de la entidad bajo el cursor (no solo curvas).
- **Lista ↔ mapa (demo).** Clic en una entidad del árbol → `centrarEnItem` centra el mapa en su
  centroide y la **selecciona/resalta** (la selección ya la hacía `seleccionEnArbol`).
- **Pines A/B arrastrables (demo).** El `eventFilter` se instala sobre la `MapView` para
  interceptar el clic antes del desplazamiento: al presionar sobre un pin `punto_ab` se arrastra;
  el `MouseMove` vuelca la coordenada bajo el cursor (`xAxis/yAxis->pixelToCoord` +
  `fromAxisCoords`) en los campos del punto (que redibujan el pin y la lectura); al soltar,
  re-ejecuta el análisis.

Compila sin warnings, **19 tests** en verde (dos nuevos en `tst_mapwidget`). README, BITÁCORA,
`arquitectura.html` y PDF al día. API pública ampliada (`iconAnchor`, `setFeatureLayerTransient`).

## 93. dem_to_hillshade: `--water-mask` (máscara de agua vectorial, costa exacta)

El umbral de cota (`--sea-level`/`--water-level`) no basta cuando el DEM no distingue agua de
tierra baja (bahías/lagunas que el SRTM rellena con cota positiva). La solución exacta es una
**máscara vectorial**: sombrear solo dentro de los polígonos de tierra.

- **`--water-mask <vector.sqlitedb>` (+ `--land-layer <capa>`).** Lee los **polígonos** (geometría
  = polígono) de una BD de entidades (`VectorRepository`, esquema `entidad`/`entidad_vertice`),
  opcionalmente de una capa. Guarda cada parte con su bbox.
- **Por tesela**, rasteriza con `QPainter` (fill-rule par-impar, así los **huecos** = lagunas
  interiores restan) los polígonos cuyo bbox la toca, en coordenadas de píxel de tesela → una
  máscara de 256×256. El bucle de píxeles **solo sombrea donde hay tierra**; el resto queda
  transparente, con la **costa en el borde exacto del vector**. La pendiente en la orilla usa
  vecinos de tierra (como con `--sea-level`), sin acantilado artificial.
- Si la BD no trae polígonos (p. ej. es una base de **teselas**, no de entidades), aborta con un
  mensaje claro.

Validado con un DEM sintético (relieve en todo el tile) + una BD de entidades con un polígono
«isla» y un hueco «laguna»: se sombrea solo la isla, el mar y la laguna quedan transparentes.
Solo cambia la herramienta; librería y API intactos. Compila sin warnings, **19 tests** en verde.
README y BITÁCORA al día.

## 94. `herramientas/osm_costa_a_sqlitedb.py`: costa de OSM → máscara de tierra

La `--water-mask` necesita una BD de **polígonos de tierra**; la fuente natural y gratuita es la
línea de costa de OpenStreetMap (`natural=coastline`). Pero OSM la publica como **líneas dirigidas
partidas en muchos tramos** (no polígonos), así que hace falta un conversor.

- **Entrada:** el GeoJSON que exporta overpass-turbo de
  `way["natural"="coastline"](19.6,-85.5,23.7,-73.5); out geom;` (los cayos pequeños ya vienen como
  `Polygon` cerrados; la costa principal, como muchos `LineString`).
- **Cosido:** une los tramos por sus **extremos comunes** (tolerancia ~1 cm) hasta **cerrar cada
  anillo** de tierra; los pocos tramos sueltos (recortados por el bbox) se cierran uniendo sus
  extremos. En Cuba: 5512 tramos → 3779 anillos + 1238 cayos ya cerrados = **5020 polígonos**
  (el continente = un anillo de 103 153 vértices), solo **3** cadenas quedaban abiertas.
- **Salida:** **una** entidad polígono multi-parte (una parte por anillo) en el esquema exacto
  (`schema_version`=2, `entidad` geometría=2, `entidad_vertice` WITHOUT ROWID, `capa`), que
  `VectorRepository` lee sin migrar. No hace falta orientar los anillos: el **par-impar** del
  rasterizador convierte los interiores (lagunas, bahías) en huecos de agua.

Verificado de punta a punta: `dem_to_hillshade … --water-mask cuba_tierra.sqlitedb --land-layer
tierra` sobre SRTM real de La Habana lee `1 polígono, 5020 partes` y recorta la costa (bahía de La
Habana y su canal salen como agua) con mucho más detalle que Natural Earth. Es un script de
reproducibilidad en `herramientas/` (Python estándar, sin dependencias); no toca la librería ni los
tests.

La misma BD sirve además como **capa de costa vectorial**: cargada como overlay `.sqlitedb` (igual
que `curvas`) dibuja la costa OSM exacta, escalable a cualquier zoom y en un solo fichero (12 MB) que
también es la máscara —una fuente de verdad en vez del ráster `Cuba_Vector` (64 MB, hecho con
`geo_to_tiles`). Como los overlays `.sqlitedb` usan el estilo guardado en cada entidad (no el bloque
`style` del manifiesto, que es solo para `.geo`), el conversor acepta `--line-color` / `--fill-color`
/ `--line-width` (por defecto línea azul `#1565c0` fina, relleno transparente) y los escribe en las
columnas `color_linea`/`color_relleno`/`ancho_linea` como ARGB (QRgb) sin signo. El estilo no afecta
a `--water-mask`, que solo lee geometría.

Probado el overlay vectorial en el demo: se ve **más fino y detallado** que el ráster `Cuba_Vector`
(hecho de un `.geo` más basto), pero **dibujar 384.895 vértices como vector arrastra** a zoom medio.
La solución sin perder la silueta fina: regenerar el ráster **desde la misma costa OSM**. El conversor
gana `--geo FICHERO.geo`, que vuelca los anillos en formato `.geo` (`lon,lat`, `0.0,0.0` separa
trazados); `geo_to_tiles` lo rasteriza con decimado sub-píxel por zoom (detalle íntegro en cada nivel,
solo se pintan los 256×256 visibles, cacheados). Cuba a z4–13 = 3.688 teselas, 21 MB; a z4–15, 100 MB.
Flujo recomendado: ráster OSM como capa base rápida y, si se quiere nitidez a zoom extremo, el overlay
`cuba_tierra` **solo desde z14** (`minZoom`), donde entran pocos anillos en pantalla. Una sola fuente
(la costa OSM) alimenta máscara, overlay y ráster.

## 95. `herramientas/osm_divisiones_a_sqlitedb.py`: divisiones de OSM → capa vectorial

Mismo patrón que la costa, aplicado a **divisiones administrativas** (provincias, municipios…). La
diferencia esencial: la costa se funde en **una** máscara de tierra, pero cada división es **una
entidad CON NOMBRE**, así que se escribe **una `entidad` por relación** (para rotularla y
seleccionarla), no una fundida.

- **Entrada:** el GeoJSON de overpass-turbo de
  `{{geocodeArea:Cuba}}->.cu; relation["boundary"="administrative"]["admin_level"="4"](area.cu); out geom;`
  Overpass-turbo (osmtogeojson) **ya ensambla** las relaciones boundary en `Polygon`/`MultiPolygon`
  con sus propiedades (`name`, `admin_level`…); los `Point` de etiqueta del export se ignoran. Cuba
  admin_level=4 = **16 provincias**, 36 479 vértices.
- **Salida:** una `entidad` polígono por división (multi-parte si tiene varias islas/anillos), con
  `nombre` = `name` (clave configurable con `--name-key`), en la capa `--capa` (por defecto
  `provincias`), estilo `--line-color`/`--fill-color`/`--line-width` (defecto frontera roja `#c62828`
  fina, relleno transparente) y `etiqueta_visible=1` (quitable con `--no-labels`). Mismo esquema v2
  que el resto; se carga como **overlay `.sqlitedb`** (como `curvas`/`costa_osm`).
- **Respaldo:** si el export trajera `LineString` en vez de polígonos, se cosen (como la costa) en
  una capa sin nombres, con aviso.
- Opcional `--geo` para rasterizar las fronteras con `geo_to_tiles` (pierde los nombres).

Verificado con `render_map`: las 16 provincias salen como contorno rojo sobre el relieve, con su
etiqueta, y maneja `Polygon` y `MultiPolygon`. Nota: las fronteras OSM admin_level=4 **incluyen el
mar territorial**, así que se extienden mar adentro y la etiqueta (centroide) puede caer sobre agua;
es correcto para límites administrativos. Script de reproducibilidad, Python estándar sin
dependencias; no toca la librería ni los tests.

## 96. `probe_db`: detección automática de MBTiles

El lector de teselas ya era configurable (tabla, columnas `colZ/colX/colY/colImage`, esquema
XYZ/TMS), así que un **MBTiles** —SQLite de teselas ráster, estándar de Mapbox— se podía usar
describiéndolo a mano. Pero MBTiles tiene particularidades que hacían fallar la sonda genérica:

1. La tabla de teselas suele ser una **VISTA** `tiles` sobre `map`+`images` (esquema normalizado);
   `probe_db` solo buscaba `type='table'` y no la encontraba. Ahora lista `type IN ('table','view')`.
2. MBTiles usa **TMS** y **zoom estándar** (`storedZ = logicalZ`) por especificación, pero sin
   `--ref-bbox` la sonda no podía decidir XYZ/TMS ni el offset. Ahora **reconoce el MBTiles** (tabla
   `metadata` + columna `tile_data`) y **fija** TMS y `zFactor=1/zOffset=0` por especificación, y toma
   la extensión de `metadata.bounds` — así se sonda **sin `--ref-bbox`**.
3. Si `metadata.format` es `pbf`/`mvt` (**MBTiles vectorial**), avisa claramente: la librería dibuja
   teselas de imagen, no vectoriales.

`detectColumns` ya reconocía `zoom_level/tile_column/tile_row/tile_data`, así que no hubo que
tocarlo. Cambios acotados a `TileDatasetProbe` (`detectZMapping`/`detectScheme` reciben un flag
`mbtiles` que corta la inferencia y fija lo conocido). Nuevo caso en `tst_probe`
(`buildSyntheticMbtiles`: vista `tiles`, TMS, `bounds`): se sonda sin referencia y se verifica
TMS, `storedZ=logicalZ`, columnas, extensión sobre Cuba y lectura real a través de la vista. **19
tests** en verde. README con el ejemplo de uso. No cambia la API pública, pero sí se actualizó
`docs/arquitectura.html` (sonda MBTiles + los scripts OSM de costa y divisiones) y se **regeneró el
PDF** `docs/LibMapaStatic_Documentacion.pdf`.

## 97. Etiquetas ancladas, movibles y seleccionables con línea guía (Fase 0)

Primera fase del bloque de **objetivos y áreas**. La pieza base, que comparten objetivos y entidades:
una **etiqueta de texto** (multilínea por `\n`) que cuelga de un punto por una **línea guía**, con un
**desplazamiento en píxeles** relativo al anclaje. Como el desplazamiento es relativo, **al mover el
elemento la etiqueta lo sigue** (se mueven al unísono); al arrastrar la etiqueta solo cambia el
offset.

- **Datos (API pública, aditivos):** `FeatureStyle.labelOffsetPx` y `FeatureStyle.labelAnchorVertex`
  (−1 = automático: el punto o el centro de la geometría; ≥0 = ese vértice, p. ej. 0 para colgar el
  rótulo del primer punto de un área); `MapTarget.labelOffsetPx`. Todos con valor por defecto neutro:
  el comportamiento actual no cambia.
- **Dibujo (dentro de las capas QCustomPlot):** `FeatureLayer` y `TargetLayer` colocan el rótulo en
  `anclaje + hueco + offset`, lo parten por `\n` y trazan la línea guía cuando está desplazado. El
  cálculo de la caja se comparte con el hit-test para que coincidan al píxel.
- **Hit-test:** `FeatureLayer::labelAt` / `TargetLayer::labelAt`, expuestos por la fachada como
  `MapWidget::featureLabelAt` / `targetLabelAt` (permiten seleccionar/arrastrar el rótulo aparte de la
  geometría). `TargetLayer` guarda las cajas realmente dibujadas (respeta nivel de detalle y
  declutter).
- **Setters:** `MapWidget::setFeatureLabelOffset` (persiste vía `updateFeature`) y
  `setTargetLabelOffset` (`TargetModel::setLabelOffset`).
- **Persistencia:** esquema **v3** (`kCurrentVersion=3`): `ALTER TABLE entidad ADD COLUMN label_dx/dy
  REAL DEFAULT 0`. Migración acumulativa y segura: una BD v2 (p. ej. una máscara generada por script)
  se abre y se migra sola; la lectura es tolerante (si faltan las columnas, offset 0).

Falta (fases siguientes): el **arrastre con ratón** en vivo (enrutado en `MapView`), el polígono
«área objetivo» (≤10 vértices, selección por el 1er punto, tipo propio) y el demo que lo muestre. Sin
`.cpp` nuevos → CMake/qmake intactos. Compila sin warnings, **19 tests** en verde (nuevos:
`savesAndLoadsLabelOffset`, `migratesV2DatabaseToV3`, `movableLabelHitTestAndOffset`). README y
`arquitectura.html` + PDF al día.

## 98. Objetivos/áreas Fase 1: arrastre de la etiqueta con el ratón

Sobre la base de la Fase 0, se **engancha el arrastre** del rótulo en la propia librería (en
`MapView`, no en el demo), para que cualquier app lo tenga.

- Con la herramienta **None**, un clic sobre la **caja de etiqueta** (de entidad u objetivo) inicia su
  arrastre; tiene **prioridad** sobre seleccionar un objetivo o desplazar el mapa. El nuevo
  desplazamiento es `offset_inicial + (cursor − inicio)`, así que la etiqueta sigue al cursor y, al ser
  relativo, cuando el punto se mueve la etiqueta lo acompaña.
- `MapView` recuerda de quién es el rótulo (`m_dragLabelFeature` / `m_dragLabelTarget`), el píxel de
  inicio y el offset inicial; en `mouseMove` aplica el nuevo offset y en `mouseRelease` cierra el gesto
  (cursor `SizeAllCursor` durante el arrastre).
- Escritura en vivo: `OverlayModel::setLabelOffset` (nuevo; mismo camino que mover un vértice: apila
  deshacer, avisa con `featureUpdated`/`changed` y se persiste con antirebote) y
  `TargetModel::setLabelOffset` (de la Fase 0). El arrastre de una entidad va envuelto en un grupo de
  deshacer para deshacerse de una vez.

Sin cambios en la API pública ni en CMake/qmake. Compila sin warnings, **19 tests** en verde (nuevo:
`setsLabelOffsetAndNotifies` en `tst_overlaymodel`). El gesto en vivo se valida en el demo (Fase 3);
la lógica de modelo/capa queda cubierta por tests. Falta: el polígono «área objetivo» (Fase 2) y el
demo (Fase 3).

## 99. Objetivos/áreas Fase 2: «área objetivo» (≤N vértices, etiqueta en el 1er punto)

El «área objetivo» que pidió el usuario NO es un tipo de geometría nuevo: es un **polígono** con un
**tipo de dominio** propio (`type = "objetivo_area"`), un **tope de vértices** y la **etiqueta
anclada a su primer punto**. Todo con piezas ya existentes + un único añadido (el tope).

- **Tope de vértices (nuevo):** `MapView::setDraftMaxVertices(n)` (−1 = sin límite), expuesto por
  `MapWidget::setDraftMaxVertices`. Al trazar punto a punto, pasado el tope se **ignoran los clics**;
  el límite se guarda en la entidad (atributo `max_vertices`) y `OverlayModel::insertVertex` lo
  **respeta también al editar**. Así un «área objetivo» se limita a ≤10 puntos de principio a fin.
- **Etiqueta en el primer punto:** `FeatureStyle::labelAnchorVertex = 0` (de la Fase 0) hace que el
  rótulo cuelgue del vértice 0 (y se arrastre/seleccione como el de un objetivo, Fases 0–1).
- **Selección y coordenadas:** ya estaban: clic dentro del polígono lo selecciona (acierto directo por
  área, aunque el relleno sea transparente), los tiradores de vértice salen al seleccionar, y
  `feature(id)->geometry` da sus ≤10 coordenadas.
- **Tipo propio:** `type`/`featuresOfType` (ya existentes) distinguen el «área objetivo» de un
  polígono normal, sin tocar la librería.

La app arma el área con lo que ya hay: `setActiveFeatureLayer`, `setDraftType("objetivo_area")`,
`setDraftStyle(...labelAnchorVertex=0...)`, `setDraftMaxVertices(10)`, `setActiveTool(DrawPolygon)`.
Sin `.cpp` nuevos (CMake/qmake intactos). Compila sin warnings, **19 tests** en verde (nuevos:
`insertVertexRespectsMaxVerticesCap`, `areaLabelHangsFromFirstVertex`). El trazado por clics con tope
se valida en el demo (Fase 3). README, `arquitectura.html` + PDF al día.

## 100. Objetivos/áreas Fase 3: demo (Objetivo, Área objetivo) + anclaje persistente

Cierre del bloque: el **demo** demuestra las capacidades, y se completa la persistencia.

- **Demo (`demo/main.cpp`):** dos herramientas nuevas en la barra de entidades.
  **Objetivo** configura el borrador (`type="objetivo"`, estilo con etiqueta) y crea un punto; al
  crearse se le pone un nombre de **3 líneas** por defecto para que su etiqueta se vea al momento. Se
  arrastra el rótulo con **Navegar** (Fase 1) y el punto con **Editar**; al mover el punto la etiqueta
  lo sigue. **Área objetivo** configura `type="objetivo_area"`, `labelAnchorVertex=0` y
  `setDraftMaxVertices(10)`: polígono de ≤10 puntos con la etiqueta en el **primer vértice**. Las
  herramientas de dibujo genéricas reponen el borrador por defecto (`reponerBorrador`), para no
  heredar la configuración de objetivo. Al seleccionar un área, la barra de estado **lista sus
  coordenadas** (`mostrarCoordsArea`, usando `feature()->geometry`).
- **Persistencia del anclaje:** el esquema v3 gana además `label_anchor` (−1 = automático), para que
  `labelAnchorVertex` sobreviva guardar/cargar (si no, un área recargada perdería la etiqueta del 1er
  punto). Lectura tolerante: en una BD anterior la columna no existe → se queda en −1, no en 0.

Verificado con `render_map` sobre un `.sqlitedb` v3 real: el objetivo sale con su etiqueta de 3 líneas
desplazada y su línea guía, y el área con la etiqueta colgando del primer vértice. Compila sin
warnings (demo incluido), **19 tests** en verde (el de offset ahora cubre `label_anchor`). No se añade
ningún `.cpp` (solo el demo y columnas de esquema) → CMake/qmake intactos. README, `arquitectura.html`
+ PDF al día. Pendiente opcional: migrar el arrastre de los pines A/B del demo (hoy por `eventFilter`)
al mecanismo de la librería.

## 101. Arrastre de marcadores en la librería (pines A/B del demo migrados)

El demo arrastraba sus pines A/B con un `eventFilter` propio sobre el `QCustomPlot` (hit-test manual,
mover spinboxes, recalcular al soltar). Se **sube ese gesto a la librería** como un mecanismo general.

- **`MapFeature::draggable`** (nuevo, por defecto false, no se persiste): si un **punto** lo lleva, la
  herramienta **Navegar** (None) lo **arrastra** al pulsar sobre él (en vez de desplazar el mapa),
  con prioridad por debajo del arrastre de etiquetas y por encima de seleccionar objetivo / mover el
  mapa.
- **Señales** (`MapView` → fachada): `featureDragStarted(id)`, `featureDragMovedTo(id, coord)` y
  `featureDragFinished(id)`. La librería además **mueve el punto en el modelo** (`moveVertex`), así el
  marcador se mueve por defecto; la app decide la semántica con las señales.
- **Demo:** los pines A/B se marcan `draggable`; se borra el bloque de arrastre del `eventFilter` (el
  hover/tooltip se queda) y se conectan las señales: al empezar se resuelve A o B, en cada movimiento
  se vuelca la coordenada en los campos lat/lon (que redibujan el pin) y al soltar se recalcula el
  análisis. Comportamiento idéntico al anterior, pero el gesto lo provee la librería.

Sin cambios de esquema (draggable es una pista de interacción, no se guarda). Compila sin warnings
(demo incluido), **19 tests** en verde. `arquitectura.html` + PDF y README al día. Con esto el bloque
de objetivos/áreas queda cerrado también en el lado de interacción.

## 102. Demo más profesional — Fase A: barra de menú + barras por actividad + overlays ocultos

El demo tenía tres barras de herramientas sueltas (`construirBarraMapa`, `construirBarraEntidades`,
`construirBarraDatos`) con botones ad-hoc, sin barra de menú y con los overlays visibles al arrancar
(cargaban el sistema). Primera fase de una reorganización por fases para que el demo parezca una app
real; **solo toca `demo/main.cpp`** (ni la librería ni los tests).

- **Una sola fuente de acciones.** Se centralizan todas las órdenes en `QAction` miembro (helper
  `accHerr(texto, tool, atajo)` para las de dibujo, que las hace *checkable*, les pone el `MapTool`
  en `setData` y las mete en el grupo exclusivo `m_grupo`). Las mismas acciones se reparten entre el
  menú y las barras, así que menú y barra quedan siempre sincronizados (una acción, un estado).
- **Barra de menú** (`construirMenuYBarra`): **Archivo** (Abrir/Guardar/Guardar como/Salir, con
  `QKeySequence` estándar), **Ver** (submenú *Capa base* con grupo exclusivo, Ampliar/Reducir/Toda
  Cuba, Rejilla, Cobertura; se guarda `m_menuVer` porque la **Fase B** colgará ahí los toggles de los
  paneles), **Dibujo** (Navegar · Punto/Línea/Polígono · Objetivo/Área objetivo · Editar/Borrar ·
  Deshacer/Rehacer), **Elevación** (Perfil · Visión A→B · Viewshed · Fijar A/B · Limpiar análisis),
  **Datos** (Cargar .geo · Simular objetivos) y **Ayuda** (Acerca de).
- **Dos barras agrupadas por actividad**, en vez de tres mezcladas:
  - *Principal* (`barraPrincipal`): selector de capa base + navegación (Navegar/Medir/Zoom área) +
    dibujo (Punto/Línea/Polígono/Objetivo/Área/Editar/Borrar) + Deshacer/Rehacer + Guardar.
  - *Terreno y datos* (`barraDatos`): Rejilla/Cobertura + z, botón DEM (carpeta .hgt o .sqlitedb),
    Cargar .geo, y el bloque de simulación (nº objetivos, Simular, Traza).
- **Overlays ocultos al iniciar** (`ocultarOverlaysAlIniciar`, llamada tras `reconstruirPanel`): recorre
  `featureLayers()` y apaga las no editables (= overlays) con `setFeatureLayerVisible(id,false)`. Así
  al abrir solo se ve el mapa base; cada overlay se enciende a mano (no cargan el sistema de entrada),
  tal como pidió el usuario.
- **`limpiarAnalisis()`** pasa de lambda del botón «Limpiar» a *slot* reutilizable (lo usan el menú
  Elevación y la pestaña). La pestaña «Elevación» conserva sus botones propios (sin duplicar lógica).

Compila sin warnings (`demo` recompilado en limpio), **19 tests** siguen en verde (la librería no
cambia; es prueba de no-regresión). Validación visual en el PC del usuario. Siguientes fases: Panel de
Capas (B), diálogo de nueva entidad (C), propiedades con coordenadas editables (D), guardado explícito
(E) y pulido (F).

## 103. Demo más profesional — Fase B: Panel de Capas con tres grupos (Base/Overlays/Entidades)

El árbol del panel listaba todas las capas vectoriales en un mismo nivel, mezclando overlays (solo
lectura) y capas de usuario, y las bases solo estaban en el combo de la barra. Se reorganiza el árbol
en **tres grupos fijos** que reflejan los cuatro tipos de capa (ver también la explicación al usuario):
sigue siendo **solo `demo/main.cpp`** (ni librería ni tests).

- **Grupo «Base (mapa)».** Los datasets de teselas (`availableBaseLayers`) con casillas de
  **comportamiento de radio**: marcar una fija la capa base (`setBaseLayerId`) y desmarca las demás;
  intentar apagar la activa se ignora (siempre hay un mapa de fondo). No editables. Se mantiene en
  sincronía con el combo de la barra y el submenú *Ver → Capa base* vía la señal `baseLayerChanged`
  (`sincronizarBaseEnArbol`), porque cambiar la base no reconstruye el panel.
- **Grupo «Overlays (solo lectura)».** Las capas vectoriales cargadas del paquete, que la librería marca
  `editable=false` (`MapWidget::loadOverlays → setLayerEditable(id,false)`). Casilla = visibilidad de la
  capa; **arrancan ocultas** (Fase A) para no cargar el sistema. **No se despliegan sus entidades**: un
  overlay puede tener miles de polígonos (la costa, las provincias), así que solo se muestra el recuento.
- **Grupo «Entidades (editables)».** Las capas de usuario (`editable=true`): se despliegan hoja a hoja
  como antes (nombre, tipo, nº de vértices, color), con selección sincronizada con el mapa. Es el único
  grupo modificable.
- **Cabeceras de grupo** (`nuevoGrupo`): ítems no seleccionables ni con casilla, en negrita y a ancho
  completo; solo organizan. El recorrido del árbol pasa a `QTreeWidgetItemIterator` (las hojas de
  entidad cuelgan ahora de grupo→capa→entidad) en `seleccionEnMapa` y `resaltarCapaActiva`.
- **Menú «Ver».** Se cuelgan del `m_menuVer` (que dejó preparado la Fase A) el toggle del panel
  (`QDockWidget::toggleViewAction`) y accesos directos a las pestañas «Capas» y «Elevación».

Rol nuevo en el árbol `RolBase` (id de capa base). Compila sin warnings, **19 tests** en verde
(no-regresión; la librería no cambia). Validación visual en el PC del usuario. Siguiente: Fase C
(diálogo de nueva entidad + flujo dibuja→Navegar).

## 104. Demo más profesional — Fase C: diálogo de alta de entidad + flujo dibuja→Navegar

Antes, al elegir Punto/Línea/Polígono (u Objetivo/Área) se entraba directamente en modo dibujo con el
estilo que hubiera en el panel, y la entidad nacía sin nombre, en la capa activa, y había que volver a
Navegar a mano. Ahora, al elegir una herramienta de creación **sale un diálogo** para fijar el
**nombre**, la **capa** y los **colores de borde y relleno** (+ ancho); al aceptar se dibuja y, al
cerrar el trazo, **se vuelve solo a Navegar**. Sigue siendo **solo `demo/main.cpp`**.

- **`dialogoNuevaEntidad`** (modal): *Nombre* (sugerido según la herramienta), *Capa* en un combo
  **editable** (se elige una existente o se teclea una nueva), y botones de color de borde/relleno
  (reutilizan `pintarBotonColor`/`QColorDialog` con canal alfa) más el ancho de línea.
- **`configurarNuevaEntidad(tipo, estiloBase, maxVert, nombreSug)`**: punto común de los cinco modos.
  Se **difiere con `QTimer::singleShot(0)`** para que el diálogo salga *después* de que el
  `QActionGroup` fije la herramienta de dibujo; así, al **Cancelar**, `volverANavegar` no queda pisado
  por esa activación automática (era el único punto delicado del flujo). Al **Aceptar**: crea la capa si
  es nueva (`addFeatureLayer`, no-op si existe), la pone activa, fija el borrador
  (`setDraftType`/`setDraftStyle`/`setDraftMaxVertices`) con los colores elegidos y **arma** el alta
  (`m_nombrePendiente`, `m_esperandoDibujo`).
- **`alEntidadCreada(id)`** (señal `featureAdded`): si hay un alta armada, pone el nombre a la entidad
  recién dibujada (`feature`+`updateFeature`), **vuelve a Navegar** y la deja **seleccionada** (se ven
  sus datos en el panel). El alta se desarma también si el usuario cambia a una herramienta que no
  dibuja (en el handler del grupo) o al cancelar.
- Las herramientas genéricas parten del **estilo actual del panel** (`estiloActual`); Objetivo y Área
  conservan sus rasgos propios (etiqueta visible, ancla en el 1er punto, tope de 10 vértices) y solo
  dejan elegir nombre/capa/colores encima.

Compila sin warnings, **19 tests** en verde (no-regresión; la librería no cambia). Validación visual en
el PC del usuario. Siguiente: Fase D (panel de propiedades con coordenadas editables).

## 105. Demo más profesional — Fase D: coordenadas editables en el panel de propiedades

El panel mostraba nombre, tipo, atributos y estilo de la entidad seleccionada, pero no sus
**coordenadas**. Se añade una tabla **lat/lon editable** que usa el API de edición de vértices de la
librería. Sigue siendo **solo `demo/main.cpp`** (la librería ya tenía `moveVertex`/`insertVertex`/
`removeVertex`).

- **Tabla «Coordenadas»** (`m_tablaCoords`, 2 columnas): se rellena con los vértices de la entidad
  seleccionada (`feature(id)->geometry`) en `mostrarCoords`, llamada desde `mostrarPropiedades`. Grados
  decimales a 6 cifras.
- **Editar una celda → `moveVertex`** (`coordEditada`): valida el rango (lat −90..90, lon −180..180) y,
  si es inválido, restaura el valor bueno; reutiliza el guardia `m_actualizandoPropiedades` para no
  reescribir al rellenar la tabla.
- **`+ Vértice` → `insertVertex`** (`anadirVertice`): inserta tras el vértice seleccionado en el **punto
  medio** hacia el siguiente (en polígono cierra el anillo; en línea usa el vecino que haya). **`-
  Vértice` → `removeVertex`** (`quitarVertice`): la librería rechaza bajar del mínimo (2 línea / 3
  polígono) y se avisa en la barra de estado.
- **Solo entidades de usuario.** `capaEditable(layerId)` consulta `LayerInfo::editable`: si la capa no
  es editable (overlays del paquete) o la geometría es **multiparte** (p. ej. un `.geo`), la tabla queda
  de **solo lectura** y los botones +/− deshabilitados. En un **punto** (un solo vértice) la celda es
  editable pero no hay +/−. Datasets (teselas) no son entidades: no entran aquí.

Tras insertar/quitar se repuebla la tabla (cambia el nº de filas); al mover, el mapa redibuja el vértice
sin perder el foco de edición. Compila sin warnings, **19 tests** en verde (no-regresión). Validación
visual en el PC del usuario. Siguiente: Fase E (guardado explícito; elevación y A/B siguen siendo
temporales).

## 106. Demo más profesional — Fase E: guardado explícito (fuera el autoguardado)

El demo activaba el **autoguardado** de la librería (`MapConfig::featuresDbFile`): cada dibujo/edición se
escribía solo en el fichero `--features`. El usuario pidió **guardado explícito**: los cambios viven en
memoria hasta que se pulsa Guardar. Sigue siendo **solo `demo/main.cpp`** (la librería ya ofrecía las dos
vías: automática con `featuresDbFile`, manual con `saveFeaturesTo`/`loadFeaturesFrom`).

- **Sin autoguardado.** Ya no se fija `cfg.featuresDbFile`. Si se pasa `--features <fichero>`, se
  **carga** al arrancar (si existe) y se recuerda como **fichero de trabajo** (`m_archivoActual`) para
  «Guardar»; nada se guarda solo. `loadFeaturesFrom` reinyecta los overlays del paquete, así que la carga
  va **antes** de `ocultarOverlaysAlIniciar` para que los overlays sigan arrancando ocultos.
- **Estado «sucio».** `m_sucio` + `marcarSucio()`: un **asterisco** en el título marca que hay cambios
  sin guardar. Lo llaman solo las ediciones de **entidades de usuario** (alta, propiedades, estilo,
  vértices, borrar/vaciar/crear/mover capa, cargar `.geo`); las capas **temporales** (análisis de
  elevación, pines A/B — ya marcadas `setFeatureLayerTransient`) **no** lo llaman, así que trabajar con
  el terreno no ensucia el documento. Guardar/Guardar como/Abrir lo limpian.
- **Confirmación al cerrar/abrir.** `closeEvent` y `abrir()` pasan por `confirmarDescartar()`
  (Guardar / Descartar / Cancelar); si el guardado falla o se cancela, no se cierra ni se reemplaza.
- **Elevación y A/B siguen temporales** (ya lo eran): nunca se guardan como entidades, ni con el
  guardado explícito, porque sus capas son `transient`. Confirmado.

Compila sin warnings, **19 tests** en verde (no-regresión; la librería no cambia). Validación visual en
el PC del usuario. Siguiente: Fase F (pulido + documentación final).

## 107. Demo más profesional — Fase F: pulido + cierre del bloque

Últimos retoques tras las fases A–E y cierre del bloque de profesionalización del demo. **Solo
`demo/main.cpp`** (la librería no cambia en todo el bloque).

- **Sin lógica duplicada.** El botón «Limpiar» de la pestaña Elevación tenía una lambda que repetía
  punto por punto `limpiarAnalisis()` (el slot del menú). Ahora **reutiliza el slot**: una sola fuente de
  verdad para limpiar el análisis.
- **Ayudas en herramientas** (`setStatusTip`/`setToolTip`): cada herramienta de dibujo/navegación explica
  qué hace al pasar el ratón y en la barra de estado. En particular se aclara la diferencia que no es
  obvia: **Polígono** (genérico, sin tope de vértices) frente a **Área objetivo** (≤10 vértices, etiqueta
  y selección en el primer punto).
- **Sin cambios de API ni de arquitectura** en A–F: la librería es la misma, por eso no se regenera el
  PDF (regla de `CLAUDE.md`). README y BITÁCORA quedan al día.

**Resumen del bloque (A–F), todo en el demo:** barra de menú + barras por actividad y overlays ocultos
al iniciar (A); panel de capas en tres grupos Base/Overlays/Entidades (B); diálogo de alta con
nombre/capa/colores y vuelta automática a Navegar (C, con la corrección de la capa nueva tecleada);
coordenadas lat/lon editables de la entidad seleccionada (D); guardado explícito con aviso de cambios
sin guardar, dejando elevación y pines A/B como temporales (E); y este pulido (F). Compila sin warnings,
**19 tests** en verde. Validación visual en el PC del usuario.

## 108. Fix del guardado explícito: `MapConfig::autosaveFeatures` (el autoguardado seguía activo con paquete)

La Fase E apagó el autoguardado quitando `cfg.featuresDbFile`, pero **con un paquete abierto eso no
bastaba**: al dejar `featuresDbFile` vacío, `MapWidget` caía al **fichero de entidades del propio
paquete** (`DataPackage::prepareFeaturesFile`) y activaba el autoguardado ahí. Resultado: aunque se
pulsara «Descartar» al cerrar, lo dibujado ya se había guardado solo en el fichero del paquete y
**reaparecía** al reabrir. Reportado por el usuario.

- **Librería (cambio aditivo del API):** nuevo campo **`MapConfig::autosaveFeatures`** (por defecto
  `true`, sin cambiar el comportamiento de otras apps). En `MapWidget`, el relleno desde el paquete y la
  llamada a `setFeaturesDbFile` quedan **condicionados** a `autosaveFeatures`. Con `false`, **ni se usa
  el fichero del paquete ni se autoguarda nada**, aunque se abra un paquete; la persistencia pasa a ser
  totalmente explícita (`loadFeaturesFrom`/`saveFeaturesTo`).
- **Demo:** fija `cfg.autosaveFeatures = false`. Con esto, dibujar → Descartar → reabrir ya **no**
  recarga lo descartado. `--features` sigue cargándose a mano al arrancar (Fase E) y «Guardar» escribe
  solo cuando el usuario lo pide.
- **Nota para el usuario:** las entidades que se autoguardaron en sesiones anteriores (con el bug) siguen
  en el fichero del paquete en disco (`<AppDataLocation>/<id_paquete>/<fichero_de_entidades>`); ya **no
  se cargan**, pero se pueden borrar a mano si se quiere partir de cero, o abrir con «Abrir» si se
  quieren recuperar.
- **Test:** `tst_mapwidget::autosaveFeaturesFalseSkipsPackagePersistence`: con paquete y
  `autosaveFeatures=false`, lo dibujado no crea el fichero de entidades y no reaparece al reabrir; la capa
  fija (overlay) del paquete sí sigue cargando. Siguen **19 tests** (un slot nuevo del ejecutable
  existente), en verde.

Cambia el API (campo nuevo de `MapConfig`) → `arquitectura.html` y el PDF al día. Compila sin warnings.

## 109. Demo: panel lateral contextual + temas claro/oscuro

El panel lateral metía en una sola columna con scroll el árbol, las propiedades, los atributos, las
coordenadas y el estilo: recargado y poco «de producto». Rediseño **solo del `demo`** (`demo/main.cpp`),
con el enfoque elegido por el usuario: **contextual** y con **dos temas** elegibles.

- **Panel contextual.** Por defecto se ve **solo el árbol de capas** (limpio). Al seleccionar una
  entidad aparece debajo su **ficha**: título «nombre · tipo», campos Nombre/Tipo/Descripción, la **capa**
  (solo lectura) y tres **secciones plegables** — *Atributos*, *Coordenadas*, *Estilo* — colapsadas por
  defecto. Al deseleccionar, la ficha se oculta (`m_ficha->setVisible(hay)` en `mostrarPropiedades`). El
  contenido va en un `QScrollArea` para que nunca se corte.
- **`SeccionPlegable`** (nueva clase auxiliar, sin `Q_OBJECT`): cabecera `QToolButton` con flecha que
  muestra/oculta su contenido. Reutilizable.
- **Temas claro y oscuro**, elegibles por el usuario en **Ver → Tema** (grupo exclusivo) y **recordados**
  entre sesiones con `QSettings` (`tema/oscuro`; se fija `OrganizationName`/`ApplicationName` en `main`).
  Son hojas de estilo (QSS) propias: tipografía, cabeceras de sección, separadores sutiles y color de
  acento (azul). `aplicarTema()` repinta los botones de color (llevan estilo inline propio). Claro por
  defecto.

Sin cambios de API ni de arquitectura (todo en el demo) → no se regenera el PDF. Compila sin warnings,
**19 tests** en verde (no-regresión). Validación visual en el PC del usuario (alternar tema en Ver →
Tema; seleccionar una entidad para ver su ficha).

## 110. Demo: barras con iconos, dos filas y arreglo del menú en tema oscuro

Tras ver la app real, la barra era **una sola fila larguísima de botones de texto** (las dos barras
quedaban en la misma línea): parecía prototipo. Se le da aspecto de producto. **Solo `demo/main.cpp`**.

- **Iconos dibujados en código** (`glifo(clave, color)`): un set coherente de glifos lineales 20×20
  (navegar, medir, zoom, punto, línea, polígono, objetivo, área, editar, borrar, deshacer/rehacer,
  guardar, abrir, rejilla, cobertura, geo, simular, DEM). Se **recolorean con el tema**
  (`aplicarIconos` desde `aplicarTema`, con el primer plano claro u oscuro), así casan en ambos.
- **Barras de solo iconos con tooltip** (`Qt::ToolButtonIconOnly`, 20 px): compactas y legibles (el
  nombre sale al pasar el ratón y en la barra de estado). Cada acción lleva su clave de glifo en una
  propiedad (`accHerr` la pone para las de dibujo; un bloque la pone para el resto).
- **Dos filas de verdad:** `addToolBarBreak()` fuerza que «Terreno y datos» baje a su propia fila en
  vez de amontonarse con la principal. Se añade también **Abrir** a la barra principal.
- **Arreglo del tema oscuro:** el menú superior se quedaba claro; se fija `background-color` y `color`
  explícitos en `QMenuBar`/`QMenuBar::item` (en claro y oscuro) para que el menú siga al tema.

Verificado con capturas offscreen en claro y oscuro (coherentes). Compila sin warnings, **19 tests** en
verde (no-regresión). Sin cambios de API → sin PDF.

## 111. Demo: paleta por tema (fin del fondo blanco), fondos de grupo e iconos pulidos

Retoques finales de diseño sobre el tema, a partir del feedback del usuario. **Solo `demo/main.cpp`**.

- **Paleta (QPalette) por tema.** La hoja de estilo no alcanzaba a los contenedores «planos» (páginas de
  las pestañas, el `QScrollArea`, la ficha), que se quedaban **blancos** en el tema oscuro. Se aplica
  ahora también una `QPalette` propia (`paletaClara`/`paletaOscura`: Window/Base/Text/Button/Highlight…)
  junto a la QSS; así todos los widgets planos toman el fondo del tema y desaparece el blanco.
- **Fondos de grupo en el árbol.** Las cabeceras Base/Overlays/Entidades reciben una **banda sutil**
  (`QPalette::AlternateBase`) para separarlas visualmente. Al cambiar de tema se **reconstruye el panel**
  para que el color de banda se actualice.
- **Iconos pulidos.** Se redibujan `editar` (lápiz limpio, sin la línea que se salía del lienzo ni la
  variable sin usar) y `deshacer`/`rehacer` (flecha curva de retorno con `QPainterPath`, cabeza sólida).
- Acento **azul** (elegido por el usuario) sin cambios. Panel un poco más ancho (min. 340 px).

Verificado con capturas offscreen en claro y oscuro: sin blanco, bandas de grupo visibles e iconos
limpios. Compila sin warnings, **19 tests** en verde (no-regresión). Sin cambios de API → sin PDF.

## 112. Demo: tooltips legibles (estilo de `QToolTip` por tema)

Los tooltips de la barra salían **texto casi blanco sobre fondo casi blanco** (ilegibles): la paleta no
basta para `QToolTip` en Windows. Se añade una regla **`QToolTip`** a las dos hojas de estilo (claro:
texto oscuro sobre blanco con borde; oscuro: texto claro sobre gris oscuro), así el nombre de cada
herramienta se lee bien al pasar el ratón. **Solo `demo/main.cpp`**; sin cambios de API. Compila sin
warnings, **19 tests** en verde.

## 113. Demo: cobertura completa del API (selección de zona + visibilidad de objetivos)

Auditoría de la superficie pública de `MapWidget` frente a lo que el `demo` ejercita, antes del PR. El
demo ya cubría casi todo; faltaban dos capacidades de cara al usuario, que se añaden (**solo
`demo/main.cpp`**):

- **Selección de zona sin zoom:** herramientas **«Seleccionar área»** (`MapTool::SelectArea`) y
  **«Seleccionar polígono (zona)»** (`MapTool::SelectPolygon`) en el menú *Datos*. La librería deja el
  recuadro/contorno visible y emite `areaSelected`/`polygonSelected`; el demo informa de la zona en la
  barra de estado (es lo que usa `fill_map` para elegir qué teselas descargar).
- **Mostrar/ocultar objetivos:** acción *«Mostrar objetivos»* en *Datos* (`setTargetsVisible`).

El resto de la API queda cubierto por el demo: navegación (zoom/centro/ajuste a Cuba), capas base
(exclusivas) y overlays, **entidades** (alta/edición/estilo/atributos/coordenadas, mover/insertar/quitar
vértices, deshacer/rehacer, guardar/cargar explícito, `.geo`), **objetivos** (simulación con `kind` +
atributos, proveedor de símbolos, presupuesto de detalle, traza, clic→datos), **elevación** (perfil,
visión A→B, viewshed, picos, hillshade en vivo, cota bajo el cursor) y herramientas (medir, zoom-área,
rejilla, cobertura por zoom). Quedan sin UI dedicada solo *getters* y variantes redundantes (p. ej.
`visibleNorthWest`, `featuresOfType`, `clearUndoHistory`) o deliberadamente no usadas por el guardado
explícito (`setFeaturesDbFile`/`saveFeaturesNow`); el rango de zoom por capa se demuestra con los
overlays del paquete (curvas de nivel con `z` limitado). Compila sin warnings, **19 tests** en verde.

## 114. Alertas de zona: avisar cuando un objetivo entra o sale de un polígono

**Problema.** La librería ya tenía objetivos móviles y polígonos, pero nada los
relacionaba: para saber si un buque había entrado en una zona prohibida la app
tenía que repetir, en cada actualización, una prueba de punto en polígono contra
todas sus zonas y llevar ella el estado «dentro/fuera». Es geometría pura y la
librería ya conoce las dos cosas.

**Antes, una decisión de alcance.** Se valoró que la librería leyera directamente
NMEA (GPS/GLONASS/BeiDou) o AIS. Se descartó: recibir datos es puerto serie o
UDP/TCP, y la librería tiene prohibida la red (guarda en `CMakeLists.txt`); además
la obligaría a conocer cada protocolo, en contra del diseño (objetivos agnósticos
del dominio). La recepción y decodificación quedan en la app, que llama a
`addTarget`/`updateTarget`. Las alertas de zona, en cambio, no necesitan saber de
dónde viene el objetivo: encajan dentro.

**Qué se hizo.**
- `geo/ZoneMonitor` (núcleo, sin widget ni señales): guarda los contornos de las
  zonas, la última posición de cada objetivo y quién está dentro de qué. Cada
  operación devuelve las transiciones (entra/sale) que provoca. Se prueba sin
  ventanas (`tst_zonemonitor`, 8 casos).
- `MapWidget`: `watchZone(idPoligono)`, `unwatchZone`, `isZoneWatched`,
  `watchedZones`, `targetsInZone`, `zonesOfTarget` y las señales
  `targetEnteredZone(objetivo, zona)` / `targetLeftZone`. `addTarget`,
  `updateTarget`, `removeTarget` y `clearTargets` pasan por el monitor.
- La zona sigue a su entidad: `featureUpdated` (vértices, arrastre, edición) la
  reevalúa; `featureRemoved` la deja de vigilar. Las cargas en bloque y el
  **deshacer** solo emiten `layersChanged`, así que ahí también se resincronizan
  las zonas vigiladas (comparando el contorno guardado para no reevaluar si la
  geometría no cambió).

**Por qué así.**
- **«Dentro» en Web Mercator, no en grados.** Los polígonos se dibujan con lados
  rectos en la proyección del mapa. Hacer el ray-casting en lon/lat daba un
  resultado distinto de lo que ve el operador en lados largos e inclinados: en un
  triángulo (0,0)-(0,60)-(60,60), a lon 30 la hipotenusa pasa por lat 30 en grados
  pero por lat ≈35,3 en Mercator; un objetivo a lat 33 se VE dentro y así cuenta
  (lo fija un test). `GeoMath::pointInPolygon` sigue en grados porque solo decide
  qué teselas bajar.
- **Vigilar avisa de lo que ya hay dentro.** Una zona recién vigilada empieza vacía
  y los objetivos que ya están en ella producen una entrada: la app se entera del
  estado inicial por el mismo canal, sin una consulta aparte.
- **Borrar no es salir.** Quitar un objetivo, vaciar los objetivos o borrar/dejar
  de vigilar una zona no emite salidas: la app lo ha pedido y ya lo sabe; una
  «salida» ahí dispararía alarmas falsas. Una posición inválida sí saca al objetivo
  de todas sus zonas (ha perdido el fijo; ya no se sabe que esté dentro).
- **Multi-parte = cualquier parte.** Un `.geo` con varios anillos es una sola zona;
  basta con estar dentro de uno.
- **Coste.** O(zonas) por actualización de objetivo, con descarte por caja
  envolvente antes de recorrer los lados; 2000 objetivos contra 20 zonas se
  resuelven en milisegundos en el test.
- **No se persiste** la vigilancia (igual que los objetivos): la app la vuelve a
  pedir al cargar, p. ej. para todas las entidades de cierto `type`.

Limitación conocida: zonas que crucen el antimeridiano (±180°) no se tratan.

Compila sin warnings; **20 tests** en verde (17 sin QCustomPlot), Qt 6.4.

## 115. Demo: probar las alertas de zona

Para poder probar §114 sin escribir código, el `demo` añade en *Datos* la acción
**«Vigilar zona seleccionada»**: con un polígono seleccionado lo vigila (o deja de
vigilarlo si ya lo estaba). Las señales `targetEnteredZone`/`targetLeftZone` se
muestran en la barra de estado («⚠ Buque 3 ENTRA en «Zona A» (2 dentro)»). Se
prueba dibujando un polígono sobre la ruta de la simulación de objetivos. **Solo
`demo/main.cpp`**; sin cambios de API. Compila sin warnings, **20 tests** en verde.

## 116. Escala gráfica y cuadrícula de coordenadas

**Problema.** En un mapa sin conexión de uso operativo faltaban dos referencias
básicas: cuánto mide lo que se ve (no había escala) y en qué coordenadas está
cada cosa sin mover el ratón (no había cuadrícula; la «rejilla» existente es de
depuración y marca teselas, no grados).

**Qué se hizo.**
- `geo/MapDecor` (núcleo, funciones puras, `tst_mapdecor`): `scaleBar` elige la
  barra con valor redondo 1-2-5×10ⁿ que cabe en 120 px, en metros/km o en millas
  náuticas; `graticuleStep` elige el paso de la cuadrícula; `formatLatitude` /
  `formatLongitude` rotulan en sexagesimal con la precisión del paso.
- Dos capas nuevas en `src/widget/`, como las demás (`QCPLayerable`):
  `ScaleBarLayer` (abajo a la izquierda, encima de todo) y `GraticuleLayer`
  (encima de las entidades, debajo de los objetivos).
- `MapWidget`: `setScaleBarVisible`, `setScaleBarUnits`, `metersPerPixel`,
  `setGraticuleVisible`, `setGraticuleColor`. Ambas **ocultas por defecto** para no
  cambiar lo que ya pintan las apps existentes.
- `demo`: en *Ver*, «Escala gráfica» (encendida), «Escala en millas náuticas» y
  «Cuadrícula de coordenadas» (blanca sobre la capa satelital).

**Por qué así.**
- **La escala se mide, no se deduce del zoom.** Se toma la distancia geodésica
  real entre dos píxeles separados 100 px en el CENTRO de la vista. En Mercator la
  escala cambia con la latitud (a 60° un píxel cubre la mitad de terreno que en el
  ecuador), y el centro es donde mira el operador. Comprobado sobre la captura: a
  zoom 8 y 22°N un grado de longitud ocupa ~182 px (≈103 km), y la barra de 50 km
  mide ~88 px. El test verifica además que un zoom más da la mitad de m/px.
- **Millas náuticas** porque el seguimiento es naval/aéreo; el redondeo se hace
  en millas (0.2, 0.5, 1, 2 NM), no en metros, para que la etiqueta salga limpia.
- **Pasos de carta, no decimales.** La cuadrícula usa 90°…1°, 30'…1', 30"…1":
  dividen el grado sexagesimal, así las líneas caen en 23°30' y no en 23,5°, que
  es como se leen las cartas. El paso sale del eje que más líneas pediría (ancho
  o alto) y es el mismo en ambos, para una cuadrícula homogénea.
- **Paralelos en la proyección del mapa.** Se dibujan en el eje Y en grados de
  Mercator, igual que las teselas: caen exactamente donde debe estar esa latitud.
- **Rótulos que no se pisan.** Longitudes arriba, latitudes a la izquierda; se
  omite lo que caería en la esquina superior izquierda y en la franja inferior
  donde va la escala (se vio en una captura que el rótulo de 21°55'N tapaba la
  barra).
- **Repintado diferido.** Como el resto de `MapWidget`, los cambios repintan con
  `rpQueuedReplot`; el test procesa eventos antes de capturar.

Compila sin warnings; **21 tests** en verde (18 sin QCustomPlot), Qt 6.4.

## 117. Intercambio vectorial: GeoJSON, KML y GPX

**Problema.** Las entidades solo entraban y salían en formatos propios: la BD
SQLite del repositorio vectorial y los `.geo` heredados. Para traer una zona
dibujada en QGIS o Google Earth, o descargar los puntos de un GPS de mano, había
que convertir fuera; y lo dibujado en el mapa no se podía llevar a ninguna otra
herramienta.

**Qué se hizo.**
- `VectorFormats` (núcleo, API pública en `include/libmapa/VectorFormats.h`,
  `tst_vectorformats`): `readVectorFile` / `writeVectorFile` eligen el formato por
  la extensión (`.geojson`/`.json`, `.kml`, `.gpx`), más los lectores y escritores
  sobre memoria por formato.
- `MapWidget::importVectorFile(ruta, capa)` añade las entidades del fichero a una
  capa (como `loadFeaturesAsLayer`); `exportVectorFile(ruta, capa)` escribe todas
  o las de una capa.
- `demo`: *Datos → Importar GeoJSON/KML/GPX…* y *Exportar capa activa…*.

**Por qué así.**
- **Sin dependencias nuevas.** JSON (`QJsonDocument`) y XML (`QXmlStreamReader/
  Writer`) vienen en Qt Core, en Qt 5 y 6. Nada de GDAL: sería una dependencia
  enorme para tres formatos de texto, y la librería se despliega en PCs sin
  conexión con dos juegos de DLL ya cuidados.
- **Qué se conserva en cada formato** (lo que el formato admite, nada inventado):
  GeoJSON lo guarda todo — nombre, descripción, tipo de dominio
  (`libmapa_type`), atributos y estilo con las claves «simplestyle» (`stroke`,
  `fill`…), que entienden geojson.io y muchos visores. KML guarda tipo y
  atributos en `ExtendedData` y el estilo en `LineStyle`/`PolyStyle` (colores
  `aabbggrr`); al leer se resuelven los `styleUrl` a estilos compartidos, que es
  como exportan Google Earth y QGIS. GPX no tiene polígonos ni atributos: un
  polígono se escribe como track cerrado con una extensión propia
  (`libmapa:polygon`), válida según el esquema, y al releerlo vuelve a ser
  polígono; la elevación de un waypoint (`ele`) se guarda como atributo y vuelve
  a su sitio al exportar.
- **Multi-parte de verdad.** `MultiPolygon`/`MultiLineString`/`MultiPoint`,
  `MultiGeometry` de KML y los varios `trkseg` de un track son UNA entidad con
  varias partes, igual que un `.geo`.
- **Huecos ignorados.** `MapFeature` no modela anillos interiores; se toma el
  exterior. Mejor una zona sin su hueco que inventar una parte que se rellenaría.
- **Robustez al leer.** Vértices o partes inválidos se descartan (una línea de
  un solo punto no entra); un fichero sin ninguna geometría válida es un error
  con motivo, no una capa vacía silenciosa. En GPX, el `<name>` de cada `rtept`
  no pisa el de la ruta (solo cuentan los hijos directos).
- **Escritura atómica** con `QSaveFile`: un fallo a medias no deja un fichero
  truncado encima del bueno. GeoJSON se escribe compacto (indentado, cada
  coordenada ocupaba cuatro líneas).
- Detalle de pruebas: `MapFeature` lleva un `QPixmap` (icono del estilo), que
  exige `QGuiApplication`; por eso `tst_vectorformats` usa `QTEST_MAIN` como los
  demás tests que manejan entidades.

Compila sin warnings; **22 tests** en verde (19 sin QCustomPlot), Qt 6.4.

## 118. Alertas de zona: también para entidades punto, resalte en el mapa, y deshacer arreglado

**Problema (lo vio el usuario en el demo).** Un «Objetivo» colocado dentro de
un «Área objetivo» no avisaba de nada. Causas:
1. En el demo, «Objetivo» es una **entidad punto** (`type="objetivo"`), no un
   objetivo móvil (`MapTarget`); §114 solo vigilaba objetivos móviles.
2. El área no se vigilaba sola: había que seleccionarla y usar *Vigilar zona
   seleccionada*.
3. El único aviso era un texto efímero en la barra de estado: nada en el mapa.

**Qué se hizo.**
- `MapWidget` vigila también las **entidades punto** con un segundo
  `ZoneMonitor` (los ids de entidad y de objetivo son espacios distintos y
  podrían coincidir): señales `featureEnteredZone` / `featureLeftZone`, consultas
  `featuresInZone` / `zonesOfFeature`. Se alimenta de `featureAdded`,
  `featureUpdated` (edición y arrastre) y `featureRemoved`; tras cargas en bloque
  o deshacer (solo `layersChanged`) se resincronizan todos los puntos. Solo
  trabaja si hay zonas vigiladas (coste cero si no). `ZoneMonitor::trackedTargets`
  permite olvidar los que ya no existen.
- **Resalte en el mapa** (`setZoneAlertHighlight`, activo por defecto;
  `setZoneAlertColor`): una zona vigilada con algo dentro se dibuja con borde
  ancho y velo rojo, y cada punto dentro, con un aro. Lo pinta `FeatureLayer`
  por debajo de la entidad, en píxeles; solo redibuja si cambia el conjunto.
- `demo`: las «Área objetivo» se vigilan **solas** (al crearlas, cargarlas o
  deshacer), salvo las que el usuario desactive; los objetivos móviles dentro de
  una zona se pintan en **rojo y más grandes** (por el proveedor de símbolos); y
  un indicador fijo en la barra de estado («⚠ N en zonas») lo resume.

**Fallo de fondo encontrado al probarlo: deshacer no deshacía.** El test de
«deshacer el arrastre saca el punto de la zona» falló porque el punto seguía
dentro tras `undo()`. En `OverlayModel`, cinco ediciones (`moveVertex`,
`insertVertex`, `removeVertex`, `moveFeature`, `setLabelOffset`) tomaban un
iterador del `QHash` y DESPUÉS llamaban a `pushUndo()`, que copia el hash para
la pila de deshacer. Los contenedores de Qt comparten datos entre copias
(*implicit sharing*): escribir por el iterador antiguo no separaba los datos y
modificaba también la instantánea guardada. Resultado: deshacer un arrastre,
una inserción de vértice o un movimiento de entidad restauraba el estado ya
modificado — no hacía nada. Ningún test cubría el deshacer de esas ediciones.
Arreglo mínimo: volver a buscar el iterador tras `pushUndo()` (fuerza la
separación). Nuevo test `tst_overlaymodel::undoRestoresEveryEdit` que cubre las
cinco y el rehacer.

Compila sin warnings; **22 tests** en verde (19 sin QCustomPlot), Qt 6.4.

## 119. Coordenadas UTM y MGRS, y cuadrícula UTM

**Problema.** Todo se mostraba e introducía en grados decimales. En operaciones
se trabaja en UTM y, sobre todo, en MGRS («17Q LF 60056 56750»): sin ellos el
operador tenía que convertir a mano para leer o dar una posición.

**Qué se hizo.**
- `Coordinates` (núcleo, API pública `include/libmapa/Coordinates.h`,
  `tst_coordinates`): `toUtm`/`fromUtm` (zona natural o forzada), `toMgrs`/
  `fromMgrs` (1 m a 10 km), `utmZoneFor`, y `formatCoordinate` /
  `parseCoordinate` para decimal, GMS, grados-minutos decimales, UTM y MGRS.
- `GraticuleLayer` con modo UTM (`MapWidget::setGraticuleMode`): líneas de este y
  norte constantes por cada zona visible, cada 100 km … 100 m según el zoom, con
  los límites de zona marcados; a escala de continente solo los límites.
- `demo`: *Ver → Formato de coordenadas* (la de bajo el cursor), *Cuadrícula UTM*
  e *Ir a coordenada…* (acepta cualquier formato y muestra los demás).

**Por qué así.**
- **Series de Krüger de orden 6 (Karney 2011)**, no las fórmulas clásicas de
  Snyder: error sub-milimétrico en toda la zona y fuera de ella (sirve para
  forzar la zona vecina en la cuadrícula), con el mismo coste.
- **Verificado contra referencias externas**, no solo ida y vuelta: 13 puntos
  (La Habana, Nueva York, Sídney, Islandia, la excepción noruega 32V, Svalbard
  31X/33X a ambos lados de 9°E, el ecuador, un punto a 0,0001° al sur, 83,5°N y
  79,9°S) calculados con PROJ 9.8 (pyproj) y con la librería *mgrs* (GeoTrans):
  este/norte a < 1 mm y MGRS idéntico carácter a carácter. Incluso el caso del
  meridiano 180 exacto (zona 1) se alineó con GeoTrans, que era lo que el test
  esperaba al revés.
- **MGRS trunca**, no redondea: la referencia nombra la esquina suroeste de la
  celda que contiene el punto (norma NGA). Al leer se reconstruye el norte con
  la fila (ciclo de 2000 km) y la banda: una banda mide ~900 km, así que solo hay
  una solución.
- **Excepciones de zona** de Noruega y Svalbard en las coordenadas; la
  cuadrícula dibuja las zonas estándar de 6° (documentado).
- **Cuadrícula UTM en la proyección del mapa**: una línea de este constante es
  una curva en Mercator; se muestrea (40 tramos) y se recorta a los 6° de su
  zona. En el límite de dos zonas se ven las dos cuadrículas, ligeramente
  giradas entre sí, como en una carta. Se comprobó sobre capturas a zoom 3, 6, 9
  y 12 (centro 21,5°N 78,2°W, zona 17: rótulos ~790 kmE y ~2380 kmN, coherentes);
  a zoom 6 los rótulos de 100 km se pisaban y se añadió un descarte de rótulos
  solapados.
- **Lectura tolerante**: MGRS con o sin espacios ni cero en la zona, UTM con
  banda, decimal con coma o espacio, y sexagesimal con hemisferio (también «O»
  de oeste y coma decimal), en cualquier orden lat/lon.

Fuera de 80°S–84°N no hay UTM/MGRS (los polos son UPS, no soportado): las
funciones devuelven inválido y el demo muestra «(fuera de UTM)».

Compila sin warnings; **23 tests** en verde (20 sin QCustomPlot), Qt 6.4.

## 120. Historial completo de cada objetivo, desde su origen

**Problema (lo pidió el usuario).** La traza de un objetivo solo guardaba sus
últimas 300 posiciones (`setTargetTrailLength`): pasado eso se perdía de dónde
venía. Para saber el **origen** —dónde se empezó a ver en el sistema— hay que
conservar todo el recorrido mientras el objetivo exista, y al seleccionarlo
verlo entero desde el primer punto.

**Qué se hizo.**
- `TargetModel` guarda, aparte de la traza que se dibuja para todos, un
  **historial** por objetivo con posición y hora. Se abre con el alta (ORIGEN),
  crece con cada `update` y se pierde solo al borrar el objetivo.
- `MapWidget`: `targetHistory(id)`, `targetOrigin(id)` (`TargetTrackPoint` =
  posición + hora), `setTargetHistoryLimit`, `setTargetHistoryMinDistance` y
  `setSelectedTargetHistoryVisible`. `addTarget`/`updateTarget` aceptan la hora
  del dato (p. ej. la del mensaje AIS); sin ella, la actual en UTC.
- `TargetLayer`: el objetivo **seleccionado** dibuja todo su historial del
  origen a la posición actual (línea de su color sobre halo blanco) y marca el
  origen con un cuadro y su hora («origen 22:13:20»).
- `demo`: al pinchar un objetivo, la barra de estado añade su origen (en el
  formato de coordenadas elegido), la hora y cuántos puntos tiene el historial.

**Por qué así.**
- **Historial separado de la traza dibujada.** La traza corta es para pintar
  miles de objetivos a 30 fps; el historial es el dato. Mezclarlos obligaba a
  elegir entre perder el origen o dibujar recorridos de horas para todos.
- **Memoria acotada sin perder el origen.** Con miles de objetivos durante
  horas, guardarlo todo sin límite agotaría la memoria. Tres medidas: puntos
  compactos (lat, lon, ms: 24 bytes; `QGeoCoordinate`+`QDateTime` costarían
  varias veces más), no anotar un punto a menos de 1 m del anterior (un objetivo
  parado no crece) y, pasado el tope (5000 por defecto), **aclarar** quitando
  uno de cada dos intermedios: el recorrido conserva su forma con la mitad de
  resolución y **el origen y la última posición nunca se pierden**.
- **Dibujado fuera del culling.** El historial del seleccionado se pinta
  aunque el objetivo esté fuera de pantalla: su recorrido —y su origen— pueden
  cruzar la vista.
- **Hora del dato, no de llegada**, cuando la app la tiene: el origen debe decir
  cuándo se vio, no cuándo se procesó el mensaje.
- `QDateTime::fromMSecsSinceEpoch(ms).toUTC()` en vez de la variante con
  `Qt::UTC`, obsoleta en Qt 6.9+ (avisaría en el Qt 6.11 del PC local).

Comprobado en captura: un objetivo que hace una «L» con la traza dibujada
limitada a 5 puntos muestra, seleccionado, la «L» entera y el origen con su hora.
Tests: `tst_targetmodel` (historial más allá de la traza, aclarado que conserva
extremos, distancia mínima, horas, borrado) y `tst_mapwidget`.

Compila sin warnings; **23 tests** en verde (20 sin QCustomPlot), Qt 6.4.

## 121. Historial a GPX con horas, exportar la vista a imagen o PDF, y «Seleccionar área / polígono» que por fin se ven

**Qué se hizo.**
- **Historial de objetivos a GPX.** `VectorFormats` gana `GpxTrack` (nombre,
  descripción, tipo, id del objetivo y puntos `TargetTrackPoint`),
  `tracksToGpx`, `writeGpxTracks` y `parseGpxTracks`. `MapWidget::exportTargetHistory(path, ids)`
  escribe un `<trk>` por objetivo con su historial completo desde el origen y la
  hora de cada punto (`<time>` UTC ISO 8601 con milisegundos). El id va como
  extensión `libmapa:targetId`.
- **Exportar la vista.** `MapWidget::renderImage(scale)` devuelve la vista como
  `QImage`; `exportView(path, MapExportOptions)` la guarda en PDF o en cualquier
  formato de imagen de Qt según la extensión. `MapExportOptions`: título, notas,
  atribución, pie sí/no, factor de resolución de la imagen y hoja/margen del PDF.
  El pie lleva la extensión (NO/SE), el zoom, la fecha UTC y, en PDF, la
  **escala de impresión** («escala aprox. 1:1 570 000 (en el centro)»).
- **Consultar una zona.** `targetsInArea(polígono)` y `featuresInArea(polígono)`
  dicen qué hay dentro sin vigilar nada; `polygonArea` y `pathLength`
  (`Coordinates.h`) dan superficie y perímetro.
- `clearAreaSelection()` quita ahora también el contorno de `SelectPolygon`.
- **Demo.** Datos → *Exportar vista a imagen o PDF…* (Ctrl+P) y *Exportar
  historial de objetivos (GPX)…*. Y el arreglo de *Seleccionar área* /
  *Seleccionar polígono*: ver abajo.

**El fallo de «Seleccionar área / polígono» en la demo.** La librería hacía
su parte: deja la marca en el mapa y emite `areaSelected` / `polygonSelected`.
Pero la demo, al recibir la señal, volvía a Navegar en el acto
(`setActiveTool(None)`), y cambiar de herramienta **borra la marca**. Lo único
que quedaba era un mensaje de 10 s en la barra de estado («Polígono de zona:
4 vértices»), fácil de no ver. Parecía que no hacían nada. Además, nada
explicaba para qué servían: en `fill_map` eligen qué teselas bajar, pero en la
demo no hacían nada con la zona.
Ahora, al cerrar la zona (fuera del evento de ratón, con `singleShot`, para que
se repinte con la marca) se abre **«Zona seleccionada»** con:
- superficie, perímetro y esquinas (en el formato de coordenadas elegido);
- objetivos y entidades que hay dentro (cuántos y los primeros nombres);
- *Crear área* (polígono en la capa activa), *Crear y vigilar* (lo mismo y
  queda como zona de alerta), *Exportar zona…* (encuadra y exporta la vista),
  *Cerrar*.
Al cerrar se quita la marca y se vuelve a Navegar. Los textos de ayuda de
las dos herramientas explican cómo se usan (rectángulo con dos clics; polígono
clic a clic, cerrado con doble clic, clic derecho o Intro; Esc cancela).

**Por qué así.**
- **GPX con hora y no GeoJSON.** GPX es el formato de recorridos con tiempo que
  entienden QGIS (capa temporal), GPXSee o Google Earth. Lo de antes,
  `toGpx(features)`, no sirve: una entidad línea no tiene horas.
  `parseGpxTracks` cierra el ciclo y permite releer un historial para
  reproducirlo. Las horas se leen con o sin milisegundos y con cualquier zona
  horaria, y se pasan a UTC.
- **PDF vectorial a la resolución lógica de la pantalla.** Con `QPdfWriter` a
  300 ppp, los textos de las capas (en puntos) salían el triple de grandes en
  proporción al mapa. A la resolución de la pantalla la hoja es una copia fiel
  de la vista, y no se pierde calidad: entidades, objetivos, escala y
  cuadrícula van como vectores (`QCustomPlot::toPainter` sobre un `QCPPainter`
  en modo vectorial) y las teselas a su resolución nativa. Se hace con
  `QPdfWriter` (QtGui) y no con `savePdf` de QCustomPlot (que usa `QPrinter`)
  porque hacían falta título, pie y una hoja con márgenes.
- **La escala 1:N solo en PDF.** En una imagen no hay tamaño físico. En la hoja
  sí: metros de terreno del ancho del mapa entre los milímetros que ocupa. Se
  calcula en el centro de la vista (Mercator varía con la latitud) y se
  redondea a tres cifras.
- **Escritura atómica (`QSaveFile`)** como en el resto de exportaciones; con una
  extensión desconocida no se crea fichero. JPEG se aplana sobre blanco, porque
  no tiene canal alfa.
- **Superficie exacta en el elipsoide para lo habitual.** La fórmula esférica
  de Chamberlain-Duquette con radio autálico se equivocaba hasta un 0.9 % a
  latitudes altas. Con **latitudes autálicas** (Snyder 3-12), un trapecio de
  meridianos y paralelos sale exacto en WGS84 (12 308.46 km² para 1°×1° en el
  ecuador, 6 123.14 km² a 60° N; comprobado en el test).
- **`featuresInArea` por vértices**: una entidad cuenta si algún vértice cae
  dentro. Es lo que espera quien marca una zona («qué hay aquí») y cuesta
  O(vértices). Un polígono enorme que rodea la zona sin vértices dentro no
  cuenta, y está documentado.

Comprobado: el PNG con título y pie, el PDF (renderizado con `pdftoppm`) y la
demo bajo Xvfb. El diálogo de una zona de 4 vértices muestra 126 284 km², 41
objetivos y 1 entidad; *Crear y vigilar* deja «Zona 1» vigilada con sus 41
objetivos en rojo.
Tests: `tst_vectorformats::gpxTracksKeepTimes`,
`tst_coordinates::measuresAreaAndLength`, y en `tst_mapwidget`:
`exportsTargetHistoryToGpx`, `exportsViewToImageAndPdf` y
`areaQueriesFindTargetsAndFeatures`.

Compila sin warnings; **23 tests** en verde (20 sin QCustomPlot), Qt 6.4.

## 122. Alertas de proximidad entre objetivos (CPA/TCPA)

**Qué se hizo.**
- **`Proximity.h`** (público, núcleo): `CpaResult` (distancia en el CPA, TCPA,
  distancia actual y dónde estará cada uno en el CPA), `computeCpa(posA,
  velA, rumboA, posB, velB, rumboB)`, `ProximityAlert` y `knotsToMetersPerSecond`.
- **`geo/ProximityMonitor`** (núcleo; `tst_proximity`): guarda el estado de
  cada móvil (posición, velocidad, rumbo y hora) y devuelve qué pares entran o
  salen de alerta, igual que `ZoneMonitor` con las zonas.
- **`TargetModel`**: hora de la última posición (`lastUpdateMsecs`) y
  `estimateVelocity(id, ventana)`, la velocidad y el rumbo según la propia traza.
- **`MapWidget`**:
  - `setProximityAlert(distancia, horizonte)` (0 apaga), `proximityAlerts`,
    `targetCpa`, `setTargetVelocity`, `setProximityLinesVisible` y
    `setProximityColor`;
  - señales `proximityAlertRaised(a, b, cpaMetros, tcpaSeg)` y
    `proximityAlertCleared(a, b)`.
- **`TargetLayer`**: dibuja cada par en alerta con una línea discontinua de cada
  objetivo a su punto de CPA, la distancia mínima entre esos dos puntos y el
  rótulo («CPA 453 m en 1 min», o «… (se alejan)»). Va encima de los objetivos
  y omite los rótulos que se pisarían.
- **Demo**:
  - *Datos → Alertas de proximidad (CPA)…*, con la distancia en NM y el
    horizonte en minutos;
  - un aviso en la barra de estado y el recuento «⚠ N CPA» en el indicador;
  - la simulación pasa la hora a la librería: es un «time-lapse» de 30 s
    simulados por paso.

**Criterio de alerta.** Un par está en alerta si ya está a menos de la
distancia, o si su CPA cae por debajo de ella dentro del horizonte
(0 ≤ TCPA ≤ horizonte). Lo primero cubre a dos que ya están encima aunque se
alejen. Lo segundo es el aviso anticipado, que es lo útil.

**Por qué así.**
- **Velocidad estimada por la traza, no por `MapTarget::speed`.** La librería
  no interpreta `speed`: sus unidades son de la app (la demo pone nudos solo a
  los buques). Para no cambiar ese contrato, la velocidad sale del
  desplazamiento en los últimos 30 s del historial, que ya tiene horas (§120).
  La app con datos mejores, como SOG/COG del AIS, los da con
  `setTargetVelocity` en m/s, y esos mandan sobre la estima.
- **Estima que se para.** El historial no anota movimientos de menos de 1 m;
  un objetivo parado deja de crecer, y sin más la estima se quedaría con la
  última velocidad. Por eso cada objetivo guarda la hora de su última posición
  aunque no se anote, y la estima mide hasta ella. Se leyó el reloj una sola
  vez por actualización: con dos lecturas, historial y «última hora» podían
  diferir en 1 ms, y la estima habría tomado el punto actual como origen
  (velocidad 0).
- **Estima de posición entre fuentes desfasadas.** Si un móvil informó hace 60 s
  y el otro ahora, se lleva el atrasado a la hora del reciente antes del
  cálculo. Sin ello, un AIS cada 10 s frente a un radar cada 2 s daría TCPA
  erróneos (comprobado en `monitorDeadReckonsOlderFixes`).
- **Plano tangente local.** Para distancias de alerta (decenas de km) es
  exacto. Las posiciones del CPA se calculan sobre la esfera con
  `atDistanceAndAzimuth`. Se tratan los pares a ambos lados de ±180°.
- **Coste.** Mover un objetivo reevalúa sus pares, O(n). Antes del cálculo
  completo hay un descarte barato: la distancia actual menos lo máximo que
  pueden acercarse en el horizonte. 2000 móviles en un recuadro de 2°
  cuadran con la fuerza bruta (1022 pares) y la pasada completa tarda unos
  230 ms en Release, es decir ~0.1 ms por actualización. Con las alertas
  apagadas no se alimenta el monitor y el coste es cero.
- **Borrar no es alejarse**, como en las zonas: quitar un objetivo no emite fin
  de alerta. Apagar las alertas sí emite el fin de las activas.
- **Demo en tiempo simulado.** La simulación movía los objetivos 0.002–0.006°
  cada 100 ms reales, unos 4 km/s de reloj de pared. Con la estima por traza eso
  daba 190 pares en alerta y CPA en segundos. Ahora pasa a la librería su hora
  simulada (30 s por paso): las velocidades salen de 7 a 22 m/s, y a 1 NM /
  10 min hay unas pocas alertas creíbles.

Comprobado en la demo bajo Xvfb (con 250 objetivos, unas 3–7 alertas a
1 NM / 10 min). De cerca, dos aeronaves casi de frente muestran su punto de
encuentro y «CPA 453 m en 1 min».
Tests: `tst_proximity` (de frente, rumbos que se cruzan con referencia hecha a
mano —TCPA 100 s, CPA 707.1 m—, paralelos, alejándose, entradas inválidas,
±180°, transiciones, estima de posición, cambio de criterios, borrados y 2000
móviles contra fuerza bruta), `tst_targetmodel::estimatesVelocityFromTrack` y
`tst_mapwidget::proximityAlertsWarnOfCloseApproach`.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 123. Anillos de distancia y marcaciones, e inicio y fin de las rutas

**Qué se hizo.**
- **`RangeRings` y `BearingLine`** (públicos, `MapTypes.h`):
  - anillos de distancia (VRM) alrededor de un punto o de un objetivo, cada
    cierta separación, con radiales opcionales;
  - líneas de marcación (EBL) de un punto u objetivo a otro, o con un rumbo y
    una longitud.
- **`MapWidget`**: `addRangeRings`, `updateRangeRings`, `addBearingLine`,
  `updateBearingLine`, `removeRangeMark`, `clearRangeMarks`, `rangeMarks` y
  `bearingLineMeasure` (rumbo y distancia actuales).
- **`RangeLayer`** (`src/widget`) las dibuja:
  - anillos geodésicos con su distancia arriba (el exterior en continua, los
    demás discontinuos);
  - radiales con su marcación («030°») y cruz en el centro;
  - marcaciones por la ortodrómica, con halo, flecha en el destino y el
    rótulo «202° · 26.7 NM».
- **`MapDecor`**: `formatDistance` (sin ceros sobrantes: «1 NM», «2.5 km»),
  `formatBearing` («045°») y `geodesicCircle`. Los rótulos CPA usan ahora el
  mismo formato.
- **Inicio y fin de rutas**:
  - al leer GPX, cada ruta o track guarda `gpx_kind` y, si sus puntos traen
    hora, `time_start` y `time_end`; un historial exportado trae además
    `target_id`;
  - `FeatureLayer` marca el inicio (círculo verde) y el fin (cuadrado rojo);
  - la línea seleccionada lleva además «inicio 13:56:42» y «fin 14:26:42», con
    fecha si no son de hoy o la ruta cruza de día;
  - `setRouteEndpoints`: `Hidden`, `Selected`, `Routes` (por defecto: siempre
    las rutas GPX, y cualquier línea al seleccionarla) o `AllLines`.
- **Demo**, en el menú *Ver*:
  - *Anillos de distancia…*: separación en NM, número y radiales cada 30°,
    alrededor del objetivo seleccionado o de un punto que se pinche;
  - *Marcación (rumbo y distancia)*: del objetivo seleccionado, o de un punto,
    a otro objetivo o punto; si se pincha cerca de un objetivo, la línea
    cuelga de él;
  - *Inicio y fin de rutas* (los cuatro modos) y *Quitar anillos y
    marcaciones*.

**Por qué así.**
- **En la capa de los objetivos.** Unos anillos o una marcación que cuelgan de
  un objetivo tienen que moverse con él. `RangeLayer` es otro `QCPLayerable` de
  la capa *buffered* «targets», creado antes que `TargetLayer`. Cuando los
  objetivos repintan su capa se repinta también, sin tocar teselas ni
  entidades, y los objetivos quedan encima.
- **Geodésicos, no círculos de pantalla.** En Mercator una distancia real se
  estira con la latitud: un anillo de 5 NM dibujado como círculo de píxeles
  mentiría al norte y al sur. Se trazan con `atDistanceAndAzimuth` y las
  marcaciones largas siguen la ortodrómica. El test comprueba el radio a 0°,
  45° y 75°.
- **El rumbo y la distancia se miden al dibujar**, con los objetivos donde
  estén. No se guarda ningún valor que pueda quedarse viejo, y
  `bearingLineMeasure` da lo mismo que el rótulo.
- **Inicio y fin solo en las rutas.** Ponerlos en todas las líneas llenaría de
  marcas las costas y overlays de solo lectura. Lo que el usuario pidió eran
  las rutas de los historiales: por eso por defecto van siempre en lo leído de
  GPX y en cualquier línea al seleccionarla. Los rótulos salen solo en la
  seleccionada y hacia fuera del recorrido (el de inicio por detrás, el de fin
  por delante), para que no se pisen aunque la ruta sea corta. Las marcas se
  dibujan encima de los tiradores de edición.
- **La hora viaja en atributos.** Así la conserva también un GeoJSON exportado
  después, y sirve para cualquier GPX con tiempos, no solo los de esta
  librería.

Comprobado en la demo bajo Xvfb: anillos de 5 NM con radiales alrededor de una
aeronave y una marcación «202° · 26.7 NM» hasta un buque; y un historial
exportado, reimportado y seleccionado con «inicio 13:56:42» y «fin 14:26:42».
Tests:
- `tst_mapdecor`: formatos, y radio del anillo a varias latitudes;
- `tst_vectorformats::gpxRoutesKnowStartAndEnd`;
- `tst_mapwidget`: `rangeRingsAndBearingLinesFollowTargets`, que comprueba que
  la marcación pasa de 045° a 180° al moverse el objetivo, y
  `routesShowStartAndEnd`.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 124. Reproducción de historiales en el tiempo

**Qué se hizo.**
- **`TrackReplay.h`** (público, núcleo):
  - `sampleTrack(pista, hora)` devuelve si la pista existía en ese instante,
    dónde estaba (interpolando por la ortodrómica entre los puntos grabados),
    con qué rumbo y qué último punto había alcanzado;
  - `trackTimeRange` da el rango de horas de un conjunto de pistas.
- **`TrackPlayer`** (público, widget):
  - `loadFile` / `setTracks`, `play`, `pause`, `stop` (vuelve al principio),
    `seek(hora)` y `setSpeed` (factor sobre el tiempo real; 60 por defecto);
  - `setRoutesVisible` y `targetIdOf`;
  - señales `timeChanged`, `playingChanged` y `finished`.
- **Capas temporales fuera de deshacer.** `OverlayModel::setLayerUndoable` y
  `MapWidget::setFeatureLayerTransient` sacan una capa de deshacer/rehacer:
  sus altas, bajas y vaciados no apilan, y al restaurar una instantánea se
  conserva su contenido actual.
- **Demo**: *Datos → Reproducir historial (GPX)…*, con una barra abajo:
  - reproducir/pausa, volver al principio y un deslizador de tiempo;
  - la hora, la velocidad (×1, ×10, ×60, ×300, ×1800), «Rutas» y cerrar.
  Al abrir un historial se para la simulación.

**Cómo funciona.** Cada pista es un **objetivo normal del mapa** mientras
existió (entre su primer y su último punto con hora). Cuando aparece, se da de
alta en su origen con su hora y se le pasan uno a uno los puntos grabados ya
alcanzados, también con su hora. Después recibe la posición interpolada del
instante. Al acabar su tiempo se quita.

**Por qué así.**
- **Objetivos, no una capa aparte.** Al reproducir con la API pública de
  objetivos funciona todo lo demás sin tocarlo: los iconos por `kind` (el
  `type` del track), la traza, el historial con su «origen HH:mm:ss» al
  seleccionar, las alertas de zona y de proximidad y los anillos o marcaciones
  colgados de una pista. Las alertas CPA usan además las horas del fichero, así
  que el análisis de un incidente pasado da los mismos avisos que en vivo.
- **Se pasan los puntos reales, no solo el interpolado.** A ×1800 un
  fotograma salta minutos. Si solo se moviera el objetivo a la posición
  interpolada, su traza y su historial se saltarían los giros grabados. Se le
  pasan todos los puntos alcanzados con su hora.
- **Hacia atrás se rehace desde el origen.** Ir atrás en el tiempo no puede
  dejar al objetivo con la traza «del futuro»: el reproductor lo quita y lo
  vuelve a construir hasta esa hora. El test comprueba que el historial se
  acorta y que el origen sigue siendo el primer punto.
- **Tiempo real medido.** Cada fotograma (100 ms) avanza según el tiempo real
  transcurrido (`QElapsedTimer`) por la velocidad, no según el intervalo
  teórico. Así un equipo cargado no ralentiza la reproducción.
- **Rutas completas con inicio y fin.** Con «Rutas», cada pista entera se ve en
  discontinua en una capa temporal, como track GPX: lleva sus marcas de inicio
  y fin (§123), y al seleccionarla, sus horas. Se ve qué falta por recorrer.
- **Fuera de deshacer.** Esas rutas entraban en la pila de deshacer, y como
  deshacer restaura instantáneas completas, un «deshacer» del usuario podía
  quitarlas, o hacerlas reaparecer tras cerrar la reproducción. Ahora las capas
  temporales no apilan y se conservan al restaurar. El siguiente id nunca
  retrocede, para no repetir el de una entidad temporal conservada. Vale
  también para cualquier otra capa temporal de la app.
- Solo toca sus objetivos: los demás del mapa no se ven afectados (comprobado
  en el test).

Comprobado en la demo bajo Xvfb: 30 objetivos simulados, historial exportado y
reproducido a ×60. A mitad de la reproducción, «Buque 1» seleccionado muestra
su origen y su recorrido hasta ese momento; el resto de su ruta va en
discontinua hasta el fin, y la barra muestra la hora.
Tests:
- `tst_vectorformats::samplesTracksInTime`: interpolación, rumbo, límites y
  puntos sin hora;
- `tst_mapwidget::trackPlayerReplaysHistory`: aparición escalonada, posiciones
  interpoladas, ir atrás, rutas, reproducción hasta `finished`, limpieza;
- `tst_overlaymodel::undoLeavesTransientLayersAlone`.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 125. Salir del modo reproducción

**El problema.** La reproducción (§124) se abría y funcionaba bien, pero no se
veía cómo terminar. El único botón de cerrar era un icono «X» al final de la
barra, sin texto, fácil de pasar por alto. Además, la barra se podía ocultar
desde el menú contextual de las barras de herramientas, y entonces la
reproducción seguía activa (objetivos y rutas en el mapa) sin ningún control
para quitarla.

**Qué se hizo.**
- **Librería**: `TrackPlayer::isLoaded()` dice si hay una reproducción
  activa, y la nueva señal `cleared()` avisa al salir. `clear()` queda
  documentado como «salir de la reproducción»: para, quita del mapa los
  objetivos y las rutas que puso, olvida las pistas y emite `cleared()` (solo
  si había algo).
- **Demo**, varias formas de salir:
  - el botón **«Salir de la reproducción»**, con texto, en la propia barra;
  - la misma acción en **Datos**;
  - la tecla **Esc**;
  - un clic en el indicador azul **«▶ Reproducción · Esc para salir»** de la
    barra de estado, que se ve mientras dura el modo. Sirve además para no
    confundir lo grabado con objetivos en vivo.
- La barra de reproducción ya no se puede ocultar desde el menú contextual
  (`toggleViewAction` desactivada): solo se cierra saliendo.
- Activar la simulación sale antes de la reproducción: en vivo y reproducido
  no se mezclan.

**Por qué Esc solo durante la reproducción.** Fuera de ese modo, Esc es de las
herramientas del mapa (cancelar un dibujo o una medida). La acción está
desactivada mientras no se reproduce, así que su atajo no le quita la tecla a
la vista.

Comprobado en la demo bajo Xvfb, con pulsación real (`QTest::keyClick`, que
pasa por los atajos de Qt, enlazado solo para la prueba). Abierta con 20
pistas: 20 objetivos y 20 rutas. Tras Esc: 0 objetivos, 0 rutas, la barra
cerrada y el indicador apagado. El indicador y «Simular» también salen.
`tst_mapwidget::trackPlayerReplaysHistory` comprueba `cleared()` e
`isLoaded()`.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 126. Medir superficie en pantalla

**Qué se hizo.**
- **`MapTool::MeasureArea`**: se marca un polígono clic a clic y la librería
  rotula en vivo, junto al cursor, la **superficie** y el **perímetro**,
  contando el lado hasta el cursor y el de cierre. Con dos puntos rotula la
  longitud del lado.
- Doble clic, clic derecho o Intro cierran el polígono. Entonces se emite
  **`areaMeasured(AreaMeasurement)`** (polígono, m² y m), y el contorno se
  queda con su medida en el centro hasta cambiar de herramienta o llamar a
  `clearAreaSelection`. Un clic más empieza otra medida. No crea ninguna
  entidad.
- `MapDecor::formatArea`: «850 m²», «12.5 ha», «3.42 km²» o «1 NM²», según
  las unidades de la escala.
- `FeatureLayer::setDraftLabel`: rótulo del borrador, junto al cursor
  mientras se traza y en el centro del polígono una vez cerrado.
- **Demo**: «Medir superficie» junto a «Medir», en la barra y en *Dibujo*. El
  resultado sale también en la barra de estado.

**Por qué así.** Reutiliza el trazado de `SelectPolygon` (borrador con línea de
goma, cierre por doble clic, Intro o clic derecho) con un indicador más: es el
mismo gesto, pero da un número en lugar de una zona. El cálculo es el de
`polygonArea`/`pathLength` (§121), sobre el elipsoide: el rótulo y la señal dan
exactamente lo mismo. El color morado distingue la medida de una selección
(negra) y de una entidad.

Comprobado en la demo bajo Xvfb (medida en vivo y polígono cerrado con su
rótulo). `tst_mapwidget::measureAreaToolMeasuresPolygon` marca con clics reales
un cuadrado de unos 2 km y comprueba:
- que la medida en vivo cambia lo dibujado;
- que `areaMeasured` sale al pulsar Intro y no antes;
- que su superficie es exactamente `polygonArea` del polígono marcado y vale
  unos 4 km² (con la cuantización de los píxeles a zoom 12);
- que no se crea ninguna entidad, que cambiar de herramienta quita la marca y
  que con dos vértices no hay medida.

`tst_mapdecor` comprueba los formatos.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 127. Importar un fichero es una sola operación de deshacer

**El problema.** `importVectorFile` (GeoJSON/KML/GPX) y `loadFeaturesAsLayer`
añadían las entidades una a una con `addFeature`, y cada alta apilaba una
instantánea para deshacer. Eso tenía dos efectos:
- Para quitar una importación de 300 entidades había que deshacer 300 veces.
  Como la pila guarda 50 pasos, era imposible volver a lo de antes.
- Cada instantánea copia el estado (al modificarlo, la copia compartida de Qt
  se separa), así que la importación costaba O(n²).

**Qué se hizo.** `MapWidget::Impl::addFeaturesAsOneUndo` añade un lote de
entidades dentro de un grupo de deshacer (`beginUndoGroup`/`endUndoGroup`, el
mismo mecanismo que agrupa el arrastre de un vértice). Lo usan las dos
funciones. El grupo se abre **antes** de crear la capa, así que deshacer la
importación quita también la capa nueva, y rehacer la devuelve entera.

**Comprobado.** `tst_mapwidget::importIsOneUndoStep` importa 300 puntos.
Comprueba que un deshacer los quita todos y también la capa, que la entidad
previa del usuario sigue y que un rehacer los devuelve. Contra el código
anterior el test falla (un deshacer solo quitaba la última entidad), y la
importación pasa de 6 ms a 1 ms.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 128. Textos propios de Qt en español en la demo

**El problema.** Los botones y diálogos estándar de Qt (`QDialogButtonBox`,
`QMessageBox`, diálogos de fichero) salían en inglés («Close», «Cancel»,
«Yes») dentro de una demo escrita en español. Además, los scripts de
despliegue usan `windeployqt --no-translations`, así que aunque la demo
cargara una traducción, en lo desplegado no habría ninguna que cargar.

**Qué se hizo.**
- La demo carga `qtbase_es.qm` al arrancar (`cargarTraduccionQt`). Lo busca
  primero en `translations` junto al ejecutable (lo desplegado) y, si no, en la
  instalación de Qt (`QLibraryInfo`, con la variante de Qt 5 y la de Qt 6). Si
  no lo encuentra, sigue en inglés sin error.
- `desplegar.bat` (Qt 6) y `desplegar_qt5.bat` (Qt 5) copian **solo**
  `qtbase_es.qm` a `translations\`. No se quita `--no-translations`, que
  copiaría las traducciones de todos los idiomas. Cada script toca solo el suyo,
  como pide la guía de despliegue.

**Comprobado** en la demo bajo Xvfb: un `QDialogButtonBox` con
Open/Close/Cancel/Yes muestra «Abrir», «Cerrar», «Cancelar» y «Sí».

## 129. Filtro de objetivos

**Qué se hizo.**
- **`TargetFilter`** (público, `MapTarget.h`). Un objetivo **pasa** si cumple
  todos los criterios que se pongan:
  - clases (`kinds`);
  - velocidad mínima y máxima (m/s);
  - solo dentro de alguna zona vigilada;
  - un `predicate` libre de la app, por ejemplo sobre `attributes`.

  Con `dim`, los que no pasan se ven **tenues** (25 %, sin rótulo ni traza);
  sin `dim` se **ocultan** y no se pueden pinchar.
- **`MapWidget`**: `setTargetFilter`, `clearTargetFilter`, `hasTargetFilter`,
  `targetPassesFilter` y `filteredOutTargets`.
- **`TargetLayer::setFilteredOut`** y `MapView::targetAt`, que se salta los
  objetivos ocultos.
- **Demo**: *Ver → Filtrar objetivos…* (Ctrl+F).
  - Casillas con los tipos presentes, velocidad mínima y máxima en nudos,
    «solo dentro de zonas vigiladas», «atenuar en vez de ocultar» y «Quitar
    filtro».
  - Indicador «Filtro: N de M» en la barra de estado.
  - El rótulo «kn» de los buques simulados es ahora la velocidad a la que se
    mueven de verdad; antes era un número al azar que no casaba con el filtro.

**Por qué así.**
- **La velocidad es la de CPA.** La velocidad dada por la app
  (`setTargetVelocity`) o, si no hay, la estimada por la traza. Así el filtro
  no interpreta `MapTarget::speed`, que es de la app.
- **O(1) por actualización.** Al moverse un objetivo, o al cambiar su
  velocidad, se reevalúa solo él. Cuando una transición de zona lo mete o lo
  saca de una zona vigilada, también. A la capa solo se le avisa si su
  estado cambió. Recorrer todos los objetivos ocurre solo al cambiar el
  filtro.
- **Ocultar no es borrar.** Los ocultos siguen en `targets()` y cuentan para
  las alertas de zona y de proximidad: un filtro de vista no debe callar una
  alerta.
- **Atenuar** sirve para ver lo que interesa sin perder el contexto. El
  objetivo seleccionado se ve siempre normal, aunque no pase.

`tst_mapwidget::targetFilterHidesOrDims` comprueba:
- clase y velocidad, y que un oculto no se puede pinchar y uno atenuado sí;
- que la imagen cambia en cada modo;
- la reevaluación al cambiar la velocidad y al entrar en una zona;
- el predicado sobre atributos, el borrado y quitar el filtro.

En la demo bajo Xvfb, con 60 objetivos y el filtro «buques a más de 15 kn»,
quedan 19 visibles.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 130. Agrupar objetivos al alejar el zoom

**Qué se hizo.**
- **`MapDecor::clusterPoints`** (núcleo): agrupa puntos de pantalla por celdas.
  Una celda con al menos N puntos es un grupo, con su centroide. O(n).
- **`TargetClustering`** (público, `MapTarget.h`): `enabled`, `cellPixels`,
  `minTargets`, `maxZoom` (por encima no se agrupa) y `color`.
- **`TargetLayer`** dibuja cada grupo como un círculo con el número de
  objetivos que reúne. El radio crece con el logaritmo del número, para que 10
  y 1000 se distingan sin que los grandes tapen el mapa. Los grupos van encima
  de los objetivos sueltos.
- **`MapWidget`**: `setTargetClustering`, `targetClustering`,
  `targetClusterAt` y la señal **`targetClusterClicked(ids, posición)`**.
- **Demo**: *Ver → Agrupar objetivos al alejar*, activo por defecto hasta
  zoom 10. Pinchar un grupo encuadra a sus objetivos, o amplía dos niveles si
  están casi en el mismo punto.

**Por qué así.**
- **Rejilla anclada al mundo.** Las celdas se cuentan desde un punto fijo (0°,
  0°) pasado a píxeles, no desde la esquina de la pantalla. Con una rejilla de
  pantalla, al desplazar el mapa los objetivos cambiarían de celda y los grupos
  «bailarían». Así solo cambian al cambiar de zoom (comprobado en el test).
- **Solo cambia el dibujo.** Los objetivos agrupados siguen existiendo y
  cuentan para las alertas y los filtros. Lo que no se puede es pinchar uno
  suelto dentro de un grupo: `targetAt` lo salta y el clic va al grupo.
- **Nunca se agrupan** el objetivo seleccionado (el usuario lo está mirando) ni
  los atenuados por el filtro (se ven tenues a propósito).
- **Rejilla y no distancias.** Agrupar por distancias entre todos los pares
  (DBSCAN o k-medias) cuesta O(n²) o tiene parámetros difíciles de elegir. La
  rejilla es O(n) por fotograma con miles de objetivos y da grupos estables y
  predecibles. A cambio, los grupos se alinean en una cuadrícula, cosa que a
  zoom bajo se lee como «densidad por zona».

Comprobado en la demo bajo Xvfb: 1000 objetivos a zoom 6 se ven como unos 35
grupos con su número. Pinchar el de 47 encuadra y deja grupos pequeños y
objetivos sueltos a zoom 8.
Tests:
- `tst_mapdecor::clustersByWorldAnchoredCells`: grupos, centroide, mínimo,
  orden y estabilidad al desplazar;
- `tst_mapwidget::targetClusteringGroupsCrowds`: un grupo de 20 frente a un
  objetivo suelto, el clic al grupo, que el seleccionado sale y que `maxZoom`
  desactiva.

Compila sin warnings; **24 tests** en verde (21 sin QCustomPlot), Qt 6.4.

## 131. Ejemplo `app_seguimiento`

`examples/app_minima` se quedó en lo mínimo a propósito: es la plantilla que se
copia para empezar un producto, y meterle objetivos, alertas y menús la haría
peor plantilla. Por eso el API nuevo (secciones 116–130) se enseña en un ejemplo
hermano, `examples/app_seguimiento`, con los mismos `CMakeLists.txt` y `.pro`
(usa la librería **instalada** con `find_package` / `libmapa.pri`, nunca las
fuentes).

En unas 220 líneas, sin `Q_OBJECT` (todo con lambdas, para no necesitar moc en
el `.pro`):
- tres buques simulados que se mueven por estima cada segundo
  (`updateTarget` con hora, `setTargetVelocity` en nudos);
- una zona vigilada con aviso al entrar (`targetEnteredZone`);
- alerta de proximidad CPA/TCPA (0,5 NM, 15 min) sobre el barco propio;
- anillos de distancia en el propio y demora hacia otro buque;
- agrupación al alejar el zoom y un filtro «solo a más de 9 kn» atenuado;
- medir superficie; exportar la vista a PDF y el historial a GPX;
- reproducir el historial con `TrackPlayer` y salir de la reproducción.

La demo sigue siendo el escaparate completo; este ejemplo es lo que hay que leer
para saber **qué llamadas** usar en una app propia.

Comprobado: instalada la librería en una carpeta temporal, el ejemplo configura
y compila sin warnings con `-DCMAKE_PREFIX_PATH` y arranca. No añade tests (es un
ejemplo fuera del árbol de la librería): siguen **24 tests** en verde.

## 132. `demo/main.cpp` partido en ficheros

La demo había crecido hasta 4864 líneas en un solo `main.cpp`, con toda la
clase `Ventana` escrita dentro de su declaración. Era difícil de leer y cada
cambio recompilaba todo. Se ha repartido **sin cambiar ni una línea de lógica**:

| Fichero | Contenido |
|---|---|
| `main.cpp` | `main()` y la traducción de Qt |
| `Comun.h` / `Comun.cpp` | includes, roles del árbol, `SeccionPlegable`, iconos y glifos dibujados en código |
| `Ventana.h` | la declaración de la clase (métodos y miembros) |
| `Ventana.cpp` | constructor, señales del mapa, barra de estado |
| `VentanaMenus.cpp` | menús, barras y temas claro/oscuro |
| `VentanaPanel.cpp` | árbol de capas/entidades, ficha, estilo, coordenadas |
| `VentanaDibujo.cpp` | alta de entidades y vuelta a Navegar |
| `VentanaElevacion.cpp` | pestaña Elevación (perfil, visión, viewshed, picos) |
| `VentanaFicheros.cpp` | guardar/abrir, importar/exportar, vista a imagen/PDF, GPX |
| `VentanaObjetivos.cpp` | seguimiento, zonas, CPA, anillos, medidas, reproducción, simulación, filtro |

Sigue siendo **una clase**: partirla en varias habría cambiado el diseño y
habría hecho falta pasar punteros entre ellas, todo para una demo. Así cada
fichero se lee de una vez y lo que se toca en uno no recompila los demás.

Se hizo con un script (no a mano) para no introducir errores: mueve cada cuerpo
de método fuera de la clase como `Ventana::metodo`, quitando de la definición
los argumentos por defecto, `override` y `static`, y deja en `Ventana.h` la
firma original. `qcustomplot.h` solo lo incluye `VentanaElevacion.cpp` (la
cabecera adelanta `class QCustomPlot;`). De paso se escribió el comentario que
faltaba encima de 26 métodos (la norma es uno por función).

La demo no está en qmake, así que solo cambia su `add_executable` en
`CMakeLists.txt`. Compila sin warnings y arranca; **24 tests** en verde.

## 133. Anillos de distancia: automáticos según el zoom, puntos fijos y gestor

Probándolo en Windows, los anillos no convencían:
- con 20 NM de separación a zoom 10 solo se veía un anillo y **ningún rótulo**:
  las distancias iban arriba de cada anillo, fuera de la pantalla;
- en el centro salía un **«000° · 0 m»**: una marcación con origen y destino en
  el mismo sitio (dos clics en el mismo punto);
- el diálogo solo admitía la separación **en NM**, y el centro solo podía ser el
  objetivo seleccionado o un clic: no se podía poner un **radar u observador**
  en unas coordenadas conocidas;
- solo se podían **quitar todos** a la vez.

Cambios en la librería:
- `RangeRings::spacingMeters = 0` → **separación automática**. En cada repintado
  se mide la escala en el centro (píxeles entre el centro y un punto a 1 km al
  este; Mercator es conforme) y `MapDecor::niceRingSpacing` elige el mayor valor
  1-2-2.5-5 × 10ⁿ, en la unidad de la escala, con el que los `count` anillos
  caben en el 45 % del lado corto de la vista. Al alejar de zoom 8 a 6, unos
  anillos de 10 NM pasan a 50 NM. Valores redondos para que el rótulo se lea
  («2.5 km», no «2.37 km»).
- `RangeRings::name`: rótulo junto al centro; los centros fijos llevan además un
  cuadrado (los de un objetivo, solo la cruz bajo su icono).
- **Rótulos siempre visibles**: si lo alto de un anillo queda fuera, su distancia
  va al punto visible del anillo más cercano; la marcación de un radial que se
  sale de la vista, al borde por donde sale.
- `addBearingLine` devuelve **-1** si la marcación no tiene longitud (mismo
  objetivo o mismo punto en los dos extremos), y `updateBearingLine` false; si
  dos objetivos llegan a coincidir más tarde, la línea simplemente no se dibuja
  mientras tanto.
- Consultas: `rangeRings(id)`, `bearingLine(id)` y `rangeRingsSpacing(id)` (la
  separación con la que se dibujan ahora, útil con la automática).

Cambios en la demo:
- **Ver → Anillos de distancia…**: nombre; centro = objetivo seleccionado, un
  punto que se pinche (si cae en un objetivo, lo sigue) o **coordenadas escritas**
  en cualquier formato (decimal, GMS, UTM, MGRS); separación **automática** (por
  defecto) o fija **en m, km o NM** (al cambiar de unidad el valor se convierte);
  radiales cada 10/15/30/45/90°; color.
- **Ver → Anillos y marcaciones puestos…**: lista con cada uno (nombre, separación
  —la automática con su valor actual—, centro; o rumbo y distancia), **Centrar**
  (o doble clic), **Quitar** uno y **Quitar todos**.
- Si el destino de una marcación coincide con el origen, se vuelve a pedir en vez
  de dibujar «000° · 0 m».

Tests: `tst_mapdecor::ringSpacingIsRoundAndFits` y, en
`tst_mapwidget::rangeRingsAndBearingLinesFollowTargets`, marcaciones nulas
rechazadas, consultas por id y anillos automáticos que caben y crecen al alejar.
Comprobado con capturas a zoom 8 y 6 (radar con nombre y radiales, anillos fijos
de 37 km en una aeronave con sus rótulos llevados a la vista). **24 tests** en verde.

## 134. Menú del clic derecho según lo que haya debajo

El usuario pidió acciones en el clic derecho «en dependencia de dónde se hace»:
sobre un objetivo, las suyas; sobre otros elementos, las de cada uno.

**Librería** (la parte reutilizable, sin imponer un menú):
- `MapHit`: lo que hay bajo un pixel, **cada cosa por separado** (coordenada,
  objetivo suelto, miembros del grupo, entidad, anillos o marcación). Un objetivo
  puede estar encima de un polígono y dentro de unos anillos a la vez: la app
  decide la prioridad.
- `MapWidget::hitTest(pixel)` lo calcula (reúne `targetAt`, `targetClusterAt`,
  `featureAt` y el nuevo `RangeLayer::markAt`).
- `RangeLayer::markAt`: a menos de 6 px de una marcación, de un anillo o del
  centro (10 px), con la misma geometría que el dibujo (la separación automática
  incluida). Las marcaciones, que van encima, ganan a los anillos.
- Señal `contextMenuRequested(MapHit, globalPos)`: la emite el clic derecho
  salvo mientras se dibuja (ahí sigue cerrando el trazado). `clicked(…, Right)`
  se sigue emitiendo como antes.

**Demo** (`demo/VentanaContexto.cpp`), un apartado por cada cosa bajo el cursor
y, al final, las del punto del mapa (en un submenú si ya hay otras):
- **Objetivo**: seleccionar (ver su traza), centrar, anillos en él (rápidos o con
  diálogo), marcación desde él, y con otro seleccionado marcación hasta él y
  **CPA/TCPA**; mostrar solo los de su tipo / quitar el filtro; exportar su
  historial a GPX; copiar su posición; quitarlo.
- **Grupo de objetivos**: ampliar el grupo o ir a uno de ellos (hasta 25).
- **Anillos**: separación automática sí/no (al fijarla se queda la que se ve),
  radiales, quitarlos; **marcación**: quitarla; y la lista de todos.
- **Entidad**: seleccionar y ver propiedades, encuadrar, editar vértices (si la
  capa se edita), medir (superficie y perímetro, o longitud), vigilar como zona
  (polígonos), anillos en un punto (radar u observador, con su nombre), borrar.
- **Mapa**: cabecera con la coordenada y la cota (si hay DEM); copiar
  coordenadas en decimal, GMS, UTM o MGRS; centrar; anillos aquí (rápidos o con
  el diálogo ya con estas coordenadas); marcación desde aquí o del seleccionado
  hasta aquí; nuevo punto aquí; fijar el punto A o B de Elevación.

La cabecera de cada apartado es una acción desactivada en negrita, no
`addSection`, que en el estilo de Windows no pinta el texto. Del rótulo de un
objetivo se usa la primera línea («UAV 9», no «UAV 9 Bat 79%»).

Test `tst_mapwidget::hitTestAndContextMenu`: objetivo y entidad a la vez, solo
la entidad, centro y anillo (y nada entre ellos), marcación, mapa vacío, y un
clic derecho real que emite la señal con lo de debajo (el izquierdo no).
Comprobado en la demo bajo Xvfb con capturas de los cuatro menús. **24 tests**
en verde.

## 135. Límites en anillos y menú contextual con prioridad; Elevación automática

Probando el menú del clic derecho en Windows salieron varias cosas:
- se podían poner **anillos encima de anillos** sobre el mismo punto, y la cruz
  del centro tapaba el propio punto;
- con un objetivo dentro de unos anillos, el menú juntaba el objetivo, los
  anillos y el lugar: **largo y con nombres repetidos** («Anillos de distancia
  en él» y «Anillos de distancia en él…»);
- los pines A y B de Elevación **solo se veían** con la pestaña Elevación abierta
  y «Visión A→B» elegido; fijar un punto desde el menú no abría la pestaña.

**Librería:** `RangeRings::featureId` cuelga los anillos de una **entidad punto**
(el radar u observador que coloca el usuario). Los siguen al moverla (arrastre,
edición, deshacer: `RangeLayer` escucha `featureUpdated` del modelo) y dejan
de dibujarse si se borra. Con dueño (objetivo o entidad) **no se dibuja marca
de centro** y los radiales arrancan a 14 px: el símbolo del dueño queda encima.

**Demo:**
- **Un dueño, unos anillos.** `anillosDe(objetivo, entidad)` busca los que ya
  tenga; poner otros los **cambia** («Se cambian los anillos que ya tenía…»).
  El diálogo tiene un modo «Cambiar» (el centro no se toca) y ofrece como centro
  el objetivo o el punto seleccionados, un lugar pinchado o coordenadas.
  Borrar un punto u objetivo desde el menú quita sus anillos.
- **Prioridad en el menú:** se muestra UN elemento, el primero de objetivo →
  grupo → entidad (los puntos del usuario) → anillos sueltos o marcación; los
  anillos de un objetivo o punto van **dentro de su menú** (submenú «Anillos de
  distancia»: poner con los últimos ajustes / eligiendo las opciones, o, si ya
  tiene, separación automática, radiales, cambiar, quitar). Después, en un
  submenú, «Este lugar del mapa». Sin nombres repetidos.
- Sobre el centro de unos anillos el menú del lugar **no ofrece poner otros**.
- **Elevación desde el menú** (de un lugar, un punto o un objetivo, si hay DEM):
  «Perfil desde aquí», «Visión desde A hasta aquí», «Viewshed desde aquí»,
  «10 picos más altos alrededor», y fijar A o B. Cada una **abre la pestaña
  Elevación** con su análisis elegido (fijar B pasa a «Visión A→B», que es la
  única que lo usa, y así se ven los dos pines) y, las de análisis, lo calculan.
- Los pines A/B tienen su propio menú (ver la pestaña, análisis desde ahí), no el
  de una entidad cualquiera (sin anillos ni borrar).

Tests: en `rangeRingsAndBearingLinesFollowTargets`, anillos en una entidad
punto que la siguen y que no se dibujan al borrarla. Comprobado bajo Xvfb que
poner dos veces anillos al mismo punto deja uno. **24 tests** en verde.

## 136. Un punto en el centro de unos anillos se queda con ellos

Caso real: clic derecho en el mapa → anillos aquí; luego, en ese mismo lugar,
«Nuevo punto aquí». El punto nacía **aparte** y su menú volvía a ofrecer
«Anillos de distancia», como si no los tuviera. Lo natural es que el punto
(un radar, un observador) sea el **dueño** de esos anillos y sean un solo
elemento.

- `anillosSueltosEn(p)`: anillos **sin dueño** con su centro a menos de 12 px.
  `nuevoPunto(p)` (menú del lugar, y «Poner un punto en su centro…» del menú
  de unos anillos sueltos) coloca el punto **exactamente** en ese centro, le
  propone el nombre de los anillos y los liga a él (`featureId`); el rótulo
  pasa al punto (se le activa) y los anillos dejan de rotular su nombre y de
  dibujar la cruz. A partir de ahí su menú es el del punto, con los anillos
  dentro, y no ofrece ponerle otros.
- Al revés: anillos puestos **pinchando** o con **coordenadas** sobre un punto
  existente (`puntoEn(p)`, sin contar los pines A/B) también se ligan a él en
  vez de quedar sueltos encima.

Comprobado bajo Xvfb: unos anillos sueltos y un punto nuevo a 3 px de su centro
→ el punto queda en el centro (distancia 0), es el dueño y sigue habiendo un
solo juego de anillos. **24 tests** en verde.

## 137. Lógica de los menús: todo cuelga de un elemento

Probando en Windows: se pusieron anillos en un lugar del mapa y, justo al lado,
el punto observador de Elevación (perfil desde «aquí»): **no coincidían**. Y se
podía poner una marcación hacia un lugar vacío. Había dos conceptos
a medias —anillos «sueltos» y marcaciones «a un lugar»— que chocaban con los
puntos. Se revisa toda la lógica con una sola regla.

**Modelo (demo):**
- **Elementos:** objetivo móvil, **punto** del usuario (radar, observador…),
  zona o línea, y grupo de objetivos. Cada uno tiene su menú.
- **Anillos y marcaciones no son elementos aparte: cuelgan de uno.** Los anillos
  son de un objetivo o de un punto (como mucho unos por dueño); una marcación une
  **dos** elementos. Un punto nuevo con anillos es un solo elemento.
- **Un lugar vacío** solo ofrece: copiar coordenadas, centrar, **nuevo punto**, **nuevo
  punto con anillos** y elevación en él. Ya no hay «anillos aquí» ni «marcación
  desde aquí».
- **Un solo elemento por clic derecho**, el de más prioridad: objetivo → grupo →
  entidad (punto, zona, línea) → marca (línea de unos anillos o de una marcación).
  Clic en la línea de unos anillos abre el menú de su dueño. Las acciones del lugar
  van en «Este lugar del mapa», reducidas a copiar/centrar si el elemento es un
  objetivo o un punto (ahí no se crea otro punto).

**Librería:** `BearingLine::fromFeature` / `toFeature` (un punto del mapa en cada
extremo, que la marcación sigue); rechaza el mismo punto en los dos extremos.

**Demo:**
- `crearPunto`: capa activa si se edita o, si no, la primera editable (nunca las
  `elev_*` de resultados). Nombre por defecto «Punto N».
- **Anillos:** el diálogo ofrece objetivo seleccionado, punto seleccionado, **punto
  nuevo en unas coordenadas** o **punto nuevo donde se pinche**; el nombre solo se
  pide para el punto nuevo. Pinchar sobre un objetivo o un punto existente los hace
  suyos; sobre un lugar vacío, crea el punto. Poner otros anillos a un dueño que ya
  los tiene **los cambia**.
- **Marcación** (menú Ver o submenú «Marcación» de un objetivo/punto): origen y
  destino deben ser un objetivo o un punto. Un clic en un lugar vacío, en el mismo
  elemento que el origen o repetir una marcación idéntica se avisa y se rechaza; la
  herramienta sigue armada. En el submenú: «Hasta un objetivo o punto (pinchar)…»,
  hasta el objetivo o el punto seleccionados, y quitar las que ya tiene.
- **Esc cancela** la captura de punto (anillos, marcación, A, B); antes la
  herramienta de pinchar no tenía más salida que elegir «Navegar».
- **Borrar** un punto (o quitar un objetivo) se lleva sus anillos y marcaciones.
- **Elevación:** el punto es el observador. Desde su menú, «Usar como observador
  (punto A)», «Usar como punto B», «Perfil / Viewshed / 10 picos desde él» y
  «Visión desde A hasta él», con sus coordenadas exactas. Desde un lugar, las
  mismas, que **se enganchan a un punto del usuario a menos de ~12 px** (también el
  botón «Mapa» de la pestaña), para que análisis y anillos partan del mismo sitio.
- Los pines A/B y los resultados de Elevación tienen un menú propio corto.
- El gestor «Anillos y marcaciones puestos» nombra los extremos
  («Marcación Radar → Observador · 077° · 59.2 km»).

Test `rangeRingsAndBearingLinesFollowTargets`: marcación entre un punto y un
objetivo que sigue al punto al moverlo, rechazo del mismo punto en los dos extremos
y que no se mide si el punto se borra. Comprobado bajo Xvfb: un punto con anillos
en un lugar queda en su centro (0 m); repetir junto a él no duplica nada;
marcaciones nulas y a lugares vacíos rechazadas; borrar un punto quita sus 3 marcas.
**24 tests** en verde.

## 138. Marcación: origen = elemento, destino = cualquier lugar; limpiar el análisis anterior

Dos fallos al probar §137 en Windows:

**1. La recta de visibilidad se quedaba pintada.** Con «Visión A→B» ya calculada,
al cambiar el selector a «Viewshed» la línea de visión (con su «Obstáculo a… m»),
el texto «BLOQUEADA…» y la ventana del perfil seguían ahí. Causa: cada análisis
solo prepara **su** capa (`elev_vision`, `elev_viewshed`, `elev_picos`) y nadie
quitaba las de los demás. Ahora `quitarResultadosAnalisis()` limpia las tres
capas, el texto de resultado, los picos y esconde el perfil, y se llama al
**cambiar de análisis** (y desde «Limpiar»). Los análisis del menú contextual
cambian el selector antes de calcular, así que parten de una pestaña limpia.

**2. La marcación no dejaba elegir un lugar del mapa.** En §137 se exigió que
**los dos** extremos fueran elementos; lo pedido era que **para poder poner una
marcación debe existir algo** de lo que partir. Regla corregida:
- el **origen** es un objetivo o un punto (no se parte de un lugar vacío);
- el **destino** es otro elemento (al que la línea sigue) **o cualquier lugar**
  del mapa (coordenada fija).
En la demo: «Marcación ▸ Hasta un objetivo, punto o lugar (pinchar)…» desde un
elemento; desde el menú de un lugar vacío, «Marcación desde <objetivo o punto
seleccionado> hasta aquí» (desactivada, con el motivo, si no hay nada
seleccionado); y el menú Ver → Marcación. Se siguen rechazando un origen vacío,
un destino que es el propio origen y una marcación idéntica a otra. El gestor
nombra el destino («Radar → 23.29, -82.37 · 043° · 41.4 km»).

Comprobado bajo Xvfb: una capa `elev_vision` y su texto desaparecen al cambiar el
selector a Viewshed; marcaciones de un punto a dos lugares vacíos (rumbo y distancia
correctos), la repetida rechazada, el origen vacío rechazado. **24 tests** en verde.

## 139. Ratón y teclado: revisión completa y tarjeta del clic izquierdo

Tras listar todos los gestos del ratón y probarlos con eventos reales (sonda bajo
Xvfb) salieron 15 fallos. Se corrigen en la librería (con 10 tests nuevos) y en la
demo; además, el clic izquierdo sobre un elemento muestra ahora una **tarjeta**.

**Fallos corregidos (librería, `MapView`)**
- *Rueda fina ignorada*: los toques de 15° o menos no hacían nada; ahora se
  acumulan (`m_wheelAccum`) hasta completar un paso.
- *Pulsar sobre un objetivo se tragaba el desplazamiento*: la pulsación ya no
  resuelve nada; el clic se decide al **soltar** (`Pend`) con un margen de 4 px
  (`kClickSlopPx`) y el arrastre empieza al superarlo. Solo las etiquetas y las
  entidades `draggable` se agarran al pulsar.
- *Doble clic*: Qt entrega el doble clic EN LUGAR de la segunda pulsación; se
  trata como pulsación (`m_dblPress`) y, en un lugar vacío, acerca un nivel.
- *Objetivo imposible de deseleccionar* y *entidades no seleccionables en
  Navegar*: el clic en vacío deselecciona; el clic sobre una entidad la selecciona.
- *`clicked` tras un arrastre*: ya no se emite si hubo desplazamiento.
- *Edit*: el temblor de 1–2 px movía un vértice ~800 m; ahora hay margen. Ctrl+clic
  añade vértice; las capas con `LayerInfo::editable == false` no se editan, dibujan
  ni borran (señal `interactionBlocked` con el motivo).
- *Botón central* desplaza con cualquier herramienta; teclado: flechas, +/−, Esc,
  Intro, Retroceso, Supr y la tecla de menú.
- *Clic derecho con una medida a medias* abría el menú: ahora cancela
  (`pickCancelled`).
- *Rendimiento*: `featureAt` costaba 30 ms con 6000 entidades porque
  `OverlayModel::features()` copiaba y ordenaba todo en cada llamada. Un índice
  por capa (`forEachInLayer`) lo deja en 12 ms; la demo, además, espera a que el
  ratón pare antes de buscar bajo el cursor (cursor de mano / de arrastre).

**API nueva**: señales `elementClicked(MapHit, QPoint)`, `pickCancelled()`,
`interactionBlocked(QString)`; `MapHit::vertexFeature/vertexIndex` (tirador bajo
el cursor).

**Demo**
- *Tarjeta* (`demo/VentanaTarjeta.cpp`): clic izquierdo sobre un objetivo, punto,
  zona, línea o marca → popup con sus datos (posición, rumbo, velocidad en nudos,
  cota, superficie, anillos, marcaciones…) y botones rápidos (Centrar, Anillos o
  Quitar anillos, Marcación…, Encuadrar, Vigilar zona) más «Más…», que abre el
  menú completo del clic derecho. Los botones cierran el popup antes de actuar
  porque varias acciones abren diálogos o esperan un clic.
- Los puntos del usuario se arrastran y sus anillos los siguen; el menú de un
  tirador ofrece «Borrar este vértice» (zonas ≥ 3, líneas ≥ 2).
- Ayuda → «Ratón y teclado…»: tabla de todos los gestos.

Pendiente conocido: deshacer el borrado de un punto no restaura sus anillos.
**24 tests** en verde.

## 140. Marcaciones a un lugar libre: nombre del extremo

En «Marcación ▸ Quitar la marcación con …» las marcaciones cuyo extremo es un
lugar libre (sin objetivo ni punto) salían como «punto -1», todas iguales. Ahora
`nombreExtremo` devuelve «un lugar del mapa» para un extremo libre y el submenú
muestra sus coordenadas con rumbo y distancia («23.29, -82.37 (043° · 41.4 km)»),
de modo que dos marcaciones a lugares distintos se distinguen. 24 tests en verde.

## 141. El pin A solo se ve cuando se usa la elevación

El pin azul del punto A de Elevación estaba siempre sobre el mapa, aunque no se
estuviera analizando nada. Ahora `marcarPuntosActivos` solo lo dibuja (y el B en
Visión) si `elevacionEnUso()`: la pestaña Elevación está a la vista, o hay un
análisis calculado en el mapa (`m_pinsAnalisis`, que se apaga al limpiar o
cambiar de análisis). Se redibuja al cambiar de pestaña y al mostrar u ocultar el
panel. Las acciones del menú de Elevación abren la pestaña, así que los pines
aparecen al usarlas. 24 tests en verde.

## 142. Cerrar el perfil libera el pin A

Con un análisis de Perfil, el pin A se quedaba en el mapa al cerrar la ventana del
perfil y pasar a la pestaña Capas, hasta que se hacía algo en Elevación: el flag
`m_pinsAnalisis` seguía activo y nada lo apagaba. El perfil solo deja su ventana
(no hay capa en el mapa), así que al cerrarla (botón, Esc o la X; ahora el botón
usa `reject` para que todo emita `finished`) el análisis termina, el flag se apaga
y los pines se redibujan según la pestaña. Con Visión A→B, que deja su línea en el
mapa, los pines se mantienen. 24 tests en verde.

## 143. Entradas estándar: GeoTIFF como elevación y MBTiles sin sondar

Pedido: aceptar MBTiles y GeoTIFF como entrada **sin que cambie nada de lo que ya
funciona**. Cada formato se enchufó en el punto de la arquitectura que ya estaba
pensado para ello.

**MBTiles (ya se leían: faltaba el atajo).** Antes de tocar nada se comprobó qué
había: `RMapsTileSource` ya lee cualquier tabla cuyas columnas y esquema describa un
`TileDataset`, y `TileDatasetProbe`/`probe_db` ya detectaban los MBTiles (§ anterior
del README). Lo que faltaba era usarlos **sin pasar por la sonda**. Ahora
`TileDataset::fromJson` parte de la disposición MBTiles (`zoom_level`,
`tile_column`, `tile_row`, `tile_data`, TMS, sin `s`) cuando el `filePath` acaba en
`.mbtiles` o hay `"format":"mbtiles"`, y `MBTiles::describe` (nuevo
`src/tiles/MBTiles.{h,cpp}`) completa del propio fichero lo que el JSON no diga:
zooms, lado de tesela (decodificando una tesela real), nombre y esquema. Se llama
al cargar `datasets.json` (`TileService::loadDatasets`) y un paquete de datos
(`DataPackage::load`). Lo escrito a mano manda sobre lo detectado; los vectoriales
(`pbf`, o teselas gzip aunque el metadato diga png) se rechazan con un mensaje claro.
Para las bases RMaps los valores por defecto de `fromJson` son los de siempre
(un test lo fija), y un bloque generado por `probe_db` se carga idéntico (otro test
compara campo a campo). Se intentó primero una rama de `probe_db` para `.mbtiles`;
se **revirtió** porque sustituía su informe y su prueba de humo, que ya hacían bien
ese trabajo.

**GeoTIFF como elevación.** Hay una interfaz (`IElevationSource`) y el resto de la
librería (perfil, visibilidad, viewshed, curvas, relieve) solo pide `elevationAt`,
así que bastaba una fuente nueva:
- `GeoTiffImage` (`src/dem/GeoTiff.{h,cpp}`): lector de TIFF **sin GDAL**. TIFF
  clásico y BigTIFF, orden de bytes cualquiera, franjas o teselas, planos juntos o
  separados, compresión ninguna/LZW/Deflate/PackBits con predictores 2 y 3, enteros
  de 8/16/32 bits y coma flotante de 32/64. Georreferencia por escala+enlace o
  `ModelTransformation` sin rotación, PixelIsArea/PixelIsPoint, grados (cualquier
  datum) o Web Mercator, y la etiqueta `GDAL_NODATA`. Cualquier otro CRS se rechaza
  diciendo cuál es. Lee **bajo demanda** los bloques que se consultan (caché con cerrojo, ver §144), así que un DEM de varios GB no ocupa memoria. Deflate usa
  `qUncompress` (Qt ya trae zlib): sin dependencias nuevas.
- `GeoTiffElevation`: un fichero o una carpeta de ellos, con índice por celdas de 1°
  y atajo a la última imagen que respondió. Bilineal; NaN fuera o junto a un hueco.
- `openElevationSource(ruta)` (`ElevationSources.h`): elige por la ruta —`.tif` o
  carpeta con `.tif` y sin `.hgt` → GeoTIFF; carpeta con `.hgt` → `HgtElevation`;
  otro fichero → `SqliteElevation`— y es lo que usan ahora `MapWidget`
  (configuración y `setElevationDb/Dir`) y `PackageCheck`. Los campos
  `elevationDbFile`/`elevationDir` y `"elevation"` de un paquete aceptan, pues,
  cualquier formato; `DataPackage::files()` incluye también los `.tif`. La demo
  deja elegir `.tif` en sus diálogos de elevación.

**Cómo se probó.** `tst_geotiff` (39 casos) fabrica GeoTIFF con todas las
variantes (21 combinaciones de tipo, compresión, predictor, teselas, BigTIFF y
orden de bytes; bandas; PixelIsPoint; Web Mercator; sin dato; rechazos; imagen de
1500×1100; carpeta de zonas; fábrica) y, además, lee **seis ficheros reales de
`tests/data/geotiff` escritos por tifffile+imagecodecs** (LZW, Deflate,
predictores 2 y 3, BigTIFF, big-endian): así el lector no solo coincide con el
escritor de los tests —que también es mío—, sino con lo que produce otra
implementación. `tst_mbtiles` (9 casos) prueba el volteo TMS, `datasets.json`,
paquetes, rechazos y compatibilidad. En `tst_mapwidget`, un MBTiles se dibuja de
extremo a extremo (el color del centro es el de la tesela correcta) y un `.tif`
alimenta `elevationAt` y `elevationProfile`, por configuración y en caliente.
`probe_db` + `check_data` sobre un MBTiles real siguen dando su informe.
**26 tests** en verde, sin avisos.

**Límites conocidos.** Una sola banda y metros como unidad vertical; sin rotación;
sin JPEG/WebP dentro del TIFF; las ortofotos GeoTIFF **no** se dibujan como capa base
(haría falta reproyectarlas a teselas: queda como herramienta futura, el lector ya
lee varias bandas); un `typicalFill` de 1.0 por defecto en MBTiles con huecos grandes
se ajusta a mano en el JSON.

## 144. Una sola caché de bloques para todos los GeoTIFF

Al probar con una carpeta real de Cuba (42 cuadros de Copernicus de 3600x3600) se vio
un fallo de diseño propio del §143: cada `GeoTiffImage` tenía SU presupuesto de ~96
MiB, así que un análisis grande (un viewshed que toca decenas de cuadros) podía
llegar a ocupar varios GB. Ahora la caché es **una para todo el proceso**
(`SharedCache`, 256 MiB por defecto, `GeoTiffImage::setCacheLimit`), con un solo
cerrojo y expulsión por antigüedad; al cerrar una imagen se liberan sus bloques.
Un test (`sharedCacheStaysBounded`) fuerza un presupuesto de unos pocos bloques, y
otro a cero, con dos imágenes a la vez: las lecturas siguen saliendo exactas aunque
se descodifiquen una y otra vez. De paso, un aviso de `-Wshadow` y una comprobación
vacía en un test. 26 tests en verde.

Para el usuario: los cuadros de Copernicus 30 m miden 25-50 MB comprimidos y un
bloque descodificado de 1024x1024 ocupa 4 MiB, así que 256 MiB son ~64 bloques en
memoria a la vez, con independencia de cuántos ficheros haya en la carpeta.

## 145. La demo dice qué elevación cargó (y por qué no)

El usuario descargó 42 cuadros GeoTIFF de Copernicus y dijo que la demo «solo lee
.geo». Había dos causas, una mía:
- Las entradas para cargar elevación solo estaban en el botón **«DEM…»** de la
  barra; el menú **Datos** solo tenía «Cargar .geo…». Se le había indicado un menú
  que no las tenía. Ahora están también en **Datos → Cargar elevación (DEM)** y al
  principio del menú **Elevación** (las mismas dos `QAction`, compartidas con el
  botón): «Carpeta de GeoTIFF o .hgt…» y «Fichero GeoTIFF o base de datos…».
- La demo decía «Elevación: <ruta>» aunque no hubiera cargado nada, y los errores
  solo iban al registro de Qt Creator. Nuevo API: `MapWidget::elevationDescription()`
  (p. ej. «42 GeoTIFF, lat 19.0..24.0, lon -85.0..-74.0», «12 ficheros .hgt», «base
  de datos x.sqlitedb», vacía si no hay nada utilizable) y `elevationWarnings()`
  (ficheros ilegibles, carpeta sin `.hgt` ni `.tif`, ruta inexistente). Una carpeta
  sin datos ya no cuenta como DEM. La demo muestra la descripción en la barra de
  estado y, si algo falló, un aviso con el motivo; `m_demActivo` sale de lo que se
  cargó de verdad. `openElevationSource` devuelve ahora los avisos también cuando
  solo fallan algunos ficheros.

Probado en la demo real (Xvfb) con una carpeta de dos cuadros reales de Copernicus:
«2 GeoTIFF, lat 20.0..21.0, lon -78.0..-76.0», sin avisos, 70 m en tierra y 0 m en el
mar; y el menú Datos con la entrada nueva. `tst_mapwidget` cubre descripción,
fichero roto (aviso con su nombre, el bueno sigue funcionando) y carpeta vacía.
26 tests en verde.

## 146. `dem_to_hillshade` también lee GeoTIFF (Copernicus)

El usuario tiene los cuadros de Copernicus DEM en GeoTIFF y quería una segunda capa de
relieve sombreado (gris) junto a `cuba_relieve` en colores. La herramienta solo
abría carpetas `.hgt` o `.sqlitedb`. Ahora abre el origen con `openElevationSource`
(la misma fábrica que usan el mapa y la demo), así que `--in` acepta también una
carpeta o un fichero GeoTIFF, y muestra los avisos de ficheros ilegibles.

Probado con dos cuadros reales de Copernicus (N20/N21, W078/W077): bbox
21,-78 .. 20,-76, z8..10, `--sea-level 0` -> 33 teselas en 6 s, relieve nítido y el mar
transparente (en Copernicus el mar vale 0). Uso:
`dem_to_hillshade --in C:\...\dem_cuba --cuba --out relieve_cop.sqlitedb --sea-level 0
--id relieve_cop --name "Relieve Copernicus"`.

Se descartó la rama de ortofotos (herramienta `ortho_to_tiles`): por ahora no aporta
nada al usuario. Queda guardada en la rama local `backup-ortofotos` / PR #11.

## 147. `geotiff_check`: detectar GeoTIFF cortados o dañados

El relieve de Copernicus del usuario salió con un hueco rectangular al este de la
isla, aunque estaban los 42 cuadros. La lista de ficheros cubría toda la tierra de
Cuba (los que faltan son mar abierto, que Copernicus casi no publica), así que no era
un cuadro ausente. Los bordes rectos (~30 km de alto, 70–96 km de ancho) encajan con
bloques internos de 1024×1024 píxeles: un bloque cuyo dato falta o no se puede
descomprimir se lee como «sin dato» y no da ningún aviso. Se sospecha una descarga
cortada en `N20 W075`/`N19 W075`, pero no se pudo confirmar sin los ficheros.

- `GeoTiffImage::verifyBlocks()` recorre los bloques de la primera banda sin pasar por
  la caché y cuenta los vacíos (tamaño 0, legítimos), los que caen más allá del final
  del fichero y los que no se descomprimen, con la posición de los primeros.
- Herramienta **`geotiff_check <fichero|carpeta>…`**: una línea por fichero (`ok`/`MAL`,
  tamaño, extensión, nº de bloques) y los bloques con problema. Código de salida 1 si
  alguno falla. Probada con los cuadros reales y con uno truncado a propósito.
- Test `verifyBlocksFindsDamage` en `tst_geotiff` (sano y cortado). 26 tests en verde.

## 148. Búsqueda de lugares sin conexión

Tras revisar qué le falta a la librería, la mejora que más aportaba era poder ir a un
sitio escribiendo su nombre (hasta ahora, solo coordenadas o arrastrar el mapa).

- **Datos:** `herramientas/osm_lugares_a_sqlitedb.py` convierte un export de Overpass
  (la consulta va en su cabecera: ciudades, pueblos, aldeas, islas, aeropuertos,
  puertos, bahías, cabos, picos…) en `lugares.sqlitedb`:
  `lugar(id, nombre, busqueda, tipo, lat, lon, poblacion, peso)`. `busqueda` es el
  nombre sin tildes ni mayúsculas; `peso` ordena (ciudad > aldea, más la magnitud de la
  población). Los elementos que son superficie (un aeródromo mapeado como polígono) se
  reducen al centro de su caja y los duplicados (nodo + polígono) se funden.
- **Librería:** `PlaceIndex` (`src/io`) busca con SQL indexado por el pool de
  conexiones (lectura, una por hilo): todas las palabras deben aparecer, los nombres que
  EMPIEZAN por lo escrito van antes, luego el peso. `%` y `_` se escapan (son texto,
  no comodines). `MapWidget::setPlacesDb/hasPlaces/searchPlaces/goToPlace`,
  `MapConfig::placesDbFile` y `"places"` en `mapa.json` (también en `files()` y en
  `check_data`). `Place::typeLabel()` da el tipo en español y `suggestedZoom()` el zoom
  de encuadre. El id de conexión es un contador, no la dirección del objeto: una
  instancia nueva en la misma dirección heredaría la conexión de otra BD.
- **Demo:** campo «Buscar lugar…» en la barra principal (Ctrl+K), con desplegable
  «nombre — tipo» que no filtra por prefijo ("guantanamo" sugiere "Guantánamo"); Intro o
  elegir lleva al lugar. Sin índice se ve desactivado y dice cómo añadirlo.
- **Corrección menor en la demo:** el «29 %%» de la barra de estado era un `%%` de
  `printf` en un `QString::arg`, que no lo interpreta; ahora un solo `%`.
- **Tests:** `tst_places` (nuevo, 9 casos: normalización idéntica a la del script fila
  a fila, tildes, orden, todas las palabras, comodines literales, límites, BD
  inutilizables, etiquetas y el bloque `places` del manifiesto) y un caso en
  `tst_mapwidget`. Fixture `tests/data/places/` (12 lugares inventados con tildes y
  casos límite), generado con el propio script. 27 tests en verde (24 sin QCustomPlot).
- **Pendiente del usuario:** bajar el export de Overpass en su PC (el contenedor no
  llega a Overpass) y ejecutar el script; no se probó con datos reales de Cuba.

## 149. Añadir una capa base con el mapa abierto

Para ver su relieve de Copernicus (`relieve_cop.sqlitedb`) el usuario tuvo que editar
`datasets.json` a mano, aunque la librería ya sabía leerlo. Ahora se añade desde la
propia aplicación.

- **Servicio:** `TileService::addDataset` valida el dataset (válido, el fichero existe,
  id libre) y avisa al hilo trabajador con `TileLoader::addDataset`. Hace falta un slot
  nuevo porque `configure()` VACÍA las fuentes ya abiertas: reutilizarlo habría cerrado
  las capas en uso. La caché de la capa nueva se crea al usarla, como las demás.
- **API:** `MapWidget::addBaseLayer(fichero, &error, id)`. Un `.mbtiles` se describe
  con su `metadata` (`MBTiles::fromFile`); cualquier otra base de teselas se sondea como
  `probe_db` (`TileDatasetProbe`), usando la zona del paquete para decidir XYZ/TMS. El
  id sale del nombre del fichero (minúsculas, cifras y `_`) y se hace único con un
  sufijo (`capa_2`). No activa la capa: se hace con `setBaseLayerId`. Errores con
  motivo (no existe, no es una base de teselas).
- **Demo:** Datos → «Añadir capa base (teselas)…». Añade la capa al desplegable de la
  barra, al menú Ver → Capa base y al árbol de capas, y la activa. El menú de capas
  base pasó a poder crecer (`registrarCapaBase`).
- **Test:** `addsBaseLayersAtRuntime` en `tst_mapwidget` (MBTiles y base normal, id
  único y a elegir, activar y dibujar, ficheros inexistentes o basura). Detectó que
  el primer borrador dependía de otro test para existir su fichero: ahora crea el suyo.
  27 tests en verde (24 sin QCustomPlot).
- **Sin probar a mano:** el diálogo de abrir fichero de la demo (no hay forma de teclear
  en el contenedor); sí está probado todo lo que hay detrás.

## 150. Contrato de instalación: los ejemplos usan la librería instalada

Antes de desarrollar varias aplicaciones de ejemplo, se asegura cómo la usarán: se instala
una vez y cada app la encuentra con `find_package`, sin tocar las fuentes.

- **Comprobado a mano** (Linux, Qt 6.4): compilar, `cmake --install` a un prefijo, y
  compilar `app_minima` y `app_seguimiento` contra él. Copiando el prefijo a otra ruta
  los ejemplos siguen enlazando (el `Config` no guarda rutas absolutas). 27 tests en verde.
- **`herramientas/probar_instalacion.sh <prefijo>`**: mira que el prefijo tenga el
  `Config`, las bibliotecas, las cabeceras y `check_data`; fabrica un paquete de datos
  mínimo (una pirámide de teselas de 1×1 px, sin red, con `python3`); compila TODOS los
  ejemplos contra el prefijo; y arranca `app_minima --comprobar` y `check_data`. La
  atribución del paquete es obligatoria: sin ella `--comprobar` sale con 1 (lo cazó la
  primera ejecución).
- **`examples/CMakeLists.txt`** y **`examples/examples.pro`**: compilan todos los ejemplos
  de una vez. Es un proyecto aparte, no un `add_subdirectory` de la raíz: cada ejemplo
  hace su propio `find_package`, así que compilarlos prueba la instalación de verdad. Un
  ejemplo nuevo se apunta en ambos ficheros.
- **CI:** tras los tests, instala en un prefijo y ejecuta el script (Qt 5 y Qt 6).
- **Docs:** apartado 3.3 bis de `DESPLIEGUE.md` y README.
- **Sin probar:** el camino qmake y los `.bat` (no hay qmake ni Windows en el contenedor).

## 151. Hoja de rutas del PC de desarrollo

Para no depender de la memoria al instalar y desplegar, `docs/MIS_RUTAS.md` recoge los
comandos con las rutas reales de este PC (Qt 6.11.2 en `C:\Qt`, libmapa en
`C:\libmapa\qt6`, paquete en `D:\QtPro\Recursos`): ponerse en la rama, reinstalar la
librería desde `build-qt6`, comprobar el paquete con `check_data`, compilar `examples.pro`,
desplegar con `desplegar.bat` y probar con `--comprobar`. Solo documentación; las rutas
salen de un listado que pegó el usuario, no se han ejecutado aquí (no hay Windows).

## 152. Los rótulos van encima de todo, y el de la marcación se mueve

Con `app_seguimiento` en el PC se vio que los textos flotantes quedaban *debajo*: el
nombre de una zona («Zona restringida») y el del CPA los pintaba `FeatureLayer` en su capa,
por debajo de los anillos, de las marcaciones y de los objetivos, así que cualquier línea los
tapaba; el de la marcación («080° · 2.51 NM»), además, no se podía mover y caía justo sobre el
nombre del buque.

- **`LabelLayer`** (nueva, `src/widget/`): un `QCPLayerable` en la misma capa *buffered*
  «targets» pero creado el último, así que queda por encima de todo y se repinta con los
  objetivos. No guarda estado: pide `FeatureLayer::drawLabels` (nombre de entidades y alertas
  CPA, que son rótulos de entidad) y `RangeLayer::drawLabels` (distancias de anillos, nombre
  del centro y rótulo de marcación). `FeatureLayer::drawFeature` ya no pinta la etiqueta; el
  hit-test (`labelAt`) no cambia porque la caja se calcula igual.
- **Rótulo de marcación movible**: `BearingLine::labelOffsetPx`, que se cambia arrastrando
  el rótulo con el ratón (como los de entidad y objetivo) o con
  `MapWidget::setBearingLabelOffset`; `bearingLabelAt` lo localiza por la caja de la última
  pasada de dibujo. Con desplazamiento, una línea guía del color de la medida lo une a la
  línea. En un clic gana el rótulo de marcación (se pinta el último), luego el de entidad y
  el de objetivo; el cursor «mover» también aparece sobre él.
- **Error de colocación corregido**: el rótulo de una marcación se ponía en
  `linea.at(size/2)`, y una marcación de menos de 40 km solo tiene los dos extremos, así que
  caía en el DESTINO (justo encima del símbolo y del nombre del objetivo). Ahora va en la
  mitad de la longitud de la línea.
- **Tests:** `featureLabelsDrawAboveRangeMarks` (una marcación cruza el rótulo: con el
  arreglo no hay píxeles azules sobre la caja; con el dibujo antiguo salían 6, comprobado) y
  `bearingLabelMovesWithMouse` (localizar, desplazar, arrastrar con el ratón sin mover el
  mapa). 27 tests en verde (24 sin QCustomPlot).
- **Sin probar a mano:** el aspecto en pantalla con la `app_seguimiento` real (el
  contenedor no tiene pantalla): conviene mirar que los rótulos arrastrados se leen bien.
  Los rótulos de **anillos** (distancias) pasan a la capa de arriba pero no se mueven: van
  pegados a su anillo a propósito.

## 153. Primera aplicación de la serie de ejemplos: `visor_gpx`

Con el contrato de instalación asegurado (sección 150) empieza la serie de aplicaciones
de ejemplo que sirven de base a productos futuros. Cada una vive en `examples/<nombre>/`,
usa la librería **instalada** (`find_package` / `libmapa.pri`), cabe en un fichero de menos
de ~400 líneas y trae su README, su `.pro` y un modo `--comprobar` sin ventana.

- **`examples/visor_gpx`**: abre GPX, KML o GeoJSON como capa y la encuadra
  (`importVectorFile`, `fitBounds`), marca inicio y fin de las rutas
  (`setRouteEndpoints`), al pinchar una ruta da su longitud y dibuja su perfil de elevación
  con ascenso y descenso (`elevationProfile`, solo si el paquete trae elevación), reproduce
  un GPX con horas con play, pausa, deslizador y velocidad (`TrackPlayer`) y exporta la vista a
  PDF o imagen (`exportView`). `ruta_ejemplo.gpx` son datos sintéticos (una ruta y un track
  con horas). Opciones `--comprobar` (sale con 0 si lee algo) y `--captura=imagen.png`.
- **`examples/CMakeLists.txt` y `examples.pro`** lo incluyen, y
  `herramientas/probar_instalacion.sh` lo arranca con `--comprobar` sobre su GPX (CI, Qt 5 y 6).
- **Verificado** en el contenedor: compila con `-Wall -Wextra` sin avisos contra una instalación
  nueva; `--comprobar` lee 2 entidades, 8.97 km y horas; sin fichero, sale con 1; la ventana
  se ve bien en una captura. **Sin probar**: pinchar una ruta con elevación real, la
  reproducción interactiva y la exportación (no hay pantalla ni datos de elevación aquí) y el
  camino qmake.
- **Siguientes del plan**: `buscador_lugares`, `medidor_campo`, `monitor_trafico`,
  `vigilancia_zonas`, `analisis_terreno`, `editor_capas`, `cliente_telemetria`.
- **Corrección tras probarlo en el PC (misma sección 153):** el perfil no salía y la
  gráfica decía lo mismo sin ruta seleccionada que con una ruta sin datos de elevación, así que
  no se sabía por qué. Ahora el mensaje distingue los casos (ninguna ruta seleccionada, la
  entidad no es una línea, o «sin datos de elevación: el paquete no trae `elevation` o no cubre
  la ruta»), y al abrir un fichero se selecciona sola la primera ruta para que el perfil salga de
  entrada. `--comprobar` imprime también el perfil de la primera ruta (ascenso, descenso y
  cotas). Probado con un paquete de prueba con un `.hgt` sintético de La Habana
  (ascenso 57 m, cotas 318–375 m) y sin elevación (lo dice).
- **Otra corrección tras probarlo con el paquete de Cuba (misma sección 153):** tras pinchar el
  mapa ya no se podía volver a seleccionar la ruta. Causa: las capas `.sqlitedb` del paquete
  (`provincias`, `curvas`) conservan entidades seleccionables a propósito (para leer la cota), y
  la capa de rutas, con el orden de dibujo por defecto, quedaba por DEBAJO de ellas; un clic dentro
  de una provincia la seleccionaba a ella (un polígono acierta en todo su interior) y la ruta
  perdía la selección. Ahora la capa del fichero se crea con orden 1000 (encima de todo el paquete) y
  el visor solo atiende a sus propias entidades: pinchar una provincia, unas curvas o el fondo no quita
  la ruta seleccionada, así que su perfil no desaparece. Reproducido FUERA del repositorio con un
  paquete sintético y una capa «provincias» seleccionable de orden 20 (clics simulados sobre la
  ventana): con el código anterior ningún píxel de la ruta respondía y el clic seleccionaba la
  provincia; con el nuevo se conserva la ruta y se cambia al pinchar otra. **Lección para los
  ejemplos siguientes:** una capa propia de la app que deba ser seleccionable va con un orden mayor
  que el de las capas del paquete. **Idea para más adelante:** un `"selectable": false` en las
  entradas de `overlays` de `mapa.json` para quien no quiera esas capas pinchables.
- **Distinguir rutas y no ensuciar el paquete (misma sección 153):** un GPX con una ruta
  planificada (`<rte>`, rectas entre puntos de paso) y un track grabado (`<trk>`, el recorrido con
  horas) mostraba las dos líneas iguales y se leía como una sola ruta. Ahora la planificada va en azul
  y el track en rojo (por el atributo `gpx_kind` que pone el lector). Y la capa del fichero se marca
  como temporal (`setFeatureLayerTransient`): sin eso, un paquete con `"features"` (base de entidades con
  guardado automático) guardaría ahí lo abierto en el visor.

## 154. Segunda aplicación de la serie: `editor_rutas`

Al probar `visor_gpx` salió la pregunta de si las rutas se pueden crear: el visor solo lee, a propósito.
El complemento es un ejemplo aparte, `examples/editor_rutas`, para que cada uno enseñe una cosa.

- **`examples/editor_rutas`**: dibuja una ruta clic a clic (`MapTool::DrawPolyline`, estilo con
  `setDraftStyle` sobre una capa propia activa con `setActiveFeatureLayer`), edita vértices
  (`MapTool::EditFeature`), deshace y rehace (`undo`/`redo`), pone nombre al crear, muestra la
  longitud de la seleccionada en vivo y guarda a GPX, KML o GeoJSON (`exportVectorFile`; el formato
  lo da la extensión) con aviso de cambios sin guardar (título con `*`, pregunta al cerrar o abrir).
  Con `MapConfig::autosaveFeatures = false` nada se escribe solo en la base de entidades del paquete.
  Aplica la lección de la sección 153: la capa de rutas va con orden de dibujo alto (por encima del
  paquete) para que sus provincias seleccionables no roben el clic.
- **`--comprobar`** crea una ruta por código, la guarda a GPX, la vuelve a leer en otra capa y exige
  los mismos puntos, la misma longitud (< 1 m) y el mismo nombre. Entra en
  `herramientas/probar_instalacion.sh` (CI, Qt 5 y 6), en `examples/CMakeLists.txt` y en `examples.pro`.
- **Hallazgo**: la librería escribe las líneas a GPX siempre como `<trk>` (no hay escritor de
  `<rte>`), así que una ruta planificada guardada vuelve como track. Documentado en el README del
  ejemplo; si hiciera falta, sería un cambio de la librería.
- **Verificado** en el contenedor: compila con `-Wall -Wextra` sin avisos; `--comprobar` da 3 puntos y
  2.73 km al ir y volver; con un programa de prueba FUERA del repositorio y clics simulados sobre la
  ventana: dibujar con tres clics y doble clic crea una ruta de 3 vértices, el diálogo de nombre la
  nombra, el título pasa a `*`, deshacer quita primero el nombre y luego la ruta (dos pasos, porque
  nombrar es una edición aparte), rehacer la devuelve y guardar escribe un GPX correcto y quita el
  `*`. **Sin probar**: arrastrar vértices con el ratón, el aviso al cerrar y el camino qmake.

## 155. Tercera aplicación de la serie: `buscador_lugares`

Ejemplo de la búsqueda de lugares sin conexión (sección 148) y de las coordenadas UTM/MGRS: la base
de un callejero offline o de una utilidad de coordenadas.

- **`examples/buscador_lugares`**: cuadro de búsqueda con la lista de resultados que se rellena al
  escribir (`searchPlaces`: sin tildes ni mayúsculas, ciudades antes que aldeas, todas las palabras);
  Intro o un clic van al lugar con el zoom que le toca (`goToPlace`) y lo marcan con un pin en una
  capa temporal por encima del paquete. Lo escrito también se lee como coordenada en cualquier
  formato (`parseCoordinate`: decimal, grados y minutos, UTM, MGRS) y sale como primer resultado.
  La coordenada bajo el cursor se muestra en el formato elegido (`formatCoordinate`) y un clic la copia
  al portapapeles. Ctrl+K enfoca el cuadro. Sin `"places"` en el `mapa.json` sigue sirviendo para
  coordenadas y lo avisa.
- **`--comprobar`**: busca «guantanamo» (debe encontrar «Guantánamo»), lee una coordenada MGRS y
  comprueba la ida y vuelta. **Hallazgo propio, no de la librería:** MGRS con 5 cifras es una rejilla
  de 1 m y trunca, así que formatear y releer pierde hasta √2 ≈ 1,4 m; mi primera tolerancia (1 m)
  daba un falso fallo, ahora es de 2 m.
- **`herramientas/probar_instalacion.sh`** ahora mete en su paquete de prueba el índice de lugares de
  `tests/data/places` (si está) y arranca el buscador; `examples/CMakeLists.txt` y `examples.pro` lo
  incluyen (CI, Qt 5 y 6).
- **Verificado** en el contenedor, con un programa de prueba FUERA del repositorio y eventos reales
  sobre la ventana: `guantanamo` da 3 resultados (ciudad, caserío, bahía) y Intro centra en
  20.1444, -75.2092 a zoom 12; `bahia guan` da solo la bahía (todas las palabras); una coordenada MGRS
  sale como resultado y centra donde toca; `100%` encuentra «100% Cubano» (comodín literal); `zzz` no da
  nada; un clic en el mapa copia la coordenada al portapapeles. **Sin probar**: con el índice real
  de Cuba (el del PC del usuario, `lugares.sqlitedb`), y el camino qmake.

## 156. Las capas fijas del paquete arrancan ocultas y no seleccionables

Al probar los ejemplos con el paquete de Cuba, un clic en el mapa seleccionaba una provincia
(primero en `visor_gpx`, sección 153, y el usuario vio que seguía pasando). El visor y el editor lo
esquivaron con un orden de dibujo alto, pero la causa es de la librería, y el usuario pidió dejarlo
establecido: las capas `overlays` son decoración fija, no deben verse al iniciar ni
poder elegirse con el ratón; para editarlas, en todo caso, una herramienta aparte.

- **`LayerInfo::selectable`** (nueva bandera de capa, `true` por defecto) y
  `MapWidget::setFeatureLayerSelectable`. `FeatureLayer::featureAt` (la que usa el ratón para elegir,
  arrastrar y poner el cursor) y `labelAt` saltan las capas no seleccionables.
- **Overlays del paquete: `visible` y `selectable` en falso por defecto**; en el `mapa.json` se
  activan con `"visible": true` y `"selectable": true` en la entrada del overlay (`DataPackage::Overlay`).
  Se quita el `selectable = false` por entidad que se ponía solo a los `.geo`: la bandera de capa
  cubre `.geo` y `.sqlitedb` (las curvas de nivel, con miles de entidades, no hay que recorrerlas).
- **Consulta frente a selección:** `MapWidget::featureAt` (público) sigue viendo las capas fijas aunque no
  sean seleccionables, porque la demo la usa para el tooltip de las curvas (la cota) y eso no elige nada;
  `hitTest`, que alimenta el menú contextual, solo devuelve lo que se puede elegir.
- **Demo:** quitado `ocultarOverlaysAlIniciar` (la librería ya lo hace).
- **Tests:** 3 nuevos o ampliados en `tst_mapwidget` (la capa fija sale oculta, bloqueada y no
  seleccionable; el `mapa.json` la activa y la API la cambia; un polígono de una capa no seleccionable
  no se lleva el clic ni sale en `hitTest` pero `featureAt` lo ve, y al activarla el clic lo elige) y
  una comprobación en `tst_datapackage`. 27 en verde (24 sin QCustomPlot).
- **Cambio de comportamiento:** una app que contase con ver los overlays al abrir, o con
  seleccionarlos, debe ahora pedirlo. Los ejemplos `visor_gpx`, `editor_rutas` y `buscador_lugares` siguen
  funcionando tal cual (su capa con orden 1000 ya no hace falta, pero no estorba).

## 157. Cuarta aplicación de la serie: `medidor_campo`

Medir sobre el mapa es lo que más repite cualquier app (parcelas, tramos, áreas de búsqueda), y la
librería ya lo traía (`MapTool::Measure`, `MapTool::MeasureArea`, `polygonArea`, `pathLength`,
marcaciones), pero sin un ejemplo que junte las piezas. `examples/medidor_campo/` (~545 líneas, un solo
`main.cpp`) es esa plantilla, y la primera de la serie que parte de `main` ya con todo lo anterior
fusionado (#18 a #21).

- **Distancia:** `measurementFinished` se convierte en una **marcación** (`addBearingLine`) que se queda
  dibujada con su rótulo «097° · 4286 NM», arrastrable. Dos puntos iguales no anotan nada (la librería
  devuelve -1 y la app lo dice en la barra de estado).
- **Superficie:** `areaMeasured` deja un polígono en una capa propia y temporal (`medidas`, orden 1000,
  `selectable = false`); el contorno nativo se quita con `clearAreaSelection` **fuera** de la señal
  (`QTimer::singleShot(0)`), para no tocar el estado de `MapView` mientras sigue emitiendo.
- **Lista y unidades:** las medidas se guardan en metros y metros² y las unidades solo se aplican al
  mostrarlas. Distancia: métrico o náutico (`setScaleBarUnits`, así los rótulos del mapa y la lista
  coinciden); superficie: ha, km², m², acres, y el rótulo del polígono (`A1 · 1.23 ha`) se actualiza con
  `updateFeature`. Suma de distancias y de superficies, borrar una o todas, y **Copiar tabla** con
  tabuladores. Las millas terrestres se dejaron fuera a propósito: la escala gráfica no las tiene y los
  rótulos del mapa no podrían seguirlas.
- **Portabilidad:** `QButtonGroup::buttonClicked(int)` ya no existe en Qt 6 (y `idClicked` llega con 5.15),
  así que se usa la señal con `QAbstractButton*` y `id(boton)`, igual en Qt 5.14 y 6.
- **`--comprobar`:** rectángulo de 0,1° × 0,1° frente a la fórmula esférica (113,50 km² contra 113,77:
  0,24 %, dentro del 0,5 % que se deja por usar esfera y no elipsoide), línea de 0,5° de meridiano
  (55,60 km, rumbo 0°), la marcación dibujada mide lo mismo, suma, tabla y borrado. Entra en
  `herramientas/probar_instalacion.sh` y en `examples/CMakeLists.txt` / `examples.pro` (CI, Qt 5 y 6).
- **Verificado** en el contenedor con un programa fuera del repositorio y clics reales sobre la ventana:
  dos clics con la herramienta de distancia dan una fila y una marcación; tres clics y doble clic con la de
  superficie dan una fila y un polígono; cambiar a millas náuticas reescribe la lista; borrar quita la fila y
  su marca del mapa. **Sin probar**: con el paquete real de Cuba en el PC del usuario y el camino qmake.

## 158. Quinta aplicación de la serie: `monitor_trafico`

Seguir contactos en tiempo real es el caso de uso principal de la librería, y `app_seguimiento` solo lo
enseña como un recorrido por el API (tres buques y un menú). Una app de verdad necesita además lo que se
echa en falta al intentar convertir ese ejemplo en producto: **ver qué hay en una tabla**, **saber qué se
ha dejado de oír** y **entender los términos** que muestra la pantalla. `examples/monitor_trafico/` (~870
líneas, un solo `main.cpp`) es esa plantilla. El usuario pidió expresamente un **glosario de los términos
que se utilicen**, dentro de la interfaz.

- **Tabla de contactos** (panel inferior): propio arriba y el resto ordenado por distancia, con rumbo,
  velocidad, distancia, CPA, TCPA y estado (`Activo`, `Alerta`, `Perdido`). CPA y TCPA vienen de
  `targetCpa` con la velocidad y el rumbo que da el feed (`setTargetVelocity`).
- **Selección enlazada:** elegir una fila selecciona el contacto y dibuja la marcación del propio hasta él
  (una sola, la anterior se quita); pinchar un contacto en el mapa (`targetClicked`) elige su fila.
- **Contactos perdidos:** sin partes durante 90 s (tiempo simulado) pasan a `Perdido`: se atenúan en el mapa
  con un `TargetFilter::predicate` sobre el atributo `estado` (`dim = true`), pierden el CPA y no dibujan
  marcación; a los 5 min se borran. Es lo que hay que hacer con cualquier feed real. El umbral es «más de»,
  no «al menos»: a 90 s justos sigue activo.
- **Alertas** en un registro con hora: proximidad (0,5 NM en 15 min), zona vigilada (entra y sale) y la
  pérdida y el borrado de contactos. Filtro por tipo y agrupar al alejar.
- **Glosario** (panel derecho, *Ayuda → Glosario*, F1): 20 términos con buscador por nombre o por palabra
  de la definición, y la definición de cada columna también como ayuda al pasar el ratón por su cabecera.
  **Una prueba impide que se quede atrás:** `--comprobar` falla si alguna cabecera de la tabla o algún
  término de la interfaz no tiene su definición. Quien reutilice el ejemplo y añada una columna con un
  término nuevo tiene que definirlo.
- **Feed simulado** (clase `Simulador`): ocho contactos con rumbo y velocidad fijos y un reloj simulado
  de 30 s por segundo real. Uno viene de proa contra el propio (CPA casi 0) y otro se queda sin señal. Para
  conectar un decodificador real basta llamar a `Ventana::recibir` con cada parte; el README lo explica.
- **Fallo real encontrado al mirar la captura, no por los tests:** el aviso de zona salía como
  «Zona restringida: entra» sin el nombre. Las señales de zona y de proximidad llegan *dentro* de
  `addTarget`/`updateTarget`, antes de que la aplicación haya apuntado el id del contacto recién creado, así que
  una tabla `id → nombre` rellenada tras `addTarget` aún no lo tiene. Se lee el nombre del propio objetivo del mapa
  (`target(id)->label`), y `--comprobar` verifica ahora que los avisos de zona llevan nombre. Vale también
  para cualquier app que reaccione a esas señales.
- **Prueba (`--comprobar`):** 8 contactos y glosario completo; tabla de 8 filas, propio arriba y orden por
  distancia; el frontal da CPA 0,00 NM y TCPA 14,5 min (5 NM a 10 + 10 kn = 15 min menos los 30 s ya
  pasados) y está en `Alerta`; elegir una fila selecciona y reemplaza la marcación; el filtro «aeronave»
  atenúa 6 contactos (8 menos el propio y el helicóptero); `Pesquero 2` sigue activo a 90 s, está `Perdido`
  pasado ese umbral y se borra pasados 300 s. Entra en `herramientas/probar_instalacion.sh`,
  `examples/CMakeLists.txt` y `examples.pro` (CI, Qt 5 y 6). Mis tres primeros fallos fueron errores de la
  prueba, no del ejemplo: una cuenta mal hecha (6 atenuados y no 5), un `—` comparado como Latin-1 con
  `QLatin1String` (hay que usar `QStringLiteral` con UTF-8) y el umbral de borrado, que exige «más de» 300 s.
- **Verificado** en el contenedor con un programa fuera del repositorio sobre la ventana real: clic sobre un
  contacto del mapa (en su píxel real, comprobado con `targetAt`) lo selecciona y elige su fila; el buscador
  del glosario filtra «cpa» a 5 términos; captura de la ventana revisada. **Sin probar**: con el paquete real
  de Cuba en el PC del usuario, con un feed real y el camino qmake.

## 159. Sexta aplicación de la serie: `vigilancia_zonas`

`watchZone` y las señales de entrada y salida ya estaban en la librería y se usan en `app_seguimiento` y en
`monitor_trafico`, pero siempre con una zona fija. Una app que vigila de verdad necesita lo que se cuelga de
esas señales: **que el operador dibuje sus zonas, que cada zona tenga sus reglas, que quede un historial** y
que todo se pueda guardar. `examples/vigilancia_zonas/` (~1220 líneas, un solo `main.cpp`) es esa plantilla.

- **Reglas por zona, de la aplicación, no de la librería:** avisar al entrar, avisar al salir y avisar si un
  contacto pasa más de N minutos dentro (la *permanencia*). Se guardan como `MapFeature::attributes`
  (`avisar_entrada`, `avisar_salida`, `max_min`), así que **viajan en el GeoJSON** sin tocar la librería, y el
  contorno cambia de color según la regla más grave (rojo, naranja, azul, gris si solo registra).
- **Historial de eventos** (`Entra`, `Sale`, `Permanencia`) con hora, zona, contacto, permanencia y aviso; filtro
  «Solo avisos»; **exportación a CSV** (`hora_utc;zona;contacto;evento;permanencia_min;aviso`, separado por `;`,
  hora en UTC ISO). Se anotan todos los hechos y el aviso lo marca la regla.
- **Permanencia:** al entrar se guarda desde cuándo está dentro cada par (contacto, zona); al salir se calcula y
  se anota, y un temporizador avisa **una sola vez** si se pasa del máximo mientras sigue dentro.
- **Dibujar y editar:** `MapTool::DrawPolygon` con `featureCreated` → diálogo de nombre y reglas (si se cancela,
  la zona se borra) → `watchZone`. Editar la forma con `EditFeature` reevalúa quién queda dentro y la librería
  emite las entradas y salidas que provoque: no hace falta código propio.
- **Guardar y abrir** las zonas (`exportVectorFile` / `importVectorFile`), con `autosaveFeatures = false`. Al abrir se
  recolorean según las reglas leídas y se vuelven a vigilar.
- **Glosario** en la interfaz (14 términos, buscable, F1, y como ayuda en las cabeceras de las dos tablas), con la
  misma comprobación de `monitor_trafico`: `--comprobar` falla si una cabecera o un término de la ventana no está
  definido.
- **Un orden que importa:** los contadores de una zona se crean **antes** de `watchZone`, porque esa llamada ya
  emite una entrada por cada contacto que esté dentro; al revés, esas entradas se perderían. Y el nombre del
  contacto se lee de su objetivo del mapa (`target(id)->label`), no de una tabla propia, por lo mismo que en el
  monitor: las señales llegan dentro de `addTarget`/`updateTarget`.
- **Tres fallos míos, ninguno de la librería:** (1) el escenario del simulador: el Remolcador seguía hacia el este y
  a los 40 min entraba en el Fondeadero (la librería hacía lo correcto: lo vi en el historial impreso); ahora se
  detiene a los 20 min, fuera de todo. (2) La captura enseñó «Avisa al entrar **Â·** al salir»: un `QLatin1String`
  con el carácter `·` (UTF-8) se lee como Latin-1; es el mismo error que en `monitor_trafico` con `—`, así que
  queda una comprobación del texto y un `grep` limpio en los ejemplos. (3) Las columnas de la tabla de zonas, repartidas
  a partes iguales, cortaban «Zona restringida» y las reglas; ahora el texto largo se lleva el sobrante.
- **Prueba (`--comprobar`):** 23 comprobaciones sobre tres zonas de ejemplo y cinco contactos: el que ya está dentro al
  vigilar produce su entrada; Remolcador sale del Puerto con aviso tras ~10 min; Pesquero 1 entra en la zona
  restringida con aviso y sale tras ~30 min sin él; Carguero A, parado en el Fondeadero, da UN aviso de permanencia
  a los 30 min; Pesquero 2 lo cruza en ~20 min sin aviso; Carguero B no da ningún evento; 8 eventos y 3 avisos en
  total, y los contadores y el «dentro» coinciden. Mover el Fondeadero hace salir a Carguero A (35 min dentro) y
  devolverlo lo vuelve a meter; cambiar reglas las guarda y recolorea; dibujar una zona pide reglas y la vigila,
  y cancelar la borra; borrarla la deja de vigilar; el CSV tiene cabecera, 6 columnas por fila y la fila del
  Remolcador completa; guardar, borrar y abrir devuelve las tres zonas con sus reglas y otra vez vigiladas. Entra
  en `herramientas/probar_instalacion.sh`, `examples/CMakeLists.txt` y `examples.pro` (CI, Qt 5 y 6). Para no
  depender de `QTextStream::setCodec` (distinto en Qt 5 y 6) el CSV se escribe como bytes UTF-8.
- **Verificado** en el contenedor con un programa fuera del repositorio sobre la ventana real: 4 clics y un doble clic
  con `DrawPolygon` crean una zona (con las reglas de una petición simulada), que queda vigilada y anota las 5
  entradas de los contactos que había dentro; elegir una fila de la tabla de zonas la selecciona en el mapa; captura
  de la ventana revisada. **Sin probar**: con el paquete real de Cuba en el PC del usuario, con un feed real, el
  diálogo de reglas a mano (la prueba lo sustituye) y el camino qmake.

## 160. Séptima aplicación de la serie: `analisis_terreno`

El análisis de elevación (`elevationProfile`, `lineOfSight`, `viewshed`, relieve sombreado) es de lo más potente
de la librería y de lo que menos se ha enseñado como aplicación: las pruebas lo cubren, pero nadie lo ha usado
con un ratón. `examples/analisis_terreno/` (~1050 líneas, un solo `main.cpp`) lo pone en manos de una persona: dos
puntos arrastrables, la visual en vivo, el perfil, la zona visible y un terreno de demostración.

- **Línea de vista en vivo:** A (observador) y B (objetivo) son `MapFeature` con `draggable = true`; la señal
  `featureDragMovedTo` recalcula `lineOfSight` y `elevationProfile` con la altura de las dos antenas. También se
  fijan con un clic (`PickPoint`). El texto dice si hay vista y con qué holgura, o dónde bloquea y qué falta.
- **Perfil que explica el porqué:** se dibuja el terreno **más el abombamiento** `d·(D−d)/(2·k·R)` (k = 4/3), así la
  visual es una recta y lo que la corta es lo que bloquea; azul si hay vista, roja si no, con un círculo en el punto
  crítico. Una leyenda dice que las cotas del eje son «efectivas» (al ver la captura, «328 m» junto a una cresta de
  320 m parecía una incoherencia).
- **«¿Qué altura necesita la antena de B?»:** una bisección sobre `lineOfSight` (la visibilidad solo mejora al subir la
  antena); el texto la da redondeada al metro hacia arriba. La prueba comprueba la coherencia: con esa altura se ve y
  con 2 m menos no.
- **Zona visible:** `viewshed` de 360 rayos con progreso y cancelación (`ViewshedProgress` que procesa eventos y
  devuelve `false` al pulsar «Cancelar»). Se pinta **como en la demo** (`Ventana::analizarViewshed`): una cuña por
  cada tramo de cada rayo, **una entidad con todas las cuñas como partes** por tipo, **azul = visible** y **amarillo =
  oculto** (lo oculto debajo), y lo que no tiene dato sin pintar. Además, **la máxima visibilidad**: un marcador en el
  punto visible más lejano, unido a A por una línea (esto último es adición mía; la demo solo da el alcance máximo en el
  texto). El texto informa de visible, oculto y sin dato (% del círculo), de la máxima visibilidad y de la zona ciega.
- **Sin elevación, lo dice y ofrece un terreno de demostración:** un `.hgt` sintético (SRTM3, 1201², big-endian)
  escrito en una carpeta temporal, con una llanura que sube 20 m por grado, una loma de 300 m en lon -82.5 y un cerro
  de 120 m; así el ejemplo se prueba sin datos reales y `--comprobar` es determinista. Relieve sombreado con
  interruptor y azimut del sol.
- **Rendimiento medido** (contenedor de pruebas, Release): la altura mínima de antena tarda 13 ms y recalcular visual y perfil
  al arrastrar un punto, 15 ms (fluido en vivo); la zona visible de 20 km con 360 rayos, 0,3 s. Alcances de cientos de km no
  se han medido; por eso el cálculo trae progreso y cancelación.
- **Glosario** en la interfaz (15 términos, buscable, F1) con la misma comprobación que en los dos ejemplos
  anteriores: `--comprobar` falla si un término que la ventana muestra no está definido.
- **El resultado que no esperaba, y que era correcto:** mi primera prueba pedía que la zona visible fuera el 80–92 %
  del círculo de 20 km y salió el 78 %; antes de tocar la expectativa miré por azimut. Hacia el oeste el alcance era
  18,84 km y no 20: **la curvatura de la Tierra pone el horizonte de un ojo a 10 m y un objetivo a 2 m en
  √(2kR·10)+√(2kR·2) = 18,86 km**, que yo había olvidado. Con ese círculo menos el casquete tras la loma
  (18,84²·acos(12,24/18,84) − 12,24·√(18,84²−12,24²) ≈ 132 km² de 1115) sale 983 km², y la librería da 978. Tampoco
  llega a 20 km en el llano porque el objetivo está por debajo del horizonte. Las comprobaciones ahora son
  esas tres y no un porcentaje inventado: alcance al oeste = horizonte teórico (±600 m), superficie entre 950 y 1020
  km², y **coherencia con `lineOfSight` en 5 azimuts** (el último punto visible del rayo se ve y 60 m después no).
- **Prueba (`--comprobar`):** sin elevación, la visual lo dice y la zona no se calcula; con el terreno de demostración,
  cota de la cresta 320 m y del llano 20 m; A–B a 24,47 km; con antenas de 10 m el terreno bloquea a 12,24 km con
  298,8 m de falta; el perfil tiene 817 muestras y llega a 320 m, y el widget dibuja el terreno (36 000 píxeles de
  su color); altura mínima de B = 617 m y coherente; con B a 700 m hay vista y holgura positiva; arrastrar A
  recalcula; la zona tiene 360 rayos, alcance mínimo ~12 km al este; un punto fuera de los datos da «Sin dato» y no
  zona; el relieve se enciende; el glosario define lo que se muestra. Entra en `herramientas/probar_instalacion.sh`,
  `examples/CMakeLists.txt` y `examples.pro` (CI, Qt 5 y 6).
- **Verificado** en el contenedor con un programa fuera del repositorio sobre la ventana real: arrastrar B con el
  ratón (pulsar, mover, soltar) lo mueve y recalcula; fijar A con `PickPoint` y un clic lo mueve y la herramienta
  vuelve a Navegar; capturas revisadas. La casilla del relieve refleja el estado aunque se active por código.
  **Sin probar**: con el paquete real de Cuba (`cuba_dem.sqlitedb`) en el PC del usuario, con GeoTIFF, con alcances
  de cientos de km y el camino qmake. Con el paquete mínimo del CI el mapa se ve como el mundo entero.

### 160 (addendum). Corrección tras probar con datos reales: el Pico Turquino daba «0 %»

El usuario abrió el ejemplo con su `cuba_dem.sqlitedb` y puso A en el **Pico Turquino** (cota 1973 m, antena 20 m,
objetivo 10 m, alcance 20 km). La línea de vista A–B salió bien (clara, holgura 12 m a 0,1 km de A: el terreno cae en
picado), pero la zona visible decía **«se ve el 0 % del círculo, mayor alcance 0,4 km, menor 0,1 km»**: imposible desde el
punto más alto de Cuba. Preguntó «¿es posible eso?». **No lo era, y el fallo era mío.**

- **Causa.** El ejemplo pintaba y medía solo `ViewshedRay::visibilityReachM`, el **primer tramo contiguo** desde A (la
  documentación de la librería ya lo dice, y yo había dejado «documentada» esa limitación en el README). Desde una cumbre
  redondeada **no se ve la ladera justo bajo su borde** (zona ciega: queda por debajo del horizonte del observador), pero
  **sí las tierras bajas de lejos**. Esos segundos tramos eran lo que se ignoraba, y con datos reales la «simplificación»
  daba un resultado sin sentido.
- **Reproducción antes de arreglar.** Añadí al terreno de demostración un pico redondeado de 880 m (σ = 2 km, en 23.75,
  -82.25). Con el ojo a 20 m y el objetivo a 10 m, la librería da en cada rayo **dos tramos visibles**: de 0 a 0,75 km y de
  9,75 a 19,98 km; `lineOfSight` coincide (a 300 m se ve, a 3 km no, a 17 km sí). Es decir, el cálculo estaba bien; lo que
  fallaba era qué se mostraba.
- **Arreglo.** La zona se pinta y se mide como en la demo (todos los tramos, visibles y ocultos), con el marcador de la
  máxima visibilidad. En el pico: **se ve el 74 %** (931 km², en 720 tramos) y se oculta el 25 %; el primer tramo solo habría
  sido 2 km². Se añade el término «Zona ciega» al glosario (15 términos) y el texto informa de ella.
- **Otro fallo que cazaron las pruebas al tocar esto:** `borrarZonaVisible` quitaba una sola entidad, pero la zona pasó a ser
  varias (azul, amarilla, línea y marcador): **cada cálculo habría ido apilando las del anterior**. Ahora vacía la capa entera
  (es solo de la zona).
- **Pruebas nuevas (`--comprobar`):** desde la cima (cota 905 m) la zona ciega empieza a ~0,8 km; se ve entre el 60 y el 90 %
  del círculo (74 % medido) frente a 2 km² del primer tramo; en 4 azimuts hay exactamente dos tramos y `lineOfSight` dice
  se ve / no se ve / se ve a 300 m / 3 km / 17 km; lo oculto (~25 %) se pinta en amarillo; una entidad azul y otra amarilla con una
  cuña por tramo y los colores de la demo; el marcador de máxima visibilidad cae a la distancia correcta; visible + oculto +
  sin dato suman el 100 %. La leyenda del perfil pasó a una banda propia: con terreno alto a la izquierda se pisaba con la curva.
- **No se hizo:** las opciones de la demo «mar = 0» (`voidElevation`) y «sin curvatura». Con la costa de Cuba, el mar sin
  dato queda sin pintar; si hace falta analizar objetivos sobre el agua, es poner `voidElevation = 0` en `ViewshedParams`.

### 160 (ampliación). Mar a 0, coordenadas escritas, 10 picos y ángulos de cierre

Tras ver la corrección, el usuario pidió: «pon el mar en 0 y mantén la curvatura, así es más real; permite introducir las
coordenadas de A y B; agrega los 10 picos máximos en 10 km de radio, y ángulos de cierre en el perfil o como gráfica
secundaria, y ángulos de cierre hasta la distancia máxima seleccionada en los 360°». Va en la misma rama y PR
(`claude/app-analisis-terreno`), porque es el mismo ejemplo y aún no estaba fusionado.

- **Mar = 0.** Casilla marcada por omisión: `voidElevation = 0` en el perfil, la línea de vista, la bisección de altura
  y el viewshed; y `cotaEn()` aplica la misma regla a las cotas que muestra (sin dato o negativa = 0). La curvatura no se
  toca (k = 4/3). Efecto colateral buscado: un punto fuera de los datos pasa a ser mar y no «sin dato»; la prueba que
  esperaba lo segundo se hizo con la casilla desmarcada y se añadió la del caso nuevo. Cambiar la casilla borra la zona y
  los picos, porque eran de la otra regla.
- **Coordenadas.** Dos cajas con `parseCoordinate` (decimal, sexagesimal con O, UTM, MGRS), que mueven el punto y centran
  el mapa; texto inválido = caja roja y punto intacto. Al arrastrar el punto la caja se reescribe (sin disparar su propia
  edición, con `QSignalBlocker`). Solo se centra si la posición cambió más de 1 m: `editingFinished` también salta al perder
  el foco con el mismo texto.
- **Ángulo de cierre en el perfil.** La librería ya devolvía `horizonDeg` por rayo; para el perfil A–B el ejemplo calcula
  `atan2(z − ojoA − d²/(2·k·R), d)` por muestra y su máximo acumulado (`angulosPerfil`), en una gráfica con el mismo eje de
  distancias bajo el perfil. Es la misma pregunta que `lineOfSight` dicha en ángulos, así que se **verifica contra
  `lineOfSight`** con B a 10, 100, 300, 500, 700 y 900 m: coinciden. Primera versión: el primer paso (30 m de A) cae a −20° y
  aplastaba la escala; ahora el eje ignora el primer 3 % del trayecto y la curva se recorta al marco.
- **Ángulos de cierre en los 360°.** Gráfica por azimut (`CierresWidget`), alimentada por el mismo viewshed que la zona
  visible (`ViewshedRay::horizonDeg`), con el máximo, el mínimo y el azimut hacia B. Comprobación física independiente:
  sobre mar abierto, 10 m de ojo y 10 km de alcance, el horizonte cae a atan((−h − R²/(2·k·Rt))/R) = **−0,091°** y la
  librería da −0,091° en los 360 azimuts.
- **Picos.** Malla alrededor de A, máximos locales estrictos (sin celdas del borde ni junto a huecos), orden por cota y
  separación mínima de radio/10; para cada uno, distancia, azimut, ángulo y línea de vista. 10 km en ~50 ms. Para probarlo
  hace falta saber la respuesta: un terreno de 12 conos de 400 a 950 m en un anillo de 6 km. Primer intento con colinas
  gaussianas: se solapaban (los vecinos sumaban ~23 m), la cima de una gaussiana **no se ve desde lejos** (su hombro la
  tapa; era mi modelo, no la función) y la cota entera por la malla creaba «picos» de 1 m en las colas. Con conos de punta
  afilada salen los 10 esperados (950 … 500, a 6 km, en sus azimuts, todos visibles) y con radio 5 km, ninguno.
- **Interfaz.** Panel izquierdo en un `QScrollArea` (ya no cabía), tabla de picos (clic centra, doble clic pone B ahí),
  dock *Ángulos de cierre (360°)* al lado del perfil, tres términos nuevos en el glosario (18).
- `--comprobar`: 61 comprobaciones (eran 34), todas verdes. **Sin probar:** con el `cuba_dem.sqlitedb` del usuario
  (picos del Turquino, rendimiento de la malla con 20-50 km de radio).

### 160 (ajuste). Zoom automático en la gráfica de ángulos del perfil

Con datos reales (A en el Turquino, B a 3,4 km al sur) todos los ángulos son negativos y la escala vertical iba de −47° a −16°:
B (−18,12°) y el cierre antes de B (−18,27°) quedaban pegados y el margen de 0,15° no se veía. El usuario pidió zoom automático.

- **Regla.** El eje se centra en lo que decide la visibilidad (`ejeAngulos`): ventana de ±4 veces la diferencia entre el ángulo de
  B y el cierre antes de B, con mínimo ±0,5°. Lo que cae fuera (la subida inicial del cierre) se recorta al marco. Si la ventana
  saldría más ancha que el rango completo (B muy tapado: en la demostración, 1,4° de diferencia), no mejora nada y se deja el
  completo. Doble clic en el perfil alterna zoom/todo; la leyenda dice cuál está activo.
- **Comprobado.** Con una diferencia de 0,15°, el margen ocupa un 11 % del eje con zoom y un 0,7 % sin él; el eje con zoom
  siempre contiene a B y al cierre y nunca es más ancho que el completo. Visualmente, con B a 0,03° sobre el cierre las dos
  líneas se separan con claridad (antes eran una sola). `--comprobar`: 65 comprobaciones.

## 161. Una biblioteca nueva, `libtelemetria`, y las aplicaciones `emisor_nmea` y `cliente_telemetria`

El plan era un ejemplo, `cliente_telemetria`: recibir NMEA 0183 y AIS por UDP y pintarlo en el mapa. Al plantear cómo, el usuario
propuso **hacer el decodificador como biblioteca aparte** en vez de meterlo en el ejemplo. Tiene razón, y por tres motivos:
decodificar texto en datos no tiene nada que ver con teselas ni ventanas; el cliente, el emisor, una herramienta de consola o un
test lo usan igual; y un decodificador dentro de un ejemplo es código que cada producto copiaría.

### La biblioteca (`libtelemetria/`)

- **Aparte de libmapa, solo Qt Core.** Sin red, sin Gui, sin mapa y **sin E/S**: se le dan bytes y devuelve eventos. Su
  `CMakeLists.txt` se detiene si alguien le añade Network, Gui, Sql, Positioning o libmapa (como la guarda de red de libmapa).
  Se compila sola (`cmake -S libtelemetria`) o metida en la raíz (`LIBMAPA_BUILD_TELEMETRIA`, activa) y se **instala en el mismo
  prefijo** que libmapa: `find_package(libtelemetria)` → `telemetria::telemetria`; con qmake, `telemetria.pri` junto a
  `libmapa.pri`, y `qmake/libmapa/libmapa.pro` compila e instala las dos.
- **`Nmea.h`**: `parseSentence` (dirección, emisor, tipo, campos —los vacíos cuentan— y suma de comprobación, que se exige y de
  la que dice cuál debería ser), `formatSentence`, y RMC, GGA, VTG y HDT con su decodificador y codificador. Campo vacío = NaN.
- **`Ais.h`**: tipos 1, 2, 3, 5 (dos frases), 18, 19 y 24 (partes A y B); `AisAssembler` para los mensajes de varias frases
  (por secuencia y canal, memoria acotada); etiquetas en español del tipo de barco y del estado de navegación. «No disponible»
  (181°, 91°, 1023, 3600, 511) es NaN/−1 y no un dato. Un tipo no soportado devuelve solo tipo y MMSI.
- **`Decoder.h`**: `feed` (flujo; la línea a medias espera), `feedDatagram` (UDP), `feedLine`; devuelve `Event` y lleva
  `DecoderStats` (líneas, decodificadas, suma mala, mal formadas, ignoradas, AIS completos/no soportados, fragmentos perdidos).

### Cómo se comprobó que decodifica bien

Un codificador y un decodificador del mismo autor que solo se prueban entre sí pueden estar equivocados de acuerdo. Por eso los
tests incluyen datos de **fuente independiente**:
- Las sumas publicadas de las frases de ejemplo más usadas (`GPRMC…*6A`, `GPGGA…*47`, `GPVTG…*48`) salen exactas.
- El mensaje AIS de la documentación de gpsd (`!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C`) da MMSI 477553000, 47.582833° N,
  122.345833° O, rumbo 51°, proa 181°, segundo 15, amarrado: el diseño de bits (6+2+30+4+8+10+1+28+27+12+9+6…) salió bien a la primera,
  y la suma 5C también.
- Aparte, ida y vuelta de cada tipo con coordenadas negativas y extremas (complemento a dos de 28 y 27 bits), reensamblado fuera de
  orden y entrelazado, cargas malas y el decodificador de flujo con líneas partidas y ruido.

Lo que **no** está probado: un receptor AIS real, ni una captura real larga. El diseño de bits de los tipos 5, 18, 19 y 24 está
verificado por la ida y vuelta, no por un mensaje publicado de cada uno (solo el tipo 1 lo tiene).

### Los ejemplos

- **`emisor_nmea`** (sin libmapa ni ventana): cinco barcos frente a La Habana (el propio, un carguero de frente, un pesquero de
  clase B, un petrolero y un remolcador que se calla a los 90 s) y ruido (suma mala cada 20 s, GSV cada 40 s, frase cortada cada
  60 s). Por UDP, agrupado o no, o a un fichero. Su `--comprobar` decodifica lo emitido con la propia biblioteca y lo compara con lo
  simulado: el ruido inyectado es exactamente lo que se cuenta (6/2/3) y ningún fragmento se pierde.
- **`cliente_telemetria`** (libmapa + libtelemetria + Qt Network): `QUdpSocket` → `Decoder` → barco propio (RMC/GGA/VTG/HDT, o `!AIVDO`
  si no hay GPS) y contactos AIS **fundidos por MMSI** (la posición y el nombre llegan en mensajes distintos y en cualquier orden),
  tabla ordenada por distancia con la marcación, contactos perdidos a los 60 s y borrados a los 5 min (reaparecen si vuelven),
  contadores del enlace, registro, reproducción de capturas y glosario (23 términos). **48 comprobaciones**, entre ellas el mensaje
  de gpsd a través del cliente, la distancia de la tabla contra el cálculo a mano y **un datagrama UDP real por localhost**.
- **De punta a punta:** los dos binarios hablando por UDP en el contenedor (emisor a escala ×10, cliente con `--puerto` y
  `--captura`): 508 líneas, 489 decodificadas, 5 sumas malas y 2 mal formadas (lo que el emisor inyectó), 3 ignoradas, 89 mensajes
  AIS, los cuatro contactos con nombre, tipo y clase, y el propio moviéndose.

### Fallos propios por el camino

- **Primer borrador del cliente, incompleto**: me dejé una función a medias (`c.classB();`) antes de enviarlo y faltaban la prueba y
  el `main`; el filtro de escritura lo rechazó y lo reescribí entero antes de compilarlo.
- **La «frase cortada» del emisor no se cortaba**: `formatRmc(Rmc())` con hora inválida da una RMC vacía de 21 caracteres, y
  `left(25)` no la cortaba. Resultado: una frase válida contada como decodificada. La prueba que cuenta las cortadas lo cazó.
- **Mis expectativas de contadores estaban mal** en dos tests del decodificador (los mensajes AIS cuentan como «decodificadas»; la
  frase vacía no cuenta como línea); se corrigieron los tests, no el código.
- **Compilando sola**, `libtelemetria` no encontraba los `.moc` de los tests (faltaba `CMAKE_AUTOMOC`, que la raíz sí pone) y
  `-Wconversion` daba dos avisos (`qsizetype` → `int` de Qt 6).
- **Al tocar la raíz**: el `add_subdirectory` va al final, tras `LIBMAPA_INSTALL`, para heredar la instalación y los avisos.

### Verificación

Compilación completa de la raíz con `libtelemetria` dentro: **30 tests** (27 + 3), todos en verde y sin avisos;
`herramientas/probar_instalacion.sh` pasa con libmapa y libtelemetria en el mismo prefijo (comprueba además
`libtelemetriaConfig.cmake`, las cabeceras y `libtelemetria.a`) y arranca `emisor_nmea` y `cliente_telemetria` con `--comprobar`;
`libmapa.pro` (que instala libmapa **y** libtelemetria en el mismo prefijo) compila e instala con qmake, y con esa instalación
`emisor_nmea.pro` y `cliente_telemetria.pro` compilan y pasan su `--comprobar` (Qt 6 en el contenedor). `docs/arquitectura.html` y
su PDF tienen la sección 8d. **Sin probar:** un receptor o una captura reales, TCP/serie (no existen), qmake con Qt 5 y con MinGW en
el PC del usuario, y el despliegue con Qt Network.

### 161 (ajuste). Avisos de Qt 6.11 en los dos ejemplos nuevos

Al compilar en el PC del usuario (Qt 6.11.2 MinGW), `emisor_nmea` y `cliente_telemetria` dieron avisos `-Wdeprecated-declarations`:
`QDateTime(QDate, QTime, Qt::TimeSpec)` está obsoleto desde Qt 6.9 («pass QTimeZone instead»). Con Qt 6.4 del contenedor no avisaba, y la CI
(Qt 5 y Qt 6 de Ubuntu) tampoco. `QTimeZone::UTC`, que lo sustituye, solo existe desde Qt 6.5, así que no vale un simple cambio: la biblioteca
ofrece `telemetria::utcDateTime(fecha, hora)` con el `#if` y la usan `Rmc::utc`, los tests y los dos ejemplos. Las inclusiones de red de
los ejemplos pasan a `<QtNetwork/QUdpSocket>` (la ruta con módulo) para que el modelo de código de Qt Creator las encuentre aunque no tenga
el directorio de QtNetwork en su lista. **Sin reproducir:** los errores `'QHostAddress' file not found` que mostró Qt Creator; el compilador sí
llegó a las líneas 342-535 del emisor (dio avisos en la 394), así que eran del modelo de código (clang) y no del compilador, pero es una inferencia.

Después, en el PC del usuario, `emisor_nmea` dio `'utcDateTime' was not declared`: había traído el ejemplo nuevo pero seguía con las cabeceras de
`libtelemetria` de la instalación anterior (`C:/libmapa/qt6/include/telemetria`), que no tenían la función. Es el desfase normal entre una
biblioteca instalada y un ejemplo más nuevo, pero el mensaje no lo decía. Ahora `telemetria_export.h` define `TELEMETRIA_API_LEVEL` (2) y los dos
ejemplos exigen ese nivel con un `#error` que dice «reinstálala»; una instalación anterior al nivel (sin la macro) cae en ese mensaje claro.

### 161 (ajuste). El mapa temblaba y no se dejaba mover con «Centrar el mapa en mi barco»

El usuario, probando emisor y cliente en su PC: «el mapa salta constantemente por la horizontal y no me deja moverlo», y «cuando está
seleccionado centrar en mi barco es peor». Y preguntó en qué se diferencia de la demo, que no hacía esos saltos.
- **Diferencia.** Los contactos se dibujan igual (`addTarget`/`updateTarget`; el simulador de la demo hace lo mismo en `pasoSimulacion`). Lo que
  la demo **no** hace es mover el mapa: solo hace `setCenter` ante una acción de la persona. Yo recentraba en el barco propio con **cada RMC**
  (1 por segundo, 10 con `--escala=10`) y con la casilla marcada **por omisión**: el mapa se deslizaba bajo el barco sin parar y volvía a
  agarrarse cada vez que se intentaba arrastrar.
- **Arreglo.** Desmarcada por omisión (como la demo); marcada, recentra solo cuando el barco sale de la zona central (el 35 % del medio ancho
  de la ventana, con `metersPerPixel`); arrastrar el mapa con el ratón la desmarca (`centerChanged` con un botón pulsado, y no si el centro lo
  movió el propio seguimiento); marcarla recentra al momento. 8 comprobaciones nuevas en `--comprobar` (no sigue por omisión; sigue al marcar; un
  desplazamiento de 100 m no mueve el mapa; uno de 40 km lo recentra; el arrastre la desmarca; sin ratón no; desmarcada se queda donde se puso;
  re-marcar recentra). **Sin probar:** la sensación en una ventana real en el PC del usuario, y el arrastre real (el contenedor no tiene ratón:
  se probó la lógica con `alCambiarCentro(arrastrando)`).

## 162. `cliente_telemetria` por TCP y por puerto serie

El ejemplo recibía solo por UDP. Muchos aparatos reales no emiten así: los receptores AIS de red, AIS-catcher o gpsd sirven un **flujo TCP**, y
los GPS y los AIS «de cable» hablan por un **puerto serie**. El usuario pidió añadir ese modo («si tu aparato no emite por UDP») y se fue a dormir
con el encargo de terminarlo; esto se hizo sin revisión suya. La biblioteca **no cambia** (el `Decoder` ya tenía `feed` para flujos y
`feedDatagram` para UDP): no hay que reinstalar `libtelemetria`.

### Qué hace

- **Tres fuentes** con el mismo decodificador, elegidas en el panel *Enlace* (y por línea de comandos: `--puerto=`, `--tcp=host:puerto`,
  `--serie=COM3[,baudios]`): UDP (`feedDatagram`), TCP como cliente (`QTcpSocket`) y puerto serie (`QSerialPort`, 8N1, solo lectura, 4800-115200).
- **Reconexión automática** (cada 3 s) del TCP y del serie: si el servidor no está, si se corta o si se desenchufa el cable, el panel dice «Sin conexión con…
  reintentando cada 3 s (N intentos)», el registro lo anota el primer intento y luego uno de cada diez (no se llena), y al volver sigue solo. `parar()`
  cancela el reintento. Se detecta con `stateChanged` (TCP) en vez de las señales de error, que cambian entre Qt 5.14, 5.15 y 6.
- **Una frase a medias no se pega a la siguiente conexión:** al parar o caerse el enlace se entrega lo que hubiera como mal formado
  (`feedDatagram` vacío).
- **El serie es OPCIONAL.** Qt Serial Port es un módulo adicional (en el instalador de Qt de Windows, «Additional Libraries»); hacerlo obligatorio
  habría roto la compilación a quien no lo tenga marcado. `CMakeLists.txt` lo busca con `QUIET` y `.pro` usa `qtHaveModule(serialport)`; sin él, el
  cliente compila con UDP y TCP y la opción «Puerto serie» sale desactivada con su explicación.
- **`emisor_nmea --tcp=PUERTO`**: hace de servidor TCP (varios clientes, olvida al que se va). **No emite por serie**; para probar el serie en
  un PC hace falta un par de puertos virtuales (com0com, `socat`).
- Glosario con tres términos más (TCP, Puerto serie, Baudios): 26.

### Comprobado (contenedor, Qt 6.4 y Linux)

- **TCP real** en `--comprobar` (`QTcpServer` en localhost): conectar, media frase que no hace nada y el resto que la completa, un tipo 5 de dos frases y un
  tipo 1 en un solo bloque, el corte del servidor (se anota y se programa el reintento), la **reconexión a los ~3 s**, que por la conexión nueva sigue
  llegando todo, que **`parar` no reconecta** y un servidor que no existe.
- **Puerto serie** con un pseudoterminal (`posix_openpt`) que hace de cable: abrir a 38400, frase partida en dos escrituras, parar, y un puerto que no existe.
  Solo corre en Linux y con el módulo presente; sin él, la prueba comprueba que `abrirSerie` dice que no está.
- **De punta a punta** con los dos binarios por TCP: el cliente arrancó antes que el emisor (reintentó), recibió los cuatro contactos con nombre y,
  al acabar el emisor, volvió a mostrar «Sin conexión… reintentando».
- qmake (`cliente_telemetria.pro` con `libmapa.pro`/`telemetria.pro` instalados): compila con `serialport` y pasa `--comprobar`. El aviso de «reinstala
  `libtelemetria`» saltó de verdad con un prefijo de qmake anterior: hizo su trabajo.
- La CI instala ahora `qt6-serialport-dev` y `libqt5serialport5-dev` para compilar y probar también la rama del serie con Qt 5 y Qt 6.

### Sin probar

- **Aparatos reales:** el TCP contra AIS-catcher, gpsd o un conversor serie-red; el serie con un GPS/AIS físico y con Windows (`COM3`): no se ha
  ejecutado nunca allí. El pseudoterminal de Linux no es un puerto USB: p. ej. no ejercita el desenchufado (`ResourceError`) ni los errores de velocidad.
- Qt Serial Port **del PC del usuario**: si su kit no lo trae, el cliente compilará sin serie (por diseño), pero esa rama no se ha compilado aquí.
- Formatos serie distintos de 8N1 y control de flujo; TCP servidor, TLS y autenticación.

## 163. MBTiles vectoriales (pbf / Mapbox Vector Tile) como capa base

El usuario pasó dos scripts (`descargar-mbtiles.sh` / `.bat`) que bajan el mapa de Cuba de **OpenMapTiles** en MBTiles **vectorial**:
`cuba-osm-z16.mbtiles` (z0-16, 360 MB) y `cuba-osm.mbtiles` (913 MB), y pidió «preparar la librería para trabajar con mbtiles pbf
vectoriales… no se puede dañar lo que ya existe, es un agrego más». Hasta ahora `MBTiles::describe` los **rechazaba** («solo se admiten
MBTiles ráster») y `probe_db` avisaba de que no se mostrarían.

### Qué se decidió y por qué

- **Pintar la tesela vectorial a imagen en el hilo de carga, y nada más.** El motor entero (caché por capa, escalera de respaldo,
  planificador, cobertura, `addBaseLayer`, cambio de capa) trabaja con `QImage` de 256 px. Si el `TileLoader` entrega una imagen, nada de
  eso se entera de que la fuente es vectorial: el cambio queda acotado a **dónde se decodifica** (`TileLoader::load`), y una capa con
  `vector = false` —todas las anteriores— sigue el camino de siempre línea a línea. Dibujar la geometría directamente en el `MapView`
  (como `FeatureLayer`) habría dado zoom continuo sin escalones, pero obligaba a duplicar caché, respaldo y planificación: mucho riesgo
  para un «agregado».
- **Sin dependencias nuevas.** MVT es protobuf en gzip. libprotobuf y zlib complicarían el despliegue con MinGW (`desplegar.bat`,
  `desplegar_qt5.bat`, qmake). El protobuf de MVT solo usa varints, enteros fijos y bloques con longitud: el lector propio
  (`src/vector/MvtTile.cpp`) son unas 200 líneas. El gzip lo infla **miniz**, que ya estaba en el repo para `fill_hgt`; ahora también va
  dentro de `libmapa_core` (CMake y `core.pro`).
- **Estilo propio y sencillo, no Mapbox GL.** El formato de estilos de Mapbox GL exige expresiones, sprites y glifos remotos; para pintar
  sin conexión con `QPainter` basta una lista de reglas en orden (capa, filtro por propiedad, zooms, relleno, trazo con grosor por zoom,
  borde, discontinuo, rótulo). El **estilo por defecto** es para el esquema OpenMapTiles (colores tipo OSM Bright) y va embebido como JSON,
  que es a la vez el ejemplo del formato (`docs/estilo_vectorial.json`, `vector_tile --print-style`). Uno propio se pone con
  `"vectorStyle"` en el dataset (relativo al JSON, viaja con el paquete); si no se puede leer, se avisa y se usa el de por defecto: un
  estilo roto no deja el mapa en blanco.
- **Detección por contenido, no solo por metadata.** `describe` decodifica una tesela real: si es MVT marca `TileDataset::vector`; si
  dice `pbf` pero trae otra cosa (p. ej. PNG) se sigue rechazando con su motivo. Así siguen pasando sin tocarlos los casos de
  `tst_mbtiles::rejectsWhatItCannotUse`.
- **Rótulos sin cortes.** Cada tesela se pinta sola, y un texto que cruza el borde saldría cortado por la vecina. Solo se rotulan los
  puntos cuyo ancla cae **dentro** de la tesela (la vecina no lo repite aunque lo traiga en su margen) y el texto se desliza lo justo para
  caber entero; los que chocan con otro se omiten. Se pierden algunos nombres junto a los bordes, a cambio de no ver nunca medio nombre.
  Los rótulos a lo largo de líneas (calles) no se dibujan.
- **En paralelo.** Medido con el fichero real: una tesela z14 del centro de La Habana (140 KB en gzip, ~8600 edificios) tarda 50-130 ms;
  una pantalla de 20 teselas, 1,5 s en un hilo. `TileLoader::renderVector` las reparte entre su hilo y un `QThreadPool` propio
  (núcleos − 2; no el global, que puede ser de la app) y abandona si la petición caduca: 35 teselas z15 en ~350 ms en el contenedor.
- **`check_data`** decodificaba una tesela de muestra como imagen y habría dado un error falso («falta el plugin de imagen»); ahora una
  capa vectorial se comprueba como MVT y sale «imagenes mvt (gzip)».

### Un fallo encontrado con el dato real

`tinfl_decompress_mem_to_heap` (la función de miniz que usa `fill_hgt`) devuelve NULL también cuando el resultado tiene **cero bytes**, y
el MBTiles de Cuba está lleno de teselas vacías (gzip de nada: mar abierto). La primera prueba con el fichero real lo rechazó entero por
«tesela comprimida corrupta». Se cambió a `tinfl_decompress_mem_to_callback` y esa tesela vacía, tal cual sale del fichero, es ahora un caso
del test.

### Herramienta nueva: `vector_tile`

`--info` (capas, entidades, claves y clases de la tesela de un punto), `--out` (pinta una vista a PNG, con `--style` opcional, y dice lo
que tarda cada tesela) y `--print-style`. Solo núcleo, se instala con las demás.

### Comprobado

- **Referencia independiente:** los fixtures oficiales de `mapbox/mvt-fixtures` (CC0, `tests/data/mvt/`): punto, línea, polígono,
  multipunto, multilínea, multipolígono con hueco, capa vacía y los siete tipos de `Value`, con el resultado que da la especificación; gzip
  y zlib comprimidos con Python. Además, ida y vuelta con teselas fabricadas en el test (estilo, píxeles, rótulos en el borde, MBTiles →
  `TileLoader` con estilo por defecto, propio y roto, `datasets.json` y paquete + `PackageCheck`). `tst_vectortiles`: 16 casos.
- **Fichero real** (`cuba-osm-z16.mbtiles`, MD5 `b9f78193…` como dice el script): se detecta vectorial z0-16; `vector_tile --info` lee
  sus 14 capas; vistas de La Habana a z7, z11, z14 y z16 revisadas a ojo; `render_map` con el `MapWidget` real; `check_data` sobre un
  paquete con él: 0 errores.
- `render_map` deja en respaldo las celdas del borde de la imagen aunque las teselas ya hayan llegado; **pasa igual con un MBTiles ráster**
  hecho de la misma zona, así que es algo previo de `render_map` sin ventana, no de lo vectorial.
- Qt 6.4 y Qt 5.15 (Linux, GCC): compila sin avisos nuevos; `core.pro` con qmake6 compila; contrato de instalación
  (`probar_instalacion.sh`) en verde.

### Sin probar / pendiente

- **El PC del usuario** (Qt 6.11 MinGW, Windows): ni compilado ni visto allí. El dibujo de texto desde varios hilos a la vez es soportado
  por Qt sobre `QImage`, pero en Windows no se ha ejecutado.
- `cuba-osm.mbtiles` (z17, 913 MB): no se descargó; debería comportarse igual.
- **Este MBTiles no trae polígonos de océano** (y su `metadata.bounds` declara el este en 0°): el mar sale con el color de fondo. Es del
  dato, no del dibujo; un estilo propio puede cambiar el fondo.
- Más allá del zoom máximo del fichero se usa el respaldo de siempre (la tesela padre escalada, borrosa); sobre-zoom vectorial nítido
  (pintar una parte de la tesela padre a más escala) queda como mejora.

**Estado: 31 tests (28 sin QCustomPlot), Qt 6.4 y Qt 5.15.**

### 163 (ajuste). «La tabla `tiles` no tiene la columna zoom_level» con los ficheros recién descargados

Primera prueba del usuario en su PC: `vector_tile --info` sobre los dos MBTiles bajados con el `.bat` respondió «no es un MBTiles: la tabla
`tiles` no tiene la columna zoom_level». Ese texto despista: es lo que sale cuando el fichero **no es un SQLite entero** (SQLite lo abre sin
quejarse y solo «no ve» la tabla). Se reprodujo aquí con un `cuba-osm-z16.mbtiles` cortado a 5 MB y con una página HTML guardada como
`.mbtiles`: los dos daban exactamente ese mensaje. Los MBTiles grandes se bajan en partes y se unen con `copy /b`: si falta una parte, se cortó
una descarga o el servidor devolvió una página web, el resultado es justo eso.

`MBTiles::describe` mira ahora, antes de abrir la base, la firma `SQLite format 3` y que el fichero mida lo que dice su cabecera (tamaño de
página × número de páginas; para el de Cuba, 4096 × 87849 = 359 829 504 bytes). Si no, lo dice: «no es una base de datos SQLite… parece una
página web» o «está INCOMPLETO: mide X bytes y debería medir Y (faltan Z MB)», con la sugerencia de volver a descargar y comprobar el MD5.
Vale para cualquier MBTiles, ráster o vectorial. Tres casos más en `tst_mbtiles::rejectsWhatItCannotUse` (texto, HTML y un MBTiles válido
cortado a la mitad; el entero sigue sin queja). **Sin confirmar** que esa sea la causa en el PC del usuario: depende de lo que digan el
tamaño y el MD5 de sus ficheros.

## 164. El mar en las capas vectoriales: máscara de tierra (`landMask`)

El usuario, ya con `cuba-osm.mbtiles` (z17) en su `mapa.json`: «agrega color al mar». El MBTiles de Cuba **no trae océano en
ningún zoom** (`vector_tile --info` en z4-z12, sobre tierra y en mar abierto: la capa `water` solo tiene `lake` y `river`; las teselas
de mar abierto faltan o son gzip vacíos). No hay nada en el fichero que diga dónde acaba la tierra. Los «triángulos» de agua de las vistas
generales son golfos etiquetados como lago. Cambiar el color de fondo tampoco sirve: pintaría de azul también la tierra.

### Qué se decidió y por qué

- **Usar la costa que el usuario ya tiene.** `herramientas/osm_costa_a_sqlitedb.py` (§94) genera `cuba_tierra.sqlitedb`: la costa de
  OSM cosida en polígonos de tierra, la misma BD que `dem_to_hillshade --water-mask` y la capa «Costa exacta» de su paquete. Es la
  única fuente sin conexión de «esto es tierra» que hay en el proyecto, y con el detalle de OSM, igual que el MBTiles. La alternativa
  (los polígonos de agua de osmdata.openstreetmap.de) son cientos de MB más y un formato nuevo.
- **`LandMask` (`src/vector/`)**: lee los polígonos con `VectorRepository` (como `dem_to_hillshade`), los guarda en Mercator normalizado
  con su recuadro y, por tesela, rellena la tierra con la regla **par-impar** (las lagunas y bahías cerradas quedan como agua sin orientar
  anillos). El renderizador, si tiene máscara, rellena el fondo con `"sea"` del estilo, pinta la tierra con `"background"` y después las
  reglas de siempre: los lagos, parques y usos del suelo quedan encima.
- **Que no cueste.** El anillo de la isla grande tiene ~100 000 vértices y toca casi todas las teselas. En vez de pasárselo entero a
  `QPainter`, `paint` descarta los anillos que no tocan la tesela y los de menos de medio píxel, reduce cada **racha de puntos que queda al
  mismo lado fuera de la tesela** a sus extremos (el tramo nuevo cae en el mismo semiplano, así que no cambia nada dentro de la tesela) y
  quita los puntos a menos de 0,3 px. Medido con la costa real (5735 anillos, 429 676 vértices): **1-8 ms por tesela** (z7-z16), frente a
  los 50-130 ms de pintar la tesela vectorial; cargarla, ~300 ms una vez por capa.
- **Configuración**: `"landMask"` (y opcional `"landLayer"`) en la entrada del dataset, relativa al JSON, como `"vectorStyle"`; viaja
  con el paquete (`DataPackage::files()`) y `check_data` avisa si falta (aviso, no error: el mapa se ve, sin mar). Sin `landMask`, todo
  igual que antes. `"sea"` en el estilo (por defecto `#a5c9ea`, el del agua). `vector_tile --land-mask … [--land-layer …]`.
- En el mapa, las teselas que el MBTiles **no tiene** (mar abierto a zoom alto) se cubren con la del nivel superior, que ya sale de mar:
  comprobado con `render_map` a z12 frente a La Habana.

### Comprobado

- Costa real bajada de Overpass (la misma consulta de §94) y convertida con el script del repo: 5735 anillos, 429 676 vértices.
  Vistas a z7, z11, z14 y z16 revisadas a ojo: mar azul, cayos, bahía de La Habana y ensenadas recortadas por la costa OSM.
- `tst_vectortiles` (18 casos): mar, tierra y laguna en su sitio y el lago del MVT encima; sin tesela conocida, el fondo de antes; y el
  recorte rápido pinta **lo mismo** que `QPainter` con el polígono entero (una isla dentada de 20 000 vértices que entra y sale de la
  tesela, comparada píxel a píxel). `landMask`/`landLayer` en `datasets.json` (rutas relativas, ida y vuelta por JSON) y el aviso de
  `check_data`.

**Sin probar:** con el `cuba_tierra.sqlitedb` del usuario (el suyo tiene 5020 anillos, de otra fecha de OSM) y en su PC.

**Estado: 31 tests (28 sin QCustomPlot), Qt 6.4.**

## 165. Mapa híbrido: el MBTiles vectorial encima de cualquier base, nombres de calles y capas conmutables

El usuario, con la capa vectorial ya funcionando: «agrega los nombres de las calles o ponlo como una capa, que otras trae el mbtiles», y
después, al ver las opciones: «¿se pueden crear overlays para mostrar en cualquier capa seleccionada, no solo en la vectorial?… me gusta la
idea, mapas híbridos». Tres piezas, en este orden porque cada una se apoya en la anterior.

### 1. Nombres de calles a lo largo de la calle

El MBTiles trae `transportation_name` (nombre y `ref` de cada vía) y `water_name` como **líneas**. Un rótulo de punto no sirve: hay que
escribir siguiendo la calle. `VectorTileRenderer` coloca el texto **letra a letra** sobre la línea (posición y ángulo de cada letra por la
longitud recorrida), lo prueba en el centro de la línea y, si no cabe o choca, a un tercio y a dos tercios. Lo da la vuelta si la línea va
«hacia atrás» para que se lea de izquierda a derecha, y lo descarta si la línea dobla más de 35° entre dos letras (se leería mal) o si
alguna letra se sale de la tesela. Es la misma regla de los rótulos de punto: mejor no ponerlo que cortarlo. Una calle se rotula una vez por
tesela, con lo que en una calle larga el nombre se repite cada ~256 px, como en cualquier mapa. Los rótulos se ordenan por importancia
(lugares, aguas y aeropuertos; luego calles; luego cimas, POI y números de portal): el primero que llega se queda el sitio.

### 2. Las capas que trae el MBTiles, como capas

En vez de un interruptor solo para las calles, cada regla del estilo puede pertenecer a un **grupo** (`"group"`), con su título en
`"groups"`. El estilo por defecto trae siete: Nombres de calles, Nombres de lugares, Puntos de interés, Edificios, Números de portal (nuevo,
`housenumber` a z17), Cimas (nuevo, `mountain_peak`) y Límites administrativos. El agua, las vías y los usos del suelo no tienen grupo:
son el mapa. `"hiddenGroups"` en el dataset da el estado de partida; `MapWidget::baseLayerGroups` / `setBaseLayerGroupVisible` lo
cambian en caliente, y en la demo salen como casillas debajo de la capa base vectorial.

Cambiar un grupo obliga a repintar las teselas de esa capa sin mezclar las viejas con las nuevas. `TileService::setVectorHiddenGroups`:
- **No toca el renderizador en uso**: varios hilos del pool pueden estar pintando con él. El `TileLoader` pone uno NUEVO, copia del anterior
  con el estilo y la máscara ya cargados, y el viejo muere cuando nadie lo usa.
- **Vacía la caché** de la capa.
- **Descarta lo que esté en camino**: lo que llegue de peticiones anteriores se tira al llegar (`m_discardBefore`), aunque ya estuviera en
  la cola de señales.

### 3. Mapa híbrido: el vectorial encima de cualquier base

El motor tenía UNA capa de teselas. Para poner el vectorial encima del satélite (o del relieve, u OSM) hacía falta una segunda, y se hizo
**reutilizando todo lo que ya había** en vez de inventar un camino nuevo:
- **Un segundo `TileService`** con el MBTiles como única capa: hilo, caché, planificador y escalera de respaldo propios. Cambiar de base no
  toca el overlay, y un overlay lento no frena la base.
- **Un segundo `TileLayer`** en su propia capa de QCustomPlot (`tileoverlay`: encima del relieve, debajo de las entidades), con color de
  «sin datos» transparente: donde no hay overlay se ve la base. `MapView::requestVisibleTiles` y `refreshPlan` piden y planifican los dos.
- **Estilo híbrido** (`VectorStyle::hybrid`; `TileDataset::vectorOverlay` lo elige por defecto). Lleva fondo transparente, sin rellenos ni
  máscara, carreteras finas semitransparentes y rótulos blancos con halo oscuro, que se leen sobre una foto. POI y números de portal
  arrancan apagados (`"visible": false` en `"groups"`) porque sobre el satélite tapan demasiado.
- **El nivel de fondo del overlay es su último zoom.** Con el de una base (z0) aparecían, mientras cargaba, rótulos de una tesela mucho más
  gruesa ampliados; con texto eso se ve fatal. Así solo se usa la del nivel de encima. Más allá del zoom máximo del MBTiles (si la base
  llega a z19) se amplía su última tesela, como con cualquier capa.

API: `MapWidget::setVectorOverlay(fichero[, error, estilo])`, `clearVectorOverlay`, `setVectorOverlayVisible`, `vectorOverlayGroups`,
`setVectorOverlayGroupVisible` y la señal `vectorOverlayChanged`; en `mapa.json`, `"vectorOverlay": { "filePath", "vectorStyle",
"hiddenGroups", "visible" }` (lo lee `DataPackage`, viaja con el paquete). Demo: **Datos → Mapa híbrido…** / **Quitar el mapa híbrido**, y
en el panel, «Híbrido: …» en Overlays con sus grupos. `vector_tile --hybrid`, `--hide` y `--print-style --hybrid`
(`docs/estilo_hibrido.json`).

### Comprobado

- Vistas de La Habana a z15 y z16 con nombres de calles (horizontales, verticales y en diagonal, sin cortes) y el overlay híbrido sobre
  una base ráster oscura (que imita un satélite) con `render_map --data` y `"vectorOverlay"` en el paquete.
- `tst_vectortiles` (24 casos): grupos del estilo (orden, títulos, apagados de partida), un grupo oculto no se pinta, el estilo híbrido es
  transparente, el nombre sigue la línea (caja ancha en una calle horizontal, alta en una vertical, nada si no cabe), el `TileLoader`
  cambia los grupos en caliente, y `"vectorOverlay"` en el paquete (y el aviso si falta).
- `tst_mapwidget::hybridOverlayOverAnyBase`: sobre una base ráster gris, el overlay se ve, se apaga por grupo y entero, vuelve, se quita, y
  un MBTiles ráster no sirve de overlay. En Qt 5 la base sola deja una costura de 1 px entre teselas (no es del overlay): el test compara
  con la base sola en vez de con cero.
- Qt 6.4 y Qt 5.15: 31 tests en verde, sin avisos nuevos.

**Sin probar:** sobre un satélite de verdad (aquí solo hay una base ráster oscura sintética) y en el PC del usuario.

**Estado: 31 tests (28 sin QCustomPlot), Qt 6.4 y Qt 5.15.**

### 165 (ajuste). Fuera los puntos de interés y los números de portal

El usuario, probándolo en su PC: «quita los puntos de interés o dime cómo agrego alguno, quita también los números de portal… quitar
significa quitar de la lista, no muestran nada ahora mismo, no tienen datos». En el híbrido arrancaban apagados y, encendidos, a los zooms
en que él mira no salía nada: los POI empezaban en z15 y los números de portal en z17. Una casilla que no hace nada estorba, así que se
quitan **de los dos estilos por defecto**, grupo y reglas. Quedan cinco grupos en la base (Nombres de calles, Nombres de lugares,
Edificios, Cimas y Límites) y cinco en el híbrido (Carreteras y calles, Nombres de calles, Nombres de lugares, Cimas y Límites). Para quien
quiera algunos POI concretos (hospitales, policía, gasolineras…), el README explica la regla de estilo propio, con su grupo y su filtro por
`class`: el MBTiles los trae.

De paso, `"visible": false` en un grupo del estilo ya vale también para una capa **base** vectorial, no solo para el overlay. Antes lo
aplicaba solo `MapWidget::setVectorOverlay`, y una base lo ignoraba. Ahora lo hace `TileService` al registrar cualquier capa vectorial sin
`"hiddenGroups"` propio (`TileDataset::vectorHiddenGroupsSet` distingue «no lo dijo» de «lo dijo vacío»). Un caso más en
`tst_vectortiles`. 31 tests en verde.

Al comprobar el ejemplo del README para añadir POI (hospitales y policía con un estilo propio) apareció un fallo de `vector_tile` que venía
de §163: **`--style` no funcionaba nunca**. Qt se queda con `-style`/`--style` (es su opción para el estilo visual de la aplicación) antes de
que la vea `QCommandLineParser`, así que la herramienta pintaba siempre con el estilo por defecto sin decir nada. La opción pasa a ser
`--style-file`. Con ella, el ejemplo pinta 717 píxeles rojos de hospitales y comisarías en el Vedado y Centro Habana. A la librería no le
afectaba: `"vectorStyle"` va en el JSON, no por la línea de comandos.

## 166. «¿Qué hay aquí?»: las entidades del MBTiles vectorial bajo un punto

Primera de las ideas aparcadas en `PENDIENTES.md`. Hasta ahora una tesela vectorial solo servía para **pintar**: se decodificaba, se
convertía en píxeles y la información de cada calle o edificio se perdía. Pero el MBTiles de OpenMapTiles trae mucho más de lo que se ve
(tipo de vía, superficie, altura del edificio, nombre, clase del lugar…), y para «qué es esto que acabo de pinchar» hace falta la consulta
inversa: dado un punto, qué entidades hay debajo.

**Cómo se hace** (`src/vector/FeatureQuery.{h,cpp}`, solo núcleo, sin Gui):
- `featurequery::query(tile, key, lat, lon, radiusM)` pasa el punto a coordenadas de la tesela (la misma proyección Web Mercator de
  siempre) y mide contra cada entidad de cada capa: distancia a un punto, a la polilínea más cercana, o 0 si cae **dentro** de un polígono.
- Los polígonos MVT traen los huecos como anillos de área negativa a continuación de su exterior; se evalúan por grupos para que el patio
  de un edificio no cuente como «dentro» pero tampoco anule al edificio vecino. Un punto en un patio queda a la distancia de su borde.
- El radio va en **metros** y la conversión usa el tamaño real de la tesela a esa latitud (Mercator estira con 1/cos), no unidades de
  tesela: a z12 una unidad son ~2,2 m y a z16 ~0,14 m, y el mismo radio en píxeles abarca cosas distintas.
- Orden: primero los polígonos que contienen el punto, luego por cercanía; a igualdad (±0,5 m), puntos antes que líneas y líneas antes que
  polígonos, que es lo más específico arriba.

**API pública:** `MapWidget::vectorFeaturesAt(position, radiusMeters = -1)` devuelve `QVector<VectorFeatureInfo>` (capa, geometría
`point`/`line`/`polygon`, distancia, atributos, y si vino del overlay híbrido). Con radio negativo usa ~8 px al zoom actual. Mira primero el
overlay (si está visible) y luego la base, y solo las que son vectoriales. Si el zoom actual supera el máximo del MBTiles (se está
ampliando) baja hasta el nivel más alto que tenga la tesela: la geometría es la misma, menos detallada. Abre su propia conexión SQLite con
un id distinto del de los hilos de carga (las conexiones no se comparten entre hilos) y bloquea lo que tarde una lectura más un
decodificado: es para un clic, no para un bucle.

**Dónde se ve:**
- Demo: clic derecho en el mapa → **«¿Qué hay aquí?»** abre una ventana con cada entidad (nombre o clase como título, capa, geometría,
  distancia y todos sus atributos; se omiten las traducciones `name:xx`, que solo hacen ruido).
- Consola: `vector_tile --mbtiles f.mbtiles --at lat,lon [--zoom 16] [--radius 15]` lista lo mismo, para comprobar un MBTiles o afinar el
  radio sin abrir la demo.

**Pruebas:**
- `tst_vectortiles::queryFindsWhatIsHere`: un edificio con patio, una calle con nombre y un hospital puestos a mano en una tesela z12;
  dentro del edificio (distancia 0 y su `render_height`), en el patio (vacío con radio corto; con radio largo, a la distancia del borde),
  junto a la calle (con su nombre, a ~11 m), junto al hospital, lejos de todo, y el orden con radio grande. Al escribir la prueba salió un
  detalle del formato que conviene recordar: el `MoveTo` de cada anillo de un polígono es **relativo al cursor** tras el anillo anterior,
  no a (0,0); el patio mal codificado se leía en otro sitio de la tesela y la consulta daba «dentro» de forma legítima.
- `tst_mapwidget::hybridOverlayOverAnyBase` (ampliado): sobre el overlay de prueba, la frontera bajo el centro de una tesela sale con su
  `admin_level`, marcada como del overlay; un 10 % de tesela más arriba no sale; sin radio usa ~8 px; con el overlay apagado y una base
  ráster no hay nada.
- Qt 6.4: 31 tests en verde.

**Sin probar:** con el `cuba-osm.mbtiles` real y en el PC del usuario (aquí no está el fichero). Queda abierto, y es lo que sigue en
`PENDIENTES.md`, enseñar el resultado en una tarjeta al **pinchar** (clic izquierdo) en vez de por el menú, y los grupos de POI por tema.

**Estado: 31 tests (28 sin QCustomPlot).**

### Alcance de B desde A en `rutas_tierra`

Se pidió ver, desde el punto A, hasta dónde puede estar B. El límite real es `kMaxRouteTiles` (3000 teselas z14): `findRoute` lee el rectángulo origen-destino con margen y, si pasa del tope, da «ruta demasiado larga». Para no duplicar esa cuenta en la app, el cálculo del rectángulo se sacó a un ayudante único (`rectFor`) que usan `findRoute` y la función nueva `routeplanner::routeReach` / `MapWidget::routeReach(from, bearings = 72)`: por cada rumbo busca por bisección la distancia máxima que cabe (las teselas crecen siempre con la distancia). Devuelve el contorno, que **no es un círculo**: en línea recta llega a ~140 km y en diagonal a ~110 km a la latitud de Cuba. El ejemplo lo dibuja en verde discontinuo (capa «Alcance de B», bajo la ruta) en cuanto se pone A, lo redibuja al arrastrar A y lo explica en el panel. Solo promete que no saldrá «demasiado larga», no que haya camino. Prueba: `routeReachContour` en `tst_vectortiles` y una comprobación más en `rutas_tierra --comprobar` (al 90 % del borde no da «demasiado larga», al 120 % sí).

### 166 (ajuste). La ventana «¿Qué hay aquí?» en lenguaje llano

El usuario, probándola en su PC con el MBTiles de Cuba: no le gustó cómo se veía. Con razón: era un volcado de atributos crudos (`class`,
`oneway`, `brunnel`, `subclass`, en inglés), una tabla por entidad con huecos enormes, y la misma calle repetida una vez por tramo. Ahora:
- Una línea por cosa: **título** (el nombre o, si no tiene, su tipo traducido: «Carretera principal», «Zona residencial», «Hierba») y debajo, en
  gris, categoría y detalles («vía · sentido único · puente · a 120 m»). Traduce lo habitual de OpenMapTiles (vías, usos del suelo, terreno,
  agua, POI, lugares); lo que no conoce lo deja tal cual.
- Dos bloques: **Aquí** (lo que contiene el punto: el edificio, la zona) y **Cerca** (calles y lugares a unos metros).
- Los tramos de una misma calle se agrupan («2 tramos»), conservando el más cercano.
- Los atributos crudos del MBTiles quedan tras la casilla «Mostrar todos los atributos del mapa».
Es solo la demo (`Ventana::queHayAqui`); la API (`vectorFeaturesAt`) no cambia. Compila; no lo he visto en pantalla con datos reales.

### 166 (ajuste 2). «¿Qué hay aquí?» también en el clic izquierdo, como tarjeta

Tras ver la ventana rehecha, el usuario aceptó la propuesta de una **tarjeta pequeña junto al cursor**, como la de los objetivos, enganchada al
clic izquierdo. Ahora un clic izquierdo en un lugar **vacío** (navegando, sin captura de punto en curso y sin nada elegible debajo) abre una
tarjeta con hasta 4 líneas de lo más relevante del punto y un botón «Todo… (N más)» que abre la ventana completa de antes (la del clic
derecho). Decisiones:
- Si no hay nada vectorial cerca, o la base no es vectorial, **no sale nada**: un clic en el vacío no abre cosas sin motivo.
- Se espera lo que tarda un doble clic (`QApplication::doubleClickInterval`) y se cancela si el zoom cambió, porque el doble clic en vacío acerca
  el mapa y la tarjeta saldría encima del zoom.
- Sobre un elemento (objetivo, entidad, marca) sigue saliendo su tarjeta de siempre; esta solo cubre el vacío.
- La composición del texto (agrupar tramos, «Aquí»/«Cerca», traducciones) es una sola, compartida por la tarjeta y la ventana.
Solo la demo; la API no cambia. Compila y los 31 tests siguen en verde; sin ver en pantalla con datos reales.

### 166 (ajuste 3). La tarjeta del clic izquierdo se cierra sola

Petición del usuario: que la tarjeta dure **5 segundos** y se cierre si el ratón sale de ella. Un temporizador de 5 s y otro de 100 ms que
comprueba la posición del cursor con un margen de 30 px alrededor de la tarjeta (sale 12 px en diagonal del cursor, así que sin margen se
cerraría al nacer). Se vigila la posición en vez de esperar un evento de salida porque un `Qt::Popup` captura el ratón y el «leave» no es
fiable. Los 5 s no se pausan con el cursor encima: si hace falta más tiempo, «Todo…» abre la ventana completa, que no caduca.

## 167. Puntos de interés por tema en los estilos por defecto

Segunda de las ideas aparcadas en `PENDIENTES.md`. En §165 se quitaron los POI de los estilos porque la única casilla que había no mostraba nada
útil (empezaban en z15 y salían todos mezclados). Ahora vuelven, pero **por tema**, y apagados de partida.

**Qué hay:** cinco grupos nuevos en el estilo de base y en el híbrido, cada uno con su color y su símbolo, visibles desde z13 y con el nombre al lado:
Salud (cruz roja: `hospital`, `doctors`, `pharmacy`, `dentist`, `clinic`, `veterinary`), Seguridad (cuadrado azul: `police`, `fire_station`), Combustible
(rombo naranja: `fuel`, `charging_station`), Alojamiento (triángulo morado: `lodging`, `campsite`) y Transporte (círculo verde: `bus`, `railway`,
`ferry_terminal`, `harbor`). Arrancan apagados (`"visible": false` en el grupo), así que un mapa que no los pide se ve igual que antes; cada uno
sale como una casilla más en la demo, debajo de la capa. En el híbrido van con colores claros y halo oscuro, para leerse sobre satélite.

**Símbolo propio por tema:** hasta ahora un punto solo podía ser un círculo. Se añade `"pointShape"` a las reglas (`circle`, `square`, `triangle`,
`diamond`, `cross`; una forma desconocida se queda en círculo), con `drawMarker` en el renderizador. Todas menos el círculo llevan un contorno fino del
color del halo para que se lean sobre cualquier fondo. Así se distingue de un vistazo un hospital de una gasolinera, que con puntos del mismo tamaño
no se podía.

**Por qué por temas y no una sola casilla «Puntos de interés»:** el MBTiles de Cuba trae cientos de POI por tesela (427 en una z14 del centro de La
Habana). Encenderlos todos juntos tapa el mapa; por tema, el usuario enciende lo que busca. Las clases son las de OpenMapTiles; quien necesite otras
tiene en el README la receta del estilo propio, ahora con `pointShape`.

**Pruebas:** `tst_vectortiles::poiGroupsAndShapes` (los dos estilos traen cinco grupos poi_* apagados, cada uno con forma y color distintos y visibles
desde z14 como mucho; las cinco formas se leen del JSON y se dibujan de verdad, sin dos iguales y con la geometría esperada en puntos concretos). Se
actualizaron los tests que listaban los grupos de los estilos (`styleGroups`, `hybridStyleIsTransparent`, `hybridOverlayOverAnyBase`). 31 tests en
verde en Qt 6.4. `docs/estilo_vectorial.json` y `docs/estilo_hibrido.json` se regeneraron con `vector_tile --print-style`.

**Sin probar:** con el `cuba-osm.mbtiles` real. Las clases y el zoom de partida salen de la documentación de OpenMapTiles y de lo que ya se vio en §165;
si alguna clase no existe en ese fichero, su grupo se queda vacío sin error. Solo salen los POI con nombre: las paradas de autobús sin nombre no se
ven. Queda abierto (PENDIENTES): buscar POI por nombre o tipo y sacar una selección como entidades.

**Estado: 31 tests (28 sin QCustomPlot).**

## 168. Buscar puntos de interés por nombre o tipo

Tercera de las ideas aparcadas. Con los grupos de §167 se *ven* los POI, pero no se podían *buscar*: «¿dónde hay una farmacia por aquí?». El campo de
búsqueda de la barra (Ctrl+K) solo conocía los lugares del `lugares.sqlitedb` (ciudades, bahías, picos). Ahora también busca sitios.

**Decisión de diseño: sin índice aparte.** `PENDIENTES.md` hablaba de sumarlos al `lugares.sqlitedb`. Se descartó: obligaría a generar y mantener otra base
de datos por cada MBTiles (con el de Cuba, cientos de miles de POI) y los POI ya están en las teselas. En su lugar se lee la capa `poi` de las teselas
que rodean el centro del mapa, así que sirve cualquier MBTiles de OpenMapTiles sin preparar nada. El coste es que solo busca **cerca del centro**
(6 km por defecto, hasta 15 km), no en toda la provincia; para eso haría falta el índice, y queda anotado como posible paso futuro.

**Cómo funciona** (`src/vector/PoiSearch.{h,cpp}`, solo núcleo):
- `MapWidget::searchPointsOfInterest(texto, límite, radio)` calcula las teselas z14 (o el máximo del MBTiles) que cubren el radio (como mucho 9×9),
  pide a SQLite **de una sola consulta** el rectángulo de las que faltan, y guarda lo leído en una caché (por fichero y tesela; vacía también cuenta,
  para no volver a pedir teselas sin POI). Solo la primera búsqueda en una zona lee; teclear después es inmediato.
- `mvt::decode` gana una variante con lista de capas: se salta las demás sin decodificar sus entidades. Una tesela de ciudad trae miles de edificios y
  vías, y para buscar POI solo hace falta `poi`; es la diferencia entre decenas de milisegundos y uno por tesela.
- `poisearch::inTile` saca los puntos con su coordenada geográfica y descarta los del margen de la tesela (los trae la vecina, y si no se duplicarían).
  El nombre sale de `name:es`, `name` o `name:latin`.
- `poisearch::search`: cada palabra del texto tiene que estar en el nombre **o** en el tipo. El tipo se busca en español (tabla de clases y subclases con
  su traducción), en el inglés original (`pharmacy`) y en palabras de uso común que no son traducción literal («cupet», «servicentro», «botica»,
  «guagua», «paladar»). Así «farmacia» da las farmacias **aunque no tengan nombre** (muchas no lo tienen en OSM), y «hospital calixto» exige las dos
  cosas. Orden: los que empiezan por lo escrito, luego los que lo contienen en el nombre, luego los que casan solo por el tipo; dentro de cada grupo,
  más cercanos al centro primero. Con menos de dos letras no devuelve nada (una «a» casaría con todo).
- API pública: `PointOfInterest` (nombre, tipo, subtipo, coordenada, distancia, `typeLabel()` en español, `displayName()`), `canSearchPointsOfInterest()`,
  `searchPointsOfInterest` y `goToPointOfInterest`.

**En la demo:** el mismo campo de la barra, con el desplegable en dos tandas: los lugares primero («Guantánamo — ciudad») y los sitios después
(«Farmacia Central — farmacia · a 1,2 km»). Elegir uno centra el mapa a z17 y lo dice en la barra de estado. El campo se activa si hay índice de lugares
**o** mapa vectorial (se reevalúa al cambiar de base o de capa híbrida), y su ayuda dice cuál de las dos cosas falta.

**Pruebas:** `tst_vectortiles::decodeOnlyChosenLayers` (la variante por capas, y que un MVT roto siga dando error), `searchesPointsOfInterest` (cuatro POI
puestos a mano: nombre sin tildes, `name:es`, tipo en español e inglés, «cupet», nombre + tipo, orden por grupo y cercanía, límite, texto corto, el
punto del margen y la línea que no cuentan) y `tst_mapwidget::searchesPointsOfInterest` (de extremo a extremo con un MBTiles: sin capa vectorial no
hay donde buscar; con el overlay sí; la sin nombre sale tras la que lo tiene; la caché da lo mismo; fuera del radio no sale; ir al POI centra el mapa).
En el camino salió que las pruebas de §166 daban tres avisos `-Wconversion` (`qsizetype` a `int` en `FeatureQuery.cpp`), corregidos.
31 tests en verde en Qt 6.4.

**Sin probar:** con el `cuba-osm.mbtiles` real (tiempo de la primera búsqueda en el centro de La Habana, con cientos de POI por tesela) ni en el PC del
usuario. El rendimiento se estimó, no se midió con datos reales. La tabla de tipos cubre las clases habituales de OpenMapTiles; las que no conoce se
buscan y se muestran con su nombre en inglés.

**Estado: 31 tests (28 sin QCustomPlot).**

## 169. Sacar los POI de una zona como entidades

Última de las ideas aparcadas de los POI: «todos los hospitales de la provincia». Con §168 se *buscan* sitios cerca del centro; aquí se
*convierten en entidades* para trabajar con ellos como con cualquier otra cosa del mapa (filtrar, editar, guardar, exportar).

**Qué hace:** `MapWidget::importPointsOfInterest(zona, texto, capa)` lee los POI de las teselas que cubren un polígono, se queda con los que están
**dentro** del polígono y casan con el texto (vacío = todos) y los añade como puntos a una capa, que se crea si no existe. Cada punto lleva el nombre
(o el tipo si no tiene), `type` = la clase del MBTiles, el color de su tema (`poisearch::themeColor`, el mismo de los grupos `poi_*` de los estilos
de §167, para que el punto y el símbolo del mapa se reconozcan) y los atributos `clase`, `subclase`, `tipo` (español) y `origen`. Como son entidades
normales, se exportan con `exportVectorFile` a GeoJSON/KML/GPX (lo de «exportarlas» del plan no necesitó código nuevo) y se editan, filtran y guardan.

**Decisiones:**
- **Zona como polígono.** Un rectángulo son sus cuatro esquinas; así sirve igual la herramienta «Seleccionar área» que «Seleccionar polígono».
- **Tope de lectura** (`kMaxPoiImportTiles` = 5000 teselas z14, unos 24.000 km² a latitud de Cuba): una provincia grande entra, el país entero no. Se
  cuenta ANTES de leer y, si se pasa, devuelve `tooLarge` con cuántas teselas abarca, en vez de pasarse minutos leyendo. Se cuentan las teselas del
  rectángulo que envuelve el polígono, no las del polígono: un polígono muy diagonal cuenta de más.
- **Fila a fila y sin caché**: una consulta SQLite por fila de teselas, así que la memoria no crece con la zona; la caché de §168 (800 teselas) se
  desbordaría y una importación no se repite. Solo se guardan los POI que casan.
- **Progreso y corte**: el callback recibe (filas hechas, totales) y si devuelve `false` se corta. Un corte **no deja nada a medias**: no se añade
  ninguna entidad.
- **No duplicar**: un POI ya presente en la capa (mismo tipo, nombre y sitio a ~1 m) se cuenta como `skipped`. Se puede repetir o ampliar («farmacia» y
  luego todos) sin ensuciar la capa.
- **Tope de entidades** (`maxFeatures`, 5000): el resto se cuenta en `found` y se marca `truncated`.
- `poisearch::filter`: la misma regla de la búsqueda (todas las palabras en nombre o tipo) pero sin ordenar, sin tope y sin mínimo de letras;
  `search` ahora comparte con él la función `casa`.

**En la demo:** al marcar una zona (Datos → Seleccionar área o polígono) la ventana «Zona seleccionada» trae el botón **«Sitios de la zona…»**: pide
qué sitios y la capa, lee con barra de progreso cancelable, deja la capa como activa (así «Datos → Exportar capa activa» la saca a GeoJSON/KML/GPX
directamente) y lo cuenta en la barra de estado. Si la zona es demasiado grande lo dice con las teselas que abarca.

**Pruebas:** `tst_vectortiles::filtersPointsOfInterest` (vacío = todos, sin mínimo de letras, orden de entrada, y que el color por tema de cada clase
coincide con el de la regla `poi_*` del estilo) y `tst_mapwidget::importsPointsOfInterestAsFeatures` (de extremo a extremo con un MBTiles de una sola
tesela z14: los errores —sin mapa vectorial, zona sin área, sin capa—, las dos farmacias como puntos con tipo, color y atributos, repetir sin duplicar,
ampliar, el tope, cortar a medias sin añadir nada, zonas sin POI o que el polígono recorta, y zona demasiado grande). 31 tests en verde en Qt 6.4.

**Sin probar:** con el `cuba-osm.mbtiles` real: cuánto tarda una provincia (la estimación es de unos segundos por cada mil teselas) ni en el PC del
usuario. No hay prueba automática del cuadro de diálogo de la demo (compila). Con esto no queda nada de los POI en `PENDIENTES.md`.

**Estado: 31 tests (28 sin QCustomPlot).**

## 170. Búsqueda de calles sin conexión

Primera de «Otras ideas del mismo MBTiles». Con §168 se buscan sitios; faltaba «Obispo»: escribir el nombre de una calle y que el mapa vaya a ella.
Esta rama sale de `main` sin esperar a #35 (importar POI como entidades) porque no depende de ella; si #35 se fusiona antes, `BITACORA.md`
(§169 y esta) y las filas de `CLAUDE.md` chocan en texto y se resuelven manteniendo las dos.

**Qué hace:** `MapWidget::searchStreets(texto, límite, radio)` lee la capa `transportation_name` (la que lleva los nombres de las vías en OpenMapTiles)
de las teselas z14 que rodean el centro, como §168 hace con los POI, sin índice aparte. `goToStreet` la encuadra entera, o la centra a z17 si mide menos
de 150 m. En la demo, el mismo campo de la barra (Ctrl+K) muestra tras los lugares y los sitios las calles («Obispo — calle · a 1,2 km»); al elegir una
se resalta con una línea naranja en una capa temporal («Búsqueda», no se guarda ni se elige con el ratón) y se encuadra. El resalte se quita al vaciar
el campo.

**Lo difícil: una calle no es un tramo.** Las teselas parten las calles. Una avenida larga llega como decenas de trozos, uno por tesela (y por cada
cruce), con un margen que hace que los trozos de teselas vecinas se solapen unos metros. Y «Calle 5» no es una calle: hay una en cada barrio.
`streetsearch::search` agrupa los tramos por nombre y, dentro de cada nombre, **une los que se tocan** a menos de 15 m (vértices de uno contra los
segmentos del otro, con descarte por cajas) con un union-find. Así una avenida cortada sale como UNA calle con todos sus trozos, y dos «Calle 5»
lejanas salen como dos, la más cercana primero. 15 m cubre el solape de las teselas y los cortes por una intersección; una calle con un hueco de
más de 15 m se vería como dos. El «punto más cercano» de cada calle (a donde se va) es el de su trazado más próximo al centro, no su mitad.

**Cómo se casa el texto:** cada palabra (sin tildes ni mayúsculas) tiene que estar en el nombre o en la referencia (`ref`: «CC», «A1»; así se encuentran
carreteras sin nombre). Las genéricas «calle», «avenida», «carretera», «calzada», «paseo», «camino»… son **opcionales**: «calle obispo» encuentra «Obispo»
aunque el MBTiles no ponga «Calle» en el nombre (OSM en Cuba a menudo no lo pone). Si lo único que se escribe es una genérica, se busca esa palabra.
Orden: las que empiezan por lo escrito, luego las que lo contienen; dentro de cada grupo, las más cercanas. Menos de dos letras: nada. Las vías sin
nombre ni referencia y los puntos de la capa no cuentan.

**Refactor:** la lectura de teselas alrededor del centro (rectángulo, una consulta SQLite por zona, caché, solo una capa decodificada) estaba escrita
para los POI; pasa a una plantilla `itemsAround` que usan los POI y las calles, cada uno con su caché. `Street::typeLabel()` traduce los tipos de vía
(autopista, vía troncal, carretera principal/secundaria/terciaria, calle, vía de servicio, camino, sendero…).

**Pruebas:** `tst_vectortiles::searchesStreets` (tres teselas a mano: «Obispo» cortada en dos teselas que se tocan = una calle de dos partes; dos «Calle 5»
lejanas = dos, ordenadas; palabra genérica opcional y solo-genérica; por referencia; tildes y mayúsculas; empiezan-por antes que contienen; límite; texto
corto; los puntos y las vías sin nombre no cuentan) y `tst_mapwidget::searchesStreets` (de extremo a extremo con un MBTiles: tres teselas de la misma fila
forman una calle de tres partes y las filas lejanas no entran; sin capa vectorial no hay donde buscar; poco texto; lejos no sale; ir a la calle la encuadra y
una calle corta se centra a z17). 31 tests en verde en Qt 6.4.

**Sin probar:** con el `cuba-osm.mbtiles` real: que `transportation_name` traiga a z14 todas las calles con nombre (en OpenMapTiles las menores salen entre
z12 y z14, según su clase) y cuánto tarda la primera búsqueda. Si en z14 faltaran calles menores, habría que subir el zoom de lectura (z15, el doble de
teselas por lado). El resalte de la demo solo se ha compilado, no visto. Solo busca cerca del centro (6 km por defecto, 15 km como máximo), no en toda la
ciudad: para eso haría falta un índice de calles, igual que con los POI.

**Estado: 31 tests (28 sin QCustomPlot).**

### 170 (ajuste). El buscador dice cuándo no mira donde está el usuario

El usuario escribió «Obi» a zoom 10 con el centro del mapa en el mar, al sur de Artemisa: salieron cinco lugares del índice y ninguna calle ni sitio, y
pareció que «Obispo» no existía. Sí existe, pero a unos 30 km del centro, y sitios y calles solo se buscan alrededor del centro (6 km). Nada en pantalla lo
decía, y es lo que dije que pasaría en «Sin probar»: en uso real se tropieza a la primera. Dos cambios en la demo, sin tocar la API:
- El radio sigue al zoom: lo que se ve de ancho (metros por píxel × ancho / 2), entre 6 y 15 km. A zoom lejano mira más, a zoom cercano se queda en 6.
- Si no sale ni un sitio ni una calle, el desplegable lo dice («sin sitios ni calles a 15 km del centro: acerca el mapa a la zona»), en una fila que es solo
  un aviso (elegirla no hace nada).
Con el MBTiles de Cuba y 15 km sigue sin llegar de Artemisa a La Habana desde el centro del mar. La solución de verdad, buscar en todo el mapa, es un índice de
calles y sitios del MBTiles entero, generado una vez y consultado como el de lugares; no está hecho.

## 171. Rutas sin conexión por la red vial (coche)

Última de «Otras ideas del mismo MBTiles» y, como se anotó, la más costosa. Dos decisiones del usuario acotaron la primera versión: la red vial se
**lee de las teselas al vuelo** (sin preparar ningún fichero) y el alcance es **dos puntos → ruta con distancia y tiempo, solo en coche**. Esta rama sale de
`main` sin esperar a #36 (búsqueda de calles) porque no depende de ella; `BITACORA.md` (§170 de #36 y esta) y `CLAUDE.md` chocarán en texto y se
resuelven manteniendo las dos entradas.

**Cómo funciona:**
- `routeplanner::findRoute` (`src/vector/RoutePlanner`) calcula las teselas z14 del rectángulo que une origen y destino con un margen (3 teselas o un cuarto de
  lo que mida), las lee **fila a fila** (una consulta SQLite por fila, decodificando solo la capa `transportation`) y arma el grafo. Tope de 3000 teselas
  (unos 14.000 km² a latitud de Cuba): si se pasa, lo dice antes de leer. Si no hay camino, repite una vez con el margen triplicado (un camino que rodea
  una bahía queda fuera del primer rectángulo), siempre que quepa.
- `roadgraph::Graph` (`src/vector/RoadGraph`): un nodo por vértice y una arista por cada par consecutivo, con la clase de la vía y su tiempo
  (longitud / velocidad media: autopista 90, troncal 80, principal 60, secundaria 50, terciaria 40, calle 30, servicio y camino 15 km/h; sendero, ferri y
  tren no valen para coche). A* con la distancia en línea recta a 90 km/h como heurística, que nunca sobrestima: la ruta es la de menor tiempo del grafo.

**Lo difícil, y lo que salió mal al probar:**
- **Coser las teselas.** Cada tesela redondea sus coordenadas a su propia rejilla (a z14 unos 0,6 m) y trae un margen que repite lo de la vecina, así que
  una misma vía llega con vértices que no coinciden. El grafo **suelda** los vértices a menos de 1,5 m, y a menos de 4 m si son extremos de una vía
  (donde se juntan las calles). Hay una prueba con dos teselas cuyas coordenadas del borde difieren una unidad.
- **Un puente no se cruza con lo de abajo.** Cada nodo recuerda los niveles que pasan por él (suelo, puente, túnel) y no se suelda un puente con una calle
  que pasa por debajo, salvo en los extremos del puente (donde se une con el suelo).
- **Sentido único** (`oneway` 1 / -1) y vías cerradas (`access` = no/private). Una calle de sentido único a la que solo se llega (-1 en un extremo) también es un destino
  válido; al principio el enganche la ignoraba por no tener aristas de salida.
- **Enganchar al punto, no al vértice.** La primera versión enganchaba origen y destino al vértice más cercano. La prueba de extremo a extremo midió 2432 m donde
  debían ser 2102: una carretera recta de 2 km con tres vértices dejaba el origen a 55 m del vértice y el destino a 108 m. Las carreteras rurales tienen
  vértices cada kilómetro, así que era un fallo real, no de la prueba. Ahora se engancha al **punto más cercano de cualquier tramo** y se parte ese tramo en dos con
  nodos virtuales (de salida y de llegada). Si origen y destino caen en el mismo tramo se va directo si el sentido lo permite.
- La escala en metros depende de la latitud: el grafo usa el coseno de la latitud media de origen y destino. Una prueba que armaba la red a 23º con teselas a 24,4º
  se equivocaba un 1 %; el grafo de verdad usa la latitud de la ruta.

**API y demo:** `MapWidget::findRoute(origen, destino, progreso)` devuelve un `RouteResult` (trazado, distancia, tiempo estimado, metros por tipo de vía,
enganche de origen y destino, teselas y nodos), con `error` si no hay ruta (sin mapa vectorial, punto a más de 500 m de toda vía, demasiado largo, vías sin
unir, cancelada). En la demo: clic derecho → *Ruta (coche)* → «Ruta desde aquí» (marca el origen) y «Ruta hasta aquí» (barra de progreso cancelable; dibuja la línea
en la capa «Rutas», con la distancia y el tiempo en el nombre y los km por vía en sus atributos, la encuadra y la deja como capa activa para exportarla con
Datos → Exportar capa activa). `vector_tile --route lat,lon:lat,lon [--geojson f]` hace lo mismo en consola; se probó con un MBTiles sintético de dos vías
(2,10 km, 3 min).

**Límites, dichos sin rodeos:** el tiempo es una **estimación** por la velocidad media de cada tipo; las teselas de OpenMapTiles no traen giros prohibidos ni límites
de velocidad reales ni semáforos. No hay instrucciones giro a giro (los nombres están en otra capa). Y solo coche.

**Pruebas:** `tst_vectortiles::roadGraphRoutes` (elige lo más rápido y no lo más corto, sentido único en los dos sentidos, vías cerradas y senderos, puente sin soldar con
lo de abajo, enganche a vía recta larga con y sin sentido único, errores con motivo), `roadGraphWeldsTileSeams` (dos teselas con el borde desajustado se sueldan; sin soldar no
habría ruta; `oneway`, `access` y `brunnel` se leen de la capa) y `tst_mapwidget::findsARoute` (de extremo a extremo con un MBTiles z14: la distancia es la esperada, las dos vías
por tipo, el progreso, la vuelta mide lo mismo, punto lejos de la vía, cancelada, sin capa vectorial y demasiado larga). 31 tests en verde en Qt 6.4.

**Sin probar:** con el `cuba-osm.mbtiles` real: cuánto tarda una ruta de decenas de km (el grafo de una provincia tiene cientos de miles de nodos; el enganche recorre
todas las aristas, unos milisegundos), y sobre todo si las **intersecciones llegan cosidas**: si el MBTiles simplifica a z14 y pierde el vértice donde una calle toca otra, el
grafo no las une y la ruta falla con «las vías no llegan a unirse». Es lo primero que hay que mirar con datos reales. La demo solo se ha compilado.

**Estado: 31 tests (28 sin QCustomPlot).**

## 172. Ejemplo `rutas_tierra`: probar las rutas por tierra

Petición del usuario: «crea una app de ejemplo en examples para probar rutas de tierra», para probar con datos suyos lo de §171. Se entendió «rutas por tierra» como rutas por la
red vial y, dentro de ellas, **los caminos de tierra** (la clase `track` de OpenMapTiles), que son los que más dudas dan: cuánto de una ruta es de tierra y si el cálculo los usa
como se espera.

**El ejemplo** (`examples/rutas_tierra`, un solo `main.cpp` de ~700 líneas, con su `README.md`, `CMakeLists.txt` y `.pro`, como los demás): dos clics en el mapa ponen A y B, o se escriben
en cualquier formato de coordenada, o se **arrastran los marcadores** y la ruta se recalcula al soltar. La ruta se pinta **por tramos de un mismo tipo de vía**: asfalto en azul y caminos de
tierra en marrón y discontinuos; un panel da la distancia, el tiempo estimado, lo que se anda hasta la vía, la tabla de kilómetros y % por tipo (la fila de tierra resaltada) y un aviso con
cuánta tierra lleva. Barra de progreso cancelable, invertir, limpiar y guardar a GPX/KML/GeoJSON.

**Red de demostración propia.** Probar rutas necesita un MBTiles vectorial con vías, y no todo el mundo lo tiene a mano. El ejemplo **escribe el suyo** en una carpeta temporal (una tesela z14
con la capa `transportation`, codificada a mano en MVT, más el `mapa.json`): un pueblo a cada lado unidos por un camino de tierra casi recto (~1,9 km), una calle de asfalto que da la
vuelta por el sur (~4,6 km), una carretera principal que sube hasta el camino y un ramal de tierra sin salida a una finca. Con `--demo`, o sin paquete de datos, se usa; con un paquete real, se usa ese.
Hay tres rutas de ejemplo en un desplegable. Es el mismo patrón que el terreno de demostración de `analisis_terreno`.

**Cambio en la biblioteca: `RouteResult::legs`.** Para pintar la tierra de otro color, la ruta tenía que decir qué tramo es de qué tipo de vía; hasta entonces solo daba el trazado entero y
los totales por tipo. Ahora `legs` parte el trazado en tramos seguidos de un mismo tipo (`RouteLeg`: tipo, trazado, longitud), cada uno empezando donde acabó el anterior. Al probarlo salió
un fallo: si el origen cae justo en un cruce, el tramo parcial del enganche mide 0 m y abría un tramo «de calle» de 0 m delante de la carretera por la que se iba (la ruta cruce → finca salía
en tres tramos en vez de dos). Las aristas de longitud cero ya no abren tramo, con su caso en `roadGraphRoutes`.

**Prueba sin ventana** (`rutas_tierra --comprobar`): tres rutas de longitud conocida sobre la demo (pueblo oeste → pueblo este: ~1,9 km, todo de tierra, un solo tramo; cruce → finca: carretera
principal y luego tierra, dos tramos seguidos; pueblo oeste → finca: solo tierra), que la vuelta mida lo mismo y la exportación a GPX. Entra en `herramientas/probar_instalacion.sh`, que se ha
corrido entero contra una instalación local (los diez ejemplos). 32 casos de `tst_vectortiles` en verde, 31 tests.

**Límites, sin rodeos:** lo de §171 (tiempo estimado, sin giros prohibidos). Y una cosa más: el ejemplo **no puede evitar** los caminos de tierra, solo mostrar cuánto hay; evitarlos pide
una opción en `findRoute` (anotada en `PENDIENTES.md`). Con datos reales, lo primero que hay que mirar sigue siendo si las intersecciones llegan unidas, y cuántos `track` trae el MBTiles de la zona.
La ventana no se ha visto en pantalla (compila y su prueba sin ventana pasa); el dibujo discontinuo de la tierra es lo primero que hay que mirar.

**Estado: 31 tests (28 sin QCustomPlot).**

## 173. Los 27 tipos AIS en `libtelemetria` (aportación del fork `randoloc`) y un catálogo en el emisor

**Origen.** Al revisar el fork `randoloc/LibMapaStatic` apareció la rama `port-ais-libtelemetria` (un commit, 2026-10-23): pasa el decodificador y el codificador AIS de 7 tipos (1, 2, 3, 5, 18, 19, 24) a
los **27 de ITU-R M.1371**, trasladando la cobertura del `CAisDecoder` de su proyecto Sas-3 a la estructura única `AisMessage` de la biblioteca. El autor abrió ese mismo cambio como el PR #40 y se fusionó en `main` mientras esta rama estaba abierta, así que el port (código, tests, README y `PORT_AIS.md`) llega a `main` por #40 y esta rama se puso al día con un merge. Lo que aporta esta rama encima: el arreglo del tipo 19, el nivel de API 3, el catálogo del emisor y el cliente (abajo), y una sección «Después del port» en `PORT_AIS.md`.

**Qué cambia en la biblioteca.** Los campos nuevos de `AisMessage` son aditivos y no rompen a ningún consumidor: fecha y hora UTC (4 y 11), aeronave SAR (9), ayuda a la navegación (21, con el nombre de 20
caracteres más su extensión), textos de seguridad (12, 14), acuses (7, 13), interrogación (15), modo asignado (16), correcciones DGNSS (17), enlace de datos (20), canales (22), grupo (23), largo alcance (27) y
los binarios (6, 8, 25, 26), de los que se entrega la carga **sin interpretar** (`dac`, `fid`, `binaryData`, `binaryBits`). Más `aisAtonTypeText` y `aisEpfsTypeText`. Un detalle de diseño: la posición de las
correcciones DGNSS (17) es la de la **estación de referencia**, no la del emisor, así que va en `dgnssLat`/`dgnssLon` y `hasPosition()` no la cuenta. Los tipos reservados (0, 28 a 31) siguen devolviendo solo tipo y MMSI.
`TELEMETRIA_API_LEVEL` pasa a **3** y los dos ejemplos de telemetría lo exigen (así una instalación vieja da el mensaje «reinstálala» y no un «no está declarado»).

**Un fallo que apareció al probarlo.** El decodificador del tipo 19 leía `epfsType` y `assigned`, pero `encodeAis` escribía ese bloque a ceros: un 19 con GPS salía del emisor sin fijador. Lo vio el catálogo del emisor
(abajo), no los tests del fork. Ahora se escribe y `roundTripClassB` lo comprueba.

**Lo que NO es.** La verificación independiente de los tipos nuevos son los vectores fijos de `tst_ais`, que el autor generó con un codificador Python escrito desde la tabla de M.1371 y contrastó con el decodificador de
Sas-3 (160 campos coincidentes, 5 diferencias que son fallos del otro decodificador: minuto y segundo de 5 bits en los tipos 4 y 11, y la tabla de textos de las ayudas). **No** son frases publicadas por una fuente
independiente (como el ejemplo de gpsd del tipo 1), que es lo que pide la norma de la biblioteca; sigue pendiente conseguir alguna por tipo. No se ha probado con un receptor AIS real.

**Los ejemplos.** `emisor_nmea` emite ahora, además de los barcos, tres **estaciones fijas** (el faro del Morro y una boya del canal, tipo 21 cada 30 s, y la estación base del puerto, tipo 4 cada 10 s y un aviso
de seguridad, tipo 14, cada minuto) y un **catálogo** de 23 mensajes: un ejemplo de cada tipo y variante que faltaba (direccionado y difundido, con y sin DAC/FID, área y destinos, una ayuda virtual con nombre largo),
uno por segundo cada minuto, por el canal B para que sus identificadores de secuencia no se mezclen con los tipos 5 de los barcos. Cada ejemplo lleva en el código **su comprobación** (qué campos debe conservar), y
`--comprobar` verifica los 23 y que **salen los 27 tipos**. `cliente_telemetria` ahora distingue qué es cada estación: las ayudas (morado, «virtual» si no existen), las bases (gris azulado) y las aeronaves SAR (rojo) son
objetivos aparte, con su tipo y su clase en la tabla; las ayudas, que informan cada ~3 min, se dan por perdidas a los 10 min (a los 60 s un barco); los avisos de seguridad (12 y 14) se anotan en el registro; y los tipos
que no describen a una estación (binarios, acuses, gestión de canales...) se cuentan pero **no crean un contacto vacío**. La demostración (`--demo`) incluye ahora las estaciones fijas.

**Límites.** Del AIS el cliente no interpreta ningún binario de aplicación (el significado de un DAC/FID lo decide una aplicación). El catálogo es ida y vuelta con la propia biblioteca. La ventana del cliente no se ha visto
en pantalla (su prueba sin ventana, sí); los colores y el orden de la tabla son lo primero que hay que mirar.

**Estado: 31 tests (28 sin QCustomPlot).** Verificado: los tres tests de `libtelemetria`, el contrato de instalación (`herramientas/probar_instalacion.sh`, con los diez ejemplos y sus `--comprobar`).

## 174. `vigilar_randy.sh`: aviso cuando cambia `docs/Randy.md`

El usuario pidió un script que revisara `docs/Randy.md` dos veces al día (es el documento de investigación de la carta náutica cubana, que otras personas pueden seguir tocando). Un script no puede
programarse a sí mismo, así que son dos piezas: `herramientas/vigilar_randy.sh` y la línea de planificación que va en su cabecera (cron en Linux, una tarea de Git Bash en el Programador de tareas de Windows,
a las 9:00 y a las 18:00). **No se ha instalado ninguna tarea programada**: eso lo decide el usuario en su máquina.

**Cómo decide que ha cambiado.** Hace `git fetch` de `origin/main` y compara el *blob* de `docs/Randy.md` (el SHA del contenido, no la fecha ni el commit) con el último que vio, guardado en `.git/vigilar_randy.estado`
(dentro de `.git`, así que no se commitea ni ensucia el árbol). Comparar el contenido evita avisar por un merge o un commit que toca otros ficheros. La primera vez solo apunta la versión. Si cambió, enseña los
commits que tocaron el fichero y el diff contra la versión anterior, y apunta la nueva. No toca la rama ni el árbol de trabajo: solo `fetch` y lectura.

**Códigos de salida**, para encadenar un aviso: 0 sin cambios o primera vez, 10 cambió, 11 el fichero ya no existe en el remoto, 1 error (sin red, remoto inexistente). `--sin-guardar` mira sin apuntar, para probar o para
seguir viendo el aviso hasta haberlo leído. `--fichero`, `--rama` y `--remoto` sirven para vigilar otro fichero con el mismo script.

**Probado** en este contenedor, en sus seis caminos: primera vez, sin cambios, cambio (simulado apuntando como «vista» la versión de `Randy.md` del commit `dd498d3`, que sí enseñó los dos commits y las 56 líneas
añadidas después), fichero inexistente, remoto malo y ejecución desde otra carpeta. **Sin probar:** en Windows con Git Bash ni con el Programador de tareas, ni con cron de verdad; y el script **no avisa por sí
solo**: con cron deja el resultado en `.git/vigilar_randy.log`, y un aviso al móvil o al correo habría que encadenarlo a mano.

**Estado: 31 tests (28 sin QCustomPlot).** Es un script, sin código de la librería.
