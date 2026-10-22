# Pendientes: ideas aparcadas para otro momento

Trabajo hablado con el usuario y dejado para más adelante, con lo que ya se sabe
para retomarlo sin repetir el análisis. Al hacer una, se borra de aquí y se cuenta
en `BITACORA.md`.

## Puntos de interés (POI) del MBTiles vectorial

*Aparcado el 2026-10-21 (§165), retomado en §166 (qué hay aquí, con tarjeta al pinchar) y §167
(grupos por tema).* Quedan la búsqueda y la selección como entidades.

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

**Lo que quedaría por hacer, cuando toque:**
1. **Buscar POI** por nombre o tipo («farmacia») cerca de la vista, sumándolo al
   índice de lugares (`lugares.sqlitedb`).
2. **Sacar una selección como entidades** («todos los hospitales de la provincia»):
   para filtrarlas, vigilarlas con alertas de zona y exportarlas a GeoJSON/KML/GPX.

## Otras ideas del mismo MBTiles (plan del 2026-10-21)

- **Búsqueda de calles** («Obispo») sin conexión, desde `transportation_name`.
- **Rutas sin conexión** por la red vial (tipo de vía, sentido único, acceso). Es
  lo más costoso: hay que coser los tramos que las teselas cortan.
- Recordar la licencia del dato: **ODbL**, «© OpenStreetMap contributors, © OpenMapTiles».
