cmake_minimum_required(VERSION 3.16)
project(libmapa VERSION 0.1.0 LANGUAGES C CXX)

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
        # Estos tres solo existen en C++ (hay un .c de terceros, miniz, que
        # compila con el compilador de C): limitarlos a CXX evita el aviso
        # "option is valid for C++ but not for C".
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
        -Wcast-align
        -Wunused
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
    )
    # -Wnull-dereference no se usa: dispara falsos positivos dentro de
    # QCustomPlot y de los contenedores de Qt (QVector/QCPRange inline), que
    # inundan la salida sin aportar nada sobre NUESTRO codigo.
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

    src/io/GeoFile.cpp
    include/libmapa/GeoFile.h
    src/io/DataPackage.cpp
    src/io/DataPackage.h
    include/libmapa/DataPackage.h
    src/io/PackageCheck.cpp
    src/io/PackageCheck.h

    src/dem/IElevationSource.h
    src/dem/GridElevation.cpp
    src/dem/GridElevation.h
    src/dem/HgtElevation.cpp
    src/dem/HgtElevation.h
    src/dem/SqliteElevation.cpp
    src/dem/SqliteElevation.h
    src/dem/ElevationAnalysis.cpp
    src/dem/ElevationAnalysis.h
    src/dem/Contours.cpp
)

target_include_directories(libmapa_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# Nombre con el que lo ve otro proyecto: libmapa::core, tanto instalado
# (find_package) como metido con add_subdirectory (el alias).
set_target_properties(libmapa_core PROPERTIES EXPORT_NAME core OUTPUT_NAME mapa_core)
add_library(libmapa::core ALIAS libmapa_core)

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
        src/widget/TargetModel.cpp
        src/widget/TargetModel.h
        src/widget/TargetLayer.cpp
        src/widget/TargetLayer.h
        src/widget/CoverageLayer.cpp
        src/widget/CoverageLayer.h
        src/widget/HillshadeLayer.cpp
        src/widget/HillshadeLayer.h
        src/widget/MapView.cpp
        src/widget/MapView.h
        src/widget/MapWidget.cpp
        include/libmapa/MapWidget.h
        include/libmapa/MapTypes.h
        include/libmapa/MapFeature.h
        include/libmapa/MapTarget.h
    )

    target_include_directories(libmapa_widget
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    set_target_properties(libmapa_widget PROPERTIES EXPORT_NAME widget OUTPUT_NAME mapa_widget)
    add_library(libmapa::widget ALIAS libmapa_widget)

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
    target_link_libraries(demo PRIVATE libmapa_widget Qt${QT_VERSION_MAJOR}::PrintSupport)
    # El demo dibuja el perfil con QCustomPlot (ya compilado dentro de
    # libmapa_widget); su cabecera va como SYSTEM para que nuestros -W... no la marquen.
    target_include_directories(demo SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})
endif()

# -----------------------------------------------------------------------------
if(LIBMAPA_BUILD_TOOLS)
    add_executable(probe_db tools/probe_db/main.cpp)
    target_link_libraries(probe_db PRIVATE libmapa_core)

    # Comprueba un paquete de datos (mapa.json) antes de distribuirlo.
    add_executable(check_data tools/check_data/main.cpp)
    target_link_libraries(check_data PRIVATE libmapa_core)

    add_executable(bench_tiles tools/bench_tiles/main.cpp)
    target_link_libraries(bench_tiles PRIVATE libmapa_core)

    # Construye una BD de elevacion (.sqlitedb) desde una carpeta de `.hgt`.
    add_executable(dem_to_db tools/dem_to_db/main.cpp)
    target_link_libraries(dem_to_db PRIVATE libmapa_core)
    target_include_directories(dem_to_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Rasteriza vector (.geo/.xyz) a un piramide de teselas SQLite. Necesita
    # QGuiApplication (QImage/QPainter): en un servidor sin pantalla se ejecuta
    # con QT_QPA_PLATFORM=offscreen.
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)
    add_executable(geo_to_tiles tools/geo_to_tiles/main.cpp)
    target_link_libraries(geo_to_tiles PRIVATE libmapa_core Qt${QT_VERSION_MAJOR}::Gui)
    target_include_directories(geo_to_tiles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Hornea un relieve sombreado (hillshade) del DEM a una piramide de teselas
    # SQLite (offline, para la vista general). Tambien necesita Gui (QImage).
    add_executable(dem_to_hillshade tools/dem_to_hillshade/main.cpp)
    target_link_libraries(dem_to_hillshade PRIVATE libmapa_core Qt${QT_VERSION_MAJOR}::Gui)
    target_include_directories(dem_to_hillshade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Genera curvas de nivel (isohipsas) del DEM y las guarda como capa vectorial
    # (.sqlitedb de entidades) para activar/desactivar y leer la cota. Solo core.
    add_executable(dem_to_contours tools/dem_to_contours/main.cpp)
    target_link_libraries(dem_to_contours PRIVATE libmapa_core)
    target_include_directories(dem_to_contours PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Rellena huecos de una BD de teselas descargando de una fuente XYZ sin
    # clave (Esri satelite por defecto). El motor (TileFiller) es comun a la
    # version de consola y a la de ventana con mapa. Necesita el modulo Network.
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network)
    add_executable(fill_tiles
        tools/fill_tiles/main.cpp
        tools/common/TileFiller.cpp
        tools/common/TileFiller.h)
    target_link_libraries(fill_tiles PRIVATE libmapa_core Qt${QT_VERSION_MAJOR}::Network)
    target_include_directories(fill_tiles PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/common)

    # Descarga ficheros de elevacion SRTM .hgt (30 m) de AWS Skadi (sin clave) y
    # los descomprime con miniz (descompresor gzip vendoreado, dominio publico).
    # Es AUTONOMO: no necesita zlib externa, asi que compila igual en Linux/GCC y
    # en Qt MinGW de Windows sin instalar nada. Alimenta a dem_to_db.
    add_executable(fill_hgt
        tools/fill_hgt/main.cpp
        third_party/miniz/miniz.c)
    target_link_libraries(fill_hgt PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
    target_include_directories(fill_hgt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz)
    # miniz.c es codigo de terceros: sin nuestros avisos (nos llenaria de ruido).
    if(NOT MSVC)
        set_source_files_properties(third_party/miniz/miniz.c
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    if(LIBMAPA_HAVE_QCP)
        add_executable(render_map tools/render_map/main.cpp)
        target_link_libraries(render_map PRIVATE libmapa_widget)
        target_include_directories(render_map PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_include_directories(render_map SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})

        # Ventana con mapa para rellenar teselas (mismo motor TileFiller).
        add_executable(fill_map
            tools/fill_map/main.cpp
            tools/common/TileFiller.cpp
            tools/common/TileFiller.h)
        target_link_libraries(fill_map PRIVATE
            libmapa_widget Qt${QT_VERSION_MAJOR}::Network)
        target_include_directories(fill_map PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/common)
        target_include_directories(fill_map SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})
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
    libmapa_add_test(tst_hgtelevation)
    libmapa_add_test(tst_sqliteelevation)
    libmapa_add_test(tst_elevationanalysis)
    libmapa_add_test(tst_contours)
    libmapa_add_test(tst_datapackage)
    libmapa_add_test(tst_packagecheck)

    libmapa_add_test(tst_geofile)
    target_compile_definitions(tst_geofile PRIVATE
        LIBMAPA_TESTDATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

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

        add_executable(tst_targetmodel tests/tst_targetmodel.cpp)
        target_link_libraries(tst_targetmodel
            PRIVATE libmapa_widget Qt${QT_VERSION_MAJOR}::Test)
        target_include_directories(tst_targetmodel
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_include_directories(tst_targetmodel SYSTEM PRIVATE ${LIBMAPA_QCP_DIR})
        add_test(NAME tst_targetmodel COMMAND tst_targetmodel)
    endif()
endif()

# -----------------------------------------------------------------------------
# Guarda: la LIBRERIA no usa la red.
#
# El producto final trabaja sin conexion. Solo las herramientas que preparan
# los datos (fill_tiles, fill_map, fill_hgt) pueden enlazar Qt Network. Si
# alguien se lo anade a libmapa_core o libmapa_widget, la configuracion se
# para aqui en vez de dejar que la red se cuele en todas las apps.
# (Verificado ademas que Qt Positioning, la unica dependencia que podria
# arrastrarla, solo depende de Qt Core.)
# -----------------------------------------------------------------------------
foreach(_libmapa_lib libmapa_core libmapa_widget)
    if(TARGET ${_libmapa_lib})
        get_target_property(_libmapa_deps ${_libmapa_lib} LINK_LIBRARIES)
        get_target_property(_libmapa_ideps ${_libmapa_lib} INTERFACE_LINK_LIBRARIES)
        if("${_libmapa_deps};${_libmapa_ideps}" MATCHES "Network")
            message(FATAL_ERROR
                "${_libmapa_lib} enlaza Qt Network, y la libreria debe funcionar "
                "sin conexion. La red es solo para las herramientas de descarga.")
        endif()
    endif()
endforeach()

# -----------------------------------------------------------------------------
# Instalacion: para usar la libreria desde OTRO proyecto con
#
#     find_package(libmapa REQUIRED)
#     target_link_libraries(miapp PRIVATE libmapa::widget)
#
# Se instalan las dos bibliotecas (estaticas: no hay DLL propia que repartir),
# las cabeceras PUBLICAS (include/libmapa; las de src/ son internas), el
# libmapaConfig.cmake que busca las dependencias de Qt, y las herramientas de
# consola (check_data, probe_db...) para preparar y revisar los datos.
#
#     cmake --install build --prefix C:/libmapa
# -----------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_libmapa_install_default ON)
else()
    set(_libmapa_install_default OFF)    # metida con add_subdirectory
endif()
option(LIBMAPA_INSTALL "Generar las reglas de instalacion" ${_libmapa_install_default})

if(LIBMAPA_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(_libmapa_targets libmapa_core)
    if(TARGET libmapa_widget)
        list(APPEND _libmapa_targets libmapa_widget)
    endif()

    install(TARGETS ${_libmapa_targets}
        EXPORT libmapaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/libmapa
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

    install(EXPORT libmapaTargets
        NAMESPACE libmapa::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libmapa)

    # El Config sabe con que Qt (5 o 6) se compilo y si trae el widget.
    set(LIBMAPA_CONFIG_HAVE_WIDGET ${LIBMAPA_HAVE_QCP})
    configure_package_config_file(
        cmake/libmapaConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/libmapaConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libmapa)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/libmapaConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libmapaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/libmapaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libmapa)

    # Herramientas de consola que existan en esta configuracion.
    foreach(_libmapa_tool check_data probe_db render_map dem_to_db fill_hgt
                          fill_tiles fill_map geo_to_tiles bench_tiles dem_to_hillshade
                          dem_to_contours)
        if(TARGET ${_libmapa_tool})
            install(TARGETS ${_libmapa_tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endforeach()

    install(FILES mapa.example.json herramientas/desplegar.bat
        DESTINATION ${CMAKE_INSTALL_DATADIR}/libmapa)
endif()
