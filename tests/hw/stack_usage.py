#!/usr/bin/env python3
"""Stack high-water mark of the bootloader on real hardware (SWD through probe.py).

  stack_usage.py --elf build/bluepill/umcub_bluepill_f103c8_cm3.elf paint     fill the stack with a pattern
  ... run a scenario (boot, upgrade, SMP upload in recovery) ...
  stack_usage.py --elf build/bluepill/umcub_bluepill_f103c8_cm3.elf measure   deepest use since paint

Probe options (--probe stm32cube|pyocd, --target, ...): probe.py.

`paint` connects under reset (the core is held at its reset vector), writes the
pattern from the end of .bss up to the top of the stack and lets the core start.
`measure` reads the area back while the code runs and reports how much of it
was overwritten. After the jump the application owns the RAM: measure boot
paths with an application that does not touch it (`loop-app` writes a signed
`b .` image for that), or measure while the bootloader stays in recovery.
"""
import argparse
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools", "hw"))
import probe  # noqa: E402

PATTERN = 0x5AA5C33C


def symbols(elf):
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms["_ebss"], syms["_estack"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", help="bootloader ELF (for _ebss / _estack)")
    ap.add_argument("--margin", type=int, default=64, help="bytes left unpainted above .bss")
    probe.add_arguments(ap)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("paint")
    sub.add_parser("measure")
    la = sub.add_parser("loop-app", help="raw .bin of an application that only loops (vector table + b .)")
    la.add_argument("--vtor", type=lambda s: int(s, 0), required=True, help="its vector table address")
    la.add_argument("--sp", type=lambda s: int(s, 0), required=True, help="initial stack pointer")
    la.add_argument("-o", "--output", required=True)
    a = ap.parse_args()

    if a.cmd == "loop-app":
        img = struct.pack("<II", a.sp, a.vtor + 8 + 1) + b"\xfe\xe7" + bytes(2)
        open(a.output, "wb").write(img + bytes(256 - len(img)))
        return

    lo, top = symbols(a.elf)
    lo = (lo + a.margin + 3) & ~3
    size = top - lo
    try:
        p = probe.open_probe(a, "under-reset" if a.cmd == "paint" else "attach")
        try:
            if a.cmd == "paint":
                p.write_mem(lo, struct.pack("<I", PATTERN) * (size // 4))
            else:
                data = p.read_mem(lo, size)
        finally:
            p.close()
    except probe.ProbeError as e:
        sys.exit(f"stack_usage: {e}")
    if a.cmd == "paint":
        print(f"painted 0x{lo:08x}..0x{top:08x} ({size} bytes)")
        return

    words = struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4])
    untouched = next((i for i, w in enumerate(words) if w != PATTERN), len(words))
    used = size - untouched * 4
    print(f"stack used: {used} of {size} bytes ({100 * used // size} %), deepest 0x{lo + untouched * 4:08x}")


if __name__ == "__main__":
    main()
