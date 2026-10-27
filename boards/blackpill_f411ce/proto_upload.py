#!/usr/bin/env python3
"""Host side of the example board protocol of boards/blackpill_f411ce/umcub_board.c
(variants/proto_user.h): uploads a signed image into the primary slot.

  boards/blackpill_f411ce/proto_upload.py -d /dev/ttyUSB0 app.signed.bin [--reset]

The connection comes from the umcub host library (tools/host/umcub, docs/host.md):
board frames are KIND_USER payloads - "05 0E base64" lines on a serial port, one
packet on UDP / CAN, DATA on a umcub link node.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "host"))
import umcub  # noqa: E402

CHUNK = (512 - 3) // 4 * 3 - 5      # data per 'W' frame in a 512-byte base64 line


def request(dev, frame, timeout=5.0, retries=5):
    for _ in range(retries):
        dev.send(umcub.KIND_USER, frame)
        r = dev.recv(umcub.KIND_USER, timeout)
        if r and len(r) == 6 and r[0] == frame[0]:
            return struct.unpack("<bI", r[1:6])
    raise umcub.NoAnswer("no answer from the bootloader")


def upload(dev, data, image, reset):
    rc, _ = request(dev, b"B" + struct.pack("<BI", image, len(data)), timeout=10.0)
    if rc:
        raise umcub.DeviceError(f"begin refused ({rc})", rc)
    off = 0
    while off < len(data):
        rc, taken = request(dev, b"W" + struct.pack("<I", off) + data[off:off + CHUNK], timeout=10.0)
        if rc:
            raise umcub.DeviceError(f"write at {off} refused ({rc})", rc)
        off = taken
        print(f"\r{off}/{len(data)}", end="", file=sys.stderr, flush=True)
    rc, _ = request(dev, b"E")
    print(file=sys.stderr)
    if rc:
        raise umcub.DeviceError(f"image refused ({rc})", rc)
    print("ok: image written into the primary slot")
    if reset:
        request(dev, b"R")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="serial port, IP address or can:<interface>:<channel>")
    ap.add_argument("file")
    ap.add_argument("--image", type=int, default=0)
    ap.add_argument("--reset", action="store_true", help="reset the device afterwards")
    a = ap.parse_args()
    try:
        with umcub.connect(a.device) as dev:
            upload(dev, open(a.file, "rb").read(), a.image, a.reset)
    except umcub.UmcubError as e:
        sys.exit(f"proto_upload: {e}")


if __name__ == "__main__":
    main()
