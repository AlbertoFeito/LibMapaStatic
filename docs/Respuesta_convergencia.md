# Respuesta a la propuesta de convergencia con LibMapa

Documento para el equipo que mantiene la versión extendida. Contrasta su propuesta
(versión original y modificada, más la aclaración) con el código actual de `main`
y plantea un plan de trabajo por fases.

## 1. Resumen

- Estamos de acuerdo en converger, y en hacerlo **añadiendo, sin romper la API**.
- Parte de lo que piden **ya existe** en `main` (su comparación se hizo contra una versión antigua).
- Lo que falta se haría en **cuatro ramas independientes**, cada una con su PR, tests y documentación.
- Hay dos cosas que necesitamos de ustedes (sección 6).

## 2. Qué ya existe en `main`

| Lo que piden | Estado |
|---|---|
| Formulario de objetivo arrastrable (§4.7) | **Casi hecho.** Existen `MapTarget::labelOffsetPx`, `MapWidget::setTargetLabelOffset`, `MapWidget::targetLabelAt` (hit-test del rótulo) y la línea guía hacia el rótulo. El arrastre ya está enganchado en `MapView` (`mousePress/Move/Release`), con prioridad sobre el desplazamiento del mapa. |
| SubEntity (§4.1) | **Parcial.** `MapFeature::parts` ya permite varias geometrías en una entidad que comparten `kind`, `style`, `name` y `attributes`. Faltan nombre y color por parte, y `subIndex` en `MapHit`. |
| category / entity y `clearFeatureCategory` (§4.2) | **Parcial.** Hay capas de entidades (`layerId`, `addFeatureLayer`, `clearFeatureLayer`, `setFeatureLayerVisible`) y `featuresOfType`. No hay subgrupo ni conteos por tipo. |
| LabelAnchor (§4.4) | **Parcial.** Existen `labelOffsetPx` y `labelAnchorVertex`. No hay las 9 posiciones predefinidas. |
| Circle / Arc (§4.3), `labelFontSize` (§4.6), `colorOverride` (§4.5), `subIndex` en `MapHit` (§5) | **No existen.** |

Pendiente de verificar en el código: selección en azul del rótulo, anclaje por azimut de
la línea guía, `defaultLabelOffsetPx` global y una señal al terminar el arrastre.

## 3. Observaciones sobre la propuesta

- Las cifras de rendimiento (≈200 B frente a ≈50 B por sub-entidad, O(1)+O(N)) no están
  medidas. Conviene medirlas antes de justificar con ellas el cambio más grande.
- `GeometryKind` se guarda como entero en SQLite (`VectorRepository.cpp`). `Circle` y `Arc`
  deben añadirse **al final** del enum para no invalidar bases ya guardadas, y hay que
  completar los `switch` sobre `kind` (validación, mínimo de vértices, formatos de exportación).
- Ampliar `parts` es preferible a crear una estructura `SubEntity` paralela: evita duplicar
  `geometry` y `parts`, y mantiene la compatibilidad con lo ya persistido.

## 4. Plan por fases

Cada fase es una rama `claude/<tema>` desde `main` con su PR en borrador. Todo es aditivo,
con valores por defecto que conservan el comportamiento actual.

| Fase | Rama | Contenido | Esfuerzo |
|---|---|---|---|
| 1 | `claude/rotulos-ajustes` | `labelFontSize` (0 = fuente global). `LabelAnchor` con 9 posiciones como posición base de la caja; `labelOffsetPx` se suma encima y `labelAnchorVertex` decide a qué vértice se ancla. Cierre de lo que falte del formulario de objetivo (`defaultLabelOffsetPx`, señal al soltar, detalles visuales). | Bajo-medio |
| 2 | `claude/circulos-arcos` | `Circle` y `Arc` (`radiusMeters`, `arcStartDeg`, `arcEndDeg`) dibujados con distancia geodésica. Hit-test, caja envolvente, persistencia y exportación a GeoJSON/KML como polígono aproximado. | Medio |
| 3 | `claude/agrupacion-entidades` | Propuesta: `category` = capa y `entity` = `type`. Se añaden `featureTypeCounts`, `clearFeaturesOfType` y un alta en lote con una sola señal. Solo si no basta, un campo `category` propio. | Bajo-medio |
| 4 | `claude/entidad-subpartes` | Nombre y `colorOverride` por parte dentro de `parts`, y `MapHit::subIndex`. Antes, medir memoria y tiempo con unas 5.000 entidades para confirmar que compensa. | Alto |

Las fases 1 y 2 no dependen de nada más y pueden empezar ya.

## 5. Compatibilidad

- No se elimina ni se renombra nada de la API pública.
- Los campos nuevos de `FeatureStyle` y `MapFeature` llevan valores por defecto equivalentes al comportamiento actual.
- Cada fase añade tests, comentarios por función en español y una entrada en `docs/BITACORA.md`.

## 6. Qué necesitamos de ustedes

1. **Los dos ficheros mencionados** (fundamentación y anexos con el código), o al menos el
   código de `SubEntity`, `LabelAnchor` y el arrastre del formulario. Lo revisaremos contra las
   convenciones del repo (comentarios, soporte de qmake, tests) y diremos qué se aprovecha tal cual.
2. **El caso real de category / entity**: si capa y tipo les bastan, nos ahorramos una fase.

Además, confirmar el orden de las fases. Nuestra recomendación es empezar por la 1.
