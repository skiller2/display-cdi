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
- [ ] T201 Convertir íconos a PNG (check, reset, test, map, config, efaisa)
- [ ] T202 Cargar las imágenes en EEZ y armar el header de la página Main
- [ ] T203 Reemplazar los botones `enviar_*` por `btn_ack`, `btn_reset`, `btn_test`, `btn_map`, `btn_config`
- [ ] T204 Enlazar ACK al envío UART existente (`main/panel_ui.c`)
- ✔ Se ve el encabezado; ACK envía `cmdACK`

## Etapa 3 — Estados generales
- [ ] T301 Fila de indicadores en EEZ (`st_alarma`, `st_falla`, …)
- [ ] T302 `panel_ui_refresh_status()` colorea los indicadores según el modelo
- [ ] T303 Timer de refresco de 200 ms que solo redibuja si `dirty`
- ✔ Los indicadores siguen a `barstatus`

## Etapa 4 — Sin conexión
- [ ] T401 Barra `fault_conn` (oculta) en EEZ
- [ ] T402 Mostrarla si `now - last_rx > N`
- ✔ Desconectar y reconectar la UART muestra y oculta la barra

## Etapa 5 — Lista de líneas
- [ ] T501 Contenedor `bars_list` con scroll en EEZ
- [ ] T502 Copiar los textos ES de líneas (`main/texts_es.h`)
- [ ] T503 Mapear estado → color, ícono y texto; orden `customOrder`
- [ ] T504 Crear y reusar las barras en `bars_list`
- ✔ Las barras de líneas se ven correctamente

## Etapa 6 — Lista de entradas
- [ ] T601 Textos ES de entradas y prefijo M_I1/M_I2
- [ ] T602 Render debajo de las líneas; estados 10/11 → aviso
- ✔ Se ven las entradas

## Etapa 7 — Teclado PIN
- [ ] T701 Modal en EEZ: dropdown de usuarios, textarea con password, teclado numérico
- [ ] T702 Al recibir un JSON con `USR`: decodificar (base64 → JSON) y guardar; usuarios default si nunca llegó (`main/panel_model.c`)
- [ ] T703 Validar código y `levelAccess >= requerido`; parpadeo rojo si es incorrecto
- ✔ El teclado abre, valida y rechaza

## Etapa 8 — Acciones y avisos
- [ ] T801 Reset y Test pasan por el teclado (nivel 1); Config por el teclado (nivel 2)
- [ ] T802 Modal de aviso con los textos de éxito y error
- [ ] T803 Config → navegar a Wifi / SysInfo
- ✔ Las acciones se ejecutan con aviso

## Etapa 9 — Arranque
- [ ] T901 Enviar `{"MAC":"…","cmd":"getStatus"}` por UART al iniciar y al recuperar la conexión
- ✔ La pantalla Main se llena sola al prender
