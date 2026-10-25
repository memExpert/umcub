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

CASES = [   # name, probe, exit code, text in the output[, text in --header (a "#" after it: nothing more)]
    ("H7 USART3 PD8/PD9", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8, 7", rx="'D', 9, 7"), 0, "2 pin(s) fit"),
    ("H7 wrong AF", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8, 4", rx="'D', 9, 7"), 1,
     "UMCUB_CFG_UART_TX_PIN = PD8 AF4: not USART3_TX"),
    ("H7 package LQFP144", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8, 7", rx="'D', 9, 7") +
     ENTRY.format(p="'C', 13, 0") + PART.format(p="STM32H755ZIT6"), 0, "package LQFP144"),
    ("H743 LQFP100 without port G", '@@MCU "STM32H743xx"\n' + UART.format(i=1, tx="'A', 9, 7", rx="'A', 10, 7") +
     ENTRY.format(p="'G', 2, 0") + PART.format(p="STM32H743VIT6"), 1, "PG2: not brought out on STM32H743VIT6 (LQFP100)"),
    ("F103 LQFP48", '@@MCU "STM32F103xB"\n' + UART.format(i=1, tx="'A', 9, 0", rx="'A', 10, 0") +
     PART.format(p="STM32F103C8T6"), 0, "package LQFP48"),
    ("F103 LQFP48 without port E", '@@MCU "STM32F103xB"\n' + UART.format(i=1, tx="'A', 9, 0", rx="'A', 10, 0") +
     ENTRY.format(p="'E', 0, 0") + PART.format(p="STM32F103C8T6"), 1, "PE0: not brought out"),
    ("entry pin off: not checked", '@@MCU "STM32F103xB"\n@@ "UMCUB_CFG_ENTRY_GPIO" 0\n'
     '@@ "UMCUB_CFG_ENTRY_GPIO_PIN" PIN \'E\', 0, 0\n' + PART.format(p="STM32F103C8T6"), 0, "fit"),
    ("SMPS part: its own package file", '@@MCU "STM32H7A3xxQ"\n' + ENTRY.format(p="'I', 0, 0") +
     PART.format(p="STM32H7A3IIK6Q"), 1, "PI0: not brought out on STM32H7A3IIK6Q"),
    ("same pin, non-SMPS part", '@@MCU "STM32H7A3xx"\n' + ENTRY.format(p="'I', 0, 0") +
     PART.format(p="STM32H7A3IIK6"), 0, "fit"),
    ("analog-switch pad PC2_C is not PC2", '@@MCU "STM32H755xx"\n' + ENTRY.format(p="'C', 2, 0") +
     PART.format(p="STM32H755ZIT6"), 1, "PC2: not brought out"),
    ("status LED on the UART TX pin", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8, 7", rx="'D', 9, 7") +
     '@@ "UMCUB_CFG_STATUS_LED" 1\n@@ "UMCUB_CFG_STATUS_LED_PIN" PIN \'D\', 8, 0\n', 1,
     "PD8 is both UMCUB_CFG_UART_TX_PIN and UMCUB_CFG_STATUS_LED_PIN"),
    ("entry button on USB D+", '@@MCU "STM32F103xB"\n@@ "UMCUB_CFG_TRANSPORT_USB_CDC" 1\n' + ENTRY.format(p="'A', 12, 0"),
     1, "PA12 is both"),
    # pins without an AF: filled in from the pin data, written to the header
    ("H7 USART3 PD8/PD9 without AF", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8", rx="'D', 9"), 0,
     "AF filled in: UART_TX_PIN PD8 AF7, UART_RX_PIN PD9 AF7",
     "#define UMCUB_CFG_UART_TX_PIN UMCUB_PIN('D', 8, 7)"),
    ("F411 USART1 PA9/PA10 without AF", '@@MCU "STM32F411xE"\n' + UART.format(i=1, tx="'A', 9", rx="'A', 10") +
     PART.format(p="STM32F411CEU6"), 0, "PA9 AF7", "#define UMCUB_CFG_UART_RX_PIN UMCUB_PIN('A', 10, 7)"),
    ("H7 USART3 on a pin without it", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'A', 9", rx="'D', 9"), 1,
     "UMCUB_CFG_UART_TX_PIN = PA9: no USART3_TX on this pin"),
    ("H7 CAN and RMII without AF", '@@MCU "STM32H755xx"\n@@ "UMCUB_CFG_TRANSPORT_CAN" 1\n@@ "UMCUB_CFG_CAN_DRIVER" 0\n'
     '@@ "UMCUB_CFG_CAN_INSTANCE" 1\n@@ "UMCUB_CFG_CAN_TX_PIN" PIN \'D\', 1\n@@ "UMCUB_CFG_CAN_RX_PIN" PIN \'D\', 0\n'
     '@@ "UMCUB_CFG_TRANSPORT_ETH" 1\n@@ "UMCUB_CFG_ETH_DRIVER" 0\n'
     '@@ "UMCUB_CFG_ETH_RMII_PINS" PIN \'A\', 1, PIN \'A\', 2, PIN \'G\', 13, 11\n', 0, "CAN_TX_PIN PD1 AF9",
     "#define UMCUB_CFG_ETH_RMII_PINS UMCUB_PIN('A', 1, 11), UMCUB_PIN('A', 2, 11), UMCUB_PIN('G', 13, 11)"),
    ("CAN by the board file: AF where the pin has it", '@@MCU "STM32H755xx"\n@@ "UMCUB_CFG_TRANSPORT_CAN" 1\n'
     '@@ "UMCUB_CFG_CAN_DRIVER" 1\n@@ "UMCUB_CFG_CAN_INSTANCE" 1\n@@ "UMCUB_CFG_CAN_TX_PIN" PIN \'D\', 1\n'
     '@@ "UMCUB_CFG_CAN_RX_PIN" PIN \'E\', 2\n', 0, "fit",
     "#define UMCUB_CFG_CAN_TX_PIN UMCUB_PIN('D', 1, 9)\n#undef UMCUB_CFG_CAN_RX_PIN\n"
     "#define UMCUB_CFG_CAN_RX_PIN UMCUB_PIN('E', 2, 0)"),
    ("software DE and GPIOs: AF 0", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8, 7", rx="'D', 9, 7") +
     '@@ "UMCUB_CFG_UART_DE_PIN" PIN \'A\', 8\n' + ENTRY.format(p="'C', 13"), 0, "fit",
     "#define UMCUB_CFG_UART_DE_PIN UMCUB_PIN('A', 8, 0)"),
    ("F103 without AF numbers: AF 0", '@@MCU "STM32F103xB"\n' + UART.format(i=1, tx="'A', 9", rx="'A', 10"), 0,
     "fit", "#define UMCUB_CFG_UART_TX_PIN UMCUB_PIN('A', 9, 0)"),
    ("all AFs given: empty header", '@@MCU "STM32H755xx"\n' + UART.format(i=3, tx="'D', 8, 7", rx="'D', 9, 7"), 0,
     "2 pin(s) fit", "Do not edit. */\n#"),
    ("part of another line", '@@MCU "STM32H743xx"\n' + PART.format(p="STM32F103C8T6"), 1, "not a part of STM32H743xx"),
    ("unknown part", '@@MCU "STM32H743xx"\n' + PART.format(p="STM32H743QQT6"), 1, "no such part"),
]


def main():
    failed = 0
    with tempfile.TemporaryDirectory() as d:
        for name, probe, want_rc, want, *header in CASES:
            path, hdr = os.path.join(d, "pins.txt"), os.path.join(d, "umcub_pins.h")
            with open(path, "w") as f:
                f.write(probe)
            if os.path.exists(hdr):
                os.remove(hdr)
            r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/build/check_pins.py"), path, "--header", hdr],
                               capture_output=True, text=True)
            out = " ".join((r.stdout + r.stderr).split())
            text = open(hdr).read() if os.path.exists(hdr) else ""
            if header and header[0].endswith("#"):          # nothing after the comment
                bad = not (text.rstrip() + "\n#").endswith(header[0])
            else:
                bad = bool(header) and header[0] not in text
            if r.returncode != want_rc or want not in out or bad:
                print(f"FAIL {name}: exit {r.returncode} (want {want_rc}), output: {out}" +
                      (f"\n  header: {text}" if header else ""))
                failed += 1
    print(f"{len(CASES) - failed}/{len(CASES)} pin check cases" + (" FAILED" if failed else " passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
