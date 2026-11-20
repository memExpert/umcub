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
WRITE_ALIGN = {"STM32F1": 8, "STM32F4": 8, "STM32G4": 8, "STM32H7": 32}


def die(msg):
    raise UmcubError(msg)


def sectors(family, flash_kb, dual_bank=True):
    """[(address, size)] of the erase sectors of a part (port/stm32<fam>/flash.c); `dual_bank`: the bank mode of
    parts that switch it with an option bit (UMCUB_CFG_FLASH_DUAL_BANK)."""
    if family == "STM32F1":
        page = 1 * KB if flash_kb <= 128 else 2 * KB        # RM0008 §3.3.3: 1 KiB up to medium density
        sizes = [page] * (flash_kb * KB // page)
    elif family == "STM32F4":
        sizes = [16 * KB] * 4 + [64 * KB] + [128 * KB] * ((flash_kb - 128) // 128)   # RM0383 Table 4
    elif family == "STM32G4":                               # RM0440 Tables 7 and 8
        if not dual_bank:
            sizes = [4 * KB] * (flash_kb // 4)
        else:                                               # bank 2 from 0x08040000 whatever the size
            bank = [(FLASH_BASE + o, 2 * KB) for o in range(0, flash_kb * KB // 2, 2 * KB)]
            return bank + [(a + 0x40000, s) for a, s in bank]
    elif family == "STM32H7":
        sizes = [128 * KB] * (flash_kb // 128)
    else:
        die(f"no sector map for {family}")
    out, a = [], FLASH_BASE
    for s in sizes:
        out.append((a, s))
        a += s
    return out


def smallest_region(family, flash_kb, start, needed, dual_bank=True):
    """Size of the fewest whole sectors from `start` that hold `needed` bytes, None if they do not fit."""
    size = 0
    for addr, s in sectors(family, flash_kb, dual_bank):
        if addr >= start:
            if addr != start + size:
                return None                 # a gap in the address space (G4 banks)
            size += s
            if size >= needed:
                return size
    return None


def sector_map(family, flash_kb, dual_bank=True):
    """The sectors as a line of text: "4 x 16 KiB, 64 KiB, 3 x 128 KiB" (" from 0x..." after a gap)."""
    runs, end = [], FLASH_BASE
    for addr, size in sectors(family, flash_kb, dual_bank):
        if runs and runs[-1][1] == size and addr == end:
            runs[-1][0] += 1
        else:
            runs.append([1, size, addr if addr != end else None])
        end = addr + size
    return ", ".join((f"{n} x {s // KB} KiB" if n > 1 else f"{s // KB} KiB") + (f" from 0x{a:08X}" if a else "")
                     for n, s, a in runs)


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


def overlap(values, images):
    """Two regions in use sharing flash - the bootloader, the slots of the `images` built, the scratch area: what
    overlaps, or None (an update would erase the other; config/umcub_config_check.h stops the same in C)."""
    names = ["BOOT", "SCRATCH"] + [f"IMG{i}_{s}" for i in range(images) for s in ("PRIMARY", "SECONDARY")]
    label = {"BOOT": "the bootloader", "SCRATCH": "the scratch area"}
    regions = [(n, values.get(f"{n}_ADDR", 0), values.get(f"{n}_SIZE", 0)) for n in names]
    regions = [(label.get(n) or f"image {n[3]} {n[5:].lower()} slot", a, s) for n, a, s in regions if s]
    for k, (n1, a1, s1) in enumerate(regions):
        for n2, a2, s2 in regions[k + 1:]:
            if a1 < a2 + s2 and a2 < a1 + s1:
                return (f"{n1} (0x{a1:08X} + 0x{s1:X}) and {n2} (0x{a2:08X} + 0x{s2:X}) overlap: an update would "
                        f"erase one with the other")
    return None


def misplaced(family, flash_kb, values, images, dual_bank=True):
    """The slots (IMG<n>_...) against the flash: one that is not inside it, or None (sector boundaries:
    config/umcub_config_check.h, which cannot see gaps between banks)."""
    secs = sectors(family, flash_kb, dual_bank)
    for i in range(images):
        for slot in ("PRIMARY", "SECONDARY"):
            addr, size = values[f"IMG{i}_{slot}_ADDR"], values[f"IMG{i}_{slot}_SIZE"]
            if not size:
                continue
            end, where = addr + size, f"image {i} {slot.lower()} slot 0x{addr:08X} + 0x{size:X}"
            inside = sum(min(a + s, end) - max(a, addr) for a, s in secs if a < end and a + s > addr)
            if inside != size:
                return f"{where} is outside the flash ({sector_map(family, flash_kb, dual_bank)})"
    return None


def check(family, flash_kb, values, mode, images, dual_bank=True):
    """A set layout (IMG<n>_..., SCRATCH_SIZE) against MCUboot's rule, a primary slot needs a
    secondary one too: what is wrong, or None. Slots off the flash: misplaced()."""
    secs = sectors(family, flash_kb, dual_bank)

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
