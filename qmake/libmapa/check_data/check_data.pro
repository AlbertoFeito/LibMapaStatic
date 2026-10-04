#-------------------------------------------------------------------
# check_data.pro  -  revisa un paquete de datos (mapa.json) y lo copia.
#
# El script de despliegue lo usa para copiar solo los ficheros del paquete.
# Se compila en Release y se instala en <PREFIX>/bin junto con el script de
# despliegue que corresponde a este Qt (desplegar.bat o desplegar_qt5.bat).
#-------------------------------------------------------------------

include(../comun.pri)

TEMPLATE = app
CONFIG += console release
CONFIG -= app_bundle debug_and_release
QT += core gui sql positioning
QT -= widgets

TARGET = check_data
DESTDIR = $$OUT_PWD/../bin

SOURCES += $$ROOT/tools/check_data/main.cpp

LIBS += -L$$LIBOUT -lmapa_core
win32-msvc*: PRE_TARGETDEPS += $$LIBOUT/mapa_core.lib
else: PRE_TARGETDEPS += $$LIBOUT/libmapa_core.a

target.path = $$PREFIX/bin

equals(QT_MAJOR_VERSION, 5): despliegue.files = $$ROOT/herramientas/desplegar_qt5.bat
else: despliegue.files = $$ROOT/herramientas/desplegar.bat
despliegue.files += $$ROOT/mapa.example.json
despliegue.path = $$PREFIX/share/libmapa
# Son ficheros de texto: sin esto qmake les pasaria strip como a un .exe.
despliegue.CONFIG += nostrip

INSTALLS += target despliegue
