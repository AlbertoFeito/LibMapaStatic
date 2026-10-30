# Análisis: ¿cumple la aplicación con lo que se pidió?

Contraste de **`laboratorio_anillos`** y de la librería con lo que pidió el equipo de la versión extendida en sus
siete documentos (propuesta original y modificada, aclaración, tres respuestas y los anexos A, B y C), visto desde quien
lo usaría: un **operador** que vigila objetivos, zonas y puntos en un mapa sin conexión.

## 1. Qué se pidió y para qué

El objetivo común de todo lo pedido es el mismo: que el operador **lea rápido y sin errores** lo que hay en pantalla y
**decida él** cómo verlo. Cada petición lo sirve de una manera:

| Petición (documento) | Para qué la quieren (el objetivo operativo) | Librería | Ejemplo antes | Veredicto |
|---|---|---|---|---|
| Formulario de objetivo arrastrable, línea a la esquina más cercana, soltar fuera cancela, color de selección (§4.7, Anexo A) | **Legibilidad**: cuando varios objetivos están cerca los formularios se pisan; el operador los aparta | Cumple (`targetLabelMoved`, `labelCornerFor`, offset por defecto) | Solo había 3 objetivos fijos y lejos entre sí: **no se podía comprobar el solape** | Corregido: «Agregar 3 objetivos pegados», agregar con un clic y simulación de movimiento |
| Formulario multicolor: color por segmento, 8+8, ancho configurable (resp. 3 §2) | **El color es información operativa**: identificación en el primer segmento, estado (detección, emergencia) en el segundo | Cumple, **pero faltaba cambiarlo**: no había forma de cambiar el rótulo por segmentos de un objetivo ya creado sin borrarlo y perder su traza | No se podía cambiar el estado | **Brecha real cerrada**: `MapWidget::setTargetLabelContent`; en el ejemplo, el estado elegido cambia el color del segmento |
| `LabelAnchor` (§4.4) | **Evitar solapes**: poner los nombres de elementos cercanos en lados distintos | Cumple (10 valores, `Auto` por defecto) | Un solo lado para **todos** los puntos a la vez: no cumplía el objetivo | Corregido: lado y fuente **por elemento** (y «aplicar a todos») |
| `labelFontSize` (§4.6) | Adaptar el tamaño a la pantalla y a la importancia de cada elemento | Cumple | Igual: un solo tamaño para todos | Corregido: por elemento |
| `colorOverride` / `PartInfo` (§4.5, Anexo C) | **Estado dinámico**: sensores activos en verde, inactivos en gris, en alarma en rojo; destacar una provincia | Cumple (`PartInfo::colorOverride`) | Los colores salían de una paleta fija: no se podía cambiar el estado de un elemento | Corregido: botones Activo, Inactivo y Alarma sobre la parte elegida |
| `SubEntity` → `PartInfo`, `MapHit::subIndex` (§4.1, §5) | Agrupar elementos con el mismo icono (18 aeródromos = 1 entidad): **memoria y tiempo** | Cumple; medido con 5.000 puntos (BITACORA §177) | Sí | Cumple. Falta medir el **dibujado** en pantalla real |
| `category` / `entity` (§4.2) | Filtrar («solo aeródromos internos»), panel de capas con casillas, borrar una categoría de una vez | Cumple por equivalencia (capa y tipo) con recuento, borrado y visibilidad por tipo | Tres casillas **fijas** («sectores», «sitios», «puntos»): no era el panel de capas que se pedía | Corregido: pestaña **Capas**, árbol con todas las capas y tipos con sus cuentas, «solo este tipo» y «borrar este tipo» |
| `Circle` / `Arc` (§4.3, Anexo B) | **Zonas de cobertura, TMA, sectores de radar** que el operador tiene que vigilar | Cumple (geodésicos; barridos del Anexo B probados) | Los «sectores» salían como arcos sueltos, sin área | Corregido: el sector es una **cuña rellena**, se gestiona uno por uno y dice qué objetivos tiene dentro |
| Gráfico polar: 30 o 60 anillos, 36 radiales, un anillo de cada cinco y un radial de cada tres más oscuros, grises (Anexo C, resp. 3 §4) | Medir **distancia y marcación** desde un punto con lectura rápida | Cumple en **un solo** `RangeRings` (tope 100, `majorRingEvery`, `majorSpokeEveryDeg`, `majorColor`, `spokeColor`) | Solo en rojo; los grises del Anexo C no estaban | Corregido: selector de colores con los grises del Anexo C |
| Medidas de la fase 4 (resp. 3 §5) | Decidir si la fase merece la pena | Hechas por nosotros | n/a | Cumple (con la salvedad del dibujado) |

## 2. Por qué resultaba confuso (visto como operador)

1. **Un solo panel largo** con doce grupos mezclados: anillos, rótulos, sectores, formularios... Había que desplazarse
   para dar con cada cosa y no se sabía para qué servía cada una. → **Pestañas** (Anillos, Sectores, Puntos, Objetivos,
   Capas, Ayuda), cada una con una línea que dice **para qué sirve**; la lectura del ratón y de la última entidad tocada
   queda siempre a la vista debajo.
2. **Controles globales donde el objetivo era por elemento** (lado del rótulo, fuente, color): cambiaban todo a la vez.
3. **Datos fijos de demostración** (puntos, sitios, tres objetivos) que no se podían cambiar ni ampliar.
4. **Nombres técnicos en pantalla** («una entidad, tres partes»): ahora se dice lo que se hace («Agregar como parte de
   Sitios»).
5. **Sin respuesta al tocar**: ahora, al tocar una entidad en el mapa se elige también en la lista de su pestaña.

## 3. Lo que sigue sin cumplir o conviene decidir

| Tema | Situación | Propuesta |
|---|---|---|
| **RangeRings: ¿uno o dos?** | El compañero pidió **dos** objetos (círculos y radiales) en su 3.ª respuesta; aquí se decidió **uno** con `spokeColor` y tope 100 (cubre sus 96). Esa decisión estaba en una respuesta que no se ha enviado | Confirmar que lo sabe: si implementa su lado esperando dos objetos, no encajará |
| **Nombres de los parámetros** | Pidió `labelMaxChars`, `labelLine1Seg1Chars` y `labelLine1Seg2Chars`; aquí son `TargetLabelFormat::maxChars` y `segmentChars` | Decir la equivalencia en su respuesta o añadir los alias |
| **Color de selección** | Su fragmento usaba azul; el acuerdo es el actual (negro) con `setTargetLabelSelectionColor` | Sin cambio: se elige con el botón de la pestaña Objetivos |
| **Visibilidad por tipo** | Un tipo se oculta en **todas** las capas (no «el tipo X solo dentro de la capa Y») | Documentado; si hace falta por capa, es otra fase |
| **Radio de un círculo con el ratón** | No hay tiradores: el radio se cambia por valores (acordado dejarlo para después) | Sin cambio |
| **Recuento de objetivos dentro de un sector** | Se recalcula al cambiar un sector y en cada paso de la simulación, no con cada movimiento real | Si se necesita en tiempo real, engancharlo a `updateTarget` en la aplicación |
| **Dibujado con 5.000 elementos** | Sin medir en pantalla real | Medirlo en tu máquina con el mapa de costas |

## 4. Cómo comprobarlo

`laboratorio_anillos` → pestaña **Ayuda**: qué hace y cómo comprobar cada pestaña. `--comprobar` ejecuta lo medible sin
pantalla (escala, sectores uno por uno, partes, estado de un objetivo y color de una parte).
