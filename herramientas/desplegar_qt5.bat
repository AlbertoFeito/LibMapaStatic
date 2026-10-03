@echo off
REM ===================================================================
REM  desplegar_qt5.bat  -  igual que desplegar.bat, pero para una
REM                        aplicacion compilada con Qt 5 (5.14 / 5.15).
REM                        Deja la app lista para copiarla a otro PC
REM                        SIN Qt instalado y SIN internet.
REM
REM  Para Qt 6 se usa desplegar.bat. Este script comprueba con que Qt se
REM  compilo la app y, si no es Qt 5, se para y lo dice.
REM
REM  Uso (desde una ventana de cmd, no desde Git Bash):
REM     desplegar_qt5.bat <app.exe> <carpeta destino> [carpeta del paquete]
REM
REM  Ejemplo:
REM     desplegar_qt5.bat build\release\miapp.exe D:\Entrega D:\QtPro\Recursos
REM
REM  Que hace:
REM   1. Comprueba que app.exe es de Qt 5 y esta compilada en Release.
REM   2. Copia app.exe a la carpeta destino.
REM   3. Ejecuta windeployqt: copia junto al .exe las DLL de Qt, el runtime de
REM      MinGW y los plugins. Despues QUITA los plugins que el mapa no usa y que
REM      arrastran Qt Network o Qt SerialPort (bearer, generic, position): el
REM      windeployqt de Qt 5 no tiene la opcion --skip-plugin-types de Qt 6.
REM   4. COMPRUEBA los plugins sin los que el mapa sale en blanco SIN avisar:
REM        platforms\qwindows.dll   (sin el, la app ni abre)
REM        sqldrivers\qsqlite.dll   (sin el, no se abre ninguna base)
REM        imageformats\qjpeg.dll   (sin el, la satelital no se decodifica)
REM      Si windeployqt no los dejo, los copia a mano. Y avisa si algo de lo
REM      copiado sigue necesitando Qt5Network.dll.
REM   5. Si se pasa el paquete: lo comprueba con check_data y copia SOLO los
REM      ficheros que usa su mapa.json a <destino>\datos (reanudable). La app
REM      debe usar  cfg.dataDir = QCoreApplication::applicationDirPath() + "/datos"
REM
REM  Que Qt usa:  la variable QTDIR si existe; si no, C:\Qt\5.14.2\mingw73_64.
REM  MinGW:       la variable MINGW_BIN si existe; si no, C:\Qt\Tools\mingw730_64\bin.
REM               (Para Qt 5.15.2:  set QTDIR=C:\Qt\5.15.2\mingw81_64
REM                                 set MINGW_BIN=C:\Qt\Tools\mingw810_64\bin)
REM  check_data:  junto a app.exe, o en ..\..\bin (libmapa instalada), o en el
REM               PATH. Tiene que estar compilado con Qt 5 tambien.
REM ===================================================================

setlocal enabledelayedexpansion

if "%~2"=="" (
    echo.
    echo  Uso: desplegar_qt5.bat ^<app.exe^> ^<carpeta destino^> [carpeta del paquete]
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

if not defined QTDIR set "QTDIR=C:\Qt\5.14.2\mingw73_64"
if not defined MINGW_BIN set "MINGW_BIN=C:\Qt\Tools\mingw730_64\bin"
if not exist "%QTDIR%\bin\windeployqt.exe" (
    echo  ERROR: no encuentro windeployqt en %QTDIR%\bin
    echo         Indica tu Qt 5 con:  set QTDIR=C:\Qt\^<version^>\mingw^<..^>_64
    exit /b 1
)
if not exist "%MINGW_BIN%\g++.exe" (
    echo  ERROR: no encuentro el compilador MinGW en %MINGW_BIN%
    echo         Indica el tuyo con:  set MINGW_BIN=C:\Qt\Tools\mingw^<..^>_64\bin
    exit /b 1
)
REM  g++ en el PATH: asi windeployqt sabe que runtime de MinGW copiar.
set "PATH=%QTDIR%\bin;%MINGW_BIN%;%PATH%"

REM --- 1. Que Qt usa la app -------------------------------------------
REM  El nombre de la DLL de Qt que importa el .exe lo dice: Qt5Core.dll es
REM  Qt 5 Release, Qt5Cored.dll es Qt 5 Debug, Qt6Core.dll es Qt 6.
echo.
echo  [1/5] Comprobando con que Qt se compilo %~nx1
findstr /m /i /c:"Qt6Core.dll" "%APP%" >nul && (
    echo  ERROR: %~nx1 esta compilada con Qt 6. Para Qt 6 usa desplegar.bat
    exit /b 1
)
findstr /m /i /c:"Qt5Cored.dll" "%APP%" >nul && (
    echo  ERROR: %~nx1 esta compilada en DEBUG. Compilala en Release:
    echo         en Qt Creator, el selector de abajo a la izquierda ^(el monitor^) -^> Release.
    exit /b 1
)
findstr /m /i /c:"Qt5Core.dll" "%APP%" >nul || (
    echo  ERROR: %~nx1 no parece una aplicacion de Qt 5 ^(no usa Qt5Core.dll^).
    exit /b 1
)
echo         Qt 5, Release. Se despliega con %QTDIR%

REM --- 2. Ejecutable --------------------------------------------------
echo  [2/5] Copiando %~nx1 a %DEST%
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%APP%" "%DEST%\" >nul || (echo  ERROR al copiar & exit /b 1)

REM --- 3. windeployqt -------------------------------------------------
echo  [3/5] windeployqt (DLL de Qt, runtime de MinGW, plugins)
windeployqt --release --no-translations --compiler-runtime "%DEST%\%~nx1" >"%DEST%\windeployqt.log" 2>&1
if errorlevel 1 (
    echo  ERROR: windeployqt fallo. Detalle en %DEST%\windeployqt.log
    exit /b 1
)
REM  Plugins que el mapa no usa y que arrastran red o puerto serie:
REM    bearer   -> Qt5Network (gestion de conexiones de red)
REM    generic  -> Qt5Network (entrada tactil TUIO por red)
REM    position -> Qt5SerialPort (GPS por puerto serie); el mapa no lee GPS
for %%D in (bearer generic position) do (
    if exist "%DEST%\%%D" rmdir /s /q "%DEST%\%%D"
)
for %%L in (Qt5Network.dll Qt5SerialPort.dll) do (
    if exist "%DEST%\%%L" (
        findstr /m /i /c:"%%L" "%DEST%\%~nx1" >nul || del /q "%DEST%\%%L"
    )
)

REM --- 4. Plugins imprescindibles -------------------------------------
echo  [4/5] Comprobando plugins imprescindibles
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
REM  Lo que quede no deberia necesitar la red. Si algo la pide, se avisa.
set "RED="
for /f "delims=" %%F in ('findstr /s /m /i /c:"Qt5Network.dll" "%DEST%\*.dll" "%DEST%\*.exe" 2^>nul') do (
    set "RED=!RED! %%~nxF"
)
if defined RED (
    echo  AVISO: estos ficheros siguen pidiendo Qt5Network.dll:!RED!
    echo         La app funciona igual, pero la entrega no queda libre de red.
) else (
    echo         nada de red  ok
)

REM --- 5. Paquete de datos --------------------------------------------
if "%DATOS%"=="" (
    echo  [5/5] Sin paquete de datos: copia la carpeta del paquete a %DEST%\datos
    echo         ^(o vuelve a lanzar con la carpeta del paquete como tercer argumento^).
    goto :fin
)

set "CHECK="
if exist "%~dp1check_data.exe" set "CHECK=%~dp1check_data.exe"
if not defined CHECK if exist "%~dp0..\..\bin\check_data.exe" set "CHECK=%~dp0..\..\bin\check_data.exe"
if not defined CHECK for %%C in (check_data.exe) do if not "%%~$PATH:C"=="" set "CHECK=%%~$PATH:C"
if not defined CHECK (
    echo  ERROR: no encuentro check_data.exe ^(compila libmapa con qmake\libmapa\libmapa.pro^).
    exit /b 1
)
REM  Un check_data de Qt 6 no arrancaria con las DLL de Qt 5.
findstr /m /i /c:"Qt5Core.dll" "%CHECK%" >nul || (
    echo  ERROR: %CHECK% no esta compilado con Qt 5.
    echo         Usa el de tu instalacion de Qt 5 ^(C:\libmapa\qt5\bin^).
    exit /b 1
)

echo  [5/5] Comprobando y copiando el paquete con check_data
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
