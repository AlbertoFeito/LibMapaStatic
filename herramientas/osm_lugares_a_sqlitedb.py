#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Convierte lugares de OpenStreetMap (GeoJSON de Overpass) en una base de datos
de BUSQUEDA `lugares.sqlitedb` para libmapa: escribes "guantanamo" y el mapa va alli.

Uso tipico (en el PC, con el export de overpass-turbo). Lugares de Cuba:

    [out:json][timeout:300];
    {{geocodeArea:Cuba}}->.cu;
    (
      node["place"~"^(city|town|village|hamlet|suburb|island|islet|locality)$"]["name"](area.cu);
      nwr["aeroway"="aerodrome"]["name"](area.cu);
      node["harbour"="yes"]["name"](area.cu);
      node["natural"~"^(bay|cape|peak|strait)$"]["name"](area.cu);
    );
    out center;

    Exportar -> GeoJSON -> descargar, y luego:

    python3 osm_lugares_a_sqlitedb.py lugares_cuba.geojson -o lugares.sqlitedb

Cada elemento con nombre es una fila. Los que no son un punto (un aerodromo
mapeado como poligono) se reducen al centro de su caja. Se guarda ademas el nombre
NORMALIZADO (minusculas y sin tildes) en `busqueda`, para que "Banes", "banes" y
"Báñes" encuentren lo mismo; la libreria normaliza igual lo que escribe la persona.
`peso` ordena los resultados: una ciudad antes que una aldea del mismo nombre.

Esquema escrito:
    lugar(id INTEGER PRIMARY KEY, nombre, busqueda, tipo, lat, lon, poblacion, peso)
    lugar_meta(clave PRIMARY KEY, valor)         -- version, creado, origen, filas
"""

import argparse
import json
import math
import os
import sqlite3
import sys
import time
import unicodedata

# Tipo interno por etiqueta de OSM, y peso base (mayor = sale antes).
PESOS = {
    "city": 100, "town": 80, "island": 70, "airport": 70, "village": 50,
    "bay": 45, "harbour": 45, "cape": 40, "peak": 40, "suburb": 40,
    "strait": 35, "hamlet": 30, "islet": 30, "locality": 20,
}


# Minusculas y sin tildes: "Núñez Jiménez" -> "nunez jimenez". Es la misma
# normalizacion que PlaceIndex::normalize en C++ (descomposicion NFKD, fuera las
# marcas combinantes, minusculas y espacios simples).
def normalizar(texto):
    d = unicodedata.normalize("NFKD", texto)
    d = "".join(c for c in d if not unicodedata.combining(c))
    return " ".join(d.lower().split())


# Tipo interno de un elemento segun sus etiquetas, o None si no interesa.
def tipo_de(props):
    lugar = props.get("place")
    if lugar in PESOS:
        return lugar
    if props.get("aeroway") == "aerodrome":
        return "airport"
    if props.get("harbour") == "yes":
        return "harbour"
    natural = props.get("natural")
    if natural in ("bay", "cape", "peak", "strait"):
        return natural
    return None


# Recorre todas las coordenadas de una geometria GeoJSON (cualquier anidamiento).
def coordenadas(geom):
    if not geom:
        return
    t = geom.get("type")
    c = geom.get("coordinates")
    if t == "GeometryCollection":
        for g in geom.get("geometries", []):
            yield from coordenadas(g)
    elif t == "Point" and c:
        yield c
    elif c:
        pila = [c]
        while pila:
            x = pila.pop()
            if x and isinstance(x[0], (int, float)):
                yield x
            else:
                pila.extend(x)


# Centro (lat, lon) de una geometria: el propio punto, o el centro de su caja.
def centro(geom):
    pts = list(coordenadas(geom))
    if not pts:
        return None
    lons = [p[0] for p in pts]
    lats = [p[1] for p in pts]
    return (min(lats) + max(lats)) / 2.0, (min(lons) + max(lons)) / 2.0


# Habitantes como entero (OSM los da como texto, a veces con separadores) o 0.
def poblacion_de(props):
    txt = str(props.get("population", "")).replace(",", "").replace(".", "").strip()
    return int(txt) if txt.isdigit() else 0


# Lee el GeoJSON y devuelve la lista de filas (sin id) ya normalizadas y sin duplicados.
def leer(ruta, clave_nombre):
    with open(ruta, encoding="utf-8") as f:
        gj = json.load(f)
    filas = []
    vistos = set()
    for ft in gj.get("features", []):
        props = ft.get("properties") or {}
        nombre = (props.get(clave_nombre) or props.get("name") or "").strip()
        tipo = tipo_de(props)
        if not nombre or not tipo:
            continue
        c = centro(ft.get("geometry"))
        if c is None:
            continue
        lat, lon = c
        busqueda = normalizar(nombre)
        # Un aerodromo suele venir como nodo Y como poligono: se queda uno.
        clave = (busqueda, tipo, round(lat, 2), round(lon, 2))
        if clave in vistos:
            continue
        vistos.add(clave)
        pob = poblacion_de(props)
        peso = PESOS[tipo] + (int(math.log10(pob)) if pob > 0 else 0)
        filas.append((nombre, busqueda, tipo, lat, lon, pob, peso))
    return filas


# Punto de entrada: escribe la BD (en un .part que se renombra al final).
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("geojson", help="Export de Overpass (lugares).")
    ap.add_argument("-o", "--salida", default="lugares.sqlitedb", help="BD de salida.")
    ap.add_argument("--name-key", default="name", dest="name_key",
                    help="Etiqueta del nombre (por defecto name; p. ej. name:es).")
    ap.add_argument("--overwrite", action="store_true", help="Reemplazar la salida si existe.")
    args = ap.parse_args()

    if os.path.exists(args.salida) and not args.overwrite:
        sys.exit("%s ya existe (usa --overwrite)" % args.salida)
    filas = leer(args.geojson, args.name_key)
    if not filas:
        sys.exit("No se encontro ningun lugar con nombre en %s" % args.geojson)

    parte = args.salida + ".part"
    if os.path.exists(parte):
        os.remove(parte)
    con = sqlite3.connect(parte)
    con.executescript("""
        CREATE TABLE lugar(id INTEGER PRIMARY KEY, nombre TEXT NOT NULL,
            busqueda TEXT NOT NULL, tipo TEXT NOT NULL, lat REAL NOT NULL,
            lon REAL NOT NULL, poblacion INTEGER NOT NULL DEFAULT 0, peso INTEGER NOT NULL);
        CREATE INDEX lugar_busqueda ON lugar(busqueda);
        CREATE TABLE lugar_meta(clave TEXT PRIMARY KEY, valor TEXT);
    """)
    con.executemany(
        "INSERT INTO lugar(nombre,busqueda,tipo,lat,lon,poblacion,peso) VALUES (?,?,?,?,?,?,?)",
        filas)
    con.executemany("INSERT INTO lugar_meta VALUES (?,?)", [
        ("version", "1"),
        ("creado", time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())),
        ("origen", os.path.basename(args.geojson)),
        ("filas", str(len(filas))),
    ])
    con.commit()
    con.close()
    os.replace(parte, args.salida)

    por_tipo = {}
    for f in filas:
        por_tipo[f[2]] = por_tipo.get(f[2], 0) + 1
    print("Escrito %s: %d lugares" % (args.salida, len(filas)))
    for t, n in sorted(por_tipo.items(), key=lambda x: -x[1]):
        print("  %-9s %d" % (t, n))
    print('\nAñade al mapa.json:  "places": { "file": "%s" }' % os.path.basename(args.salida))


if __name__ == "__main__":
    main()
