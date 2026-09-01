@echo off
REM ===================================================================
REM  ejecutar_bench.bat  -  busca bench_tiles.exe y lo lanza
REM
REM  Uso:
REM     ejecutar_bench.bat
REM     ejecutar_bench.bat "D:\ruta\a\datasets.json"
REM     ejecutar_bench.bat "D:\ruta\datasets.json" "D:\ruta\build-...\bench_tiles.exe"
REM
REM  Busca en el ABUELO de este script, porque Qt Creator crea la
REM  carpeta de compilacion como HERMANA del proyecto:
REM
REM     D:\2026\fromgit\libmapaClaude\
REM        +-- libmapa\herramientas\ejecutar_bench.bat   <- este fichero
REM        +-- build-libmapa-Desktop_Qt_5_14_2_MinGW_64_bit-Release\
REM
REM  Si hay varias compilaciones, prefiere la de Release: en Debug los
REM  tiempos de decodificacion no significan nada.
REM
REM  Genera bench15.txt y bench16.txt junto a este script.
REM ===================================================================

setlocal enabledelayedexpansion

set "PROYECTO=%~dp0.."
set "BUSQUEDA=%~dp0..\.."
set "SALIDA=%~dp0"

REM --- 1. Ejecutable -------------------------------------------------
set "EXE=%~2"

if not defined EXE (
    REM Primera pasada: solo carpetas de Release.
    for /r "%BUSQUEDA%" %%F in (bench_tiles.exe) do (
        if not defined EXE (
            echo %%~dpF | findstr /i "release" >nul && set "EXE=%%F"
        )
    )
)

if not defined EXE (
    REM Segunda pasada: cualquiera.
    for /r "%BUSQUEDA%" %%F in (bench_tiles.exe) do (
        if not defined EXE set "EXE=%%F"
    )
    if defined EXE (
        echo.
        echo  AVISO: solo se encontro una compilacion de Debug. Los tiempos
        echo         que salgan no son representativos; recompila en Release.
    )
)

if not defined EXE (
    echo.
    echo  ERROR: no se encontro bench_tiles.exe bajo
    echo         %BUSQUEDA%
    echo.
    echo  bench_tiles se anadio en la fase 3. Si tu carpeta de compilacion
    echo  se creo con un paquete anterior, CMake no sabe que existe.
    echo.
    echo  En Qt Creator:
    echo     Compilar -^> Ejecutar CMake
    echo     Compilar -^> Recompilar todo
    echo.
    echo  O pasa la ruta a mano:
    echo     ejecutar_bench.bat "ruta\datasets.json" "ruta\bench_tiles.exe"
    echo.
    exit /b 1
)

REM --- 2. datasets.json ----------------------------------------------
set "JSON=%~1"

if not defined JSON (
    if exist "%PROYECTO%\datasets.json" set "JSON=%PROYECTO%\datasets.json"
)
if not defined JSON (
    for /r "%BUSQUEDA%" %%F in (datasets.json) do (
        if not defined JSON set "JSON=%%F"
    )
)
if not defined JSON (
    echo.
    echo  ERROR: no se encontro datasets.json.
    echo  Pasalo como argumento:
    echo     ejecutar_bench.bat "D:\ruta\datasets.json"
    echo.
    exit /b 1
)

echo.
echo  Ejecutable: !EXE!
echo  Datasets  : !JSON!
echo.

REM --- 3. Ejecutar ---------------------------------------------------
REM  2^>^&1 junta stderr con stdout: los mensajes de libmapa.* salen por
REM  stderr y el informe por stdout; se quieren los dos en el fichero.

echo  Midiendo zoom 15 ... (la satelital son 3.4 GiB, puede tardar)
"!EXE!" --datasets "!JSON!" --zoom 15 --pans 40 > "%SALIDA%bench15.txt" 2>&1

echo  Midiendo zoom 16 ...
"!EXE!" --datasets "!JSON!" --zoom 16 --pans 40 > "%SALIDA%bench16.txt" 2>&1

echo.
echo  Listo:
echo     %SALIDA%bench15.txt
echo     %SALIDA%bench16.txt
echo.

endlocal
