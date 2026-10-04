#-------------------------------------------------------------------
# app_minima.pro  -  la misma app minima que CMakeLists.txt, con qmake.
#
# Es la plantilla para un producto qmake que usa libmapa INSTALADA (no toca
# las fuentes de la libreria). Paso a paso en docs/DESPLIEGUE.md:
#
#   1. Instalar libmapa con tu Qt (qmake/libmapa/libmapa.pro + make install).
#      Queda en C:/libmapa/qt5 o C:/libmapa/qt6.
#   2. Abrir este .pro en Qt Creator con el MISMO kit y compilar en Release.
#   3. Desplegar con su paquete de datos:
#        Qt 6:  C:\libmapa\qt6\share\libmapa\desplegar.bat     <exe> D:\Entrega D:\QtPro\Recursos
#        Qt 5:  C:\libmapa\qt5\share\libmapa\desplegar_qt5.bat <exe> D:\Entrega D:\QtPro\Recursos
#-------------------------------------------------------------------

QT += widgets
CONFIG += c++17

TARGET = app_minima
TEMPLATE = app
SOURCES += main.cpp

# Donde esta libmapa instalada. Por defecto, la que corresponde al Qt del
# kit (qt5 o qt6). Otra carpeta:  qmake LIBMAPA_PREFIX=D:/otra/carpeta
isEmpty(LIBMAPA_PREFIX): LIBMAPA_PREFIX = C:/libmapa/qt$${QT_MAJOR_VERSION}
!exists($$LIBMAPA_PREFIX/libmapa.pri) {
    error("No encuentro libmapa en $${LIBMAPA_PREFIX}. Instalala antes con este\
 mismo Qt (docs/DESPLIEGUE.md, paso 2).")
}
include($$LIBMAPA_PREFIX/libmapa.pri)
