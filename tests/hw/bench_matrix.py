#!/usr/bin/env python3
"""Every transport of every node on a bench, end to end: the example application signed with its own version is
uploaded through the transport, the node is reset, the application reports on the RS485 bus (umcub link) which
image runs and through which transport the last update came, and goes back to the bootloader ('b').

  bench_matrix.py --rs485 /dev/ttyUSB1 [--can can:socketcan:can0] [--usb /dev/ttyACM0] [--eth 192.168.1.50]
                  --node 5=build/bp:examples/bluepill_app/bluepill_app.elf:rs485,can [--node ...] [--base 1.0]

--node N=TREE:ELF:TRANSPORTS: the node's link address, the build tree of its bootloader (signing key, slots) and
the example application in it (built with -DAPP_NODE_ADDR=none, its console on the bootloader's UART), and its
transports (rs485, can, usb-cdc, usb-dfu, eth; dfu-util picks the one DFU device attached; TREE and ELF without ":"
- POSIX paths). Every node must be in its bootloader at the start; a transport whose
host side is missing (no port, interface down, no answer to ping) is reported as skipped. Exit code: the number of
failed steps.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[2]
UMCUB = [sys.executable, "-m", "umcub"]
ENV = dict(os.environ, PYTHONPATH=str(ROOT / "tools/host"))

# what the example prints after "last update via" (examples/common/umcub_example.c, transports[])
VIA = {"rs485": "uart", "can": "can", "usb-cdc": "usb-cdc", "usb-dfu": "usb-dfu", "eth": "eth"}


def run(cmd, timeout):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=ENV)
    return p.returncode, (p.stdout + p.stderr).strip()


def device(a, node, transport):
    """umcub arguments that reach `node` through `transport`, or None when the host side is missing."""
    if transport == "rs485":
        return ["-d", a.rs485, "--node", str(node)] if os.path.exists(a.rs485) else None
    if transport == "can":
        iface = a.can.split(":")[-1]
        up = Path(f"/sys/class/net/{iface}/operstate")
        return ["-d", a.can, "--node", str(node)] if up.exists() and up.read_text().strip() != "down" else None
    if transport == "usb-cdc":
        return ["-d", a.usb] if os.path.exists(a.usb) else None
    if transport == "usb-dfu":
        return [] if "Found DFU" in run(["dfu-util", "-l"], 20)[1] else None
    if transport == "eth":
        rc, _ = run(["ping", "-c", "1", "-W", "1", a.eth], 5) if a.eth else (1, "")
        return ["-d", a.eth] if rc == 0 else None
    raise ValueError(transport)


def console(a, cmd, wait, expect):
    """Send one key to the application console on the bus, collect what it prints."""
    with serial.Serial(a.rs485, a.baud, timeout=0.2) as s:
        s.reset_input_buffer()
        s.write(cmd)
        out, end = b"", time.monotonic() + wait
        while time.monotonic() < end:
            out += s.read(512)
            if expect.encode() in out:
                break
        return out.decode(errors="replace")


def secure_keys(tree, transport):
    """The keys of a SECURE link transport of the tree (its configuration names them), else none."""
    cfg = json.loads((ROOT / tree / "umcub_kconfig" / "umcub_config.json").read_text())
    name = {"rs485": "UART_LINK", "can": "CAN_LINK", "usb-cdc": "USB_CDC_LINK", "eth": "ETH_LINK"}.get(transport)
    if cfg["values"].get(name) != 2:
        return []
    return ["--admin-key", cfg["keys"]["HOST"], "--device-key", cfg["keys"]["DEVICE"]]


def step(a, node, tree, elf, transport, version, tmp):
    dev = device(a, node, transport)
    if dev is None:
        return "skipped", "host side not available"
    dev += secure_keys(tree, transport)
    base = Path(tmp) / f"n{node}_{transport}"
    rc, out = run(UMCUB + ["image", "sign", "--board", str(ROOT / tree / "umcub_kconfig"), "--version", version,
                           "--quiet", "-o", str(base), str(ROOT / tree / elf)], 60)
    img = Path(f"{base}.signed.bin")
    if rc or not img.exists():
        return "FAIL", f"sign: {out[-200:]}"
    if transport == "usb-dfu":
        rc, out = run(["dfu-util", "-a", "0", "-D", str(img), "-R"], 120)       # secondary slot, then reset
        ok = "Download done" in out
    else:
        rc, out = run(UMCUB + dev + ["upload", str(img), "--reset"], 180)
        ok = rc == 0
    if not ok:
        return "FAIL", f"upload: {out.splitlines()[-1] if out else rc}"
    info = ""
    for _ in range(10):                     # the application starts after the bootloader's reset / install
        time.sleep(1)
        info = console(a, b"i", 1.5, expect="keys:")
        if "image 0:" in info:
            break
    m = re.search(r"image 0: (\S+)", info)
    via = re.search(r"last update via ([\w-]+)", info)
    got = (m.group(1).split("+")[0] if m else "?", via.group(1) if via else "?")
    back = console(a, b"b", 1.0, expect="bootloader")
    time.sleep(1.5)
    rc, out = run(UMCUB + ["-d", a.rs485, "--node", str(node)] + secure_keys(tree, "rs485") + ["cmd", "i"], 30)
    if got != (version, VIA[transport]):
        return "FAIL", f"application reports {got[0]} via {got[1]}, expected {version} via {VIA[transport]}"
    if "-> bootloader" not in back or rc:
        return "FAIL", f"{version} ran, but did not go back to the bootloader: {out[-120:]}"
    return "ok", f"{version} ran (last update via {got[1]}), back in the bootloader"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rs485", required=True, help="serial port of the RS485 bus (link, application consoles)")
    ap.add_argument("--baud", type=int, default=115200, help="of the RS485 bus (UMCUB_CFG_UART_BAUD)")
    ap.add_argument("--can", default="can:socketcan:can0")
    ap.add_argument("--usb", default="/dev/ttyACM0")
    ap.add_argument("--eth", default="", help="IP address of the node with Ethernet")
    ap.add_argument("--node", action="append", required=True, metavar="N=TREE:ELF:TRANSPORTS")
    ap.add_argument("--only", help="node:transport,... (default: all)")
    ap.add_argument("--base", default="1.0", help="versions are <base>.<n>, n counting the steps from 1")
    a = ap.parse_args()
    only = {tuple(x.split(":")) for x in a.only.split(",")} if a.only else None
    steps = []
    for spec in a.node:
        n, rest = spec.split("=", 1)
        tree, elf, transports = rest.split(":")
        steps += [(int(n), tree, elf, t) for t in transports.split(",") if not only or (n, t) in only]
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, (node, tree, elf, transport) in enumerate(steps, 1):
            result, text = step(a, node, tree, elf, transport, f"{a.base}.{i}", tmp)
            results.append(result)
            print(f"node {node}  {transport:8s} {result:8s} {text}", flush=True)
    print(f"bench matrix: {results.count('ok')} ok, {results.count('skipped')} skipped, "
          f"{results.count('FAIL')} failed of {len(steps)}")
    return results.count("FAIL")


if __name__ == "__main__":
    sys.exit(main())
