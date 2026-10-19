#!/usr/bin/env python3
"""Check the pins of a bootloader configuration against ST's pin data
(third_party/st/stm32_open_pin_data, the STM32CubeMX database).

  check_pins.py <pins.txt> [--data DIR]

<pins.txt> is the preprocessed probe CMake writes (cmake/umcub_config.cmake):
"@@MCU STM32H755xx" and one "@@ <option> PIN '<port>' <num> <af>" line per
configured pin, "@@ <option> <value>" for the options that select a pin's
signal (instance, enabled transports, drivers) and for UMCUB_CFG_MCU_PART.
For every pin the expected signal (e.g. USART3_TX for UMCUB_CFG_UART_TX_PIN
with UMCUB_CFG_UART_INSTANCE 3) must exist on that pin with that alternate
function number; otherwise the pins that carry the signal are listed.
Families without AF numbers (F1: AFIO remap) are checked by their port at
build time instead. With UMCUB_CFG_MCU_PART (the part number, e.g.
STM32F103C8T6) every pin in use must also be brought out by that package.
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
            opts[name] = val.strip().strip('"')
    return mcu, opts


def children(el, tag):
    """Direct children / all descendants named `tag`, whatever the XML namespace."""
    return [c for c in el if c.tag.rsplit("}", 1)[-1] == tag]


def descendants(el, tag):
    return [c for c in el.iter() if c.tag.rsplit("}", 1)[-1] == tag]


def find_part(data, part):
    """The pin data file of a part number: ST names them with ranges and
    wildcards (STM32F103C(8-B)Tx.xml matches STM32F103C8T6)."""
    for f in sorted(os.listdir(os.path.join(data, "mcu"))):
        if not f.endswith(".xml"):
            continue
        pat = re.sub(r"\(([A-Z0-9-]+)\)", lambda m: "[" + m.group(1).replace("-", "") + "]", f[:-4]).replace("x", ".")
        if re.match(pat, part):
            return f
    return None


def package_pins(data, f):
    """(package, {pin names}) of a part file; "PC14-OSC32_IN" counts as PC14."""
    root = ET.parse(os.path.join(data, "mcu", f)).getroot()
    pins = {m.group(0) for p in children(root, "Pin") for m in [re.match(r"P[A-K]\d+", p.get("Name") or "")] if m}
    return root.get("Package"), pins


def gpio_modes(data, mcu, files=None):
    """Signal -> {(pin name, AF)} of the GPIO data of every part of `mcu` (or
    of `files` only); None if the family has no AF numbers. `mcu` is the CMSIS
    define (STM32H755xx, STM32F411xE): the part names start with it up to the x."""
    prefix = re.sub(r"x.*$", "", mcu)
    versions = set()
    for f in files or os.listdir(os.path.join(data, "mcu")):
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


def used_pins(opts):
    """(option, pin) of every pin the configuration drives."""
    out = [(o, p) for o, p, _ in expected(opts)]
    on = lambda n: opts.get(n, "0") not in ("0", "")   # noqa: E731
    for o, en in (("UMCUB_CFG_UART_DE_PIN", "UMCUB_CFG_TRANSPORT_UART"), ("UMCUB_CFG_ENTRY_GPIO_PIN", "UMCUB_CFG_ENTRY_GPIO")):
        if on(en) and isinstance(opts.get(o), list):
            out += [(o, p) for p in opts[o] if (o, p) not in out]
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
    errors, notes, files = [], [], None
    part = opts.get("UMCUB_CFG_MCU_PART", "")
    if part:
        f = find_part(a.data, part)
        if not f or not f.startswith(re.sub(r"x.*$", "", mcu)):
            print(f"umcub: UMCUB_CFG_MCU_PART {part}: " +
                  (f"not a part of {mcu} (pin data {f})" if f else "no such part in the ST pin data"))
            return 1
        files = [f]
        package, bonded = package_pins(a.data, f)
        for opt, (port, num, af) in used_pins(opts):
            if f"P{port}{num}" not in bonded:
                errors.append(f"{opt} = P{port}{num}: not brought out on {part} ({package})")
        notes.append(f"package {package}")
    signals, info = gpio_modes(a.data, mcu, files)
    checks = expected(opts) if signals is not None else []
    if signals is None:
        notes.append(f"no AF numbers ({info}): AF checked by the port")
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
    n = len(used_pins(opts)) if part else len(checks)
    print(f"umcub: pin check: {n} pin(s) fit {part or mcu} (ST pin data" + "".join(f"; {x}" for x in notes) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
