# libtelemetria - decodificador de NMEA 0183 y AIS.
#
# Biblioteca APARTE de libmapa: no sabe nada de mapas ni de ventanas. Solo Qt Core.
# Convierte lineas de texto (lo que llega por UDP, TCP, serie o de un fichero de
# captura) en datos: posicion y rumbo propios (RMC, GGA, VTG, HDT) y mensajes AIS de
# los demas barcos (tipos 1, 2, 3, 5, 18, 19 y 24). Tambien los codifica, para
# simular un receptor y para probar el decodificador.
#
# Se compila sola o metida en la raiz del repositorio (add_subdirectory) y se
# instala con el mismo prefijo que libmapa:
#
#     find_package(libtelemetria REQUIRED)
#     target_link_libraries(miapp PRIVATE telemetria::telemetria)

cmake_minimum_required(VERSION 3.16)
project(libtelemetria VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_telemetria_standalone ON)
else()
    set(_telemetria_standalone OFF)
endif()

option(LIBTELEMETRIA_BUILD_TESTS "Compilar los tests de libtelemetria" ON)
option(LIBTELEMETRIA_INSTALL "Generar las reglas de instalacion de libtelemetria" ${_telemetria_standalone})

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Mismos avisos que libmapa. Metida en la raiz ya heredan los suyos.
if(_telemetria_standalone)
    if(MSVC)
        add_compile_options(/W4 /permissive-)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Woverloaded-virtual -Wnon-virtual-dtor
                            -Wold-style-cast -Wcast-align -Wunused -Wconversion -Wsign-conversion
                            -Wdouble-promotion)
    endif()
endif()

add_library(libtelemetria STATIC
    src/Nmea.cpp
    src/Ais.cpp
    src/Decoder.cpp
)

target_include_directories(libtelemetria PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Nombre con el que lo ve otro proyecto: telemetria::telemetria, instalado o con add_subdirectory.
set_target_properties(libtelemetria PROPERTIES EXPORT_NAME telemetria OUTPUT_NAME telemetria)
add_library(telemetria::telemetria ALIAS libtelemetria)

target_link_libraries(libtelemetria PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(libtelemetria PUBLIC TELEMETRIA_STATIC)

# Guarda: esta biblioteca solo decodifica texto. Ni red ni interfaz grafica ni mapa.
get_target_property(_telemetria_deps libtelemetria LINK_LIBRARIES)
if("${_telemetria_deps}" MATCHES "Network|Gui|Widgets|Sql|Positioning|libmapa")
    message(FATAL_ERROR "libtelemetria debe depender solo de Qt Core: quien lee la red o dibuja es la aplicacion.")
endif()

# -----------------------------------------------------------------------------
if(LIBTELEMETRIA_BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(_t tst_nmea tst_ais tst_decoder)
        add_executable(${_t} tests/${_t}.cpp)
        target_link_libraries(${_t} PRIVATE libtelemetria Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME ${_t} COMMAND ${_t})
    endforeach()
endif()

# -----------------------------------------------------------------------------
if(LIBTELEMETRIA_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS libtelemetria
        EXPORT libtelemetriaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/telemetria
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

    install(EXPORT libtelemetriaTargets
        NAMESPACE telemetria::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libtelemetria)

    configure_package_config_file(
        cmake/libtelemetriaConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/libtelemetriaConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libtelemetria)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/libtelemetriaConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libtelemetriaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/libtelemetriaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libtelemetria)
endif()
