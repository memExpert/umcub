#!/usr/bin/env python3
"""Compare two MCUboot images signed from the same input: header, payload and
SHA-256 TLV must match (the ECDSA signature itself is randomized).

  compare_images.py a.signed.bin b.signed.bin
"""
import struct
import sys

TLV_INFO_MAGIC = (0x6907, 0x6908)     # unprotected / protected TLV area
TLV_SHA256 = 0x10


def parse(path):
    d = open(path, "rb").read()
    if struct.unpack_from("<I", d, 0)[0] != 0x96F3B83D:
        sys.exit(f"{path}: not an MCUboot image")
    hdr_size, = struct.unpack_from("<H", d, 8)
    img_size, = struct.unpack_from("<I", d, 12)
    end = hdr_size + img_size
    sha, off = None, end
    while off + 4 <= len(d):
        magic, length = struct.unpack_from("<HH", d, off)
        if magic not in TLV_INFO_MAGIC:
            break
        area_end, off = off + length, off + 4
        while off + 4 <= area_end:
            t, n = struct.unpack_from("<HH", d, off)
            if t == TLV_SHA256:
                sha = d[off + 4:off + 4 + n]
            off += 4 + n
    return d[:end], sha


a, b = parse(sys.argv[1]), parse(sys.argv[2])
if a[0] != b[0] or a[1] is None or a[1] != b[1]:
    sys.exit(f"images differ: header+payload {'same' if a[0] == b[0] else 'DIFFERENT'}, "
             f"sha256 {'same' if a[1] == b[1] else 'DIFFERENT'}")
print("same image (header, payload, sha256)")
