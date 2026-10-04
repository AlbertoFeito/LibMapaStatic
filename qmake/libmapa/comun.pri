#-------------------------------------------------------------------
# comun.pri  -  ajustes compartidos por los .pro de qmake/libmapa.
#
# ROOT    raiz del repositorio (donde estan src/ e include/).
# LIBOUT  carpeta del build donde quedan las bibliotecas estaticas.
# PREFIX  carpeta de instalacion. Por defecto C:/libmapa/qt5 o C:/libmapa/qt6
#         segun el Qt con el que se compile, para poder tener las dos a la
#         vez. Se cambia al lanzar qmake:  qmake PREFIX=D:/otra/carpeta
#-------------------------------------------------------------------

CONFIG += c++17
DEFINES += LIBMAPA_STATIC

ROOT = $$clean_path($$PWD/../..)
LIBOUT = $$clean_path($$OUT_PWD/../lib)

isEmpty(PREFIX): PREFIX = C:/libmapa/qt$${QT_MAJOR_VERSION}

INCLUDEPATH += $$ROOT/include $$ROOT/src

# Nada de Qt 5.7 ni anteriores: el objetivo es 5.14 / 5.15 / 6.x.
lessThan(QT_MAJOR_VERSION, 5)|equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 14) {
    error("libmapa necesita Qt 5.14 o posterior (este kit es Qt $$[QT_VERSION]).")
}
