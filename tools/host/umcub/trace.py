"""describe(): a tap event (umcub.add_tap) as one line of an exchange log - the umcub --trace output and GUIs."""
import struct
from dataclasses import dataclass

from . import link, lite
from .connection import KIND_LINK, KIND_LITE, KIND_SMP

SMP_OPS = {0: "read", 1: "read-rsp", 2: "write", 3: "write-rsp"}
SMP_COMMANDS = {(0, 0): "os/echo", (0, 5): "os/reset", (1, 0): "image/state", (1, 1): "image/upload",
                (100, 0): "umcub/verify", (100, 1): "umcub/hash", (100, 2): "umcub/read"}
# 100 = smp.INSPECT_GROUP, not imported: umcub (and so this module) loads without smpclient


@dataclass
class Described:
    kind: str           # "link", "smp", "lite", "text" or "data" (anything else, e.g. a board protocol's frame)
    node: int | None    # the link node the frame goes to or comes from (None: host, broadcast, not a link frame)
    text: str           # one line: what the payload is


def describe(ev):
    """A TapEvent as Described. Packet transports (UDP, CAN) give no kind: a link frame or an SMP packet is
    recognized by its header."""
    p = ev.payload
    if ev.kind == "text":
        return Described("text", None, p.decode(errors="replace").strip())
    if ev.kind in (KIND_LINK, None):
        fr = link.parse_frame(p)
        if fr:
            node = fr["src"] if ev.direction == "rx" else fr["dst"]
            return Described("link", None if node in (link.HOST_ADDR, link.BROADCAST) else node,
                             " ".join(link.describe_frame(p).splitlines()[0].split()))    # no column padding
    if ev.kind in (KIND_SMP, None) and len(p) >= 8 and p[0] in SMP_OPS:
        op, _, length, group, seq, command = struct.unpack(">BBHHBB", p[:8])
        if ev.kind == KIND_SMP or length == len(p) - 8:
            name = SMP_COMMANDS.get((group, command), f"group {group}/{command}")
            return Described("smp", None, f"{SMP_OPS[op]} {name} seq {seq}, {len(p)} B")
    if ev.kind == KIND_LITE and len(p) >= 2 and p[0] == lite.MAGIC:
        return Described("lite", None, f"{chr(p[1])} {len(p)} B")
    return Described("data", None, f"{len(p)} B")
