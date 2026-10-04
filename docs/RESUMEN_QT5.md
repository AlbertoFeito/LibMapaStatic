# Montar libmapa con Qt 5 — resumen rápido

Versión corta del **juego Qt 5**, pensada para arrancar de cero en un PC con
Qt 5.14 / 5.15 MinGW. La guía completa, paso a paso y con la tabla de problemas,
está en [`DESPLIEGUE.md`](DESPLIEGUE.md) (en esta misma carpeta).

> Donde ponga `qt5` / `5.14.2` / `mingw73_64`, ajústalo a tu versión de Qt.

---

## Antes de nada, necesitas en tu PC

- **Qt 5.14 (o 5.15) con MinGW 64-bit** y **Qt Creator** instalados. Comprueba
  que existe `C:\Qt\5.14.2\mingw73_64\bin\Qt5Positioning.dll`; si no, añade el
  módulo *Qt Positioning* con el *Qt Maintenance Tool*.
- **QCustomPlot 2.1.1** → **no viene en git** (su licencia). Descárgalo y copia
  `qcustomplot.h` y `qcustomplot.cpp` a `LibMapaStatic\third_party\qcustomplot\`
  (instrucciones en el `LEEME.md` de esa carpeta).
- **El paquete de datos** (carpeta `Recursos` con `mapa.json` y los `.sqlitedb`)
  → **tampoco viene en git** (son los mapas, pesan mucho). Te lo pasa quien ya lo
  tenga (pendrive, red…).

Todo lo demás (la librería, el proyecto qmake, el ejemplo `app_minima`, los
scripts de despliegue y las guías) sí te llega con el `git clone`.

---

## 1. Clonar la rama

```
git clone -b claude/sharp-goodall-dt7hh5 <url del repo> LibMapaStatic
```

## 2. Compilar e instalar la librería (una sola vez)

Abre la consola **«Qt 5.14.2 (MinGW 7.3.0 64-bit)»** del menú Inicio (una ventana
`cmd`; **no** uses Git Bash) y escribe, línea a línea:

```bat
cd /d <ruta>\LibMapaStatic
mkdir build-qt5
cd build-qt5
qmake ..\qmake\libmapa\libmapa.pro
mingw32-make -j4
mingw32-make install
```

Queda instalada en `C:\libmapa\qt5`. Termina bien si **no** aparece la palabra
`Error`. Para reinstalar más adelante basta, dentro de `build-qt5`,
`mingw32-make -j4 && mingw32-make install` (el `qmake` solo se repite si borras
la carpeta `build-qt5`).

## 3. Probarla con el ejemplo (sin escribir código)

1. En Qt Creator abre `LibMapaStatic\examples\app_minima\app_minima.pro` y
   configúralo con tu **kit Qt 5**.
2. *Proyectos → Ejecución → Argumentos de la línea de órdenes*: pon la carpeta
   del paquete de datos (p. ej. `D:\Recursos`).
3. Pulsa **Ejecutar** (triángulo verde) → debe abrirse el mapa.

> Si sale *«No se encuentra el manifiesto …\datos\mapa.json»* **no es un fallo**:
> la librería ya funciona, solo le falta saber dónde están los datos. Es el
> argumento del punto 2.

## 4. Desplegar para otro PC (opcional)

Compila el ejemplo (o tu app) en **Release** y, desde `cmd`:

```bat
C:\libmapa\qt5\share\libmapa\desplegar_qt5.bat <app.exe en Release> D:\Entrega D:\Recursos
```

Espera a ver **`Listo: D:\Entrega`**. Esa carpeta se copia entera a un PC **sin
Qt ni internet** y el programa funciona.

> Si tu Qt no está en `C:\Qt\5.14.2\mingw73_64`, antes del script indica dónde
> está, por ejemplo para 5.15.2:
> ```bat
> set QTDIR=C:\Qt\5.15.2\mingw81_64
> set MINGW_BIN=C:\Qt\Tools\mingw810_64\bin
> ```

## 5. Usar la librería en tu propia app

Una sola línea al final del `.pro` de tu aplicación:

```qmake
include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)
```

y en el código, crear el mapa apuntando al paquete de datos:

```cpp
#include <libmapa/MapWidget.h>
#include <QCoreApplication>

libmapa::MapConfig cfg;
cfg.dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/datos");
auto *mapa = new libmapa::MapWidget(cfg, this);
setCentralWidget(mapa);
```

---

¿Algo falla? Mira la tabla **«Problemas frecuentes»** al final de
[`DESPLIEGUE.md`](DESPLIEGUE.md): recoge los errores típicos (Git Bash,
`windeployqt`, DEBUG vs Release, QCustomPlot, `mapa.json`…).
