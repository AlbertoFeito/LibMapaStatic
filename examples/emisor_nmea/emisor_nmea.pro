#-------------------------------------------------------------------
# emisor_nmea.pro  -  el emisor de NMEA/AIS, como CMakeLists.txt, con qmake.
#
# Solo usa libtelemetria INSTALADA (no libmapa) y Qt Network. Paso a paso:
#
#   1. Instalar las bibliotecas con tu Qt (qmake/libmapa/libmapa.pro + make install
#      instala libmapa y libtelemetria; telemetria.pro, solo esta). Queda en
#      C:/libmapa/qt5 o C:/libmapa/qt6.
#   2. Abrir este .pro en Qt Creator con el MISMO kit y compilar.
#-------------------------------------------------------------------

QT += network
QT -= gui
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = emisor_nmea
TEMPLATE = app
SOURCES += main.cpp

# Donde estan instaladas las bibliotecas. Por defecto, la que corresponde al Qt del
# kit (qt5 o qt6). Otra carpeta:  qmake LIBMAPA_PREFIX=D:/otra/carpeta
isEmpty(LIBMAPA_PREFIX): LIBMAPA_PREFIX = C:/libmapa/qt$${QT_MAJOR_VERSION}
!exists($$LIBMAPA_PREFIX/telemetria.pri) {
    error("No encuentro libtelemetria en $${LIBMAPA_PREFIX}. Instalala antes con este\
 mismo Qt (qmake/libtelemetria/telemetria.pro, make install).")
}
include($$LIBMAPA_PREFIX/telemetria.pri)
