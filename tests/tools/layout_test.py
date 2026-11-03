#!/usr/bin/env python3
"""umcub.layout: MCUboot's rule on the slots. The F411 cases of compatible() were seen on a
Black Pill: "Cannot upgrade: slots have non-compatible sectors"."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/host"))
from umcub import layout  # noqa: E402

KB = 1024
OW, SC, SO = 1, 2, 4
bad = 0


def expect(what, got, want):
    global bad
    if got != want:
        bad += 1
        print(f"FAIL {what}: {got}, expected {want}")


def k(*sizes):
    return [s * KB for s in sizes]


for what, pri, sec, mode, scratch, ok in (
    ("F411 overwrite one sector each", k(128), k(128), OW, 0, True),
    ("F411 overwrite 224 / 256: seen refused on the board", k(16, 16, 64, 128), k(128, 128), OW, 0, False),
    ("F411 overwrite 208 / 256", k(16, 64, 128), k(128, 128), OW, 0, False),
    ("small sectors fit one larger", k(16, 16, 64), k(128), OW, 0, True),
    ("uniform equal", k(1) * 16, k(1) * 16, OW, 0, True),
    ("uniform 7 / 8: overwrite refuses", k(128) * 7, k(128) * 8, OW, 0, False),
    ("uniform 8 / 7: overwrite refuses", k(128) * 8, k(128) * 7, OW, 0, False),
    ("scratch fits", k(128), k(128), SC, 128 * KB, True),
    ("scratch too small", k(128), k(128), SC, 64 * KB, False),
    ("swap-scratch: 7 / 8", k(128) * 7, k(128) * 8, SC, 128 * KB, False),
    ("swap-offset one more", k(128) * 3, k(128) * 4, SO, 0, True),
    ("swap-offset, primary of one sector: image size limit 0, seen on the board", k(128), k(128, 128), SO, 0, False),
    ("swap-offset two more", k(128) * 3, k(128) * 5, SO, 0, False),
    ("swap-offset mixed", k(64, 128), k(128, 128, 128), SO, 0, False),
    ("direct-xip has no rule", k(16, 16, 64, 128), k(128, 128), 5, 0, True),
):
    expect(what, layout.compatible(pri, sec, mode, scratch) is None, ok)


# a set layout
base = {"IMG0_PRIMARY_ADDR": 0x08008000, "IMG0_PRIMARY_SIZE": 0x38000, "IMG0_SECONDARY_ADDR": 0x08040000,
        "IMG0_SECONDARY_SIZE": 0x40000}
expect("check: refused", bool(layout.check("STM32F4", 512, base, OW, 1)), True)
base.update(IMG0_PRIMARY_ADDR=0x08020000, IMG0_PRIMARY_SIZE=0x20000, IMG0_SECONDARY_SIZE=0x20000)
expect("check: accepted", layout.check("STM32F4", 512, base, OW, 1), None)
expect("check: nothing set", layout.check("STM32F4", 512, {"IMG0_PRIMARY_ADDR": 0, "IMG0_PRIMARY_SIZE": 0, "IMG0_SECONDARY_ADDR": 0,
                                                           "IMG0_SECONDARY_SIZE": 0}, OW, 1), None)
nosec = dict(base, IMG0_SECONDARY_ADDR=0, IMG0_SECONDARY_SIZE=0)
expect("check: no secondary slot", "no secondary slot" in (layout.check("STM32F4", 512, nosec, OW, 1) or ""), True)
two = dict(base, IMG1_PRIMARY_ADDR=0x08060000, IMG1_PRIMARY_SIZE=0x20000, IMG1_SECONDARY_ADDR=0, IMG1_SECONDARY_SIZE=0)
expect("check: image 1 without a secondary slot", "image 1" in (layout.check("STM32F4", 512, two, OW, 2) or ""), True)
expect("check: image 1 not in use", layout.check("STM32F4", 512, two, OW, 1), None)
expect("sector map", layout.sector_map("STM32F4", 512), "4 x 16 KiB, 64 KiB, 3 x 128 KiB")
expect("region: F1 pages", layout.smallest_region("STM32F1", 64, 0x08000000, 28620), 28 * KB)
expect("region: exactly a page", layout.smallest_region("STM32F1", 64, 0x08000000, 28 * KB), 28 * KB)
expect("region: F4 two sectors", layout.smallest_region("STM32F4", 512, 0x08000000, 26236), 32 * KB)
expect("region: F4 into the 64 KiB sector", layout.smallest_region("STM32F4", 512, 0x08000000, 65 * KB), 128 * KB)
expect("region: H7 bank 2", layout.smallest_region("STM32H7", 2048, 0x08100000, 30000), 128 * KB)
expect("region: does not fit", layout.smallest_region("STM32F1", 64, 0x08000000, 65 * KB), None)

print(f"layout: {'FAILED' if bad else 'all cases passed'}")
sys.exit(1 if bad else 0)
