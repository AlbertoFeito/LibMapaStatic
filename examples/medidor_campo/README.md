# medidor_campo

Medidor sobre un paquete de datos de libmapa: **distancias con su rumbo** (dos clics) y
**superficies con su perímetro** (polígono clic a clic). Cada medida queda dibujada en el mapa
y en una lista con la suma de distancias y de superficies; las unidades se cambian al vuelo y
la tabla se copia al portapapeles. Sirve de base para topografía ligera, parcelas,
planificación de tramos o estimar un área de búsqueda.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Medir distancia y rumbo en dos clics | `setActiveTool(MapTool::Measure)`, señal `measurementFinished` |
| Dejar la distancia dibujada, con su rótulo «045° · 5.2 NM» que se arrastra | `addBearingLine`, `bearingLineMeasure`, `removeRangeMark` |
| Medir superficie y perímetro marcando un polígono | `setActiveTool(MapTool::MeasureArea)`, señal `areaMeasured`, `clearAreaSelection` |
| Dejar el polígono y rotularlo | `addFeatureLayer` + `setFeatureLayerTransient`, `addFeature`, `updateFeature` |
| Unidades métricas o náuticas | `setScaleBarUnits` (los rótulos del mapa siguen a la escala) |
| Medir sin mapa | `polygonArea`, `pathLength` (`Coordinates.h`) |
| Centrar la vista en una medida | `fitBounds` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
medidor_campo D:\QtPro\Recursos
```

1. **Medir distancia**: clic en el origen y clic en el destino. Aparece en la lista como `D1`
   (distancia y rumbo verdadero) y en el mapa como una línea con su rótulo, que se puede arrastrar.
2. **Medir superficie**: un clic por vértice; doble clic, Intro o clic derecho cierra. Aparece como `A1`
   (superficie y perímetro) y el polígono queda rotulado en el mapa.
3. Cambia **la unidad de distancia** (métrico o millas náuticas) o **la de superficie**
   (ha, km², m², acres): la lista, los totales y los rótulos se actualizan.
4. Un clic en una medida de la lista centra el mapa en ella. **Borrar la seleccionada**, **Borrar todas**
   o **Copiar tabla** (texto con tabuladores, se pega en una hoja de cálculo).

`--comprobar` no abre ventana: mide un rectángulo y una línea conocidos, los registra como lo haría el
ratón y comprueba áreas, longitudes, sumas y borrado (lo usa `herramientas/probar_instalacion.sh`).
`--captura=imagen.png` abre la ventana con unas medidas de ejemplo y guarda una imagen.

## Detalles que conviene saber

- Las medidas son **geodésicas** (sobre el elipsoide WGS84): no dependen del zoom ni de la proyección.
  El área se calcula en la esfera de igual superficie; para polígonos pequeños, la diferencia con un
  cálculo exacto en el elipsoide es muy inferior al 0,5 %.
- Las medidas **no se guardan**: la capa de polígonos es temporal y no se toca la base de entidades del
  paquete. Para conservarlas, copia la tabla o exporta la vista (`exportView`).
- Las millas terrestres no están: la escala gráfica de la librería solo maneja métrico y náutico, y los
  rótulos del mapa deben coincidir con la lista.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`medidor_campo.pro`](medidor_campo.pro) con el mismo kit con el que se instaló la
librería. También se compila junto a los demás ejemplos desde
[`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
