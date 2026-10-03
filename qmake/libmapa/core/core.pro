#-------------------------------------------------------------------
# core.pro  -  libmapa_core (estatica): datos, geodesia, teselas, SQLite.
#
# Lo compila libmapa.pro; no hace falta abrirlo por separado. Se genera en
# Release (mapa_core) y en Debug (mapa_cored): con Qt 5 MinGW una app Debug
# usa las DLL de Qt con "d" (Qt5Cored.dll) y no se puede mezclar con una
# biblioteca compilada contra las de Release.
#-------------------------------------------------------------------

include(../comun.pri)

TEMPLATE = lib
CONFIG += staticlib debug_and_release build_all
QT += core gui sql positioning
QT -= widgets

TARGET = mapa_core
CONFIG(debug, debug|release): TARGET = mapa_cored
DESTDIR = $$LIBOUT

SOURCES += \
    $$ROOT/src/core/Logging.cpp \
    $$ROOT/src/geo/WebMercator.cpp \
    $$ROOT/src/geo/TileMatrix.cpp \
    $$ROOT/src/db/SqliteConnectionPool.cpp \
    $$ROOT/src/db/Schema.cpp \
    $$ROOT/src/db/VectorRepository.cpp \
    $$ROOT/src/tiles/TileDataset.cpp \
    $$ROOT/src/tiles/RMapsTileSource.cpp \
    $$ROOT/src/tiles/TileDatasetProbe.cpp \
    $$ROOT/src/tiles/TileCache.cpp \
    $$ROOT/src/tiles/TilePlanner.cpp \
    $$ROOT/src/tiles/TileLoader.cpp \
    $$ROOT/src/tiles/TileService.cpp \
    $$ROOT/src/io/GeoFile.cpp \
    $$ROOT/src/io/DataPackage.cpp \
    $$ROOT/src/io/PackageCheck.cpp \
    $$ROOT/src/dem/GridElevation.cpp \
    $$ROOT/src/dem/HgtElevation.cpp \
    $$ROOT/src/dem/SqliteElevation.cpp

# Cabeceras con Q_OBJECT (para el MOC).
HEADERS += \
    $$ROOT/src/db/VectorRepository.h \
    $$ROOT/src/tiles/TileLoader.h \
    $$ROOT/src/tiles/TileService.h

# --- Instalacion (mingw32-make install) -------------------------------------
# La biblioteca, las cabeceras PUBLICAS (include/libmapa; las de src/ son
# internas) y el libmapa.pri que incluye la aplicacion.
target.path = $$PREFIX/lib

cabeceras.files = $$ROOT/include/libmapa/*.h
cabeceras.path = $$PREFIX/include/libmapa

pri.files = $$PWD/../libmapa.pri
pri.path = $$PREFIX

# libmapa.pri comprueba que la app usa el mismo Qt (5 o 6) que la biblioteca.
# Ese dato se escribe aqui, en el build, y se instala a su lado.
LIBMAPA_QT_LINEAS = "LIBMAPA_QT_MAJOR = $$QT_MAJOR_VERSION"
LIBMAPA_QT_LINEAS += "LIBMAPA_QT_VERSION = $$[QT_VERSION]"
write_file($$OUT_PWD/libmapa_qt.pri, LIBMAPA_QT_LINEAS)
pri_qt.files = $$OUT_PWD/libmapa_qt.pri
pri_qt.path = $$PREFIX

INSTALLS += target cabeceras pri pri_qt
