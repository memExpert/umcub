#!/usr/bin/env python3
"""Bootloader flash size per MCU and the cost of every transport / feature.

Builds the reference boards' bootloaders in many variants (Release, -Os) and
prints the Markdown tables of docs/size.md:

  tools/build/size_table.py [--out build/sizes] [-j 4]     print the tables
  tools/build/size_table.py --update                       rewrite them in docs/size.md
  tools/build/size_table.py --check                        exit 1 if they are stale

Base = MCUboot + ECDSA-P256 validation + jump: no transport, no log, no text
commands, no inspection. Every other row is what one item adds (size =
.text + .data, flash). SMP (MCUboot's boot_serial + zcbor) comes once with the
first SMP transport; it and the UART driver are told apart by building CAN
alone and UART + CAN (without CAN: USB CDC alone and UART + USB CDC). Transports and features are measured on top of base +
SMP + UART.
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = "nucleo_h755zi_q"

# name, core, PRE overlay, extra POST lines, board
CHIPS = [
    ("STM32H755 CM7 (2 images, SINGLE_BOOT)", "cm7", None, ""),
    # The board gives the CM4 instance only CAN; borrow the CM7 pins to size the rest
    # (status LED: LD2, the default PC13 is the B1 button there).
    ("STM32H755 CM4 (PER_CORE)", "cm4", "boards/nucleo_h755zi_q/variants/per_core.h",
     "#define UMCUB_CFG_STATUS_LED_PIN UMCUB_PIN('E', 1, 0)\n#define UMCUB_CFG_UART_INSTANCE 3\n#define UMCUB_CFG_UART_BAUD 115200\n"
     "#define UMCUB_CFG_UART_TX_PIN UMCUB_PIN('D', 8, 7)\n#define UMCUB_CFG_UART_RX_PIN UMCUB_PIN('D', 9, 7)\n"
     "#define UMCUB_CFG_ETH_PHY_ADDR 0\n#define UMCUB_CFG_ETH_RMII_PINS UMCUB_PIN('A', 1, 11), UMCUB_PIN('A', 2, 11), "
     "UMCUB_PIN('A', 7, 11), UMCUB_PIN('C', 1, 11), UMCUB_PIN('C', 4, 11), UMCUB_PIN('C', 5, 11), "
     "UMCUB_PIN('G', 11, 11), UMCUB_PIN('G', 13, 11), UMCUB_PIN('B', 13, 11)\n"),
    ("STM32H743 / H753 (single core)", "cm7", "tests/configs/feature/single_core.h",
     "#undef UMCUB_CFG_MCU\n#define UMCUB_CFG_MCU STM32H743xx\n"
     "#undef UMCUB_CFG_MCU_PART\n#define UMCUB_CFG_MCU_PART \"STM32H743ZIT6\"\n"
     "#undef UMCUB_CFG_PWR_SUPPLY\n#define UMCUB_CFG_PWR_SUPPLY UMCUB_H7_SUPPLY_LDO\n"),
    # Cortex-M3, sized in the 44 KiB bootloader region of the USB layout; no
    # CAN FD and no Ethernet on this part; "all on" without CAN (shares its
    # SRAM with USB on the F103).
    ("STM32F103 (Blue Pill, overwrite)", "", None, f'#include "{ROOT}/boards/bluepill_f103c8/variants/usb.h"\n',
     "bluepill_f103c8", {"CAN_FD", "TRANSPORT_ETH"}, {"TRANSPORT_CAN"}),
    # Cortex-M4, in the 48 KiB region of the USB layout; no CAN and no Ethernet on the F411.
    ("STM32F411 (Black Pill, overwrite)", "cm4", None, f'#include "{ROOT}/boards/blackpill_f411ce/variants/usb.h"\n',
     "blackpill_f411ce", {"TRANSPORT_CAN", "CAN_FD", "TRANSPORT_ETH"}, set()),
]

OFF = {"TRANSPORT_UART": 0, "TRANSPORT_USB_CDC": 0, "TRANSPORT_USB_DFU": 0, "TRANSPORT_CAN": 0,
       "CAN_FD": 0, "CAN_LOOPBACK": 0, "TRANSPORT_ETH": 0, "LOG_LEVEL": 0, "CMD_ENABLE": 0,
       "INSPECT_VERIFY": 0, "INSPECT_HASH": 0, "READBACK": 0, "STATUS_LED": 0}
U = {"TRANSPORT_UART": 1}
CAN = {"TRANSPORT_CAN": 1}

# name, settings on top of OFF
VARIANTS = [
    ("base", {}),
    ("UART", U),
    ("CAN only", CAN),
    ("UART+CAN", {**U, **CAN}),
    ("USB CDC", {**U, "TRANSPORT_USB_CDC": 1}),
    ("USB DFU", {**U, "TRANSPORT_USB_DFU": 1}),
    ("CAN FD", {**U, **CAN, "CAN_FD": 1}),
    ("Ethernet", {**U, "TRANSPORT_ETH": 1}),
    ("USB CDC only", {"TRANSPORT_USB_CDC": 1}),
    ("Ethernet only", {"TRANSPORT_ETH": 1}),
    ("UART lite", {**U, "SMP_ENABLE": 0}),
    ("log", {**U, "LOG_LEVEL": 3}),
    ("commands", {**U, "CMD_ENABLE": 1}),
    ("status LED", {**U, "STATUS_LED": 1}),
    ("verify+hash", {**U, "INSPECT_VERIFY": 1, "INSPECT_HASH": 1}),
    ("readback", {**U, "INSPECT_VERIFY": 1, "INSPECT_HASH": 1, "READBACK": 1}),
    ("link", {**U, "UART_LINK": 1}),
    ("secure", {**U, "UART_LINK": 2}),
    ("encryption", {**U, "UART_LINK": 2, "LINK_ENCRYPT": 1}),
    ("encrypted images", {**U, "ENCRYPT_IMAGES": 1}),
    ("everything", {"TRANSPORT_UART": 1, "TRANSPORT_USB_CDC": 1, "TRANSPORT_USB_DFU": 1, "TRANSPORT_CAN": 1,
                    "CAN_FD": 1, "TRANSPORT_ETH": 1, "LOG_LEVEL": 3, "CMD_ENABLE": 1, "INSPECT_VERIFY": 1,
                    "INSPECT_HASH": 1, "READBACK": 1, "STATUS_LED": 1}),
]
# The same builds with link-time optimisation (UMCUB_CFG_LTO, experimental):
# name -> the variant it repeats.
LTO_OF = {"base LTO": "base", "UART LTO": "UART", "everything LTO": "everything"}
_v = dict(VARIANTS)
VARIANTS += [(n, _v[o], True) for n, o in LTO_OF.items()]

# Rows of the cost table: label, then the variants added and subtracted
# ("uart" = the UART driver alone: UART+CAN - CAN only).
ROWS = [
    ("**base**: MCUboot, signature check, jump", ["base"], []),
    ("+ SMP (recovery protocol: once, with the first SMP transport)", ["UART"], ["base", "uart"]),
    ("+ UART", ["uart"], []),
    ("+ USB CDC", ["USB CDC"], ["UART"]),
    ("+ USB DFU (needs no SMP)", ["USB DFU"], ["UART"]),
    ("+ CAN", ["UART+CAN"], ["UART"]),
    ("+ CAN FD (on top of CAN)", ["CAN FD"], ["UART+CAN"]),
    ("+ Ethernet (IPv4, UDP, DHCP)", ["Ethernet"], ["UART"]),
    ("+ lite upload (instead of SMP)", ["UART lite"], ["base", "uart"]),
    ("+ log (level 3)", ["log"], ["UART"]),
    ("+ text commands", ["commands"], ["UART"]),
    ("+ status LED", ["status LED"], ["UART"]),
    ("+ verify + hash", ["verify+hash"], ["UART"]),
    ("+ readback (on top of verify + hash)", ["readback"], ["verify+hash"]),
    ("+ umcub link (addressed)", ["link"], ["UART"]),
    ("+ umcub link SECURE", ["secure"], ["UART"]),
    ("+ link encryption (on top of SECURE)", ["encryption"], ["secure"]),
    ("+ encrypted images", ["encrypted images"], ["UART"]),
    ("**all on**", ["everything"], []),
]
# The bootloader region (UMCUB_CFG_BOOT_SIZE) a set of transports needs on
# each ported family: the measured size of CHIPS[chip] plus the 256-byte info
# block, rounded up to the erase units from the start of flash (KiB).
LAYOUTS = [
    ("STM32H7 (128 KiB sectors)", 0, [128] * 16),
    ("STM32F4 (16, 16, 16, 16, 64, 128 KiB ...)", 4, [16] * 4 + [64] + [128] * 7),
    ("STM32F1 (1 KiB pages; 2 KiB on high-density parts)", 3, [1] * 1024),
]
SECTOR_SETS = ["UART lite", "UART", "USB CDC only", "CAN only", "Ethernet only", "secure", "everything"]
SECTOR_LABELS = {"UART lite": "UART, lite (no SMP)", "UART": "UART", "USB CDC only": "USB CDC",
                 "CAN only": "CAN", "Ethernet only": "Ethernet", "secure": "UART, link SECURE",
                 "everything": "all on"}
DOC = ROOT / "docs/size.md"
BEGIN, END = "<!-- size-table:begin -->", "<!-- size-table:end -->"


def build(out, ci, chip, vi, variant):
    _, core, pre, extra = chip[:4]
    board = chip[4] if len(chip) > 4 else BOARD
    unsupported = chip[5] if len(chip) > 5 else set()
    not_in_all = chip[6] if len(chip) > 6 else set()
    name, settings = variant[0], variant[1]
    lto = len(variant) > 2 and variant[2]
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
    if abs(n) < 50:
        return "0"
    return f"+{n / 1024:.1f} K" if n >= 0 else f"{n / 1024:.1f} K"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "build/sizes"))
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 4) // 4))
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--update", action="store_true", help="rewrite the tables in docs/size.md")
    mode.add_argument("--check", action="store_true", help="exit 1 if docs/size.md differs")
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

    def value(ci, name):
        if name == "uart":                      # the UART driver: next to CAN, or (no CAN) next to USB CDC
            for with_uart, without in (("UART+CAN", "CAN only"), ("USB CDC", "USB CDC only")):
                a_, b_ = size.get((ci, with_uart)), size.get((ci, without))
                if a_ is not None and b_ is not None:
                    return a_ - b_
            return None
        return size.get((ci, name))

    def cost_table():
        rows = ["| | " + " | ".join(c[0] for c in CHIPS) + " |", "|---|" + "---:|" * len(CHIPS)]
        for label, plus, minus in ROWS:
            cells = []
            for ci in range(len(CHIPS)):
                vals = [value(ci, n) for n in plus + minus]
                if any(v is None for v in vals):
                    cells.append("—")
                    continue
                n = sum(vals[:len(plus)]) - sum(vals[len(plus):])
                cells.append(delta(n) if minus or label.startswith("+") else kb(n))
            rows.append(f"| {label} | " + " | ".join(cells) + " |")
        return "\n".join(rows)

    def region(layout, n):
        need, used = n + 256, 0
        for s in layout:
            used += s
            if used * 1024 >= need:
                return used
        return None

    def sector_table():
        rows = ["| Bootloader size → `UMCUB_CFG_BOOT_SIZE` | " + " | ".join(SECTOR_LABELS[c] for c in SECTOR_SETS) + " |",
                "|---|" + "---:|" * len(SECTOR_SETS)]
        for name, ci, layout in LAYOUTS:
            cells = []
            for col in SECTOR_SETS:
                n = size.get((ci, col))
                r = None if n is None else region(layout, n)
                cells.append("—" if r is None else f"{kb(n)} → {r} K")
            rows.append(f"| {name} | " + " | ".join(cells) + " |")
        return "\n".join(rows)

    def lto_table():
        cols = {"base LTO": "base", "UART LTO": "base + SMP + UART", "everything LTO": "all on"}
        rows = ["| with LTO | " + " | ".join(c[0] for c in CHIPS) + " |", "|---|" + "---:|" * len(CHIPS)]
        for col, label in cols.items():
            cells = []
            for ci in range(len(CHIPS)):
                n, plain = size.get((ci, col)), size.get((ci, LTO_OF[col]))
                cells.append("—" if n is None or plain is None else f"{kb(n)} ({delta(n - plain)})")
            rows.append(f"| {label} | " + " | ".join(cells) + " |")
        return "\n".join(rows)

    tables = cost_table() + "\n\n" + sector_table() + "\n\n" + lto_table()
    if not (a.update or a.check):
        print(tables)
        return
    text = DOC.read_text()
    if BEGIN not in text or END not in text:
        sys.exit(f"{DOC}: markers {BEGIN} / {END} not found")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new_text = f"{head}{BEGIN}\n{tables}\n{END}{tail}"
    if a.check:
        if new_text != text:
            print("docs/size.md size tables are stale - run tools/build/size_table.py --update")
            print(tables)
            sys.exit(1)
        print("docs/size.md size tables are up to date")
    elif new_text != text:
        DOC.write_text(new_text)
        print("docs/size.md size tables updated")

if __name__ == "__main__":
    main()
