#!/usr/bin/env python3
"""Host side of the umcub lite upload protocol (bootloaders built without SMP,
UMCUB_CFG_LITE_UPLOAD; frames: transport/include/umcub_lite.h).

  umcub_lite.py --port /dev/ttyUSB0 upload app.signed.bin        into the primary slot (like SMP recovery)
  umcub_lite.py --port /dev/ttyUSB0 upload app.signed.bin --slot secondary --test --reset
  umcub_lite.py --udp 192.168.1.50 upload app.signed.bin
  umcub_lite.py --can --channel can0 upload app.signed.bin       (ISO-TP, umcub default IDs)
  umcub_lite.py --port /dev/ttyUSB0 cmd i                        a text command, prints the answer
  umcub_lite.py --port /dev/ttyUSB0 reset

Serial (UART, USB CDC): frames as lines 05 0C <base64> \\n, other lines (log)
are skipped. UDP (port 1337) and CAN ISO-TP: one frame per packet. Every
frame is answered; lost frames are repeated.
"""
import argparse
import base64
import binascii
import os
import socket
import struct
import sys
import time

MAGIC = 0xA6
ERRORS = {-1: "invalid (EINVAL)", -2: "flash / image error (EIO)", -3: "timeout", -4: "not supported",
          -5: "busy (slot in use or the running image is not confirmed)"}


def frame(kind, payload=b""):
    body = bytes([MAGIC, ord(kind)]) + payload
    return body + struct.pack(">H", binascii.crc_hqx(body, 0))       # CRC-16/XMODEM


def parse_answer(f):
    if len(f) != 9 or f[0] != MAGIC or f[1] != ord("k") or binascii.crc_hqx(f[:7], 0) != struct.unpack(">H", f[7:])[0]:
        return None
    return struct.unpack("<bI", f[2:7])


class Serial:
    def __init__(self, port, baud):
        import serial
        self.s = serial.Serial(port, baud, timeout=0.2)
        self.buf = b""

    def send(self, f):
        self.s.write(b"\x05\x0c" + base64.b64encode(f) + b"\n")

    def recv(self, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.buf += self.s.read(self.s.in_waiting or 1)
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                i = line.find(b"\x05\x0c")
                if i >= 0:
                    try:
                        return base64.b64decode(line[i + 2:].strip(b"\r"))
                    except binascii.Error:
                        pass
        return None

    def text(self, cmd, wait):
        self.s.reset_input_buffer()
        self.s.write(cmd.encode() + b"\r")
        time.sleep(wait)
        return self.s.read(self.s.in_waiting).decode(errors="replace")


class Udp:
    def __init__(self, host, port):
        self.addr = (host, port)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, f):
        self.s.sendto(f, self.addr)

    def recv(self, timeout):
        self.s.settimeout(timeout)
        try:
            return self.s.recv(4096)
        except socket.timeout:
            return None

    def text(self, cmd, wait):
        self.send(cmd.encode())
        r = self.recv(wait)
        return r.decode(errors="replace") if r else ""


class Can:
    def __init__(self, a):
        import can
        import isotp
        self.bus = can.Bus(interface=a.interface, channel=a.channel, bitrate=a.bitrate)
        mode = isotp.AddressingMode.Normal_29bits if a.ext else isotp.AddressingMode.Normal_11bits
        addr = isotp.Address(mode, txid=a.tx_id + a.addr, rxid=a.rx_id + a.addr)
        params = {"tx_padding": 0xCC, "blocking_send": True, "max_frame_size": 4095}
        self.notifier = can.Notifier(self.bus, [])
        self.stack = isotp.NotifierBasedCanStack(self.bus, self.notifier, address=addr, params=params)
        self.stack.start()

    def send(self, f):
        self.stack.send(f, send_timeout=5.0)

    def recv(self, timeout):
        r = self.stack.recv(block=True, timeout=timeout)
        return bytes(r) if r is not None else None

    def text(self, cmd, wait):
        self.send(cmd.encode())
        r = self.recv(wait)
        return r.decode(errors="replace") if r else ""


def request(link, f, timeout, retries=5):
    for _ in range(retries):
        link.send(f)
        r = link.recv(timeout)
        ans = parse_answer(r) if r else None
        if ans:
            return ans
    sys.exit("umcub_lite: no answer from the bootloader")


def upload(link, a):
    data = open(a.file, "rb").read()
    slot = 1 if a.slot == "secondary" else 0
    rc, _ = request(link, frame("B", struct.pack("<BBI", a.image, slot, len(data))), a.timeout + 10)
    if rc:
        sys.exit(f"begin refused: {ERRORS.get(rc, rc)}")
    off, t0 = 0, time.monotonic()
    while off < len(data):
        chunk = data[off:off + a.chunk]
        rc, taken = request(link, frame("D", struct.pack("<I", off) + chunk), a.timeout)
        if rc:
            sys.exit(f"data at {off} refused: {ERRORS.get(rc, rc)}")
        off = taken
        print(f"\r{off}/{len(data)}", end="", file=sys.stderr, flush=True)
    flags = (1 if a.test or a.permanent else 0) | (2 if a.permanent else 0)
    rc, _ = request(link, frame("E", bytes([flags])), a.timeout + 30)     # in-place decryption takes a while
    print(file=sys.stderr)
    if rc:
        sys.exit(f"end refused: {ERRORS.get(rc, rc)} (not a valid image for this device?)")
    print(f"uploaded {len(data)} bytes in {time.monotonic() - t0:.1f} s")
    if a.reset:
        request(link, frame("R"), a.timeout)
        print("reset")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port (UART, USB CDC)")
    g.add_argument("--udp", help="device IP address")
    g.add_argument("--can", action="store_true", help="CAN ISO-TP (python-can)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--udp-port", type=int, default=1337)
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--tx-id", type=lambda s: int(s, 0), default=0x7C0)
    ap.add_argument("--rx-id", type=lambda s: int(s, 0), default=0x7C8)
    ap.add_argument("--addr", type=int, default=0, help="node address (CAN IDs = base + address)")
    ap.add_argument("--ext", action="store_true", help="29-bit CAN identifiers")
    ap.add_argument("--timeout", type=float, default=3.0)
    sub = ap.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("upload")
    u.add_argument("file")
    u.add_argument("--image", type=int, default=0)
    u.add_argument("--slot", choices=("primary", "secondary"), default="primary")
    u.add_argument("--test", action="store_true", help="secondary: mark for a test boot")
    u.add_argument("--permanent", action="store_true", help="secondary: mark as permanent")
    u.add_argument("--reset", action="store_true", help="reset the device afterwards")
    u.add_argument("--chunk", type=int, default=256, help="data bytes per frame (line length of the bootloader)")
    c = sub.add_parser("cmd")
    c.add_argument("text")
    c.add_argument("--wait", type=float, default=1.0)
    sub.add_parser("reset")
    a = ap.parse_args()

    link = Serial(a.port, a.baud) if a.port else Udp(a.udp, a.udp_port) if a.udp else Can(a)
    if a.cmd == "upload":
        upload(link, a)
    elif a.cmd == "cmd":
        sys.stdout.write(link.text(a.text, a.wait))
    else:
        request(link, frame("R"), a.timeout)
        print("reset")
    sys.stdout.flush()
    os._exit(0)     # python-can notifier threads would keep the process alive


if __name__ == "__main__":
    main()
