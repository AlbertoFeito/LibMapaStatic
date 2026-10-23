# Port de tipos AIS a libtelemetria (2026-10-22)

Contribución preparada desde el proyecto Sas-3 (Randolo): su decodificador
`CAisDecoder` cubre los 27 tipos de ITU-R M.1371, y libtelemetria solo
decodificaba 1, 2, 3, 5, 18, 19 y 24. Este port traslada la **cobertura** de
aquel a la **ingeniería** de esta biblioteca: misma estructura única
(`AisMessage`), mismos criterios («lo que no se da, no se inventa»: NaN / −1 /
0 según el campo), memoria acotada y sin tocar la API existente. Los campos
nuevos son aditivos; ningún consumidor actual se rompe.

Queda **en el árbol local, sin commit ni push**: la decisión de subirlo es del
autor del proyecto Sas-3 y del mantenedor de este repositorio.

## Tipos portados y campos

Todos en `decodeAis` **y** en `encodeAis` (ida y vuelta probada), decodificando
solo lo que define M.1371:

| Tipo | Qué | Campos de `AisMessage` |
|---|---|---|
| 21 | Ayuda a la navegación (AtoN) | `atonType`, `name` (20 caracteres **más la extensión** si viene, unidos), `lat`/`lon`, `accuracy`, dimensiones en `toBowM`/`toSternM`/`toPortM`/`toStarboardM`, `epfsType`, `second`, `offPosition`, `virtualAton`, `assigned` |
| 4 | Estación base | `utcYear`…`utcSecond` (fecha y hora UTC completas), `lat`/`lon`, `accuracy`, `epfsType` |
| 11 | Respuesta UTC/fecha | Igual que el 4 (misma estructura en M.1371) |
| 9 | Aeronave SAR | `altitudeM`, `lat`/`lon`, `sogKn` (**nudos enteros**, no décimas), `cogDeg`, `second`, `accuracy`, `assigned`; sin proa (`headingDeg` queda −1) |
| 12 | Texto de seguridad direccionado | `text`, `destMmsi`, `sequence`, `retransmit` |
| 14 | Texto de seguridad difundido | `text` |
| 6 | Binario direccionado | `destMmsi`, `sequence`, `retransmit`, `dac`, `fid`, `binaryData`/`binaryBits` (carga cruda) |
| 8 | Binario difundido | `dac`, `fid`, `binaryData`/`binaryBits` |
| 7 / 13 | Acuses | `ackMmsi` + `ackSequence` (hasta 4; la secuencia son **2 bits**, 0–3) |
| 10 | Petición UTC/fecha | `destMmsi` |
| 15 | Interrogación | `interrogations` (hasta 2 estaciones; a la primera se le pueden pedir 2 tipos) |
| 16 | Modo asignado | `assignments` (1 o 2: MMSI, offset, incremento) |
| 17 | Correcciones DGNSS | `dgnssLat`/`dgnssLon` (posición de la **estación**, en 1/10 de minuto; no va en `lat`/`lon` para que `hasPosition()` no la confunda con la del emisor), `binaryData`/`binaryBits` |
| 20 | Enlace de datos | `reservations` (1–4: offset, `slotCount`, timeout, incremento) |
| 22 | Gestión de canales | `channelA`/`channelB`, `txRxMode`, `lowPower`, `addressed`; si es direccionado, `destMmsi`/`destMmsi2`; si no, área `neLat`/`neLon`/`swLat`/`swLon` (1/10 de minuto); `bandChannelA`/`bandChannelB`, `zoneSize` |
| 23 | Asignación a grupo | área NE/SO, `stationType`, `shipType`, `txRxMode`, `reportInterval`, `quietTime` |
| 25 | Binario de un intervalo | `addressed`, `structured`, `destMmsi`, `dac`/`fid` si estructurado, `binaryData`/`binaryBits` |
| 26 | Binario de varios intervalos | Igual que el 25; los últimos 20 bits (estado de comunicaciones) **no** cuentan como datos |
| 27 | Largo alcance | `lat`/`lon` (1/10 de minuto), `navStatus`, `accuracy`, `sogKn` (nudos enteros; 63 = no disponible), `cogDeg` (grados enteros; 511 = no disponible), `gnss` |

Completado además en un tipo ya existente: el **19** ahora rellena también
`epfsType` y `assigned` (estaban en el mensaje y no se leían).

Ayudantes de texto nuevos, en español como los existentes:
`aisAtonTypeText(int)` (tabla de tipos de ayuda de M.1371) y
`aisEpfsTypeText(int)`.

### Decisiones que conviene conocer

- **`Reservation::slotCount`, no `slots`.** El nombre natural chocaba con la
  macro `slots` de Qt: el campo se declaraba bien en la cabecera, pero
  cualquier acceso `reserva.slots` desde código con las palabras clave de Qt
  activadas (todo QObject) fallaba al compilar. Detectado por los tests.
- **Los binarios no se interpretan.** En 6/8/17/25/26 la carga de aplicación se
  entrega cruda (`binaryData` + `binaryBits`, con `dac`/`fid` cuando el tipo
  los trae): qué significa depende de la aplicación (DAC/FID), igual que hace
  el decodificador de origen con su `datosHex`. Decodificar aplicaciones
  concretas (meteorología IMO 236/289, etc.) queda fuera, a propósito.
- **El estado de radio/comunicaciones no se expone** (tipos 1, 4, 9, 26…),
  coherente con lo que la biblioteca ya hacía con los tipos originales.
- **Codificación del 20:** con una sola reserva el mensaje suma 70 bits y el
  mínimo de M.1371 son 72; `encodeAis` añade los 2 bits de repuesto finales.
- **Codificación del 26:** el estado de comunicaciones se escribe a cero al
  codificar (no se guarda en `AisMessage`), como ya se hacía con los campos
  de radio de los tipos originales.

## Reinstalación qmake (previa al port)

Las instalaciones de `~/workspace/libmapa-inst/` estaban cruzadas, como se
sospechaba: la de **qt6** traía un `telemetria_qt.pri` que decía
`TELEMETRIA_QT_MAJOR = 5` / `5.15.13` (una compilación con Qt 5 instalada en
el prefijo de Qt 6), y en la de **qt5** ni siquiera existían
`telemetria.pri`/`telemetria_qt.pri`. Se reinstaló **en secuencia** (nunca en
paralelo: el `.pro` genera `telemetria_qt.pri` y dos builds a la vez se lo
pisan), primero Qt 5 y después Qt 6, y se repitió la misma secuencia al
terminar el port para que las instalaciones lleven el código nuevo.
Resultado verificado en ambos prefijos: qt5 → `5 / 5.15.13`, qt6 → `6 / 6.4.2`,
con cabeceras y bibliotecas (`libtelemetria.a`, `libtelemetriad.a`) presentes.

## Pruebas

En `tests/tst_ais.cpp` (estilo gpsd de la casa: vectores fijos + ida y vuelta):

- **Nuevos:** `type21AtonVector`, `type4And11UtcVector`, `type9SarVector`,
  `safetyTextVectors` (12/14), `binaryVectors` (6/8),
  `ackRequestInterrogationVectors` (7/13/10/15), `dgnssAndLinkVectors`
  (17/20), `channelAndGroupVectors` (22 ×2 formas, 23),
  `singleSlotAndLongRangeVectors` (25 ×2, 26, 27),
  `roundTripManagementTypes` (16/17/20/22/23/26/27 por `encodeAis`) y los
  textos de `aisAtonTypeText`/`aisEpfsTypeText` en `textLabels`.
- **Ajustados por el cambio de comportamiento** (el tipo 4 ya no es «no
  soportado»): `unsupportedTypeKeepsMmsi` (tst_ais) y
  `ownShipAndUnsupportedAis` (tst_decoder) usan ahora el tipo **28**
  (reservado) como ejemplo de tipo no decodificado, con la misma intención
  que tenían.

Resultados (0 avisos con `-Wall -Wextra -Wconversion` y compañía):

| Suite | Qt 5.15.13 (CMake/ctest) | Qt 6.4.2 (qmake) |
|---|---|---|
| tst_nmea | 14/14 | 14/14 |
| tst_ais | 24/24 | 24/24 |
| tst_decoder | 10/10 | 10/10 |

Nota sobre el Qt 6: en esta máquina, `find_package(QT NAMES Qt6 Qt5 …)` de
CMake resuelve **Qt 5** aunque Qt 6 esté instalado — el `Qt6Config.cmake` de
Debian (6.4.2) se niega a cargarse cuando `CMAKE_FIND_PACKAGE_NAME` no es
exactamente `Qt6` (línea 125 del propio fichero), y CMake pasa entonces al
siguiente nombre. Por eso la validación de Qt 6 se hizo con qmake (la vía que
el propio repositorio documenta para la biblioteca), verificando que los
binarios enlazan `libQt6Core.so.6`. Con `find_package(Qt6 …)` directo, CMake
sí encuentra Qt 6.4.2.

## Verificación cruzada contra el decodificador de Sas-3

Los vectores de los tests **no** salieron de ninguno de los dos
decodificadores: se generaron con un codificador Python escrito desde la
tabla de M.1371 para esta verificación (22 vectores, uno o dos por tipo).
Después, cada vector se decodificó con **ambos** decodificadores (el
`CAisDecoder` real de Sas-3, con su parser y reensamblador, y
`telemetria::decodeAis`) y se compararon los campos clave:

**160 campos coincidentes, 5 diferencias — y las 5 son fallos del
decodificador de Sas-3, no del port:**

1. **Tipos 4 y 11, minuto y segundo (4 campos).** Sas-3 lee el minuto en los
   bits 66–70 (5 bits) y el segundo en 72–76 (5 bits); M.1371 define **6 bits**
   para cada uno (66–71 y 72–77). Con el vector de las 18:37:42, Sas-3 da
   minuto 18 y segundo 21 (el valor correcto partido por dos: pierde el bit
   bajo), y el port da 37 y 42, que es lo que lleva el vector. Corregir en
   `CAisDecoder::decodificarCarga`, casos 4/11: `minuto = U(66, 6)` y
   `segundo = U(72, 6)`. El vector se eligió con minuto impar a propósito.
2. **Tipo 21, texto del tipo de ayuda (1 campo).** El código numérico
   coincide (20), pero la tabla de textos de Sas-3 no es la de M.1371 para
   los valores 13–31 (p. ej. su 20 = «Peligro aislado (flotante)»; en
   M.1371, 20 = flotante cardinal N y el peligro aislado flotante es 28).
   El port usa la tabla de M.1371.

Y una diferencia de **cobertura** (no de valores): en los tipos 7/13, Sas-3
solo expone el primer acuse (`mmsi_1`); el port devuelve los cuatro posibles
con sus secuencias. El primer acuse coincide en ambos vectores.

## Pendientes

- **Ningún tipo de M.1371 queda sin portar:** 1–27 están todos (los que
  faltan en la numeración no existen). Los tipos reservados/futuros (0,
  28–31) siguen devolviéndose como `supported = false` con tipo y MMSI.
- Sin probar con un receptor AIS real, como el resto de la biblioteca.
- Los dos fallos detectados en el decodificador de Sas-3 (minuto/segundo de
  5 bits y tabla de textos AtoN) quedan **anotados aquí, sin corregir**:
  ese código no era parte de este encargo.
