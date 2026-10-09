#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Convierte divisiones administrativas de OSM (GeoJSON de Overpass: relaciones
boundary=administrative) en una BD de entidades `.sqlitedb` para cargarla como
capa vectorial (overlay) en libmapa, UNA entidad por división con su nombre.

Uso tipico (en el PC, con el export de overpass-turbo). Provincias de Cuba:

    [out:json][timeout:300];
    {{geocodeArea:Cuba}}->.cu;
    relation["boundary"="administrative"]["admin_level"="4"](area.cu);
    out geom;

    python3 osm_divisiones_a_sqlitedb.py export.geojson -o provincias.sqlitedb --capa provincias

A diferencia de la costa (que se funde en una sola mascara de tierra), cada
relacion es una division CON NOMBRE, asi que se escribe una `entidad` por cada
una (geometria de poligono, multi-parte si tiene varios anillos/islas), con su
`nombre` para que la capa la rotule y se pueda seleccionar. Overpass-turbo ya
ensambla las relaciones boundary en `Polygon`/`MultiPolygon`; si el export trae
solo lineas sueltas, se cosen como respaldo en una unica capa sin nombres.

Esquema escrito (identico a src/db/Schema.cpp, version 2): tablas
`schema_version`, `entidad`, `entidad_vertice` (WITHOUT ROWID) y `capa`.
"""

import argparse
import json
import os
import sqlite3
import sys
import time
from collections import defaultdict


# Convierte un color "#RRGGBB" o "#AARRGGBB" en el entero ARGB (QRgb) que guarda
# la columna. Sin alfa se asume opaco. Se almacena sin signo para que
# VectorRepository lo lea con toUInt() -> QColor::fromRgba().
def color_argb(texto):
    s = texto.lstrip("#")
    if len(s) == 6:
        s = "ff" + s
    if len(s) != 8:
        raise ValueError("color debe ser #RRGGBB o #AARRGGBB: %r" % texto)
    return int(s, 16)


# Lee el GeoJSON y devuelve (divisiones, lineas_sueltas). Cada division es
# (nombre, anillos) donde anillos es la lista de todas sus partes (anillos
# exteriores e interiores de su Polygon/MultiPolygon), cada una lista de puntos
# [lon,lat]. Las lineas sueltas (si las hubiera) se devuelven aparte para el
# respaldo de cosido.
def leer_geojson(ruta, clave_nombre):
    with open(ruta, encoding="utf-8") as f:
        gj = json.load(f)

    divisiones = []
    lineas = []
    for ft in gj.get("features", []):
        g = ft.get("geometry") or {}
        t = g.get("type")
        c = g.get("coordinates")
        if not c:
            continue
        nombre = (ft.get("properties") or {}).get(clave_nombre) or ""
        if t == "Polygon":
            divisiones.append((nombre, [anillo for anillo in c if len(anillo) >= 4]))
        elif t == "MultiPolygon":
            partes = []
            for poly in c:
                for anillo in poly:
                    if len(anillo) >= 4:
                        partes.append(anillo)
            if partes:
                divisiones.append((nombre, partes))
        elif t == "LineString":
            lineas.append(c)
        elif t == "MultiLineString":
            lineas.extend(c)
    return divisiones, lineas


# Cose tramos de linea en anillos cerrados (respaldo, igual que el conversor de
# costa) por si el export trae lineas en vez de poligonos. Devuelve la lista de
# anillos cerrados.
def coser(segmentos, tol):
    def clave(p):
        return (round(p[0] / tol), round(p[1] / tol))

    n = len(segmentos)
    usado = [False] * n
    extremos = defaultdict(list)
    for i, s in enumerate(segmentos):
        extremos[clave(s[0])].append((i, 0))
        extremos[clave(s[-1])].append((i, 1))

    def libre(nodo):
        for (j, e) in extremos[nodo]:
            if not usado[j]:
                return (j, e)
        return None

    anillos = []
    for i in range(n):
        if usado[i]:
            continue
        usado[i] = True
        cad = list(segmentos[i])
        while clave(cad[-1]) != clave(cad[0]):
            nx = libre(clave(cad[-1]))
            if not nx:
                break
            j, e = nx
            usado[j] = True
            cad.extend((segmentos[j] if e == 0 else segmentos[j][::-1])[1:])
        while clave(cad[-1]) != clave(cad[0]):
            nx = libre(clave(cad[0]))
            if not nx:
                break
            j, e = nx
            usado[j] = True
            cad = (segmentos[j] if e == 1 else segmentos[j][::-1])[:-1] + cad
        if len(cad) >= 3:
            anillos.append(cad if clave(cad[0]) == clave(cad[-1]) else cad + [cad[0]])
    return anillos


# Crea el esquema de entidades (version 2) sobre una conexion sqlite3 abierta.
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


# Escribe una entidad poligono por division (con su nombre y estilo). Cada anillo
# de la division va como una parte; los vertices (parte, orden) con lat/lon.
# Devuelve (n_entidades, n_vertices).
def escribir(con, capa, divisiones, linea, relleno, grosor, etiqueta):
    ahora = int(time.time())
    con.execute(
        "INSERT INTO capa (id, nombre, visible, editable, z_orden)"
        " VALUES (?,?,1,0,0)", (capa, capa))
    cl, cr = color_argb(linea), color_argb(relleno)
    nent = nvert = 0
    for nombre, anillos in divisiones:
        cur = con.execute(
            "INSERT INTO entidad (capa, tipo, geometria, nombre,"
            " color_linea, color_relleno, ancho_linea, estilo_linea,"
            " etiqueta_visible, creado_utc)"
            " VALUES (?,?,2,?,?,?,?,1,?,?)",
            (capa, "division", nombre, cl, cr, grosor,
             1 if etiqueta else 0, ahora))
        eid = cur.lastrowid
        filas = []
        for parte, anillo in enumerate(anillos):
            for orden, (lon, lat) in enumerate(anillo):
                filas.append((eid, parte, orden, lat, lon))
        con.executemany(
            "INSERT INTO entidad_vertice (entidad_id, parte, orden, latitud, longitud)"
            " VALUES (?,?,?,?,?)", filas)
        nent += 1
        nvert += len(filas)
    return nent, nvert


# Escribe TODOS los anillos de todas las divisiones como un .geo (lon,lat;
# "0.0,0.0" separa trazados) para rasterizar las fronteras con geo_to_tiles. El
# raster pierde los nombres (solo dibuja lineas), pero es rapido a bajo zoom.
def escribir_geo(ruta, divisiones):
    n = 0
    with open(ruta, "w", encoding="utf-8") as f:
        for _nombre, anillos in divisiones:
            for anillo in anillos:
                for lon, lat in anillo:
                    f.write("%.7f,%.7f\n" % (lon, lat))
                    n += 1
                f.write("0.0,0.0\n")
    return n


# Orquesta: lee el GeoJSON, arma una entidad por division (o cose lineas de
# respaldo), vuelca la BD y, opcionalmente, un .geo. Informe por stdout.
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("geojson", help="Export de Overpass (relaciones boundary).")
    ap.add_argument("-o", "--salida", default="provincias.sqlitedb",
                    help="BD de salida (por defecto provincias.sqlitedb).")
    ap.add_argument("--capa", default="provincias",
                    help="Nombre/id de la capa (por defecto 'provincias').")
    ap.add_argument("--name-key", default="name", dest="name_key",
                    help="Propiedad del GeoJSON con el nombre (por defecto 'name').")
    ap.add_argument("--line-color", default="#c62828",
                    help="Color de la frontera (#RRGGBB o #AARRGGBB).")
    ap.add_argument("--fill-color", default="#00000000",
                    help="Relleno de la division (por defecto transparente).")
    ap.add_argument("--line-width", type=float, default=1.5,
                    help="Grosor de la línea de frontera (px).")
    ap.add_argument("--no-labels", action="store_true",
                    help="No marcar las divisiones para mostrar su etiqueta.")
    ap.add_argument("--tol", type=float, default=1e-7,
                    help="Tolerancia de cosido (solo respaldo de líneas).")
    ap.add_argument("--geo", metavar="FICHERO.geo",
                    help="Además, escribe las fronteras como .geo (para geo_to_tiles).")
    args = ap.parse_args()

    divisiones, lineas = leer_geojson(args.geojson, args.name_key)
    if not divisiones and lineas:
        # Respaldo: el export trajo lineas, no poligonos. Se cosen en una sola
        # capa sin nombres (no se puede saber a que division va cada tramo).
        anillos = coser(lineas, args.tol)
        if anillos:
            divisiones = [("", anillos)]
        print("Aviso: el export no traia poligonos; se cosieron %d anillos sin "
              "nombre." % len(anillos), file=sys.stderr)
    if not divisiones:
        print("No se encontraron divisiones (ni poligonos ni lineas) en %s"
              % args.geojson, file=sys.stderr)
        return 1

    for sufijo in ("", "-wal", "-shm"):
        try:
            os.remove(args.salida + sufijo)
        except OSError:
            pass

    con = sqlite3.connect(args.salida)
    try:
        con.execute("PRAGMA journal_mode=OFF")
        con.execute("PRAGMA synchronous=OFF")
        crear_esquema(con)
        nent, nvert = escribir(con, args.capa, divisiones, args.line_color,
                               args.fill_color, args.line_width, not args.no_labels)
        con.commit()
    finally:
        con.close()

    if args.geo:
        escribir_geo(args.geo, divisiones)

    con_nombre = sum(1 for n, _ in divisiones if n)
    print("Entrada:    %s" % args.geojson)
    print("Salida:     %s  (capa '%s')" % (args.salida, args.capa))
    print("Divisiones: %d  (%d con nombre)" % (nent, con_nombre))
    print("Vertices:   %d" % nvert)
    if args.geo:
        print("GEO:        %s  (para geo_to_tiles)" % args.geo)
    print("Cárgala como overlay .sqlitedb en tu mapa.json.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
