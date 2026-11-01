"""Flash layout: the erase sectors of a part and the automatic placement of
the image slots behind the bootloader (UMCUB_CFG_LAYOUT_AUTO).

The slots take all flash behind the bootloader, split at sector boundaries so
that the largest image fits: per image a primary and a secondary slot of about
the same size (swap-offset: the secondary one sector larger), swap-scratch
keeps the last sector as scratch area. With two images the flash is first
halved between them.
"""
from .errors import UmcubError

FLASH_BASE = 0x08000000
KB = 1024
MODE_SWAP_SCRATCH, MODE_SWAP_OFFSET = 2, 4
# Program unit of the flash driver (UMCUB_FAMILY_WRITE_ALIGN, config/umcub_config_check.h compares them).
WRITE_ALIGN = {"STM32F1": 8, "STM32F4": 8, "STM32H7": 32}


def sectors(family, flash_kb):
    """[(address, size)] of the erase sectors of a part (port/stm32<fam>/flash.c)."""
    if family == "STM32F1":
        page = 1 * KB if flash_kb <= 128 else 2 * KB        # RM0008 §3.3.3: 1 KiB up to medium density
        sizes = [page] * (flash_kb * KB // page)
    elif family == "STM32F4":
        sizes = [16 * KB] * 4 + [64 * KB] + [128 * KB] * ((flash_kb - 128) // 128)   # RM0383 Table 4
    elif family == "STM32H7":
        sizes = [128 * KB] * (flash_kb // 128)
    else:
        raise UmcubError(f"no sector map for {family}")
    out, a = [], FLASH_BASE
    for s in sizes:
        out.append((a, s))
        a += s
    return out


def _split(secs, mode):
    """(primary, secondary) of a run of sectors: the split where the image that
    fits both is largest."""
    best = None
    for k in range(1, len(secs)):
        p, s = secs[:k], secs[k:]
        psize, ssize = sum(x[1] for x in p), sum(x[1] for x in s)
        usable = min(psize, ssize - (s[-1][1] if mode == MODE_SWAP_OFFSET else 0))
        if best is None or usable > best[0]:
            best = (usable, p, s)
    if best is None or best[0] <= 0:
        raise UmcubError("not enough flash behind the bootloader for two slots")
    return best[1], best[2]


def auto(family, flash_kb, boot_addr, boot_size, mode, images):
    """{UMCUB_CFG name without prefix: value} of the slots (and scratch area)."""
    secs = sectors(family, flash_kb)
    end = boot_addr + boot_size
    if not any(a + s == end for a, s in secs):
        raise UmcubError(f"the bootloader region 0x{boot_addr:08X}+0x{boot_size:X} does not end on a sector boundary")
    rest = [x for x in secs if x[0] >= end]
    out = {}
    if mode == MODE_SWAP_SCRATCH:
        if len(rest) < 3:
            raise UmcubError("swap-scratch: not enough sectors behind the bootloader")
        out["SCRATCH_ADDR"], out["SCRATCH_SIZE"] = rest[-1]
        rest = rest[:-1]
    if images == 2:
        half = sum(x[1] for x in rest) / 2
        k, acc = 0, 0
        while k < len(rest) - 1 and acc + rest[k][1] <= half:
            acc += rest[k][1]
            k += 1
        groups = [rest[:k], rest[k:]]
    else:
        groups = [rest]
    for i, g in enumerate(groups):
        for name, part in zip(("PRIMARY", "SECONDARY"), _split(g, mode)):
            out[f"IMG{i}_{name}_ADDR"] = part[0][0]
            out[f"IMG{i}_{name}_SIZE"] = sum(x[1] for x in part)
    return out


def describe(v):
    """One line per region, for the configure log."""
    lines = []
    for i in (0, 1):
        for slot in ("PRIMARY", "SECONDARY"):
            a, s = v.get(f"IMG{i}_{slot}_ADDR"), v.get(f"IMG{i}_{slot}_SIZE")
            if s:
                lines.append(f"image {i} {slot.lower():9} 0x{a:08X} {s // KB} KiB")
    if v.get("SCRATCH_SIZE"):
        lines.append(f"scratch           0x{v['SCRATCH_ADDR']:08X} {v['SCRATCH_SIZE'] // KB} KiB")
    return lines
