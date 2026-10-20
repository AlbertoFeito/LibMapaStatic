# cliente_telemetria - receptor de NMEA 0183 y AIS por UDP con libmapa y libtelemetria INSTALADAS.
#
# No contiene nada de las librerias, solo las encuentra con find_package. Para compilarlo solo:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#   cmake --build build
#
# o junto a los demas ejemplos: cmake -S .. -B build-examples (ver ../CMakeLists.txt).

cmake_minimum_required(VERSION 3.16)
project(cliente_telemetria LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libmapa REQUIRED)
find_package(libtelemetria REQUIRED)
# La red (QUdpSocket) la pone la aplicacion; ni libmapa ni libtelemetria enlazan Qt Network.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network)

add_executable(cliente_telemetria main.cpp)
target_link_libraries(cliente_telemetria PRIVATE libmapa::widget telemetria::telemetria Qt${QT_VERSION_MAJOR}::Network)

# Aplicacion de ventana en Windows (sin consola negra detras).
if(WIN32)
    set_target_properties(cliente_telemetria PROPERTIES WIN32_EXECUTABLE ON)
endif()
