#!/usr/bin/env python3
"""Self-test of tools/build/check_pins.py: probes as CMake writes them, each
with the exit code and the message it must give (run by the build matrix)."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UART = '@@ "UMCUB_CFG_TRANSPORT_UART" 1\n@@ "UMCUB_CFG_UART_INSTANCE" {i}\n' \
       '@@ "UMCUB_CFG_UART_TX_PIN" PIN {tx}\n@@ "UMCUB_CFG_UART_RX_PIN" PIN {rx}\n'
ENTRY = '@@ "UMCUB_CFG_ENTRY_GPIO" 1\n@@ "UMCUB_CFG_ENTRY_GPIO_PIN" PIN {p}\n'
PART = '@@ "UMCUB_CFG_MCU_PART" "{p}"\n'

CASES = [   # name, probe, exit code, text in the output
    ("H7 USART3 PD8/PD9", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D' 8 7", rx="'D' 9 7"), 0, "2 pin(s) fit"),
    ("H7 wrong AF", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D' 8 4", rx="'D' 9 7"), 1,
     "UMCUB_CFG_UART_TX_PIN = PD8 AF4: not USART3_TX"),
    ("H7 package LQFP144", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D' 8 7", rx="'D' 9 7") +
     ENTRY.format(p="'C' 13 0") + PART.format(p="STM32H755ZIT6"), 0, "package LQFP144"),
    ("H743 LQFP100 without port G", '@@MCU "STM32H743xx"\n' + UART.format(i=1, tx="'A' 9 7", rx="'A' 10 7") +
     ENTRY.format(p="'G' 2 0") + PART.format(p="STM32H743VIT6"), 1, "PG2: not brought out on STM32H743VIT6 (LQFP100)"),
    ("F103 LQFP48", '@@MCU "STM32F103xB"\n' + UART.format(i=1, tx="'A' 9 0", rx="'A' 10 0") +
     PART.format(p="STM32F103C8T6"), 0, "package LQFP48"),
    ("F103 LQFP48 without port E", '@@MCU "STM32F103xB"\n' + UART.format(i=1, tx="'A' 9 0", rx="'A' 10 0") +
     ENTRY.format(p="'E' 0 0") + PART.format(p="STM32F103C8T6"), 1, "PE0: not brought out"),
    ("entry pin off: not checked", '@@MCU "STM32F103xB"\n@@ "UMCUB_CFG_ENTRY_GPIO" 0\n'
     '@@ "UMCUB_CFG_ENTRY_GPIO_PIN" PIN \'E\' 0 0\n' + PART.format(p="STM32F103C8T6"), 0, "fit"),
    ("part of another line", '@@MCU "STM32H743xx"\n' + PART.format(p="STM32F103C8T6"), 1, "not a part of STM32H743xx"),
    ("unknown part", '@@MCU "STM32H743xx"\n' + PART.format(p="STM32H743QQT6"), 1, "no such part"),
]


def main():
    failed = 0
    with tempfile.TemporaryDirectory() as d:
        for name, probe, want_rc, want in CASES:
            path = os.path.join(d, "pins.txt")
            with open(path, "w") as f:
                f.write(probe)
            r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/build/check_pins.py"), path],
                               capture_output=True, text=True)
            out = " ".join((r.stdout + r.stderr).split())
            if r.returncode != want_rc or want not in out:
                print(f"FAIL {name}: exit {r.returncode} (want {want_rc}), output: {out}")
                failed += 1
    print(f"{len(CASES) - failed}/{len(CASES)} pin check cases" + (" FAILED" if failed else " passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
