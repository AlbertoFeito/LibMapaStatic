#-------------------------------------------------------------------
# widget.pro  -  libmapa_widget (estatica): MapWidget y sus capas.
#
# Lleva QCustomPlot dentro. Se busca en third_party/qcustomplot/ o donde diga
# la variable LIBMAPA_QCP_DIR:  qmake LIBMAPA_QCP_DIR=D:/ruta/a/qcustomplot
#-------------------------------------------------------------------

include(../comun.pri)

TEMPLATE = lib
CONFIG += staticlib debug_and_release build_all
QT += core gui widgets printsupport sql positioning

TARGET = mapa_widget
CONFIG(debug, debug|release): TARGET = mapa_widgetd
DESTDIR = $$LIBOUT

isEmpty(LIBMAPA_QCP_DIR): LIBMAPA_QCP_DIR = $$ROOT/third_party/qcustomplot
!exists($$LIBMAPA_QCP_DIR/qcustomplot.h)|!exists($$LIBMAPA_QCP_DIR/qcustomplot.cpp) {
    error("Falta QCustomPlot 2.1.1: copia qcustomplot.h y qcustomplot.cpp en\
 $$ROOT/third_party/qcustomplot/ (ver el LEEME.md de esa carpeta)\
 o indica su carpeta con: qmake LIBMAPA_QCP_DIR=<ruta>")
}
INCLUDEPATH += $$LIBMAPA_QCP_DIR

SOURCES += \
    $$LIBMAPA_QCP_DIR/qcustomplot.cpp \
    $$ROOT/src/widget/TileLayer.cpp \
    $$ROOT/src/widget/OverlayModel.cpp \
    $$ROOT/src/widget/FeatureLayer.cpp \
    $$ROOT/src/widget/TargetModel.cpp \
    $$ROOT/src/widget/TargetLayer.cpp \
    $$ROOT/src/widget/CoverageLayer.cpp \
    $$ROOT/src/widget/MapView.cpp \
    $$ROOT/src/widget/MapWidget.cpp

# Cabeceras con Q_OBJECT (para el MOC).
HEADERS += \
    $$LIBMAPA_QCP_DIR/qcustomplot.h \
    $$ROOT/include/libmapa/MapWidget.h \
    $$ROOT/src/widget/TileLayer.h \
    $$ROOT/src/widget/OverlayModel.h \
    $$ROOT/src/widget/FeatureLayer.h \
    $$ROOT/src/widget/TargetModel.h \
    $$ROOT/src/widget/TargetLayer.h \
    $$ROOT/src/widget/CoverageLayer.h \
    $$ROOT/src/widget/MapView.h

target.path = $$PREFIX/lib
INSTALLS += target
