# Ramas y PR

Historial de las ramas de trabajo (fusionadas en `main`, salvo la última si su PR sigue abierto).
Se movió aquí desde `CLAUDE.md` para no cargarlo entero en cada sesión. Al abrir una rama nueva, añade su línea
al final; al fusionarse, no se borra.

| Rama | PR | Para qué |
|---|---|---|
| `refactor-libmapa` | #1 | Refactor de libmapa |
| `claude/sharp-goodall-dt7hh5` | #2–#7 | Elevación, descarga de teselas, paquete de datos sin conexión, seguimiento de objetivos, análisis de elevación y demo |
| `claude/fervent-mccarthy-v9rl6d` | #8–#14 | Alertas, anillos y marcaciones, medir superficie, GeoTIFF/MBTiles, búsqueda de lugares, capa base en caliente (rama larga anterior) |
| `claude/apps-ejemplo` | #15, #17 | Contrato de instalación (`probar_instalacion.sh`, `examples/` en bloque, CI); #17 lleva a `main` lo de #16 |
| `claude/etiquetas-arriba` | #16 | Rótulos encima de todo (`LabelLayer`) y rótulo de marcación movible |
| `claude/app-visor-gpx` | #18 | Ejemplo `visor_gpx` |
| `claude/app-editor-rutas` | #19 | Ejemplo `editor_rutas` |
| `claude/app-buscador-lugares` | #20 | Ejemplo `buscador_lugares` |
| `claude/overlays-fijos` | #21 | Capas fijas del paquete ocultas y no seleccionables |
| `claude/app-medidor-campo` | #22 | Ejemplo `medidor_campo` |
| `claude/claude-md-ramas` | #23 | Regla de ramas y registro en `CLAUDE.md` |
| `claude/app-monitor-trafico` | #24 | Ejemplo `monitor_trafico` (tabla de contactos, perdidos, alertas y glosario) |
| `claude/app-vigilancia-zonas` | #25 | Ejemplo `vigilancia_zonas` (zonas con reglas de aviso, historial de eventos, CSV y GeoJSON) |
| `claude/app-analisis-terreno` | #26 | Ejemplo `analisis_terreno` (línea de vista, perfil, zona visible y terreno de demostración) |
| `claude/app-cliente-telemetria` | #27 | `libtelemetria` (biblioteca aparte: decodificador NMEA 0183 y AIS) y los ejemplos `emisor_nmea` y `cliente_telemetria` |
| `claude/docs-quitar-mundo` | #28 | Quita de `docs/MIS_RUTAS.md` la nota pendiente de la capa `mundo` |
| `claude/cliente-tcp-serie` | #29 | `cliente_telemetria` por TCP (cliente, con reconexión) y por puerto serie (opcional); `emisor_nmea --tcp` |
| `claude/sweet-cray-74nz88` | #30 | MBTiles vectoriales (pbf/MVT): `src/vector/` (gzip, decodificador, estilo, renderizador), `vector_tile` |
| `claude/sweet-cray-74nz88` (otra vez, desde `main`) | #31 | Mapa híbrido: el MBTiles vectorial encima de cualquier base, nombres de calles y capas conmutables |
| `claude/stoic-clarke-rn7r7f` | #32 | «¿Qué hay aquí?»: `MapWidget::vectorFeaturesAt`, ventana y tarjeta de la demo y `vector_tile --at` |
| `claude/stoic-clarke-rn7r7f` (otra vez, desde `main`) | #33 | Puntos de interés por tema en los estilos por defecto (cinco grupos apagados, con `pointShape`) |
| `claude/stoic-clarke-rn7r7f` (y otra vez, desde `main`) | #34 | Buscar sitios (POI) por nombre o tipo: `MapWidget::searchPointsOfInterest` y el buscador de la demo |
| `claude/stoic-clarke-rn7r7f` (y otra más, desde `main`) | #35 | Sacar los POI de una zona como entidades: `MapWidget::importPointsOfInterest` y «Sitios de la zona…» en la demo |
| `claude/buscar-calles` | #36 | Búsqueda de calles sin conexión: `MapWidget::searchStreets`, resaltado en la demo |
| `claude/rutas-sin-conexion` | #37 | Rutas en coche sin conexión por la red vial de las teselas: `MapWidget::findRoute`, «Ruta (coche)» en la demo y `vector_tile --route` |
| `claude/app-rutas-tierra` | #38 | Ejemplo `rutas_tierra` (rutas por tierra con caminos de tierra y red de demostración) y `RouteResult::legs` |
| `claude/inspiring-wright-u0smtv` | #41 | Los 27 tipos AIS en `libtelemetria` (aportación del fork `randoloc`), catálogo de tipos en `emisor_nmea` y estaciones fijas en `cliente_telemetria` |
| `ccr-1c6d88f3-of2nuy` | #43 | Respuesta a la propuesta de convergencia del fork extendido: `docs/Respuesta_convergencia.md` |
| `ccr-1c6d88f3-of2nuy` (otra vez, desde `main`) | #44 | Segunda respuesta de la convergencia: `docs/Respuesta_convergencia_2.md` |
| `ccr-1c6d88f3-of2nuy` (y otra vez, desde `main`) | #45 | Propuesta final de la convergencia: `docs/Propuesta_final_convergencia.md` |
| `ccr-1c6d88f3-of2nuy` (y otra más, desde `main`) | #46 | Fase 1 de la convergencia con el fork extendido: `LabelAnchor` y `labelFontSize` por entidad, formulario de objetivo configurable (offset por defecto, `Hanging`, colores por segmento, `targetLabelMoved`), `RangeRings` hasta 100 con énfasis y el ejemplo `laboratorio_rotulos` |
| `claude/sweet-cray-74nz88` (tercera vez, desde `main`) | #47 | Ejemplo `laboratorio_anillos`: mapa de costas sin híbrido, anillos y radiales con escala, centro con un clic y coordenadas del ratón; incluye la fase 1 de la convergencia (escala polar de 300/600 km con énfasis, rótulos de entidades y formulario de objetivos arrastrable) y las fases 2 a 4 (círculos y arcos geodésicos, agrupar por capa y tipo, partes con nombre y color por entidad) con su medición de 5.000 puntos |
| `claude/sweet-cray-74nz88` (cuarta vez, desde `main`) | #48 | Formulario y objetivo por separado: botón configurable para arrastrar el formulario (`setTargetLabelDragButton`), formulario agarrado aparte del objetivo resaltado y deselección desde la app (`clearSelectedTarget`, señales de cambio); en `laboratorio_anillos`, con el clic derecho |
| `claude/preparar-sesion` | #49 | `herramientas/preparar_sesion.sh`: deja un contenedor nuevo listo (Qt 6 por apt, QCustomPlot y, con `--qt5`, Qt 5.14.2 con aqtinstall) |
| `claude/intelligent-ride-jqhh3a` | pendiente | Primera prueba real de `--qt5`: includes que faltaban para Qt 5 (`QStringList`, `QDebug`), aviso de `-include limits` y log de aqt fuera del repo |
