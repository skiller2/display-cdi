# Spec 001 — Pantalla Main (panel CDI)

**Estado:** Borrador
**Origen:** replicar parte de `cdi/web/src/pages/Main.jsx` en el display ESP32-S3 (LVGL + EEZ Studio).

## Objetivo

El display muestra el estado del panel CDI igual que la página Main de la web: estados generales, líneas, entradas y acciones (ACK / Reset / Test / Config) con control de acceso por PIN.

## Alcance

Incluido:
- Estados generales (`barstatus`): ALARMA, FALLA, DESCONEXION, TIERRA, TEST, BATERIA, ALIMENTACION, RED, EXTINCION, MODULO_*, MANUALMODE.
- Lista de líneas (`LINEAS`) y entradas (`ENTRADAS`).
- Barra de falla "sin conexión con el micro".
- Teclado PIN con usuarios de `USR`.
- Acciones ACK, Reset, Test, Config.
- Textos solo en español.

Excluido:
- Íconos-link de cada barra (matafuego, cámara, mapa, manual).
- Mapas, cámaras, multi-idioma.

## Fuente de datos

- Entrada: `uart_poll_timer_cb` (`main/main.c`). Llega el **mismo JSON que se envía a la web** por WebSocket.
- Claves usadas: `barstatus`, `LINEAS`, `ENTRADAS`, `USR`.
- Se ignora `MAC`.
- Salida: comandos JSON por UART (`{"cmdACK":{}}`, `{"cmdReset":{}}`, `{"cmdTest":{}}`, `{"MAC":"…","cmd":"getStatus"}`).

### Formato de mensajes (muestras reales)

Pedido de estados (ESP → micro):
```json
{"MAC":"D4-8A-FC-C8-46-58","cmd":"getStatus"}
```

El micro responde en **JSON separados**: uno lleva `barstatus` y otro `LINEAS` + `ENTRADAS`. Todos traen `MAC`.

`barstatus`:
```json
{"barstatus":{"ALARMA":false,"FALLA":true,"DESCONEXION":false,"TIERRA":false,"TEST":false,
 "FLAGFBAT":false,"BATERIA":100,"ALIMENTACION":true,"RED":true,"EXTINCION":false,"SIREN":false,
 "FIRMWARE":1000,"MODULO_I1":-1,"MODULO_I2":-1,"MODULO_O1":-1,"MODULO_O2":-1,"MANUALMODE":false},
 "MAC":"D4-8A-FC-C8-46-58"}
```
- `BATERIA`: porcentaje (0–100).
- `FLAGFBAT`: falla de batería.
- `MODULO_*`: -1 = no instalado.
- `MANUALMODE`: false = automático.

`LINEAS` / `ENTRADAS` (array completo en cada mensaje):
```json
{"MAC":"…","LINEAS":[{"number":1,"enable":1,"type":0,"status":0,"alias":"LINEA 1","cam":{"url":"…"}}],
 "ENTRADAS":[{"number":2,"enable":0,"status":0,"alias":"ENTRADA 2","cam":{"url":""}}]}
```
- Campos usados: `number`, `enable`, `status`, `alias`.
- `type` y `cam` se ignoran.
- Cada mensaje reemplaza la lista completa.
- La MAC del pedido: usar la que trae el micro en `MAC` (se guarda la última recibida). Hasta recibir la primera, usar la MAC del ESP con formato `XX-XX-XX-XX-XX-XX`.

## Requisitos funcionales

| ID | Requisito |
|----|-----------|
| FR-1 | Parsear cada JSON recibido por UART y actualizar un modelo en RAM. |
| FR-2 | Mostrar indicadores de `barstatus`; actualizar al recibir cambios. |
| FR-3 | Mostrar barra roja "sin conexión con el micro" si no llega JSON en N s (N configurable, default 10). |
| FR-4 | Mostrar una barra por línea con `enable == 1`: color, ícono, `Línea N <estado>`, alias. |
| FR-5 | Mostrar una barra por entrada con `enable == 1`, con prefijo `M_I1` (1–7) / `M_I2` (8–14). |
| FR-6 | Ordenar barras como la web (ver tabla de estados). |
| FR-7 | ACK se envía directo. Reset y Test requieren nivel ≥ 1; Config requiere nivel ≥ 2. |
| FR-8 | Teclado PIN: elegir usuario, código enmascarado, 0–9, borrar, aceptar. Error: borde rojo 250 ms y se borra el código. |
| FR-9 | Usuarios: un JSON recibido por UART con clave `USR` (`{"USR":"<base64>"}`, el mismo formato que devuelve `/api/config/usuarios`). `USR` decodificado = JSON `[{id, username, levelAccess, code}]`. Se guarda la última lista válida. Si nunca llegó `USR`: usar default (1111/nivel 1, 2222/nivel 2). El transporte no importa: solo se procesa el JSON. |
| FR-10 | Tras cada acción mostrar aviso de éxito o error con el texto de la web. |
| FR-11 | Al arrancar enviar `getStatus` y el pedido de usuarios. |

## Tabla de estados

Líneas (orden de prioridad: 2, 3, 4, 6, 7, 8, 0):

| status | Significado | Color | Ícono |
|--------|-------------|-------|-------|
| 0 | Normal | verde | check |
| 2 | Alarma | rojo | campana |
| 3 | Pre-alarma | naranja | campana |
| 4 | Alarma técnica | rojo | campana |
| 6 | Abierto | amarillo | falla |
| 7 | Cortocircuito | amarillo | falla |
| 8 | Deshabilitado | amarillo | desconexión |
| 19 | Robo | rojo | robo |
| 21–26 | Puerta / presencia / temperatura / baja presión / corte de suministro / evacuación | amarillo | campana |

Entradas (orden: 12, 9, 1, 4, 5, 0):

| status | Significado | Color | Ícono |
|--------|-------------|-------|-------|
| 0 | Normal | verde | check |
| 1 | Alarma | rojo | campana |
| 4 | Alarma técnica | rojo | campana |
| 5 | Falla | amarillo | falla |
| 8 | Deshabilitado | amarillo | falla |
| 9 | PDM | rojo | campana |
| 10 | ACK → aviso, no barra | — | — |
| 11 | Reset → aviso, no barra | — | — |
| 12 | Aborto | rojo | campana |

Textos exactos: copiar de `cdi/web/src/dictionary` (`main.bar.*`, `main.serverConn`, `modals.alert.*`), solo ES.

## Criterios de aceptación

- Con el micro conectado, la pantalla Main refleja el mismo estado que la web en < 1 s.
- Al desconectar la UART aparece la barra de falla en ≤ N s; al reconectar desaparece.
- Una línea en alarma aparece primero y en rojo.
- Reset y Test no se ejecutan sin un PIN válido de nivel suficiente.

## Preguntas abiertas

- [x] Muestra real de JSON del micro (ver "Formato de mensajes").
- [x] MAC del pedido `getStatus`: la del micro (la última recibida en `MAC`).
- [x] Usuarios: llegan en un JSON con `USR` (ver FR-9).
- [ ] Destino de Config: ¿Wifi + SysInfo o pantalla nueva?
