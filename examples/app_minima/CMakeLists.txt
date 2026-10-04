# app_minima - la aplicacion mas pequena que usa libmapa INSTALADA.
#
# Es la plantilla para un producto: no contiene nada de la libreria, solo la
# encuentra con find_package. Para compilarla:
#
#   1. Instalar libmapa (desde la carpeta de la libreria):
#        cmake --install <build de libmapa> --prefix C:/libmapa
#   2. Compilar esta app apuntando a esa instalacion y a Qt:
#        cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#        cmake --build build
#   3. Desplegarla con su paquete de datos:
#        C:\libmapa\share\libmapa\desplegar.bat build\app_minima.exe D:\Entrega D:\QtPro\Recursos

cmake_minimum_required(VERSION 3.16)
project(app_minima LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libmapa REQUIRED)

add_executable(app_minima main.cpp)
target_link_libraries(app_minima PRIVATE libmapa::widget)

# Aplicacion de ventana en Windows (sin consola negra detras).
if(WIN32)
    set_target_properties(app_minima PROPERTIES WIN32_EXECUTABLE ON)
endif()
