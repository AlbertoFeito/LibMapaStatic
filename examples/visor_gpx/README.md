# visor_gpx

Visor de rutas y tracks (GPX, KML, GeoJSON) sobre un paquete de datos de
libmapa. Es el ejemplo para quien necesite **ver, medir y reproducir
recorridos**: senderismo, post-análisis de una misión, revisión de un track de GPS.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Abrir un fichero como capa y encuadrarlo | `MapWidget::importVectorFile`, `fitBounds` |
| Inicio (verde) y fin (rojo) de cada ruta | `setRouteEndpoints(RouteEndpoints::Routes)` |
| Longitud y perfil de elevación de la ruta seleccionada (la primera al abrir; otra, pinchándola) | `featureSelected`, `pathLength`, `elevationProfile` |
| Reproducir un GPX con horas (play, pausa, ir a una hora, velocidad) | `TrackPlayer` |
| Exportar la vista a PDF o imagen, con título y atribución | `exportView` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.
El perfil de elevación solo sale si el paquete de datos trae elevación (SRTM,
GeoTIFF o `.sqlitedb`, la clave `"elevation"` de `mapa.json`) y esta cubre la ruta;
si no, la gráfica lo dice.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
visor_gpx D:\QtPro\Recursos examples\visor_gpx\ruta_ejemplo.gpx
```

- **Menú Archivo → Abrir ruta…** carga otro fichero; **Exportar vista…** guarda PDF o imagen.
- Con un GPX con horas aparece la barra de reproducción.
- `--comprobar` no abre ventana: dice qué ha leído, el perfil de la primera ruta
  si hay elevación, y sale con 0 si todo está bien (lo usa
  `herramientas/probar_instalacion.sh`).
- `--captura=imagen.png` abre la ventana, guarda una imagen y sale.

`ruta_ejemplo.gpx` son datos sintéticos (una ruta planificada y un track con
24 puntos, uno por minuto) cerca de La Habana.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`visor_gpx.pro`](visor_gpx.pro) con el mismo kit con el que se
instaló la librería. También se compila junto a los demás ejemplos desde
[`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
