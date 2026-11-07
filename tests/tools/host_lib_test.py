#!/usr/bin/env python3
"""umcub host library without a device: transport registry, plugins (a folder and an
installed package's entry point), device strings."""
import sys
import tempfile
import textwrap
from pathlib import Path

tmp = Path(tempfile.mkdtemp(prefix="umcub_host_lib_"))
# An "installed" plugin: a dist-info with an entry point, found on sys.path. Set up
# before umcub loads the installed plugins (once, on first use).
(tmp / "site/umcub_test_ep-1.0.dist-info").mkdir(parents=True)
(tmp / "site/umcub_test_ep-1.0.dist-info/METADATA").write_text("Metadata-Version: 2.1\nName: umcub-test-ep\nVersion: 1.0\n")
(tmp / "site/umcub_test_ep-1.0.dist-info/entry_points.txt").write_text(
    "[umcub.transports]\nep = umcub_test_ep:register\nbroken_ep = umcub_test_ep_missing\n")
(tmp / "site/umcub_test_ep.py").write_text(textwrap.dedent("""
    import umcub
    class Ep(umcub.Connection):
        name = "ep"
    def register():
        umcub.register_transport("ep", lambda target, node: Ep(), description="entry point test")
"""))
sys.path.insert(0, str(tmp / "site"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/host"))
import umcub  # noqa: E402
from umcub.connection import Serial, Udp  # noqa: E402,F401

bad = 0


def expect(what, ok, detail=""):
    global bad
    if not ok:
        bad += 1
        print(f"FAIL {what} {detail}")


def raises(what, fn, exc=umcub.UmcubError, text=""):
    try:
        fn()
    except exc as e:
        expect(what, text in str(e), f"- message: {e}")
        return
    expect(what, False, f"- no {exc.__name__}")


# Folder plugins: a loopback transport, a node-selecting one, a broken file, a duplicate
# scheme, one returning the wrong type, and a skipped "_" file.
plug = tmp / "plugins"
plug.mkdir()
(plug / "loop.py").write_text(textwrap.dedent("""
    import collections
    import umcub
    class Loop(umcub.Connection):
        def __init__(self, target, delay):
            self.name, self.delay, self.q = f"loop:{target}", delay, collections.deque()
        def send(self, kind, payload):
            self.q.append(payload)
        def recv(self, kind, timeout):
            return self.q.popleft() if self.q else None
    class Addressed(Loop):
        selects_node = True
    def register():
        umcub.register_transport("loop", lambda target, node, delay=0, fast=False: Loop(target, delay),
                                 {"delay": int, "fast": bool}, "loopback for tests")
        umcub.register_transport("addressed", lambda target, node: Addressed(target, node), description="node by IDs")
        umcub.register_transport("wrong", lambda target, node: object())
"""))
(plug / "broken.py").write_text("raise RuntimeError('broken on purpose')\n")
(plug / "dup.py").write_text("import umcub\ndef register():\n    umcub.register_transport('serial', print)\n")
(plug / "noreg.py").write_text("X = 1\n")
(plug / "_helper.py").write_text("raise RuntimeError('must be skipped')\n")

found = {p.name: p for p in umcub.load_plugins(plug)}
expect("installed plugin loaded", "ep" in found and not found["ep"].error, str(found.get("ep")))
expect("installed plugin source", found.get("ep") and found["ep"].source == "package umcub-test-ep")
expect("broken installed plugin reported", found.get("broken_ep") and "ModuleNotFoundError" in found["broken_ep"].error)
expect("folder plugin loaded", "loop" in found and not found["loop"].error, str(found.get("loop")))
expect("broken folder plugin reported", found.get("broken") and "broken on purpose" in found["broken"].error)
expect("duplicate scheme refused", found.get("dup") and "already registered" in found["dup"].error)
expect("module without register() reported", found.get("noreg") and "register" in found["noreg"].error)
expect("_ files skipped", "_helper" not in found)
again = {p.name: p for p in umcub.load_plugins(plug)}
expect("folder loaded once", all(again[n] is found[n] for n in found), str(sorted(again)))
expect("plugins() lists them", {p.name for p in umcub.plugins()} >= {"ep", "loop", "broken"})

t = umcub.transports()
expect("built-in schemes", {"serial", "udp", "can"} <= set(t), str(sorted(t)))
expect("plugin schemes", {"loop", "addressed", "ep"} <= set(t), str(sorted(t)))
raises("bad scheme name", lambda: umcub.register_transport("Bad scheme", print), text="lower-case")

c = umcub.open_connection("loop:abc,delay=7,fast")
expect("plugin connection", type(c).__name__ == "Loop" and c.name == "loop:abc" and c.delay == 7)
raises("unknown option", lambda: umcub.open_connection("loop:abc,speed=1"), text="unknown option 'speed'")
raises("not a Connection", lambda: umcub.open_connection("wrong:x"), text="not a umcub Connection")
expect("entry point connection", umcub.open_connection("ep:x").name == "ep")

with umcub.connect("loop:x") as dev:                       # no node: the connection itself
    dev.send(umcub.KIND_SMP, b"ping")
    expect("device round trip", dev.recv(umcub.KIND_SMP, 0.1) == b"ping" and dev.name == "loop:x")
raises("node by IDs refuses a UID", lambda: umcub.connect("addressed:x", uid="00"), umcub.LinkError, "not by UID")
with umcub.connect("addressed:x", node=3) as dev:          # node chosen when opening: no link layer
    expect("node by IDs: no link", dev.link is None)

# Adapters: fake serial ports and network interfaces, a plugin's own list, a failing source.
import types  # noqa: E402
from serial.tools import list_ports  # noqa: E402
from serial.tools.list_ports_common import ListPortInfo  # noqa: E402


def port(dev, hwid="n/a", desc="n/a", vid=None, pid=None, sn=None, mfr=None):
    p = ListPortInfo(dev, skip_link_detection=True)
    p.hwid, p.description, p.vid, p.pid, p.serial_number, p.manufacturer = hwid, desc, vid, pid, sn, mfr
    return p


list_ports.comports = lambda: [port("/dev/ttyS10"), port("/dev/ttyS2"),
                               port("/dev/ttyUSB10", "USB VID:PID=0403:6001", "FT232R USB UART", 0x0403, 0x6001, "A1",
                                    "FTDI"),
                               port("/dev/ttyUSB2", "USB VID:PID=1a86:7523", "USB Serial", 0x1a86, 0x7523)]
a = umcub.adapters(["serial"])
expect("serial: empty ttyS hidden, natural order", [x.spec for x in a] == ["/dev/ttyUSB2", "/dev/ttyUSB10"],
       str([x.spec for x in a]))
expect("serial: USB facts", a[1].description == "FT232R USB UART (FTDI)" and (a[1].vid, a[1].pid, a[1].serial_number)
       == (0x0403, 0x6001, "A1"), str(a[1]))
expect("serial: --all", [x.spec for x in umcub.adapters(["serial"], all_serial_ports=True)][:2] ==
       ["/dev/ttyS2", "/dev/ttyS10"])

AF = __import__("socket").AF_INET
addr = types.SimpleNamespace
sys.modules["psutil"] = types.SimpleNamespace(
    net_if_stats=lambda: {"eth0": addr(isup=True), "lo": addr(isup=True), "down0": addr(isup=False),
                          "ptp": addr(isup=True)},
    net_if_addrs=lambda: {"eth0": [addr(family=AF, address="192.168.5.7", netmask="255.255.255.0"),
                                   addr(family=-1, address="fe80::1", netmask=None)],
                          "lo": [addr(family=AF, address="127.0.0.1", netmask="255.0.0.0")],
                          "down0": [addr(family=AF, address="10.1.1.1", netmask="255.255.255.0")],
                          "ptp": [addr(family=AF, address="10.9.9.9", netmask="255.255.255.255")]})

# Linux SocketCAN from a fake sysfs: a USB adapter that is down, a vcan that is up, an Ethernet port.
from umcub.adapter import socketcan_adapters  # noqa: E402
fs = tmp / "sysfs"
usbdev = fs / "devices/usb1/1-1.2"
(usbdev / "1-1.2:1.0").mkdir(parents=True)
for k, v in {"idVendor": "1d50", "idProduct": "606f", "product": "candleLight", "manufacturer": "bytewerk",
             "serial": "0042"}.items():
    (usbdev / k).write_text(v + "\n")
for name, typ, state, dev in (("can0", "280", "down", usbdev / "1-1.2:1.0"), ("vcan0", "280", "unknown", None),
                              ("eth0", "1", "up", None)):
    (fs / "net" / name).mkdir(parents=True)
    (fs / "net" / name / "type").write_text(typ + "\n")
    (fs / "net" / name / "operstate").write_text(state + "\n")
    if dev:
        (fs / "net" / name / "device").symlink_to(dev)
a = socketcan_adapters(str(fs / "net"))
expect("socketcan: CAN interfaces only", [x.spec for x in a] == ["can:socketcan:can0", "can:socketcan:vcan0"],
       str([x.spec for x in a]))
expect("socketcan: USB facts, down hint", a and (a[0].vid, a[0].pid, a[0].serial_number, a[0].location) ==
       (0x1d50, 0x606f, "0042", "1-1.2") and a[0].description.startswith("candleLight (bytewerk), down: sudo ip link"),
       str(a[:1]))
expect("socketcan: vcan", len(a) > 1 and a[1].description == "virtual", str(a[1:]))

# Network: psutil above, sysfs here; usbeth0 is a USB-Ethernet adapter without a cable, wlan0 has a link but no
# address, the others have no sysfs device (virtual) and are left out unless they have an IPv4 subnet.
from umcub.adapter import network_adapters  # noqa: E402
(fs / "devices/usb2/2-5.2/2-5.2:1.0").mkdir(parents=True)
for k, v in {"idVendor": "0bda", "idProduct": "8153", "product": "USB 10/100/1000 LAN", "manufacturer": "Realtek"}.items():
    (fs / f"devices/usb2/2-5.2/{k}").write_text(v + "\n")
(fs / "devices/pci/wifi").mkdir(parents=True)
for name, carrier, dev in (("usbeth0", "0", fs / "devices/usb2/2-5.2/2-5.2:1.0"), ("wlan0", "1", fs / "devices/pci/wifi"),
                          ("wlan1", "0", fs / "devices/pci/wifi")):
    (fs / "net" / name).mkdir(parents=True)
    (fs / "net" / name / "type").write_text("1\n")
    (fs / "net" / name / "carrier").write_text(carrier + "\n")
    (fs / "net" / name / "device").symlink_to(dev)
(fs / "net/wlan1/wireless").mkdir()
ps = sys.modules["psutil"]
stats, addrs_ = ps.net_if_stats(), ps.net_if_addrs()
stats.update({"usbeth0": addr(isup=True), "wlan0": addr(isup=True), "wlan1": addr(isup=False)})
addrs_.update({"usbeth0": [], "wlan0": [addr(family=-1, address="fe80::2", netmask=None)], "wlan1": []})
ps.net_if_stats, ps.net_if_addrs = (lambda: stats), (lambda: addrs_)
a = network_adapters(str(fs / "net")) if sys.platform.startswith("linux") else []
if sys.platform.startswith("linux"):
    expect("network: subnets and unusable Ethernet ports",
           [(x.spec, x.name, x.description) for x in a] ==
           [("udp:192.168.5.255", "eth0 192.168.5.7/24", "broadcast 192.168.5.255"),
            ("", "usbeth0", "no link (cable?), USB 10/100/1000 LAN (Realtek)"),
            ("", "wlan0", "no IPv4 address"), ("", "wlan1", "not connected")], str([(x.spec, x.name, x.description) for x in a]))
    expect("network: USB facts", a[1].vid == 0x0bda and a[1].location == "2-5.2", str(a[1]))

calls = []
umcub.register_transport("listed", lambda target, node: None, adapters=lambda: (calls.append(1), [
    umcub.Adapter("listed:1", "listed", "one"), "not an adapter"])[1])
umcub.register_transport("failing", lambda target, node: None, adapters=lambda: 1 / 0)
errors = []
a = umcub.adapters(["listed", "failing"], errors=errors)
expect("plugin adapters", [x.spec for x in a] == ["listed:1"] and calls == [1], str(a))
expect("failing source reported", errors == ["failing: ZeroDivisionError: division by zero"], str(errors))

u = umcub.open_connection("10.0.0.1:1400")
expect("IP address with port is UDP", isinstance(u, Udp) and u.addr == ("10.0.0.1", 1400))
u.close()
u = umcub.open_connection("udp:10.0.0.2,mtu=256")
expect("udp: options", u.addr == ("10.0.0.2", 1337) and u.mtu == 256)
u.close()
raises("can: needs a channel", lambda: umcub.open_connection("can:socketcan"), text="can:<python-can interface>")
raises("unknown scheme is a serial port", lambda: umcub.open_connection("nosuch:/dev/x"), text="nosuch:/dev/x")

print(f"host library: {'all cases passed' if not bad else f'{bad} failure(s)'}")
sys.exit(1 if bad else 0)
