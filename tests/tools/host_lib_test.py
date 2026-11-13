#!/usr/bin/env python3
"""umcub host library without a device: transport registry, plugins (a folder and an
installed package's entry point), device strings, adapter lists, error codes, the tap,
the sniffer (on a pseudo terminal) and flash() (SMP / lite faked)."""
import ast
import base64
import hashlib
import os
import string
import struct
import sys
import tempfile
import textwrap
import threading
import time
import types
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
from serial.tools import list_ports  # noqa: E402
from serial.tools.list_ports_common import ListPortInfo  # noqa: E402
import umcub.smp  # noqa: E402  (flash() tests replace umcub.smp.Smp)
from umcub import link  # noqa: E402
from umcub.adapter import network_adapters, socketcan_adapters  # noqa: E402
from umcub.connection import Udp, can_bus  # noqa: E402
from umcub.errors import MESSAGES, DeviceError  # noqa: E402

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

# Error codes: every code used in the library is in MESSAGES with exactly the params its template needs, every
# message is used, and the errors carry code / params / rc.
used = set()
for f in sorted((Path(umcub.__file__).parent).glob("*.py")):
    for node in ast.walk(ast.parse(f.read_text())):
        if not isinstance(node, ast.Call):
            continue
        kw = {k.arg: k.value for k in node.keywords if k.arg}
        code, names = None, set(kw) - {"code"}
        if isinstance(kw.get("code"), ast.Constant):
            code = kw["code"].value
        elif getattr(node.func, "id", "") == "_check" and len(node.args) > 1 and isinstance(node.args[1], ast.Constant):
            code, names = node.args[1].value, names | {"reason", "rc"}         # lite._check adds both
        if code is None:
            continue
        used.add(code)
        if code not in MESSAGES:
            expect(f"{f.name}:{node.lineno} code {code} in MESSAGES", False)
            continue
        fields = {n for _, n, _, _ in string.Formatter().parse(MESSAGES[code]) if n}
        extra = names - fields - {"rc"}                 # rc is kept for DeviceError even when the text has none
        expect(f"{f.name}:{node.lineno} {code} params", fields <= names and not extra,
               f"- template {sorted(fields)}, given {sorted(names)}")
expect("every message is used", set(MESSAGES) - {"error"} <= used, str(sorted(set(MESSAGES) - {"error"} - used)))

e = DeviceError(rc=5, code="smp.device", reason="no image in that slot")
expect("device error", (str(e), e.code, e.rc, e.params) ==
       ("device: no image in that slot (rc 5)", "smp.device", 5, {"reason": "no image in that slot", "rc": 5}), repr(e))
e = umcub.UmcubError("plain text")
expect("plain message", (e.code, e.params) == ("error", {"message": "plain text"}), str(e.params))
raises("image file error code", lambda: umcub.Image(b"\0" * 64), text="no header magic")
try:
    umcub.Image(b"\0" * 64)
except umcub.UmcubError as err:
    expect("image error code", err.code == "image.not_signed", err.code)
junk = tmp / "junk.pem"
junk.write_text("not a key")
for private in (False, True):
    try:
        link.load_key(junk, private)
        expect("junk key refused", False)
    except umcub.LinkError as err:
        expect("junk key: LinkError key.invalid", err.code == "key.invalid", err.code)

# SECURE signing: the key file signer signs the transcript hash a server computes the same way.
from cryptography.exceptions import InvalidSignature  # noqa: E402
from cryptography.hazmat.primitives import hashes, serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec, utils  # noqa: E402

KEYS = Path(__file__).resolve().parents[2] / "tools/keys"
challenge, eph_pub, nonce = bytes(range(56)), bytes(range(64)), bytes(range(100, 132))   # CHALLENGE: identity + 32
th = hashlib.sha256(b"umcub-link-v1" + struct.pack("<H", 0xFFFE) + challenge + eph_pub + nonce).digest()
expect("transcript hash", link.transcript(challenge, eph_pub, nonce) == th)
sig = link.key_signer(KEYS / "dev-admin-p256.pem")(challenge, eph_pub, nonce)
admin_pub = link.load_key(KEYS / "dev-admin-p256.pem", False)
try:
    admin_pub.verify(utils.encode_dss_signature(int.from_bytes(sig[:32], "big"), int.from_bytes(sig[32:], "big")),
                     th, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    expect("key signer: r || s of the transcript hash", len(sig) == 64)
except InvalidSignature:
    expect("key signer: r || s of the transcript hash", False)
pub = tmp / "admin-pub.pem"
pub.write_bytes(admin_pub.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
raises("key signer needs the private key", lambda: link.key_signer(pub), umcub.LinkError, "a private key is needed")

# A connection opened by the program is shared: devices on it do not close it.
shared = umcub.open_connection("loop:t")
d = umcub.connect(shared)
d.close()
shared.send(None, b"still open")
expect("a device on a given connection leaves it open", shared.recv(None, 0) == b"still open")
shared.close()

# describe(): one line per tap event, the node it concerns (link frames), for exchange logs (CLI --trace, GUIs).
from umcub.lite import frame as lite_frame  # noqa: E402

def ev(direction, kind, payload):
    return umcub.TapEvent(0.0, direction, kind, payload, "test")


hello = link.make_frame(link.T_HELLO, 0, 5, 7, b"")
d = umcub.describe(ev("tx", umcub.KIND_LINK, hello))
expect("describe: link frame", d.kind == "link" and d.node == 5 and d.text.startswith("HELLO") and "seq=7" in d.text,
       str(d))
d = umcub.describe(ev("rx", None, link.make_frame(link.T_ANNOUNCE, 0, 0xFFFE, 9, b"")))   # UDP / CAN: no kind
expect("describe: link frame on a packet transport", d.kind == "link" and d.text.startswith("ANNOUNCE"), str(d))
d = umcub.describe(ev("rx", umcub.KIND_LINK, link.make_frame(link.T_ANNOUNCE, 0, link.HOST_ADDR, 9, b"", src=6)))
expect("describe: a node's answer is that node's", d.node == 6, str(d))
smp_echo = bytes([2, 0, 0, 7, 0, 0, 3, 0]) + b"\xa1aedhey"           # write, group 0 (os), seq 3, command 0 (echo)
d = umcub.describe(ev("tx", umcub.KIND_SMP, smp_echo))
expect("describe: SMP request", (d.kind, d.node, d.text) == ("smp", None, "write os/echo seq 3, 15 B"), str(d))
d = umcub.describe(ev("rx", None, bytes([3, 0, 0, 2, 0, 1, 4, 1]) + b"\xa0\xa0"))  # write response, image/upload
expect("describe: SMP response on a packet transport", d.text == "write-rsp image/upload seq 4, 10 B", str(d))
d = umcub.describe(ev("tx", umcub.KIND_LITE, lite_frame("R")))
expect("describe: lite frame", d.kind == "lite" and d.text.startswith("R"), str(d))
d = umcub.describe(ev("rx", "text", b"ok valid\r\n"))
expect("describe: text", (d.kind, d.text) == ("text", "ok valid"), str(d))
d = umcub.describe(ev("rx", None, b"\x01\x02"))
expect("describe: unknown bytes", d.kind == "data" and d.text == "2 B", str(d))
import contextlib  # noqa: E402
import io  # noqa: E402

from umcub import cli_device  # noqa: E402

err = io.StringIO()
with contextlib.redirect_stderr(err):
    cli_device._trace(ev("tx", umcub.KIND_LINK, hello))
expect("umcub --trace prints the described line", " tx link node 5 HELLO" in err.getvalue() and hello[:8].hex() in
       err.getvalue(), err.getvalue())

# Tap: every payload and text command of a connection, plugins included; a failing tap does not stop the transfer.
events = []
with umcub.connect("loop:t", tap=events.append) as dev:
    dev.send(umcub.KIND_SMP, b"abc")
    dev.recv(umcub.KIND_SMP, 0.1)
    dev.recv(umcub.KIND_SMP, 0.0)                         # timeout: no event
expect("tap: tx and rx", [(e.direction, e.kind, e.payload, e.connection) for e in events] ==
       [("tx", umcub.KIND_SMP, b"abc", "loop:t"), ("rx", umcub.KIND_SMP, b"abc", "loop:t")], str(events))
events.clear()
with umcub.connect("loop:t", tap=events.append) as dev:          # a plugin inherits the packet text()
    answer = dev.text("i", 0.1)
expect("plugin text(): one packet each way", answer == "i", repr(answer))
expect("tap: a text command is one tx / rx pair", [(e.direction, e.kind, e.payload) for e in events] ==
       [("tx", "text", b"i"), ("rx", "text", b"i")], str(events))
c = umcub.open_connection("loop:t", tap=lambda ev: 1 / 0)
c.send(None, b"x")
expect("tap error kept, transfer goes on", isinstance(c.tap_error, ZeroDivisionError) and c.recv(None, 0) == b"x")

# Sniffer on a pseudo terminal: link frames and a raw line, stops on the event.
if sys.platform.startswith("linux"):
    import pty
    master, slave = pty.openpty()
    got, stop = [], threading.Event()
    th = threading.Thread(target=link.sniff, args=(os.ttyname(slave), got.append), kwargs=dict(raw=True, stop=stop))
    th.start()
    time.sleep(0.3)
    f = link.make_frame(link.T_HELLO, 0, 5, 1, b"")
    os.write(master, b"\x05\x0b" + base64.b64encode(f) + b"\nnoise line\n")
    for _ in range(50):
        if len(got) >= 2:
            break
        time.sleep(0.05)
    stop.set()
    th.join(2)
    expect("sniff: stops on the event", not th.is_alive())
    expect("sniff: link frame parsed", got and got[0].frame and got[0].frame["dst"] == 5 and got[0].data == f,
           str(got[:1]))
    expect("sniff: raw line", len(got) > 1 and got[1].frame is None and got[1].text.startswith("raw "), str(got[1:]))
    os.close(master)
    os.close(slave)
raises("sniff: port that does not exist", lambda: link.sniff("/dev/umcub-no-such-port", print), text="umcub-no-such-port")


# flash(): board type check, upload, check without readback (verify, else hash), mark, reset; SMP and lite faked.
def signed_image(board_type=None, encrypted=False, payload=b"\x11" * 64):
    tlvs = struct.pack("<HH", 0x10, 32) + hashlib.sha256(payload).digest()
    if board_type is not None:
        tlvs += struct.pack("<HHI", 0xA0, 4, board_type)
    hdr = struct.pack("<IIHHIIBBHI", 0x96F3B83D, 0, 32, 0, len(payload), 0x04 if encrypted else 0, 1, 2, 3, 0)
    return umcub.Image(hdr.ljust(32, b"\0") + payload + struct.pack("<HH", 0x6907, 4 + len(tlvs)) + tlvs)


NOT_SUPPORTED = DeviceError(rc=umcub.smp.InspectErr.ENOTSUP, code="smp.device", reason="not supported")


class FakeSmp:
    """Records the calls; verify / hash answer with `answers` (an exception is raised)."""
    calls, answers = [], {}

    def __init__(self, dev):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        pass

    def _answer(self, what):
        a = FakeSmp.answers[what]
        if isinstance(a, Exception):
            raise a
        return a

    def upload(self, data, image=0, progress=None, cancel=None):
        FakeSmp.calls.append(("upload", image, cancel))
        if progress:
            progress(len(data), len(data))

    def verify(self, image, slot):
        FakeSmp.calls.append(("verify", image, slot))
        return self._answer("verify")

    def hash(self, image, slot, off=0, length=0):
        FakeSmp.calls.append(("hash", image, slot, off, length))
        return self._answer("hash"), length

    def mark(self, image_hash, permanent=False):
        FakeSmp.calls.append(("mark", image_hash, permanent))

    def reset(self):
        FakeSmp.calls.append(("reset",))


umcub.smp.Smp = FakeSmp
lite_calls = []
umcub.lite.upload = lambda *a, **kw: lite_calls.append(("upload", a[1:], kw))
umcub.lite.reset = lambda dev: lite_calls.append(("reset",))
dev = types.SimpleNamespace(identity={"board_type": 0x46103001})
img = signed_image(0x46103001)


def flash(answers, *args, image_=img, **kw):
    FakeSmp.calls, FakeSmp.answers = [], answers
    return umcub.flash(dev, image_, *args, **kw)


done = []
expect("flash: verified by the device", flash({"verify": True}, progress=lambda d, t: done.append((d, t))) == "verify"
       and FakeSmp.calls == [("upload", 0, None), ("verify", 0, 0)] and done == [(img.size, img.size)],
       str(FakeSmp.calls))
expect("flash: no verify, the hash matches",
       flash({"verify": NOT_SUPPORTED, "hash": hashlib.sha256(img.data).digest()}) == "hash"
       and FakeSmp.calls[-1] == ("hash", 0, 0, 0, img.size), str(FakeSmp.calls))
expect("flash: neither verify nor hash: not checked", flash({"verify": NOT_SUPPORTED, "hash": NOT_SUPPORTED}) is None)
for answers, code in (({"verify": False}, "flash.invalid"),
                      ({"verify": NOT_SUPPORTED, "hash": b"\0" * 32}, "flash.hash_mismatch")):
    try:
        flash(answers, reset=True)
        expect(f"flash: {code}", False, "- no error")
    except umcub.UmcubError as err:
        expect(f"flash: {code}, no reset", err.code == code and ("reset",) not in FakeSmp.calls,
               f"{err.code} {FakeSmp.calls}")
enc = signed_image(0x46103001, encrypted=True)
expect("flash: encrypted image in the primary slot is not hashed (decrypted there)",
       flash({"verify": NOT_SUPPORTED, "hash": b"\0" * 32}, image_=enc) is None
       and not any(c[0] == "hash" for c in FakeSmp.calls), str(FakeSmp.calls))
cancel = threading.Event()
expect("flash: secondary slot, test boot, reset",
       flash({"verify": True}, slot="secondary", test=True, reset=True, cancel=cancel) == "verify"
       and FakeSmp.calls == [("upload", 2, cancel), ("verify", 0, 1), ("mark", img.hash, False), ("reset",)],
       str(FakeSmp.calls))
expect("flash: image 1 primary slot (direct upload)", flash({"verify": True}, image=1, slot="primary") == "verify"
       and FakeSmp.calls == [("upload", 3, None), ("verify", 1, 0)], str(FakeSmp.calls))
for kw in (dict(test=True), dict(permanent=True, slot="primary")):
    try:
        flash({"verify": True}, **kw)
        expect(f"flash {kw}: refused", False)
    except umcub.UmcubError as err:
        expect(f"flash {kw}: refused before the upload", err.code == "flash.mark_needs_secondary"
               and not FakeSmp.calls, f"{err.code} {FakeSmp.calls}")
try:
    flash({"verify": True}, image_=signed_image(0x46411001))
    expect("flash: wrong board type refused", False)
except umcub.UmcubError as err:
    expect("flash: wrong board type refused before the upload", err.code == "image.board_type" and not FakeSmp.calls)
expect("flash: lite, secondary slot, permanent, reset: not checked",
       flash({}, protocol="lite", slot="secondary", permanent=True, reset=True, chunk=64, cancel=cancel) is None
       and lite_calls == [("upload", (img.data, 0, 1, False, True, 64, None, cancel), {}), ("reset",)]
       and not FakeSmp.calls, str(lite_calls))

# Board names: JSON files merge (the later one wins), a new load replaces the catalog; shown wherever a board type is.
(tmp / "names1.json").write_text('{"0x46103001": "Blue Pill", "0x00000007": "old name"}')
(tmp / "names2.json").write_text('{"0x00000007": "Pump controller", "0X46411001": "Black Pill"}')
got = umcub.load_board_names(tmp / "names1.json", tmp / "names2.json")
expect("board names: merged, later wins",
       got == {0x46103001: "Blue Pill", 7: "Pump controller", 0x46411001: "Black Pill"}, str(got))
expect("board label: name", umcub.board_label(0x46103001) == "0x46103001 (Blue Pill)", umcub.board_label(0x46103001))
expect("board label: unknown stays hex", umcub.board_label(0x12345678) == "0x12345678")
ident = dict(addr=5, uid="00" * 12, board_type=0x46103001, board_rev=1, link=1, enc=False, closed=False,
             version="1.0.0")
expect("discover / info line with the name", "board 0x46103001 (Blue Pill)" in link.describe_identity(ident),
       link.describe_identity(ident))
raises("board type mismatch with names", lambda: signed_image(7).check_board(0x46103001),
       text="board type 0x00000007 (Pump controller), the device is 0x46103001 (Blue Pill)")
raises("image without board type: device name", lambda: signed_image().check_board(0x46103001),
       text="the device is 0x46103001 (Blue Pill)")
expect("a new load replaces the catalog", umcub.load_board_names(tmp / "names2.json") == {7: "Pump controller",
       0x46411001: "Black Pill"} and umcub.board_label(0x46103001) == "0x46103001")
for name, text in (("bad.json", "{"), ("list.json", "[1]"), ("key.json", '{"pump": "x"}'), ("value.json", '{"7": 7}')):
    (tmp / name).write_text(text)
    try:
        umcub.load_board_names(tmp / name)
        expect(f"board names {name}: refused", False)
    except umcub.UmcubError as err:
        expect(f"board names {name}: names.invalid with the path", err.code == "names.invalid" and name in str(err),
               f"{err.code} {err}")
raises("board names: missing file", lambda: umcub.load_board_names(tmp / "none.json"), text="none.json")
expect("a failed load keeps the catalog", umcub.board_label(7) == "0x00000007 (Pump controller)")
umcub.load_board_names()

# Which connections may run at once: a serial port one at a time (a shared bus, half duplex), CAN and UDP together;
# a plugin says so when it registers (default: one at a time per target).
umcub.register_transport("par", lambda target, node: None, description="parallel test", parallel=True)
for spec, key in (("/dev/ttyUSB1", "serial:/dev/ttyUSB1"), ("serial:/dev/ttyUSB1,baud=9600", "serial:/dev/ttyUSB1"),
                  ("COM3", "serial:COM3"), ("192.168.1.5", None), ("udp:10.0.0.255,port=1337", None),
                  ("can:socketcan:can0", None), ("can:pcan:PCAN_USBBUS1,bitrate=250000", None), ("loop:t", "loop:t"),
                  ("par:x", None)):
    expect(f"exclusive_key({spec})", umcub.exclusive_key(spec) == key, repr(umcub.exclusive_key(spec)))

# CAN: the connections of one channel share one python-can bus (single-handle drivers), each with its node's IDs.
try:
    import can
    import isotp
    from umcub.connection import CAN_RX, CAN_TX
    peer = can.Bus(interface="virtual", channel="umcub-test")
    echo = {}
    for n in (1, 2):    # the devices' side: receive on the host's TX id, answer on its RX id
        echo[n] = isotp.NotifierBasedCanStack(peer, can.Notifier(peer, []) if n == 1 else echo[1].notifier,
                                              address=isotp.Address(isotp.AddressingMode.Normal_11bits,
                                                                    txid=CAN_RX + n, rxid=CAN_TX + n),
                                              params={"tx_padding": 0xCC, "blocking_send": False})
        echo[n].start()
    a = umcub.open_connection("can:virtual:umcub-test", node=1)
    b = umcub.open_connection("can:virtual:umcub-test", node=2)
    expect("CAN: one bus per channel", a.shared.bus is b.shared.bus)
    a.send(None, b"to node 1 " * 3)
    b.send(None, b"to node 2 " * 3)
    got = {n: echo[n].recv(block=True, timeout=2) for n in (1, 2)}
    expect("CAN: each node's IDs", got == {1: b"to node 1 " * 3, 2: b"to node 2 " * 3}, repr(got))
    echo[2].send(b"answer of 2")
    expect("CAN: answers reach their connection", b.recv(None, 2) == b"answer of 2" and a.recv(None, 0.2) is None)
    a.close()
    echo[2].send(b"still open")
    expect("CAN: the bus stays open for the other node", b.recv(None, 2) == b"still open")
    raises("CAN: the same channel with other settings", lambda: umcub.open_connection(
        "can:virtual:umcub-test,bitrate=250000", node=3), text="other settings")
    b.close()
    c = umcub.open_connection("can:virtual:umcub-test,bitrate=250000", node=3)
    expect("CAN: closed with its last connection (new settings take)", c.shared.bus is not b.shared.bus)
    c.close()
    with can_bus("can:virtual:umcub-test") as held:          # discover() keeps the channel open between probes
        d = umcub.open_connection("can:virtual:umcub-test", node=4)
        d.close()
        e = umcub.open_connection("can:virtual:umcub-test", node=5)
        expect("CAN: a held channel is not closed and reopened", e.shared is held)
        e.close()
    raises("CAN: can_bus of a non-CAN device string", lambda: can_bus("/dev/ttyUSB0").__enter__(), text="CAN")
    for n in (1, 2):
        echo[n].stop()
    echo[1].notifier.stop()
    peer.shutdown()
except ImportError as err:
    print(f"skip CAN bus tests: {err}")

expect("link mode names exported", umcub.LINK_NAMES == {0: "plain", 1: "addressed", 2: "secure"})

# Install extras: the build-only dependency is in umcub[build]; nothing above (device side) loaded it.
if sys.version_info >= (3, 11):
    import tomllib
    proj = tomllib.loads((Path(__file__).resolve().parents[2] / "tools/host/pyproject.toml").read_text())["project"]
    expect("esp-idf-kconfig only in the build extra", not any("esp-idf-kconfig" in d for d in proj["dependencies"])
           and any("esp-idf-kconfig" in d for d in proj["optional-dependencies"]["build"]), str(proj))
expect("device side without esp-idf-kconfig", "esp_kconfiglib" not in sys.modules)

print(f"host library: {'all cases passed' if not bad else f'{bad} failure(s)'}")
sys.exit(1 if bad else 0)
