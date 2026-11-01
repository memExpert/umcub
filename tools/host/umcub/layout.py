"""Flash layout: the erase sectors of a part, the rule MCUboot applies to the
slots, and the automatic placement of the slots behind the bootloader
(UMCUB_CFG_LAYOUT_AUTO).

MCUboot refuses every upgrade ("Cannot upgrade: ...", the application simply stays) unless the
sectors of the two slots line up: compatible() is its boot_slots_compatible().
So on a part with sectors of different sizes the largest image is not "half of
the flash": the F411's 16, 16, 64, 128, 128, 128 KiB behind the bootloader give
one 128 KiB sector per slot. The placement takes the layout with the largest
image that MCUboot accepts, closest to the bootloader; with two images the
flash is first halved between them, swap-scratch keeps the last sector as
scratch area, direct-xip has no such rule.
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


def _usable(pri, sec, mode):
    """The largest image of two slots (lists of sizes)."""
    return min(sum(pri), sum(sec) - (sec[-1] if mode == MODE_SWAP_OFFSET else 0))


def _split(secs, mode, scratch):
    """(primary, secondary) in a run of sectors: the largest image MCUboot accepts,
    in the least flash, closest to the start of the run."""
    sizes = [s for _, s in secs]
    n = len(secs)
    if mode in MODES_XIP or len(set(sizes)) == 1:       # no search: thousands of pages on some parts
        if mode in MODES_XIP:
            k = next(k for k in range(1, max(n, 2)) if sum(sizes[:k]) * 2 >= sum(sizes)) if n > 1 else 0
            end = n
        else:
            k = (n - 1) // 2 if mode == MODE_SWAP_OFFSET else n // 2
            end = 2 * k + (1 if mode == MODE_SWAP_OFFSET else 0)
        if k < (2 if mode == MODE_SWAP_OFFSET else 1):
            die("not enough flash behind the bootloader for two slots" +
                (" (swap-offset: at least two sectors in the primary)" if mode == MODE_SWAP_OFFSET else ""))
        return secs[:k], secs[k:end]
    best = None
    for a in range(n):
        for b in range(a + 1, n):
            for c in range(b + 1, n + 1):
                pri, sec = sizes[a:b], sizes[b:c]
                if mode == MODE_SWAP_SCRATCH and sum(pri) != sum(sec):
                    continue                    # compatible() has no totals to compare if they never meet
                if compatible(pri, sec, mode, scratch) is None:
                    # the image is what counts; then the least flash, the rest stays free
                    key = (_usable(pri, sec, mode), -(sum(pri) + sum(sec)), -a)
                    if best is None or key > best[0]:
                        best = (key, secs[a:b], secs[b:c])
    if best is None or best[0][0] <= 0:
        die("no two slots behind the bootloader that MCUboot accepts: their sectors must line up" +
            (", and swap-offset needs at least three sectors of one size" if mode == MODE_SWAP_OFFSET else ""))
    return best[1], best[2]


def auto(family, flash_kb, boot_addr, boot_size, mode, images):
    """{UMCUB_CFG name without prefix: value} of the slots (and scratch area)."""
    secs = sectors(family, flash_kb)
    end = boot_addr + boot_size
    if not any(a + s == end for a, s in secs):
        die(f"the bootloader region 0x{boot_addr:08X}+0x{boot_size:X} does not end on a sector boundary")
    rest = [x for x in secs if x[0] >= end]
    out = {}
    scratch = 0
    if mode == MODE_SWAP_SCRATCH:
        if len(rest) < 3:
            die("swap-scratch: not enough sectors behind the bootloader")
        out["SCRATCH_ADDR"], out["SCRATCH_SIZE"] = rest[-1]
        scratch = rest[-1][1]
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
        for name, part in zip(("PRIMARY", "SECONDARY"), _split(g, mode, scratch)):
            out[f"IMG{i}_{name}_ADDR"] = part[0][0]
            out[f"IMG{i}_{name}_SIZE"] = sum(x[1] for x in part)
    return out


def check(family, flash_kb, values, mode, images):
    """A set layout (IMG<n>_..., SCRATCH_SIZE) against MCUboot's rule: what is wrong, or None.
    Slots off the sector boundaries: config/umcub_config_check.h stops those."""
    secs = sectors(family, flash_kb)

    def sizes(addr, size):
        run = [s for a, s in secs if addr <= a < addr + size]
        return run if sum(run) == size and any(a == addr for a, _ in secs) else None

    for i in range(images):
        pri = sizes(values[f"IMG{i}_PRIMARY_ADDR"], values[f"IMG{i}_PRIMARY_SIZE"])
        sec = sizes(values[f"IMG{i}_SECONDARY_ADDR"], values[f"IMG{i}_SECONDARY_SIZE"])
        if pri and sec:
            why = compatible(pri, sec, mode, values.get("SCRATCH_SIZE", 0))
            if why:
                return (f"image {i}: MCUboot would refuse every upgrade between these slots ({why}): primary sectors "
                        f"{[s // KB for s in pri]} KiB, secondary {[s // KB for s in sec]} KiB (docs/board.md, Slots)")
    return None


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
