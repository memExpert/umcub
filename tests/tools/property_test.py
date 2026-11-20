#!/usr/bin/env python3
"""Property tests of the host library's codecs and parsers (Hypothesis): round trips, damaged input refused,
random input never raising anything but UmcubError.

The same examples on every run (derandomized, no example database): ctest stays deterministic. To search for new
counterexamples: UMCUB_PROPS_RANDOM=1 tests/tools/property_test.py - pin what it finds with @example.
"""
import json
import os
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/host"))
try:
    from hypothesis import HealthCheck, example, given, settings, strategies as st
    from hypothesis.configuration import set_hypothesis_home_dir
except ImportError:
    sys.exit("property tests: hypothesis missing (tools/setup.sh installs tools/requirements.txt)")
_home = tempfile.TemporaryDirectory()
set_hypothesis_home_dir(_home.name)             # no .hypothesis/ in the checkout

import umcub  # noqa: E402
from umcub import UmcubError, link, lite  # noqa: E402
from umcub.board_names import board_label, load_board_names  # noqa: E402
from umcub.connection import _parse, exclusive_key  # noqa: E402
from umcub.image import Image  # noqa: E402
from umcub.nlip import NlipDecoder, nlip_encode  # noqa: E402

RANDOM = os.environ.get("UMCUB_PROPS_RANDOM") == "1"
settings.register_profile("umcub", derandomize=not RANDOM, database=None, deadline=None,
                          max_examples=1000 if RANDOM else 150,
                          suppress_health_check=[HealthCheck.too_slow])
settings.load_profile("umcub")

bad = 0
PROPS = []


def prop(fn):
    PROPS.append(fn)
    return fn


u8, u16, u32 = (st.integers(0, (1 << n) - 1) for n in (8, 16, 32))


# --- umcub link frames ----------------------------------------------------------------------------------------------
@prop
@given(t=u8, flags=u8, dst=u16, src=u16, seq=u32, payload=st.binary(max_size=600))
def link_frame_round_trip(t, flags, dst, src, seq, payload):
    flags &= ~link.F_MAC
    f = link.make_frame(t, flags, dst, seq, payload, src=src)
    fr = link.parse_frame(f)
    assert fr and (fr["type"], fr["flags"], fr["dst"], fr["src"], fr["seq"], fr["payload"]) == \
        (t, flags, dst, src, seq, payload)


@prop
@given(payload=st.binary(max_size=200), cut=st.integers(-40, 40), mac=st.booleans())
def link_frame_wrong_length_refused(payload, cut, mac):
    f = link.make_frame(link.T_DATA, link.F_MAC if mac else 0, 5, 1, payload)
    f += bytes(link.TAG) if mac else b""
    g = f[:len(f) + cut] if cut < 0 else f + bytes(cut)
    assert (link.parse_frame(g) is None) == (cut != 0)


@prop
@given(data=st.binary(max_size=300))
def link_any_bytes_never_raise(data):
    link.parse_frame(data)
    link.describe_frame(data)
    i = link.identity(data)
    if i:
        link.describe_identity(i)
    assert (i is None) == (len(data) < 24)


@prop
@given(key=st.binary(min_size=16, max_size=16), seq=u32, data=st.binary(max_size=300))
def ctr_is_its_own_inverse(key, seq, data):
    assert link._ctr(key, seq, link._ctr(key, seq, data)) == data


def _session():
    return link.Session(bytes(range(96)))


def _mac_frame(seq, payload):
    f = link.make_frame(link.T_DATA, link.F_MAC, link.HOST_ADDR, seq, payload, src=5)
    s = _session()
    return f + link._hmac(s.mac_d2h, f)[:link.TAG]


@prop
@given(seq=st.integers(1, (1 << 32) - 1), payload=st.binary(max_size=100), bit=st.integers(0, 10000))
def session_tampered_or_replayed_refused(seq, payload, bit):
    f = _mac_frame(seq, payload)
    s = _session()
    assert s.check(link.parse_frame(f))                         # the genuine frame once
    assert not s.check(link.parse_frame(f))                     # replayed
    g = bytearray(f)
    g[(bit // 8) % len(g)] ^= 1 << (bit % 8)
    fr = link.parse_frame(bytes(g))
    s2 = _session()
    if fr:                                                      # damaged but still a frame: never accepted
        assert not s2.check(fr) and s2.rx_seq == -1


# --- lite -------------------------------------------------------------------------------------------------------------
@prop
@given(kind=st.sampled_from("upwvbr?"), rc=st.integers(-128, 127), taken=u32)
def lite_answer_round_trip(kind, rc, taken):
    f = lite.frame(kind, struct.pack("<bI", rc, taken))
    assert lite.parse_answer(f) == (kind, rc, taken)


@prop
@given(rc=st.integers(-128, 127), taken=u32, bit=st.integers(0, 71), extra=st.binary(max_size=4))
def lite_answer_damaged_refused(rc, taken, bit, extra):
    f = bytearray(lite.frame("u", struct.pack("<bI", rc, taken)))
    if bit // 8 == 1:
        return                                                  # the kind byte: any kind is a valid answer
    f[bit // 8] ^= 1 << (bit % 8)
    assert lite.parse_answer(bytes(f)) is None
    if extra:
        assert lite.parse_answer(lite.frame("u", struct.pack("<bI", rc, taken)) + extra) is None


# --- NLIP (SMP over a stream) ---------------------------------------------------------------------------------------
@prop
@given(pkt=st.binary(min_size=8, max_size=1200), cuts=st.lists(st.integers(1, 64), max_size=40),
       noise=st.lists(st.binary(max_size=40).filter(lambda b: b"\n" not in b), max_size=3))
def nlip_any_chunking(pkt, cuts, noise):
    stream = b"".join(n + b"\n" for n in noise if not n.startswith((b"\x06\x09", b"\x04\x14"))) + nlip_encode(pkt)
    d, got, pos = NlipDecoder(), [], 0
    for c in cuts:
        got += d.feed(stream[pos:pos + c])
        pos += c
    got += d.feed(stream[pos:])
    assert got == [pkt]


@prop
@given(a=st.binary(min_size=8, max_size=300), b=st.binary(min_size=8, max_size=300), bit=st.integers(0, 10000))
def nlip_damaged_packet_dropped_next_kept(a, b, bit):
    first = bytearray(nlip_encode(a))
    i = 2 + (bit // 8) % max(1, len(first) - 3)                 # inside the first line, after its delimiter
    if first[i] in b"\r\n":
        return
    first[i] ^= 1 << (bit % 6)
    if first[i] in b"\r\n":
        return
    got = NlipDecoder().feed(bytes(first) + nlip_encode(b))
    # dropped (CRC, base64), or the same packet - damage in the length field (it only ends the read) or in the unused
    # bits of the last base64 character; the CRC covers the content: never a different packet
    assert got[-1:] == [b] and all(x in (a, b) for x in got), got


@prop
@given(data=st.binary(max_size=500))
def nlip_garbage_never_raises(data):
    NlipDecoder().feed(data)


# --- images (TLV) and board names -----------------------------------------------------------------------------------
def _image(hdr=32, body=b"\0" * 16, tlvs=((0x10, b"\x11" * 32),), prot=()):
    h = struct.pack("<IIHHII", 0x96F3B83D, 0, hdr, 0, len(body), 0) + struct.pack("<BBHI", 1, 2, 3, 0)
    h = h.ljust(hdr, b"\0")
    out = h + body
    if prot:
        area = b"".join(struct.pack("<HH", t, len(v)) + v for t, v in prot)
        out += struct.pack("<HH", 0x6908, 4 + len(area)) + area
        h2 = bytearray(out[:hdr])
        struct.pack_into("<H", h2, 10, 4 + len(area))
        out = bytes(h2) + out[hdr:]
    area = b"".join(struct.pack("<HH", t, len(v)) + v for t, v in tlvs)
    return out + struct.pack("<HH", 0x6907, 4 + len(area)) + area


@prop
@given(body=st.binary(max_size=200), board=st.one_of(st.none(), u32), extra=st.lists(
    st.tuples(st.integers(0x40, 0x9F), st.binary(max_size=40)), max_size=3))
def image_round_trip(body, board, extra):
    tlvs = [(0x10, b"\x22" * 32)] + extra + ([(0xA0, struct.pack("<I", board))] if board is not None else [])
    img = Image(_image(body=body, tlvs=tlvs))
    assert img.hash == b"\x22" * 32 and img.board_type == board and img.version == "1.2.3" and not img.encrypted


@prop
@given(cut=st.integers(0, 80), flips=st.lists(st.tuples(st.integers(0, 200), u8), max_size=6),
       tail=st.binary(max_size=20))
@example(cut=20, flips=[], tail=b"")                             # the hash TLV cut short
def image_damaged_only_umcub_errors(cut, flips, tail):
    """A real image cut, with bytes changed or appended: refused with UmcubError or parsed, never another error -
    and a parsed one has a hash of its algorithm's length."""
    data = bytearray(_image(tlvs=[(0x10, b"\x22" * 32), (0xA0, b"\x01\x00\x00\x00")]))
    for i, v in flips:
        data[i % len(data)] = v
    data = bytes(data[:len(data) - cut]) + tail
    try:
        img = Image(data)
    except UmcubError:
        return
    assert len(img.hash) in (32, 48, 64)


@prop
@given(cut=st.integers(1, 40))
def image_tlv_past_the_end_refused(cut):
    """A TLV area longer than the data: refused, not a short hash taken as the image's."""
    try:
        Image(_image()[:-cut])
    except UmcubError as e:
        assert e.code in ("image.truncated", "image.no_hash"), e.code
        return
    raise AssertionError("accepted")


@prop
@given(names=st.dictionaries(u32, st.text(min_size=1, max_size=20).filter(lambda s: s.strip() == s and s),
                             max_size=5))
def board_names_round_trip(names):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "n.json"
        p.write_text(json.dumps({f"0x{k:08X}": v for k, v in names.items()}), encoding="utf-8")
        load_board_names(p)
        for k, v in names.items():
            assert v in board_label(k)


@prop
@given(text=st.text(max_size=80))
def board_names_bad_file_only_umcub_errors(text):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "n.json"
        p.write_text(text, encoding="utf-8")
        try:
            load_board_names(p)
        except UmcubError:
            pass


# --- connection specs -----------------------------------------------------------------------------------------------
@prop
@given(spec=st.text(max_size=60), node=st.one_of(st.none(), st.integers(0, 255)))
def connection_spec_only_umcub_errors(spec, node):
    try:
        t, target, opts = _parse(spec)
        exclusive_key(spec, node)
    except UmcubError:
        return
    scheme, sep, _ = spec.strip().partition(":")
    if sep and scheme in umcub.transports():
        assert t.scheme == scheme


def main():
    global bad
    for p in PROPS:
        try:
            p()
        except Exception as e:                                  # noqa: BLE001  (Hypothesis prints the example)
            bad += 1
            print(f"FAIL {p.__name__}: {type(e).__name__}: {str(e).splitlines()[0] if str(e) else ''}")
    print(f"properties: {'FAILED' if bad else f'all {len(PROPS)} hold'}" + (" (random)" if RANDOM else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
