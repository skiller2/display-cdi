# tools

## sim_micro.py — Simulador del micro CDI

Simula desde la PC los mensajes JSON que el micro CDI manda al display por UART. Sirve para probar la pantalla Main (spec `specs/001-main-panel`) sin el panel real.

### Requisitos

- Python 3.8+
- `pyserial`:
  ```
  pip install pyserial
  ```
- Un adaptador USB-serie (3.3 V) conectado a la UART del display:

  | Adaptador | Display |
  |-----------|---------|
  | TX | RX (`UART_RX_GPIO`) |
  | RX | TX (`UART_TX_GPIO`) |
  | GND | GND |

  Los pines están en `main/main.c` (`UART_RX_GPIO` / `UART_TX_GPIO`). La velocidad es 9600 baudios.

### Uso

```
python tools/sim_micro.py COM5              # menú interactivo
python tools/sim_micro.py COM5 --auto 2     # envía el estado cada 2 s, sin menú
python tools/sim_micro.py COM5 -v           # muestra el JSON de cada envío
python tools/sim_micro.py COM5 --baud 115200
```

En Linux/macOS el puerto es `/dev/ttyUSB0`, `/dev/tty.usbserial-…`, etc.

Al conectarse envía el estado completo una vez.

### Comandos del menú

| Comando | Acción |
|---------|--------|
| `h` | Ayuda |
| `s` | Enviar `barstatus` |
| `l` | Enviar `LINEAS` + `ENTRADAS` |
| `u` | Enviar `USR` (usuarios) |
| `a` | Enviar todo |
| `f <CLAVE>` | Alternar un flag de `barstatus` y enviar (`f ALARMA`, `f FALLA`, `f MANUALMODE`…) |
| `b <0-100>` | Batería % y enviar |
| `L <n> <estado>` | Estado de la línea `n` (`L 1 2` = línea 1 en alarma) |
| `E <n> <estado>` | Estado de la entrada `n` |
| `le <n> <0\|1>` | Habilitar o deshabilitar la línea `n` |
| `ie <n> <0\|1>` | Habilitar o deshabilitar la entrada `n` |
| `r` | Estados aleatorios y enviar todo |
| `raw <json>` | Enviar un JSON crudo (`raw {"barstatus":{"ALARMA":true}}`) |
| `bad` | Enviar una trama con CRC roto (el display la debe descartar) |
| `big` | Enviar una trama > 2048 B (el display la debe descartar) |
| `v` | Verbose on/off |
| `q` | Salir |

### Códigos de estado

| Líneas | | Entradas | |
|---|---|---|---|
| 0 | Normal | 0 | Normal |
| 2 | Alarma | 1 | Alarma |
| 3 | Pre-alarma | 4 | Alarma técnica |
| 4 | Alarma técnica | 5 | Falla |
| 6 | Abierto | 8 | Deshabilitado |
| 7 | Cortocircuito | 9 | PDM |
| 8 | Deshabilitado | 10 | ACK (aviso) |
| 19 | Robo | 11 | Reset (aviso) |
| 21–26 | Puerta / presencia / temperatura / baja presión / corte / evacuación | 12 | Aborto |

### Estado inicial

- `barstatus`: todo en normal, batería 100 %, alimentación y red OK, modo automático.
- Líneas 1–10: habilitadas la 1 y la 2.
- Entradas 1–14: deshabilitadas.
- Usuarios: `Operador` / 1111 / nivel 1 y `Tecnico` / 2222 / nivel 2.
- MAC: `D4-8A-FC-C8-46-58`.

### Respuestas automáticas

El simulador muestra cada trama que manda el display (`<< OK …` o `<< CRC MAL …`) y responde como el micro:

| Recibe | Responde |
|--------|----------|
| `{"MAC":…,"cmd":"getStatus"}` | `barstatus`, `LINEAS` + `ENTRADAS` y `USR` |
| `{"cmdTest":{}}` | `barstatus` con `TEST=true` |
| `{"cmdReset":{}}` | Todo a normal y envío completo |
| `{"cmdACK":{}}` | Solo lo muestra |

### Formato de trama

```
[AA 55][len u16 LE][crc16 u16 LE][JSON compacto UTF-8]
```

- `crc16 = esp_crc16_le(0xFFFF, payload, len)`: CRC-16 reflejado (polinomio 0x8408), con complemento a la entrada y a la salida.
- Payload máximo: 2048 bytes. `LINEAS` + `ENTRADAS` por defecto ocupa ~1860 B.

### Verificar el CRC

Tocar **ACK** en el display. Si el simulador muestra `<< OK {"cmdACK":{}}`, el CRC coincide en los dos sentidos.
