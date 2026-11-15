# cambio_pantalla

Banco de pruebas del **cambio de pantalla de objetivos** y de lo que se añadió con él. Sirve para
comprobar a ojo, sin tocar la app real, lo que los tests ya comprueban solos.

```
cambio_pantalla [paquete de datos]          (por defecto: <exe>/datos)
cambio_pantalla <paquete> --comprobar       sin ventana; sale con 0 si todo está bien
cambio_pantalla <paquete> --captura=img.png abre, guarda una imagen y sale
```

Se compila como los demás ejemplos, contra libmapa **instalada**
(`cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH=<prefijo>`, o `cambio_pantalla.pro`).

## Qué hay en el mapa

- Dos «informaciones» de objetivos: **A** (naranja, ids 100–109) y **B** (verde azulado, ids
  200–209). Las dos se mueven a la vez en un almacén de la app, se vea la que se vea.
- Una **zona vigilada** roja en el centro. Del objetivo A-0 cuelgan un **sector** y unos
  **anillos**.
- Al este, dos **zonas solapadas** y una **línea** naranja que las cruza.
- Al oeste, una **red** de líneas dibujada como entidades.
- Al norte, tres **FUENTES** con su rótulo debajo.

## Qué probar

**1. Pantallas.** Pulsa «Pantalla B» y luego «Pantalla A».
- Con las tres casillas marcadas: el contador de alarmas **no sube**, las trayectorias vuelven
  **enteras** (incluido lo recorrido mientras no se veían), el sector y los anillos de A-0 siguen ahí.
- Desmarca «Carga silenciosa» y repite: las alarmas de zona se **repiten** en cada vuelta.
- Desmarca «Devolver la historia»: las trayectorias **empiezan de cero**.
- Desmarca «Conservar sectores…»: el sector de A-0 **desaparece** para siempre (el registro lo dice).
- «Resaltar un objetivo» y cambia de pantalla: el resaltado **se quita** solo.

**2. Varias entidades bajo el clic.** Clic derecho donde la línea naranja cruza las dos zonas: sale
**un** menú con un submenú por cada una de las tres. El clic izquierdo escribe la lista en el registro.

**3. Seleccionar sin resaltar.** Clic en una línea de la red: el registro dice `featureSelected`, pero
la línea no se enciende. Marca «Resaltar la red al seleccionarla» y repite: ahora sí.

**4. Caja de los rótulos.** Cambia el fondo y su opacidad (0 = sin caja), el borde y el halo: se ve
en las FUENTES. Con «También anillos y cuadrícula» (y «Mostrar la cuadrícula») se aplica a los
rótulos que no son de una entidad.

## Lo que usa

`beginTargetLoad` / `endTargetLoad`, `clearTargets(true)`, `addTarget(objetivo, historia)`,
`targetHistory`, `MapHit::features`, `MapFeature::highlightWhenSelected`,
`FeatureStyle::labelBackground` / `labelBorderColor` / `labelHaloColor` y `setLabelBoxStyle`.
