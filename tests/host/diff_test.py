#!/usr/bin/env python3
"""Differential property tests: the same bytes through the firmware's C code (tests/host/diff_harness.c, run once,
cases on stdin) and through the host library (tools/host/umcub) or the reference it relies on - the results must
agree.

  diff_test.py <tests/host build dir>

  codec   CRC-16 and base64 of src/util/codec.c against binascii / base64
  link    umcub link frames: what src/proto/link.c accepts against link.parse_frame (+ the routing of an ADDRESSED
          node); the ANNOUNCE it answers a HELLO with parsed by the host
  lite    the host's lite upload (lite.upload) through a lossy channel into src/proto/lite.c: the slot holds the image
  isotp   src/transport/can/isotp.c against python-can-isotp (the host's ISO-TP stack), both directions
  image   signed images with a changed TLV area: what MCUboot validates, umcub.Image reads with the same hash,
          version and board type

The same examples on every run (derandomized, no example database); UMCUB_PROPS_RANDOM=1 searches with new ones.
"""
import base64
import binascii
import hashlib
import logging
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/host"))
try:
    import isotp
    from hypothesis import HealthCheck, example, given, settings, strategies as st
    from hypothesis.configuration import set_hypothesis_home_dir
except ImportError as e:
    sys.exit(f"diff tests: {e.name} missing (tools/setup.sh installs tools/requirements.txt)")
logging.getLogger("isotp").setLevel(logging.CRITICAL)   # its warnings about the damaged frames below
_home = tempfile.TemporaryDirectory()
set_hypothesis_home_dir(_home.name)             # no .hypothesis/ in the checkout

from umcub import UmcubError, link, lite  # noqa: E402
from umcub.image import Image  # noqa: E402

RANDOM = os.environ.get("UMCUB_PROPS_RANDOM") == "1"
settings.register_profile("umcub", derandomize=not RANDOM, database=None, deadline=None,
                          max_examples=1000 if RANDOM else 150,
                          suppress_health_check=[HealthCheck.too_slow, HealthCheck.data_too_large])
settings.load_profile("umcub")


class Harness:
    """tests/host/diff_harness: one case per line, "<cmd> <hex> ..." -> one answer line."""

    def __init__(self, path):
        self.p = subprocess.Popen([str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        cfg = dict(kv.split("=") for kv in self.p.stdout.readline().split()[1:])
        self.mtu, self.node, self.board = int(cfg["mtu"]), int(cfg["node"]), int(cfg["board"], 16)
        self.uid = bytes.fromhex(cfg["uid"])

    def ask(self, *words):
        self.p.stdin.write(" ".join(w.hex() if isinstance(w, bytes) else str(w) for w in words) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError(f"diff_harness died (exit {self.p.wait()}) on {words[0]}")
        return line.split()

    def close(self):
        self.p.stdin.close()
        return self.p.wait()


H = None
IMAGE = b""
PROPS = []


def prop(fn):
    PROPS.append(fn)
    return fn


u8, u32 = (st.integers(0, (1 << n) - 1) for n in (8, 32))


# --- codec ------------------------------------------------------------------------------------------------------------
@prop
@given(data=st.binary(max_size=600))
def codec_crc_and_encode(data):
    crc, enc = H.ask("crc", data)[0], H.ask("b64e", data)
    assert int(crc, 16) == binascii.crc_hqx(data, 0)
    assert (enc[0] if enc else "") == base64.b64encode(data).decode()


def b64_reference(text):
    """What src/util/codec.c base64_decode() makes of a text: up to the end of the line and the first '=', any
    other character outside the alphabet refuses it, a trailing group decodes as far as its bits reach (no strict
    padding - the link and NLIP lines are written by our own encoder)."""
    s = text.split(b"\0")[0].split(b"\n")[0].split(b"\r")[0].split(b"=")[0]
    alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    if any(c not in alphabet for c in s):
        return None
    return base64.b64decode(s + b"A" * (-len(s) % 4))[:len(s) * 6 // 8]


@prop
@given(text=st.one_of(st.binary(max_size=200),
                      st.binary(max_size=150).map(lambda b: base64.b64encode(b)),
                      st.text("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\n\r",
                              max_size=200).map(str.encode)))
@example(text=base64.b64encode(b"\x01\x02\x03\x04")[:-1])     # canonical but unpadded
def codec_decode(text):
    got = H.ask("b64d", text)
    want = b64_reference(text)
    assert (None if got[0] == "ERR" else bytes.fromhex(got[1] if len(got) > 1 else "")) == want, (got, want)
    try:
        canonical = base64.b64encode(base64.b64decode(text, validate=True)) == text
    except binascii.Error:
        canonical = False
    if canonical:
        assert want == base64.b64decode(text)                     # the host's encoding: exactly its data


# --- umcub link (ADDRESSED) ---------------------------------------------------------------------------------------------
class Addressed:
    """What an ADDRESSED node takes: link.parse_frame, the payload limit, the routing - and the one state, the node
    selected by its UID (HELLO with F_UID; afterwards frames to address 0 are its), dropped by a HELLO without it."""

    def __init__(self):
        self.selected = False

    def take(self, f):
        fr = link.parse_frame(f)
        if fr is None or len(fr["payload"]) > H.mtu:
            return False
        t, p, dst = fr["type"], fr["payload"], fr["dst"]
        if t == link.T_DISCOVER:
            return dst == link.BROADCAST
        if t == link.T_HELLO and fr["flags"] & link.F_UID:
            self.selected = p[:12] == H.uid
            return self.selected
        if t == link.T_HELLO:
            self.selected = False
            return H.node != 0 and dst == H.node
        return (H.node != 0 and dst == H.node) or (dst == 0 and self.selected)


NODE = 5                                                         # diff_harness.c: the node's address
TYPES = st.sampled_from([link.T_DISCOVER, link.T_HELLO, link.T_DATA, link.T_ANNOUNCE, 0x7F])
FRAME = st.tuples(TYPES, st.one_of(st.just(0), st.just(link.F_UID), u8), u32, st.booleans(),
                  st.sampled_from([NODE] * 4 + [0, 0, NODE + 1, link.BROADCAST, 0x1234]),
                  st.one_of(st.binary(max_size=40), st.integers(1400, 1500).map(bytes)),
                  st.one_of(st.none(), st.none(), st.tuples(st.sampled_from([-3, -1, 0, 1, 3]),
                                                            st.integers(-1, 14 * 8 - 1))))


def host_frame(t, flags, seq, uid, dst, payload, damage):
    """A frame the host builds, sometimes cut, extended or with a header bit flipped (damage: cut, bit)."""
    if uid:
        payload = H.uid + payload
    if t == link.T_DISCOVER and dst == NODE:
        dst = link.BROADCAST                                     # DISCOVER goes to everybody
    f = bytearray(link.make_frame(t, flags, dst, seq, payload, src=link.HOST_ADDR))
    f += bytes(link.TAG) if flags & link.F_MAC else b""
    if damage:
        cut, bit = damage
        if bit >= 0:
            f[bit // 8] ^= 1 << (bit % 8)
        f = f[:len(f) + cut] if cut < 0 else f + bytes(cut)
    return bytes(f)


@prop
@given(frames=st.lists(FRAME, min_size=1, max_size=4))
def link_same_frames_accepted(frames):
    """A few frames in a row, from a node nobody selected (a HELLO to another address first)."""
    model = Addressed()
    reset = link.make_frame(link.T_HELLO, 0, NODE + 1, 0, b"")
    assert H.ask("link", reset)[0] == "0" and not model.take(reset)
    for spec in frames:
        f = host_frame(*spec)
        got = H.ask("link", f)
        assert (got[0] == "1") == model.take(f), (got, f.hex())
        fr = link.parse_frame(f)
        if got[0] == "1" and fr["type"] == link.T_HELLO:
            ans = link.parse_frame(bytes.fromhex(got[1]))       # the ANNOUNCE, built by link.c
            assert ans and ans["type"] == link.T_ANNOUNCE and ans["dst"] == fr["src"], got     # to the sender
            ident = link.identity(ans["payload"])
            assert ident and ident["uid"] == H.uid.hex() and ident["board_type"] == H.board and \
                ident["addr"] == H.node, ident


# --- lite upload through a lossy channel ---------------------------------------------------------------------------------
class LossyDevice:
    """A packet device for lite.upload(): every send goes to lite.c; the n-th send can be lost, its answer lost,
    or the frame damaged on the way."""
    stream = False
    timeout = 0.0

    def __init__(self, lose, lose_answer, damage):
        self.lose, self.lose_answer, self.damage = lose, lose_answer, damage
        self.n, self.answers, self.lost_end_answer = 0, [], False

    def send(self, kind, f):
        n, self.n = self.n, self.n + 1
        if n in self.lose:
            return
        if n in self.damage:
            f = bytearray(f)
            f[self.damage[n] % len(f)] ^= 0x10
            f = bytes(f)
        got = H.ask("lite", f)
        if got[0] != "-" and n not in self.lose_answer:
            self.answers.append(bytes.fromhex(got[0]))
        elif got[0] != "-" and f[1:2] == b"E":
            self.lost_end_answer = True

    def recv(self, kind, timeout):
        return self.answers.pop(0) if self.answers else None


@prop
@given(chunk=st.integers(1, lite.CHUNK_PACKET), events=st.lists(
    st.tuples(st.sampled_from(["lose", "lose_answer", "damage"]), st.integers(0, 60), u8), max_size=4))
def lite_upload_lossy(chunk, events):
    """At most 4 mishaps: never 5 in a row for one frame, so the upload never runs out of retries."""
    lose = {n for k, n, _ in events if k == "lose"}
    lose_answer = {n for k, n, _ in events if k == "lose_answer"}
    damage = {n: b for k, n, b in events if k == "damage"}
    dev = LossyDevice(lose, lose_answer, damage)
    try:
        lite.upload(dev, IMAGE, chunk=chunk)
    except UmcubError as e:
        # the answer to the end frame lost: the upload did end, the host cannot know
        assert e.code == "lite.end_unconfirmed" and dev.lost_end_answer, e.code
    assert bytes.fromhex(H.ask("slot", len(IMAGE))[0]) == IMAGE


# --- ISO-TP -------------------------------------------------------------------------------------------------------------
def _stack(frame_len, rx, tx):
    params = {"tx_data_length": frame_len, "tx_data_min_length": None, "blocking_send": False,
              "stmin": 0, "blocksize": 0, "rx_flowcontrol_timeout": 1000, "rx_consecutive_frame_timeout": 1000,
              "max_frame_size": 4095 if frame_len == 8 else 1 << 20, "can_fd": frame_len > 8}
    addr = isotp.Address(isotp.AddressingMode.Normal_11bits, txid=0x7C0, rxid=0x7C8)
    return isotp.TransportLayerLogic(rxfn=rx, txfn=tx, address=addr, params=params, error_handler=lambda e: None)


def py_frames(msg, frame_len):
    """The CAN frames python-can-isotp sends for msg (a receiver answering every first frame with CTS)."""
    out, inbox = [], []
    s = _stack(frame_len, lambda timeout: inbox.pop(0) if inbox else None, lambda m: out.append(bytes(m.data)))
    s.send(msg)
    for _ in range(100000):
        s.process()
        if out and out[0][0] >> 4 == 1 and not inbox and len(out) == 1:
            inbox.append(isotp.CanMessage(arbitration_id=0x7C8, data=b"\x30\x00\x00", is_fd=frame_len > 8))
        if not s.transmitting():
            break
    return out


def py_receive(frames, frame_len):
    """What python-can-isotp delivers from these frames."""
    inbox = [isotp.CanMessage(arbitration_id=0x7C8, data=f, is_fd=frame_len > 8,
                              dlc=None) for f in frames]
    s = _stack(frame_len, lambda timeout: inbox.pop(0) if inbox else None, lambda m: None)
    got = []
    for _ in range(len(frames) + 2):
        s.process()
        while s.available():
            got.append(s.recv())
    return got


@prop
@given(msg=st.binary(min_size=1, max_size=2000), fd=st.booleans())
def isotp_round_trip(msg, fd):
    fl = 64 if fd else 8
    frames = py_frames(msg, fl)
    got = H.ask("isotp", fl, ",".join(f.hex() for f in frames))
    assert got[:1] == [msg.hex()], (got, len(frames))
    sent = H.ask("isotps", fl, msg)
    back = py_receive([bytes.fromhex(x) for x in sent[0].split(",")], fl)
    assert back == [msg], (len(back), sent[0][:80])


def off_standard(frames, fl):
    """Frames ISO 15765-2 rules out, which the two stacks treat differently - neither sends them:
    a CAN-FD single frame over 8 bytes with its length in the first byte (isotp.c takes it, the standard and
    python-can-isotp ignore it); a first frame whose length fits into itself (isotp.c refuses it with an overflow,
    python-can-isotp takes it)."""
    for f in frames:
        pci = f[0] >> 4
        if pci == 0 and len(f) > 8 and f[0] & 0xF:
            return True
        if pci == 1 and len(f) >= 2:
            total, hdr = (f[0] & 0xF) << 8 | f[1], 2
            if total == 0 and len(f) >= 6:
                total, hdr = int.from_bytes(f[2:6], "big"), 6
            if total < len(f) - hdr or total <= (len(f) - (2 if fl > 8 else 1)):
                return True
    return False


@prop
@given(msg=st.binary(min_size=1, max_size=300), fd=st.booleans(), drop=st.sets(st.integers(0, 50), max_size=2),
       flips=st.lists(st.tuples(st.integers(0, 50), st.integers(0, 63), u8), max_size=3))
def isotp_damaged_same_message(msg, fd, drop, flips):
    """Frames lost or changed: whatever isotp.c delivers, the host's stack delivers too (and the other way)."""
    fl = 64 if fd else 8
    frames = [bytearray(f) for i, f in enumerate(py_frames(msg, fl)) if i not in drop]
    for i, j, v in flips:
        if frames:
            f = frames[i % len(frames)]
            f[j % len(f)] = v
    frames = [bytes(f) for f in frames if f]
    got = H.ask("isotp", fl, ",".join(f.hex() for f in frames) or "-")
    c_msgs = [bytes.fromhex(x) for x in got if x != "-"]
    if not off_standard(frames, fl):
        assert c_msgs == py_receive(frames, fl), (c_msgs, [f.hex() for f in frames])


# --- images --------------------------------------------------------------------------------------------------------------
def _hashed_len(data):
    hdr, prot, img = struct.unpack_from("<HHI", data, 8)
    return hdr + img + prot


@prop
@given(extra=st.lists(st.tuples(st.integers(0, 0xFF), st.binary(max_size=40)), max_size=3), where=st.integers(0, 3),
       flips=st.lists(st.tuples(st.integers(0, 1 << 20), u8), max_size=2), cut=st.integers(0, 8))
@example(extra=[(0x10, b"\x00" * 32)], where=0, flips=[], cut=0)           # a second hash TLV, first in the area
@example(extra=[(0x10, b"\x00" * 32)], where=3, flips=[], cut=0)           # ... and last
@example(extra=[(0xA0, b"\x00\x00\x00\x00")], where=0, flips=[], cut=0)    # a board type outside the protected area
def image_same_reading(extra, where, flips, cut):
    """TLVs added to the unprotected area (which no signature covers), bytes changed, the end cut: an image MCUboot
    validates reads the same in umcub.Image - the hash it checked, the version, the board type."""
    data = bytearray(IMAGE)
    t_off = _hashed_len(data)                                    # the unprotected area: 0x6907 + its length
    magic, total = struct.unpack_from("<HH", data, t_off)
    assert magic == 0x6907
    tlvs, off = [], t_off + 4
    while off < t_off + total:
        t, n = struct.unpack_from("<HH", data, off)
        tlvs.append((t, bytes(data[off + 4:off + 4 + n])))
        off += 4 + n
    for t, v in extra:
        tlvs.insert(where % (len(tlvs) + 1), (t, v))
    area = b"".join(struct.pack("<HH", t, len(v)) + v for t, v in tlvs)
    data = data[:t_off] + struct.pack("<HH", 0x6907, 4 + len(area)) + area
    for i, v in flips:
        data[i % len(data)] = v
    data = bytes(data[:len(data) - cut])
    valid = H.ask("img", data)[0] == "0"
    if not valid:
        return
    # the same bytes as the slot: erased flash after the file (a cut into a TLV MCUboot does not read still
    # validates there; umcub.Image refuses the file itself as cut short)
    img = Image(data + b"\xff" * 64)                            # never refuses what the device validates
    h = hashlib.sha256(data[:_hashed_len(data)]).digest()
    assert img.hash == h, "umcub.Image reads another hash than MCUboot checked"
    v = struct.unpack_from("<BBHI", data, 20)
    assert img.version == f"{v[0]}.{v[1]}.{v[2]}" + (f"+{v[3]}" if v[3] else "")
    assert img.board_type == H.board


def main():
    global H, IMAGE
    build = Path(sys.argv[1])
    H = Harness(build / "umcub_diff")
    assert H.node == NODE, f"diff_harness.c node {H.node}, the strategies use {NODE}"
    IMAGE = (build / "diff_image.signed.bin").read_bytes()
    bad = 0
    for p in PROPS:
        try:
            p()
        except Exception as e:                                  # noqa: BLE001  (Hypothesis prints the example)
            bad += 1
            print(f"FAIL {p.__name__}: {type(e).__name__}: {str(e).splitlines()[0] if str(e) else ''}")
    rc = H.close()
    if rc:
        bad += 1
        print(f"FAIL diff_harness exit {rc} (sanitizer report above)")
    print(f"diff properties: {'FAILED' if bad else f'all {len(PROPS)} hold'}" + (" (random)" if RANDOM else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
