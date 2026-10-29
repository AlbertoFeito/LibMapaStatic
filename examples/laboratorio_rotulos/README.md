# laboratorio_rotulos

Banco de pruebas de los **rótulos** y de la **escala polar** de libmapa. Un panel a la derecha
cambia en vivo lo que añadió la fase 1 de la convergencia con el fork extendido, sobre seis
puntos de ejemplo, tres objetivos con formulario y una escala de anillos y radiales.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Lado donde cuelga el nombre de una entidad (10 posiciones; `Auto` = como siempre) | `FeatureStyle::labelAnchor` (`LabelAnchor`) |
| Tamaño de fuente propio del rótulo de una entidad | `FeatureStyle::labelFontSize` |
| Caja del rótulo de una entidad en píxeles | `MapWidget::featureLabelRect` |
| Formulario de un objetivo con colores por segmento | `MapTarget::labelContent` (`TargetLabelLine`, `TargetLabelSegment`) |
| Formulario colgado de la esquina más cercana, fuente monoespaciada, ancho y columnas | `MapWidget::setTargetLabelFormat` (`TargetLabelFormat`, `TargetLabelPlacement`) |
| Desplazamiento por defecto de los formularios y propio de cada objetivo | `setDefaultTargetLabelOffset`, `setTargetLabelOffset`, `clearTargetLabelOffset`, `MapTarget::labelOffsetSet` |
| Color del texto del objetivo seleccionado | `setTargetLabelSelectionColor` |
| Aviso al soltar un formulario arrastrado | señal `targetLabelMoved` |
| Escala de 300 o 600 km con anillos cada 10 km y radiales cada 10° en UN `RangeRings`, con énfasis | `RangeRings::count` (hasta 100), `majorRingEvery`, `majorSpokeEveryDeg`, `majorColor`, `spokeColor` |

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)):

```bat
laboratorio_rotulos D:\QtPro\Recursos
```

1. **Rótulos de entidades**: cambia *Lado del anclaje* y *Fuente*: los nombres de los seis puntos de ejemplo
   se recolocan y cambian de tamaño. Con un lado elegido, el desplazamiento libre se suma encima.
2. **Formulario de objetivos**: arrastra uno con el ratón (queda fijado como propio y la barra de estado
   dice dónde lo soltaste). Soltarlo **fuera del mapa** cancela el arrastre y vuelve a como estaba.
   Marca *Colgado de la esquina más cercana* para que la caja cuelgue del punto objetivo + offset por la
   esquina que corresponde al azimut del desplazamiento (arriba-izquierda → esquina inferior derecha, etc.),
   y con *Fuente monoespaciada*, *Ancho* y *Columnas 8 + 8* ve cómo el formulario mide siempre lo mismo.
3. **Offset por defecto**: cambia X e Y; los objetivos que no fijaron el suyo lo siguen. *Quitar offsets
   propios* devuelve todos al de por defecto.
4. **Escala polar**: 300 km (30 anillos) o 600 km (60 anillos) con 36 radiales, y el énfasis en gris oscuro
   de un anillo de cada cinco y un radial de cada tres.

`--comprobar` no abre ventana: monta lo anterior y comprueba posiciones, tamaños, el offset por defecto y
los 60 anillos (lo usa `herramientas/probar_instalacion.sh`). `--captura=imagen.png` guarda una imagen.

## Detalles que conviene saber

- **Convención de los offsets**: píxeles de pantalla, X hacia la derecha e Y **hacia abajo**. Una aplicación
  que use Y hacia arriba convierte en su capa.
- Los valores por defecto reproducen el rótulo de siempre: `LabelAnchor::Auto`, `labelFontSize = 0`,
  colocación `Classic`, offset por defecto `(0, 0)` y color de selección negro. Nada existente cambia.
- `RangeRings::count` cuenta **solo anillos** (tope 100); los radiales salen de `spokeEveryDeg` y no suman.
- El color de selección solo tiñe los segmentos que **no** traen color propio, para no perder la información
  que lleva el color.
