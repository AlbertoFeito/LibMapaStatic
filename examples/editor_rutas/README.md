# editor_rutas

Editor de rutas sobre un paquete de datos de libmapa: **dibujar** una ruta clic a clic,
**editar** sus vértices, **deshacer** y **guardar** a GPX, KML o GeoJSON. Es el
complemento de [`visor_gpx`](../visor_gpx), que solo lee. Sirve de base para
planificadores de recorridos, editores de escenarios o cualquier app que cree geometrías.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Capa propia para las rutas, por encima de las del paquete | `addFeatureLayer(id, nombre, orden)`, `setActiveFeatureLayer` |
| Dibujar una ruta (doble clic o clic derecho la cierra) | `setActiveTool(MapTool::DrawPolyline)`, `setDraftStyle`, señal `featureCreated` |
| Editar: arrastrar vértices, insertar en un lado, borrar con Supr | `setActiveTool(MapTool::EditFeature)` |
| Deshacer y rehacer | `undo`, `redo`, `clearUndoHistory` |
| Guardar y abrir, el formato lo da la extensión | `exportVectorFile`, `importVectorFile` |
| Que nada se guarde solo en el paquete | `MapConfig::autosaveFeatures = false` |
| Longitud de la ruta seleccionada, en vivo | señales `featureUpdated`, `featureSelected`, `pathLength` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
editor_rutas D:\QtPro\Recursos
```

1. **Ruta → Dibujar ruta** (tecla D): clic para cada punto, doble clic para cerrarla; pide un nombre.
2. **Ruta → Editar vértices** (tecla E): arrastra un tirador, doble clic en un lado inserta uno, Supr borra.
3. **Edición → Deshacer** (Ctrl+Z) / **Rehacer** (Ctrl+Y). Poner el nombre cuenta como un paso aparte.
4. **Archivo → Guardar** (Ctrl+S) o **Guardar como...**; el título lleva un `*` mientras haya cambios sin guardar, y al cerrar pregunta.

`--comprobar` no abre ventana: crea una ruta por código, la guarda a GPX, la vuelve a
leer y comprueba que es la misma (lo usa `herramientas/probar_instalacion.sh`).

## Detalles que conviene saber

- La librería escribe las líneas a GPX como **`<trk>`** (recorrido), no como `<rte>`
  (ruta planificada). Se vuelven a leer sin pérdida, pero otro programa las verá como tracks.
- Un GPX no guarda estilo; KML y GeoJSON sí.
- La capa de rutas va con un orden de dibujo alto: las capas fijas del paquete (provincias,
  curvas) ya no son seleccionables por defecto, pero si un `mapa.json` las declara con
  `"selectable": true` y quedaran por encima, un clic seleccionaría la provincia y no la ruta.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`editor_rutas.pro`](editor_rutas.pro) con el mismo kit con el que se instaló
la librería. También se compila junto a los demás ejemplos desde
[`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
