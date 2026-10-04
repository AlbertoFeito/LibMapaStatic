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

CONFIG += c++17

TARGET = fill_tiles
TEMPLATE = app

# La raiz del proyecto, subiendo un nivel desde qmake/
ROOT = $$PWD/..

INCLUDEPATH += $$ROOT/src $$ROOT/include $$ROOT/tools/common

# Solo el nucleo minimo: geodesia, el descriptor de dataset, el log y el motor
# de descarga comun (TileFiller). No arrastra el motor de teselas del mapa.
SOURCES += \
    $$ROOT/src/core/Logging.cpp \
    $$ROOT/src/geo/WebMercator.cpp \
    $$ROOT/src/geo/TileMatrix.cpp \
    $$ROOT/src/tiles/TileDataset.cpp \
    $$ROOT/tools/common/TileFiller.cpp \
    $$ROOT/tools/fill_tiles/main.cpp

HEADERS += \
    $$ROOT/src/core/Logging.h \
    $$ROOT/src/geo/WebMercator.h \
    $$ROOT/src/geo/TileMatrix.h \
    $$ROOT/src/tiles/TileDataset.h \
    $$ROOT/tools/common/TileFiller.h

DESTDIR = $$ROOT/bin
