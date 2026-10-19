# analisis_terreno

Análisis de terreno sobre un paquete de datos de libmapa: **¿se ven dos puntos?** y **¿qué se ve desde uno?**
Arrastras un observador (A) y un objetivo (B) con la altura de su antena y la aplicación dice, en vivo, si hay
**línea de vista**, dibuja el **perfil de elevación** con la visual encima y calcula qué **altura de antena** haría
falta. Desde A calcula la **zona visible** (viewshed 360°) y la dibuja en el mapa. Es la base de un radioenlace,
un puesto de vigilancia, una torre de observación o cualquier cosa que razone sobre el relieve.

Sin elevación en el paquete la app lo dice y ofrece un **terreno de demostración** (una loma de 300 m en una
llanura), de modo que se puede probar sin datos reales.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Cota del terreno en un punto (y bajo el cursor) | `elevationAt` |
| ¿Se ven A y B? Holgura y punto que bloquea | `lineOfSight`, `LineOfSightParams`, `LineOfSightResult` |
| Perfil del terreno entre A y B | `elevationProfile`, `ElevationProfileParams`, `ElevationProfile` |
| Zona visible desde A, con progreso y cancelación | `viewshed`, `ViewshedParams`, `ViewshedProgress`, `ViewshedRay::visibleRanges` y `hiddenRanges` |
| Superficie de la zona | `polygonArea` (`Coordinates.h`) |
| Puntos que se arrastran con el ratón | `MapFeature::draggable`, señal `featureDragMovedTo` |
| Fijar un punto con un clic | `setActiveTool(MapTool::PickPoint)`, señal `pointPicked` |
| Relieve sombreado | `setHillshadeVisible`, `setHillshadeSun` |
| Qué elevación hay cargada | `elevationDescription`, `elevationWarnings`, `setElevationDir` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Cómo se lee el perfil

El perfil de abajo no es el terreno «a secas»: se dibuja **el terreno más el abombamiento de la Tierra** a cada
distancia, `d·(D−d)/(2·k·R)` con k = 4/3 (la refracción estándar), de modo que **la visual es una recta** y lo que la
corta es exactamente lo que bloquea. Por eso las cotas del eje son «efectivas» y la cima de una loma de 320 m sale
unos metros más alta en un trayecto largo. La visual es **azul** si hay vista y **roja** si no; el círculo marca el
punto que bloquea (o, si hay vista, el de menor holgura).

## «¿Qué altura necesita la antena de B?»

Cuando el terreno bloquea, el texto dice la menor altura de antena de B con la que A vería B. Es una **bisección**
sobre `lineOfSight` (la visibilidad solo mejora al subir la antena): unas 16 llamadas, **13 ms** en el trayecto de
24 km de la demostración (medido en el contenedor de pruebas; en tu PC variará). Si ni con 5000 m se ve, lo dice.

## La zona visible

`viewshed` lanza 360 rayos desde A. Cada rayo dice **qué tramos se ven y cuáles se esconden** de un objetivo a la
altura elegida (`visibleRanges` y `hiddenRanges`). El dibujo es **el mismo que el de la demo**: una cuña por cada
tramo de cada rayo, todas las de un tipo como partes de **una sola entidad** (**azul = visible**, **amarillo =
oculto**; lo oculto va debajo y lo visible encima; lo que no tiene dato queda sin pintar). Además se pinta **la máxima
visibilidad**: un marcador en el punto visible más lejano, unido a A por una línea y con su distancia.

El texto de debajo dice el porcentaje del círculo que se ve, el que se esconde y el que no tiene dato, la máxima
visibilidad y dónde está la **zona ciega** más cercana.

### Por qué no basta con `visibilityReachM`

La primera versión de este ejemplo pintaba solo `visibilityReachM`, **el primer tramo contiguo desde A**. Con datos
reales dio un resultado imposible: desde el **Pico Turquino** (1973 m, el punto más alto de Cuba) decía «se ve el 0 %,
máximo 0,4 km». No era un fallo del cálculo, sino de lo que se miraba: desde una cumbre redondeada **no se ve la ladera
justo bajo su borde** (la zona ciega), pero **sí las tierras bajas de lejos**. Esos segundos tramos eran justo lo que se
ignoraba. Con el pico de 880 m de la demostración (`23.75, -82.25`), un observador a 20 m y un objetivo a 10 m, cada
rayo tiene dos tramos: de 0 a ~0,8 km (la cima) y de ~9,8 km al alcance (el llano). Se ve el **74 %** del círculo; el
primer tramo solo suponía 2 km² de los 931 km² visibles. `lineOfSight` coincide punto a punto: a 300 m se ve, a 3 km no
y a 17 km sí.

Dos cosas que sorprenden la primera vez y que son correctas:

- **El alcance no llega al máximo ni en terreno llano.** La curvatura de la Tierra pone el horizonte de un ojo a
  10 m y un objetivo a 2 m en `√(2·k·R·10) + √(2·k·R·2)` ≈ **18,8 km**. Pedir 20 km no los da en llano.
- **El alcance sube si el terreno sube.** Un objetivo a 2 m sobre una ladera más alta asoma por encima del horizonte.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
analisis_terreno D:\QtPro\Recursos
```

Con tu paquete (el `mapa.json` con `"elevation"`, como el `cuba_dem.sqlitedb`), A y B arrancan a 12 km del centro
del mapa. Arrástralos o pulsa **A** / **B** y haz clic en el mapa. Sin elevación, pulsa **Usar terreno de
demostración**: centra el mapa en la loma y pone A y B a cada lado.

- **Antenas a 10 m:** el terreno bloquea a ~12 km (le faltan unos 300 m) y la app dice que con la antena de B a unos
  617 m se vería.
- **Calcular zona visible (V):** con la loma, unos 980 km² visibles (78 %) con alcance de 20 km, recortada hacia el este.
  Para ver la zona ciega, pon A en el pico (`23.75, -82.25`) con la antena a 20 m y el objetivo a 10 m: se ve el 74 %, con un
  disco amarillo de ~9,8 km de radio alrededor de la cumbre.
- **Relieve sombreado:** la loma se ve como tal. El azimut del sol se mueve con el deslizador.

`--comprobar` no abre ventana: crea el terreno sintético y comprueba la cota de la cresta, la línea de vista
bloqueada y clara, el perfil, la altura mínima de antena (con su coherencia: con esa altura se ve, con 2 m menos no),
la zona visible, un punto fuera de los datos y el glosario (lo usa `herramientas/probar_instalacion.sh`).
`--captura=imagen.png` guarda una imagen de la ventana con la zona calculada.

## El glosario

Panel **Glosario** (*Ayuda → Glosario*, tecla F1) con los 15 términos que usa la ventana: observador y objetivo,
altura de antena, cota, modelo de elevación (DEM), línea de vista, holgura, perfil de elevación, curvatura y
refracción, zona visible, zona ciega, alcance, azimut, relieve sombreado y terreno de demostración. `--comprobar` verifica que
todo término que la ventana muestra está definido.

## Detalles que conviene saber

- **Sin cota no se decide.** Si falta el dato en A, en B o en parte del trayecto, `lineOfSight` devuelve un resultado no
  válido y la app lo dice («Sin dato de elevación…») en vez de inventar una respuesta. Lo mismo con la zona visible si
  no hay cota en A.
- **El mar.** Por omisión (`voidElevation` = NaN) los huecos se saltan y la batimetría negativa cuenta como terreno. Para
  analizar objetivos sobre el agua, pon `voidElevation = 0` en `LineOfSightParams` / `ViewshedParams`: el fondo marino no
  tapa una visual de superficie.
- **Rendimiento** (medido en el contenedor de pruebas, compilación Release; en tu PC variará). Recalcular la visual y
  el perfil al arrastrar un punto tarda **15 ms** en el trayecto de 24 km, así que es fluido en vivo. La zona visible de
  20 km con 360 rayos tarda **0,3 s**; el tiempo crece con el alcance, por eso el cálculo trae barra de progreso y botón
  **Cancelar** para alcances de cientos de km (que no he medido). El paso de muestreo es de 30 m (más grueso en trayectos
  muy largos) y el de la zona visible, `alcance/700`.
- **Radio efectivo 4/3.** Es la refracción estándar. Con `curvature = false` (o `k = 1`) el cálculo es puramente
  geométrico y las visuales largas salen más optimistas.
- El mapa del paquete mínimo del CI (teselas de 1 px, zoom 0–2) se ve como el mundo entero y A y B se solapan; con
  tu paquete real se ven separados.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`analisis_terreno.pro`](analisis_terreno.pro) con el mismo kit con el que se instaló la librería.
También se compila junto a los demás ejemplos desde [`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
