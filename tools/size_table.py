#!/usr/bin/env python3
"""Bootloader flash size per MCU and the cost of every transport / feature.

Builds the reference boards' bootloaders in many variants (Release, -Os) and
prints the Markdown tables of README.md "Bootloader size":

  tools/size_table.py [--out build/sizes] [-j 4]     print the tables
  tools/size_table.py --update                       rewrite them in README.md
  tools/size_table.py --check                        exit 1 if README.md is stale

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

# name, core, PRE overlay, extra POST lines, board
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
    # Cortex-M3, sized in the 44 KiB bootloader region of the USB layout; no
    # CAN FD and no Ethernet on this part; "all on" without CAN (shares its
    # SRAM with USB on the F103).
    ("STM32F103 (Blue Pill, overwrite)", "", None, f'#include "{ROOT}/tools/config/bluepill_usb.h"\n',
     "bluepill_f103c8", {"CAN_FD", "TRANSPORT_ETH"}, {"TRANSPORT_CAN"}),
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
    ("UART lite", {**UART, "SMP_ENABLE": 0}, "base"),
    ("CAN", {**UART, "TRANSPORT_CAN": 1}, "UART"),
    ("CAN FD", {**UART, "TRANSPORT_CAN": 1, "CAN_FD": 1}, "UART"),
    ("Ethernet", {**UART, "TRANSPORT_ETH": 1}, "UART"),
    ("log", {**UART, "LOG_LEVEL": 3}, "UART"),
    ("commands", {**UART, "CMD_ENABLE": 1}, "UART"),
    ("verify+hash", {**UART, "INSPECT_VERIFY": 1, "INSPECT_HASH": 1}, "UART"),
    ("readback", {**UART, "INSPECT_VERIFY": 1, "INSPECT_HASH": 1, "READBACK": 1}, "verify+hash"),
    ("link", {**UART, "UART_LINK": 1}, "UART"),
    ("secure", {**UART, "UART_LINK": 2}, "UART"),
    ("encryption", {**UART, "UART_LINK": 2, "LINK_ENCRYPT": 1}, "secure"),
    ("encrypted images", {**UART, "ENCRYPT_IMAGES": 1}, "UART"),
    ("everything", {"TRANSPORT_UART": 1, "TRANSPORT_USB_CDC": 1, "TRANSPORT_USB_DFU": 1, "TRANSPORT_CAN": 1,
                    "CAN_FD": 1, "TRANSPORT_ETH": 1, "LOG_LEVEL": 3, "CMD_ENABLE": 1, "INSPECT_VERIFY": 1,
                    "INSPECT_HASH": 1, "READBACK": 1}, None),
]
# The same builds with link-time optimisation (UMCUB_CFG_LTO, experimental):
# name -> the variant it repeats.
LTO_OF = {"base LTO": "base", "UART LTO": "UART", "UART lite LTO": "UART lite", "everything LTO": "everything"}
_v = {v[0]: v for v in VARIANTS}
VARIANTS += [(n, _v[o][1], None, True) for n, o in LTO_OF.items()]

# README column names
LABELS = {"UART": "UART (+SMP)", "Ethernet": "Ethernet (+DHCP)", "log": "log (level 3)",
          "commands": "text commands", "UART lite": "UART, lite upload (no SMP)", "verify+hash": "verify + hash", "link": "umcub link (addressed)",
          "secure": "umcub link SECURE", "encryption": "+ link encryption"}
# Sectors the bootloader region needs: sector layouts from the start of flash
# (KiB), sizes taken from the measured row CHIPS[chip] - for series without a
# port an estimate from a build for the same core. None = page flash (any size,
# shown in KiB). The bootloader region also holds the 256-byte info block.
LAYOUTS = [
    ("STM32H7 (128 KiB sectors)", 0, [128] * 8, ""),
    ("STM32F2 / F4, F72x / F73x (16, 16, 16, 16, 64, 128 KiB ...)", 1, [16] * 4 + [64] + [128] * 7,
     "estimate: Cortex-M4 build"),
    ("STM32F74x ... F77x (32, 32, 32, 32, 128, 256 KiB ...)", 0, [32] * 4 + [128] + [256] * 7,
     "estimate: Cortex-M7 build"),
    ("STM32F1 (1 / 2 KiB pages)", 3, None, ""),
    ("STM32G4 / L4 (2 KiB pages)", 1, None, "estimate: Cortex-M4 build"),
]
SECTOR_SETS = ["UART lite", "UART", "USB CDC", "USB CDC+DFU", "CAN", "Ethernet", "secure", "everything"]
SECTOR_LABELS = {"UART lite": "UART, lite (no SMP)", "UART": "UART", "USB CDC": "UART + USB CDC", "USB CDC+DFU": "UART + USB CDC + DFU",
                 "CAN": "UART + CAN", "Ethernet": "UART + Ethernet", "secure": "UART, link SECURE",
                 "everything": "all on"}
README = ROOT / "README.md"
BEGIN, END = "<!-- size-table:begin -->", "<!-- size-table:end -->"
TRANSPORTS = ["UART", "USB CDC", "USB DFU", "USB CDC+DFU", "CAN", "CAN FD", "Ethernet", "DFU only", "UART lite"]
FEATURES = ["log", "commands", "verify+hash", "readback", "link", "secure", "encryption", "encrypted images"]


def build(out, ci, chip, vi, variant):
    _, core, pre, extra = chip[:4]
    board = chip[4] if len(chip) > 4 else BOARD
    unsupported = chip[5] if len(chip) > 5 else set()
    not_in_all = chip[6] if len(chip) > 6 else set()
    name, settings = variant[0], variant[1]
    lto = len(variant) > 3 and variant[3]
    if name.startswith("everything"):
        settings = {k: (0 if k in unsupported or k in not_in_all else v) for k, v in settings.items()}
    elif any(settings.get(k) for k in unsupported):
        return None, None                       # not available on this family
    d = out / f"c{ci}_v{vi}"
    d.mkdir(parents=True, exist_ok=True)
    post = d / "post.h"
    lines = [extra]
    for k, v in {**OFF, **settings}.items():
        lines.append(f"#undef UMCUB_CFG_{k}\n#define UMCUB_CFG_{k} {v}")
    post.write_text("\n".join(lines) + "\n")
    args = ["cmake", "-S", str(ROOT), "-B", str(d / "b"), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DUMCUB_BOARD={board}", f"-DUMCUB_CORE={core}", f"-DUMCUB_CONFIG_POST={post}",
            f"-DUMCUB_LTO={'ON' if lto else 'OFF'}"]
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
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--update", action="store_true", help="rewrite the tables in README.md")
    mode.add_argument("--check", action="store_true", help="exit 1 if README.md differs")
    a = ap.parse_args()
    out = Path(a.out)

    size = {}
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        jobs = {ex.submit(build, out, ci, c, vi, v): (ci, v[0])
                for ci, c in enumerate(CHIPS) for vi, v in enumerate(VARIANTS)}
        for fut in concurrent.futures.as_completed(jobs):
            n, log = fut.result()
            size[jobs[fut]] = n
            if n is None and log is not None:
                print(f"note: {CHIPS[jobs[fut][0]][0]} / {jobs[fut][1]} does not build (see {log})", file=sys.stderr)

    ref = {v[0]: v[2] for v in VARIANTS}

    def cell(ci, col):
        n, r = size.get((ci, col)), ref[col]
        if n is None or size.get((ci, r)) is None:
            return "—"
        return delta(n - size[(ci, r)])

    def table(cols, first):
        rows = ["| MCU | " + " | ".join(first + [LABELS.get(c, c) for c in cols]) + " |",
                "|---|" + "---:|" * (len(first) + len(cols))]
        for ci, c in enumerate(CHIPS):
            lead = [kb(size[(ci, "base")])] if "base" in first else []
            if "all on" in first:
                lead.append(kb(size[(ci, "everything")]) if size.get((ci, "everything")) else "—")
            rows.append(f"| {c[0]} | " + " | ".join(lead + [cell(ci, col) for col in cols]) + " |")
        return "\n".join(rows)

    def sectors(layout, n):
        need, used = n + 256, 0
        for i, s in enumerate(layout):
            used += s * 1024
            if used >= need:
                return i + 1
        return len(layout) + 1

    def sector_table():
        rows = ["| Flash layout | " + " | ".join(SECTOR_LABELS[c] for c in SECTOR_SETS) + " |",
                "|---|" + "---:|" * len(SECTOR_SETS)]
        for name, ci, layout, note in LAYOUTS:
            cells = []
            for col in SECTOR_SETS:
                n = size.get((ci, col))
                if n is None:
                    cells.append("—")
                elif layout is None:
                    cells.append(kb(n))
                else:
                    k = sectors(layout, n)
                    cells.append(f"{k} ({kb(n)})" + (" > 3" if k > 3 else ""))
            rows.append(f"| {name}{' — ' + note if note else ''} | " + " | ".join(cells) + " |")
        return "\n".join(rows)

    def lto_table():
        cols = {"base LTO": "base", "UART LTO": "UART (+SMP)", "UART lite LTO": "UART, lite upload (no SMP)",
                "everything LTO": "all on"}
        rows = ["| MCU, with LTO | " + " | ".join(cols.values()) + " |", "|---|" + "---:|" * len(cols)]
        for ci, c in enumerate(CHIPS):
            cells = []
            for col in cols:
                n, plain = size.get((ci, col)), size.get((ci, LTO_OF[col]))
                cells.append("—" if n is None or plain is None else f"{kb(n)} ({delta(n - plain)})")
            rows.append(f"| {c[0]} | " + " | ".join(cells) + " |")
        return "\n".join(rows)

    tables = (table(TRANSPORTS, ["base", "all on"]) + "\n\n" + table(FEATURES, []) + "\n\n" +
              sector_table() + "\n\n" + lto_table())
    if not (a.update or a.check):
        print(tables)
        return
    text = README.read_text()
    if BEGIN not in text or END not in text:
        sys.exit(f"{README}: markers {BEGIN} / {END} not found")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new_text = f"{head}{BEGIN}\n{tables}\n{END}{tail}"
    if a.check:
        if new_text != text:
            print("README.md size tables are stale - run tools/size_table.py --update")
            print(tables)
            sys.exit(1)
        print("README.md size tables are up to date")
    elif new_text != text:
        README.write_text(new_text)
        print("README.md size tables updated")

if __name__ == "__main__":
    main()
