---
name: libmapa-docs
description: >
  Flujo para DOCUMENTAR y REGISTRAR una funcionalidad nueva en libmapa /
  LibMapaStatic, y dejar el repo correctamente actualizado. Úsala al terminar
  cualquier cambio de código (una herramienta, una capa, un método del API):
  comentarios por función, entrada en la bitácora, README/arquitectura/PDF si
  procede, build + 17 tests, y commit+push con la atribución del proyecto.
  Dispara con: "documenta esto", "actualiza la bitácora", "registra la feature",
  "deja el repo al día", "resume y documenta".
---

# Documentar y registrar una feature en libmapa

Las reglas base están en `CLAUDE.md` (raíz). Esta skill es el procedimiento
repetible para cerrar una feature bien documentada. Sigue los pasos en orden.

## 1. Comentarios por función
Toda función nueva o modificada lleva un comentario `//` en español ENCIMA,
explicando qué hace y, si importa, por qué. Igual que el resto del código. El
header documenta el API público; el `.cpp`, la implementación. No dejes ninguna
función del cambio sin comentar.

## 2. Bitácora — `docs/BITACORA.md`
Añade una sección nueva al final, numerada con el siguiente entero
(`## N. Título`), en el mismo tono que las demás: qué problema había, qué se
decidió y POR QUÉ (y, si se midió algo, el dato). No es documentación de uso.
Termina la sección con una línea de estado (p.ej. "13 tests, Qt 5.15 y 6.4").

## 3. README y arquitectura (solo si aplica)
- Si cambió el API público, las herramientas o el estado: actualiza
  `README.md` (nº de tests, tabla de herramientas, tabla de estado).
- Si cambió la arquitectura o se añadió una capa/módulo: actualiza
  `docs/arquitectura.html` y **regenera el PDF**. En el contenedor:
  ```bash
  CHROME=$(ls /opt/pw-browsers/chromium-*/chrome-linux/chrome | head -1)
  "$CHROME" --headless --no-sandbox --disable-gpu --no-pdf-header-footer \
    --print-to-pdf=docs/LibMapaStatic_Documentacion.pdf docs/arquitectura.html
  ```
  En el PC local (Windows), con el Chrome instalado (ver `CLAUDE.md` § Entorno):
  ```powershell
  & "C:\Program Files\Google\Chrome\Application\chrome.exe" --headless --disable-gpu `
    --no-pdf-header-footer --print-to-pdf="$PWD\docs\LibMapaStatic_Documentacion.pdf" `
    "$PWD\docs\arquitectura.html"
  ```

## 4. Verificación (si tocaste código)
En el PC local (Windows, Qt 6.11.2 MinGW), compila con Qt Creator o en la
carpeta `build/Desktop_Qt_6_11_2_MinGW_64_bit_Release` y lanza `ctest` con
`C:\Qt\6.11.2\mingw_64\bin` y `C:\Qt\Tools\mingw1310_64\bin` en el `PATH`.

Contenedor nuevo sin Qt:
```bash
sudo apt-get update && sudo apt-get install -y qt6-base-dev qt6-positioning-dev
```
QCustomPlot (gitignored) si falta:
```bash
curl -fsSL -o /tmp/qcp.tgz https://www.qcustomplot.com/release/2.1.1/QCustomPlot-source.tar.gz
tar xzf /tmp/qcp.tgz -C /tmp
cp /tmp/qcustomplot-source/qcustomplot.{h,cpp} third_party/qcustomplot/
```
Compila y pasa los tests (sin pantalla):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
cd build && QT_QPA_PLATFORM=offscreen ctest
```
Deben pasar los 17 (14 si no hay QCustomPlot). Cambios solo de documentación (.md/.html/skills) no
necesitan build.

## 5. Commit + push
En la rama `claude/sharp-goodall-dt7hh5` (nunca `main`). Mensaje en español
explicando el porqué, cerrado con la atribución que indica `CLAUDE.md`. Luego `git push -u origin claude/sharp-goodall-dt7hh5` (reintenta
con backoff si hay 503). Nunca pongas un identificador de modelo en el repo.
