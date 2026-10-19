#!/usr/bin/env python3
"""Host side of the umcub lite upload protocol (bootloaders built without SMP,
UMCUB_CFG_LITE_UPLOAD; frames: src/proto/include/umcub_lite.h).

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
import binascii
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from umcub_bus import add_bus_arguments, open_bus  # noqa: E402

MAGIC = 0xA6
# Data bytes per 'D' frame that fit every build (UMCUB_CFG_SMP_MTU >= 512,
# checked at build time): a line of base64 text in the 512-byte line buffer on
# serial (2 prefix bytes + newline), one 512-byte packet otherwise; 8 bytes of
# each frame are header and CRC.
CHUNK_SERIAL = (512 - 3) // 4 * 3 - 8      # 373
CHUNK_PACKET = 512 - 8                      # 504
ERRORS = {-1: "invalid (EINVAL)", -2: "flash / image error (EIO)", -3: "timeout", -4: "not supported",
          -5: "busy (slot in use or the running image is not confirmed)"}


def frame(kind, payload=b""):
    body = bytes([MAGIC, ord(kind)]) + payload
    return body + struct.pack(">H", binascii.crc_hqx(body, 0))       # CRC-16/XMODEM


def parse_answer(f):
    if len(f) != 9 or f[0] != MAGIC or f[1] != ord("k") or binascii.crc_hqx(f[:7], 0) != struct.unpack(">H", f[7:])[0]:
        return None
    return struct.unpack("<bI", f[2:7])


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
    add_bus_arguments(ap)
    ap.add_argument("--timeout", type=float, default=3.0)
    sub = ap.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("upload")
    u.add_argument("file")
    u.add_argument("--image", type=int, default=0)
    u.add_argument("--slot", choices=("primary", "secondary"), default="primary")
    u.add_argument("--test", action="store_true", help="secondary: mark for a test boot")
    u.add_argument("--permanent", action="store_true", help="secondary: mark as permanent")
    u.add_argument("--reset", action="store_true", help="reset the device afterwards")
    u.add_argument("--chunk", type=int, help=f"data bytes per frame (default {CHUNK_SERIAL} on serial, {CHUNK_PACKET} "
                   "otherwise: fits UMCUB_CFG_SMP_MTU 512; up to 3/4 of the MTU - 8 on serial, MTU - 8 otherwise)")
    c = sub.add_parser("cmd")
    c.add_argument("text")
    c.add_argument("--wait", type=float, default=1.0)
    sub.add_parser("reset")
    a = ap.parse_args()

    link = open_bus(a, b"\x05\x0c")
    if a.cmd == "upload" and not a.chunk:
        a.chunk = CHUNK_SERIAL if a.port else CHUNK_PACKET
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
