# Propuesta final de convergencia: LibMapa y la versión extendida

Documento que cierra las tres rondas de intercambio. Recoge lo acordado, corrige las especificaciones de los anexos
contra el código actual de `main` y fija el alcance, el orden y los criterios de aceptación de cada fase.

## 1. Principios acordados

- **Solo se añade.** Ningún nombre ni comportamiento existente cambia. Todo campo nuevo lleva un valor por defecto que
  reproduce lo de hoy.
- **Una fase, una rama, un PR en borrador** contra `main`. Orden: 1 → 2 → 3 → 4.
- **Convención de la librería para los offsets:** píxeles de pantalla, Y hacia abajo. La aplicación que use otra
  convención convierte en su capa.
- `category` = capa (`layerId`) y `entity` = `type`. No se crean campos nuevos para eso.
- Las partes con datos propios se resuelven ampliando `parts` (`PartInfo` paralelo), no con `SubEntity`.

## 2. Lo que ya existe y no se vuelve a hacer

| Pedido | Ya está en `main` |
|---|---|
| Arrastre del formulario de objetivo | `MapView` (con `TargetLayer::labelAt` y `TargetModel::setLabelOffset`), con prioridad sobre el mapa |
| Offset por objetivo y línea guía | `MapTarget::labelOffsetPx` y línea al rótulo |
| Capas, borrado y visibilidad por categoría | `layerId`, `clearFeatureLayer`, `setFeatureLayerVisible`, `featuresInLayer`, `featuresOfType` |
| Gráfico polar (círculos y radiales) | `RangeRings`: `spacingMeters`, `count` (hasta 50), `spokeEveryDeg`, rótulos |

## 3. Fase 1: `claude/rotulos-ajustes`

**1a. Rótulos de entidades.**
- `FeatureStyle::labelFontSize` (int, 0 = fuente global).
- `LabelAnchor` con 10 valores: `Auto` (por defecto, comportamiento actual), `Top`, `Bottom`, `Left`, `Right`,
  `TopLeft`, `TopRight`, `BottomLeft`, `BottomRight`, `Center`. **El `Top` por defecto de su fragmento cambiaría el
  aspecto de todo lo existente**, por eso se añade `Auto`. Es la posición base de la caja: `labelOffsetPx` se suma encima
  y `labelAnchorVertex` decide a qué vértice se ancla.

**1b. Formulario de objetivo.**
- **Offset por defecto global**, en `MapWidget` (`setDefaultTargetLabelOffset` / `defaultTargetLabelOffset`), valor
  inicial `(0, 0)`. Se nombran distinto de `setTargetLabelOffset(id, …)` para evitar sobrecargas ambiguas.
- **`MapTarget::labelOffsetSet`.** Offset efectivo: el del objetivo si `labelOffsetSet` es verdadero **o** si
  `labelOffsetPx` no es `(0, 0)`; si no, el global. Así las aplicaciones que ya fijan `labelOffsetPx` a mano no se rompen.
- **Señal `MapWidget::targetLabelMoved(id, offsetPx)`** al soltar. Soltar fuera del viewport cancela y restaura el offset
  anterior (en `MapView`). No se añade `beginLabelDrag` ni similares a `TargetLayer`.
- **Color de selección del rótulo:** `MapWidget::setTargetLabelSelectionColor`, por defecto el actual (negro).
- **Línea guía a la esquina más cercana**, hoy va al centro de la caja. Función pura
  `labelCornerFor(QPointF offsetPx)`, con el azimut `atan2(dx, −dy)` sobre el offset de pantalla, normalizado a [0, 360):

  | Azimut | Esquina de la caja |
  |---|---|
  | 0° | inferior derecha (desempate) |
  | (0°, 90°] | inferior izquierda |
  | (90°, 180°] | superior izquierda |
  | (180°, 270°] | superior derecha |
  | (270°, 360°) | inferior derecha |

  Ejemplo: offset de pantalla `(-31, +31)` (formulario abajo-izquierda) → 225° → esquina superior derecha.

**1c. Anillos.** `RangeRings::majorRingEvery`, `majorSpokeEveryDeg` y `majorColor` (0 = sin énfasis): un anillo o
radial de cada N con otro color. Sustituye a `colorOverride` en el gráfico polar (Anexo C).

**1d. Formulario multicolor (solo si lo confirman).** Hoy el rótulo es un `QString label` plano. Propuesta:
`TargetLabelSegment {texto, color}`, `TargetLabelLine {segmentos}` y `MapTarget::labelContent`; vacío = se usa `label` como
hasta ahora. Fuente monoespaciada, ancho de hasta 16 caracteres y truncado **configurable** (`labelMaxChars`, 0 = sin
truncar), porque cortar en silencio pierde datos. `labelLines` sobra: se deduce del contenido.

**Tests de la fase 1** (`QT_QPA_PLATFORM=offscreen ctest`, estilo de `tests/tst_mapwidget.cpp`):
tabla `_data()` de `labelCornerFor` con 0°, 90°, 180°, 270°, 360° y un caso por cuadrante; offset efectivo con y sin
`labelOffsetSet`; `labelFontSize` con la fuente resultante; `LabelAnchor` con el rectángulo esperado en píxeles;
cancelar el arrastre al soltar fuera; la señal `targetLabelMoved`.

## 4. Fase 2: `claude/circulos-arcos`

- `GeometryKind::Circle` y `Arc` **añadidos al final** del enum (se guarda como entero en SQLite).
- `MapFeature::radiusMeters`, `arcStartDeg`, `arcEndDeg` (grados, [0, 360), sentido horario desde el norte). Un círculo o
  arco por entidad; el radio por parte queda para la fase 4.
- **Validez:** 1 vértice, `radiusMeters > 0`; en `Arc`, ambos ángulos finitos y `arcStartDeg != arcEndDeg`. Sin valor
  centinela 500: con ángulos por defecto `0, 0` ya es inválido.
- **Dibujo por muestreo geodésico** (`QGeoCoordinate::atDistanceAndAzimuth`, un punto cada pocos grados o según el tamaño
  en píxeles) y polilínea proyectada. **No `drawArc` sobre un elipse:** con radios de 300 km en Mercator el círculo no
  es un círculo en pantalla.
- **Barrido:** `sweep = fmod(arcEndDeg − arcStartDeg + 360, 360)`. Correcciones al Anexo B si alguien usa `QPainter`:
  `spanAngle = −sweep·16` (el caso 315°→45° es 90°, no 270°) y para 180°→270° `startAngle = −1440`, no `−2880`.
- Persistencia (columnas nuevas con migración), caja envolvente, hit-test, exportación a GeoJSON/KML como
  polígono o polilínea aproximada, y edición del centro. La edición del radio se deja para después.
- **Tests:** tabla de barridos (0→90, 45→135, 315→45, 180→270, 0→359, 90→270), validez, ida y vuelta por SQLite y
  puntos extremos del muestreo comprobados con `distanceTo` frente al radio.

## 5. Fase 3: `claude/agrupacion-entidades`

- `featureTypeCounts(layerId = "")`, `clearFeaturesOfType(type, layerId = "")`.
- Alta en lote con una sola señal, solo si no existe ya: se comprueba antes de escribirlo.
- Opcional, a petición suya: `setFeatureTypeVisible(type, bool)` para «mostrar solo aeródromos internos».

## 6. Fase 4: `claude/entidad-subpartes` (con medición previa)

**Antes de escribir código**, ustedes aportan las medidas con ~5.000 elementos:
memoria (`sizeof`), tiempo de alta, de borrado y de dibujado, de 1 entidad con N partes frente a N entidades.
Con eso se decide si la fase merece la pena.

Si se hace:
- `struct PartInfo { QString name; QColor colorOverride; LabelAnchor labelAnchor; }` y
  `QVector<PartInfo> partsInfo` paralelo a `parts`. Puede estar vacío; si tiene elementos, mismo tamaño que `parts`
  (`isValid()` lo comprueba).
- `moveVertex`, `insertVertex`, `removeVertex`, importación y persistencia mantienen ambos vectores sincronizados.
- `MapHit::subIndex` (−1 si no aplica) y las funciones de `MapWidget` que lo necesiten con un parámetro nuevo opcional.
- Tests de sincronización de ambos vectores en cada operación.

## 7. Criterios de aceptación de cada fase

- Compila con Qt 5.14/5.15/6.x y pasa **todos** los tests (`ctest`), sin tocar los existentes.
- Comentario `//` en español encima de cada función; el header documenta el API y el `.cpp` la implementación.
- Si se añade un `.cpp`, también en `qmake/libmapa/core.pro` o `widget.pro`.
- Entrada en `docs/BITACORA.md`; si cambia el API, también `docs/arquitectura.html` y su PDF; README al día.
- Línea en la tabla de ramas de `CLAUDE.md` y PR en borrador contra `main`.

## 8. Pendiente de ustedes

1. **Confirmar la fase 1d** (formulario multicolor) y si el ancho de 16 caracteres y el 8+8 deben ser configurables.
2. Corregir el **Anexo A**: mezcla dos convenciones (el texto dice Y hacia arriba, los diagramas de A.2.2 y A.3.2 usan
   signos de pantalla) y da el offset por defecto como (-30, -30) y como (-31, -31).
3. Aportar las **medidas de la fase 4**.
4. Decir **quién implementa cada fase**. Cada una se puede hacer por cualquiera de los dos equipos.

## 9. Una nota de CI

El job Qt6 puede fallar por el entorno y no por los cambios: el ejecutor ha traído Qt 6.10.2 en Ubuntu 26.04 sin el
plugin SQLite de Qt (`tst_mbtiles`, `tst_places`). Si pasa, no es de la fase; se corrige aparte instalando
`libqt6sql6-sqlite` en `.github/workflows/ci.yml`.
