# Pendientes: ideas aparcadas para otro momento

Trabajo hablado con el usuario y dejado para más adelante, con lo que ya se sabe
para retomarlo sin repetir el análisis. Al hacer una, se borra de aquí y se cuenta
en `BITACORA.md`.

## Puntos de interés (POI) del MBTiles vectorial

*Aparcado el 2026-10-21 (§165), retomado en §166 (qué hay aquí, con tarjeta al pinchar), §167
(grupos por tema), §168 (búsqueda) y §169 (selección como entidades).*

**Lo que ya se sabe del dato** (`cuba-osm.mbtiles`, OpenMapTiles):
- Capa `poi`, z12–17, con `name:latin`, `class`, `subclass`, `rank` e `indoor`.
- Solo en una tesela z14 del centro de La Habana hay 427. Clases vistas: `hospital`,
  `doctors`, `pharmacy`, `police`, `fuel`, `bank`, `atm`, `school`, `college`,
  `lodging`, `restaurant`, `cafe`, `bar`, `bus`, `place_of_worship`, `art_gallery`,
  `theatre`, `cinema`, `stadium`, `post`, `shop`, `grocery`…
- `vector_tile --mbtiles … --info --zoom 15 --center lat,lon` lista las clases de una zona.
- Ni teléfono, ni horario, ni dirección.

**Ya hecho (§167):** los dos estilos por defecto traen cinco grupos de POI por tema
(salud, seguridad, combustible, alojamiento, transporte), apagados de partida y desde
z13, con color y símbolo propios. Falta verlos con el `cuba-osm.mbtiles` real: las
clases y el zoom desde el que salen salieron de la documentación de OpenMapTiles.

**Lo que quedaría por hacer, cuando toque:** nada de los POI: lo que se aparcó está hecho (§166-§169).
En su día se podría añadir una importación por provincia con el índice de §168, o vigilar un POI con una
zona alrededor, pero nadie lo ha pedido.

## Otras ideas del mismo MBTiles (plan del 2026-10-21)

- **Rutas: lo que quedó fuera de la primera versión** (§171, coche, leída de las teselas al vuelo):
  - *Más modos*: a pie (senderos, calles peatonales; no autopistas) y en bici, cada uno con sus
    vías y su velocidad. El grafo ya guarda la clase de cada tramo; falta el filtro y la tabla de velocidades por modo.
  - *Rutas largas* (de punta a punta del país): hoy el tope es de ~14.000 km². Haría falta un grafo
    precalculado en un fichero, generado una vez por una herramienta, con la red cosida de todo el MBTiles.
  - *Giros prohibidos y límites de velocidad*: las teselas de OpenMapTiles no los traen. Habría que
    sacarlos de OSM aparte; sin ellos el tiempo es una estimación.
  - *Instrucciones giro a giro* («gira a la derecha en Obispo»): las teselas traen los nombres en otra capa
    (`transportation_name`) y habría que cruzarlos con el trazado.
  - *Varios destinos / ruta más corta en vez de más rápida / evitar autopistas / **evitar caminos de tierra***: hoy
    `examples/rutas_tierra` solo MUESTRA cuánta tierra lleva la ruta. Haría falta un `RouteOptions` en `findRoute`
    (p. ej. `avoidTracks`, que pondría el coste de `track` muy alto o lo quitaría del grafo).
- Recordar la licencia del dato: **ODbL**, «© OpenStreetMap contributors, © OpenMapTiles».
- **Comprobar los grupos de POI con el `cuba-osm.mbtiles` real** (§167): las clases y el zoom de salida
  salieron de la documentación de OpenMapTiles, no del dato.

## Ideas nuevas (repaso del 2026-10-22)

Salieron de repasar lo que hay; ninguna está pedida todavía. Encajan con la arquitectura actual.

- **Ejemplo `planificador_viajes`**: varios destinos, orden óptimo y exportación a GPX, sobre `findRoute`.
- **Isócronas**: la zona que se alcanza en N minutos desde un punto, con el mismo grafo vial.
- **Geocodificación inversa**: «calle y lugar más cercano» al pinchar, con los índices de calles y lugares.
- **Capa meteorológica o de mareas**: un overlay con datos cargados desde fichero, sin conexión.
- **Grabar y reproducir el flujo de telemetría en vivo**, para repetir sesiones.
- **Más protocolos en `libtelemetria`**: ADS-B (Mode S / SBS-1) y MAVLink, con dato publicado por fuente independiente.
- **Plantillas de simbología**: un juego de iconos estándar (p. ej. tipos AIS) como hook opcional.
- **Edición avanzada**: deshacer/rehacer global, snapping a vértices y estilos de capa guardados.
- **Atlas impreso**: varias páginas por cuadrícula, con leyenda, a partir de `exportView`.
- **Rendimiento vectorial**: caché en disco de teselas ya renderizadas y precarga de las vecinas.
- **Empaquetador de datos**: herramienta que, dada una zona, genere el paquete completo (`mapa.json` incluido).
- **Calidad y distribución**: tests de interfaz con capturas y paquetes (vcpkg o Conan).
