# visor_gpx - visor de rutas y tracks (GPX, KML, GeoJSON) con libmapa INSTALADA.
#
# Igual que app_minima: no contiene nada de la libreria, solo la encuentra con
# find_package. Para compilarla sola:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#   cmake --build build
#
# o junto a los demas ejemplos: cmake -S .. -B build-examples (ver ../CMakeLists.txt).

cmake_minimum_required(VERSION 3.16)
project(visor_gpx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libmapa REQUIRED)

add_executable(visor_gpx main.cpp)
target_link_libraries(visor_gpx PRIVATE libmapa::widget)

# Aplicacion de ventana en Windows (sin consola negra detras).
if(WIN32)
    set_target_properties(visor_gpx PROPERTIES WIN32_EXECUTABLE ON)
endif()
