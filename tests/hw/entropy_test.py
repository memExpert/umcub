#!/usr/bin/env python3
"""Quality of the ADC entropy source (port/stm32f1/entropy.c, port/stm32f4/entropy.c) on a board.

  entropy_test.py [--family f1|f4] [--serial ST-LINK-ID] [--rounds 256] [--boots 100] [--save DIR]

Builds tests/hw/entropy_sampler_<family>.c (arm-none-eabi-gcc), loads it into SRAM
over SWD with pyOCD (nothing is written to flash; the board is reset into its
bootloader afterwards) and collects the raw ADC conversions the bootloader
uses: one long run (--rounds x 4096 samples) and the first 4096 samples of
--boots fresh starts. Then estimates, NIST SP 800-90B style:
  - min-entropy of the low 2 bits per conversion (most common value, Markov
    estimate per bit, Markov estimate of the most likely 128-conversion
    sequence = one 32-byte block of entropy.c),
  - what SysTick adds (its step between conversions),
  - the first 32-byte block after start-up across boots (repeats).
Estimates assume the conditions of the run (one board, its temperature).
"""
import argparse
import collections
import math
import os
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
N = 4096                    # samples per round (entropy_sampler_f1.c)
SHARED = 0x20001000         # struct shared: magic, ready, rounds, ms, timeouts, v[N], st[N]
MAGIC = 0x454E5452
# family: CPU flags, device define, CMSIS device package, SRAM size
FAMILIES = {
    "f1": (["-mcpu=cortex-m3"], "STM32F103xB", "cmsis_device_f1", "20K"),
    "f4": (["-mcpu=cortex-m4", "-mfloat-abi=soft"], "STM32F411xE", "cmsis_device_f4", "128K"),
}
LD = """MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = %s }
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > RAM
  .text : { *(.text*) *(.rodata*) } > RAM
  .shared 0x20001000 (NOLOAD) : { *(.shared) } > RAM
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
}
"""


def build(tmp, family):
    cpu, device, cmsis, ram = FAMILIES[family]
    ld = os.path.join(tmp, "sampler.ld")
    elf = os.path.join(tmp, "sampler.elf")
    binf = os.path.join(tmp, "sampler.bin")
    with open(ld, "w") as f:
        f.write(LD % ram)
    tp = os.path.join(ROOT, "third_party")
    subprocess.run(["arm-none-eabi-gcc", *cpu, "-mthumb", "-Os", "-ffreestanding", "-nostdlib",
                    f"-D{device}", f"-I{tp}/st/{cmsis}/Include", f"-I{tp}/cmsis_core/Include",
                    "-T", ld, "-Wl,--gc-sections", os.path.join(HERE, f"entropy_sampler_{family}.c"), "-o", elf],
                   check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", "-j", ".vectors", "-j", ".text", elf, binf], check=True)
    return open(binf, "rb").read()


def start(t, code):
    t.reset_and_halt()
    t.write_memory_block8(0x20000000, list(code))
    sp, pc = struct.unpack_from("<II", code, 0)
    t.write_core_register("sp", sp)
    t.write_core_register("pc", pc)
    t.write_core_register("xpsr", 1 << 24)
    t.resume()
    t0 = time.time()
    while t.read32(SHARED) != MAGIC:
        if time.time() - t0 > 3:
            sys.exit("entropy_test: the sampler did not start (no HSE?)")


def rounds(t, k):
    out = []
    for _ in range(k):
        t0 = time.time()
        while t.read32(SHARED + 4) != 1:
            if time.time() - t0 > 3:
                sys.exit("entropy_test: the sampler stopped")
        v = struct.unpack("<%dH" % N, bytes(t.read_memory_block8(SHARED + 20, 2 * N)))
        st = bytes(t.read_memory_block8(SHARED + 20 + 2 * N, N))
        t.write32(SHARED + 4, 0)
        out.append((v, st))
    return out


def mcv(seq):
    n = len(seq)
    p = collections.Counter(seq).most_common(1)[0][1] / n
    pu = min(1.0, p + 2.576 * math.sqrt(p * (1 - p) / (n - 1)))
    return -math.log2(pu)


def markov_bit(bits):
    """SP 800-90B 6.3.3: min-entropy per bit from a first-order Markov model."""
    t = collections.Counter(zip(bits, bits[1:]))
    p1 = sum(bits) / len(bits)

    def tp(a, b):
        tot = t[(a, 0)] + t[(a, 1)]
        return t[(a, b)] / tot if tot else 0.0
    best = {0: 1 - p1, 1: p1}
    for _ in range(127):
        best = {b: max(best[a] * tp(a, b) for a in (0, 1)) for b in (0, 1)}
    return min(-math.log2(max(best.values())) / 128, 1.0)


def markov_block(runs, length=128):
    """Min-entropy of the most likely `length`-symbol sequence (low 2 bits, first-order Markov)."""
    cnt = collections.Counter(x for s in runs for x in s)
    tr = collections.Counter((a, b) for s in runs for a, b in zip(s, s[1:]))
    row = collections.Counter()
    for (a, _), c in tr.items():
        row[a] += c
    n = sum(cnt.values())
    best = {b: cnt[b] / n for b in range(4)}
    for _ in range(length - 1):
        best = {b: max(best[a] * (tr[(a, b)] / row[a] if row[a] else 0) for a in range(4)) for b in range(4)}
    return -math.log2(max(best.values()))


def entropy_c_block(v, at):
    """The 32-byte block port/stm32f1/entropy.c makes from 128 conversions."""
    out = bytearray()
    for i in range(at, at + 128, 4):
        b = 0
        for k in range(4):
            b = (b << 2 | (v[i + k] & 3)) & 0xFF
        out.append(b)
    return bytes(out)


def report(name, data):
    lo = [[x & 3 for x in v] for v, _ in data]
    flat = [x for s in lo for x in s]
    vals = [x for v, _ in data for x in v]
    print(f"{name}: {len(flat)} conversions, ADC {min(vals)}..{max(vals)}")
    print(f"  low 2 bits: MCV {mcv(flat):.2f}, Markov bit0 {markov_bit([x & 1 for x in flat]):.2f} + "
          f"bit1 {markov_bit([x >> 1 for x in flat]):.2f} bit/conversion; same as previous "
          f"{sum(a == b for s in lo for a, b in zip(s, s[1:])) / (len(flat) - len(lo)):.2f}")
    h = markov_block(lo)
    print(f"  32-byte block (128 conversions): {h:.0f} bits min-entropy (Markov, {h / 128:.2f} bit/conversion)")
    steps = [(st[i - 1] - st[i]) & 0xFF for _, st in data for i in range(1, N)]
    common, c = collections.Counter(steps).most_common(1)[0]
    print(f"  SysTick step {common} in {c / len(steps):.1%} of conversions: {mcv(steps):.2f} bit/conversion")
    return h


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--family", choices=sorted(FAMILIES), default="f1", help="MCU family of the board")
    ap.add_argument("--serial", help="unique ID of the debug probe (pyocd list)")
    ap.add_argument("--rounds", type=int, default=256, help="rounds of 4096 conversions in the long run")
    ap.add_argument("--boots", type=int, default=100, help="fresh starts")
    ap.add_argument("--save", help="directory for the raw samples (stream.bin, boots.bin)")
    a = ap.parse_args()
    from pyocd.core.helpers import ConnectHelper
    with tempfile.TemporaryDirectory() as tmp:
        code = build(tmp, a.family)
    with ConnectHelper.session_with_chosen_probe(unique_id=a.serial, target_override="cortex_m",
                                                 connect_mode="attach", options={"frequency": 4000000}) as s:
        t = s.target
        try:
            start(t, code)
            stream = rounds(t, a.rounds)
            boots = []
            for _ in range(a.boots):
                start(t, code)
                boots += rounds(t, 1)
            timeouts = t.read32(SHARED + 16)
        finally:
            t.reset()                       # back to the bootloader in flash
    if a.save:
        os.makedirs(a.save, exist_ok=True)
        for name, data in (("stream.bin", stream), ("boots.bin", boots)):
            with open(os.path.join(a.save, name), "wb") as f:
                for v, st in data:
                    f.write(struct.pack("<%dH" % N, *v) + st)
    if timeouts:
        print(f"warning: {timeouts} ADC conversions timed out")
    report("long run", stream)
    report("first round of each start", boots)
    first = [entropy_c_block(v, 0) for v, _ in boots]
    print(f"first 32-byte block after start-up: {len(set(first))} distinct of {len(first)}; "
          f"SysTick at the first conversion: {sorted(set(st[0] for _, st in boots))}")


if __name__ == "__main__":
    main()
