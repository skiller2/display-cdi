# Tasks 001 — Pantalla Main

Formato: `- [ ] T### descripción (archivo)`. Cada etapa cierra con su verificación (✔).

## Etapa 1 — Parseo JSON
- [x] T101 Dependencia `espressif/cjson` (IDF v6 ya no trae `json`); LVGL fijado en `~9.3.0`; base64 propio (`main/idf_component.yml`)
- [x] T102 Definir structs y API del modelo (`main/panel_model.h`)
- [x] T103 `panel_model_feed(const char *json, size_t len)`: parsear `barstatus`, `LINEAS`, `ENTRADAS`, `USR` (`main/panel_model.c`)
- [x] T104 Llamar `panel_model_feed` desde `uart_poll_timer_cb` (`main/main.c`)
- [x] T105 Loguear un resumen en `lista_logs`
- ✔ La pantalla Logs muestra el resumen interpretado

## Etapa 2 — Encabezado Main
- [x] T201 Convertir íconos a PNG (check, reset, test, map, config, efaisa) → `main/ui/assets/`
- [x] T202 Cargar las imágenes en EEZ y armar el header de la página Main (`main_header`)
- [x] T203 Reemplazar los botones `enviar_*` por `btn_ack`, `btn_reset`, `btn_test`, `btn_map`, `btn_config`
- [x] T204 Enlazar ACK al envío UART existente (`main/main.c`; se mueve a `panel_ui.c` en la etapa 3)
- ✔ Se ve el encabezado; ACK envía `cmdACK`

## Etapa 3 — Estados generales
- [x] T301 Barra `status_bar` en EEZ: `st_left` (alarma, falla, desconexión, tierra, test, extinción), batería, alimentación, red, módulos `st_mod_*`, `st_autman`
- [x] T302 `refresh_status()` muestra, oculta y cambia los íconos según el modelo (`main/panel_ui.c`)
- [x] T303 Timer de refresco de 200 ms que solo redibuja si cambió `panel_model.version`
- ✔ Los indicadores siguen a `barstatus`

## Etapa 4 — Sin conexión
- [x] T401 Contenedor `bars_list` (columna con scroll) con la barra `fault_conn` (oculta) en EEZ
- [x] T402 Mostrarla si nunca llegó un JSON o si `now - last_rx > 10 s` (`main/panel_ui.c`)
- ✔ Desconectar y reconectar la UART muestra y oculta la barra

## Etapa 5 — Lista de líneas
- [x] T501 Contenedor `bars_list` con scroll en EEZ (hecho en la etapa 4)
- [x] T502 Copiar los textos ES de líneas (`main/texts_es.h`)
- [x] T503 Mapear estado → color, ícono y texto; orden `customOrder` (19–26 agregados al orden; ver nota)
- [x] T504 Crear las barras en `bars_list`; se recrean solo cuando llegan `LINEAS`/`ENTRADAS`

> Nota T503: `customOrder` de la web no incluye 19–26 (en JS el orden queda indefinido). Acá: 19 junto a 2, 21–26 después de 4. Empates por número de línea.
- ✔ Las barras de líneas se ven correctamente

## Etapa 6 — Lista de entradas
- [x] T601 Textos ES de entradas y prefijo M_I1/M_I2
- [x] T602 Render debajo de las líneas; estados 10/11 → aviso (msgbox simple; se reemplaza en la etapa 8)
- ✔ Se ven las entradas

## Etapa 7 — Teclado PIN
- [x] T701 Modal (en C, `lv_layer_top`; no en EEZ): dropdown de usuarios, textarea con password, teclado numérico (`main/panel_keypad.c`)
- [x] T702 Al recibir un JSON con `USR`: decodificar (base64 → JSON) y guardar; usuarios default si nunca llegó (`main/panel_model.c`, hecho en la etapa 1)
- [x] T703 Validar código y `levelAccess >= requerido`; parpadeo rojo si es incorrecto
- [x] T704 Reset/Test (nivel 1) y Config (nivel 2) abren el teclado; Reset/Test envían su comando al validar
- ✔ El teclado abre, valida y rechaza

## Etapa 8 — Acciones y avisos
- [x] T801 Reset y Test pasan por el teclado (nivel 1); Config por el teclado (nivel 2) (hecho en la etapa 7)
- [x] T802 Modal de aviso `panel_ui_notice()` con los textos de éxito y error de ACK, Reset y Test; también usado por las entradas 10/11
- [x] T803 Config → pantalla nueva `Config` (EEZ) con botones WiFi / Sistema / Logs / Main; navegación en C con `eez_flow_set_screen`
- ✔ Las acciones se ejecutan con aviso

## Etapa 9 — Arranque
- [ ] T901 Enviar `{"MAC":"…","cmd":"getStatus"}` por UART al iniciar y al recuperar la conexión
- ✔ La pantalla Main se llena sola al prender
