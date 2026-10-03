#!/usr/bin/env python3
"""Power-loss test of MCUboot upgrade/revert on real hardware (STM32H7, SWD).

Needs a bootloader built with -DUMCUB_CONFIG_POST=tools/config/fault_inject.h
(UMCUB_CFG_TEST_FAULT_INJECT): it resets the MCU in the middle of the K-th
flash operation (sector erase or flash-word program already started), which
leaves half-erased sectors / half-programmed ECC words like a power cut.

For every K:
  1. primary = v1 (confirmed), secondary = v2 marked for a test upgrade
  2. arm fault K, reset -> swap is interrupted, bootloader runs again
  3. check: primary == v2, secondary == v1 (upgrade completed after the fault)
  4. arm fault K again, reset without confirm -> interrupted revert
  5. check: primary == v1, secondary == v2 (revert completed)

  tools/hw/powerfail_test.py --boot build/hw/boot_fault/umcub_nucleo_h755zi_q_cm7.hex \
      --app build/hw/sb_app_cm7/h755_cm7_app.bin --points 40
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
import time

CLI = os.environ.get("STM32_PROGRAMMER_CLI", os.path.expanduser(
    "~/.local/share/stm32cube/bundles/programmer/2.23.0/bin/STM32_Programmer_CLI"))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IMGTOOL = os.path.join(ROOT, ".venv/bin/imgtool")
KEY = os.path.join(ROOT, "tools/keys/dev-ecdsa-p256.pem")
FAULT_ADDR = 0x3800F000
FAULT_MAGIC = 0xFA017E57


def _ok(out):
    # Transient connect noise from CubeProgrammer on this part ("Unable to get
    # core ID", configuration database warnings) is tolerated if the command
    # itself finished; anything else is retried.
    bad = [l for l in out.splitlines() if l.startswith("Error") and "core ID" not in l
           and "Database" not in l and "Failed to download data" not in l]
    return not bad


def cli(*args, check=True, tries=8):
    for attempt in range(tries):
        r = subprocess.run([CLI, "-c", "port=SWD", "mode=HOTPLUG", *args], capture_output=True, text=True)
        out = re.sub(r"\x1b\[[0-9;]*m", "", r.stdout + r.stderr)
        if not check or _ok(out):
            return out
        time.sleep(1.0 + attempt)
    raise RuntimeError(out[-800:])


def read_words(addr, n):
    out = cli("-r32", hex(addr), hex(4 * n))
    vals = []
    for line in out.splitlines():
        m = re.match(r"0x[0-9A-Fa-f]+\s*:\s*(.*)", line.strip())
        if m:
            vals += [int(v, 16) for v in m.group(1).split()]
    return vals[:n]


def read_mem(addr, size, tmp):
    path = os.path.join(tmp, "dump.bin")
    cli("-u", hex(addr), hex(size), path)
    return open(path, "rb").read()


def sign(payload, version, out, slot_size, extra):
    subprocess.run([IMGTOOL, "sign", "-k", KEY, "--version", version, "--header-size", "0x400", "--pad-header",
                    "--slot-size", str(slot_size), "--align", "32", "--max-align", "32", "--max-sectors",
                    str(slot_size // 0x20000), *extra, payload, out], check=True, capture_output=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--boot", required=True, help="fault-injection bootloader .hex")
    ap.add_argument("--app", required=True, help="raw application .bin (unsigned) used as payload")
    ap.add_argument("--primary", type=lambda s: int(s, 0), default=0x08020000)
    ap.add_argument("--secondary", type=lambda s: int(s, 0), default=0x08080000)
    ap.add_argument("--slot-size", type=lambda s: int(s, 0), default=0x60000)
    ap.add_argument("--sectors", default="1 2 3 4 5 6 7", help="sectors to erase before each run")
    ap.add_argument("--size", type=int, default=200 * 1024, help="payload size")
    ap.add_argument("--points", type=int, default=40)
    ap.add_argument("--settle", type=float, default=4.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="powerfail_")
    rnd = random.Random(a.seed)
    app = open(a.app, "rb").read()
    payloads = []
    for tag in (b"v1", b"v2"):
        p = app + tag + bytes(rnd.getrandbits(8) for _ in range(a.size - len(app) - 2))
        path = os.path.join(tmp, tag.decode() + ".raw")
        open(path, "wb").write(p)
        payloads.append(path)
    v1 = os.path.join(tmp, "v1.bin")
    v2 = os.path.join(tmp, "v2.bin")
    v1_hex = os.path.join(tmp, "v1.hex")
    v2_hex = os.path.join(tmp, "v2_pending.hex")
    sign(payloads[0], "1.0.0", v1, a.slot_size, [])
    sign(payloads[1], "2.0.0", v2, a.slot_size, [])
    sign(payloads[0], "1.0.0", v1_hex, a.slot_size, ["--hex-addr", hex(a.primary)])
    sign(payloads[1], "2.0.0", v2_hex, a.slot_size, ["--pad", "--hex-addr", hex(a.secondary)])
    # ECDSA signatures are randomized: compare the deterministic part (header +
    # payload); the bootloader validates the signature on every boot anyway.
    n_cmp = 0x400 + a.size
    v1b, v2b = open(v1, "rb").read()[:n_cmp], open(v2, "rb").read()[:n_cmp]

    print("flashing fault-injection bootloader")
    cli("-d", a.boot, "-v")

    def prepare():
        cli("-e", *a.sectors.split())
        cli("-d", v1_hex)
        cli("-d", v2_hex)

    def arm(k):
        cli("-w32", hex(FAULT_ADDR), hex(FAULT_MAGIC), hex(k), "0x0", "0x0", check=False)
        w = read_words(FAULT_ADDR, 4)
        if w[:2] != [FAULT_MAGIC, k]:
            raise RuntimeError(f"arming failed: {w}")

    def boot_and_check(expect_primary, expect_secondary):
        cli("-hardRst")
        time.sleep(a.settle)
        f = read_words(FAULT_ADDR, 4)
        pri = read_mem(a.primary, len(expect_primary), tmp)
        sec = read_mem(a.secondary, len(expect_secondary), tmp)
        return f, pri == expect_primary, sec == expect_secondary

    # Sizing: count flash operations of a full upgrade and of a full revert.
    prepare()
    arm(0)
    f, ok_p, ok_s = boot_and_check(v2b, v1b)
    n_up = f[2]
    print(f"upgrade without faults: {n_up} flash operations, primary==v2 {ok_p}, secondary==v1 {ok_s}")
    if not (ok_p and ok_s):
        sys.exit("baseline upgrade failed")
    arm(0)
    f, ok_p, ok_s = boot_and_check(v1b, v2b)
    n_rev = f[2]
    print(f"revert without faults: {n_rev} flash operations, primary==v1 {ok_p}, secondary==v2 {ok_s}")
    if not (ok_p and ok_s):
        sys.exit("baseline revert failed")

    def pick(n):
        ks = set(range(1, min(n, 8) + 1))                    # first erases
        ks |= {max(1, n * i // a.points) for i in range(1, a.points)}
        ks |= {rnd.randint(1, n) for _ in range(a.points // 4)}
        return sorted(ks)

    failures = 0
    for phase, n, exp_p, exp_s in (("upgrade", n_up, v2b, v1b), ("revert", n_rev, v1b, v2b)):
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
                  f"primary={'ok' if ok_p else 'WRONG'} secondary={'ok' if ok_s else 'WRONG'}", flush=True)

    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
