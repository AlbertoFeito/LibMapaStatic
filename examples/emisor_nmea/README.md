# emisor_nmea

Un receptor de **NMEA 0183 y AIS simulado** que emite por UDP, hecho con [`libtelemetria`](../../libtelemetria). Es la
pareja de [`cliente_telemetria`](../cliente_telemetria): lo que este escucha, esto lo emite. Sirve para probar un cliente
sin hardware, para generar capturas reproducibles y como ejemplo de cómo se **codifica** con la biblioteca.

A diferencia de los demás ejemplos **no usa libmapa ni tiene ventana**: solo `libtelemetria` y Qt Network (que pone la
aplicación; ni libmapa ni libtelemetria la enlazan).

## El escenario

Una mañana frente a La Habana, con cinco barcos:

| Barco | Qué es | Qué emite |
|---|---|---|
| **PROPIO** | El que lleva el receptor, 10 nudos al este | RMC, GGA, VTG y HDT cada segundo, y su informe `!AIVDO` cada 10 s |
| HABANA STAR | Carguero de clase A, de frente por el mismo carril | Tipo 1 cada 5 s, tipo 5 (dos frases) cada 30 s |
| PESCADOR 7 | Pesquero de clase B, casi parado | Tipo 18 cada 15 s, tipo 24 (partes A y B) cada 30 s |
| TANKER SUR | Petrolero de clase A, rumbo noreste | Tipo 1 y tipo 5 |
| REMOLCADOR 3 | Clase A, **se calla a los 90 s** | Tipo 1 y tipo 5 hasta entonces: sirve para ver un contacto perdido |

Y ruido, como en la realidad: una frase con la suma de comprobación estropeada cada 20 s, una `GSV` (que ningún cliente
decodifica) cada 40 s y una frase cortada cada 60 s. Un cliente decente lo cuenta y sigue.

## Uso

```
emisor_nmea [--host=127.0.0.1] [--puerto=10110] [--escala=1] [--duracion=0] [--agrupar]
emisor_nmea --salida=captura.nmea [--duracion=120]
emisor_nmea --comprobar
```

- `--escala=N`: cada segundo real avanza N segundos simulados (los barcos se mueven más rápido; los datos estáticos salen antes).
- `--duracion=S`: termina a los S segundos simulados (0 = hasta cerrar).
- `--agrupar`: todas las frases de un segundo en un solo datagrama, separadas por saltos de línea, como hacen muchos
  multiplexores. Por omisión va una frase por datagrama.
- `--salida=fichero`: en vez de la red, escribe la captura en un fichero (120 s simulados por omisión), que se reproduce
  con `cliente_telemetria --reproducir=fichero`.

Probarlo con el cliente:

```
cliente_telemetria D:\QtPro\Recursos --puerto=10110      (en una ventana)
emisor_nmea --escala=10                                   (en otra)
```

## Qué enseña de libtelemetria

`formatRmc`, `formatGga`, `formatVtg`, `formatHdt`, `encodeAis` (una o varias frases, canal, identificador de secuencia,
`!AIVDO` para el propio barco) y `formatSentence` para una frase cualquiera (la `GSV` del ruido).

## `--comprobar`

Sin red de verdad: simula 120 s (607 líneas), las **decodifica con la propia biblioteca** y compara con lo simulado:
120 RMC/GGA/VTG/HDT del propio y 12 `!AIVDO`; el ruido inyectado es exactamente lo que el decodificador cuenta (6 sumas
malas, 2 cortadas, 3 GSV); ningún fragmento AIS se pierde; los 4 ajenos llegan con su posición a menos de 0,08 NM de la
simulada, y sus nombres, indicativos, tipos y esloras por los tipos 5 y 24; el remolcador deja de dar informes a los 90 s;
la última RMC coincide con la posición simulada a 1/10000 de minuto. Y manda datagramas por UDP a localhost y comprueba que
llegan tal cual, uno por frase o agrupados.

## Límites

- Los barcos se mueven en **línea recta** a rumbo y velocidad constantes, sin cartografía: pueden atravesar tierra.
- Solo UDP y fichero. No hay TCP ni puerto serie.
- No es una verificación independiente del decodificador (codifica y decodifica la misma biblioteca): por eso las pruebas de
  libtelemetria usan además el mensaje AIS publicado en la documentación de gpsd y las sumas de frases publicadas.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`emisor_nmea.pro`](emisor_nmea.pro) con el kit con el que se instaló (necesita `telemetria.pri`).
También se compila junto a los demás ejemplos (`examples/CMakeLists.txt`, `examples.pro`).
