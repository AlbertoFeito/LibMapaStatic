# libtelemetria

Decodificador de **NMEA 0183** y **AIS** para Qt (5.14, 5.15 y 6.x). Convierte las líneas de texto que llegan de un
receptor —por UDP, TCP, un puerto serie o un fichero de captura— en datos: la posición y el rumbo del barco propio
y los mensajes AIS de los demás. También los **codifica**, para simular un receptor y para probar el decodificador.

Es una biblioteca **aparte de libmapa**: no sabe nada de mapas, ventanas ni red. Solo usa **Qt Core** y no hace
entrada/salida (se le dan bytes y devuelve datos), así que sirve igual en una app de ventana, en una herramienta de
consola o en un test. Quien lee la red es la aplicación (ver [`examples/cliente_telemetria`](../examples/cliente_telemetria)).

## Qué entiende

| Protocolo | Qué |
|---|---|
| NMEA 0183 | `RMC` (hora, posición, velocidad, rumbo, fecha), `GGA` (calidad del fijado, satélites, HDOP, altitud), `VTG` (rumbo y velocidad), `HDT` (rumbo de la proa). Cualquier otra frase correcta se entrega como «ignorada» con su dirección. |
| AIS (`!AIVDM`, `!AIVDO`) | Tipos **1, 2, 3** (posición clase A), **5** (datos estáticos y del viaje, clase A, en dos frases), **18** y **19** (posición clase B, la 19 con nombre y tamaño) y **24** (estáticos clase B, partes A y B). Los demás tipos se entregan con su tipo y su MMSI, marcados como no soportados. |

## Uso

```cpp
#include <telemetria/Decoder.h>
using namespace telemetria;

Decoder decodificador;
// Por cada datagrama UDP que llegue (cada uno trae líneas completas):
for (const Event &e : decodificador.feedDatagram(datagrama)) {
    switch (e.kind) {
    case Event::Kind::Rmc: /* e.rmc.lat, e.rmc.lon, e.rmc.sogKn, e.rmc.cogDeg, e.rmc.valid */ break;
    case Event::Kind::Ais: /* e.ais.mmsi, e.ais.name, e.ais.lat/lon, e.ais.hasPosition(), e.ownShip */ break;
    case Event::Kind::Bad: /* e.detail dice por qué (suma mala, sin suma, ...) */ break;
    default: break;
    }
}
const DecoderStats &s = decodificador.stats();   // líneas, suma mala, ignoradas, fragmentos perdidos...
```

- **Flujos** (TCP, fichero, serie): `feed(bytes)`. Una línea a medias espera al siguiente trozo.
- **UDP**: `feedDatagram(bytes)`. Lo que llegue es de líneas completas aunque la última no acabe en salto de línea.
- **Una línea suelta**: `feedLine(linea)`.
- **A mano**, sin `Decoder`: `parseSentence`, `decodeRmc`/`decodeGga`/`decodeVtg`/`decodeHdt`, `decodeAis` y
  `AisAssembler` (reensambla los mensajes de varias frases).
- **Codificar**: `formatRmc`/`formatGga`/`formatVtg`/`formatHdt`, `encodeAis` (una o varias frases) y `formatSentence`.

## Cosas que conviene saber

- **Lo que no se da, no se inventa.** Un campo vacío es NaN; un AIS con 181° de longitud, 91° de latitud, 1023 de velocidad,
  3600 de rumbo o 511 de proa es «no disponible» (NaN / −1), no un dato. `AisMessage::hasPosition()` dice si hay posición.
- **La suma de comprobación se exige** (`parseSentence(linea, requireChecksum = true)`). Una frase sin suma, con la suma mal
  o cortada se rechaza y se cuenta aparte (`badChecksum` / `malformed`). Las muy antiguas que la omiten se aceptan con `false`.
- **Los datos estáticos llegan aparte de la posición** (el nombre en el tipo 5 o 24, cada pocos minutos). Quien use los
  mensajes debe fundir por MMSI; `cliente_telemetria` enseña cómo.
- **Memoria acotada.** Una fuente que empieza mensajes de varias frases y no los termina no hace crecer el ensamblador
  más allá de 32 pendientes, y una línea de más de 4096 bytes sin salto se corta como mal formada.
- **Fechas en UTC sin avisos.** `QDateTime(fecha, hora, Qt::UTC)` está obsoleto desde Qt 6.9 y `QTimeZone::UTC` no existe antes de la 6.5:
  `utcDateTime(fecha, hora)` hace lo correcto con cada Qt (Rmc::utc la usa, y los ejemplos también).
- **No hay hilos ni señales.** Si se usa desde varios hilos, un `Decoder` por hilo.

## Cómo se verificó

3 tests (`tst_nmea`, `tst_ais`, `tst_decoder`):
- Las sumas de las frases de ejemplo de uso común (RMC `6A`, GGA `47`, VTG `48`) y el **mensaje AIS publicado en la
  documentación de gpsd** (`!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C`: MMSI 477553000, 47.582833° N, 122.345833° O,
  rumbo 51°, proa 181°, segundo 15) salen exactos.
- Ida y vuelta de cada tipo AIS (clase A, B, estáticos en dos frases, las dos partes del 24), con coordenadas negativas y
  extremas (el complemento a dos de 28 y 27 bits).
- Reensamblado fuera de orden, entrelazado por canal y con memoria acotada; cargas malas; el decodificador de flujo con líneas
  partidas, datagramas, ruido y contadores.

**Sin probar:** con un receptor AIS real ni con una captura real de más de unos cientos de mensajes. Los tipos que no se
decodifican (4, 9, 21...) quedan como «no soportados»; la codificación que sale de `encodeAis` es la de un receptor, no
una verificación independiente del decodificador (por eso el mensaje publicado de referencia).

## Compilar

CMake, sola: `cmake -S libtelemetria -B build-telemetria && cmake --build build-telemetria && ctest --test-dir build-telemetria`.
Con el resto del repositorio entra sola (`LIBMAPA_BUILD_TELEMETRIA`, activada por omisión) y se instala en el mismo
prefijo que libmapa:

```cmake
find_package(libtelemetria REQUIRED)
target_link_libraries(miapp PRIVATE telemetria::telemetria)
```

qmake: `qmake/libmapa/libmapa.pro` también la compila e instala (junto a libmapa); sola, `qmake/libtelemetria/telemetria.pro`.
En el `.pro` de la aplicación: `include(C:/libmapa/qt$${QT_MAJOR_VERSION}/telemetria.pri)`.
