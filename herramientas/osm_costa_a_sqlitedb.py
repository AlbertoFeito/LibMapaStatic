#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Convierte una linea de costa de OSM (GeoJSON de Overpass, natural=coastline)
en una BD de entidades `.sqlitedb` lista para `dem_to_hillshade --water-mask`.

Uso tipico (en el PC, con el export de overpass-turbo):

    way["natural"="coastline"](19.6,-85.5,23.7,-73.5); out geom;

    python3 osm_costa_a_sqlitedb.py export.geojson -o cuba_tierra.sqlitedb

El GeoJSON trae la costa partida en muchos tramos (LineString) mas los cayos
pequenos ya cerrados (Polygon). La costa de OSM es una linea dirigida: aqui se
cosen los tramos por sus extremos comunes hasta cerrar cada anillo de tierra.
El resultado es UNA entidad poligono multi-parte (una parte por anillo); el
`--water-mask` rasteriza todas las partes con regla par-impar, de modo que los
anillos interiores (lagunas, bahias cerradas) restan como agua sin necesidad de
conocer su orientacion.

Esquema escrito (identico a src/db/Schema.cpp, version 2): tablas
`schema_version`, `entidad`, `entidad_vertice` (WITHOUT ROWID) y `capa`.
"""

import argparse
import json
import sqlite3
import sys
import time
from collections import defaultdict


# Lee el GeoJSON y devuelve (segmentos_abiertos, anillos_cerrados): los
# LineString/MultiLineString como listas de puntos a coser, y los rings de
# Polygon/MultiPolygon que ya vienen cerrados. Cada punto es [lon, lat].
def leer_geojson(ruta):
    with open(ruta, encoding="utf-8") as f:
        gj = json.load(f)
    segmentos = []
    anillos = []
    for ft in gj.get("features", []):
        g = ft.get("geometry") or {}
        t = g.get("type")
        c = g.get("coordinates")
        if not c:
            continue
        if t == "LineString":
            segmentos.append(c)
        elif t == "MultiLineString":
            segmentos.extend(c)
        elif t == "Polygon":
            anillos.extend(c)
        elif t == "MultiPolygon":
            for poly in c:
                anillos.extend(poly)
    return segmentos, anillos


# Cose los tramos de costa en anillos cerrados. Dos extremos se consideran el
# mismo nodo si coinciden al cuantizarlos con 'tol' grados (OSM comparte los
# nodos exactos entre tramos contiguos, asi que la tolerancia solo absorbe el
# ruido del texto). Devuelve (anillos_cerrados, cadenas_abiertas).
def coser(segmentos, tol):
    def clave(p):
        return (round(p[0] / tol), round(p[1] / tol))

    n = len(segmentos)
    usado = [False] * n
    extremos = defaultdict(list)          # clave de nodo -> [(indice, 0|1)]
    for i, s in enumerate(segmentos):
        extremos[clave(s[0])].append((i, 0))
        extremos[clave(s[-1])].append((i, 1))

    # Primer tramo sin usar que toca el nodo dado (o None).
    def libre(nodo):
        for (j, e) in extremos[nodo]:
            if not usado[j]:
                return (j, e)
        return None

    cerrados = []
    abiertas = []
    for i in range(n):
        if usado[i]:
            continue
        usado[i] = True
        cad = list(segmentos[i])
        # Extiende por la cola hasta cerrar o no encontrar continuacion.
        while clave(cad[-1]) != clave(cad[0]):
            nx = libre(clave(cad[-1]))
            if not nx:
                break
            j, e = nx
            usado[j] = True
            seg = segmentos[j] if e == 0 else segmentos[j][::-1]
            cad.extend(seg[1:])
        # Extiende por la cabeza (tramos que llegan al nodo inicial).
        while clave(cad[-1]) != clave(cad[0]):
            nx = libre(clave(cad[0]))
            if not nx:
                break
            j, e = nx
            usado[j] = True
            seg = segmentos[j] if e == 1 else segmentos[j][::-1]
            cad = seg[:-1] + cad
        if clave(cad[0]) == clave(cad[-1]):
            cerrados.append(cad)
        else:
            abiertas.append(cad)
    return cerrados, abiertas


# Crea el esquema de entidades (version 2) sobre una conexion sqlite3 ya abierta.
# Copia literal de src/db/Schema.cpp para que VectorRepository lo lea sin migrar.
def crear_esquema(con):
    con.execute("CREATE TABLE schema_version (version INTEGER NOT NULL)")
    con.execute("INSERT INTO schema_version (version) VALUES (2)")
    con.execute(
        "CREATE TABLE entidad ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  capa TEXT NOT NULL,"
        "  tipo TEXT NOT NULL DEFAULT '',"
        "  geometria INTEGER NOT NULL,"
        "  nombre TEXT NOT NULL DEFAULT '',"
        "  descripcion TEXT NOT NULL DEFAULT '',"
        "  color_linea INTEGER NOT NULL DEFAULT 0,"
        "  color_relleno INTEGER NOT NULL DEFAULT 0,"
        "  ancho_linea REAL NOT NULL DEFAULT 2,"
        "  estilo_linea INTEGER NOT NULL DEFAULT 1,"
        "  radio_px REAL NOT NULL DEFAULT 6,"
        "  etiqueta_visible INTEGER NOT NULL DEFAULT 1,"
        "  visible INTEGER NOT NULL DEFAULT 1,"
        "  simbolo BLOB,"
        "  atributos TEXT NOT NULL DEFAULT '{}',"
        "  creado_utc INTEGER NOT NULL)")
    con.execute("CREATE INDEX idx_entidad_capa ON entidad(capa)")
    con.execute("CREATE INDEX idx_entidad_tipo ON entidad(tipo)")
    con.execute(
        "CREATE TABLE entidad_vertice ("
        "  entidad_id INTEGER NOT NULL REFERENCES entidad(id) ON DELETE CASCADE,"
        "  parte INTEGER NOT NULL DEFAULT 0,"
        "  orden INTEGER NOT NULL,"
        "  latitud REAL NOT NULL,"
        "  longitud REAL NOT NULL,"
        "  PRIMARY KEY (entidad_id, parte, orden)) WITHOUT ROWID")
    con.execute(
        "CREATE TABLE capa ("
        "  id TEXT PRIMARY KEY,"
        "  nombre TEXT NOT NULL DEFAULT '',"
        "  visible INTEGER NOT NULL DEFAULT 1,"
        "  editable INTEGER NOT NULL DEFAULT 1,"
        "  z_orden INTEGER NOT NULL DEFAULT 0)")


# Convierte un color "#RRGGBB" o "#AARRGGBB" en el entero ARGB (QRgb) que
# guarda la columna. Sin prefijo alfa se asume opaco (0xFF). Se almacena como
# entero SIN signo para que VectorRepository lo lea con toUInt() -> fromRgba().
def color_argb(texto):
    s = texto.lstrip("#")
    if len(s) == 6:
        s = "ff" + s
    if len(s) != 8:
        raise ValueError("color debe ser #RRGGBB o #AARRGGBB: %r" % texto)
    return int(s, 16)


# Escribe todos los anillos como UNA entidad poligono multi-parte en la capa
# dada, con el estilo (línea/relleno/grosor) que se vera si la capa se dibuja
# como overlay vectorial. Una parte por anillo; los vertices van (parte, orden)
# con lat/lon. El par-impar del rasterizador convierte los anillos anidados en
# huecos de agua (el estilo no afecta a --water-mask, que solo lee geometria).
def escribir(con, capa, anillos, linea, relleno, grosor):
    ahora = int(time.time())
    con.execute(
        "INSERT INTO capa (id, nombre, visible, editable, z_orden)"
        " VALUES (?,?,1,0,0)", (capa, capa))
    cur = con.execute(
        "INSERT INTO entidad (capa, tipo, geometria, nombre,"
        " color_linea, color_relleno, ancho_linea, estilo_linea, creado_utc)"
        " VALUES (?,?,2,?,?,?,?,1,?)",
        (capa, "tierra", "costa_osm", color_argb(linea), color_argb(relleno),
         grosor, ahora))
    eid = cur.lastrowid
    filas = []
    for parte, anillo in enumerate(anillos):
        for orden, (lon, lat) in enumerate(anillo):
            filas.append((eid, parte, orden, lat, lon))
    con.executemany(
        "INSERT INTO entidad_vertice (entidad_id, parte, orden, latitud, longitud)"
        " VALUES (?,?,?,?,?)", filas)
    return len(filas)


# Escribe los anillos como un fichero .geo de texto (una coordenada "lon,lat"
# por linea; "0.0,0.0" separa trazados), el formato que consume geo_to_tiles
# para rasterizar la costa a una piramide de teselas rapida. Asi la MISMA costa
# OSM sirve para la mascara, para el overlay vectorial y para regenerar el
# raster Cuba_Vector con el mismo detalle. Devuelve el numero de vertices.
def escribir_geo(ruta, anillos):
    n = 0
    with open(ruta, "w", encoding="utf-8") as f:
        for anillo in anillos:
            for lon, lat in anillo:
                f.write("%.7f,%.7f\n" % (lon, lat))
                n += 1
            f.write("0.0,0.0\n")   # cierra este trazado y empieza el siguiente
    return n


# Orquesta la conversion: lee, cose, cierra los pocos tramos que queden sueltos
# (uniendo sus extremos) y vuelca la BD. Devuelve un pequeno informe por stdout.
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("geojson", help="Export de Overpass (natural=coastline).")
    ap.add_argument("-o", "--salida", default="cuba_tierra.sqlitedb",
                    help="BD de salida (por defecto cuba_tierra.sqlitedb).")
    ap.add_argument("--capa", default="tierra",
                    help="Nombre de la capa de tierra (por defecto 'tierra').")
    ap.add_argument("--tol", type=float, default=1e-7,
                    help="Tolerancia de union de nodos en grados (~1 cm).")
    ap.add_argument("--line-color", default="#1565c0",
                    help="Color de la costa como overlay (#RRGGBB o #AARRGGBB).")
    ap.add_argument("--fill-color", default="#00000000",
                    help="Relleno de la tierra como overlay (por defecto transparente).")
    ap.add_argument("--line-width", type=float, default=1.0,
                    help="Grosor de la línea de costa (px) como overlay.")
    ap.add_argument("--geo", metavar="FICHERO.geo",
                    help="Además, escribe la costa como .geo (para geo_to_tiles: "
                         "regenerar el ráster Cuba_Vector con el detalle OSM).")
    args = ap.parse_args()

    segmentos, ya_cerrados = leer_geojson(args.geojson)
    cerrados, abiertas = coser(segmentos, args.tol)
    # Los tramos que quedan sueltos (recortados por el bbox) se cierran uniendo
    # sus extremos: un cordal corto sobre un hueco minusculo, inofensivo.
    for cad in abiertas:
        if len(cad) >= 3:
            cerrados.append(cad + [cad[0]])
    anillos = [a for a in (ya_cerrados + cerrados) if len(a) >= 4]

    con = sqlite3.connect(args.salida)
    try:
        con.execute("PRAGMA journal_mode=OFF")
        con.execute("PRAGMA synchronous=OFF")
        crear_esquema(con)
        nvert = escribir(con, args.capa, anillos,
                         args.line_color, args.fill_color, args.line_width)
        con.commit()
    finally:
        con.close()

    if args.geo:
        escribir_geo(args.geo, anillos)

    print("Entrada:   %s" % args.geojson)
    print("Salida:    %s  (capa '%s')" % (args.salida, args.capa))
    print("Anillos:   %d  (cosidos %d + ya cerrados %d + forzados %d)"
          % (len(anillos), len(cerrados) - len(abiertas), len(ya_cerrados),
             len(abiertas)))
    print("Vertices:  %d" % nvert)
    if args.geo:
        print("GEO:       %s  (para geo_to_tiles -> raster Cuba_Vector)" % args.geo)
    print("Listo. Prueba:  dem_to_hillshade ... --water-mask %s --land-layer %s"
          % (args.salida, args.capa))
    return 0


if __name__ == "__main__":
    sys.exit(main())
