#!/usr/bin/env python3
"""Option bytes for umcub on STM32H745/H755 (both cores enabled), through probe.py (pyOCD).

  h755_option_bytes.py show        FLASH_OPTSR / BOOT7 / BOOT4 as programmed
  h755_option_bytes.py single      CM4 boots into the bootloader vector table and waits there
                                   (UMCUB_DUALCORE_SINGLE_BOOT): BOOT_CM4_ADD0 = 0x0800
  h755_option_bytes.py per-core    CM4 boots its own bootloader in bank 2: BOOT_CM4_ADD0 = 0x0810
  h755_option_bytes.py factory     ST defaults - same as per-core

BCM7 and BCM4 are always set (both cores boot); nothing else is touched.
STM32_Programmer_CLI 2.23 maps the H745/H755 to the single-core option-byte
layout and hides BOOT_CM4_ADD0 / BCM4, so the FLASH registers are programmed
directly (RM0399 4.4.3: OPTKEYR unlock, *_PRG, OPTSTART, wait for OPT_BUSY).
That needs one debug session from the unlock to the end of the change: the
stm32cube backend reconnects for every access, and doing so in the middle of
an option byte change once left this part with garbage option bytes (RDP
level 1, PCROP at the bank starts) - hence pyOCD only. pyOCD needs no pack
here (--target defaults to cortex_m: plain register access through the CM7).
The new values take effect at once; the running code must not program flash
meanwhile. While the change runs the bus can be busy and SWD reads fail (DP wait,
AP fault): that does not disturb the change, the tool asks again and reconnects
only once the time for the change has passed.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe  # noqa: E402

FLASH = 0x52002000
OPTKEYR, OPTCR, OPTSR_CUR, OPTSR_PRG, OPTCCR = FLASH + 0x08, FLASH + 0x18, FLASH + 0x1C, FLASH + 0x20, FLASH + 0x24
BOOT7_CUR, BOOT4_CUR, BOOT4_PRG = FLASH + 0x40, FLASH + 0x48, FLASH + 0x4C
# _CUR / _PRG pairs (RM0399 4.9): OPTSR, PRAR1, SCAR1, WPSN1, BOOT7, BOOT4, PRAR2, SCAR2, WPSN2
PAIRS = [0x1C, 0x28, 0x30, 0x38, 0x40, 0x48, 0x128, 0x130, 0x138]
OPTSR_STATUS = 1 << 0 | 1 << 30           # OPT_BUSY, OPTCHANGEERR: in _CUR only
OPTLOCK, OPTSTART, OPT_BUSY, OPTCHANGEERR = 1 << 0, 1 << 1, 1 << 0, 1 << 30
BCM4, BCM7 = 1 << 22, 1 << 23
DBGMCU_IDCODE = 0x5C001000
OPT_SETTLE_S, OPT_POLL_S, OPT_TIMEOUT_S = 0.2, 0.1, 10
BOOT4 = {"single": 0x10000800, "per-core": 0x10000810, "factory": 0x10000810}   # ADD1 0x1000 kept


def rd(p, addr):
    return p.read32(addr, 1)[0]


def show(p):
    optsr, boot7, boot4 = rd(p, OPTSR_CUR), rd(p, BOOT7_CUR), rd(p, BOOT4_CUR)
    print(f"OPTSR_CUR 0x{optsr:08x}  BCM7 {optsr >> 23 & 1}  BCM4 {optsr >> 22 & 1}  RDP 0x{optsr >> 8 & 0xFF:02x}")
    print(f"BOOT7_CUR 0x{boot7:08x}  CM7 boots at 0x{(boot7 & 0xFFFF) << 16:08x}")
    print(f"BOOT4_CUR 0x{boot4:08x}  CM4 boots at 0x{(boot4 & 0xFFFF) << 16:08x}")
    return optsr, boot4


def wait_change(p, reopen):
    """Until the running change has ended; returns the session to go on with."""
    time.sleep(OPT_SETTLE_S)                # the chip needs nobody: leave the bus alone first
    failed, end = 0, time.monotonic() + OPT_TIMEOUT_S
    while time.monotonic() < end:
        try:
            if not rd(p, OPTSR_CUR) & OPT_BUSY:
                if failed:
                    print(f"note: {failed} SWD reads failed during the change (bus busy)")
                return p
        except probe.ProbeTransferError:
            failed += 1
        time.sleep(OPT_POLL_S)
    if not failed:
        raise probe.ProbeError("option byte programming timed out")
    print(f"note: no answer over SWD for {OPT_TIMEOUT_S} s, connecting again")
    p = reopen(p)
    if rd(p, OPTSR_CUR) & OPT_BUSY:
        raise probe.ProbeError("option byte programming timed out")
    return p


def program(p, boot4, reopen):
    """Returns the session to go on with (the given one, or a new one after a dead link)."""
    if rd(p, OPTSR_CUR) & OPT_BUSY:
        raise probe.ProbeError("an option byte change is already running")
    # A rejected change (OPTCHANGEERR) leaves its values in the _PRG registers;
    # OPTSTART would send them again together with ours.
    for off in PAIRS:
        cur, prg = rd(p, FLASH + off), rd(p, FLASH + off + 4)
        if off == 0x1C:
            cur &= ~OPTSR_STATUS
        if cur != prg:
            raise probe.ProbeError(f"FLASH+0x{off + 4:03x} (_PRG) 0x{prg:08x} differs from _CUR 0x{cur:08x}: "
                                   "reset the board first")
    if rd(p, OPTCR) & OPTLOCK:
        p.write32(OPTKEYR, [0x08192A3B])
        p.write32(OPTKEYR, [0x4C5D6E7F])
    if rd(p, OPTCR) & OPTLOCK:
        raise probe.ProbeError("option bytes did not unlock")
    try:
        p.write32(OPTCCR, [OPTCHANGEERR])
        p.write32(BOOT4_PRG, [boot4])
        p.write32(OPTSR_PRG, [rd(p, OPTSR_PRG) | BCM7 | BCM4])
        start = rd(p, OPTCR) | OPTSTART
        try:
            p.write32(OPTCR, [start])
        except probe.ProbeTransferError:
            pass                            # started or not: the read back in main() tells
        p = wait_change(p, reopen)
        if rd(p, OPTSR_CUR) & OPTCHANGEERR:
            raise probe.ProbeError("option byte change error (OPTCHANGEERR)")
    finally:
        try:
            p.write32(OPTCR, [rd(p, OPTCR) | OPTLOCK])
        except Exception:                   # also a closed session: must not hide the error above
            print("note: option bytes not locked again, the next reset does it")
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    probe.add_arguments(ap)
    ap.add_argument("mode", choices=["show"] + sorted(BOOT4))
    ap.set_defaults(probe="pyocd")
    a = ap.parse_args()
    if a.probe != "pyocd":
        sys.exit("h755_option_bytes: needs --probe pyocd (one session for the whole change)")
    a.target = a.target or "cortex_m"
    try:
        def reopen(old):
            old.close()
            return probe.open_probe(a, "attach")

        p = probe.open_probe(a, "attach")
        try:
            if rd(p, DBGMCU_IDCODE) & 0xFFF != 0x450:
                raise probe.ProbeError("not an STM32H745/H747/H755/H757 (DBGMCU_IDCODE DEV_ID != 0x450)")
            if a.mode != "show":
                p = program(p, BOOT4[a.mode], reopen)
            optsr, boot4 = show(p)
            if a.mode != "show" and (boot4 != BOOT4[a.mode] or (optsr & (BCM7 | BCM4)) != BCM7 | BCM4):
                raise probe.ProbeError("read back differs from the request")
        finally:
            p.close()
    except probe.ProbeError as e:
        sys.exit(f"h755_option_bytes: {e}")


if __name__ == "__main__":
    main()
