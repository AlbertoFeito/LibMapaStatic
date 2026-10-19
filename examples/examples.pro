#-------------------------------------------------------------------
# examples.pro  -  todos los ejemplos con qmake, contra libmapa INSTALADA.
#
# Equivale a examples/CMakeLists.txt. Abrelo en Qt Creator con el MISMO kit con
# el que instalaste la libreria (docs/DESPLIEGUE.md, paso 2); cada subproyecto
# incluye C:/libmapa/qt5 o qt6 segun el kit (otra carpeta: LIBMAPA_PREFIX=...).
# Un ejemplo nuevo se apunta en SUBDIRS y en examples/CMakeLists.txt.
#-------------------------------------------------------------------

TEMPLATE = subdirs
SUBDIRS += app_minima app_seguimiento visor_gpx
