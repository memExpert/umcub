"""Flash layout: the erase sectors of a part and the rule MCUboot applies to the
slots, which configure checks.

MCUboot refuses every upgrade ("Cannot upgrade: ...", the application simply stays) unless the
sectors of the two slots line up: compatible() is its boot_slots_compatible().
So on a part with sectors of different sizes the largest image is not "half of
the flash": the F411's 16, 16, 64, 128, 128, 128 KiB behind a 32 KiB bootloader
give one 128 KiB sector per slot.
"""
from .errors import UmcubError

FLASH_BASE = 0x08000000
KB = 1024
MODE_OVERWRITE, MODE_SWAP_SCRATCH, MODE_SWAP_OFFSET = 1, 2, 4
MODES_XIP = (5, 6)                  # direct-xip: no upgrade copies, no rule on the sectors
# Program unit of the flash driver (UMCUB_FAMILY_WRITE_ALIGN, config/umcub_config_check.h compares them).
WRITE_ALIGN = {"STM32F1": 8, "STM32F4": 8, "STM32H7": 32}


def die(msg):
    raise UmcubError(msg)


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
        die(f"no sector map for {family}")
    out, a = [], FLASH_BASE
    for s in sizes:
        out.append((a, s))
        a += s
    return out


def sector_map(family, flash_kb):
    """The sectors as a line of text: "4 x 16 KiB, 64 KiB, 3 x 128 KiB"."""
    runs = []
    for _, size in sectors(family, flash_kb):
        if runs and runs[-1][1] == size:
            runs[-1][0] += 1
        else:
            runs.append([1, size])
    return ", ".join(f"{n} x {s // KB} KiB" if n > 1 else f"{s // KB} KiB" for n, s in runs)


def compatible(pri, sec, mode, scratch=0):
    """MCUboot's boot_slots_compatible() on the sector sizes of two slots: None when it
    accepts them, otherwise what it says. swap_offset.c for swap-offset, swap_scratch.c
    for swap-scratch and overwrite (no scratch area there, its running totals are never reset)."""
    if mode in MODES_XIP:
        return None
    if mode == MODE_SWAP_OFFSET:
        if len(pri) != len(sec) and len(pri) + 1 != len(sec):
            return "not a compatible amount of sectors"
        if any(a != b for a, b in zip(pri, sec[:-1])):
            return "not same sector layout"
        if len(pri) < 2:                # its app_max_size(): the trailer takes a whole sector of the primary
            return "the primary slot needs at least two sectors, its image size limit is 0 with one"
        return None
    size = lambda s, n: s[n] if n < len(s) else 0       # noqa: E731   (the unused entries of its array)
    i = j = sz0 = sz1 = total0 = total1 = smaller = 0
    while i < len(pri) or j < len(sec):
        if i > len(pri) or j > len(sec):    # past its end (it reads on): can only end refused
            return "slots are not compatible"
        if sz0 == sz1:
            sz0 += size(pri, i)
            sz1 += size(sec, j)
            i += 1
            j += 1
        elif sz0 < sz1:
            sz0 += size(pri, i)
            if smaller == 2:
                return "slots have non-compatible sectors"
            smaller = 1
            i += 1
        else:
            sz1 += size(sec, j)
            if smaller == 1:
                return "slots have non-compatible sectors"
            smaller = 2
            j += 1
        if mode != MODE_OVERWRITE and sz0 == sz1:
            total0 += sz0
            total1 += sz1
            if sz0 > scratch:
                return "not all sectors fit inside scratch"
            smaller = sz0 = sz1 = 0
    if i != len(pri) or j != len(sec) or total0 != total1:
        return "slots are not compatible"
    return None


def check(family, flash_kb, values, mode, images):
    """A set layout (IMG<n>_..., SCRATCH_SIZE) against MCUboot's rule, a primary slot needs a
    secondary one too: what is wrong, or None. Slots off the sector boundaries:
    config/umcub_config_check.h stops those."""
    secs = sectors(family, flash_kb)

    def sizes(addr, size):
        run = [s for a, s in secs if addr <= a < addr + size]
        return run if sum(run) == size and any(a == addr for a, _ in secs) else None

    for i in range(images):
        if values[f"IMG{i}_PRIMARY_SIZE"] and not values[f"IMG{i}_SECONDARY_SIZE"]:
            return (f"image {i}: no secondary slot, MCUboot has nowhere to take an update from: set "
                    f"UMCUB_CFG_IMG{i}_SECONDARY_ADDR / _SIZE (docs/board.md, Slots)")
        pri = sizes(values[f"IMG{i}_PRIMARY_ADDR"], values[f"IMG{i}_PRIMARY_SIZE"])
        sec = sizes(values[f"IMG{i}_SECONDARY_ADDR"], values[f"IMG{i}_SECONDARY_SIZE"])
        if pri and sec:
            why = compatible(pri, sec, mode, values.get("SCRATCH_SIZE", 0))
            if why:
                return (f"image {i}: MCUboot would refuse every upgrade between these slots ({why}): primary sectors "
                        f"{[s // KB for s in pri]} KiB, secondary {[s // KB for s in sec]} KiB (docs/board.md, Slots)")
    return None
