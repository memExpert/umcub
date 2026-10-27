"""Host side of umcub link: several umcub devices on one bus (RS485, UDP).

Frames, addressing, DISCOVER and the SECURE session (challenge, AUTH, frame
MACs, payload encryption); the protocol is described in
src/proto/include/umcub_link.h and docs/link.md.
"""
import base64
import hashlib
import hmac
import os
import struct
import time

from .connection import KIND_LINK
from .errors import LinkError

MAGIC, VERSION = 0xA5, 1
T_DISCOVER, T_ANNOUNCE, T_HELLO, T_CHALLENGE, T_AUTH, T_AUTH_OK, T_DATA, T_CLOSE = range(1, 9)
F_MAC, F_ENC, F_UID = 0x01, 0x02, 0x04
BROADCAST, HOST_ADDR = 0xFFFF, 0xFFFE
HDR, TAG = 14, 16
LINK_NAMES = {0: "plain", 1: "addressed", 2: "secure"}
ANNOUNCE_ENC, ANNOUNCE_CLOSED = 0x40, 0x80
LABEL = b"umcub-link-v1"
FRAME_NAMES = {T_DISCOVER: "DISCOVER", T_ANNOUNCE: "ANNOUNCE", T_HELLO: "HELLO", T_CHALLENGE: "CHALLENGE",
               T_AUTH: "AUTH", T_AUTH_OK: "AUTH_OK", T_DATA: "DATA", T_CLOSE: "CLOSE"}


# --------------------------------------------------------------------------
# Frames
# --------------------------------------------------------------------------

def make_frame(type_, flags, dst, seq, payload, src=HOST_ADDR):
    return struct.pack("<BBBBHHIH", MAGIC, VERSION, type_, flags, dst, src, seq, len(payload)) + payload


def parse_frame(f):
    if len(f) < HDR or f[0] != MAGIC or f[1] != VERSION:
        return None
    _, _, type_, flags, dst, src, seq, plen = struct.unpack_from("<BBBBHHIH", f)
    need = HDR + plen + (TAG if flags & F_MAC else 0)
    if len(f) != need:
        return None
    return dict(type=type_, flags=flags, dst=dst, src=src, seq=seq, payload=f[HDR:HDR + plen],
                tag=f[HDR + plen:] if flags & F_MAC else b"", raw=f)


def identity(p):
    """ANNOUNCE / CHALLENGE payload: who the device is."""
    if len(p) < 24:
        return None
    uid = p[0:12]
    addr, btype, brev = struct.unpack_from("<HIH", p, 12)
    mode = p[20]
    return dict(uid=uid.hex(), addr=addr, board_type=btype, board_rev=brev, link=mode & 0x3f,
                enc=bool(mode & ANNOUNCE_ENC), closed=bool(mode & ANNOUNCE_CLOSED),
                version=f"{p[21]}.{p[22]}.{p[23]}")


def describe_identity(i):
    return (f"node {i['addr']:5d}  uid {i['uid']}  board 0x{i['board_type']:08x} rev {i['board_rev']}"
            f"  {LINK_NAMES.get(i['link'], '?')}{' enc' if i['enc'] else ''}{' closed' if i['closed'] else ''}"
            f"  umcub {i['version']}")


def describe_frame(f):
    """One frame as text: type, src -> dst, seq, flags, payload summary."""
    fr = parse_frame(f)
    if not fr:
        return f"?? {len(f)} B, not a link frame: {f[:32].hex()}"
    name = {BROADCAST: "bcast", HOST_ADDR: "host"}
    flags = ",".join(n for b, n in ((F_MAC, "MAC"), (F_ENC, "ENC"), (F_UID, "UID")) if fr["flags"] & b) or "-"
    s = (f"{FRAME_NAMES.get(fr['type'], fr['type']):9} {name.get(fr['src'], fr['src']):>5} -> "
         f"{name.get(fr['dst'], fr['dst']):<5} seq={fr['seq']:<10} [{flags}] len={len(fr['payload'])}")
    p = fr["payload"]
    i = identity(p) if fr["type"] in (T_ANNOUNCE, T_CHALLENGE) else None
    if i:
        s += (f"\n    uid {i['uid']} addr {i['addr']} board 0x{i['board_type']:08x} rev {i['board_rev']} "
              f"{LINK_NAMES.get(i['link'], '?')}{' enc' if i['enc'] else ''}{' closed' if i['closed'] else ''}"
              f" umcub {i['version']}")
    elif fr["type"] == T_DATA and fr["flags"] & F_ENC:
        s += f"\n    <encrypted {len(p)} B>"
    elif p and all(32 <= c < 127 or c in (9, 10, 13) for c in p):
        s += f"\n    {p.decode('ascii').strip()!r}"
    elif p:
        s += f"\n    {p[:48].hex()}{'...' if len(p) > 48 else ''}"
    return s


def sniff(port, baud=115200, raw=False, out=print):
    """Report every link frame seen on a serial port (never writes to it) as
    out(text, seconds since start); runs until interrupted."""
    import serial
    s = serial.Serial(port, baud, timeout=0.1)
    s.reset_input_buffer()
    start = bytes([0x05, KIND_LINK])
    buf, t0 = b"", time.monotonic()
    while True:
        buf += s.read(max(1, s.in_waiting))     # timestamps as the bytes arrive
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            ts = time.monotonic() - t0
            i = line.find(start)
            if i >= 0:
                try:
                    out(describe_frame(base64.b64decode(line[i + 2:].strip(), validate=True)), ts)
                    continue
                except ValueError:
                    pass
            if raw and line.strip():
                out(f"raw {line[:64].hex()}", ts)


# --------------------------------------------------------------------------
# Secure session (mirrors src/proto/link.c)
# --------------------------------------------------------------------------

def _hmac(key, data):
    return hmac.new(key, data, hashlib.sha256).digest()


def _ctr(key, seq, data):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    block = struct.pack("<I", seq) + bytes(12)      # seq LE | 8 x 0 | block counter BE from 0
    c = Cipher(algorithms.AES(key), modes.CTR(block)).encryptor()
    return c.update(data) + c.finalize()


class Session:
    def __init__(self, keys):
        self.mac_h2d, self.mac_d2h = keys[0:32], keys[32:64]
        self.enc_h2d, self.enc_d2h = keys[64:80], keys[80:96]
        self.rx_seq = -1

    def check(self, fr):
        want = _hmac(self.mac_d2h, fr["raw"][:HDR + len(fr["payload"])])[:TAG]
        if not fr["flags"] & F_MAC or not hmac.compare_digest(want, fr["tag"]) or fr["seq"] <= self.rx_seq:
            return False
        self.rx_seq = fr["seq"]
        return True


def derive(th, shared):
    """HKDF-SHA256(salt th, ikm shared, info LABEL ' keys', 96 bytes)."""
    prk = _hmac(th, shared)
    info = LABEL + b" keys"
    out, t = b"", b""
    for n in range(1, 4):
        t = _hmac(prk, t + info + bytes([n]))
        out += t
    return out


def load_key(path, private):
    from cryptography.hazmat.primitives import serialization
    try:
        data = open(path, "rb").read()
    except OSError as e:
        raise LinkError(f"{path}: {e.strerror}") from e
    try:
        k = serialization.load_pem_private_key(data, password=None)
        return k if private else k.public_key()
    except (ValueError, TypeError):
        if private:
            raise LinkError(f"{path}: a private key is needed")
        return serialization.load_pem_public_key(data)


# --------------------------------------------------------------------------
# Link endpoint
# --------------------------------------------------------------------------

class Link:
    """One host on a link bus; `addr` or `uid` selects the device for hello()
    and request(), discover() needs neither."""

    def __init__(self, conn, addr=None, uid=None, admin_key=None, device_key=None, timeout=2.0):
        self.conn, self.addr, self.uid = conn, addr, uid
        self.admin_key, self.device_key = admin_key, device_key
        self.timeout = timeout
        self.seq = int(time.time()) & 0x3FFFFFFF        # monotonic across restarts of the tool
        self.session = None
        self.ident = None
        self.peer = None

    def next_seq(self):
        self.seq += 1
        return self.seq

    @property
    def dst(self):
        return 0 if self.uid is not None else self.addr

    def send(self, type_, payload=b"", flags=0, dst=None):
        seq = self.next_seq()
        f = make_frame(type_, flags, self.dst if dst is None else dst, seq, payload)
        if self.session and type_ in (T_DATA, T_CLOSE):
            f = bytearray(f)
            if type_ == T_DATA and payload and self.ident and self.ident["enc"]:
                f[3] |= F_ENC
                f[HDR:] = _ctr(self.session.enc_h2d, seq, payload)
            f[3] |= F_MAC
            f = bytes(f) + _hmac(self.session.mac_h2d, bytes(f))[:TAG]
        self.conn.send(KIND_LINK, f)
        return seq

    def recv(self, types, timeout=None, src=None):
        end = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            raw = self.conn.recv(KIND_LINK, max(0.0, end - time.monotonic()))
            if raw is None:
                return None
            fr = parse_frame(raw)
            if not fr:
                self.conn.noise += 1
                continue
            if fr["dst"] != HOST_ADDR or fr["type"] not in types:
                continue
            if src is not None and fr["src"] != src:
                continue
            return fr

    def hello(self):
        """Select the device; SECURE: run the challenge / AUTH handshake. Returns its identity."""
        if self.addr is None and self.uid is None:
            raise LinkError("select the device by node address or UID")
        if self.uid is not None:
            self.send(T_HELLO, bytes.fromhex(self.uid), flags=F_UID, dst=0)
        else:
            self.send(T_HELLO)
        fr = self.recv((T_ANNOUNCE, T_CHALLENGE))
        if not fr:
            raise LinkError("no answer to HELLO (wrong node, not in recovery, or not a link transport)")
        self.ident = identity(fr["payload"])
        self.peer = fr["src"]
        if fr["type"] == T_ANNOUNCE:
            if self.ident["closed"]:
                raise LinkError("device refuses SECURE sessions (readout protection off, UMCUB_CFG_LINK_REQUIRE_RDP)")
            return self.ident
        if not (self.admin_key and self.device_key):
            raise LinkError("SECURE transport: the admin key and the device key are needed")
        self.authenticate(fr["payload"])
        return self.ident

    def authenticate(self, challenge):
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec, utils
        admin = load_key(self.admin_key, True)
        devpub = load_key(self.device_key, False)
        eph = ec.generate_private_key(ec.SECP256R1())
        n = eph.public_key().public_numbers()
        eph_pub = n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")
        nonce_h = os.urandom(32)
        th = hashlib.sha256(LABEL + struct.pack("<H", HOST_ADDR) + challenge + eph_pub + nonce_h).digest()
        r, s = utils.decode_dss_signature(admin.sign(th, ec.ECDSA(utils.Prehashed(hashes.SHA256()))))
        sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        shared = eph.exchange(ec.ECDH(), devpub)
        session = Session(derive(th, shared))
        self.send(T_AUTH, eph_pub + nonce_h + sig)
        fr = self.recv((T_AUTH_OK,), timeout=max(self.timeout, 5.0), src=self.peer)   # ECC on a M3: ~1.5 s
        if not fr or not session.check(fr):
            raise LinkError("authentication failed (wrong admin key, device key or no answer)")
        self.session = session

    def send_data(self, payload):
        self.send(T_DATA, payload)

    def recv_data(self, timeout=None):
        """The next DATA payload of the selected device (None on timeout)."""
        end = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            fr = self.recv((T_DATA,), timeout=max(0.0, end - time.monotonic()), src=self.peer)
            if not fr:
                return None
            if self.session:
                if not self.session.check(fr):
                    continue                    # forged / replayed: ignored
                data = fr["payload"]
                if fr["flags"] & F_ENC:
                    data = _ctr(self.session.enc_d2h, fr["seq"], data)
                return data
            return fr["payload"]

    def request(self, payload, timeout=None):
        """DATA round trip (raw SMP packet or text command)."""
        self.send_data(payload)
        return self.recv_data(timeout)

    def close(self):
        if self.session:
            self.send(T_CLOSE)
            self.session = None

    def discover(self, rounds=8, slots=16, slot_ms=20):
        """Devices answer DISCOVER in a random slot; two in the same slot collide
        and the host gets garbage. Found UIDs are listed in the next DISCOVER
        (they stay silent), the others draw new slots. Ends after a quiet round
        (nothing new, nothing garbled) or after `rounds` rounds."""
        found = {}
        for _ in range(rounds):
            known = b"".join(bytes.fromhex(u) for u in list(found)[:80])
            noise = self.conn.noise
            self.send(T_DISCOVER, struct.pack("<BH", slots, slot_ms) + known, dst=BROADCAST)
            end = time.monotonic() + slots * slot_ms / 1000.0 + 0.3
            new = 0
            while time.monotonic() < end:
                fr = self.recv((T_ANNOUNCE,), timeout=end - time.monotonic())
                if fr:
                    ident = identity(fr["payload"])
                    if ident and ident["uid"] not in found:
                        found[ident["uid"]] = ident
                        new += 1
            if not new and self.conn.noise == noise:
                break
        return list(found.values())
