# laboratorio_anillos

Banco de pruebas de los **anillos de distancia** y los **radiales** de libmapa. Sirve para comprobar,
con el ratón, que la escala de los anillos es la correcta. Arranca con el **mapa de costas**
(`costas`, `Cuba_Vector.sqlitedb`) y **sin nada híbrido** encima. Además reúne lo de la **fase 1 de la
convergencia con el fork extendido** (`docs/Propuesta_final_convergencia.md`): la **escala polar de 300 y
600 km** con énfasis, los **rótulos de entidades** con su lado y su fuente, y el **formulario de objetivos**
arrastrable con colores por segmento.

## Qué enseña

| Función | API de libmapa |
|---|---|
| Capa base de arranque elegida por la app (`costas` si el paquete la trae) y selector de capas | `availableBaseLayers`, `setBaseLayerId` |
| Solo el mapa: quita el overlay híbrido que traiga el `mapa.json` | `clearVectorOverlay`, `hasVectorOverlay` |
| Anillos con radiales, rotulados con su distancia y su marcación | `RangeRings` (`spacingMeters`, `count`, `spokeEveryDeg`, `labels`, `name`), `addRangeRings` |
| Mostrar y ocultar los anillos con los mismos ajustes | `removeRangeMark` + `addRangeRings` |
| Cambiar el centro con un clic en el mapa (Esc o clic derecho cancela) | `MapTool::PickPoint`, señales `pointPicked` y `pickCancelled` |
| Unidades de los rótulos y de la barra de escala (km o NM) | `setScaleBarUnits`, `setScaleBarVisible` |
| Cuadrícula de coordenadas | `setGraticuleVisible` |
| Coordenadas del ratón en decimal, GMS y UTM | señal `mouseMoved`, `formatCoordinate` |
| Distancia y marcación del ratón desde el centro, y el anillo más cercano | `QGeoCoordinate::distanceTo` / `azimuthTo` |
| Escala polar de 300 o 600 km: hasta 100 anillos y los radiales en **un solo** `RangeRings` (los radiales no cuentan en `count`) | `RangeRings::count`, `spokeEveryDeg` |
| Énfasis: uno de cada N anillos y de cada N grados en otro color, y radiales de otro color | `RangeRings::majorRingEvery`, `majorSpokeEveryDeg`, `majorColor`, `spokeColor` |
| Lado donde cuelga el nombre de una entidad (10 posiciones; `Auto` = como siempre) y su fuente | `FeatureStyle::labelAnchor` (`LabelAnchor`), `labelFontSize`, `MapWidget::featureLabelRect` |
| Formulario de un objetivo de tres líneas con colores por segmento | `MapTarget::labelContent` (`TargetLabelLine`, `TargetLabelSegment`) |
| Formulario colgado de la esquina más cercana, fuente monoespaciada, ancho y columnas 8 + 8 | `MapWidget::setTargetLabelFormat` (`TargetLabelFormat`, `TargetLabelPlacement`) |
| Offset por defecto de los formularios y propio de cada objetivo; color de selección | `setDefaultTargetLabelOffset`, `setTargetLabelOffset`, `clearTargetLabelOffset`, `setTargetLabelSelectionColor` |
| Aviso al soltar un formulario arrastrado (soltar fuera del mapa cancela) | señal `targetLabelMoved`, `targetLabelRect` |
| Círculo de 50 km y tres sectores (uno cruza el norte, de 330° a 30°) alrededor del centro, geodésicos | `GeometryKind::Circle` y `Arc`, `MapFeature::radiusMeters`, `arcStartDeg`, `arcEndDeg` |
| Alta de los cuatro en un lote (una señal de repintado, un paso de deshacer) y borrado por tipo | `addFeatures`, `clearFeaturesOfType` |
| Recuento por tipo y ocultar/mostrar un tipo sin borrarlo | `featureTypeCounts`, `setFeatureTypeVisible`, `isFeatureTypeVisible` |
| Una entidad de tres puntos con nombre y color cada uno (la tercera sin color propio) | `MapFeature::parts` + `partsInfo` (`PartInfo`) |
| Qué parte hay bajo el clic y su rótulo propio | `MapHit::subIndex`, señal `elementClicked`, `featurePartLabelRect` |

Los sectores siguen al centro: al elegir otro centro se vuelven a poner alrededor del nuevo. El grupo
«Sectores, tipos y partes» del panel los activa, cuenta las entidades por tipo, oculta cada tipo y dice, al
tocar una entidad, qué parte es («Sitios · parte 2 de 3 (Sitio Bravo)»).

**Puntos.** No hay puntos fijos: los agregas tú. El grupo «Agregar puntos» busca en el índice de lugares
del paquete (`searchPlaces`, que sale del propio mapa) y agrega el elegido como punto suelto o como una **parte nueva
de la entidad «Sitios»** (una sola entidad con nombre y color por parte); también se puede agregar uno con un clic en
el mapa, y «Quitar todos» los borra por tipo.

Todo en un solo fichero, [`main.cpp`](main.cpp), pensado para leerse de arriba abajo.

## Probarlo

Con libmapa instalada (ver [`docs/DESPLIEGUE.md`](../../docs/DESPLIEGUE.md)), sobre el paquete de
datos (la carpeta con el `mapa.json`):

```bat
laboratorio_anillos D:\QtPro\Recursos
```

El panel de la derecha trae la misma lista de lo que hay que ver y probar:

1. **Escala**: pasa el ratón por un anillo. «Desde el centro» tiene que dar su rótulo (10 km,
   20 km...). Compáralo también con la barra de escala.
2. **Radiales**: pasa el ratón por un radial. La marcación tiene que ser la de su rótulo
   (030°, 060°...).
3. **Centro**: pulsa «Elegir centro» y haz clic en otro sitio. Los anillos se mueven allí.
   «Centro en La Habana» los devuelve y «Llevar el mapa al centro» centra la vista en ellos.
4. **Mostrar/ocultar**: la casilla quita y vuelve a poner los anillos con los mismos ajustes.
5. **Unidades**: en millas náuticas cambian a la vez los rótulos, la barra y la lectura del ratón.
6. **Forma**: los anillos son círculos reales sobre el terreno. En el mapa (Mercator) se estiran
   un poco hacia el norte, y más cuanto más grandes.
7. **Cuadrícula**: las coordenadas del ratón tienen que coincidir con sus líneas.
8. **Escala polar**: «Escala de 600 km» pone 60 anillos de 10 km y radiales cada 10° en un solo objeto, con
   el rojo oscuro de un anillo de cada cinco y un radial de cada tres («Escala de 300 km»: 30 anillos). Los
   controles de énfasis y el de «radiales de otro color» se pueden tocar en vivo.
9. **Rótulos de entidades**: agrega puntos y cambia el lado del anclaje y la fuente; los nombres se
   recolocan y cambian de tamaño.
10. **Formulario de objetivos**: arrastra uno con el ratón (la barra de estado dice dónde lo soltaste) y
    suéltalo **fuera del mapa**: se cancela. Marca «Colgado de la esquina más cercana», la fuente
    monoespaciada, el ancho y las columnas 8 + 8; cambia el offset por defecto y el color de selección.

Si el paquete no tiene una capa `costas`, arranca con la del `mapa.json` («start») y la barra de
estado dice cuál es.

`--comprobar` no abre ventana. Monta lo anterior y comprueba:
- la capa inicial y que no hay nada híbrido;
- los diez anillos de 10 km con radiales cada 30°;
- el cambio de centro, y el ocultar y mostrar;
- las millas náuticas;
- la lectura de distancia y marcación del ratón;
- la escala de 600 km (60 anillos de 10 km, radiales cada 10°, énfasis) y la de 300 km;
- la caja del rótulo de una entidad con otro lado y otra fuente;
- el formulario de un objetivo: tres líneas, offset por defecto y propio, colgado y ancho fijo de 16 columnas.

Lo usa `herramientas/probar_instalacion.sh`.
