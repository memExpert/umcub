#!/usr/bin/env python3
"""A HardFault in the bootloader resets the MCU (SWD through probe.py); with the status LED it is lit first.

  hardfault_test.py --tree build/h755-cm7

1. A request in the handoff area keeps the bootloader in recovery after a reset.
2. The request is written again, then the core is sent to an address without memory
   (instruction bus error -> HardFault).
3. Status LED (UMCUB_CFG_STATUS_LED): lit right after the fault, the request still there.
4. Within 4 s the bootloader has started again and consumed the request (a reset happened).
Three rounds (the LED blinks in recovery, so it is lit by chance in some of them). Afterwards a plain
reset boots the application again.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools", "hw"))
import probe  # noqa: E402

REQUEST_MAGIC, REQ_ENTER_BOOT = 0x52514255, 1
NO_MEMORY = 0x00100000                  # reserved on every supported part: executing there faults
# (GPIOA base, port stride, ODR offset) by part prefix
GPIO = {"STM32F1": (0x40010800, 0x400, 0x0C), "STM32F4": (0x40020000, 0x400, 0x14),
        "STM32G4": (0x48000000, 0x400, 0x14), "STM32H7": (0x58020000, 0x400, 0x14)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True, help="build tree of the bootloader on the board")
    probe.add_arguments(ap)
    a = ap.parse_args()
    with open(os.path.join(a.tree, "umcub_kconfig", "umcub_config.json")) as f:
        v = json.load(f)["values"]
    req = v["SHARED_RAM_ADDR"]
    request = [REQUEST_MAGIC, REQ_ENTER_BOOT, 0, ~REQUEST_MAGIC & 0xFFFFFFFF ^ REQ_ENTER_BOOT]
    led = None
    if v.get("STATUS_LED"):
        pin = v["STATUS_LED_PIN"]                            # "UMCUB_PIN('B', 0)"
        port, num = ord(pin.split("'")[1]) - ord("A"), int(pin.split(",")[1].strip(" )"))
        base, stride, odr = GPIO[v["MCU_PART"][:7]]
        led = (base + port * stride + odr, num, bool(v.get("STATUS_LED_ACTIVE")))
    p = probe.open_probe(a)
    ok = True
    try:
        for _ in range(3):
            p.run_steps([("write32", req, request), ("reset", False)])
            time.sleep(1.5)                                     # in recovery, SysTick running
            if p.read32(req, 1)[0] == REQUEST_MAGIC:
                sys.exit("FAIL: the bootloader did not consume the recovery request (not running?)")
            p.run_steps([("write32", req, request), ("pc", NO_MEMORY)])
            if led:
                addr, num, active = led
                lit = ((p.read32(addr, 1)[0] >> num) & 1) == active
                still = p.read32(req, 1)[0] == REQUEST_MAGIC
                print(f"status LED lit after the fault: {lit}, not reset yet: {still}")
                ok = ok and lit and still
            t0 = time.time()
            while p.read32(req, 1)[0] == REQUEST_MAGIC:
                if time.time() - t0 > 4:
                    print("FAIL: no reset within 4 s of the fault (stuck in a fault handler?)")
                    sys.exit(1)
                time.sleep(0.2)
            print(f"reset after the fault: yes (seen {time.time() - t0:.1f} s after the first check)")
        p.run_steps([("reset", False)])                     # back to the application
        if not ok:
            sys.exit("FAIL: status LED not lit while the fault handler waits")
        print("OK")
    finally:
        p.close()


if __name__ == "__main__":
    main()
