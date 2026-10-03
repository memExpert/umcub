#!/usr/bin/env python3
"""Host side of umcub link: several umcub devices on one bus (RS485, CAN, UDP).

  umcub_link.py --port /dev/ttyUSB0 discover
  umcub_link.py --port /dev/ttyUSB0 --addr 5 info
  umcub_link.py --port /dev/ttyUSB0 --uid 4300.. cmd i
  umcub_link.py --port /dev/ttyUSB0 --addr 5 --admin-key admin.pem --device-key device.pem serve --pty
  umcub_link.py --udp 192.168.1.50 --addr 5 serve --udp-listen 127.0.0.1:1337

`serve` is a proxy for standard SMP clients: it opens a pseudo terminal (point
smpmgr / mcumgr --conntype serial at the printed path) or a local UDP port
(mcumgr --conntype udp) and forwards their requests to the selected device,
doing addressing and - on SECURE transports - the authentication, the frame
MACs and the payload encryption. The protocol is described in
transport/include/umcub_link.h.

SECURE transports need --admin-key (private EC P-256 key; the bootloader holds
its public half) and --device-key (the device key, public half is enough).
"""
import argparse
import base64
import os
import select
import socket
import struct
import sys
import time

MAGIC, VERSION = 0xA5, 1
T_DISCOVER, T_ANNOUNCE, T_HELLO, T_CHALLENGE, T_AUTH, T_AUTH_OK, T_DATA, T_CLOSE = range(1, 9)
F_MAC, F_ENC, F_UID = 0x01, 0x02, 0x04
BROADCAST, HOST_ADDR = 0xFFFF, 0xFFFE
HDR, TAG = 14, 16
LINE_START = b"\x05\x0b"
LINK_NAMES = {0: "plain", 1: "addressed", 2: "secure"}
ANNOUNCE_ENC, ANNOUNCE_CLOSED = 0x40, 0x80
LABEL = b"umcub-link-v1"


class LinkError(Exception):
    pass


# --------------------------------------------------------------------------
# Frames and transports
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


class SerialBus:
    """Stream transport: frames as '05 0B base64 \\n' lines; anything else is ignored."""

    def __init__(self, port, baud):
        import serial
        self.s = serial.Serial(port, baud, timeout=0)
        self.buf = b""

    def send(self, frame):
        self.s.write(LINE_START + base64.b64encode(frame) + b"\n")
        self.s.flush()

    def recv(self, timeout):
        end = time.monotonic() + timeout
        while True:
            nl = self.buf.find(b"\n")
            while nl >= 0:
                line, self.buf = self.buf[:nl], self.buf[nl + 1:]
                i = line.find(LINE_START)
                if i >= 0:
                    try:
                        return base64.b64decode(line[i + 2:].strip(), validate=True)
                    except ValueError:
                        pass
                nl = self.buf.find(b"\n")
            left = end - time.monotonic()
            if left <= 0:
                return None
            self.s.timeout = min(left, 0.05)
            chunk = self.s.read(4096)
            if chunk:
                self.buf += chunk


class UdpBus:
    def __init__(self, host, port):
        self.addr = (host, port)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    def send(self, frame):
        self.s.sendto(frame, self.addr)

    def recv(self, timeout):
        r, _, _ = select.select([self.s], [], [], max(timeout, 0))
        return self.s.recv(4096) if r else None


# --------------------------------------------------------------------------
# Secure session (mirrors transport/link.c)
# --------------------------------------------------------------------------

class Session:
    def __init__(self, keys):
        self.mac_h2d, self.mac_d2h = keys[0:32], keys[32:64]
        self.enc_h2d, self.enc_d2h = keys[64:80], keys[80:96]
        self.rx_seq = -1

    @staticmethod
    def hmac(key, data):
        import hmac
        import hashlib
        return hmac.new(key, data, hashlib.sha256).digest()

    @staticmethod
    def ctr(key, seq, data):
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        block = struct.pack("<I", seq) + bytes(12)      # seq LE | 8 x 0 | block counter BE from 0
        c = Cipher(algorithms.AES(key), modes.CTR(block)).encryptor()
        return c.update(data) + c.finalize()

    def check(self, fr):
        import hmac
        want = self.hmac(self.mac_d2h, fr["raw"][:HDR + len(fr["payload"])])[:TAG]
        if not fr["flags"] & F_MAC or not hmac.compare_digest(want, fr["tag"]) or fr["seq"] <= self.rx_seq:
            return False
        self.rx_seq = fr["seq"]
        return True


def derive(th, shared):
    """HKDF-SHA256(salt th, ikm shared, info LABEL ' keys', 96 bytes)."""
    prk = Session.hmac(th, shared)
    info = LABEL + b" keys"
    out, t = b"", b""
    for n in range(1, 4):
        t = Session.hmac(prk, t + info + bytes([n]))
        out += t
    return out


def load_key(path, private):
    from cryptography.hazmat.primitives import serialization
    data = open(path, "rb").read()
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

def identity(p):
    if len(p) < 24:
        return None
    uid = p[0:12]
    addr, btype, brev = struct.unpack_from("<HIH", p, 12)
    mode = p[20]
    return dict(uid=uid.hex(), addr=addr, board_type=btype, board_rev=brev, link=mode & 0x3f,
                enc=bool(mode & ANNOUNCE_ENC), closed=bool(mode & ANNOUNCE_CLOSED),
                version=f"{p[21]}.{p[22]}.{p[23]}")


class Link:
    def __init__(self, bus, addr=None, uid=None, admin_key=None, device_key=None, timeout=2.0):
        self.bus, self.addr, self.uid = bus, addr, uid
        self.admin_key, self.device_key = admin_key, device_key
        self.timeout = timeout
        self.seq = int(time.time()) & 0x3FFFFFFF        # monotonic across restarts of the tool
        self.session = None
        self.ident = None

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
                f[HDR:] = Session.ctr(self.session.enc_h2d, seq, payload)
            f[3] |= F_MAC
            f = bytes(f) + Session.hmac(self.session.mac_h2d, bytes(f))[:TAG]
        self.bus.send(f)
        return seq

    def recv(self, types, timeout=None, src=None):
        end = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            raw = self.bus.recv(max(0.0, end - time.monotonic()))
            if raw is None:
                return None
            fr = parse_frame(raw)
            if not fr or fr["dst"] != HOST_ADDR or fr["type"] not in types:
                continue
            if src is not None and fr["src"] != src:
                continue
            return fr

    def hello(self):
        """Select the device; SECURE: run the challenge / AUTH handshake."""
        if self.uid is not None:
            self.send(T_HELLO, bytes.fromhex(self.uid), flags=F_UID, dst=0)
        else:
            self.send(T_HELLO)
        fr = self.recv((T_ANNOUNCE, T_CHALLENGE))
        if not fr:
            raise LinkError("no answer to HELLO")
        self.ident = identity(fr["payload"])
        self.peer = fr["src"]
        if fr["type"] == T_ANNOUNCE:
            if self.ident["closed"]:
                raise LinkError("device refuses SECURE sessions (readout protection off, UMCUB_CFG_LINK_REQUIRE_RDP)")
            return self.ident
        if not (self.admin_key and self.device_key):
            raise LinkError("SECURE transport: --admin-key and --device-key are needed")
        self.authenticate(fr["payload"])
        return self.ident

    def authenticate(self, challenge):
        import hashlib
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

    def request(self, payload, timeout=None):
        """DATA round trip (raw SMP packet or text command)."""
        self.send(T_DATA, payload)
        while True:
            fr = self.recv((T_DATA,), timeout=timeout, src=self.peer)
            if not fr:
                return None
            if self.session:
                if not self.session.check(fr):
                    continue                    # forged / replayed: ignored
                data = fr["payload"]
                if fr["flags"] & F_ENC:
                    data = Session.ctr(self.session.enc_d2h, fr["seq"], data)
                return data
            return fr["payload"]

    def close(self):
        if self.session:
            self.send(T_CLOSE)
            self.session = None

    def discover(self, rounds=3, slots=16, slot_ms=20):
        found = {}
        for _ in range(rounds):
            known = b"".join(bytes.fromhex(u) for u in list(found)[:80])
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
            if not new:
                break
        return list(found.values())


# --------------------------------------------------------------------------
# SMP serial framing for the pty side (mcumgr / smpmgr)
# --------------------------------------------------------------------------

def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def nlip_encode(pkt):
    raw = struct.pack(">H", len(pkt) + 2) + pkt + struct.pack(">H", crc16(pkt))
    b64 = base64.b64encode(raw)
    out = b""
    for i in range(0, len(b64), 124):
        out += (b"\x06\x09" if i == 0 else b"\x04\x14") + b64[i:i + 124] + b"\n"
    return out


class NlipDecoder:
    def __init__(self):
        self.line, self.data = b"", b""

    def feed(self, chunk):
        pkts = []
        for c in chunk:
            if c != 0x0A:
                self.line += bytes([c])
                continue
            line, self.line = self.line.strip(b"\r"), b""
            if line[:2] == b"\x06\x09":
                self.data = b""
            elif line[:2] != b"\x04\x14":
                continue
            try:
                self.data += base64.b64decode(line[2:])
            except ValueError:
                self.data = b""
                continue
            if len(self.data) >= 2 and len(self.data) >= struct.unpack(">H", self.data[:2])[0] + 2:
                total = struct.unpack(">H", self.data[:2])[0]
                body = self.data[2:2 + total]
                self.data = b""
                if crc16(body) == 0:
                    pkts.append(body[:-2])
        return pkts


def serve(link, a):
    link.hello()
    print(f"connected to node {link.ident['addr']} uid {link.ident['uid']} "
          f"({LINK_NAMES.get(link.ident['link'], '?')}{', encrypted' if link.ident['enc'] else ''})", flush=True)
    last = time.monotonic()
    if a.udp_listen:
        host, port = a.udp_listen.rsplit(":", 1)
        srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        srv.bind((host, int(port)))
        print(f"SMP over UDP: mcumgr --conntype udp --connstring={host}:{port}", flush=True)
        while True:
            pkt, peer = srv.recvfrom(4096)
            if link.session and time.monotonic() - last > 20:
                link.hello()                    # session may have timed out on the device
            rsp = link.request(pkt, timeout=a.timeout)
            last = time.monotonic()
            if rsp:
                srv.sendto(rsp, peer)
    import pty
    import tty
    master, slave = pty.openpty()
    tty.setraw(slave)
    path = os.ttyname(slave)
    if a.pty_link:
        try:
            os.unlink(a.pty_link)
        except FileNotFoundError:
            pass
        os.symlink(path, a.pty_link)
        path = a.pty_link
    print(f"SMP serial: smpmgr --port {path}  /  mcumgr --conntype serial --connstring dev={path}", flush=True)
    dec = NlipDecoder()
    while True:
        r, _, _ = select.select([master], [], [], 1.0)
        if not r:
            continue
        for pkt in dec.feed(os.read(master, 4096)):
            if link.session and time.monotonic() - last > 20:
                link.hello()
            rsp = link.request(pkt, timeout=a.timeout)
            last = time.monotonic()
            if rsp:
                os.write(master, nlip_encode(rsp))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port of the bus (RS485 adapter, UART, USB CDC)")
    g.add_argument("--udp", help="device IP address (or broadcast address for discover)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--udp-port", type=int, default=1337)
    sel = ap.add_mutually_exclusive_group()
    sel.add_argument("--addr", type=int, help="node address of the device")
    sel.add_argument("--uid", help="UID of the device (hex), for unassigned nodes")
    ap.add_argument("--admin-key", help="SECURE: private admin key (PEM)")
    ap.add_argument("--device-key", help="SECURE: device key (PEM, public half is enough)")
    ap.add_argument("--timeout", type=float, default=3.0)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("discover")
    d.add_argument("--rounds", type=int, default=3)
    d.add_argument("--slots", type=int, default=16)
    d.add_argument("--slot-ms", type=int, default=20)
    sub.add_parser("info")
    c = sub.add_parser("cmd")
    c.add_argument("text")
    s = sub.add_parser("serve")
    s.add_argument("--pty", action="store_true", help="pseudo terminal for SMP serial clients (default)")
    s.add_argument("--pty-link", help="also create this symlink to the pty")
    s.add_argument("--udp-listen", help="local HOST:PORT for SMP over UDP clients")
    a = ap.parse_args()

    bus = SerialBus(a.port, a.baud) if a.port else UdpBus(a.udp, a.udp_port)
    link = Link(bus, a.addr, a.uid, a.admin_key, a.device_key, a.timeout)
    try:
        if a.cmd == "discover":
            devs = link.discover(a.rounds, a.slots, a.slot_ms)
            for d_ in devs:
                print(f"node {d_['addr']:5d}  uid {d_['uid']}  board 0x{d_['board_type']:08x} rev {d_['board_rev']}"
                      f"  {LINK_NAMES.get(d_['link'], '?')}{' enc' if d_['enc'] else ''}"
                      f"{' closed' if d_['closed'] else ''}  umcub {d_['version']}")
            print(f"{len(devs)} device(s)")
            return
        if a.addr is None and a.uid is None:
            sys.exit("select the device with --addr or --uid")
        if a.cmd == "info":
            i = link.hello()
            for k in ("addr", "uid", "board_type", "board_rev", "version"):
                print(f"{k:10s} {hex(i[k]) if k == 'board_type' else i[k]}")
            print(f"{'link':10s} {LINK_NAMES.get(i['link'], '?')}{' (encrypted)' if i['enc'] else ''}"
                  f"{' (authenticated)' if link.session else ''}")
            link.close()
        elif a.cmd == "cmd":
            link.hello()
            rsp = link.request(a.text.encode())
            link.close()
            if rsp is None:
                sys.exit("no answer")
            sys.stdout.write(rsp.decode(errors="replace"))
        else:
            serve(link, a)
    except LinkError as e:
        sys.exit(f"umcub_link: {e}")
    except KeyboardInterrupt:
        link.close()


if __name__ == "__main__":
    main()
