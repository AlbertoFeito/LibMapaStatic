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

**Decisión tomada:** este proyecto se distribuye bajo **GPLv3** (ver el fichero
`LICENSE` en la raíz), la misma licencia que QCustomPlot. Aun así, QCustomPlot
no se versiona aquí: se enlaza desde tu propia copia, para no arrastrar su
código en el repositorio y para que quede claro de dónde sale.

Si en el futuro GPLv3 no encajara con cómo se distribuye EstacionTerrena3, la
alternativa es comprar la **licencia comercial** de QCustomPlot en
qcustomplot.com y relicenciar este proyecto en consecuencia.

## Cómo añadirlo

Descarga QCustomPlot 2.x de https://www.qcustomplot.com y copia aquí:

```
third_party/qcustomplot/qcustomplot.h
third_party/qcustomplot/qcustomplot.cpp
```

O apunta a la copia que ya tenga tu proyecto:

```
cmake -DLIBMAPA_QCP_DIR=D:/ruta/a/tu/qcustomplot ...
```

También sirve la variable de entorno `QCUSTOMPLOT_DIR`.

## Sin QCustomPlot

El núcleo (`libmapa_core`) compila igual, con sus tests, y las herramientas
`probe_db`, `bench_tiles` y `vector_db` funcionan. Solo se quedan fuera el
widget (`libmapa_widget`), la aplicación `demo` y `render_map`.
