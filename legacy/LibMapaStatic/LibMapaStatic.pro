QT += core gui widgets printsupport positioning sql

greaterThan(QT_MAJOR_VERSION, 5) {
    # Qt 6 - necesita core5compat
    QT += core5compat
    CONFIG += c++17
}

TARGET = LibMapaStatic
TEMPLATE = lib
CONFIG += staticlib


CONFIG(debug, debug|release) {
    DESTDIR = $$PWD/../build/debug
} else {
    DESTDIR = $$PWD/../build/release
}

OBJECTS_DIR = $$DESTDIR/.obj/$$TARGET
MOC_DIR = $$DESTDIR/.moc/$$TARGET
RCC_DIR = $$DESTDIR/.qrc/$$TARGET
UI_DIR = $$DESTDIR/.ui/$$TARGET

DEPENDPATH = $$PWD/../Recursos

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    LibMapaStatic.cpp \
    cbdatosmapa.cpp \
    ccalculos.cpp \
    cgooglemercator.cpp \
    cmapaplot.cpp \
    cprojection.cpp \
    qcustomplot.cpp \
    utiles.cpp \
    vnuevopunto.cpp \
    wrjfile.cpp \
    vtrazaruta.cpp \
    cpunto.cpp

HEADERS += \
    definiciones.h \
    LibMapaStatic.h \
    cbdatosmapa.h \
    ccalculos.h \
    cgooglemercator.h \
    cmapaplot.h \
    constantes.h \
    cprojection.h \
    definiciones.h \
    qcustomplot.h \
    utiles.h \
    vnuevopunto.h \
    wrjfile.h \
    vtrazaruta.h \
    cpunto.h

# Default rules for deployment.
unix {
    target.path = $$[QT_INSTALL_PLUGINS]/generic
}
!isEmpty(target.path): INSTALLS += target

FORMS += \
   vnuevopunto.ui \
    vtrazaruta.ui

RESOURCES +=

