#!/usr/bin/env python3
"""SMP (mcumgr) client for the umcub CAN transport (ISO-TP over python-can).

  smp_can.py [--interface socketcan --channel can0] [--fd] list
  smp_can.py upload app.signed.bin [--image 0]
  smp_can.py reset
  smp_can.py echo hello

Default ids match the umcub defaults: host->device 0x7C0, device->host 0x7C8
(+ the node address with --addr). The SMP side is smpclient; the ISO-TP
transport is tools/umcub_smp.py (SMPIsoTpTransport).
"""
import argparse
import asyncio
import os
import sys

from smpclient import SMPClient
from smpclient.generics import error, success
from smpclient.requests.image_management import ImageStatesRead
from smpclient.requests.os_management import EchoWrite, ResetWrite

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from umcub_smp import SMPIsoTpTransport  # noqa: E402


def check(r):
    if error(r):
        sys.exit(f"device error: {r}")
    return r


async def run(a):
    transport = SMPIsoTpTransport(a.interface, a.channel, a.bitrate, tx_id=a.tx_id + a.addr,
                                  rx_id=a.rx_id + a.addr, fd=a.fd, data_bitrate=a.data_bitrate,
                                  extended=a.ext, mtu=a.mtu)
    async with SMPClient(transport, "can", timeout_s=a.timeout) as client:
        if a.cmd == "list":
            for img in check(await client.request(ImageStatesRead())).images:
                flags = [k for k in ("active", "confirmed", "pending", "permanent", "bootable") if getattr(img, k)]
                print(f"image {img.image or 0} slot {img.slot}: {img.version} "
                      f"{(img.hash or b'').hex()[:16]} {' '.join(flags)}")
        elif a.cmd == "upload":
            data = open(a.file, "rb").read()
            async for off in client.upload(data, slot=a.image, subsequent_timeout_s=a.timeout):
                print(f"\r{off}/{len(data)} bytes", end="", flush=True)
            print("\ndone")
        elif a.cmd == "reset":
            r = await client.request(ResetWrite())
            print("reset" if success(r) else r)
        else:
            print(check(await client.request(EchoWrite(d=a.text))).r)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--interface", default="socketcan")
    p.add_argument("--channel", default="can0")
    p.add_argument("--bitrate", type=int, default=500000)
    p.add_argument("--fd", action="store_true")
    p.add_argument("--data-bitrate", type=int, default=2000000)
    p.add_argument("--tx-id", type=lambda s: int(s, 0), default=0x7C0, help="host -> device")
    p.add_argument("--rx-id", type=lambda s: int(s, 0), default=0x7C8, help="device -> host")
    p.add_argument("--addr", type=int, default=0,
                   help="node address of the device: its IDs are tx-id + addr and rx-id + addr")
    p.add_argument("--ext", action="store_true", help="29-bit identifiers")
    p.add_argument("--mtu", type=int, default=1024, help="largest SMP packet (UMCUB_CFG_SMP_MTU)")
    p.add_argument("--timeout", type=float, default=5.0)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    up = sub.add_parser("upload")
    up.add_argument("file")
    up.add_argument("--image", type=int, default=0)
    sub.add_parser("reset")
    e = sub.add_parser("echo")
    e.add_argument("text")
    asyncio.run(run(p.parse_args()))


if __name__ == "__main__":
    main()
