#-------------------------------------------------------------------
# telemetria.pro  -  libtelemetria (estatica) con qmake: decodificador de NMEA 0183 y AIS.
#
# Es independiente de libmapa: solo Qt Core. Se instala en la MISMA carpeta que
# libmapa (C:/libmapa/qt5 o qt6, o la que des con PREFIX) para que una aplicacion
# encuentre las dos bajo un solo prefijo:
#
#   1. Abre este fichero en Qt Creator con tu kit y compila.
#   2. Instala:  make install  (en Windows con MinGW:  mingw32-make install)
#      Otra carpeta:  qmake PREFIX=D:/otra/carpeta
#
# Se genera en Release (telemetria) y en Debug (telemetriad), por lo mismo que
# libmapa: con Qt 5 MinGW una app Debug usa las DLL de Qt con «d».
#-------------------------------------------------------------------

CONFIG += c++17
DEFINES += TELEMETRIA_STATIC

ROOT = $$clean_path($$PWD/../../libtelemetria)
isEmpty(PREFIX): PREFIX = C:/libmapa/qt$${QT_MAJOR_VERSION}

INCLUDEPATH += $$ROOT/include

# Nada de Qt 5.7 ni anteriores: el objetivo es 5.14 / 5.15 / 6.x.
lessThan(QT_MAJOR_VERSION, 5)|equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 14) {
    error("libtelemetria necesita Qt 5.14 o posterior (este kit es Qt $$[QT_VERSION]).")
}

TEMPLATE = lib
CONFIG += staticlib debug_and_release build_all
QT = core

TARGET = telemetria
CONFIG(debug, debug|release): TARGET = telemetriad
DESTDIR = $$OUT_PWD/lib

SOURCES += \
    $$ROOT/src/Nmea.cpp \
    $$ROOT/src/Ais.cpp \
    $$ROOT/src/Decoder.cpp

# --- Instalacion (make install) ---------------------------------------------
target.path = $$PREFIX/lib

cabeceras.files = $$ROOT/include/telemetria/*.h
cabeceras.path = $$PREFIX/include/telemetria

pri.files = $$PWD/telemetria.pri
pri.path = $$PREFIX

# telemetria.pri comprueba que la app usa el mismo Qt (5 o 6) que la biblioteca.
TELEMETRIA_QT_LINEAS = "TELEMETRIA_QT_MAJOR = $$QT_MAJOR_VERSION"
TELEMETRIA_QT_LINEAS += "TELEMETRIA_QT_VERSION = $$[QT_VERSION]"
write_file($$OUT_PWD/telemetria_qt.pri, TELEMETRIA_QT_LINEAS)
pri_qt.files = $$OUT_PWD/telemetria_qt.pri
pri_qt.path = $$PREFIX

INSTALLS += target cabeceras pri pri_qt
