#-------------------------------------------------------------------
# fill_map.pro  -  alternativa qmake, por si prefieres no usar CMake.
#
# Ventana con MAPA para rellenar huecos de teselas: marcas un rectangulo,
# eliges el rango de zoom y descarga lo que falta (Esri satelite, sin clave).
#
# Abre este fichero con Qt Creator y pulsa Ejecutar, o desde consola:
#   qmake && make            (Windows/MinGW: qmake && mingw32-make)
#
# El proyecto principal usa CMake; este .pro es la via qmake equivalente.
#-------------------------------------------------------------------

QT += core gui widgets sql positioning printsupport network

CONFIG += console
CONFIG -= app_bundle

CONFIG += c++17

TARGET = fill_map
TEMPLATE = app

DEFINES += LIBMAPA_STATIC

ROOT = $$PWD/..
QCP  = $$ROOT/third_party/qcustomplot

INCLUDEPATH += $$ROOT/src $$ROOT/include $$ROOT/tools/common $$QCP

# --- Nucleo (datos, geodesia, teselas, SQLite, .geo) ------------------------
SOURCES += \
    $$ROOT/src/core/Logging.cpp \
    $$ROOT/src/geo/WebMercator.cpp \
    $$ROOT/src/geo/TileMatrix.cpp \
    $$ROOT/src/db/SqliteConnectionPool.cpp \
    $$ROOT/src/db/Schema.cpp \
    $$ROOT/src/db/VectorRepository.cpp \
    $$ROOT/src/io/GeoFile.cpp \
    $$ROOT/src/io/DataPackage.cpp \
    $$ROOT/src/dem/GridElevation.cpp \
    $$ROOT/src/dem/HgtElevation.cpp \
    $$ROOT/src/dem/SqliteElevation.cpp \
    $$ROOT/src/tiles/TileDataset.cpp \
    $$ROOT/src/tiles/RMapsTileSource.cpp \
    $$ROOT/src/tiles/TileDatasetProbe.cpp \
    $$ROOT/src/tiles/TileCache.cpp \
    $$ROOT/src/tiles/TilePlanner.cpp \
    $$ROOT/src/tiles/TileLoader.cpp \
    $$ROOT/src/tiles/TileService.cpp

# --- Widget del mapa --------------------------------------------------------
SOURCES += \
    $$ROOT/src/widget/MapWidget.cpp \
    $$ROOT/src/widget/MapView.cpp \
    $$ROOT/src/widget/TileLayer.cpp \
    $$ROOT/src/widget/FeatureLayer.cpp \
    $$ROOT/src/widget/OverlayModel.cpp \
    $$ROOT/src/widget/TargetModel.cpp \
    $$ROOT/src/widget/TargetLayer.cpp \
    $$ROOT/src/widget/CoverageLayer.cpp \
    $$QCP/qcustomplot.cpp

# --- Motor de descarga comun + la app ---------------------------------------
SOURCES += \
    $$ROOT/tools/common/TileFiller.cpp \
    $$ROOT/tools/fill_map/main.cpp

# Cabeceras con Q_OBJECT (para el MOC).
HEADERS += \
    $$ROOT/include/libmapa/MapWidget.h \
    $$ROOT/src/widget/MapView.h \
    $$ROOT/src/widget/OverlayModel.h \
    $$ROOT/src/widget/FeatureLayer.h \
    $$ROOT/src/widget/TargetModel.h \
    $$ROOT/src/widget/TargetLayer.h \
    $$ROOT/src/widget/TileLayer.h \
    $$ROOT/src/widget/CoverageLayer.h \
    $$ROOT/src/tiles/TileService.h \
    $$ROOT/src/tiles/TileLoader.h \
    $$ROOT/src/db/VectorRepository.h \
    $$ROOT/tools/common/TileFiller.h \
    $$QCP/qcustomplot.h

DESTDIR = $$ROOT/bin
