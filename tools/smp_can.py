#!/usr/bin/env python3
"""SMP (mcumgr) client for the umcub CAN transport (ISO-TP over python-can).

  smp_can.py [--interface socketcan --channel can0] [--fd] list
  smp_can.py upload app.signed.bin [--image 0]
  smp_can.py reset
  smp_can.py echo hello

Default ids match the umcub defaults: host->device 0x7C0, device->host 0x7C8.
"""
import argparse
import hashlib
import struct
import sys

import can
import cbor2
import isotp

OP_READ, OP_WRITE = 0, 2
GRP_DEFAULT, GRP_IMAGE = 0, 1


class SmpCan:
    def __init__(self, args):
        self.bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate,
                           fd=args.fd, data_bitrate=args.data_bitrate if args.fd else None)
        addr = isotp.Address(isotp.AddressingMode.Normal_29bits if args.ext else isotp.AddressingMode.Normal_11bits,
                             txid=args.tx_id + args.addr, rxid=args.rx_id + args.addr)
        params = {"tx_padding": 0xCC, "can_fd": args.fd, "tx_data_length": 64 if args.fd else 8,
                  "blocking_send": True, "max_frame_size": 4095, "bitrate_switch": args.fd}
        self.stack = isotp.NotifierBasedCanStack(self.bus, address=addr, params=params)
        self.stack.start()
        self.seq = 0
        self.timeout = args.timeout

    def close(self):
        self.stack.stop()
        self.bus.shutdown()

    def request(self, op, group, cmd, payload):
        body = cbor2.dumps(payload)
        self.seq = (self.seq + 1) & 0xFF
        hdr = struct.pack(">BBHHBB", op, 0, len(body), group, self.seq, cmd)
        self.stack.send(hdr + body, send_timeout=self.timeout)
        rsp = self.stack.recv(block=True, timeout=self.timeout)
        if rsp is None:
            raise TimeoutError("no SMP response")
        _, _, length, rgroup, rseq, rcmd = struct.unpack(">BBHHBB", rsp[:8])
        if (rgroup, rcmd) != (group, cmd):
            raise IOError(f"unexpected response group {rgroup} id {rcmd}")
        return cbor2.loads(rsp[8:8 + length]) if length else {}


def cmd_list(smp, args):
    rsp = smp.request(OP_READ, GRP_IMAGE, 0, {})
    for img in rsp.get("images", []):
        flags = [k for k in ("active", "confirmed", "pending", "permanent", "bootable") if img.get(k)]
        print(f"image {img.get('image', 0)} slot {img.get('slot')}: {img.get('version')} "
              f"{img.get('hash', b'').hex()[:16]} {' '.join(flags)}")
    if not rsp.get("images"):
        print(rsp)


def cmd_upload(smp, args):
    data = open(args.file, "rb").read()
    sha = hashlib.sha256(data).digest()
    off = 0
    while off < len(data):
        chunk = data[off:off + args.chunk]
        req = {"image": args.image, "off": off, "data": chunk}
        if off == 0:
            req.update({"len": len(data), "sha": sha})
        rsp = smp.request(OP_WRITE, GRP_IMAGE, 1, req)
        if rsp.get("rc", 0) != 0:
            sys.exit(f"upload failed at {off}: {rsp}")
        off = rsp.get("off", off + len(chunk))
        print(f"\r{off}/{len(data)} bytes", end="", flush=True)
    print("\ndone")


def cmd_reset(smp, args):
    print(smp.request(OP_WRITE, GRP_DEFAULT, 5, {}))


def cmd_echo(smp, args):
    print(smp.request(OP_WRITE, GRP_DEFAULT, 0, {"d": args.text}))


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
    p.add_argument("--timeout", type=float, default=5.0)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    up = sub.add_parser("upload")
    up.add_argument("file")
    up.add_argument("--image", type=int, default=0)
    up.add_argument("--chunk", type=int, default=512)
    sub.add_parser("reset")
    e = sub.add_parser("echo")
    e.add_argument("text")
    args = p.parse_args()

    smp = SmpCan(args)
    try:
        {"list": cmd_list, "upload": cmd_upload, "reset": cmd_reset, "echo": cmd_echo}[args.cmd](smp, args)
    finally:
        smp.close()


if __name__ == "__main__":
    main()
