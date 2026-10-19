# monitor_trafico

Monitor de tráfico sobre un paquete de datos de libmapa: **buques y aeronaves en tiempo real**, con una
**tabla de contactos** ordenada por distancia (rumbo, velocidad, CPA, TCPA, estado), **alertas** de
proximidad y de zona, **contactos perdidos** y un **glosario** de los términos que usa la ventana.
Es el esqueleto de cualquier app alimentada por un decodificador de AIS, radar o ADS-B.

Se diferencia de [`app_seguimiento`](../app_seguimiento), que enseña el API de objetivos con tres buques,
en lo que añade una aplicación de verdad: la tabla, los contactos que dejan de llegar y el glosario.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Alimentar el mapa con partes (posición, rumbo, velocidad, hora) | `addTarget`, `updateTarget`, `setTargetVelocity` |
| Tabla con distancia, CPA y TCPA respecto al propio | `targetCpa` (`Proximity.h`), `proximityAlerts` |
| Alerta de proximidad (0,5 NM en 15 min) | `setProximityAlert`, señales `proximityAlertRaised/Cleared` |
| Zona vigilada con aviso de entrada y salida | `watchZone`, señales `targetEnteredZone/LeftZone` |
| Contactos perdidos: atenuarlos y, más tarde, borrarlos | `setTargetAttribute` + `TargetFilter::predicate` (`dim`), `removeTarget` |
| Filtro por tipo y agrupar al alejar | `setTargetFilter`, `setTargetClustering` |
| Selección enlazada tabla ↔ mapa y marcación hasta el elegido | `setSelectedTarget`, señal `targetClicked`, `addBearingLine`, `removeRangeMark` |
| Anillos de distancia alrededor del propio | `addRangeRings` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## El glosario

La ventana trae un panel **Glosario** (menú *Ayuda → Glosario*, tecla F1) con los 20 términos que usa:
contacto, propio, rumbo (COG), velocidad (SOG), distancia, CPA, TCPA, estado, contacto perdido, alerta de
proximidad, zona vigilada, marcación (EBL), anillos de distancia (VRM), estela, atenuar, agrupar, NM, kn y
tiempo simulado. Se puede buscar por nombre o por palabra de la definición, y la definición de cada
columna sale también al pasar el ratón por su cabecera.

`--comprobar` verifica que el glosario **define todas las cabeceras de la tabla y los demás términos de
la interfaz**: si añades una columna o un aviso con un término nuevo y no lo defines, la prueba falla.
Para tu app, cambia las definiciones por las de tu dominio.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
monitor_trafico D:\QtPro\Recursos
```

El simulador arranca ocho contactos cerca de La Habana y deja pasar 30 s de tiempo simulado por cada
segundo real:

- **Frontal** viene de proa contra el propio: CPA casi 0 y alerta de proximidad a los pocos pasos.
- **Pesquero 2** se queda sin señal a los 150 s: a los 90 s sin datos pasa a *Perdido* (gris, sin CPA, atenuado
  en el mapa) y a los 5 min se borra, con su aviso en el registro.
- Elige una fila: el contacto se selecciona en el mapa y se dibuja la marcación del propio hasta él. Pincha un
  contacto en el mapa: se elige su fila.

`--comprobar` no abre ventana: avanza el reloj simulado a mano y comprueba la tabla, el CPA de la proa contra
proa, la alerta, el contacto perdido y su borrado, el filtro, la selección y el glosario
(lo usa `herramientas/probar_instalacion.sh`). `--captura=imagen.png` guarda una imagen de la ventana.

## Para conectar un feed real

Sustituye la clase `Simulador` por tu decodificador y llama a `Ventana::recibir` con cada parte:

```cpp
Parte p;
p.nombre = "Carguero A";            // o el MMSI
p.tipo = "buque";
p.posicion = QGeoCoordinate(23.2, -82.4);
p.rumbo = 250.0;                    // COG, grados verdaderos
p.nudos = 12.0;                     // SOG
p.hora = QDateTime::currentDateTimeUtc();   // la hora del mensaje
ventana.recibir(p);
```

Un cronómetro llama a `revisar()` para marcar y borrar los contactos que dejan de llegar. Con la hora real
de cada parte, quita `kEscala` y el reloj simulado de `paso()`.

## Detalles que conviene saber

- El CPA y el TCPA salen de la velocidad y el rumbo que da el feed (`setTargetVelocity`), no de la traza.
  Se calculan en un plano local: son exactos a decenas de km y se notan menos fiables a cientos.
- **Estado «Alerta»** marca a todo contacto que forme parte de *cualquier* alerta de proximidad, no solo
  de las del propio: dos contactos que se cruzan entre sí también lo activan.
- Un contacto perdido **no tiene CPA** (ya no se sabe por dónde va) y no dibuja marcación.
- Los umbrales (90 s perdido, 5 min borrado, 0,5 NM y 15 min de alerta) son constantes al principio de
  `main.cpp`; ajústalos a la frecuencia de tu feed (un AIS informa cada 2–10 s en movimiento, un ADS-B cada 1–2 s).
- Con el paquete mínimo del CI (teselas de 1 px, zoom 0–2) el mapa se ve como el mundo entero; con tu paquete
  real se ve el entorno de La Habana.

## Compilar

CMake: `cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"`.
qmake: abrir [`monitor_trafico.pro`](monitor_trafico.pro) con el mismo kit con el que se instaló la
librería. También se compila junto a los demás ejemplos desde
[`examples/CMakeLists.txt`](../CMakeLists.txt) o `examples.pro`.
