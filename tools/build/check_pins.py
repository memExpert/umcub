#!/usr/bin/env python3
"""Check the pins of a bootloader configuration against ST's pin data
(third_party/st/stm32_open_pin_data, the STM32CubeMX database).

  check_pins.py <pins.txt> [--data DIR] [--header umcub_pins.h]

<pins.txt> is the preprocessed probe CMake writes (cmake/umcub_config.cmake):
"@@MCU STM32H755xx" and one "@@ <option> PIN '<port>', <num>[, <af>]" line per
configured pin, "@@ <option> <value>" for the options that select a pin's
signal (instance, enabled transports, drivers) and for UMCUB_CFG_MCU_PART.
For every pin the expected signal (e.g. USART3_TX for UMCUB_CFG_UART_TX_PIN
with UMCUB_CFG_UART_INSTANCE 3) must exist on that pin with that alternate
function number; otherwise the pins that carry the signal are listed. A pin
written without an AF (UMCUB_PIN('A', 9)) gets the AF of that signal on that
pin; --header writes the completed options (#undef / #define, included by
umcub_cfg.h). Plain GPIOs and families without AF numbers get AF 0.
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
        pins = re.findall(r"PIN\s+'([A-K])'\s*,\s*(\d+)(?:\s*,\s*(\d+))?", val)
        if pins:
            opts[name] = [(p, int(n), int(af) if af else None) for p, n, af in pins]   # None: AF to fill in
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
    wildcards (STM32F103C(8-B)Tx.xml matches STM32F103C8T6). Of several that
    match, the most specific: STM32H7A3I(G-I)KxQ.xml (SMPS) for STM32H7A3IIK6Q,
    not STM32H7A3I(G-I)Kx.xml."""
    best = None
    for f in sorted(os.listdir(os.path.join(data, "mcu"))):
        if not f.endswith(".xml"):
            continue
        pat = re.sub(r"\(([A-Z0-9-]+)\)", lambda m: "[" + m.group(1).replace("-", "") + "]", f[:-4]).replace("x", ".")
        m = re.match(pat, part)
        if m and (best is None or m.end() > best[1]):         # most characters of the part number
            best = (f, m.end())
    return best[0] if best else None


def package_pins(data, f):
    """(package, {pin names}) of a part file: "PC14-OSC32_IN" counts as PC14,
    "PA11 [PA9]" (a remappable pad) as both, an analog-switch pad "PC2_C" not
    as PC2."""
    root = ET.parse(os.path.join(data, "mcu", f)).getroot()
    pins = {n for p in children(root, "Pin") for n in re.findall(r"P[A-K]\d+(?:_C)?(?!\d)", p.get("Name") or "")
            if not n.endswith("_C")}
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
                out.append((f"UMCUB_CFG_UART_{o}_PIN", p, [f"USART{i}_{s}", f"UART{i}_{s}"]))   # no LPUART port
        for p in pins("UMCUB_CFG_UART_DE_PIN"):
            if p[2]:                                   # AF 0 / none: a GPIO switched by software
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


def board_driver_pins(opts):
    """Like expected(), for CAN / Ethernet run by the board file
    (UMCUB_DRIVER_BOARD): its pins get the AF of the signal when they carry
    it (an on-chip controller), else AF 0 (an external one) - never an error."""
    out = []
    on = lambda n: opts.get(n, "0") not in ("0", "")   # noqa: E731
    pins = lambda n: opts[n] if isinstance(opts.get(n), list) else []   # noqa: E731
    if on("UMCUB_CFG_TRANSPORT_CAN") and opts.get("UMCUB_CFG_CAN_DRIVER", "0") != "0":
        i = opts.get("UMCUB_CFG_CAN_INSTANCE", "1")
        for o in ("TX", "RX"):
            out += [(f"UMCUB_CFG_CAN_{o}_PIN", p, [f"FDCAN{i}_{o}", f"CAN{i}_{o}", f"CAN_{o}"])
                    for p in pins(f"UMCUB_CFG_CAN_{o}_PIN")]
    if on("UMCUB_CFG_TRANSPORT_ETH") and opts.get("UMCUB_CFG_ETH_DRIVER", "0") != "0":
        out += [("UMCUB_CFG_ETH_RMII_PINS", p, ["ETH_*"]) for p in pins("UMCUB_CFG_ETH_RMII_PINS")]
    return out


def used_pins(opts):
    """(option, pin) of every pin the configuration drives."""
    out = [(o, p) for o, p, _ in expected(opts)]
    on = lambda n: opts.get(n, "0") not in ("0", "")   # noqa: E731
    for o, en in (("UMCUB_CFG_UART_DE_PIN", "UMCUB_CFG_TRANSPORT_UART"), ("UMCUB_CFG_ENTRY_GPIO_PIN", "UMCUB_CFG_ENTRY_GPIO"),
                  ("UMCUB_CFG_STATUS_LED_PIN", "UMCUB_CFG_STATUS_LED")):
        if on(en) and isinstance(opts.get(o), list):
            out += [(o, p) for p in opts[o] if (o, p) not in out]
    if on("UMCUB_CFG_TRANSPORT_USB_CDC") or on("UMCUB_CFG_TRANSPORT_USB_DFU"):
        out += [("USB (D- / D+)", ("A", 11, 10)), ("USB (D- / D+)", ("A", 12, 10))]   # USB FS on every ported family
    return out


def shared_pins(opts):
    """Pins two options both drive (the status LED on the UART TX, ...)."""
    owner, errors = {}, []
    for opt, (port, num, _af) in used_pins(opts):
        other = owner.setdefault((port, num), opt)
        if other != opt:
            errors.append(f"P{port}{num} is both {other} and {opt}")
    return errors


def fitting(signals, names):
    """Signal names of the pin data that match the option's patterns, in the
    order of the patterns (USART1_TX before UART1_TX)."""
    out = []
    for pat in names:
        out += sorted(n for n in signals if n not in out and (n == pat or (pat.endswith("*") and n.startswith(pat[:-1]))))
    return out


def resolve(opts, signals):
    """Fills in the AF of every pin written without one: the AF of the
    option's signal on that pin (`signals`: pin data, None without AF
    numbers), AF 0 for plain GPIOs. Returns the errors."""
    errors, need, optional = [], {}, set()
    if signals is not None:
        for opt, (port, num, af), names in expected(opts):
            if af is None:
                need[(opt, port, num)] = names
        for opt, (port, num, af), names in board_driver_pins(opts):
            if af is None:
                need[(opt, port, num)] = names
                optional.add((opt, port, num))
    for opt, val in opts.items():
        if not isinstance(val, list):
            continue
        for k, (port, num, af) in enumerate(val):
            if af is not None:
                continue
            names = need.get((opt, port, num))
            if names is None:
                val[k] = (port, num, 0)
                continue
            pin = f"P{port}{num}"
            fit = fitting(signals, names)
            afs = next((sorted({a for p_, a in signals[n] if p_ == pin}) for n in fit
                        if any(p_ == pin for p_, _ in signals[n])), [])
            if len(afs) == 1:
                val[k] = (port, num, afs[0])
                continue
            val[k] = (port, num, 0)
            if (opt, port, num) in optional:
                continue
            if afs:
                errors.append(f"{opt} = {pin}: AF{' / AF'.join(map(str, afs))} all fit - write the AF")
                continue
            where = "; ".join(f"{n}: " + ", ".join(f"{p_} (AF{a_})" for p_, a_ in sorted(signals[n]))
                              for n in fit if not names[0].endswith("*"))
            errors.append(f"{opt} = {pin}: no {' / '.join(fit or names)} on this pin" + (f"\n    {where}" if where else ""))
    return errors


def write_header(path, opts, written):
    """The options whose pins were written without an AF, completed."""
    lines = ["/* Generated by tools/build/check_pins.py: pins written without an alternate",
             " * function, completed from ST's pin data. Do not edit. */"]
    for opt in written:
        pins = ", ".join(f"UMCUB_PIN('{p}', {n}, {a})" for p, n, a in opts[opt])
        lines += [f"#undef {opt}", f"#define {opt} {pins}"]
    text = "\n".join(lines) + "\n"
    old = open(path).read() if os.path.exists(path) else None
    if old != text:                                 # unchanged: no rebuild
        with open(path, "w") as f:
            f.write(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("probe")
    ap.add_argument("--data", default=os.path.join(ROOT, "third_party/st/stm32_open_pin_data"))
    ap.add_argument("--header", help="write the pins completed with their AF here")
    a = ap.parse_args()
    mcu, opts = parse_probe(a.probe)
    unset = [o for o, v in opts.items() if isinstance(v, list) and any(af is None for _, _, af in v)]
    shared = shared_pins(opts)                      # needs no pin data
    if shared:
        print("umcub: pins used twice:\n  " + "\n  ".join(shared))
        return 1
    if not os.path.isdir(os.path.join(a.data, "mcu")):
        if any(af is None for _, (_, _, af), _ in expected(opts)):
            print("umcub: pins without an AF need ST's pin data (git submodule update --init --depth 1 "
                  "third_party/st/stm32_open_pin_data) - or write the AF: " + ", ".join(unset))
            return 1
        resolve(opts, None)
        if a.header:
            write_header(a.header, opts, unset)
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
    filled = {(o, p, n) for o in unset for p, n, af in opts[o] if af is None}
    errors += resolve(opts, signals)                # filled pins fit by construction (or were reported)
    checks = [c for c in expected(opts) if (c[0], c[1][0], c[1][1]) not in filled] if signals is not None else []
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
    if a.header:
        write_header(a.header, opts, unset)
    if unset:
        notes.append("AF filled in: " + ", ".join(f"{o.replace('UMCUB_CFG_', '')} " +
                     "/".join(f"P{p}{n} AF{af}" for p, n, af in opts[o]) for o in unset))
    n = len(used_pins(opts)) if part else len(expected(opts)) if signals is not None else 0
    print(f"umcub: pin check: {n} pin(s) fit {part or mcu} (ST pin data" + "".join(f"; {x}" for x in notes) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
