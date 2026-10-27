#!/usr/bin/env python3
"""Host side of the example board protocol of boards/blackpill_f411ce/umcub_board.c
(variants/proto_user.h): uploads a signed image into the primary slot.

  boards/blackpill_f411ce/proto_upload.py --port /dev/ttyUSB0 app.signed.bin [--reset]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "host"))
from umcub_bus import add_bus_arguments, open_bus  # noqa: E402

CHUNK = (512 - 3) // 4 * 3 - 5      # data per 'W' frame in a 512-byte base64 line


def request(link, frame, timeout=5.0, retries=5):
    for _ in range(retries):
        link.send(frame)
        r = link.recv(timeout)
        if r and len(r) == 6 and r[0] == frame[0]:
            return struct.unpack("<bI", r[1:6])
    sys.exit("proto_upload: no answer from the bootloader")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_bus_arguments(ap, can=False)
    ap.add_argument("file")
    ap.add_argument("--image", type=int, default=0)
    ap.add_argument("--reset", action="store_true", help="reset the device afterwards")
    a = ap.parse_args()
    data = open(a.file, "rb").read()
    link = open_bus(a, b"\x05\x0e")
    rc, _ = request(link, b"B" + struct.pack("<BI", a.image, len(data)), timeout=10.0)
    if rc:
        sys.exit(f"proto_upload: begin refused ({rc})")
    off = 0
    while off < len(data):
        part = data[off:off + CHUNK]
        rc, taken = request(link, b"W" + struct.pack("<I", off) + part, timeout=10.0)
        if rc:
            sys.exit(f"proto_upload: write at {off} refused ({rc})")
        off = taken
        print(f"\r{off}/{len(data)}", end="", file=sys.stderr, flush=True)
    rc, _ = request(link, b"E")
    print(file=sys.stderr)
    if rc:
        sys.exit(f"proto_upload: image refused ({rc})")
    print("ok: image written into the primary slot")
    if a.reset:
        request(link, b"R")


if __name__ == "__main__":
    main()
