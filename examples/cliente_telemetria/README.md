# cliente_telemetria

Un receptor de **NMEA 0183 y AIS por UDP** con libmapa y [`libtelemetria`](../../libtelemetria): escucha un puerto, decodifica
las frases y pone en el mapa **el barco propio y los contactos AIS**. Es lo que sustituye al «feed» simulado de
[`monitor_trafico`](../monitor_trafico) en un producto real, y la base de cualquier app que reciba telemetría de un receptor GPS/AIS,
un multiplexor NMEA o una pasarela serie-red.

La pareja para probarlo sin hardware es [`emisor_nmea`](../emisor_nmea): lo que él emite, esto lo escucha. También reproduce
una **captura** (`.nmea`) y trae un escenario de demostración.

## Qué enseña

| Función | API |
|---|---|
| Escuchar un puerto UDP | `QUdpSocket` (Qt Network; la pone la aplicación) |
| Decodificar lo que llega | `telemetria::Decoder::feedDatagram` / `feed`, `Event`, `DecoderStats` |
| Barco propio a partir de RMC, GGA, VTG y HDT; y de `!AIVDO` si no hay GPS | `Event::Kind::Rmc/Gga/Vtg/Hdt`, `Event::ownShip` |
| Fundir los mensajes AIS por MMSI (posición y estáticos llegan por separado) | `AisMessage` (`hasPosition`, `name`, `shipType`, `lengthM`...) |
| Contactos en el mapa con rumbo, velocidad y traza | `MapWidget::addTarget` / `updateTarget` / `setTargetVelocity` |
| Contacto perdido (atenuado) y borrado | `setTargetAttribute` + `TargetFilter` con `predicate` y `dim` |
| Tipo de barco y estado de navegación en español | `aisShipTypeText`, `aisNavStatusText` |
| Calidad del enlace (frases/s, suma mala, mal formadas, ignoradas, fragmentos perdidos) | `DecoderStats` |

## La ventana

- **Enlace** (izquierda): puerto UDP y botón *Escuchar/Parar*, estado, «último dato hace X s», los **contadores del decodificador** y
  las frases por segundo (promedio de 5 s).
- **Barco propio**: posición, SOG, COG, rumbo de la proa (HDT), calidad del fijado, satélites y HDOP. *Centrar el mapa en mi barco*
  lo sigue.
- **Contactos AIS** (abajo): MMSI, nombre, tipo, clase A/B, SOG, COG, distancia y marcación desde el propio, estado de navegación, edad y
  estado (Activo/Perdido), ordenados por distancia. Al pasar el ratón por cada cabecera sale su definición del glosario.
- **Registro**: sumas malas y frases descartadas, contactos perdidos y borrados, cambios de escucha. *Anotar todas las frases* lo
  vuelca todo.
- **Glosario** (*Ayuda → Glosario*, F1): 23 términos, buscable. `--comprobar` verifica que todo lo que la ventana muestra está definido.

## Cómo se tratan los datos

- **El nombre no viaja con la posición.** Llega en los mensajes estáticos (tipo 5 o 24) cada pocos minutos. Un contacto aparece con su MMSI y
  toma el nombre, tipo y eslora cuando llegan (también si llegaron antes que la posición).
- **Un mensaje de dos frases puede llegar en dos datagramas.** El decodificador espera la segunda; si falta, descarta el mensaje y lo
  cuenta en «Fragmentos perdidos».
- **«No disponible» no es un dato.** Un informe con 181° de longitud no pone el contacto en el mapa; aparece en la tabla con «—».
- **Perdido a los 60 s sin datos** (atenuado en el mapa y la tabla), **borrado a los 5 min**, **reaparece** si vuelve a emitir.
  Un AIS de clase A informa cada 2–10 s en marcha pero cada 3 min fondeado: 60 s es prudente para tráfico en marcha y se cambia en
  `kPerdidoSeg`.
- **Una suma de comprobación mala** se descarta y se cuenta: un cable con ruido se nota en el contador, no en posiciones falsas.
- **El barco propio** se mueve con la RMC si el receptor tiene fijado (estado A); con estado V no se mueve. Un `!AIVDO` sirve de posición
  solo si no hay una RMC con fijado de los últimos 10 s.

## Probarlo

```
cliente_telemetria D:\QtPro\Recursos                       (escucha UDP 10110)
emisor_nmea --escala=10                                    (otra consola)
```

o sin red, con una captura: `emisor_nmea --salida=captura.nmea` y luego `cliente_telemetria <paquete> --reproducir=captura.nmea`
(o *Archivo → Reproducir una captura NMEA*). Sin nada, *Archivo → Cargar el escenario de demostración* pone el propio y tres contactos.

Un receptor real: configurarlo para emitir UDP a la IP de este PC y a ese puerto. Un solo programa a la vez puede escuchar un puerto
(si falla, la barra de estado dice por qué).

`--comprobar` no abre ventana ni usa el puerto 10110: abre un puerto libre y comprueba, con tramas conocidas, el propio, la tabla
(ordenada por distancia y con la distancia comprobada a mano), la fusión de estáticos, el **mensaje AIS publicado en la documentación de gpsd**
(MMSI 477553000, 47.582833° N, 122.345833° O), el ruido, el contacto perdido y su borrado, **un datagrama UDP real por localhost**,
una captura en fichero y el glosario (lo usa `herramientas/probar_instalacion.sh`). `--captura=imagen.png [--espera=3]` guarda una
imagen; con `--puerto=` o `--reproducir=` fotografía lo que llegue por ahí.

## Límites

- **Solo UDP y fichero.** Muchas fuentes AIS por red son TCP (por ejemplo, AIS-catcher): `Decoder::feed` ya vale para un flujo, falta
  el `QTcpSocket`.
- Solo los tipos AIS que entiende la biblioteca (1, 2, 3, 5, 18, 19, 24). Los demás (estaciones base, ayudas a la navegación...) se
  cuentan como «ignoradas».
- **Probado con el emisor y con un mensaje publicado, no con un receptor ni una captura reales.**
- Las alertas de proximidad, las zonas y el CPA están en `monitor_trafico`; aquí no se repiten para no ocultar lo que enseña este ejemplo.
- La tabla se reconstruye cada segundo: con cientos de contactos conviene cambiarla por un modelo.
- En una red con varios receptores se pueden mezclar fuentes: no se distinguen por origen.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`cliente_telemetria.pro`](cliente_telemetria.pro) con el kit con el que se instalaron las bibliotecas
(`qmake/libmapa/libmapa.pro` instala libmapa y libtelemetria juntas). También se compila junto a los demás ejemplos.
Qt Network es una dependencia de la aplicación: al desplegar hay que llevar también su biblioteca (`windeployqt` la detecta).
