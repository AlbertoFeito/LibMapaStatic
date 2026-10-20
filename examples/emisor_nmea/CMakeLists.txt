# emisor_nmea - simula un receptor de NMEA 0183 y AIS con libtelemetria INSTALADA.
#
# No usa libmapa: solo libtelemetria y Qt Network. Para compilarlo solo:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#   cmake --build build
#
# o junto a los demas ejemplos: cmake -S .. -B build-examples (ver ../CMakeLists.txt).

cmake_minimum_required(VERSION 3.16)
project(emisor_nmea LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libtelemetria REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

add_executable(emisor_nmea main.cpp)
target_link_libraries(emisor_nmea PRIVATE telemetria::telemetria Qt${QT_VERSION_MAJOR}::Network)
