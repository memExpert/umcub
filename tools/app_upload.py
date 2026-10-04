#!/usr/bin/env python3
"""Upload a signed image through the example application (umcub_slot_* API).

  app_upload.py /dev/ttyACM0 build/h755_cm7_app.signed.bin [--image 0]

Protocol of examples/h755_cm7_app: 'u' <image:u8, bit 7 = primary slot> <size:u32le>, then chunks
of 1024 bytes, each acknowledged with 'K'; final 'D' (done) or 'E' (error).
"""
import argparse
import struct
import sys

import serial


def main():
    p = argparse.ArgumentParser()
    p.add_argument("port")
    p.add_argument("file")
    p.add_argument("--image", type=int, default=0)
    p.add_argument("--slot", choices=("default", "primary"), default="default",
                   help="primary: write the primary slot (examples: bit 7 of the image byte)")
    p.add_argument("--baud", type=int, default=115200)
    a = p.parse_args()

    data = open(a.file, "rb").read()
    with serial.Serial(a.port, a.baud, timeout=10) as s:
        s.reset_input_buffer()
        s.write(b"u" + struct.pack("<BI", a.image | (0x80 if a.slot == "primary" else 0), len(data)))

        def expect(ok):
            while True:
                c = s.read(1)
                if not c:
                    sys.exit("timeout")
                if c in (ok, b"E"):
                    if c == b"E":
                        sys.exit("device reported an error" + s.readline().decode(errors="replace").rstrip())
                    return

        expect(b"K")    # slot prepared (trailer sector erased)
        for off in range(0, len(data), 1024):
            s.write(data[off:off + 1024])
            expect(b"K")
            print(f"\r{min(off + 1024, len(data))}/{len(data)}", end="", flush=True)
        expect(b"D")
    print("\nok: image written and marked for test boot - reset the board ('r')")


if __name__ == "__main__":
    main()
