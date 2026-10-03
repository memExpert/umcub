#!/usr/bin/env python3
"""Deterministic pseudo-random payload for the host tests."""
import random
import sys

random.seed(7)
with open(sys.argv[1], "wb") as f:
    f.write(bytes(random.getrandbits(8) for _ in range(int(sys.argv[2]))))
