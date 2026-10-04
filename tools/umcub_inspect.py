#!/usr/bin/env python3
"""Check what a umcub bootloader has in its image slots (SMP group 100).

  umcub_inspect.py --port /dev/ttyACM1 verify 0 1             full MCUboot validation of image 0, slot 1
  umcub_inspect.py --port /dev/ttyACM1 hash 0 1 --file app.signed.bin
                                                              SHA-256 of the slot vs. the file that was sent
  umcub_inspect.py --udp 192.168.1.50 read 0 0 --out dump.bin [--len N]
                                                              raw readback (needs UMCUB_CFG_READBACK)

Slots: 0 primary, 1 secondary. Transports: serial (UART / USB CDC, SMP
serial framing) or UDP (port 1337), through smpclient. The bootloader must be
in recovery mode. For a umcub link transport go through `umcub_link.py serve`.
"""
import argparse
import asyncio
import functools
import hashlib
import os
import sys

from smpclient import SMPClient
from smpclient.generics import error, success
from smpclient.transport.serial import BufferSize, SMPSerialTransport
from smpclient.transport.udp import SMPUDPTransport

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from umcub_smp import INSPECT_ERRORS, InspectHash, InspectRead, InspectVerify  # noqa: E402


async def request(client, req):
    r = await client.request(req)
    if error(r):
        rc = int(r.rc) if hasattr(r, "rc") else int(r.err.rc)
        sys.exit(f"device: {INSPECT_ERRORS.get(rc, 'error')} (rc {rc})")
    if success(r) and r.rc:
        sys.exit(f"device: {INSPECT_ERRORS.get(r.rc, 'error')} (rc {r.rc})")
    return r


async def run(a):
    if a.port:
        transport = SMPSerialTransport(BufferSize(1024), baudrate=a.baud)
        address = a.port
    else:
        transport = SMPUDPTransport()
        transport.connect = functools.partial(transport.connect, port=a.udp_port)
        address = a.udp
    async with SMPClient(transport, address, timeout_s=a.timeout) as client:
        sel = {"image": a.image, "slot": a.slot}

        if a.cmd == "verify":
            r = await request(client, InspectVerify(**sel))
            print("valid" if r.valid else "INVALID (hash or signature mismatch)")
            return 0 if r.valid else 1

        if a.cmd == "hash":
            length, local = a.len, None
            if a.file:
                data = open(a.file, "rb").read()[a.off:]
                length = length or len(data)
                local = hashlib.sha256(data[:length]).digest()
            r = await request(client, InspectHash(**sel, off=a.off or None, len=length or None))
            print(f"device sha256 {r.sha.hex()} ({r.len} bytes)")
            if local is not None:
                print(f"file   sha256 {local.hex()}")
                print("MATCH" if local == r.sha else "MISMATCH")
                return 0 if local == r.sha else 1
            return 0

        # read: 128-byte chunks (bootloader response buffer)
        if not a.len:
            a.len = (await request(client, InspectHash(**sel))).len - a.off   # length of the stored image
        out = bytearray()
        while len(out) < a.len:
            n = min(128, a.len - len(out))
            out += (await request(client, InspectRead(**sel, off=a.off + len(out), len=n))).data
            print(f"\r{len(out)}/{a.len}", end="", file=sys.stderr, flush=True)
        print(file=sys.stderr)
        if a.out:
            open(a.out, "wb").write(out)
        else:
            sys.stdout.buffer.write(bytes(out))
        return 0


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
    sys.exit(asyncio.run(run(ap.parse_args())))


if __name__ == "__main__":
    main()
