# Respuesta a «Respuesta a la propuesta de convergencia con LibMapa»

Gracias. Aceptamos los tres matices y confirmamos el orden 1 → 2 → 3 → 4. Contestamos a sus cinco
preguntas y añadimos observaciones sobre los fragmentos de código que enviaron.

## 1. Respuestas a sus preguntas

1. **¿Aceptan los tres matices?** Sí: medir antes de la fase 4, ampliar `parts` y la equivalencia de category/entity.
2. **¿SubEntity o PartInfo?** **PartInfo paralelo a `parts`**, con estas condiciones:
   - `partsInfo` puede estar vacío (comportamiento actual). Si tiene elementos, su tamaño debe ser igual al de `parts`;
     `isValid()` lo comprueba.
   - Toda operación que inserte, quite o reordene partes (`moveVertex`, `insertVertex`, `removeVertex`, importación,
     persistencia) mantiene los dos vectores sincronizados. Esto se prueba con tests.
   - `PartInfo` lleva `name`, `colorOverride` (`QColor` inválido = usar el del grupo) y `labelAnchor`.
3. **¿Category = capa, entity = type?** Sí. La fase 3 queda en `featureTypeCounts`, `clearFeaturesOfType` y el alta en lote
   con una sola señal. Comprobaremos si ya existe algo de eso antes de escribirlo.
4. **¿Orden 1 → 2 → 3 → 4?** Confirmado. Cada fase en su rama y su PR.
5. **¿Convenciones del repo?** Están en `CLAUDE.md`; las principales:
   - Comentario `//` en español encima de **cada** función. El header documenta el API y el `.cpp` la implementación.
   - Qt 5.14 / 5.15 / 6.x. Si se añade un `.cpp`, hay que añadirlo también a `qmake/libmapa/core.pro` o `widget.pro`.
   - Todos los tests pasan con `QT_QPA_PLATFORM=offscreen ctest`. Cada feature añade tests.
   - Entrada nueva en `docs/BITACORA.md`. Si cambia el API, también `docs/arquitectura.html` y su PDF.
   - Una rama por tarea (`claude/<tema>`) y PR en borrador contra `main`. Los commits van en español, con el porqué.

## 2. Observaciones sobre los fragmentos de código

- **Arrastre del formulario.** En `main` el arrastre ya está en `MapView` (usa `TargetLayer::labelAt` y
  `TargetModel::setLabelOffset`). **No** añadiremos `beginLabelDrag`, `updateLabelDrag` ni `endLabelDrag` a `TargetLayer`,
  porque duplicarían ese mecanismo. Lo que sí falta:
  - la señal al soltar, pero en `MapWidget` y no en `TargetLayer` (la capa no es API pública), con el id y el offset final;
  - el comportamiento de soltar fuera del viewport (`accept = false`), que se añadiría en `MapView`.
- **`defaultLabelOffsetPx`.** Aceptado, pero el valor por defecto en la librería debe ser **(0, 0)**, como hoy; con
  (-31, -31) cambiaría el aspecto de todas las aplicaciones existentes. Ustedes lo ponen a (-31, -31) en su
  aplicación con `setDefaultLabelOffsetPx`.
- **Selección en azul.** `Qt::blue` fijo no vale: el color de selección debe salir del mismo sitio que el resto de la
  selección (o de un campo con valor por defecto). Hay que comprobar cómo se guarda la selección del rótulo en
  `TargetModel`.
- **Anclaje por azimut.** El fragmento tiene problemas: `azimut == 0.0` y `azimut > 270.0` dan la misma esquina
  (`InferiorDer`), y con coordenadas de pantalla (y hacia abajo) `atan2(x, y)` no da un azimut de brújula. Lo reescribimos
  con una prueba por cuadrantes para que la línea guía se una siempre a la esquina de la caja más cercana al objetivo.
- **`LabelAnchor`.** Aceptado tal cual. El valor por defecto debe ser el comportamiento actual (rótulo pegado al anclaje),
  de modo que ningún dato existente cambie.

## 3. Qué necesitamos

- Los **Anexos A, B y C** que mencionan (formulario arrastrable, geometría de arco, coordenadas polares). No venían
  adjuntos en el mensaje. El B nos sirve para la fase 2.
- El **caso de prueba concreto de la fase 1**: unas coordenadas y los offsets o anclajes con el resultado esperado en
  pantalla, para escribir tests que lo reproduzcan.

## 4. Siguiente paso

Empezamos la fase 1 (`claude/rotulos-ajustes`): `labelFontSize`, `LabelAnchor`, `defaultLabelOffsetPx`, señal al soltar
y anclaje por azimut. Les avisaremos con el PR en borrador.
