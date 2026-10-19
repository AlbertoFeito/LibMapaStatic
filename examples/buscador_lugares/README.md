# buscador_lugares

Buscador de sitios por **nombre** o por **coordenada**, sin conexión. Es la base de un
callejero offline o de una utilidad de coordenadas. Se apoya en el índice de lugares del
paquete de datos (ciudades, pueblos, aeropuertos, bahías, cabos, picos...).

## Qué enseña

| Función | API de libmapa |
|---|---|
| Buscar mientras se escribe (sin tildes ni mayúsculas, ciudades antes que aldeas) | `MapWidget::searchPlaces`, `hasPlaces` |
| Ir al lugar con el zoom que le toca y marcarlo con un pin | `goToPlace`, `Place::suggestedZoom`, capa temporal con `setFeatureLayerTransient` |
| Lo escrito también vale como coordenada, en cualquier formato | `parseCoordinate` (decimal, grados y minutos, UTM, MGRS) |
| Coordenada bajo el cursor en el formato elegido | señal `mouseMoved`, `formatCoordinate` |
| Un clic copia la coordenada al portapapeles | señal `clicked` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
buscador_lugares D:\QtPro\Recursos
```

- Escribe un nombre (`guantanamo`, `bahia guan`): salen los lugares; **Intro** va al primero,
  o pincha uno de la lista.
- Escribe una coordenada (`23.1136, -82.3666`, `17Q 360057 2556751`, `17QLF6005656750`,
  `23°06'48.96"N 82°21'59.76"W`): sale como primer resultado.
- **Ctrl+K** vuelve al cuadro de búsqueda.
- Cambia el formato del desplegable y mira la coordenada bajo el cursor; un clic la copia.

Sin índice de lugares (sin `"places"` en el `mapa.json`), el buscador sigue sirviendo para
coordenadas y lo avisa en la barra de estado.

## El índice de lugares

Es un `lugares.sqlitedb` local. Se genera una vez con
[`herramientas/osm_lugares_a_sqlitedb.py`](../../herramientas/osm_lugares_a_sqlitedb.py) a partir
de un export de OpenStreetMap (la consulta de Overpass va en su cabecera), y se declara en el
`mapa.json`:

```json
"places": { "file": "lugares.sqlitedb" }
```

Para probar sin datos reales sirve `tests/data/places/lugares_prueba.sqlitedb`
(12 lugares inventados).

`--comprobar` no abre ventana: busca «guantanamo» en el índice (si lo hay), lee una coordenada
MGRS y comprueba que formatear y volver a leer da el mismo punto (lo usa
`herramientas/probar_instalacion.sh`). MGRS con 5 cifras tiene 1 m de rejilla y trunca: la ida y
vuelta puede perder hasta ~1,4 m.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`buscador_lugares.pro`](buscador_lugares.pro) con el mismo kit con el que se instaló
la librería. También se compila junto a los demás ejemplos desde
[`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
