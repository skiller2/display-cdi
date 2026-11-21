#!/usr/bin/env python3
"""
Simulador de la central CDI por UART (spec 001).

Trama (little-endian):  [AA 55][len u16][crc16 u16][JSON]
CRC = esp_crc16_le(0xFFFF, payload)  -> CRC-16 reflejado poly 0x8408,
      con ~ a la entrada y a la salida (igual que la ROM del ESP32).

Uso:
    pip install pyserial
    python tools/sim_central.py COM5            # menú interactivo
    python tools/sim_central.py COM5 --auto 2   # manda estado cada 2 s

Lee también lo que manda el display (cmdACK, cmdReset, cmdTest, getStatus)
y responde a getStatus con el estado actual.

Keepalive: el display manda {"cmdGetData":"SSI_TIEMPO"} cada 1 s; el simulador
responde {"TIEMPO":...,"MAC":...,"TIMESTAMP":...}. Con 'p' deja de responder
(el display muestra "Sin conexion con la central" a los 5 s). Con 'o <seg>'
desfasa el TIMESTAMP para probar la falla de hora del display (> 5 s).
"""
import argparse
import base64
import json
import random
import struct
import sys
import threading
import time
from datetime import datetime, timedelta, timezone

try:
    import serial
except ImportError:
    sys.exit("Falta pyserial:  pip install pyserial")

MAGIC = 0x55AA
MAX_PAYLOAD = 2048
MAC = "D4-8A-FC-C8-46-58"


# ---------------------------------------------------------------- trama ---
def crc16_le(data: bytes, crc: int = 0xFFFF) -> int:
    crc = ~crc & 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return ~crc & 0xFFFF


def build_frame(obj) -> bytes:
    payload = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} > {MAX_PAYLOAD} bytes")
    return struct.pack("<HHH", MAGIC, len(payload), crc16_le(payload)) + payload


class FrameReader:
    """Decodifica tramas entrantes byte a byte."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf += data
        out = []
        while True:
            i = self.buf.find(b"\xAA\x55")
            if i < 0:
                self.buf = self.buf[-1:]
                break
            if i:
                del self.buf[:i]
            if len(self.buf) < 6:
                break
            _, ln, crc = struct.unpack_from("<HHH", self.buf)
            if ln == 0 or ln > MAX_PAYLOAD:
                del self.buf[:2]
                continue
            if len(self.buf) < 6 + ln:
                break
            payload = bytes(self.buf[6:6 + ln])
            del self.buf[:6 + ln]
            ok = crc16_le(payload) == crc
            out.append((payload, ok))
        return out


# --------------------------------------------------------------- estado ---
state = {
    "barstatus": {
        "ALARMA": False, "FALLA": False, "DESCONEXION": False, "TIERRA": False,
        "TEST": False, "FLAGFBAT": False, "BATERIA": 100, "ALIMENTACION": True,
        "RED": True, "EXTINCION": False, "SIREN": False, "FIRMWARE": 1000,
        "MODULO_I1": -1, "MODULO_I2": -1, "MODULO_O1": -1, "MODULO_O2": -1,
        "MANUALMODE": False,
    },
    "LINEAS": [
        {"number": n, "enable": 1 if n <= 2 else 0, "type": 0, "status": 0,
         "alias": f"LINEA {n}", "cam": {"url": ""}}
        for n in range(1, 11)
    ],
    "ENTRADAS": [
        {"number": n, "enable": 0, "status": 0, "alias": f"ENTRADA {n}", "cam": {"url": ""}}
        for n in range(1, 15)
    ],
    "USERS": [
        {"id": 1, "username": "Operador", "levelAccess": 1, "code": "1111"},
        {"id": 2, "username": "Tecnico", "levelAccess": 2, "code": "2222"},
    ],
}
LINE_STATES = [0, 2, 3, 4, 6, 7, 8, 19, 21, 22, 23, 24, 25, 26]
INPUT_STATES = [0, 1, 4, 5, 8, 9, 12]


def msg_barstatus():
    return {"barstatus": state["barstatus"], "MAC": MAC}


def msg_bars():
    return {"MAC": MAC, "LINEAS": state["LINEAS"], "ENTRADAS": state["ENTRADAS"]}


def msg_time(offset: int = 0):
    """Respuesta a {"cmdGetData":"SSI_TIEMPO"} (igual que createJsonTime de la central)."""
    now = int(time.time()) + offset
    # Formato real de la central: ISO 8601 con zona fija -03:00; TIMESTAMP en UTC
    tz = timezone(timedelta(hours=-3))
    return {"TIEMPO": datetime.fromtimestamp(now, tz).isoformat(timespec="seconds"),
            "MAC": MAC, "TIMESTAMP": now}


def msg_users():
    raw = json.dumps(state["USERS"], separators=(",", ":")).encode()
    return {"MAC": MAC, "USR": base64.b64encode(raw).decode()}


# ---------------------------------------------------------------- I/O -----
class Link:
    def __init__(self, port, baud, verbose):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.lock = threading.Lock()
        self.verbose = verbose
        self.paused = False          # 'p': simula central desconectada (no responde la hora)
        self.time_offset = 0         # 'o': desfase en segundos del TIMESTAMP
        self.time_reqs = 0           # pedidos de hora (keepalive) recibidos
        self.last_time_req = 0.0
        self.reader = FrameReader()
        threading.Thread(target=self._rx_loop, daemon=True).start()

    def send(self, obj, label="", keepalive=False):
        """Envía un JSON y lo muestra. Con 'v' (verbose) imprime también el contenido.
        keepalive=True: no imprime nada salvo en verbose (son 1 por segundo)."""
        frame = build_frame(obj)
        with self.lock:
            self.ser.write(frame)
        if keepalive and not self.verbose:
            return
        print(f">> {label or 'json'} ({len(frame) - 6} B)")
        if self.verbose:
            print("   ", frame[6:].decode())

    def send_all(self):
        self.send(msg_barstatus(), "barstatus")
        time.sleep(0.15)
        self.send(msg_bars(), "LINEAS+ENTRADAS")

    def _rx_loop(self):
        while True:
            try:
                data = self.ser.read(256)
            except serial.SerialException as e:
                print("!! puerto:", e)
                return
            if not data:
                continue
            for payload, ok in self.reader.feed(data):
                txt = payload.decode(errors="replace")
                obj = None
                if ok:
                    try:
                        obj = json.loads(txt)
                    except ValueError:
                        pass
                # Keepalive del display (cada 1 s): se responde; el log solo en verbose ('v')
                if isinstance(obj, dict) and obj.get("cmdGetData") == "SSI_TIEMPO":
                    self.time_reqs += 1
                    self.last_time_req = time.time()
                    if self.verbose:
                        print("\n<< OK", txt)
                    if self.paused:
                        if self.verbose:
                            print("-- keepalive: pedido de hora recibido, NO se responde (pausado)")
                    else:
                        self.send(msg_time(self.time_offset), "TIEMPO (keepalive)", keepalive=True)
                    if self.verbose:
                        print("> ", end="", flush=True)
                    continue
                print(f"\n<< {'OK ' if ok else 'CRC MAL '}{txt}")
                if not isinstance(obj, dict):
                    continue
                if obj.get("cmd") == "getStatus":
                    print("   -> respondiendo getStatus")
                    self.send_all()
                    self.send(msg_users(), "USR")
                elif "cmdTest" in obj:
                    state["barstatus"]["TEST"] = True
                    self.send(msg_barstatus(), "barstatus (TEST=1)")
                elif "cmdReset" in obj or "cmdACK" in obj:
                    if "cmdReset" in obj:
                        for b in state["barstatus"]:
                            if isinstance(state["barstatus"][b], bool) and b not in ("ALIMENTACION", "RED"):
                                state["barstatus"][b] = False
                        for l in state["LINEAS"] + state["ENTRADAS"]:
                            l["status"] = 0
                        self.send_all()
                print("> ", end="", flush=True)


# --------------------------------------------------------------- menú -----
HELP = """
ENVIAR
  s              barstatus
  l              LINEAS + ENTRADAS
  u              USR (usuarios)
  a              todo

ESTADO GENERAL (barra superior)
  f <CLAVE>      prender/apagar un indicador y enviar
                 claves: ALARMA FALLA DESCONEXION TIERRA TEST EXTINCION
                         ALIMENTACION RED MANUALMODE SIREN FLAGFBAT
  b <0-100>      batería %                          ej: b 30
  m <MOD> <v>    estado de módulo y enviar         ej: m O2 0
                 MOD: I1 I2 O1 O2   v: 1 normal, 0 falla (barra), -1 no instalado

LÍNEAS / ENTRADAS (lista de barras)
  le <n> <0|1>   habilitar/ocultar línea n          ej: le 3 1
  ie <n> <0|1>   habilitar/ocultar entrada n        ej: ie 3 1
  L <n> <est>    estado de línea n                  ej: L 1 2   (alarma)
  E <n> <est>    estado de entrada n                ej: E 3 1   (alarma)
                 (solo se ven las habilitadas: primero 'le'/'ie')

  Estados línea:   0 normal  2 alarma  3 prealarma  4 alarma técnica
                   6 abierta  7 corto  8 excluida  19 robo
                   21 puerta  22 presencia  23 temperatura  24 baja presión
                   25 corte suministro  26 evacuación
  Estados entrada: 0 normal  1 alarma  4 alarma técnica  5 falla  8 excluida
                   9 PDM  12 aborto  10 aceptación (aviso)  11 reset (aviso)

PRUEBAS
  r              todo al azar y enviar
  p              dejar de responder el keepalive -> a los 5 s "Sin conexion con la central"
  t              enviar TIEMPO ahora (sin que lo pida el display)
  o <seg>        desfasar el TIMESTAMP de TIEMPO (> 5 s -> falla de hora en el display)  ej: o 10
  k              estado del keepalive (pedidos de hora recibidos)
  bad            trama con CRC roto (el display la descarta)
  big            trama > 2048 B (el display la descarta)
  raw <json>     enviar un JSON cualquiera

OTROS
  v              verbose: JSON de cada envío y log del keepalive (1 por segundo)
  h              esta ayuda
  q              salir

Lo que manda el display se muestra como  << OK {...}
"""


def find_bar(lst, n):
    for b in lst:
        if b["number"] == n:
            return b
    raise KeyError(f"no existe {n}")


def handle(link: Link, line: str) -> bool:
    p = line.split()
    if not p:
        return True
    c = p[0]
    bs = state["barstatus"]
    try:
        if c == "q":
            return False
        elif c in ("h", "?"):
            print(HELP)
        elif c == "s":
            link.send(msg_barstatus(), "barstatus")
        elif c == "l":
            link.send(msg_bars(), "LINEAS+ENTRADAS")
        elif c == "u":
            link.send(msg_users(), "USR")
        elif c == "a":
            link.send_all()
            link.send(msg_users(), "USR")
        elif c == "f":
            k = p[1].upper()
            if not isinstance(bs.get(k), bool):
                print("flags:", [k for k, v in bs.items() if isinstance(v, bool)])
                return True
            bs[k] = not bs[k]
            print(f"   {k} = {bs[k]}")
            link.send(msg_barstatus(), "barstatus")
        elif c == "b":
            bs["BATERIA"] = max(0, min(100, int(p[1])))
            link.send(msg_barstatus(), "barstatus")
        elif c == "m":
            k = "MODULO_" + p[1].upper()
            if k not in bs:
                print("módulos: I1 I2 O1 O2")
                return True
            v = int(p[2])
            if v not in (-1, 0, 1):
                print("valor: 1 normal, 0 falla, -1 no instalado")
                return True
            bs[k] = v
            print(f"   {k} = {v}")
            link.send(msg_barstatus(), "barstatus")
        elif c in ("L", "E"):
            lst = state["LINEAS"] if c == "L" else state["ENTRADAS"]
            find_bar(lst, int(p[1]))["status"] = int(p[2])
            link.send(msg_bars(), "LINEAS+ENTRADAS")
        elif c in ("le", "ie"):
            lst = state["LINEAS"] if c == "le" else state["ENTRADAS"]
            find_bar(lst, int(p[1]))["enable"] = int(p[2])
            link.send(msg_bars(), "LINEAS+ENTRADAS")
        elif c == "r":
            for l in state["LINEAS"]:
                l["enable"] = random.choice([0, 1, 1])
                l["status"] = random.choice(LINE_STATES)
            for e in state["ENTRADAS"]:
                e["enable"] = random.choice([0, 1])
                e["status"] = random.choice(INPUT_STATES)
            for k, v in bs.items():
                if isinstance(v, bool):
                    bs[k] = random.random() < 0.3
            bs["BATERIA"] = random.randint(0, 100)
            link.send_all()
        elif c == "raw":
            link.send(json.loads(line[4:]), "raw")
        elif c == "bad":
            f = bytearray(build_frame(msg_barstatus()))
            f[4] ^= 0xFF
            link.ser.write(f)
            print(">> trama con CRC roto")
        elif c == "big":
            payload = json.dumps({"MAC": MAC, "pad": "x" * 2100}).encode()
            link.ser.write(struct.pack("<HHH", MAGIC, len(payload), crc16_le(payload)) + payload)
            print(f">> trama grande ({len(payload)} B)")
        elif c == "v":
            link.verbose = not link.verbose
            print("   verbose", link.verbose)
        elif c == "p":
            link.paused = not link.paused
            print("   keepalive", "PAUSADO (simula central muda)" if link.paused else "activo")
        elif c == "t":
            link.send(msg_time(link.time_offset), "TIEMPO")
        elif c == "o":
            link.time_offset = int(p[1])
            print(f"   TIMESTAMP desfasado {link.time_offset} s")
        elif c == "k":
            ago = f"hace {time.time() - link.last_time_req:.1f} s" if link.time_reqs else "nunca"
            print(f"   pedidos de hora: {link.time_reqs} (último {ago}), "
                  f"{'PAUSADO' if link.paused else 'respondiendo'}, desfase {link.time_offset} s")
        else:
            print("? (h = ayuda)")
    except (IndexError, ValueError, KeyError) as e:
        print("error:", e)
    return True


def main():
    ap = argparse.ArgumentParser(description="Simulador de la central CDI por UART")
    ap.add_argument("port", help="puerto serie, ej: COM5 o /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--auto", type=float, metavar="SEG",
                    help="enviar estado cada SEG segundos (sin menú)")
    ap.add_argument("-v", "--verbose", action="store_true", help="mostrar JSON enviado")
    ap.add_argument("--keepalive", type=float, default=0, metavar="SEG",
                    help="en modo menú, reenviar barstatus cada SEG s en silencio (0 = no, default). "
                         "No hace falta: el display ya pide la hora cada 1 s y eso mantiene el enlace")
    a = ap.parse_args()

    link = Link(a.port, a.baud, a.verbose)
    print(f"Conectado a {a.port} @ {a.baud}. 'h' = ayuda")

    if a.auto:
        try:
            while True:
                link.send_all()
                time.sleep(a.auto)
        except KeyboardInterrupt:
            return

    link.send_all()
    if a.keepalive > 0:
        def keepalive():
            while True:
                time.sleep(a.keepalive)
                if not link.paused:
                    link.send(msg_barstatus(), "barstatus (keepalive)", keepalive=True)
        threading.Thread(target=keepalive, daemon=True).start()
    try:
        while True:
            if not handle(link, input("> ")):
                break
    except (KeyboardInterrupt, EOFError):
        pass


if __name__ == "__main__":
    main()
