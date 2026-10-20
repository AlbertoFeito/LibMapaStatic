#-------------------------------------------------------------------
# telemetria.pri  -  lo que necesita un proyecto qmake para usar libtelemetria.
#
# Se instala en la raiz de la instalacion, junto a libmapa.pri (C:/libmapa/qt5 o
# C:/libmapa/qt6). En el .pro de tu aplicacion, UNA linea:
#
#     include(C:/libmapa/qt$${QT_MAJOR_VERSION}/telemetria.pri)
#
# y despues  #include <telemetria/Decoder.h>
#-------------------------------------------------------------------

TELEMETRIA_DIR = $$PWD

# Con que Qt se compilo esta instalacion (lo escribe telemetria.pro al instalar).
include($$TELEMETRIA_DIR/telemetria_qt.pri)

# Una biblioteca compilada con Qt 5 no enlaza con una app de Qt 6 (ni al reves).
!equals(QT_MAJOR_VERSION, $$TELEMETRIA_QT_MAJOR) {
    error("libtelemetria en $$TELEMETRIA_DIR esta compilada con Qt $${TELEMETRIA_QT_VERSION},\
 pero este kit es Qt $$[QT_VERSION]. Usa la instalacion de tu Qt o compila\
 libtelemetria con este kit.")
}

DEFINES += TELEMETRIA_STATIC
INCLUDEPATH += $$TELEMETRIA_DIR/include

# Debug enlaza la version «d» (en Qt 5 MinGW, Debug usa otras DLL de Qt).
CONFIG(debug, debug|release): TELEMETRIA_SUFIJO = d
else: TELEMETRIA_SUFIJO =
LIBS += -L$$TELEMETRIA_DIR/lib -ltelemetria$${TELEMETRIA_SUFIJO}

# Si se reinstala la biblioteca, la app se vuelve a enlazar sola.
win32-msvc* {
    PRE_TARGETDEPS += $$TELEMETRIA_DIR/lib/telemetria$${TELEMETRIA_SUFIJO}.lib
} else {
    PRE_TARGETDEPS += $$TELEMETRIA_DIR/lib/libtelemetria$${TELEMETRIA_SUFIJO}.a
}
