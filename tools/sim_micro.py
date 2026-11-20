#!/usr/bin/env python3
"""
Simulador del micro CDI por UART (spec 001).

Trama (little-endian):  [AA 55][len u16][crc16 u16][JSON]
CRC = esp_crc16_le(0xFFFF, payload)  -> CRC-16 reflejado poly 0x8408,
      con ~ a la entrada y a la salida (igual que la ROM del ESP32).

Uso:
    pip install pyserial
    python tools/sim_micro.py COM5            # menú interactivo
    python tools/sim_micro.py COM5 --auto 2   # manda estado cada 2 s

Lee también lo que manda el display (cmdACK, cmdReset, cmdTest, getStatus)
y responde a getStatus con el estado actual.
"""
import argparse
import base64
import json
import random
import struct
import sys
import threading
import time

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


def msg_users():
    raw = json.dumps(state["USERS"], separators=(",", ":")).encode()
    return {"MAC": MAC, "USR": base64.b64encode(raw).decode()}


# ---------------------------------------------------------------- I/O -----
class Link:
    def __init__(self, port, baud, verbose):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.lock = threading.Lock()
        self.verbose = verbose
        self.reader = FrameReader()
        threading.Thread(target=self._rx_loop, daemon=True).start()

    def send(self, obj, label=""):
        frame = build_frame(obj)
        with self.lock:
            self.ser.write(frame)
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
                print(f"\n<< {'OK ' if ok else 'CRC MAL '}{txt}")
                if not ok:
                    continue
                try:
                    obj = json.loads(txt)
                except ValueError:
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
Comandos:
  s            enviar barstatus
  l            enviar LINEAS + ENTRADAS
  u            enviar USR (usuarios)
  a            enviar todo
  f <CLAVE>    alternar flag de barstatus (ej: f ALARMA, f FALLA, f MANUALMODE)
  b <0-100>    batería %
  L <n> <st>   estado de línea n (ej: L 1 2 = alarma)
  E <n> <st>   estado de entrada n
  le <n> <0|1> habilitar línea n     ie <n> <0|1> habilitar entrada n
  r            estados aleatorios + enviar todo
  raw <json>   enviar JSON crudo
  bad          enviar trama con CRC roto
  big          enviar JSON > 2048 B (debe descartarse)
  v            verbose on/off
  q            salir
Estados línea:   0 normal 2 alarma 3 prealarma 4 téc 6 abierto 7 corto 8 deshab 19 robo 21-26
Estados entrada: 0 normal 1 alarma 4 téc 5 falla 8 deshab 9 PDM 10 ack 11 reset 12 aborto
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
        else:
            print("? (h = ayuda)")
    except (IndexError, ValueError, KeyError) as e:
        print("error:", e)
    return True


def main():
    ap = argparse.ArgumentParser(description="Simulador del micro CDI por UART")
    ap.add_argument("port", help="puerto serie, ej: COM5 o /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--auto", type=float, metavar="SEG",
                    help="enviar estado cada SEG segundos (sin menú)")
    ap.add_argument("-v", "--verbose", action="store_true", help="mostrar JSON enviado")
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
    try:
        while True:
            if not handle(link, input("> ")):
                break
    except (KeyboardInterrupt, EOFError):
        pass


if __name__ == "__main__":
    main()
