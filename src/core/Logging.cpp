#include "core/Logging.h"

// Definicion (una sola vez en todo el binario) de las cuatro categorias de log
// de la libreria. El .h solo las DECLARA; aqui se reservan sus objetos. El nombre
// entre comillas es el que se usa en QT_LOGGING_RULES para activar/silenciar cada
// area por separado (db, tiles, render, geo).
Q_LOGGING_CATEGORY(lcMapaDb,     "libmapa.db")
Q_LOGGING_CATEGORY(lcMapaTiles,  "libmapa.tiles")
Q_LOGGING_CATEGORY(lcMapaRender, "libmapa.render")
Q_LOGGING_CATEGORY(lcMapaGeo,    "libmapa.geo")
