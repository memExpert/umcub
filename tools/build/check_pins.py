#!/usr/bin/env python3
"""Check the pins of a bootloader configuration against ST's pin data
(third_party/st/stm32_open_pin_data, the STM32CubeMX database).

  check_pins.py <pins.txt> [--data DIR]

<pins.txt> is the preprocessed probe CMake writes (cmake/umcub_config.cmake):
"@@MCU STM32H755xx" and one "@@ <option> PIN '<port>' <num> <af>" line per
configured pin, "@@ <option> <value>" for the options that select a pin's
signal (instance, enabled transports, drivers). For every pin the expected
signal (e.g. USART3_TX for UMCUB_CFG_UART_TX_PIN with UMCUB_CFG_UART_INSTANCE
3) must exist on that pin with that alternate function number; otherwise the
pins that carry the signal are listed. Families without AF numbers (F1:
AFIO remap) are checked by their port at build time instead.
Exit 0 = fine (or not checkable: a note), 1 = wrong pins.
"""
import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NONE = 0xFFFFFFFF


def parse_probe(path):
    mcu, opts = None, {}
    for line in open(path):
        m = re.match(r'@@MCU\s+"?([A-Za-z0-9_]+)"?', line)
        if m:
            mcu = m.group(1)
            continue
        m = re.match(r'@@\s+"?(UMCUB_CFG_\w+)"?\s+(.*)$', line.strip())
        if not m:
            continue
        name, val = m.groups()
        pins = re.findall(r"PIN\s+'([A-K])'\s+(\d+)\s+(\d+)", val)
        if pins:
            opts[name] = [(p, int(n), int(af)) for p, n, af in pins]
        elif val != name:
            opts[name] = val.strip()
    return mcu, opts


def children(el, tag):
    """Direct children / all descendants named `tag`, whatever the XML namespace."""
    return [c for c in el if c.tag.rsplit("}", 1)[-1] == tag]


def descendants(el, tag):
    return [c for c in el.iter() if c.tag.rsplit("}", 1)[-1] == tag]


def gpio_modes(data, mcu):
    """Signal -> {(pin name, AF)} of the GPIO data of every part of `mcu`;
    None if the family has no AF numbers. `mcu` is the CMSIS define
    (STM32H755xx, STM32F411xE): the part names start with it up to the x."""
    prefix = re.sub(r"x.*$", "", mcu)
    versions = set()
    for f in os.listdir(os.path.join(data, "mcu")):
        if not f.startswith(prefix) or not f.endswith(".xml"):
            continue
        root = ET.parse(os.path.join(data, "mcu", f)).getroot()
        for ip in children(root, "IP"):
            if ip.get("Name") == "GPIO":
                versions.add(ip.get("Version"))
    if not versions:
        return None, f"no part {prefix}* in the pin data"
    signals, has_af = {}, False
    for v in sorted(versions):
        path = os.path.join(data, "mcu", "IP", f"GPIO-{v}_Modes.xml")
        root = ET.parse(path).getroot()
        for pin in descendants(root, "GPIO_Pin"):
            name = re.match(r"P[A-K]\d+", pin.get("Name") or "")
            if not name:
                continue
            for sig in children(pin, "PinSignal"):
                for val in descendants(sig, "PossibleValue"):
                    m = re.match(r"GPIO_AF(\d+)_", val.text or "")
                    if m:
                        has_af = True
                        signals.setdefault(sig.get("Name"), set()).add((name.group(0), int(m.group(1))))
    return (signals if has_af else None), ", ".join(sorted(versions))


def expected(opts):
    """(option, pin, [signal names that fit]) for every configured pin."""
    out = []
    on = lambda n: opts.get(n, "0") not in ("0", "")   # noqa: E731
    pins = lambda n: opts[n] if isinstance(opts.get(n), list) else []   # noqa: E731  UMCUB_PIN_NONE: none
    port_driver = lambda n: opts.get(n, "0") == "0"   # noqa: E731  UMCUB_DRIVER_PORT
    if on("UMCUB_CFG_TRANSPORT_UART"):
        i = opts.get("UMCUB_CFG_UART_INSTANCE", "1")
        for o, s in (("TX", "TX"), ("RX", "RX")):
            for p in pins(f"UMCUB_CFG_UART_{o}_PIN"):
                out.append((f"UMCUB_CFG_UART_{o}_PIN", p, [f"USART{i}_{s}", f"UART{i}_{s}", f"LPUART{i}_{s}"]))
        for p in pins("UMCUB_CFG_UART_DE_PIN"):
            if p[2]:                                   # AF 0: a GPIO switched by software
                out.append(("UMCUB_CFG_UART_DE_PIN", p, [f"USART{i}_DE", f"USART{i}_RTS", f"UART{i}_DE",
                                                         f"UART{i}_RTS"]))
    if on("UMCUB_CFG_TRANSPORT_CAN") and port_driver("UMCUB_CFG_CAN_DRIVER"):
        i = opts.get("UMCUB_CFG_CAN_INSTANCE", "1")
        for o in ("TX", "RX"):
            for p in pins(f"UMCUB_CFG_CAN_{o}_PIN"):
                out.append((f"UMCUB_CFG_CAN_{o}_PIN", p, [f"FDCAN{i}_{o}", f"CAN{i}_{o}", f"CAN_{o}"]))
    if on("UMCUB_CFG_TRANSPORT_ETH") and port_driver("UMCUB_CFG_ETH_DRIVER"):
        for p in pins("UMCUB_CFG_ETH_RMII_PINS"):
            out.append(("UMCUB_CFG_ETH_RMII_PINS", p, ["ETH_*"]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("probe")
    ap.add_argument("--data", default=os.path.join(ROOT, "third_party/st/stm32_open_pin_data"))
    a = ap.parse_args()
    mcu, opts = parse_probe(a.probe)
    if not os.path.isdir(os.path.join(a.data, "mcu")):
        print("umcub: pin check skipped - no pin data (git submodule update --init --depth 1 "
              "third_party/st/stm32_open_pin_data)")
        return 0
    signals, info = gpio_modes(a.data, mcu)
    if signals is None:
        print(f"umcub: pin check: {mcu} has no AF numbers ({info}) - checked by the port")
        return 0
    errors = []
    checks = expected(opts)
    for opt, (port, num, af), names in checks:
        pin = f"P{port}{num}"
        fit = {n for n in signals for pat in names
               if (n == pat or (pat.endswith("*") and n.startswith(pat[:-1])))}
        if any((pin, af) in signals[n] for n in fit):
            continue
        wanted = " / ".join(sorted(fit)) or " / ".join(names)
        if not fit:
            errors.append(f"{opt} = {pin} AF{af}: {mcu} has no {wanted}")
            continue
        here = sorted(f"{n} AF{a_}" for n in fit for p_, a_ in signals[n] if p_ == pin)
        where = "; ".join(f"{n}: " + ", ".join(f"{p_} (AF{a_})" for p_, a_ in sorted(signals[n]))
                          for n in sorted(fit) if not names[0].endswith("*"))
        errors.append(f"{opt} = {pin} AF{af}: not {wanted} on {mcu}" +
                      (f" (this pin has {', '.join(here)})" if here else "") + (f"\n    {where}" if where else ""))
    if errors:
        print("umcub: pins that do not fit the part (ST pin data, " + info + "):\n  " + "\n  ".join(errors))
        return 1
    print(f"umcub: pin check: {len(checks)} pin(s) fit {mcu} (ST pin data)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
