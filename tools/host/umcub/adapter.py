"""Adapters a PC has right now, each with a ready device string (for a list in a
GUI or `umcub adapters`): serial ports (USB-UART, USB-RS485, a board's USB CDC),
CAN interfaces python-can finds, network interfaces (UDP broadcast on their
subnet), and what transport plugins report.

Nothing is opened: a serial port is listed whether or not a device answers on it.
"""
import logging
import os
import re
import sys
from dataclasses import dataclass

# python-can interfaces with real hardware that python-can can enumerate. Not
# listed: slcan (shows up as a serial port), serial, virtual, udp_multicast.
CAN_INTERFACES = ("socketcan", "pcan", "kvaser", "vector", "ixxat", "gs_usb", "canalystii", "neovi", "systec",
                  "etas", "nixnet", "cantact", "usb2can")


@dataclass
class Adapter:
    spec: str                   # device string for connect() / -d
    scheme: str                 # the transport
    name: str                   # short: "/dev/ttyUSB0", "COM3", "socketcan:can0", "enp10s0 192.168.1.10/24"
    description: str = ""       # what it is: "FT232R USB UART (FTDI)", ...
    vid: int | None = None      # USB vendor / product id, when known
    pid: int | None = None
    serial_number: str | None = None    # survives renumbering (ttyUSB0 -> ttyUSB1, COM3 -> COM7)
    location: str | None = None         # USB port path, when known


def _natural(text):
    """Sort key: COM10 after COM9, ttyUSB10 after ttyUSB2."""
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", text)]


def serial_adapters(all_ports=False):
    """Serial ports; without `all_ports` the ones with nothing behind them (Linux
    lists /dev/ttyS0..31 whether or not the hardware exists) are left out."""
    from serial.tools import list_ports
    out = []
    for p in sorted(list_ports.comports(), key=lambda p: _natural(p.device)):
        if not all_ports and p.hwid in (None, "", "n/a"):
            continue
        desc = p.description if p.description not in (None, "", "n/a", p.device) else ""
        head, sep, tail = desc.partition(" - ")
        if sep and head == tail:            # Linux: "<product> - <interface>", often the same text
            desc = head
        if p.manufacturer and p.manufacturer not in desc:
            desc = f"{desc} ({p.manufacturer})" if desc else p.manufacturer
        out.append(Adapter(p.device, "serial", p.device, desc, p.vid, p.pid, p.serial_number or None,
                           p.location or None))
    return out


SYSFS_NET = "/sys/class/net"
ARPHRD_CAN = "280"


def _read(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return ""


def _usb_facts(netdir):
    """(description, vid, pid, serial, location) of the USB adapter behind a Linux
    network interface (sysfs), or None when it is not a USB device."""
    dev = os.path.realpath(os.path.join(netdir, "device"))
    for usb in (dev, os.path.dirname(dev)):         # the USB interface, then its device
        if os.path.exists(os.path.join(usb, "idVendor")):
            product, mfr = _read(os.path.join(usb, "product")), _read(os.path.join(usb, "manufacturer"))
            return (f"{product} ({mfr})" if product and mfr else product or mfr,
                    int(_read(os.path.join(usb, "idVendor")), 16), int(_read(os.path.join(usb, "idProduct")), 16),
                    _read(os.path.join(usb, "serial")) or None, os.path.basename(usb))
    return None


def socketcan_adapters(sysfs=SYSFS_NET):
    """Linux SocketCAN interfaces, also those that are down (python-can lists only
    the ones that are up), with the USB facts of the adapter behind them."""
    out = []
    try:
        names = sorted(os.listdir(sysfs), key=_natural)
    except OSError:
        return out
    for name in names:
        d = os.path.join(sysfs, name)
        if _read(os.path.join(d, "type")) != ARPHRD_CAN:
            continue
        up = _read(os.path.join(d, "operstate")) != "down"
        desc, vid, pid, sn, loc = _usb_facts(d) or ("", None, None, None, None)
        if not vid and not os.path.exists(os.path.join(d, "device")):
            desc = "virtual"            # vcan
        if not up:
            desc = (desc + ", " if desc else "") + f"down: sudo ip link set {name} up type can bitrate 500000"
        out.append(Adapter(f"can:socketcan:{name}", "can", f"socketcan:{name}", desc, vid, pid, sn, loc))
    return out


def can_adapters(interfaces=CAN_INTERFACES):
    """CAN channels: on Linux the SocketCAN interfaces from sysfs, plus what
    python-can finds on the other given interfaces; python-can's warnings about
    drivers that are not installed are kept quiet meanwhile."""
    import can
    out = []
    if sys.platform.startswith("linux") and "socketcan" in interfaces:
        out += socketcan_adapters()
        interfaces = [i for i in interfaces if i != "socketcan"]
    log = logging.getLogger("can")
    level = log.level
    log.setLevel(logging.CRITICAL)
    try:
        for i in interfaces:
            try:
                configs = can.detect_available_configs(interfaces=[i])
            except Exception:       # a broken vendor driver must not hide the others
                continue
            for c in configs:
                ch = c.get("channel")
                if ch is None:
                    continue
                desc = str(c.get("description") or c.get("device_name") or "")
                if not any(a.spec == f"can:{i}:{ch}" for a in out):
                    out.append(Adapter(f"can:{i}:{ch}", "can", f"{i}:{ch}", desc))
        return out
    finally:
        log.setLevel(level)


def network_adapters(sysfs=SYSFS_NET):
    """IPv4 interfaces that are up (not loopback): the device string is the
    broadcast address of their subnet, for discovering link devices. On Linux
    also the Ethernet ports without a link or an address (a USB-Ethernet adapter
    with no cable yet), with an empty device string and the reason."""
    import ipaddress
    import socket

    import psutil
    stats = psutil.net_if_stats()
    linux = sys.platform.startswith("linux")
    out = []
    for name, addrs in sorted(psutil.net_if_addrs().items(), key=lambda kv: _natural(kv[0])):
        st = stats.get(name)
        d = os.path.join(sysfs, name)
        usb = _usb_facts(d) if linux else None
        desc, vid, pid, sn, loc = usb or ("", None, None, None, None)
        found = False
        for a in addrs:
            if not st or not st.isup or a.family != socket.AF_INET or not a.netmask:
                continue
            net = ipaddress.ip_interface(f"{a.address}/{a.netmask}")
            if net.ip.is_loopback or net.network.prefixlen >= 31:
                continue
            bcast = str(net.network.broadcast_address)
            out.append(Adapter(f"udp:{bcast}", "udp", f"{name} {net.with_prefixlen}",
                               f"broadcast {bcast}" + (f", {desc}" if desc else ""), vid, pid, sn, loc))
            found = True
        # A physical Ethernet port (Linux: a device behind it, ARPHRD_ETHER) that cannot be used yet.
        if found or not linux or not os.path.exists(os.path.join(d, "device")) or _read(os.path.join(d, "type")) != "1":
            continue
        if st and st.isup and _read(os.path.join(d, "carrier")) == "1":
            why = "no IPv4 address"
        else:
            why = "not connected" if os.path.isdir(os.path.join(d, "wireless")) else "no link (cable?)"
        out.append(Adapter("", "udp", name, why + (f", {desc}" if desc else ""), vid, pid, sn, loc))
    return out


def adapters(schemes=None, errors=None, all_serial_ports=False):
    """Adapters of the given transports (default: all registered ones, plugins
    included). A source that fails is skipped; with `errors` (a list) its
    message is appended there."""
    from .connection import transports
    out = []
    for scheme, t in transports().items():
        if schemes is not None and scheme not in schemes:
            continue
        if t.adapters is None:
            continue
        try:
            found = t.adapters(all_serial_ports) if scheme == "serial" else t.adapters()
            out.extend(a for a in found if isinstance(a, Adapter))
        except Exception as e:
            if errors is not None:
                errors.append(f"{scheme}: {type(e).__name__}: {e}")
    return out

