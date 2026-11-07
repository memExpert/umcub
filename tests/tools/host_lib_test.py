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
