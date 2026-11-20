# Plan 001 — Pantalla Main

Ver `spec.md`. Cada etapa termina con un **resultado visible** en el display.

## Arquitectura

```
UART ──► uart_poll_timer_cb ──► panel_model (cJSON) ──► panel_ui (LVGL, objects.* de EEZ)
                                     ▲                         │
                                     └──── panel_cmd (UART) ◄──┘ botones / keypad
```

Archivos nuevos en `main/`:
- `panel_model.c/.h`: parseo JSON y estado en RAM (`panel_status_t`, `line_t[]`, `input_t[]`, `user_t[]`), marca de tiempo del último mensaje y flag `dirty`.
- `panel_ui.c/.h`: bind de EEZ, render de indicadores y barras, timer de refresco.
- `panel_keypad.c/.h`: modal PIN, validación y callback de acción.- `texts_es.h`: textos ES copiados del `dictionary` web.

Diseño: EEZ (`main/ui/Change Screen.eez-project`). Lógica: C. Las listas dinámicas se crean desde C dentro de contenedores de EEZ, igual que `wifi_list`.

Dependencias: `json` (cJSON de ESP-IDF) y `mbedtls` (base64), agregadas a `REQUIRES` en `main/CMakeLists.txt`.

## Etapas

| # | Etapa | Resultado visible |
|---|-------|-------------------|
| 1 | Parseo JSON | La pantalla Logs muestra un resumen por mensaje: `barstatus ALARMA=0 FALLA=1…`, `LINEAS: 10` |
| 2 | Encabezado Main | La pantalla Main tiene logo, título y 5 botones con íconos; ACK funciona |
| 3 | Estados generales | La fila de indicadores cambia de color según `barstatus` |
| 4 | Sin conexión | Al desconectar la UART aparece la barra roja; al reconectar desaparece |
| 5 | Lista de líneas | Barras de líneas con color, ícono, texto y alias, ordenadas |
| 6 | Lista de entradas | Barras de entradas debajo de las líneas, con prefijo de módulo |
| 7 | Teclado PIN | Reset, Test y Config abren el teclado; un código incorrecto hace parpadear el borde |
| 8 | Acciones y avisos | Con PIN válido se ejecuta la acción y aparece el aviso de éxito o error |
| 9 | Arranque | Al prender, la pantalla Main se llena sola (`getStatus`) |

## Riesgos

- **JSON grande:** la RAM interna es escasa; usar el buffer en PSRAM o parsear por partes.
- **Redibujar toda la lista:** costoso; actualizar solo si cambió algo (`dirty`) y reusar objetos.
- **Íconos:** pasar SVG/WEBP a PNG de 32–48 px; vigilar el tamaño de `images.c`.
- **Lock LVGL:** el parseo ya corre dentro de un timer LVGL; no tomar el lock otra vez.
