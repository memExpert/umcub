#!/usr/bin/env python3
"""Deterministic pseudo-random payload for the host tests.

  gen_payload.py out.bin SIZE [RESET]   RESET: reset vector of the vector table at
                                        the start (initial SP + RESET | 1)
"""
import random
import struct
import sys

random.seed(7)
data = bytearray(random.getrandbits(8) for _ in range(int(sys.argv[2])))
if len(sys.argv) > 3:
    data[0:8] = struct.pack("<II", 0x24080000, int(sys.argv[3], 0) | 1)
with open(sys.argv[1], "wb") as f:
    f.write(data)
