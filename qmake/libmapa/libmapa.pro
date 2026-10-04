#-------------------------------------------------------------------
# libmapa.pro  -  compila e instala libmapa SOLO con qmake (sin CMake).
#
# Vale para Qt 5.14 / 5.15 / 6.x. Es la via para quien no tiene CMake, por
# ejemplo un Qt 5.14 con MinGW 7.3. Paso a paso en docs/DESPLIEGUE.md.
#
#   1. Abre este fichero en Qt Creator con tu kit (p.ej. Qt 5.14.2 MinGW 64-bit).
#   2. Compilar -> Compilar todo.
#   3. Instalar:  Proyectos -> Compilar -> Pasos de compilacion ->
#      Anadir paso -> Make, con argumentos:  install
#      (o en consola, en la carpeta del build:  mingw32-make install)
#
# Queda en C:/libmapa/qt5 (o qt6), con la misma forma que la instalacion de
# CMake: include/, lib/, bin/check_data.exe, share/libmapa/ y libmapa.pri.
# Otra carpeta:  qmake PREFIX=D:/otra/carpeta
#-------------------------------------------------------------------

TEMPLATE = subdirs

SUBDIRS = core widget check_data
widget.depends = core
check_data.depends = core
