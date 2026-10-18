# Mis rutas: instalar libmapa y desplegar una app (PC Windows de Alberto)

Hoja de ruta con **las rutas reales de este PC**, lista para copiar y pegar.
La explicación de cada paso está en [`DESPLIEGUE.md`](DESPLIEGUE.md); esto es el
resumen para cuando no se recuerda cómo se hacía.

## Las rutas de este PC

| Qué | Dónde |
|---|---|
| Proyecto (fuentes de libmapa) | `D:\QtPro\LibMapaStatic` |
| Qt 6 (MinGW 64) | `C:\Qt\6.11.2\mingw_64` |
| Compilador MinGW | `C:\Qt\Tools\mingw1310_64\bin` |
| CMake / Ninja de Qt | `C:\Qt\Tools\CMake_64\bin` · `C:\Qt\Tools\Ninja` |
| libmapa **instalada** (Qt 6) | `C:\libmapa\qt6` |
| Carpeta de build de la librería (qmake) | `D:\QtPro\LibMapaStatic\build-qt6` |
| QCustomPlot (ya está, no va en git) | `D:\QtPro\LibMapaStatic\third_party\qcustomplot` |
| **Paquete de datos** (`mapa.json` y mapas) | `D:\QtPro\Recursos` |
| Carpeta de entrega al otro PC | `D:\Entrega` (la eliges tú) |

Los valores por defecto de `desplegar.bat` (`QTDIR` y `MINGW_BIN`) **ya coinciden
con este PC**: no hace falta definir nada.

> `C:\Qt\Qt5.7.1` existe pero **no se usa**: el proyecto no soporta Qt 5.7.
> Todo lo de abajo es del juego **Qt 6**; no se mezcla con Qt 5.

## 0. Abrir la consola correcta
Menú Inicio → **«Qt 6.11.2 (MinGW 13.1.0 64-bit)»** (ventana negra de `cmd`).
No uses Git Bash: se comen las barras `\`.

Comprobación rápida (debe imprimir una versión de `g++` y de `qmake`):
```bat
g++ --version
qmake --version
```

## 1. Ponerse en la rama
```bat
cd /d D:\QtPro\LibMapaStatic
git fetch origin
git checkout claude/apps-ejemplo
git pull
```
(Cuando esa rama se fusione, `git checkout main` y `git pull`.)

## 2. Instalar libmapa en `C:\libmapa\qt6`
Ya existe `build-qt6`, así que **no hace falta repetir `mkdir` ni `qmake`**:
```bat
cd /d D:\QtPro\LibMapaStatic\build-qt6
mingw32-make -j4
mingw32-make install
```
Si algo se queda raro (o cambiaste ficheros del `.pro`), empieza de cero:
```bat
cd /d D:\QtPro\LibMapaStatic
rmdir /s /q build-qt6
mkdir build-qt6
cd build-qt6
qmake ..\qmake\libmapa\libmapa.pro
mingw32-make -j4
mingw32-make install
```
Reinstala **cada vez que cambie el código de la librería** (por ejemplo al
traer una rama nueva). Comprueba el resultado:
```bat
dir /b C:\libmapa\qt6
dir /b C:\libmapa\qt6\lib
dir /b C:\libmapa\qt6\bin
```
Debe haber `libmapa.pri`, `lib\libmapa_core.a`, `lib\libmapa_widget.a`,
`bin\check_data.exe` y `share\libmapa\desplegar.bat`.

### Comprobar el paquete de datos
```bat
C:\libmapa\qt6\bin\check_data.exe D:\QtPro\Recursos --quick
```
Debe acabar con `0 error(es)`. Si hay avisos, léelos antes de desplegar.

## 3. Compilar los ejemplos contra lo instalado
En Qt Creator: **Archivo → Abrir proyecto…** →
`D:\QtPro\LibMapaStatic\examples\examples.pro`, kit **Desktop Qt 6.11.2 MinGW 64-bit**,
y elige **Release**. Compila (`Ctrl+B`).

Para ejecutar `app_minima` desde Qt Creator: **Proyectos → Ejecución →
Argumentos de la línea de órdenes** = `D:\QtPro\Recursos`.

Dónde queda cada `.exe` (búscalo si dudas):
```bat
dir /s /b D:\QtPro\LibMapaStatic\build\*app_minima.exe
```
Si tu build de Qt Creator está en otra carpeta, usa la ruta de
**Proyectos → Compilar → Directorio de compilación**.

## 4. Desplegar (carpeta para copiar a otro PC)
Con la ruta del `.exe` del paso 3 (sustituye `RUTA_AL_EXE`):
```bat
C:\libmapa\qt6\share\libmapa\desplegar.bat "RUTA_AL_EXE\app_minima.exe" D:\Entrega D:\QtPro\Recursos
```
El script copia el `.exe`, las DLL de Qt y los plugins (comprueba que estén
`qwindows`, `qsqlite` y `qjpeg`), y copia a `D:\Entrega\datos` **solo los
ficheros que usa tu `mapa.json`**. Espera a ver `Listo: D:\Entrega`.

Para otra app tuya, el mismo comando con su `.exe`. En su código:
```cpp
cfg.dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/datos");
```

## 5. Probar en el otro PC
Copia `D:\Entrega` entero (pendrive) y ejecuta el `.exe`. Para ver si el
despliegue está sano sin abrir ventana:
```bat
D:\Entrega\app_minima.exe D:\Entrega\datos --comprobar
echo %ERRORLEVEL%
```
`0` = todo bien.

## Problemas típicos
- **`No se encuentra el manifiesto …\datos\mapa.json`** → falta la carpeta
  `datos` junto al `.exe` o el argumento del paso 3. La librería funciona.
- **`qmake` o `g++` «no se reconoce»** → no estás en la consola del paso 0.
- **Mapa en blanco en el otro PC** → faltó un plugin; `desplegar.bat` avisa de
  cuál.
- **El ejemplo no enlaza** → está compilado con otro kit que la librería
  instalada: usa el mismo Qt 6.11.2 MinGW 64 en ambos.
