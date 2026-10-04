#-------------------------------------------------------------------
# dem_to_db.pro  -  alternativa qmake, por si prefieres no usar CMake.
#
# Construye una base de datos de elevacion (.sqlitedb) a partir de una
# carpeta de ficheros SRTM `.hgt`. La BD la lee SqliteElevation.
#
# El proyecto principal usa CMake (CMakeLists.txt en la raiz). Este .pro
# existe para lanzar la herramienta con qmake sin mas.
#-------------------------------------------------------------------

QT += core sql
QT -= gui widgets

CONFIG += console
CONFIG -= app_bundle

CONFIG += c++17

TARGET = dem_to_db
TEMPLATE = app

ROOT = $$PWD/..

# Solo necesita el helper de transaccion (cabecera, sin .cpp del nucleo).
INCLUDEPATH += $$ROOT/src $$ROOT/include

SOURCES += \
    $$ROOT/tools/dem_to_db/main.cpp

HEADERS += \
    $$ROOT/src/db/Transaction.h

DESTDIR = $$ROOT/bin
