#-------------------------------------------------------------------
# fill_tiles.pro  -  alternativa qmake, por si prefieres no usar CMake.
#
# Abre este fichero directamente con Qt Creator y pulsa Ejecutar, o
# desde consola:  qmake && make   (Windows/MinGW: qmake && mingw32-make)
#
# Rellena los huecos de una BD de teselas descargando de una fuente XYZ
# sin clave (Esri satelite por defecto). El proyecto principal usa CMake;
# este .pro existe para lanzar la herramienta con lo que ya tienes montado.
#-------------------------------------------------------------------

# positioning: TileMatrix/WebMercator usan QGeoCoordinate. network: la descarga.
QT += core sql network positioning
QT -= gui

CONFIG += console
CONFIG -= app_bundle

# Estandar de C++ segun la version de Qt:
#  - Qt 6 exige C++17.
#  - Qt 5.7 (MinGW 5.3 de EstacionTerrena) NO entiende la opcion c++17 de
#    qmake (llego en Qt 5.12) y caeria a C++11. c++14 existe desde Qt 5.4 y
#    basta para la libreria.
greaterThan(QT_MAJOR_VERSION, 5) {
    CONFIG += c++17
} else {
    CONFIG += c++14
}

TARGET = fill_tiles
TEMPLATE = app

# La raiz del proyecto, subiendo un nivel desde qmake/
ROOT = $$PWD/..

INCLUDEPATH += $$ROOT/src $$ROOT/include

# Solo el nucleo minimo: geodesia, el descriptor de dataset y el log. La
# herramienta lee datasets.json por su cuenta (no arrastra el motor de teselas).
SOURCES += \
    $$ROOT/src/core/Logging.cpp \
    $$ROOT/src/geo/WebMercator.cpp \
    $$ROOT/src/geo/TileMatrix.cpp \
    $$ROOT/src/tiles/TileDataset.cpp \
    $$ROOT/tools/fill_tiles/main.cpp

HEADERS += \
    $$ROOT/src/core/Logging.h \
    $$ROOT/src/geo/WebMercator.h \
    $$ROOT/src/geo/TileMatrix.h \
    $$ROOT/src/tiles/TileDataset.h

DESTDIR = $$ROOT/bin
