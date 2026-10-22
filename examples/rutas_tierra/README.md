# rutas_tierra

Probador de **rutas por tierra** sin conexión: eliges un origen A y un destino B y la
aplicación calcula la ruta más rápida en coche por la red vial del MBTiles vectorial,
la pinta **por tramos de un mismo tipo de vía** (asfalto en azul, **caminos de tierra en
marrón y discontinuos**) y dice cuántos kilómetros de la ruta son de tierra. Es la base
de un planificador de rutas offline.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Ruta más rápida en coche entre dos puntos, leyendo la red vial de las teselas al vuelo | `MapWidget::findRoute` → `RouteResult` |
| La ruta partida en tramos seguidos de un mismo tipo de vía, para pintar cada tipo de un color | `RouteResult::legs` (`RouteLeg`) |
| Reparto por tipo de vía, tiempo estimado y lo que se anda del punto elegido a la vía | `RouteResult::byClass`, `durationS`, `snapFromM`, `snapToM` |
| Barra de progreso que se puede cancelar mientras se leen las teselas | el parámetro `progress` de `findRoute` |
| Marcadores A y B que se arrastran y recalculan al soltarlos | `MapFeature::draggable`, señal `featureDragFinished` |
| Escribir A y B en cualquier formato de coordenada | `parseCoordinate` (decimal, grados y minutos, UTM, MGRS) |
| Guardar la ruta a GPX, KML o GeoJSON | `exportVectorFile` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
rutas_tierra --demo
rutas_tierra D:\QtPro\Recursos
```

- **`--demo`** (o sin paquete de datos): usa una **red de demostración** que el propio
  programa escribe en una carpeta temporal, sin necesitar ningún dato: un pueblo al oeste,
  otro al este, un **camino de tierra** casi recto entre los dos (~1,9 km), una calle de
  asfalto que da la vuelta por el sur (~4,6 km), una carretera principal que sube desde ella
  hasta el camino y un ramal de tierra sin salida que lleva a una finca. En el desplegable
  hay tres rutas de ejemplo.
- **Con un paquete real** (el de Cuba con `cuba-osm.mbtiles`, por ejemplo): el paquete
  necesita un MBTiles vectorial como capa base o como `"vectorOverlay"` en su `mapa.json`.

En la ventana:

- **Dos clics** en el mapa ponen A y B (el tercero empieza otra ruta). También puedes
  escribirlas en los campos y pulsar **Calcular** o Intro.
- **Arrastra** un marcador A o B: al soltarlo, la ruta se recalcula.
- **Invertir** cambia A por B; **Limpiar** quita todo; **Guardar la ruta…** la saca a GPX, KML
  o GeoJSON.
- La tabla da los kilómetros y el % por tipo de vía; la fila de tierra va resaltada, y debajo
  sale el aviso de cuánto de la ruta es de tierra.

### Prueba sin ventana

```bat
rutas_tierra --comprobar
```

Arma la red de demostración, calcula tres rutas de longitud conocida (el camino de tierra entre
los pueblos, el ramal a la finca desde la carretera y desde el pueblo), comprueba distancias,
tramos por tipo de vía, que la vuelta mida lo mismo y la exportación a GPX, y sale con `0` si todo
está bien. Es lo que corre `herramientas/probar_instalacion.sh`.

## Qué esperar de la ruta (límites)

- El **tiempo es una estimación** por la velocidad media de cada tipo de vía (autopista 90,
  carretera principal 60, calle 30, **camino de tierra 15 km/h**…): las teselas de
  OpenMapTiles no traen giros prohibidos, límites de velocidad reales ni semáforos.
- Un camino de tierra es, en OpenMapTiles, la clase `track` (pistas y caminos rurales). Cuántos
  caminos de ese tipo trae un MBTiles depende de cómo estén mapeados en OpenStreetMap; si en tu
  zona faltan, la ruta no los verá.
- Sirve para rutas de unos cien kilómetros como mucho (el tope es de 3000 teselas z14); más
  lejos devuelve un error que lo dice.
- Solo coche. Para **evitar** los caminos de tierra todavía no hay opción: se ve cuánto hay y se
  decide (es una de las ideas anotadas en [`docs/PENDIENTES.md`](../../docs/PENDIENTES.md)).
- Si las calles del MBTiles no llegan unidas (el mapa simplifica y pierde el vértice donde una
  calle toca otra), la ruta falla con «las vías no llegan a unirse».

## Para llevarla a otro PC

`herramientas\desplegar.bat <rutas_tierra.exe> D:\Entrega D:\QtPro\Recursos` (Qt 6) o
`desplegar_qt5.bat` (Qt 5), como los demás ejemplos.
