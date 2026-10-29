# laboratorio_anillos

Banco de pruebas de los **anillos de distancia** y los **radiales** de libmapa. Sirve para comprobar,
con el ratón, que la escala de los anillos es la correcta. Arranca con el **mapa de costas**
(`costas`, `Cuba_Vector.sqlitedb`) y **sin nada híbrido** encima.

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

Si el paquete no tiene una capa `costas`, arranca con la del `mapa.json` («start») y la barra de
estado dice cuál es.

`--comprobar` no abre ventana. Monta lo anterior y comprueba:
- la capa inicial y que no hay nada híbrido;
- los diez anillos de 10 km con radiales cada 30°;
- el cambio de centro, y el ocultar y mostrar;
- las millas náuticas;
- la lectura de distancia y marcación del ratón.

Lo usa `herramientas/probar_instalacion.sh`.
