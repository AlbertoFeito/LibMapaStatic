# vigilancia_zonas

Vigilancia de zonas sobre un paquete de datos de libmapa: **dibujas zonas con el ratón**, les pones
**reglas de aviso** (al entrar, al salir, permanencia máxima) y la aplicación **registra cada entrada,
salida y permanencia excedida** en un historial que se exporta a CSV. Las zonas se guardan y se abren
(GeoJSON) con sus reglas. Es la base de cualquier app que vigila áreas: una zona prohibida, un puerto,
un fondeadero, una zona de operaciones.

La librería da la geometría (¿está este contacto dentro de esta zona?) y avisa de cada entrada y
salida. **Lo que hace de esto una aplicación** —las reglas, el historial, la permanencia, guardar— es lo
que enseña este ejemplo, y es de la aplicación, no de la librería.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Vigilar un polígono como zona | `watchZone`, `unwatchZone`, `watchedZones` |
| Entradas y salidas de contactos | señales `targetEnteredZone` / `targetLeftZone` |
| Quién está dentro ahora | `targetsInZone`, `zonesOfTarget` |
| Dibujar una zona y, al cerrarla, pedir sus reglas | `setActiveTool(MapTool::DrawPolygon)`, señal `featureCreated`, `setDraftStyle` |
| Editar la forma: se reevalúa quién queda dentro | `setActiveTool(MapTool::EditFeature)`, `updateFeature` |
| Reglas de la zona guardadas en ella | `MapFeature::attributes` (viajan en el GeoJSON) |
| Guardar y abrir las zonas | `exportVectorFile`, `importVectorFile`, `clearFeatureLayer` |
| Que nada se guarde solo en el paquete | `MapConfig::autosaveFeatures = false` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Las reglas

Cada zona lleva tres reglas, que se ponen al dibujarla (o con *Zonas → Reglas de la zona elegida…*):

- **Avisar al entrar** (contorno rojo): una zona prohibida o de seguridad.
- **Avisar al salir** (naranja): un puerto, un área que hay que controlar que no se abandone.
- **Avisar si permanece más de N minutos** (azul): un fondeadero, una zona de espera.

Sin ninguna regla (gris), la zona solo registra. **Se anotan todos los hechos**, con o sin aviso; el
aviso (⚠) es el que una regla marca, y el filtro *Solo avisos* oculta los demás.

## El glosario

Panel **Glosario** (*Ayuda → Glosario*, tecla F1) con los 14 términos que usa la ventana, buscable por
nombre o por palabra de la definición, y la definición de cada columna también al pasar el ratón por su
cabecera. `--comprobar` verifica que están definidos todos los términos de las dos tablas y de la
interfaz: si añades una columna con un término nuevo y no lo defines, la prueba falla.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
vigilancia_zonas D:\QtPro\Recursos
```

El simulador mueve cinco contactos (30 s simulados por cada segundo real) por tres zonas de ejemplo:

- **Remolcador** sale del **Puerto** (avisa al salir): sale con aviso tras unos 10 min, y se detiene fuera de todo.
- **Pesquero 1** atraviesa la **Zona restringida** (avisa al entrar): entra con aviso y sale unos 30 min después.
- **Carguero A** llega al **Fondeadero** (avisa a los 30 min) y se queda parado: aviso de permanencia, una sola vez.
- **Pesquero 2** cruza el Fondeadero en unos 20 min: sin aviso, y **Carguero B** se aleja de todo: sin eventos.

Para dibujar la tuya: tecla **D**, clic por cada vértice, doble clic para cerrar; te pide nombre y reglas. Con
**E** arrastras los vértices y la tabla de *Dentro*, *Entradas* y *Salidas* se actualiza sola. **Archivo →
Guardar zonas** y **Abrir zonas** conservan las reglas; **Exportar eventos** escribe el CSV.

`--comprobar` no abre ventana: avanza el reloj simulado y comprueba los eventos, los avisos, los contadores,
editar una zona, cambiar reglas, dibujar y cancelar, el CSV, guardar y abrir, y el glosario. Lo usa
`herramientas/probar_instalacion.sh`. `--captura=imagen.png` guarda una imagen de la ventana.

## Para conectar un feed real

Sustituye la clase `Simulador` por tu decodificador y llama a `Ventana::recibir` con cada parte
(`nombre`, `posicion`, `rumbo`, `nudos`, `hora`). Un cronómetro llama a `revisarPermanencias()` para los
avisos de permanencia; con la hora real de cada parte, quita `kEscala` y el reloj simulado de `paso()`.

## El CSV

Una fila por evento, del más antiguo al más reciente, separado por `;` (lo abre cualquier hoja de cálculo
en español) y con la hora en UTC:

```
hora_utc;zona;contacto;evento;permanencia_min;aviso
2026-10-19T11:42:39Z;Puerto;Remolcador;Entra;0.0;0
2026-10-19T11:53:09Z;Puerto;Remolcador;Sale;10.5;1
```

`evento` es `Entra`, `Sale` o `Permanencia`; `permanencia_min` va a 0.0 en las entradas.

## Detalles que conviene saber

- **Al empezar a vigilar una zona, los contactos que ya están dentro producen una entrada.** Es el comportamiento
  de la librería y es lo que se quiere (te enteras de lo que hay), pero cuenta: al abrir un fichero de zonas, o al
  dibujar una zona sobre contactos, verás sus entradas en el historial. Esas entradas llevan permanencia 0 desde
  ese momento, no desde que el contacto llegó de verdad.
- **Orden al crear una zona:** los contadores se crean *antes* de `watchZone`, porque esa llamada ya emite las
  entradas. Al revés se perderían.
- **El nombre del contacto se lee de su objetivo** (`target(id)->label`), no de una tabla propia: las señales de zona
  llegan dentro de `addTarget`/`updateTarget`, antes de que la aplicación apunte el id de un contacto nuevo.
- **Borrar una zona o un contacto no es una salida:** la librería no la emite, y la aplicación olvida sus contadores.
- «Dentro» es lo que se ve dentro en el mapa (lados rectos en la proyección). En zonas muy grandes, los lados
  geodésicos pueden diferir ligeramente.
- Con el paquete mínimo del CI (teselas de 1 px, zoom 0–2) el mapa se ve como el mundo entero; con tu paquete real
  se ve el entorno de La Habana.
- Las zonas **no se guardan solas**: hay que usar *Guardar zonas*. No se avisa de cambios sin guardar (el
  [`editor_rutas`](../editor_rutas) sí lo hace, y es la referencia si lo necesitas).

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`vigilancia_zonas.pro`](vigilancia_zonas.pro) con el mismo kit con el que se instaló la
librería. También se compila junto a los demás ejemplos desde
[`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
