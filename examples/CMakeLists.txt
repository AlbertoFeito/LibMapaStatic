# examples - todas las aplicaciones de ejemplo, contra libmapa INSTALADA.
#
# Es un proyecto aparte de la libreria (NO se mete con add_subdirectory en el
# CMakeLists de la raiz): cada ejemplo hace find_package(libmapa), exactamente
# como lo hara un producto, asi que compilarlos aqui prueba el contrato de la
# instalacion. Para compilarlos todos de una vez:
#
#   cmake --install <build de libmapa> --prefix C:/libmapa
#   cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH="C:/libmapa;C:/Qt/6.11.2/mingw_64"
#   cmake --build build-examples
#
# Cada ejemplo sigue compilandose tambien solo (su propio CMakeLists.txt o .pro).
# Un ejemplo nuevo se apunta en la lista de abajo y en examples.pro.

cmake_minimum_required(VERSION 3.16)
project(libmapa_examples LANGUAGES CXX)

# Fallo claro y una sola vez si la libreria no esta instalada (o si falta el
# widget, que necesitan todos).
find_package(libmapa REQUIRED)
# libtelemetria (decodificador NMEA/AIS) va instalada junto a libmapa: la usan emisor_nmea y cliente_telemetria.
find_package(libtelemetria REQUIRED)

foreach(_ejemplo app_minima app_seguimiento visor_gpx editor_rutas buscador_lugares medidor_campo monitor_trafico vigilancia_zonas analisis_terreno rutas_tierra laboratorio_anillos
                 emisor_nmea cliente_telemetria)
    add_subdirectory(${_ejemplo})
endforeach()
