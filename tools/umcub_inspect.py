#!/usr/bin/env python3
"""Check what a umcub bootloader has in its image slots (SMP group 100).

  umcub_inspect.py --port /dev/ttyACM1 verify 0 1             full MCUboot validation of image 0, slot 1
  umcub_inspect.py --port /dev/ttyACM1 hash 0 1 --file app.signed.bin
                                                              SHA-256 of the slot vs. the file that was sent
  umcub_inspect.py --udp 192.168.1.50 read 0 0 --out dump.bin [--len N]
                                                              raw readback (needs UMCUB_CFG_READBACK)

Slots: 0 primary, 1 secondary. Transports: serial (UART / USB CDC, SMP
serial framing) or UDP (port 1337). The bootloader must be in recovery mode.
"""
import argparse
import base64
import hashlib
import socket
import struct
import sys

import cbor2

GROUP = 100
ID_VERIFY, ID_HASH, ID_READ = 0, 1, 2
ERRORS = {3: "invalid argument", 5: "no image in that slot", 8: "not supported (feature disabled?)"}


def crc16_xmodem(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class SerialSmp:
    def __init__(self, port, baud, timeout):
        import serial
        self.s = serial.Serial(port, baud, timeout=timeout)

    def transfer(self, pkt):
        raw = struct.pack(">H", len(pkt) + 2) + pkt + struct.pack(">H", crc16_xmodem(pkt))
        b64 = base64.b64encode(raw)
        out = b""
        for i in range(0, len(b64), 124):
            out += (b"\x06\x09" if i == 0 else b"\x04\x14") + b64[i:i + 124] + b"\n"
        self.s.reset_input_buffer()
        self.s.write(out)
        data = b""
        while True:
            line = self.s.readline()
            if not line:
                raise TimeoutError("no response")
            if line[:2] not in (b"\x06\x09", b"\x04\x14"):
                continue                       # bootloader log lines
            data += base64.b64decode(line[2:].strip())
            if len(data) >= 2 and len(data) >= struct.unpack(">H", data[:2])[0] + 2:
                total = struct.unpack(">H", data[:2])[0]
                body = data[2:2 + total]
                if crc16_xmodem(body) != 0:
                    raise IOError("CRC error")
                return body[:-2]


class UdpSmp:
    def __init__(self, host, port, timeout):
        self.addr = (host, port)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(timeout)

    def transfer(self, pkt):
        self.s.sendto(pkt, self.addr)
        return self.s.recv(4096)


def request(link, cmd_id, payload, seq=[0]):
    body = cbor2.dumps(payload)
    seq[0] = (seq[0] + 1) & 0xFF
    rsp = link.transfer(struct.pack(">BBHHBB", 0, 0, len(body), GROUP, seq[0], cmd_id) + body)
    _, _, length, group, _, rid = struct.unpack(">BBHHBB", rsp[:8])
    if (group, rid) != (GROUP, cmd_id):
        raise IOError(f"unexpected response group {group} id {rid}")
    r = cbor2.loads(rsp[8:8 + length])
    if r.get("rc", 0):
        raise SystemExit(f"device: {ERRORS.get(r['rc'], 'error')} (rc {r['rc']})")
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port (UART / USB CDC)")
    g.add_argument("--udp", help="device IP address")
    ap.add_argument("--udp-port", type=int, default=1337)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("cmd", choices=("verify", "hash", "read"))
    ap.add_argument("image", type=int)
    ap.add_argument("slot", type=int)
    ap.add_argument("--file", help="hash: compare with this file (e.g. the .signed.bin that was sent)")
    ap.add_argument("--off", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--len", type=lambda s: int(s, 0), default=0, help="0 = whole stored image")
    ap.add_argument("--out", help="read: output file")
    a = ap.parse_args()

    link = SerialSmp(a.port, a.baud, a.timeout) if a.port else UdpSmp(a.udp, a.udp_port, a.timeout)
    req = {"image": a.image, "slot": a.slot}

    if a.cmd == "verify":
        r = request(link, ID_VERIFY, req)
        print("valid" if r.get("valid") else "INVALID (hash or signature mismatch)")
        sys.exit(0 if r.get("valid") else 1)

    if a.cmd == "hash":
        length = a.len
        local = None
        if a.file:
            data = open(a.file, "rb").read()[a.off:]
            length = length or len(data)
            local = hashlib.sha256(data[:length]).digest()
        r = request(link, ID_HASH, dict(req, off=a.off, len=length))
        print(f"device sha256 {r['sha'].hex()} ({r['len']} bytes)")
        if local is not None:
            print(f"file   sha256 {local.hex()}")
            print("MATCH" if local == r["sha"] else "MISMATCH")
            sys.exit(0 if local == r["sha"] else 1)
        return

    # read: 128-byte chunks (bootloader response buffer)
    if not a.len:
        r = request(link, ID_HASH, req)        # length of the stored image
        a.len = r["len"] - a.off
    out = bytearray()
    while len(out) < a.len:
        n = min(128, a.len - len(out))
        r = request(link, ID_READ, dict(req, off=a.off + len(out), len=n))
        out += r["data"]
        print(f"\r{len(out)}/{a.len}", end="", flush=True)
    print()
    if a.out:
        open(a.out, "wb").write(out)
    else:
        sys.stdout.buffer.write(bytes(out))


if __name__ == "__main__":
    main()
