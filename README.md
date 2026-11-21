# display-cdi

Firmware del display táctil del panel de incendio **CDI**. Muestra en una pantalla de 800×480 lo mismo que la página principal de la web (`cdi/web/src/pages/Main.jsx`): estado general del panel, líneas, entradas y comandos ACK / Reset / Test con PIN.

| | |
|---|---|
| Placa | Waveshare ESP32-S3 + LCD RGB 4.3" 800×480 (ST7701), táctil GT911 |
| SDK | ESP-IDF **v6.1** |
| Gráficos | LVGL **9.3** (fijado en `~9.3.0`) + `esp_lvgl_adapter` |
| Diseño de pantallas | **EEZ Studio** (proyecto `main/ui/Change Screen.eez-project`) |
| Comunicación | UART con la central del panel (tramas JSON con CRC) |

---

## 1. Puesta en marcha

### Requisitos
- ESP-IDF v6.1 instalado (en Windows: `C:\Espressif`, perfil `Microsoft.v6.1.PowerShell_profile.ps1`).
- EEZ Studio.
- Python 3 + `pyserial` para el simulador (`pip install pyserial`).

### Compilar y flashear
Desde una terminal "ESP-IDF" (o PowerShell con el perfil de IDF cargado):

```
idf.py build
idf.py -p COMx flash monitor
```

- La primera compilación descarga los componentes (`managed_components/`).
- Salir del monitor: `Ctrl+]`.
- Si el flash no conecta: entrar en modo descarga (mantener **BOOT**, tocar **RESET**, soltar **BOOT**).

---

## 2. Estructura del proyecto

```
main/
  main.c            arranque: NVS, Wi-Fi, LCD, LVGL, UI; UART (recepción/envío de tramas); navegación
  panel_model.c/.h  modelo en RAM del panel: parsea los JSON de la central (cJSON). Sin LVGL
  panel_ui.c/.h     pantalla Main: barra de estado, barras de líneas/entradas, avisos
  panel_keypad.c/.h teclado PIN (usuarios de USR o por defecto)
  texts_es.h        textos en español copiados de cdi/web/src/dictionary.js
  wifi_config.c/.h  lógica de la pantalla WiFi (scan, conectar, olvidar)
  sys_info.c/.h     pantalla Sistema (heap, PSRAM, flash, CPU) cada 1 s
  display_config.c  timeout de inactividad del backlight (NVS)
  ui/               CÓDIGO GENERADO por EEZ Studio — no editar a mano
components/
  waveshare_rgb_lcd_port.c/.h   driver del LCD RGB y táctil
specs/001-main-panel/           spec, plan y tareas de la pantalla Main
tools/sim_central.py              simulador de la central por UART (ver tools/README.md)
```

### Regla principal: **diseño en EEZ, lógica en C**
- EEZ genera los widgets en `main/ui/` y los expone como `objects.<identificador>` (`ui/screens.h`).
- El C **no crea** pantallas: busca los widgets de EEZ por su identificador y les agrega eventos, textos, colores.
- Excepciones creadas en C: las barras dinámicas de `bars_list`, el teclado PIN y las ventanas de aviso (van sobre `lv_layer_top()`).

---

## 3. Flujo de trabajo con EEZ Studio

1. Abrir `main/ui/Change Screen.eez-project` en EEZ.
2. Editar la página. A todo widget que el C deba tocar, ponerle **Identifier** (aparece como `objects.<identifier>`).
3. **Build** en EEZ (F7) → regenera `main/ui/*.c/.h`.
4. Si EEZ agregó o borró archivos `.c` (imágenes, fuentes): `idf.py reconfigure`.
5. `idf.py build`.

> Si el proyecto EEZ se modificó fuera de EEZ (scripts, otra persona), cerrar EEZ **sin guardar** y volver a abrirlo antes de compilar; si no, EEZ pisa los cambios con su copia en memoria.

### Convenciones de UI
| Elemento | Estilo |
|---|---|
| Fondo de pantalla | `#808080` |
| Títulos | blanco, Montserrat 24, arriba a la izquierda (16, 12) |
| Botones | fondo `#f8f9fa`, borde negro 2 px, radio 8, sin sombra, texto negro |
| Contenedores/tarjetas | fondo `#f5f5f5`, borde negro 1 px, radio 8 |
| Íconos | **símbolos de LVGL** (`LV_SYMBOL_*`) en labels, Montserrat 32, color según estado |
| Textos | **sin tildes ni ñ**: Montserrat de LVGL no las incluye |

Fuentes activas (en `sdkconfig`): Montserrat 12, 14 (default), 16, 20, 24, 32. Para otro tamaño: `idf.py menuconfig` → LVGL → Font usage.

Lista de símbolos: `managed_components/lvgl__lvgl/src/font/lv_symbol_def.h`. En EEZ se escriben como texto unicode (ej. `\uf013` = engranaje).

### Pantallas
| Pantalla | Contenido | Cómo se llega |
|---|---|---|
| Main | barra de estado, encabezado (ACK/Reset/Test/Config), lista `bars_list` | inicio |
| Config | botones WiFi / Sistema / Logs | Config + PIN nivel 2 |
| WiFi | SSID, clave, conectar, scan, lista de redes | Config |
| SysInfo | chip, heap, memoria, flash, CPU | Config |
| Logs | mensajes recibidos por UART | Config |

La navegación se hace en C (`eez_flow_set_screen`). Los botones **Volver** retroceden a la pantalla anterior (historial en `main.c`).

---

## 4. Comunicación con la central (UART)

| | |
|---|---|
| Puerto | `UART_NUM_2`, 9600 8N1 |
| Pines | RX = GPIO44, TX = GPIO43 (`main/main.c`) |
| Trama | `[AA 55][len u16 LE][crc16 u16 LE][JSON]`, máx. 2048 B |
| CRC | `esp_crc16_le(0xFFFF, payload, len)` |

### Mensajes que llegan
| Clave | Uso |
|---|---|
| `barstatus` | estado general → barra de estado |
| `LINEAS` / `ENTRADAS` | listas completas → barras de `bars_list` |
| `USR` | usuarios del PIN (base64 de `[{id, username, levelAccess, code}]`) |
| `MAC` | MAC de la central (se guarda) |

Si no llega ningún JSON en 5 s, aparece la barra "Sin conexion con la central".

### Comandos que se envían
`{"cmdACK":{}}`, `{"cmdReset":{}}`, `{"cmdTest":{}}` (con `uart_send_json()` en `main.c`).

Formatos completos y ejemplos: [`specs/001-main-panel/spec.md`](specs/001-main-panel/spec.md).

### Flujo de datos
```
UART ─► uart_poll_timer_cb (50 ms) ─► frame_rx_feed ─► on_frame_payload
                                                          │
                                                          ▼
                                         panel_model_feed (cJSON → RAM, version++)
                                                          │
                     panel_ui refresh_timer (200 ms) ◄────┘  redibuja solo si cambió
```
Todo corre dentro de timers de LVGL, así que el lock de LVGL ya está tomado: **no** llamar `esp_lv_adapter_lock()` ahí.

---

## 5. Probar sin el panel real

`tools/sim_central.py` simula la central desde la PC con un adaptador USB-serie conectado a GPIO43/44:

```
python tools/sim_central.py COM9
```

Comandos útiles: `f ALARMA`, `b 30`, `le 3 1`, `L 1 2`, `ie 3 1`, `E 3 1`, `r` (azar), `p` (simular corte). Ver [`tools/README.md`](tools/README.md).

Usuarios por defecto del PIN: `1111` (nivel 1: Reset/Test) y `2222` (nivel 2: Config).

---

## 6. Recetas

**Agregar un widget que el C actualiza**
1. En EEZ: crear el widget con Identifier `mi_widget` → Build.
2. En C: `#include "ui/screens.h"` y usar `objects.mi_widget` (después de `ui_init()`).
3. `idf.py build`.

**Agregar un dato nuevo de la central**
1. `panel_model.h`: agregar el campo.
2. `panel_model.c` → `panel_model_feed()`: leerlo del JSON.
3. `panel_ui.c`: mostrarlo en `refresh_timer_cb`.
4. Probar con el simulador (`raw {...}`).

**Agregar una pantalla**
1. EEZ: nueva página, con botón Volver con Identifier → Build → `idf.py reconfigure`.
2. `main.c`: registrar la pantalla y su Volver en `nav_init()`, y navegar con `go_screen(SCREEN_ID_...)`.

---

## 7. Problemas comunes

| Síntoma | Causa / solución |
|---|---|
| `undefined reference` a `ui_image_*` / `ui_font_*` | EEZ agregó o borró archivos → `idf.py reconfigure` |
| `objects.xxx` no existe | falta Identifier en EEZ o no se hizo Build en EEZ |
| Cambios del proyecto EEZ desaparecen | EEZ estaba abierto y guardó encima → cerrar sin guardar y reabrir |
| Texto con tildes sale con cuadrados | la fuente no tiene esos caracteres → escribir sin tildes |
| Una lista no hace scroll | falta el flag `SCROLLABLE` (EEZ o `lv_obj_add_flag`) |
| `Could not open COMx` al flashear | cerrar monitores, reconectar USB, modo descarga (BOOT+RESET) |
| El build sube LVGL a otra versión | no quitar `~9.3.0` de `main/idf_component.yml` |
| `ESP_ERR_WIFI_NOT_INIT` | `wifi_init()` debe ir antes de inicializar el LCD (ya está así en `app_main`) |
