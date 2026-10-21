# Teselas MVT de prueba

Los `NNN.mvt` son fixtures de [mapbox/mvt-fixtures](https://github.com/mapbox/mvt-fixtures)
(paquete npm `@mapbox/mvt-fixtures` 4.0.0, licencia **CC0 1.0**), copiados sin tocar.
Son la referencia independiente del decodificador de `src/vector/MvtTile.cpp`: cada uno
codifica un ejemplo de la especificación MVT 2.1 cuyo resultado se conoce de antemano.

| Fichero | Contenido (según `info.json` del fixture) |
|---|---|
| `017.mvt` | punto (§4.3.5.1): (25, 17) |
| `018.mvt` | línea (§4.3.5.2): (2,2) (2,10) (10,10) |
| `019.mvt` | polígono (§4.3.5.4): (3,6) (8,12) (20,34) |
| `020.mvt` | multipunto: (5,7) (3,2) |
| `021.mvt` | multilínea: (2,2) (2,10) (10,10) y (1,1) (3,5) |
| `022.mvt` | multipolígono con hueco (§4.3.5.5) |
| `025.mvt` | capa sin entidades (válida) |
| `038.mvt` | una propiedad de cada tipo de `Value` |

`019.mvt.gz` y `019.mvt.zlib` son el mismo `019.mvt` comprimido con el `gzip`/`zlib` de
Python (otra implementación distinta de miniz), para probar la descompresión.
