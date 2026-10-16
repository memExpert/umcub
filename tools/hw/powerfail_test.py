#!/usr/bin/env python3
"""Power-loss test of MCUboot upgrade/revert on real hardware (SWD through probe.py).

Needs a bootloader built with -DUMCUB_CONFIG_POST=tests/configs/feature/fault_inject.h
(UMCUB_CFG_TEST_FAULT_INJECT): it resets the MCU in the middle of the K-th
flash operation (sector erase or flash-word program already started), which
leaves half-erased sectors / half-programmed words like a power cut.

Swap modes (--mode swap), for every K:
  1. primary = v1 (confirmed), secondary = v2 marked for a test upgrade
  2. arm fault K, reset -> swap is interrupted, bootloader runs again
  3. check: primary == v2, secondary == v1 (upgrade completed after the fault)
  4. arm fault K again, reset without confirm -> interrupted revert
  5. check: primary == v1, secondary == v2 (revert completed)
Overwrite (--mode overwrite): steps 1-3, check primary == v2 only.

Images are signed with tools/host/umcub_image.py for the bootloader's configuration
(--board / --core / --pre / --post, as for the build), so header size, alignment
and the board-type TLV match.

  NUCLEO-H755ZI-Q (swap-scratch):
  tools/hw/powerfail_test.py --boot build/hw/boot_fault/umcub_nucleo_h755zi_q_cm7.hex \\
      --app build/hw/sb_app_cm7/h755_cm7_app.bin --board nucleo_h755zi_q --core cm7 \\
      --erase 0x08020000+0xE0000 --points 40
  swap-offset (the update sits one sector into the secondary slot, the revert copy at its start):
  ... --pre tests/configs/mode/swap_offset.h --secondary 0x080A0000 --revert-copy 0x08080000
  Blue Pill (overwrite, 16 KiB slots, 1 KiB pages, no NRST on the SWD header):
  tools/hw/powerfail_test.py --boot build/bp_fault/umcub_bluepill_f103c8_cm3.hex \\
      --app build/bluepill/examples/bluepill_app/bluepill_app.bin --board bluepill_f103c8 \\
      --mode overwrite --primary 0x08008000 --secondary 0x0800C000 --erase 0x08008000+0x8000 \\
      --size 12288 --fault-addr 0x20004FF0 --sector-size 0x400 --connect under-reset --reset rst --points 40
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = os.path.join(ROOT, ".venv/bin/python")
FAULT_MAGIC = 0xFA017E57


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--boot", required=True, help="fault-injection bootloader .hex")
    ap.add_argument("--app", required=True, help="raw application .bin (unsigned) used as payload start")
    ap.add_argument("--board", required=True, help="board of the bootloader (tools/host/umcub_image.py --board)")
    ap.add_argument("--core", help="dual-core parts: cm7 / cm4")
    ap.add_argument("--pre", help="UMCUB_CONFIG_PRE of the bootloader build")
    ap.add_argument("--post", default=os.path.join(ROOT, "tests/configs/feature/fault_inject.h"),
                    help="UMCUB_CONFIG_POST of the bootloader build")
    ap.add_argument("--mode", choices=("swap", "overwrite"), default="swap")
    ap.add_argument("--primary", type=lambda s: int(s, 0), default=0x08020000)
    ap.add_argument("--secondary", type=lambda s: int(s, 0), default=0x08080000)
    ap.add_argument("--revert-copy", type=lambda s: int(s, 0),
                    help="where the previous image lands in the secondary slot after an upgrade (swap-offset: "
                         "the slot start, one sector before --secondary); default --secondary")
    ap.add_argument("--erase", default="0x08020000+0xE0000",
                    help="flash to erase before each run as ADDR+SIZE (whole sectors)")
    ap.add_argument("--size", type=int, default=200 * 1024, help="payload size")
    ap.add_argument("--fault-addr", type=lambda s: int(s, 0), default=0x3800F000,
                    help="UMCUB_CFG_TEST_FAULT_ADDR of the bootloader")
    ap.add_argument("--reset", choices=("hardRst", "rst"), default="hardRst",
                    help="hardRst (NRST wired) or rst (system reset over SWD)")
    probe.add_arguments(ap)
    ap.add_argument("--points", type=int, default=40)
    ap.add_argument("--first", type=int, default=8, help="fault points at the very first operations")
    ap.add_argument("--settle", type=float, default=4.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    p = probe.open_probe(a)
    read_words = p.read32
    read_mem = p.read_mem
    e_addr, _, e_size = a.erase.partition("+")
    e_addr, e_size = int(e_addr, 0), int(e_size, 0)

    def sign(payload, version, out, pad):
        args = [PY, os.path.join(ROOT, "tools/host/umcub_image.py"), "sign", "--board", a.board, "--version", version]
        for opt in ("core", "pre", "post"):
            if getattr(a, opt):
                args += [f"--{opt}", getattr(a, opt)]
        if pad:
            args.append("--pad")                # trailer with magic: pending test upgrade
        subprocess.run(args + ["-o", out, payload], check=True, capture_output=True)
        return out + ".signed.bin"

    tmp = tempfile.mkdtemp(prefix="powerfail_")
    rnd = random.Random(a.seed)
    app = open(a.app, "rb").read()
    payloads = []
    for tag in (b"v1", b"v2"):
        pl = app + tag + bytes(rnd.getrandbits(8) for _ in range(a.size - len(app) - 2))
        path = os.path.join(tmp, tag.decode() + "_payload.bin")
        open(path, "wb").write(pl)
        payloads.append(path)
    v1 = sign(payloads[0], "1.0.0", os.path.join(tmp, "v1"), False)
    v2 = sign(payloads[1], "2.0.0", os.path.join(tmp, "v2"), True)
    # ECDSA signatures are randomized: compare the deterministic part (header +
    # payload); the bootloader validates the signature on every boot anyway.
    hdr_size = int.from_bytes(open(v1, "rb").read()[8:10], "little")
    n_cmp = hdr_size + a.size
    v1b, v2b = open(v1, "rb").read()[:n_cmp], open(v2, "rb").read()[:n_cmp]

    print("flashing fault-injection bootloader")
    p.program(a.boot)

    def prepare():
        p.erase(e_addr, e_size)
        p.program(v1, a.primary)
        p.program(v2, a.secondary)

    def arm(k):
        p.write32(a.fault_addr, [FAULT_MAGIC, k, 0, 0])
        w = read_words(a.fault_addr, 4)
        if w[:2] != [FAULT_MAGIC, k]:
            raise RuntimeError(f"arming failed: {w}")

    revert_copy = a.revert_copy if a.revert_copy is not None else a.secondary

    def boot_and_check(expect_primary, expect_secondary):
        p.reset(hw=a.reset == "hardRst")
        time.sleep(a.settle)
        f = read_words(a.fault_addr, 4)
        pri = read_mem(a.primary, len(expect_primary))
        # after an upgrade the secondary holds v1 (revert copy), after a revert v2 (the update again)
        sec_addr = revert_copy if expect_secondary == v1b else a.secondary
        sec = read_mem(sec_addr, len(expect_secondary)) if expect_secondary else None
        return f, pri == expect_primary, expect_secondary is None or sec == expect_secondary

    swap = a.mode == "swap"
    # Sizing: count flash operations of a full upgrade (and of a full revert).
    prepare()
    arm(0)
    f, ok_p, ok_s = boot_and_check(v2b, v1b if swap else None)
    n_up = f[2]
    print(f"upgrade without faults: {n_up} flash operations, primary==v2 {ok_p}" +
          (f", secondary==v1 {ok_s}" if swap else ""))
    if not (ok_p and ok_s):
        sys.exit("baseline upgrade failed")
    phases = [("upgrade", n_up, v2b, v1b if swap else None)]
    if swap:
        arm(0)
        f, ok_p, ok_s = boot_and_check(v1b, v2b)
        n_rev = f[2]
        print(f"revert without faults: {n_rev} flash operations, primary==v1 {ok_p}, secondary==v2 {ok_s}")
        if not (ok_p and ok_s):
            sys.exit("baseline revert failed")
        phases.append(("revert", n_rev, v1b, v2b))

    def pick(n):
        ks = set(range(1, min(n, a.first) + 1))              # first erases
        ks |= {max(1, n * i // a.points) for i in range(1, a.points)}
        ks |= {rnd.randint(1, n) for _ in range(a.points // 4)}
        return sorted(ks)

    failures = 0
    for phase, n, exp_p, exp_s in phases:
        for k in pick(n):
            prepare()
            if phase == "revert":
                arm(0)
                f, ok_p, ok_s = boot_and_check(v2b, v1b)       # clean test upgrade first
                if not (ok_p and ok_s):
                    print(f"  {phase} k={k}: setup upgrade failed")
                    failures += 1
                    continue
            arm(k)
            f, ok_p, ok_s = boot_and_check(exp_p, exp_s)
            fired = f[3] == 1
            status = "OK " if (ok_p and ok_s) else "BAD"
            if not (ok_p and ok_s):
                failures += 1
            print(f"  {status} {phase:7s} fault at op {k:6d}/{n:<6d} fired={fired} "
                  f"primary={'ok' if ok_p else 'WRONG'}" +
                  (f" secondary={'ok' if ok_s else 'WRONG'}" if exp_s else ""), flush=True)

    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
