# vigilancia_zonas - vigilar zonas y registrar entradas y salidas con libmapa INSTALADA.
#
# Igual que app_minima: no contiene nada de la libreria, solo la encuentra con
# find_package. Para compilarla sola:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#   cmake --build build
#
# o junto a los demas ejemplos: cmake -S .. -B build-examples (ver ../CMakeLists.txt).

cmake_minimum_required(VERSION 3.16)
project(vigilancia_zonas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libmapa REQUIRED)

add_executable(vigilancia_zonas main.cpp)
target_link_libraries(vigilancia_zonas PRIVATE libmapa::widget)

# Aplicacion de ventana en Windows (sin consola negra detras).
if(WIN32)
    set_target_properties(vigilancia_zonas PROPERTIES WIN32_EXECUTABLE ON)
endif()
