# app_seguimiento - ejemplo de seguimiento de objetivos con libmapa INSTALADA.
#
# Muestra el API de objetivos (alertas CPA, zonas, anillos, filtro,
# agrupacion, reproduccion del historial, exportacion). Como app_minima, no
# contiene nada de la libreria: la encuentra con find_package. Para compilarla:
#
#   1. Instalar libmapa (desde la carpeta de la libreria):
#        cmake --install <build de libmapa> --prefix C:/libmapa
#   2. Compilar esta app apuntando a esa instalacion y a Qt:
#        cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#        cmake --build build
#   3. Desplegarla con su paquete de datos:
#        C:\libmapa\share\libmapa\desplegar.bat build\app_seguimiento.exe D:\Entrega D:\QtPro\Recursos

cmake_minimum_required(VERSION 3.16)
project(app_seguimiento LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libmapa REQUIRED)

add_executable(app_seguimiento main.cpp)
target_link_libraries(app_seguimiento PRIVATE libmapa::widget)

# Aplicacion de ventana en Windows (sin consola negra detras).
if(WIN32)
    set_target_properties(app_seguimiento PROPERTIES WIN32_EXECUTABLE ON)
endif()
