@echo off
REM ===================================================================
REM  desplegar.bat  -  deja una aplicacion que usa libmapa lista para
REM                    copiarla a otro PC SIN Qt instalado y SIN internet.
REM
REM  Uso:
REM     desplegar.bat <app.exe> <carpeta destino> [carpeta del paquete]
REM
REM  Ejemplo:
REM     desplegar.bat build\...\demo.exe D:\Entrega\Mapa D:\QtPro\Recursos
REM
REM  Que hace:
REM   1. Copia app.exe a la carpeta destino.
REM   2. Ejecuta windeployqt: copia junto al .exe las DLL de Qt, el runtime de
REM      MinGW y los plugins.
REM   3. COMPRUEBA los plugins sin los que el mapa sale en blanco SIN avisar:
REM        platforms\qwindows.dll   (sin el, la app ni abre)
REM        sqldrivers\qsqlite.dll   (sin el, no se abre ninguna base)
REM        imageformats\qjpeg.dll   (sin el, la satelital no se decodifica)
REM      Si windeployqt no los dejo, los copia a mano.
REM   4. Si se pasa el paquete: lo comprueba con check_data y copia SOLO los
REM      ficheros que usa su mapa.json a <destino>\datos (reanudable). La app
REM      debe usar  cfg.dataDir = QCoreApplication::applicationDirPath() + "/datos"
REM
REM  Que Qt usa:  la variable QTDIR si existe; si no, C:\Qt\6.11.2\mingw_64.
REM  MinGW:       la variable MINGW_BIN si existe; si no, C:\Qt\Tools\mingw1310_64\bin.
REM  check_data:  junto a app.exe, o en ..\..\bin (libmapa instalada), o en el PATH.
REM ===================================================================

setlocal enabledelayedexpansion

if "%~2"=="" (
    echo.
    echo  Uso: desplegar.bat ^<app.exe^> ^<carpeta destino^> [carpeta del paquete]
    echo.
    exit /b 2
)

set "APP=%~f1"
set "DEST=%~f2"
set "DATOS=%~3"
if not "%DATOS%"=="" set "DATOS=%~f3"

if not exist "%APP%" (
    echo  ERROR: no existe %APP%
    exit /b 1
)

if not defined QTDIR set "QTDIR=C:\Qt\6.11.2\mingw_64"
if not defined MINGW_BIN set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
if not exist "%QTDIR%\bin\windeployqt.exe" (
    echo  ERROR: no encuentro windeployqt en %QTDIR%\bin
    echo         Indica tu Qt con:  set QTDIR=C:\Qt\^<version^>\mingw_64
    exit /b 1
)
REM  g++ en el PATH: asi windeployqt sabe que runtime de MinGW copiar.
set "PATH=%QTDIR%\bin;%MINGW_BIN%;%PATH%"

REM --- 1. Ejecutable --------------------------------------------------
echo.
echo  [1/4] Copiando %~nx1 a %DEST%
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%APP%" "%DEST%\" >nul || (echo  ERROR al copiar & exit /b 1)

REM --- 2. windeployqt -------------------------------------------------
echo  [2/4] windeployqt (DLL de Qt, runtime de MinGW, plugins)
REM  Se saltan los plugins que el mapa no usa y que ARRASTRAN Qt Network
REM  (tls, networkinformation, generic) o Qt SerialPort (position/nmea): el
REM  producto trabaja sin conexion y no lee GPS por el puerto serie.
windeployqt --no-translations --compiler-runtime --skip-plugin-types generic,networkinformation,position,tls "%DEST%\%~nx1" >"%DEST%\windeployqt.log" 2>&1
if errorlevel 1 (
    echo  ERROR: windeployqt fallo. Detalle en %DEST%\windeployqt.log
    exit /b 1
)

REM --- 3. Plugins imprescindibles -------------------------------------
echo  [3/4] Comprobando plugins imprescindibles
set "FALTA="
for %%P in (platforms\qwindows.dll sqldrivers\qsqlite.dll imageformats\qjpeg.dll) do (
    if not exist "%DEST%\%%P" (
        if exist "%QTDIR%\plugins\%%P" (
            for %%D in ("%DEST%\%%P") do if not exist "%%~dpD" mkdir "%%~dpD"
            copy /y "%QTDIR%\plugins\%%P" "%DEST%\%%P" >nul
            echo         %%P  copiado a mano
        ) else (
            set "FALTA=!FALTA! %%P"
        )
    ) else (
        echo         %%P  ok
    )
)
if defined FALTA (
    echo  ERROR: faltan plugins y no estan en %QTDIR%\plugins:!FALTA!
    exit /b 1
)

REM --- 4. Paquete de datos --------------------------------------------
if "%DATOS%"=="" (
    echo  [4/4] Sin paquete de datos: copia la carpeta del paquete a %DEST%\datos
    echo         ^(o vuelve a lanzar con la carpeta del paquete como tercer argumento^).
    goto :fin
)

set "CHECK="
if exist "%~dp1check_data.exe" set "CHECK=%~dp1check_data.exe"
if not defined CHECK if exist "%~dp0..\..\bin\check_data.exe" set "CHECK=%~dp0..\..\bin\check_data.exe"
if not defined CHECK for %%C in (check_data.exe) do if not "%%~$PATH:C"=="" set "CHECK=%%~$PATH:C"
if not defined CHECK (
    echo  ERROR: no encuentro check_data.exe ^(compilalo, o ponlo en el PATH^).
    exit /b 1
)

echo  [4/4] Comprobando y copiando el paquete con check_data
"%CHECK%" "%DATOS%" --quick --export "%DEST%\datos"
if errorlevel 1 (
    echo.
    echo  ERROR: el paquete tiene errores o no se pudo copiar ^(ver arriba^).
    exit /b 1
)

:fin
echo.
echo  Listo: %DEST%
echo  Copia esa carpeta entera al otro PC. No necesita Qt ni internet.
echo.
endlocal
exit /b 0
