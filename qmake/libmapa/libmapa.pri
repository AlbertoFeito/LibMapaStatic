#-------------------------------------------------------------------
# libmapa.pri  -  lo que necesita un proyecto qmake para usar libmapa.
#
# Se instala en la raiz de la instalacion (C:/libmapa/qt5 o C:/libmapa/qt6).
# En el .pro de tu aplicacion basta con UNA linea:
#
#     include(C:/libmapa/qt$${QT_MAJOR_VERSION}/libmapa.pri)
#
# Escrita asi, la misma linea elige sola la instalacion qt5 o qt6 segun el
# kit con el que compiles. Despues ya puedes hacer
#     #include <libmapa/MapWidget.h>
#-------------------------------------------------------------------

LIBMAPA_DIR = $$PWD

# Con que Qt se compilo esta instalacion (lo escribe core.pro al instalar).
include($$LIBMAPA_DIR/libmapa_qt.pri)

# Una biblioteca compilada con Qt 5 no enlaza con una app de Qt 6 (ni al reves).
!equals(QT_MAJOR_VERSION, $$LIBMAPA_QT_MAJOR) {
    error("libmapa en $$LIBMAPA_DIR esta compilada con Qt $${LIBMAPA_QT_VERSION},\
 pero este kit es Qt $$[QT_VERSION]. Usa la instalacion de tu Qt o compila\
 libmapa con este kit (docs/DESPLIEGUE.md).")
}

QT += widgets sql positioning printsupport
DEFINES += LIBMAPA_STATIC
INCLUDEPATH += $$LIBMAPA_DIR/include

# Debug enlaza las versiones "d" (en Qt 5 MinGW, Debug usa otras DLL de Qt).
# El widget va ANTES que el nucleo: el widget usa el nucleo.
CONFIG(debug, debug|release): LIBMAPA_SUFIJO = d
else: LIBMAPA_SUFIJO =
LIBS += -L$$LIBMAPA_DIR/lib -lmapa_widget$${LIBMAPA_SUFIJO} -lmapa_core$${LIBMAPA_SUFIJO}

# Si se reinstala libmapa, la app se vuelve a enlazar sola.
win32-msvc* {
    PRE_TARGETDEPS += $$LIBMAPA_DIR/lib/mapa_widget$${LIBMAPA_SUFIJO}.lib \
                      $$LIBMAPA_DIR/lib/mapa_core$${LIBMAPA_SUFIJO}.lib
} else {
    PRE_TARGETDEPS += $$LIBMAPA_DIR/lib/libmapa_widget$${LIBMAPA_SUFIJO}.a \
                      $$LIBMAPA_DIR/lib/libmapa_core$${LIBMAPA_SUFIJO}.a
}
