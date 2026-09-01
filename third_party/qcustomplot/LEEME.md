# QCustomPlot no se incluye en este repositorio

## Por qué

QCustomPlot se distribuye bajo **GNU GPL v3**:

```
Copyright (C) 2011-2022 Emanuel Eichhammer
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
```

Incluirlo aquí obligaría a que todo el repositorio fuese GPLv3, y a que
cualquier aplicación que enlace con esta librería publique su código fuente.
Si eso no encaja con cómo se va a distribuir EstacionTerrena3, hay dos
caminos:

- Comprar la **licencia comercial** de QCustomPlot en qcustomplot.com.
- Mantener el proyecto bajo GPLv3 conscientemente.

En cualquier caso, la decisión es del autor del proyecto, no algo que deba
quedar decidido de tapadillo por copiar un fichero.

## Cómo añadirlo

Descarga QCustomPlot 2.x de https://www.qcustomplot.com y copia aquí:

```
third_party/qcustomplot/qcustomplot.h
third_party/qcustomplot/qcustomplot.cpp
```

O apunta a la copia que ya tenga tu proyecto:

```
cmake -DLIBMAPA_QCP_DIR=D:/2025/veliz/EstacionTerrena3/qcustomplot ...
```

También sirve la variable de entorno `QCUSTOMPLOT_DIR`.

## Sin QCustomPlot

El núcleo (`libmapa_core`) compila igual, con sus tests, y las herramientas
`probe_db`, `bench_tiles` y `vector_db` funcionan. Solo se quedan fuera el
widget (`libmapa_widget`), la aplicación `demo` y `render_map`.
