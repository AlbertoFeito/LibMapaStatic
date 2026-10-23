# Randy: carta náutica cubana (N1, N2 y N3)

Investigación del 2026-10-23. Se plantea una carta náutica de verdad sobre libmapa: sondas,
veriles con bandas de profundidad, peligros, ayudas a la navegación, canales y zonas
restringidas, con edición y fecha de actualización por hoja. La app sigue 100 % sin conexión:
se actualiza por fichero, con un importador, no descargando ella.

## Las tres rutas

- **N1, con lo libre.** Llevar el paquete actual a aspecto de carta: sondas muestreadas sobre la
  batimetría, bandas de profundidad con contorno de seguridad según el calado del operador y
  ayudas a la navegación (faros y boyas de OpenSeaMap, extraíbles del mismo OSM). Rotulada como
  «no oficial, no usar como única referencia».
- **N2, importador ráster BSB/KAP.** Una herramienta tesela las cartas BSB a MBTiles ráster y la
  librería las pinta como capa base. Se ve como la carta de papel.
- **N3, importador ENC S-57.** Leer celdas `.000` y convertir sus objetos (DEPARE, SOUNDG, WRECKS,
  LIGHTS…) a capas propias con simbología S-52 simplificada.
- **Actualización común.** Carpeta `cartas/` con `manifiesto.json` (celda u hoja, nº de edición,
  fecha, fuente, suma de control) e importación con validación, sustitución, recarga sin
  recompilar y copia de la versión anterior para volver atrás. La edición instalada se ve siempre
  en pantalla.

## Qué se pudo verificar y qué no

| Afirmación | Estado |
|---|---|
| La autoridad cubana es ONHG/GEOCUBA | Confirmado: el geoportal [iderc.cu](http://www.iderc.cu/web/iderc/cartas-y-publicaciones-nauticas) lista catálogos, correcciones, cartas analógicas y electrónicas. |
| «161 títulos» | No encontrado (la página no cargó desde el contenedor). |
| «Colección completa en BSB» | No encontrado. |
| «Celdas S-57 distribuidas por IC-ENC» | Parcial: IC-ENC distribuye ENC oficiales y un [informe de México a la IHO](https://iho.int/uploads/user/Inter-Regional%20Coordination/RHC/MACHC/MACHC25/MACHC25_2024_06.9_SP_National%20Report_Mexico.pdf) cita una celda compartida con Cuba; no hay entrada de Cuba en su catálogo que yo haya visto. |
| «NGA ya no se actualiza» | Sin comprobar. |

**Antes de invertir en N3** hay que preguntar al ONHG o a IC-ENC si las celdas cubanas existen
como producto y bajo qué licencia.

## N2: BSB/KAP

- **Formato.** Cabecera de texto (`TOKEN/valor`) más píxeles binarios comprimidos por longitud de
  racha, con paleta. Sin especificación oficial; referencias: [libbsb](https://libbsb.sourceforge.net/bsb_file_format.html)
  y el [driver BSB de GDAL](https://gdal.org/drivers/raster/bsb.html) (`bsb_read.c`).
- **Encaje.** Lector propio sin GDAL, como `GeoTiffImage`. Salida: MBTiles ráster, que ya se pinta
  como capa base; es una herramienta nueva en `tools/` y no toca el widget.
- **Lo difícil:** remuestrear la carta a teselas Web Mercator con calidad.
- **Riesgos.**
  1. *Datum*: cartas antiguas en NAD27 u otro datum local, con desplazamiento en la cabecera. Si
     se ignora, la carta queda corrida respecto al GPS. Aplicarlo y comprobarlo con puntos conocidos.
  2. *Bordes y solapes*: recortar márgenes (`PLY`) y recuadros de otra escala; ordenar hojas por
     escala. [GDAL ignora `PLY`](https://trac.osgeo.org/gdal/ticket/3175).
  3. *Paletas*: día, ocaso y noche; un MBTiles fijaría una sola.
  4. *Legal*: las cartas BSB de GEOCUBA son de pago; el repo nunca lleva datos de cartas. La
     compresión se describe como [patentada](https://docs.safe.com/fme/2016.1/html/FME_Desktop_Documentation/FME_ReadersWriters/bsb/bsb.htm);
     no se ha verificado si la patente sigue vigente.
- **Prueba independiente** (exigida por `CLAUDE.md`): una carta KAP pública de otro país validada
  contra `gdalinfo`, además de la ida y vuelta del escritor propio.

## N3: S-57

- **Formato.** ISO 8211 con registros de objetos y espaciales (nodos, aristas, caras); hay que
  reconstruir la geometría. Las actualizaciones `.001`, `.002`… se aplican en orden.
- **Cifrado S-63.** Las ENC oficiales de los centros regionales van [cifradas bajo S-63](https://mytimezero.com/tz-professional/s63-module),
  con permisos por cliente. Sin ellos no hay celdas legibles; descifrar exige la especificación y
  claves licenciadas. No se hallaron las condiciones de usuario final de IC-ENC.
- **Simbología.** S-52 completa es un proyecto en sí mismo; un subconjunto es viable. Un ECDIS
  certificado cumple normas que esta app no puede reclamar.
- **Contorno de seguridad dinámico.** Depende del calado, así que no puede ir cocido en teselas:
  hace falta una capa nueva (`QCPLayerable`) que pinte las bandas desde `DEPARE`
  (`DRVAL1`/`DRVAL2`) en ejecución.
- **Luces.** Sectores (arcos con color y alcance) y características `Fl(2) 10s` necesitan dibujo propio.
- **GDAL.** OGR leería S-57 rápido, pero rompe la política de no depender de GDAL y complica los
  dos juegos de despliegue. Los [parsers ISO 8211](https://docs.rs/crate/iso8211/) abiertos solo
  cubren la capa binaria, no el significado de los objetos.
- **Fases.** 3a leer S-57 sin cifrar a una tabla SQLite propia; 3b dibujarlo con capa nueva y
  bandas por calado; 3c S-63, solo con licencia real y datos en la mano.

## Recomendación

1. N2 primero: más barato, fiel a la carta de papel y sin cifrado. Necesita al menos un `.KAP` real.
2. Manifiesto y actualización a la vez, porque N2 ya los necesita.
3. N3a cuando haya celdas sin cifrar; N3c solo si el ONHG o IC-ENC confirman existencia y licencia.

## Pendiente

- Consulta al ONHG / IC-ENC sobre celdas cubanas y licencia.
- Conseguir un `.KAP` real (de GEOCUBA o de otro servicio) para N2.
- Aclarar la «migración de Sas» que mencionaba el texto original: no aparece en este repo.

---

# Otro tema a revisar: el ratón de la librería frente a una app con sus propias acciones

Revisión del 2026-10-23, leyendo `src/widget/MapView.cpp` y `include/libmapa/MapWidget.h`. No se ha
cambiado código. Pregunta: una app que ya tiene acciones propias con el ratón sobre el mapa, ¿choca
con lo que hace la librería? Respuesta: **sí puede**, y hoy la app no puede apagar nada.

## Cómo recibe el ratón la librería

`MapWidget` contiene un `MapView` que ocupa todo el área y recibe él los eventos de ratón y
teclado. `MapView` los acepta (`event->accept()`), así que el `MapWidget` y la ventana de la app
**no los ven**. Heredar de `MapWidget` o ponerle un filtro de eventos no sirve.

## Lo que la librería hace siempre (con `MapTool::None`)

| Gesto | Efecto fijo |
|---|---|
| Arrastrar con el botón izquierdo | Mueve el mapa; si empieza sobre una etiqueta o una entidad `draggable`, arrastra eso. |
| Botón central | Mueve el mapa con cualquier herramienta. |
| Rueda | Zoom hacia el cursor. |
| Doble clic en vacío | Acerca un nivel. |
| Clic sobre un elemento / en vacío | Selecciona / deselecciona. |
| Clic derecho | Cancela la herramienta si tiene algo a medias; si no, emite `contextMenuRequested`. |
| Teclado | Esc, Intro, Supr, Retroceso, flechas, `+`, `-`, RePág y AvPág. |

## Qué choca y qué no

- **Conviven bien**: acciones que solo reaccionan a un clic o al movimiento, con los avisos pasivos
  `mouseMoved`, `clicked(coordenada, botón)` (solo en clic sin arrastre), `elementClicked` y
  `contextMenuRequested`.
- **Chocan**: arrastres propios con el botón izquierdo (la app no sabe que empezó), rueda o doble
  clic con otro significado, y las teclas que la librería acepta (Esc, Supr, flechas, `+`/`-`).
- **Sin información**: `clicked` no trae modificadores (Ctrl, Mayús); un «Ctrl+clic» no se distingue.
- **Menú propio**: usar `contextMenuRequested`, no el `contextMenuEvent` de Qt; con una herramienta a
  medias, el clic derecho solo la cancela y no abre menú.

## Qué se puede hacer hoy sin tocar la librería

1. Usar las herramientas incorporadas (`setActiveTool`): `PickPoint`, `SelectArea`, `SelectPolygon`,
   `Measure`. Con una activa, el arrastre izquierdo no mueve el mapa (el central sí).
2. Filtro de eventos en el widget hijo (`mapa->findChildren<QWidget*>()`), devolviendo `true` para lo
   que sea de la app. Funciona, pero depende de la estructura interna y puede romperse.

## Propuesta para la librería (sin hacer)

- `setMouseNavigation(flags)`: encender y apagar por separado arrastre, rueda, doble clic y teclas.
  Por defecto todo encendido, para no cambiar lo actual.
- Avisos `mousePressed` y `mouseReleased` con modificadores, y modificadores en `clicked`.

## Pendiente

- Saber qué gestos usa la app (clic, arrastre, rueda, teclas, clic derecho) para ver cuáles chocan
  de verdad y si hace falta la rama con esos cambios.
- Si hace falta el API, añadirlo a `README.md` («Ratón y teclado») y probarlo en `tst_mapwidget`.
