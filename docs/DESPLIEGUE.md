# Usar libmapa en tu aplicación y llevarla a otro PC

Guía paso a paso. No hace falta saber nada de antemano: sigue los pasos en
orden y comprueba en cada uno que ves lo que se indica.

Al terminar tendrás una **carpeta** con tu programa, todo lo que necesita de
Qt y los mapas. Esa carpeta se copia a cualquier PC con Windows (con un
pendrive, por ejemplo) y el programa funciona **sin instalar Qt y sin
internet**.

**Los pasos, de un vistazo** (cada uno tiene su apartado más abajo):

0. **Elige tu juego** (Qt 5 o Qt 6): todo lo demás depende de él.
1. **Comprueba que tienes todo** (Qt, las fuentes, QCustomPlot, los datos).
2. **Compila e instala libmapa** una vez, con tu Qt. Queda en `C:\libmapa\qtX`.
3. **Prepara tu aplicación**: una línea en el `.pro` y unas líneas de código.
4. **Compila en Release.**
5. **Despliega**: un script crea la carpeta de entrega con el `.exe`, Qt y los datos.
6. **Pruébalo en otro PC** copiando esa carpeta.

Los pasos 0, 1 y 2 se hacen **una sola vez** por PC. Del 3 al 6 son los de cada
vez que entregas una versión nueva (y si no has tocado nada, el 3 ya está).

> ¿Solo quieres la versión corta para **Qt 5**? Está en
> [`RESUMEN_QT5.md`](RESUMEN_QT5.md) (misma carpeta). Esta guía es la detallada.

---

## Antes de empezar: cinco palabras

| Palabra | Qué significa aquí |
|---|---|
| **Qt** | La herramienta con la que se programa la aplicación. Hay versiones distintas: **Qt 6** (por ejemplo 6.11.2) y **Qt 5** (por ejemplo 5.14.2). |
| **Kit** | En Qt Creator, la combinación «versión de Qt + compilador» con la que compilas. Se ve abajo a la izquierda (el icono del monitor). |
| **Release / Debug** | Dos formas de compilar. **Debug** es para buscar fallos mientras programas. **Release** es la versión que se entrega. **Solo se despliega Release.** |
| **libmapa** | La librería del mapa. Se compila **una vez** con tu Qt, y tu aplicación la usa. |
| **Paquete de datos** | La carpeta con los mapas (`D:\QtPro\Recursos`): las bases `.sqlitedb`, los `.geo` y el fichero `mapa.json`, que dice qué hay. |

---

## Paso 0. ¿Qué Qt tienes? Elige tu juego

Hay **dos juegos de despliegue**, uno por versión de Qt. Usa **solo** el tuyo:
lo compilado con Qt 5 no sirve con Qt 6, ni al revés.

Para saber tu versión, mira en Qt Creator el kit con el que compilas, o mira
qué carpetas hay en `C:\Qt` (por ejemplo `C:\Qt\6.11.2` o `C:\Qt\5.14.2`).

| | **Juego Qt 6** | **Juego Qt 5** |
|---|---|---|
| Versión probada | Qt 6.11.2 MinGW 64-bit | Qt 5.14.2 MinGW 7.3 64-bit (vale también 5.15) |
| Carpeta de Qt | `C:\Qt\6.11.2\mingw_64` | `C:\Qt\5.14.2\mingw73_64` |
| Compilador | `C:\Qt\Tools\mingw1310_64` | `C:\Qt\Tools\mingw730_64` |
| La librería se instala en | `C:\libmapa\qt6` | `C:\libmapa\qt5` |
| Script de despliegue | `desplegar.bat` | `desplegar_qt5.bat` |

Donde la guía ponga `qtX`, cambia la X por **5** o por **6** según tu juego.

> **Aviso:** el juego Qt 5 se ha comprobado hasta donde se puede sin Windows.
> La librería **se compila e instala** con qmake (paso 2), la aplicación de
> ejemplo **enlaza** contra ella y **arranca**, y `check_data` funciona —todo
> probado con **Qt 5.15** sobre Linux, y el código es el mismo para 5.14. Lo
> único que queda por probar en un Windows real es el script
> `desplegar_qt5.bat` del paso 5, que usa `windeployqt` (una herramienta de
> Windows que no existe en Linux). Si ahí algo falla, el apartado *Problemas
> frecuentes* dice qué mirar.

---

## Paso 1. Comprueba que tienes todo

> **Si acabas de clonar el repositorio (`git clone`), dos cosas NO vienen en él**
> —a propósito, no es un olvido— y sin ellas no llegarás al final:
>
> - **QCustomPlot 2.1.1** (`qcustomplot.h` y `qcustomplot.cpp`): fuera del repo
>   por su licencia (GPLv3). Sin él, el `widget` **no compila**. Lo descargas tú
>   (punto 3 de abajo).
> - **El paquete de datos** (`Recursos\` con `mapa.json` y las `.sqlitedb`): son
>   los mapas, pesan mucho y no son código, así que tampoco van en git. **Te los
>   tiene que pasar** quien ya los tenga (pendrive, red…) — o se generan con las
>   herramientas (punto 4 de abajo).
>
> Todo lo demás (la librería, el proyecto qmake, el ejemplo `app_minima`, los
> scripts `desplegar*.bat` y esta guía) sí te llega con el `git clone`.

1. **Qt con MinGW de 64 bits**, el de tu juego (tabla de arriba), y **Qt Creator**.
   Comprueba que existe este fichero. Si no está, añade el módulo
   *Qt Positioning* con el *Qt Maintenance Tool*:
   - Qt 6: `C:\Qt\6.11.2\mingw_64\bin\Qt6Positioning.dll`
   - Qt 5: `C:\Qt\5.14.2\mingw73_64\bin\Qt5Positioning.dll`
2. **Las fuentes de libmapa**: la carpeta `LibMapaStatic` (en esta guía,
   `D:\QtPro\LibMapaStatic`).
3. **QCustomPlot 2.1.1**: los ficheros `qcustomplot.h` y `qcustomplot.cpp`
   dentro de `LibMapaStatic\third_party\qcustomplot\`. No vienen en el
   repositorio por su licencia. Cómo conseguirlos: ver el `LEEME.md` de esa carpeta.
4. **El paquete de datos**: la carpeta con `mapa.json` (aquí, `D:\QtPro\Recursos`).
   Si aún no tienes el `mapa.json`, se genera a partir de tus mapas con
   `probe_db --package` (ver el README, apartado «Paquete de datos»).

---

## Paso 2. Compilar e instalar libmapa (una vez por PC y por Qt)

Solo hay que hacerlo la primera vez, o cuando cambie la librería.

1. Abre el menú Inicio de Windows y busca la consola de tu Qt:
   - Qt 6: **«Qt 6.11.2 (MinGW 13.1.0 64-bit)»**
   - Qt 5: **«Qt 5.14.2 (MinGW 7.3.0 64-bit)»**

   Es una ventana negra de `cmd` que ya sabe dónde están tu Qt y tu compilador.
   No uses Git Bash para esto.
2. Escribe estas líneas, una a una, pulsando Intro después de cada una.
   Usa `build-qt5` o `build-qt6` según tu juego:
   ```bat
   cd /d D:\QtPro\LibMapaStatic
   mkdir build-qt5
   cd build-qt5
   qmake ..\qmake\libmapa\libmapa.pro
   mingw32-make -j4
   mingw32-make install
   ```
   `mingw32-make -j4` tarda unos minutos y escribe mucho texto. Es normal.
   Termina bien si **no** aparece la palabra `Error` al final.

   > Para **reinstalar** la librería más adelante (si cambia su código) no hace
   > falta repetir el `mkdir` ni el `qmake`: basta entrar en `build-qt6` y lanzar
   > `mingw32-make -j4 && mingw32-make install`. El `qmake` solo se repite si
   > borras la carpeta `build-qt6`.
3. Comprueba que la instalación ha creado esto (con `qt5` o `qt6`):
   ```
   C:\libmapa\qt5\
       libmapa.pri              <- lo que incluirá tu aplicación
       libmapa_qt.pri           <- con qué Qt se compiló
       include\libmapa\*.h      <- cabeceras
       lib\libmapa_core.a  lib\libmapa_widget.a     (Release)
       lib\libmapa_cored.a lib\libmapa_widgetd.a    (Debug)
       bin\check_data.exe       <- lo usa el script de despliegue
       share\libmapa\desplegar_qt5.bat   (o desplegar.bat en Qt 6)
   ```

Si quieres instalarla en otra carpeta, escribe `qmake ..\qmake\libmapa\libmapa.pro PREFIX=D:/otra/carpeta`.

> **¿Tu aplicación usa CMake en vez de qmake?** Entonces compila e instala la
> librería con CMake (`cmake --install <build> --prefix C:/libmapa/qt6`) y en
> tu `CMakeLists.txt` usa `find_package(libmapa)`. Ver el README, apartado
> «Usarla desde otra aplicación». El resto de esta guía (pasos 4 a 6) es igual.

---

## Paso 3. Preparar tu aplicación

### 3.1 En el `.pro` de tu aplicación

Añade **una línea** al final:
```qmake
include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)
```
Esta línea vale tal cual para los dos juegos: Qt Creator pone solo el 5 o
el 6 según el kit con el que compiles. Después de cambiar el `.pro`, en
Qt Creator: menú **Compilar → Ejecutar qmake**.

### 3.2 En el código: crear el mapa

Por ejemplo, en el constructor de tu ventana (`mainwindow.cpp`):
```cpp
#include <libmapa/MapWidget.h>
#include <QCoreApplication>

// ...dentro de MainWindow::MainWindow, después de ui->setupUi(this):
libmapa::MapConfig cfg;
cfg.dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/datos");
auto *mapa = new libmapa::MapWidget(cfg, this);
setCentralWidget(mapa);
```
`dataDir` es la carpeta del paquete de datos. Así escrito, el programa busca
los mapas en una carpeta `datos` **junto a su `.exe`**, que es donde los
dejará el script de despliegue.

Mientras programas en tu PC, el `.exe` está en la carpeta de compilación y
ahí no hay `datos`. Para esas pruebas puedes poner la carpeta real
(`cfg.dataDir = "D:/QtPro/Recursos";`), pero **antes de desplegar** deja
la línea de `applicationDirPath()`. (Esto es para **tu** aplicación; el
ejemplo `app_minima` no se edita: recibe la carpeta como argumento, ver 3.3.)

### 3.3 Probar con el ejemplo `app_minima` (sin escribir código)

Si no quieres escribir nada todavía, abre el ejemplo que ya viene hecho:
`LibMapaStatic\examples\app_minima\app_minima.pro` (una aplicación completa con
mapa y avisos). Al abrirlo, Qt Creator te pide **configurar el proyecto**: elige
el **mismo kit** con el que instalaste la librería en el paso 2 y pulsa
*Configurar proyecto*.

Este ejemplo no tiene fija la carpeta de datos en el código: la toma como
**argumento**. Para verlo con tus mapas mientras desarrollas, díselo así:

1. Panel izquierdo **«Proyectos»** → bajo tu kit, **«Ejecución»**.
2. En **«Argumentos de la línea de órdenes»**, escribe la carpeta de tu paquete,
   por ejemplo `D:\QtPro\Recursos` (entre comillas si tiene espacios).
3. Vuelve al editor y pulsa **Ejecutar** (el triángulo verde).

Debe abrirse la ventana con el mapa. Así pruebas sin tener que desplegar.

> **Si ves `No se encuentra el manifiesto …\datos\mapa.json`**, no es un fallo de
> la instalación: significa que la librería **ya funciona** (está enlazada y
> corriendo) y lo único que le falta es saber **dónde están los datos**. Pasa que
> lo ejecutaste sin argumento, y entonces busca una carpeta `datos` junto al
> `.exe` (dentro del build), que no existe mientras desarrollas. Solución:
> ponle la carpeta del paquete como argumento (los 3 pasos de arriba). Al
> desplegar (paso 5) ese problema desaparece, porque el script deja `datos\`
> junto al `.exe`.

### 3.4 Comprobar que compila

Pulsa **Ejecutar** (el triángulo verde). Funciona en Debug y en Release.

---

## Paso 4. Compilar en Release

1. Abajo a la izquierda, en el icono del monitor, elige **Release**.
2. Menú **Compilar → Recompilar todo**.
3. El `.exe` queda en la carpeta de compilación **Release** de tu proyecto, por
   ejemplo:
   ```
   C:\Users\<tú>\Documents\prueba\build\Desktop_Qt_6_11_2_MinGW_64_bit_Release\release\prueba.exe
   ```
   Para saber la ruta exacta, mira en Qt Creator: **Proyectos → Compilar →
   Directorio de compilación**, y dentro, la carpeta `release`.

---

## Paso 5. Desplegar: crear la carpeta para el otro PC

1. Abre una ventana de **cmd**. Vale la consola del paso 2, o
   Inicio → escribe `cmd` → Intro.

   > **No uses Git Bash** (la ventana que pone `MINGW64`). Ahí las barras `\`
   > desaparecen y sale un error como
   > `bash: C:libmapasharelibmapadesplegar.bat: command not found`.
2. Escribe **una sola línea** con tres datos: el `.exe` del paso 4, la carpeta
   donde quieres la entrega y la carpeta del paquete de datos.

   **Qt 6:**
   ```bat
   C:\libmapa\qt6\share\libmapa\desplegar.bat C:\ruta\a\release\prueba.exe D:\Entrega D:\QtPro\Recursos
   ```
   **Qt 5:**
   ```bat
   C:\libmapa\qt5\share\libmapa\desplegar_qt5.bat C:\ruta\a\release\prueba.exe D:\Entrega D:\QtPro\Recursos
   ```
   Si una ruta tiene espacios, ponla entre comillas: `"C:\Mis cosas\prueba.exe"`.
3. Si tu Qt **no** está en la carpeta de la tabla del paso 0, antes de esa línea
   indica dónde está, por ejemplo para Qt 5.15.2:
   ```bat
   set QTDIR=C:\Qt\5.15.2\mingw81_64
   set MINGW_BIN=C:\Qt\Tools\mingw810_64\bin
   ```
4. Espera hasta ver **`Listo: D:\Entrega`**. Con un paquete de varios GB, la
   copia de los mapas tarda varios minutos.

   > Mientras copia, el Explorador de Windows muestra cada fichero **con su
   > tamaño final desde el primer momento**, aunque aún se esté escribiendo, y
   > los que faltan no aparecen todavía. No cierres la ventana hasta ver
   > `Listo`. Si se corta, vuelve a lanzar la misma línea: continúa donde se
   > quedó y no vuelve a copiar lo que ya está completo.

Lo que hace el script, por si te preguntan:
1. Comprueba que el `.exe` es de tu Qt y está en Release. Solo el script de Qt 5.
2. Copia el `.exe` a la carpeta de entrega.
3. Copia al lado las DLL de Qt y los plugins (`windeployqt`), **sin** los
   plugins de red, porque el programa trabaja sin internet.
4. Comprueba los tres plugins sin los que el mapa sale en blanco:
   `qwindows`, `qsqlite` y `qjpeg`.
5. Revisa el paquete de datos y copia a `D:\Entrega\datos` **solo** los
   ficheros que usa `mapa.json`. Además de los mapas, `Recursos` tiene otros
   ficheros (PDF, iconos, estilos) que no se copian.

Resultado:
```
D:\Entrega\
    prueba.exe
    Qt6Core.dll, Qt6Gui.dll, ... (o Qt5Core.dll, ...)
    platforms\  sqldrivers\  imageformats\  styles\ ...
    datos\
        mapa.json
        Cuba_OSM_CID3.sqlitedb, Cuba_Satelital_CID3.sqlitedb, ...
        Aguas.geo, FIR.geo, ...
    windeployqt.log        (registro; se puede borrar)
```

---

## Paso 6. Probarlo en otro PC

1. Copia la carpeta `D:\Entrega` **entera** al otro PC. No vale copiar solo
   el `.exe`.
2. Haz doble clic en `prueba.exe`.
3. Debe abrirse la ventana con el mapa. Si falta algo en los datos, el
   programa arranca igual y lo dice en un aviso.

No hace falta instalar nada en ese PC: ni Qt, ni internet.

---

## Problemas frecuentes

| Lo que ves | Qué pasa y qué hacer |
|---|---|
| `bash: C:libmapa...desplegar.bat: command not found` | Lo has escrito en Git Bash. Usa **cmd** (paso 5). |
| `no encuentro windeployqt en C:\Qt\...` | Tu Qt está en otra carpeta. Usa `set QTDIR=...` (paso 5.3). |
| `no encuentro el compilador MinGW en ...` (Qt 5) | Usa `set MINGW_BIN=...` (paso 5.3). |
| `esta compilada en DEBUG` (Qt 5) | Compila en **Release** (paso 4) y despliega ese `.exe`. |
| `esta compilada con Qt 6. Para Qt 6 usa desplegar.bat` | Te has equivocado de juego: usa el script de tu Qt. |
| `no encuentro check_data.exe` | Lanza el script desde la carpeta de instalación (`C:\libmapa\qtX\share\libmapa\`), tal como en el paso 5. |
| `... no esta compilado con Qt 5` (check_data) | Has usado el `check_data` de la instalación de Qt 6. Usa el script de `C:\libmapa\qt5\`. |
| qmake: `No encuentro libmapa en C:/libmapa/qt5` | Falta el paso 2 con ese Qt, o la instalaste en otra carpeta (`PREFIX`). |
| qmake: `libmapa ... esta compilada con Qt 5.14.2, pero este kit es Qt 6.11.2` | La librería y tu app tienen que ser del mismo Qt. Cambia el kit o instala libmapa con ese Qt (paso 2). |
| qmake: `Falta QCustomPlot 2.1.1` | Paso 1, punto 3. |
| En el otro PC: *«falta Qt5Core.dll»* o *«Qt6Core.dll»* | Se ha copiado solo el `.exe`. Copia la carpeta de entrega entera. |
| El mapa sale, pero la capa satelital en blanco | Falta `imageformats\qjpeg.dll` en la entrega. El programa avisa al abrir. Vuelve a desplegar. |
| *«No se encuentra el manifiesto ...\datos/mapa.json»* | La librería funciona, pero no encuentra los datos. **Probando el ejemplo** en Qt Creator: pásale la carpeta del paquete como argumento (paso 3.3). **En tu app**: revisa `cfg.dataDir` (paso 3.2). **En la entrega**: que exista `D:\Entrega\datos\mapa.json`. |
| `AVISO: estos ficheros siguen pidiendo Qt5Network.dll` | El programa funciona, pero la entrega lleva algo de red. Avisa al responsable de la librería con el nombre de los ficheros. |
| `el paquete tiene errores` | Lee las líneas `ERROR` de arriba: dicen qué fichero falta o está mal. Revísalo con `C:\libmapa\qtX\bin\check_data.exe D:\QtPro\Recursos`. |

---

## Chuleta (cuando ya lo has hecho una vez)

**Qt 6**, desde la consola «Qt 6.11.2 (MinGW 13.1.0 64-bit)»:
```bat
cd /d D:\QtPro\LibMapaStatic & mkdir build-qt6 & cd build-qt6
qmake ..\qmake\libmapa\libmapa.pro && mingw32-make -j4 && mingw32-make install
C:\libmapa\qt6\share\libmapa\desplegar.bat <app.exe en Release> D:\Entrega D:\QtPro\Recursos
```

**Qt 5**, desde la consola «Qt 5.14.2 (MinGW 7.3.0 64-bit)»:
```bat
cd /d D:\QtPro\LibMapaStatic & mkdir build-qt5 & cd build-qt5
qmake ..\qmake\libmapa\libmapa.pro && mingw32-make -j4 && mingw32-make install
C:\libmapa\qt5\share\libmapa\desplegar_qt5.bat <app.exe en Release> D:\Entrega D:\QtPro\Recursos
```

En el `.pro` de la aplicación, para los dos:
```qmake
include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)
```
