# emisor_nmea

Un receptor de **NMEA 0183 y AIS simulado** que emite por UDP, hecho con [`libtelemetria`](../../libtelemetria). Es la
pareja de [`cliente_telemetria`](../cliente_telemetria): lo que este escucha, esto lo emite. Sirve para probar un cliente
sin hardware, para generar capturas reproducibles y como ejemplo de cómo se **codifica** con la biblioteca.

A diferencia de los demás ejemplos **no usa libmapa ni tiene ventana**: solo `libtelemetria` y Qt Network (que pone la
aplicación; ni libmapa ni libtelemetria la enlazan).

## El escenario

Una mañana frente a La Habana, con cinco barcos, tres estaciones fijas y un catálogo con un ejemplo de cada tipo AIS:

| Barco | Qué es | Qué emite |
|---|---|---|
| **PROPIO** | El que lleva el receptor, 10 nudos al este | RMC, GGA, VTG y HDT cada segundo, y su informe `!AIVDO` cada 10 s |
| HABANA STAR | Carguero de clase A, de frente por el mismo carril | Tipo 1 cada 5 s, tipo 5 (dos frases) cada 30 s |
| PESCADOR 7 | Pesquero de clase B, casi parado | Tipo 18 cada 15 s, tipo 24 (partes A y B) cada 30 s |
| TANKER SUR | Petrolero de clase A, rumbo noreste | Tipo 1 y tipo 5 |
| REMOLCADOR 3 | Clase A, **se calla a los 90 s** | Tipo 1 y tipo 5 hasta entonces: sirve para ver un contacto perdido |
| FARO MORRO, BOYA CANAL | Dos ayudas a la navegación, fijas | Tipo 21 (nombre, tipo de ayuda y posición) cada 30 s (una real emite cada 3 min) |
| BASE LA HABANA | La estación base del puerto | Tipo 4 (fecha y hora UTC) cada 10 s y un aviso de seguridad, tipo 14 (dos frases), cada minuto |

### El catálogo: los 27 tipos de ITU-R M.1371

Con los barcos (tipos 1, 5, 18 y 24) y las estaciones fijas (4, 14 y 21) salen ya 7 de los 27 tipos. Para los otros, y
para cada variante, el emisor manda **un ejemplo por segundo, cada minuto**, desde `kMmsiCatalogo` (970000001) en adelante, por el canal B:

| Tipo | Ejemplo |
|---|---|
| 2 y 3 | Posición de clase A (la misma que el 1, con otra programación) |
| 6 / 8 | Binario direccionado (DAC 200, FID 10) / difundido (DAC 1, FID 31): la carga viaja sin interpretar |
| 7 / 13 | Acuse de un binario (a dos estaciones) / de un texto de seguridad |
| 9 | Aeronave de búsqueda y rescate: 1500 m, 120 nudos enteros |
| 10 / 11 | Petición de fecha y hora UTC / su respuesta de una estación móvil (con la hora simulada) |
| 12 | Texto de seguridad direccionado a un barco del escenario |
| 15 / 16 | Interrogación a dos estaciones / asignación de modo a dos |
| 17 | Correcciones DGNSS (la posición es la de la estación de referencia) |
| 19 | Posición de clase B ampliada, con nombre y tipo |
| 20 | Gestión del enlace de datos con dos reservas |
| 21 | Ayuda **virtual** con nombre de 24 caracteres (20 más la extensión) |
| 22 | Gestión de canales, **para un área** y **direccionada a dos estaciones** |
| 23 | Asignación a un grupo de estaciones |
| 25 | Binario de un intervalo, **direccionado y estructurado** y **difundido sin estructura** |
| 26 | Binario de varios intervalos, estructurado |
| 27 | Posición de largo alcance (nudos y grados enteros) |

Son 23 mensajes. Cada uno lleva en el código su comprobación (`catalogoAis()` en `main.cpp`): qué campos debe conservar al decodificarse.

Y ruido, como en la realidad: una frase con la suma de comprobación estropeada cada 20 s, una `GSV` (que ningún cliente
decodifica) cada 40 s y una frase cortada cada 60 s. Un cliente decente lo cuenta y sigue.

## Uso

```
emisor_nmea [--host=127.0.0.1] [--puerto=10110] [--escala=1] [--duracion=0] [--agrupar]
emisor_nmea --tcp=10110 [--escala=1] [--duracion=0]
emisor_nmea --salida=captura.nmea [--duracion=120]
emisor_nmea --comprobar
```

- `--escala=N`: cada segundo real avanza N segundos simulados (los barcos se mueven más rápido; los datos estáticos salen antes).
- `--duracion=S`: termina a los S segundos simulados (0 = hasta cerrar).
- `--agrupar`: todas las frases de un segundo en un solo datagrama, separadas por saltos de línea, como hacen muchos
  multiplexores. Por omisión va una frase por datagrama.
- `--tcp=PUERTO`: en vez de UDP, hace de **servidor TCP** y manda las frases (un flujo de líneas) a los clientes que se conecten, como un
  receptor AIS de red; para el modo TCP de `cliente_telemetria`. Acepta varios clientes y olvida al que se va.
- `--salida=fichero`: en vez de la red, escribe la captura en un fichero (120 s simulados por omisión), que se reproduce
  con `cliente_telemetria --reproducir=fichero`.

Probarlo con el cliente:

```
cliente_telemetria D:\QtPro\Recursos --puerto=10110      (en una ventana)
emisor_nmea --escala=10                                   (en otra)
```

## Qué enseña de libtelemetria

`formatRmc`, `formatGga`, `formatVtg`, `formatHdt`, `encodeAis` (los 27 tipos; una o varias frases, canal, identificador de secuencia,
`!AIVDO` para el propio barco) y `formatSentence` para una frase cualquiera (la `GSV` del ruido).

## `--comprobar`

Sin red de verdad: simula 120 s (675 líneas), las **decodifica con la propia biblioteca** y compara con lo simulado:
120 RMC/GGA/VTG/HDT del propio y 12 `!AIVDO`; el ruido inyectado es exactamente lo que el decodificador cuenta (6 sumas
malas, 2 cortadas, 3 GSV); ningún fragmento AIS se pierde; los 4 ajenos llegan con su posición a menos de 0,08 NM de la
simulada, y sus nombres, indicativos, tipos y esloras por los tipos 5 y 24; el remolcador deja de dar informes a los 90 s;
la última RMC coincide con la posición simulada a 1/10000 de minuto; las dos ayudas llegan 4 veces cada una, la estación base da 12 horas UTC y 2 avisos de seguridad; los 23 ejemplos del catálogo llegan dos veces y conservan sus campos, y **salen los 27 tipos AIS**. Y manda datagramas por UDP a localhost y comprueba que
llegan tal cual, uno por frase o agrupados.

## Límites

- Los barcos se mueven en **línea recta** a rumbo y velocidad constantes, sin cartografía: pueden atravesar tierra.
- UDP, servidor TCP (`--tcp=PUERTO`) y fichero. **No emite por puerto serie**: para probar el serie del cliente hace falta un par de
  puertos virtuales (com0com en Windows, `socat` en Linux) y puentear a uno de ellos el servidor TCP o la captura; esa ruta no está probada.
- No es una verificación independiente del decodificador (codifica y decodifica la misma biblioteca): por eso las pruebas de
  libtelemetria usan además el mensaje AIS publicado en la documentación de gpsd y las sumas de frases publicadas.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`emisor_nmea.pro`](emisor_nmea.pro) con el kit con el que se instaló (necesita `telemetria.pri`).
También se compila junto a los demás ejemplos (`examples/CMakeLists.txt`, `examples.pro`).
