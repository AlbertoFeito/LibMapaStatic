cmake_minimum_required(VERSION 3.16)
project(libmapa VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(LIBMAPA_BUILD_TESTS "Compilar los tests"           ON)
option(LIBMAPA_BUILD_TOOLS "Compilar las herramientas CLI" ON)
option(LIBMAPA_BUILD_DEMO  "Compilar la aplicacion demo"   ON)

# Qt5 o Qt6, indistintamente.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Sql Positioning)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Sql Positioning)

# -----------------------------------------------------------------------------
# QCustomPlot: motor de dibujo del widget.
#
# NO se incluye en el repositorio: se usa el que ya tenga el proyecto. Se busca
# en, por este orden:
#   1. -DLIBMAPA_QCP_DIR=ruta
#   2. third_party/qcustomplot/
#   3. la variable de entorno QCUSTOMPLOT_DIR
#
# Si no aparece, se compila solo el nucleo (teselas, geodesia, BD) y se avisa.
# Asi la libreria sigue siendo util y testeable sin QCustomPlot delante.
# -----------------------------------------------------------------------------
set(LIBMAPA_QCP_DIR "" CACHE PATH "Carpeta con qcustomplot.h y qcustomplot.cpp")

if(NOT LIBMAPA_QCP_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qcustomplot/qcustomplot.h")
    set(LIBMAPA_QCP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qcustomplot")
endif()
if(NOT LIBMAPA_QCP_DIR AND DEFINED ENV{QCUSTOMPLOT_DIR})
    set(LIBMAPA_QCP_DIR "$ENV{QCUSTOMPLOT_DIR}")
endif()

set(LIBMAPA_HAVE_QCP FALSE)
if(LIBMAPA_QCP_DIR AND EXISTS "${LIBMAPA_QCP_DIR}/qcustomplot.h"
                   AND EXISTS "${LIBMAPA_QCP_DIR}/qcustomplot.cpp")
    set(LIBMAPA_HAVE_QCP TRUE)
    message(STATUS "QCustomPlot encontrado en ${LIBMAPA_QCP_DIR}")
else()
    message(WARNING
        "QCustomPlot no encontrado: se compila solo el nucleo, sin MapWidget.\n"
        "  Indica su ubicacion con -DLIBMAPA_QCP_DIR=/ruta/a/qcustomplot\n"
        "  o copia qcustomplot.h/.cpp en third_party/qcustomplot/")
endif()

# -----------------------------------------------------------------------------
# Avisos del compilador.
#
# -Woverloaded-virtual es el que habria detectado por si solo la falla F-18:
# CMapaPlot declaraba "virtual void resizeEvent();" creyendo sobreescribir
# QWidget::resizeEvent(QResizeEvent*). No sobreescribia nada: era codigo
# muerto que nunca se llamaba.
# -----------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
    )
endif()

# -----------------------------------------------------------------------------
# Nucleo sin interfaz grafica: datos, geodesia y teselas.
# Se separa a proposito para poder testearlo sin ventanas.
# -----------------------------------------------------------------------------
add_library(libmapa_core STATIC
    src/core/Logging.cpp
    src/core/Logging.h

    src/geo/WebMercator.cpp
    src/geo/WebMercator.h
    src/geo/TileMatrix.cpp
    src/geo/TileMatrix.h

    src/db/SqliteConnectionPool.cpp
    src/db/SqliteConnectionPool.h
    src/db/Transaction.h
    src/db/Schema.cpp
    src/db/Schema.h
    src/db/VectorRepository.cpp
    src/db/VectorRepository.h

    src/tiles/TileDataset.cpp
    src/tiles/TileDataset.h
    src/tiles/TileKey.h
    src/tiles/ITileSource.h
    src/tiles/RMapsTileSource.cpp
    src/tiles/RMapsTileSource.h
    src/tiles/TileDatasetProbe.cpp
    src/tiles/TileDatasetProbe.h
    src/tiles/TileCache.cpp
    src/tiles/TileCache.h
    src/tiles/TilePlanner.cpp
    src/tiles/TilePlanner.h
    src/tiles/TileLoader.cpp
    src/tiles/TileLoader.h
    src/tiles/TileService.cpp
    src/tiles/TileService.h
)

target_include_directories(libmapa_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(libmapa_core
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Positioning
)

target_compile_definitions(libmapa_core PUBLIC LIBMAPA_STATIC)

# -----------------------------------------------------------------------------
# Widget de mapa. Requiere QCustomPlot, Widgets y PrintSupport.
# -----------------------------------------------------------------------------
if(LIBMAPA_HAVE_QCP)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport)

    add_library(libmapa_widget STATIC
        ${LIBMAPA_QCP_DIR}/qcustomplot.cpp
        src/widget/TileLayer.cpp
        src/widget/TileLayer.h
        src/widget/OverlayModel.cpp
        src/widget/OverlayModel.h
        src/widget/FeatureLayer.cpp
        src/widget/FeatureLayer.h
        src/widget/MapView.cpp
        src/widget/MapView.h
        src/widget/MapWidget.cpp
        include/libmapa/MapWidget.h
        include/libmapa/MapTypes.h
        include/libmapa/MapFeature.h
    )

    target_include_directories(libmapa_widget
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # QCustomPlot como cabecera de SISTEMA: sus 25 avisos con nuestro juego de
    # -W... son ruido que no podemos arreglar y que taparia los nuestros.
    target_include_directories(libmapa_widget SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})

    target_link_libraries(libmapa_widget
        PUBLIC
            libmapa_core
            Qt${QT_VERSION_MAJOR}::Widgets
        PRIVATE
            Qt${QT_VERSION_MAJOR}::PrintSupport
    )

    # qcustomplot.cpp es codigo de terceros: no se le aplican nuestros avisos,
    # que lo llenarian de ruido sin que podamos arreglarlo.
    if(NOT MSVC)
        set_source_files_properties(${LIBMAPA_QCP_DIR}/qcustomplot.cpp
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# -----------------------------------------------------------------------------
if(LIBMAPA_BUILD_DEMO AND LIBMAPA_HAVE_QCP)
    add_executable(demo demo/main.cpp)
    target_link_libraries(demo PRIVATE libmapa_widget)
endif()

# -----------------------------------------------------------------------------
if(LIBMAPA_BUILD_TOOLS)
    add_executable(probe_db tools/probe_db/main.cpp)
    target_link_libraries(probe_db PRIVATE libmapa_core)

    add_executable(bench_tiles tools/bench_tiles/main.cpp)
    target_link_libraries(bench_tiles PRIVATE libmapa_core)

    add_executable(vector_db tools/vector_db/main.cpp)
    target_link_libraries(vector_db PRIVATE libmapa_core)

    if(LIBMAPA_HAVE_QCP)
        add_executable(render_map tools/render_map/main.cpp)
        target_link_libraries(render_map PRIVATE libmapa_widget)
        target_include_directories(render_map PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_include_directories(render_map SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})
    endif()
endif()

# -----------------------------------------------------------------------------
if(LIBMAPA_BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    enable_testing()

    function(libmapa_add_test name)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE libmapa_core Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    # -------------------------------------------------------------------
    # Comprobador de autosuficiencia de cabeceras.
    #
    # Compila un .cpp por cabecera que no incluye nada mas. Una cabecera que
    # solo compila si antes se incluyo otra funciona por casualidad, segun el
    # orden que imponga cada .cpp, y estalla al cambiar de compilador.
    #
    # Se anadio despues de que MinGW fallara donde GCC/Linux compilaba: la
    # misma cabecera se alcanzaba como "TileKey.h", "../tiles/TileKey.h" y
    # "tiles/TileKey.h", y #pragma once no siempre reconoce que son el mismo
    # fichero. Ahora hay guardas reales y una unica grafia por ruta.
    # -------------------------------------------------------------------
    file(GLOB LIBMAPA_SELFCONTAINED CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/selfcontained/sc_*.cpp")
    add_executable(tst_headers_selfcontained ${LIBMAPA_SELFCONTAINED})
    target_link_libraries(tst_headers_selfcontained PRIVATE libmapa_core)
    add_test(NAME tst_headers_selfcontained COMMAND tst_headers_selfcontained)

    libmapa_add_test(tst_tilematrix)
    libmapa_add_test(tst_connectionpool)
    libmapa_add_test(tst_tilesource)
    libmapa_add_test(tst_probe)
    libmapa_add_test(tst_tilecache)
    libmapa_add_test(tst_tileplanner)
    libmapa_add_test(tst_tileservice)
    libmapa_add_test(tst_vectorrepository)

    if(LIBMAPA_HAVE_QCP)
        add_executable(tst_overlaymodel tests/tst_overlaymodel.cpp)
        target_link_libraries(tst_overlaymodel
            PRIVATE libmapa_widget Qt${QT_VERSION_MAJOR}::Test)
        target_include_directories(tst_overlaymodel
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_include_directories(tst_overlaymodel SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})
        add_test(NAME tst_overlaymodel COMMAND tst_overlaymodel)

        add_executable(tst_mapwidget tests/tst_mapwidget.cpp)
        target_link_libraries(tst_mapwidget
            PRIVATE libmapa_widget Qt${QT_VERSION_MAJOR}::Test)
        target_include_directories(tst_mapwidget
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_include_directories(tst_mapwidget SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})
        add_test(NAME tst_mapwidget COMMAND tst_mapwidget)
    endif()
endif()
