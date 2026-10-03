#-------------------------------------------------------------------
# fill_hgt.pro  -  alternativa qmake, por si prefieres no usar CMake.
#
# Descarga ficheros de elevacion SRTM `.hgt` (30 m) de AWS Skadi (sin
# clave) para una zona y los descomprime con zlib. Alimenta a dem_to_db.
#
# El proyecto principal usa CMake (CMakeLists.txt en la raiz).
#-------------------------------------------------------------------

QT += core network
QT -= gui widgets

CONFIG += console
CONFIG -= app_bundle

CONFIG += c++17

TARGET = fill_hgt
TEMPLATE = app

ROOT = $$PWD/..

SOURCES += \
    $$ROOT/tools/fill_hgt/main.cpp

# zlib para descomprimir los .hgt.gz (gzip). En MinGW suele venir con Qt;
# si no, ajusta el nombre/ruta de la libreria z.
LIBS += -lz

DESTDIR = $$ROOT/bin
