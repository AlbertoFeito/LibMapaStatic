# Pendientes: ideas aparcadas para otro momento

Trabajo hablado con el usuario y dejado para más adelante, con lo que ya se sabe
para retomarlo sin repetir el análisis. Al hacer una, se borra de aquí y se cuenta
en `BITACORA.md`.

## Puntos de interés (POI) del MBTiles vectorial

*Aparcado el 2026-10-21 (§165).* El usuario pidió quitarlos de la lista «para otro
momento»: en los estilos por defecto la casilla no mostraba nada útil (empezaban
en z15) y se quitaron de los dos estilos, el de base y el híbrido.

**Lo que ya se sabe del dato** (`cuba-osm.mbtiles`, OpenMapTiles):
- Capa `poi`, z12–17, con `name:latin`, `class`, `subclass`, `rank` e `indoor`.
- Solo en una tesela z14 del centro de La Habana hay 427. Clases vistas: `hospital`,
  `doctors`, `pharmacy`, `police`, `fuel`, `bank`, `atm`, `school`, `college`,
  `lodging`, `restaurant`, `cafe`, `bar`, `bus`, `place_of_worship`, `art_gallery`,
  `theatre`, `cinema`, `stadium`, `post`, `shop`, `grocery`…
- `vector_tile --mbtiles … --info --zoom 15 --center lat,lon` lista las clases de una zona.
- Ni teléfono, ni horario, ni dirección.

**Cómo se añaden hoy, sin tocar código:** un estilo propio con un grupo y una regla
filtrada por `class`. Está en el README («Añadir puntos de interés»). Probado con
hospitales y policía en La Habana a z15.

**Lo que quedaría por hacer, cuando toque:**
1. **Grupos de POI por tema** en los estilos por defecto, apagados de partida y
   desde z13–14 para que se vean antes:
   - «Salud»: `hospital`, `doctors`, `pharmacy`;
   - «Seguridad»: `police`, `fire_station`;
   - «Combustible»: `fuel`;
   - «Alojamiento»: `lodging`;
   - «Transporte»: `bus`, `railway`, `ferry_terminal`;
   - …
   Cada grupo con su color y un símbolo distinto por tema: hoy solo hay un punto de color.
2. **Ver un POI concreto**: al pinchar, la tarjeta de la demo con su nombre y tipo
   (necesita el «qué hay aquí» de abajo).
3. **Buscar POI** por nombre o tipo («farmacia») cerca de la vista, sumándolo al
   índice de lugares (`lugares.sqlitedb`).
4. **Sacar una selección como entidades** («todos los hospitales de la provincia»):
   para filtrarlas, vigilarlas con alertas de zona y exportarlas a GeoJSON/KML/GPX.

## Otras ideas del mismo MBTiles (plan del 2026-10-21)

- **Qué hay aquí**: al hacer clic, qué calle, POI o edificio hay debajo, con sus
  atributos (tipo de vía, superficie, altura del edificio…). Para eso hay que
  decodificar la tesela del punto y buscar la entidad más cercana.
- **Búsqueda de calles** («Obispo») sin conexión, desde `transportation_name`.
- **Rutas sin conexión** por la red vial (tipo de vía, sentido único, acceso). Es
  lo más costoso: hay que coser los tramos que las teselas cortan.
- Recordar la licencia del dato: **ODbL**, «© OpenStreetMap contributors, © OpenMapTiles».
