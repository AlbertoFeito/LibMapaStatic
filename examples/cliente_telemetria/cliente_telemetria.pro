#-------------------------------------------------------------------
# cliente_telemetria.pro  -  el cliente de NMEA/AIS, como CMakeLists.txt, con qmake.
#
# Es la plantilla para un producto qmake que usa libmapa Y libtelemetria INSTALADAS (no
# toca las fuentes de las bibliotecas). Paso a paso en docs/DESPLIEGUE.md:
#
#   1. Instalar con tu Qt: qmake/libmapa/libmapa.pro + make install deja libmapa y
#      libtelemetria en C:/libmapa/qt5 o C:/libmapa/qt6.
#   2. Abrir este .pro en Qt Creator con el MISMO kit y compilar en Release.
#   3. Desplegar con su paquete de datos:
#        Qt 6:  C:\libmapa\qt6\share\libmapa\desplegar.bat     <exe> D:\Entrega D:\QtPro\Recursos
#        Qt 5:  C:\libmapa\qt5\share\libmapa\desplegar_qt5.bat <exe> D:\Entrega D:\QtPro\Recursos
#-------------------------------------------------------------------

QT += widgets network
CONFIG += c++17

# El puerto serie es OPCIONAL: Qt Serial Port es un modulo adicional (en el instalador de Qt de Windows, «Additional
# Libraries»). Si el kit no lo trae, compila igual con solo UDP y TCP y «Puerto serie» sale desactivado.
qtHaveModule(serialport) {
    QT += serialport
    DEFINES += TELEMETRIA_CON_SERIE
}

TARGET = cliente_telemetria
TEMPLATE = app
SOURCES += main.cpp

# Donde estan instaladas las bibliotecas. Por defecto, la que corresponde al Qt del
# kit (qt5 o qt6). Otra carpeta:  qmake LIBMAPA_PREFIX=D:/otra/carpeta
isEmpty(LIBMAPA_PREFIX): LIBMAPA_PREFIX = C:/libmapa/qt$${QT_MAJOR_VERSION}
!exists($$LIBMAPA_PREFIX/libmapa.pri) {
    error("No encuentro libmapa en $${LIBMAPA_PREFIX}. Instalala antes con este\
 mismo Qt (docs/DESPLIEGUE.md, paso 2).")
}
!exists($$LIBMAPA_PREFIX/telemetria.pri) {
    error("No encuentro libtelemetria en $${LIBMAPA_PREFIX}. libmapa.pro la instala junto a libmapa:\
 reinstala con este mismo Qt.")
}
include($$LIBMAPA_PREFIX/libmapa.pri)
include($$LIBMAPA_PREFIX/telemetria.pri)
