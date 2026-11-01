#!/usr/bin/env python3
"""umcub.layout: MCUboot's rule on the slots and the automatic placement. The F411 cases of
compatible() were seen on a Black Pill: "Cannot upgrade: slots have non-compatible sectors"."""
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


def slots(family, flash_kb, boot, mode, images=1):
    v = layout.auto(family, flash_kb, 0x08000000, boot, mode, images)
    for i in range(images):
        pri = [s for a, s in layout.sectors(family, flash_kb)
               if v[f"IMG{i}_PRIMARY_ADDR"] <= a < v[f"IMG{i}_PRIMARY_ADDR"] + v[f"IMG{i}_PRIMARY_SIZE"]]
        sec = [s for a, s in layout.sectors(family, flash_kb)
               if v[f"IMG{i}_SECONDARY_ADDR"] <= a < v[f"IMG{i}_SECONDARY_ADDR"] + v[f"IMG{i}_SECONDARY_SIZE"]]
        expect(f"{family} {flash_kb} mode {mode}: what it placed is accepted",
               layout.compatible(pri, sec, mode, v.get("SCRATCH_SIZE", 0)), None)
    return v


P, S = "IMG0_PRIMARY", "IMG0_SECONDARY"
for what, v, want in (
    ("F411 overwrite: the sectors the Black Pill uses",
     slots("STM32F4", 512, 0x8000, OW), (0x08020000, 0x20000, 0x08040000, 0x20000)),
    ("F411 overwrite, 48 KiB bootloader",
     slots("STM32F4", 512, 0xC000, OW), (0x08020000, 0x20000, 0x08040000, 0x20000)),
    ("F411 swap-scratch", slots("STM32F4", 512, 0x8000, SC), (0x08020000, 0x20000, 0x08040000, 0x20000)),
    ("F103C8 overwrite", slots("STM32F1", 64, 0x8000, OW), (0x08008000, 0x4000, 0x0800C000, 0x4000)),
    ("F103C8 odd pages", slots("STM32F1", 64, 0x8400, OW), (0x08008400, 0x3C00, 0x0800C000, 0x3C00)),
    ("H755 overwrite: 15 sectors, one left", slots("STM32H7", 2048, 0x20000, OW), (0x08020000, 0xE0000, 0x08100000, 0xE0000)),
    ("H755 swap-offset", slots("STM32H7", 2048, 0x20000, SO), (0x08020000, 0xE0000, 0x08100000, 0x100000)),
):
    expect(what, (v[P + "_ADDR"], v[P + "_SIZE"], v[S + "_ADDR"], v[S + "_SIZE"]), want)
expect("F411 swap-scratch: scratch", slots("STM32F4", 512, 0x8000, SC)["SCRATCH_ADDR"], 0x08060000)
slots("STM32F4", 512, 0x8000, OW, 2)
try:
    layout.auto("STM32F4", 512, 0x08000000, 0x8000, SO, 1)
    expect("F411 swap-offset: three 128 KiB sectors are not enough", "placed", "refused")
except layout.UmcubError:
    pass
slots("STM32H7", 2048, 0x20000, SC, 2)

# a set layout
base = {"IMG0_PRIMARY_ADDR": 0x08008000, "IMG0_PRIMARY_SIZE": 0x38000, "IMG0_SECONDARY_ADDR": 0x08040000,
        "IMG0_SECONDARY_SIZE": 0x40000}
expect("check: refused", bool(layout.check("STM32F4", 512, base, OW, 1)), True)
base.update(IMG0_PRIMARY_ADDR=0x08020000, IMG0_PRIMARY_SIZE=0x20000, IMG0_SECONDARY_SIZE=0x20000)
expect("check: accepted", layout.check("STM32F4", 512, base, OW, 1), None)

print(f"layout: {'FAILED' if bad else 'all cases passed'}")
sys.exit(1 if bad else 0)
