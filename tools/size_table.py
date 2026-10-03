#!/usr/bin/env python3
"""Bootloader flash size per MCU and the cost of every transport / feature.

Builds the reference board's bootloader in many variants (Release, -Os) and
prints Markdown tables for the README:

  tools/size_table.py [--out build/sizes] [-j 4]

Base = MCUboot + ECDSA-P256 validation + jump, no transport, no log, no text
commands, no inspection. Transport and feature costs are measured on top of
"base + UART": the first SMP transport also brings boot_serial and zcbor, which
is included in the UART column. "DFU only" is USB DFU without any SMP
transport, on top of base. Size = .text + .data (flash).
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOARD = "nucleo_h755zi_q"

# name, core, PRE overlay, extra POST lines
CHIPS = [
    ("STM32H755 CM7 (2 images, SINGLE_BOOT)", "cm7", None, ""),
    # The board gives the CM4 instance only CAN; borrow the CM7 pins to size the rest.
    ("STM32H755 CM4 (PER_CORE)", "cm4", "tools/config/per_core.h",
     "#define UMCUB_CFG_UART_INSTANCE 3\n#define UMCUB_CFG_UART_BAUD 115200\n"
     "#define UMCUB_CFG_UART_TX_PIN UMCUB_PIN('D', 8, 7)\n#define UMCUB_CFG_UART_RX_PIN UMCUB_PIN('D', 9, 7)\n"
     "#define UMCUB_CFG_ETH_PHY_ADDR 0\n#define UMCUB_CFG_ETH_RMII_PINS UMCUB_PIN('A', 1, 11), UMCUB_PIN('A', 2, 11), "
     "UMCUB_PIN('A', 7, 11), UMCUB_PIN('C', 1, 11), UMCUB_PIN('C', 4, 11), UMCUB_PIN('C', 5, 11), "
     "UMCUB_PIN('G', 11, 11), UMCUB_PIN('G', 13, 11), UMCUB_PIN('B', 13, 11)\n"),
    ("STM32H743 / H753 (single core)", "cm7", "tools/config/single_core.h",
     "#undef UMCUB_CFG_MCU\n#define UMCUB_CFG_MCU STM32H743xx\n"
     "#undef UMCUB_CFG_PWR_SUPPLY\n#define UMCUB_CFG_PWR_SUPPLY UMCUB_H7_SUPPLY_LDO\n"),
]

OFF = {"TRANSPORT_UART": 0, "TRANSPORT_USB_CDC": 0, "TRANSPORT_USB_DFU": 0, "TRANSPORT_CAN": 0,
       "CAN_FD": 0, "CAN_LOOPBACK": 0, "TRANSPORT_ETH": 0, "LOG_LEVEL": 0, "CMD_ENABLE": 0,
       "INSPECT_VERIFY": 0, "INSPECT_HASH": 0, "READBACK": 0}
UART = {"TRANSPORT_UART": 1}

# column, settings on top of OFF, reference column (delta) or None
VARIANTS = [
    ("base", {}, None),
    ("UART", UART, "base"),
    ("USB CDC", {**UART, "TRANSPORT_USB_CDC": 1}, "UART"),
    ("USB DFU", {**UART, "TRANSPORT_USB_DFU": 1}, "UART"),
    ("USB CDC+DFU", {**UART, "TRANSPORT_USB_CDC": 1, "TRANSPORT_USB_DFU": 1}, "UART"),
    ("DFU only", {"TRANSPORT_USB_DFU": 1}, "base"),
    ("CAN", {**UART, "TRANSPORT_CAN": 1}, "UART"),
    ("CAN FD", {**UART, "TRANSPORT_CAN": 1, "CAN_FD": 1}, "UART"),
    ("Ethernet", {**UART, "TRANSPORT_ETH": 1}, "UART"),
    ("log", {**UART, "LOG_LEVEL": 3}, "UART"),
    ("commands", {**UART, "CMD_ENABLE": 1}, "UART"),
    ("verify+hash", {**UART, "INSPECT_VERIFY": 1, "INSPECT_HASH": 1}, "UART"),
    ("readback", {**UART, "INSPECT_VERIFY": 1, "INSPECT_HASH": 1, "READBACK": 1}, "verify+hash"),
    ("everything", {"TRANSPORT_UART": 1, "TRANSPORT_USB_CDC": 1, "TRANSPORT_USB_DFU": 1, "TRANSPORT_CAN": 1,
                    "CAN_FD": 1, "TRANSPORT_ETH": 1, "LOG_LEVEL": 3, "CMD_ENABLE": 1, "INSPECT_VERIFY": 1,
                    "INSPECT_HASH": 1, "READBACK": 1}, None),
]
TRANSPORTS = ["UART", "USB CDC", "USB DFU", "USB CDC+DFU", "CAN", "CAN FD", "Ethernet", "DFU only"]
FEATURES = ["log", "commands", "verify+hash", "readback"]


def build(out, ci, chip, vi, variant):
    _, core, pre, extra = chip
    name, settings, _ = variant
    d = out / f"c{ci}_v{vi}"
    d.mkdir(parents=True, exist_ok=True)
    post = d / "post.h"
    lines = [extra]
    for k, v in {**OFF, **settings}.items():
        lines.append(f"#undef UMCUB_CFG_{k}\n#define UMCUB_CFG_{k} {v}")
    post.write_text("\n".join(lines) + "\n")
    args = ["cmake", "-S", str(ROOT), "-B", str(d / "b"), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DUMCUB_BOARD={BOARD}", f"-DUMCUB_CORE={core}", f"-DUMCUB_CONFIG_POST={post}"]
    if pre:
        args.append(f"-DUMCUB_CONFIG_PRE={ROOT / pre}")
    log = d / "build.log"
    with open(log, "w") as f:
        ok = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT).returncode == 0 and \
             subprocess.run(["cmake", "--build", str(d / "b")], stdout=f, stderr=subprocess.STDOUT).returncode == 0
    if not ok:
        return None, log
    elf = next((d / "b").glob("umcub_*.elf"))
    r = subprocess.run(["arm-none-eabi-size", str(elf)], capture_output=True, text=True, check=True)
    text, data = map(int, r.stdout.splitlines()[1].split()[:2])
    return text + data, log


def kb(n):
    return f"{n / 1024:.1f} K"


def delta(n):
    return f"+{n / 1024:.1f} K" if n >= 0 else f"{n / 1024:.1f} K"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "build/sizes"))
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 4) // 4))
    a = ap.parse_args()
    out = Path(a.out)

    size = {}
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        jobs = {ex.submit(build, out, ci, c, vi, v): (ci, v[0])
                for ci, c in enumerate(CHIPS) for vi, v in enumerate(VARIANTS)}
        for fut in concurrent.futures.as_completed(jobs):
            n, log = fut.result()
            size[jobs[fut]] = n
            if n is None:
                print(f"note: {CHIPS[jobs[fut][0]][0]} / {jobs[fut][1]} does not build (see {log})", file=sys.stderr)

    ref = {v[0]: v[2] for v in VARIANTS}

    def cell(ci, col):
        n, r = size.get((ci, col)), ref[col]
        if n is None or size.get((ci, r)) is None:
            return "—"
        return delta(n - size[(ci, r)])

    def table(cols, first):
        print("| MCU | " + " | ".join(first + cols) + " |")
        print("|---|" + "---:|" * (len(first) + len(cols)))
        for ci, c in enumerate(CHIPS):
            lead = [kb(size[(ci, "base")])] if "base" in " ".join(first) else []
            if "all" in " ".join(first):
                lead.append(kb(size[(ci, "everything")]) if size.get((ci, "everything")) else "—")
            print(f"| {c[0]} | " + " | ".join(lead + [cell(ci, col) for col in cols]) + " |")
        print()

    table(TRANSPORTS, ["base", "all on"])
    table(FEATURES, [])


if __name__ == "__main__":
    main()
