#-------------------------------------------------------------------
# probe_db.pro  -  alternativa qmake, por si prefieres no usar CMake.
#
# Abre este fichero directamente con Qt Creator y pulsa Ejecutar.
# Compila la herramienta de sondeo con todo el nucleo dentro.
#
# El proyecto principal usa CMake (CMakeLists.txt en la raiz), que es
# lo recomendado. Este .pro existe solo para que puedas lanzar la
# sonda cuanto antes con las herramientas que ya tienes montadas.
#-------------------------------------------------------------------

QT += core gui sql positioning
QT -= widgets

CONFIG += console c++17
CONFIG -= app_bundle

TARGET = probe_db
TEMPLATE = app

# La raiz del proyecto, subiendo un nivel desde qmake/
ROOT = $$PWD/..

INCLUDEPATH += $$ROOT/src $$ROOT/include

SOURCES += \
    $$ROOT/src/core/Logging.cpp \
    $$ROOT/src/geo/WebMercator.cpp \
    $$ROOT/src/geo/TileMatrix.cpp \
    $$ROOT/src/db/SqliteConnectionPool.cpp \
    $$ROOT/src/tiles/TileDataset.cpp \
    $$ROOT/src/tiles/RMapsTileSource.cpp \
    $$ROOT/src/tiles/TileDatasetProbe.cpp \
    $$ROOT/src/tiles/TileCache.cpp \
    $$ROOT/tools/probe_db/main.cpp

HEADERS += \
    $$ROOT/src/core/Logging.h \
    $$ROOT/src/geo/WebMercator.h \
    $$ROOT/src/geo/TileMatrix.h \
    $$ROOT/src/db/SqliteConnectionPool.h \
    $$ROOT/src/db/Transaction.h \
    $$ROOT/src/tiles/TileDataset.h \
    $$ROOT/src/tiles/TileKey.h \
    $$ROOT/src/tiles/ITileSource.h \
    $$ROOT/src/tiles/RMapsTileSource.h \
    $$ROOT/src/tiles/TileDatasetProbe.h \
    $$ROOT/src/tiles/TileCache.h

DESTDIR = $$ROOT/bin

# El ejecutable no necesita interfaz grafica, pero si enlaza QtGui para
# poder decodificar las imagenes y medir el tamano de las teselas.
