"""umcub: the command line tool of the umcub host library.

  umcub -d /dev/ttyUSB0 upload app.signed.bin --reset          SMP upload (primary slot), then reset
  umcub -d /dev/ttyUSB0 upload app.signed.bin --lite           bootloaders without SMP (lite upload)
  umcub -d /dev/ttyUSB1 discover                               umcub link devices on an RS485 bus
  umcub -d /dev/ttyUSB1 --node 5 upload app.signed.bin         one of them (SECURE: --admin-key, --device-key)
  umcub -d can:socketcan:can0 --node 5 list                    image slots over CAN (IDs base + node)
  umcub -d 192.168.1.50 verify 0 1                             MCUboot validation of image 0, slot 1
  umcub -d /dev/ttyUSB0 cmd i                                  a text command

The device (-d, or $UMCUB_DEVICE): a serial port (/dev/ttyUSB0, COM3,
serial:<port>,baud=921600), an IP address (udp:<ip>,port=1337) or a CAN bus
(can:<python-can interface>:<channel>[,bitrate=500000][,fd][,data-bitrate=N][,tx=0x7C0][,rx=0x7C8][,ext]).
The bootloader must be in recovery mode.
"""
import argparse
import os
import select
import socket
import sys
import time

from . import lite
from .connection import KIND_SMP
from .device import connect, discover
from .errors import UmcubError
from .link import LINK_NAMES, describe_identity, sniff
from .nlip import NlipDecoder, nlip_encode
from .smp import Smp, file_hash


def _progress(done, total):
    print(f"\r{done}/{total}", end="", file=sys.stderr, flush=True)


def _read(path):
    try:
        return open(path, "rb").read()
    except OSError as e:
        raise UmcubError(f"{path}: {e.strerror}") from e


def cmd_discover(a):
    devs = discover(a.device, a.rounds, a.slots, a.slot_ms, a.timeout)
    for d in devs:
        print(describe_identity(d))
    print(f"{len(devs)} device(s)")


def cmd_info(dev, a):
    i = dev.identity
    if not i:
        sys.stdout.write(dev.text("i", a.wait))
        return 0
    for k in ("addr", "uid", "board_type", "board_rev", "version"):
        print(f"{k:10s} {hex(i[k]) if k == 'board_type' else i[k]}")
    print(f"{'link':10s} {LINK_NAMES.get(i['link'], '?')}{' (encrypted)' if i['enc'] else ''}"
          f"{' (authenticated)' if dev.link.session else ''}")
    return 0


def cmd_cmd(dev, a):
    out = dev.text(a.text, a.wait)
    if not out:
        raise UmcubError("no answer")
    sys.stdout.write(out)
    return 0


def cmd_upload(dev, a):
    data = _read(a.file)
    t0 = time.monotonic()
    if a.lite:
        lite.upload(dev, data, a.image, 1 if a.slot == "secondary" else 0, a.test, a.permanent, a.chunk, _progress)
    else:
        if a.slot != "primary" or a.test or a.permanent:
            raise UmcubError("--slot / --test / --permanent are lite options; over SMP see docs/recovery.md "
                             "(UMCUB_CFG_SMP_DIRECT_UPLOAD: the image field names the slot)")
        with Smp(dev) as smp:
            smp.upload(data, a.image, _progress)
    print(file=sys.stderr)
    print(f"uploaded {len(data)} bytes in {time.monotonic() - t0:.1f} s")
    if a.reset:
        cmd_reset(dev, a)
    return 0


def cmd_reset(dev, a):
    if a.lite:
        lite.reset(dev)
    else:
        with Smp(dev) as smp:
            smp.reset()
    print("reset")
    return 0


def cmd_list(dev, a):
    with Smp(dev) as smp:
        for i in smp.images():
            flags = [k for k in ("active", "confirmed", "pending", "permanent", "bootable") if i[k]]
            print(f"image {i['image']} slot {i['slot']}: {i['version']} {i['hash'].hex()[:16]} {' '.join(flags)}")
    return 0


def cmd_echo(dev, a):
    with Smp(dev) as smp:
        print(smp.echo(a.text))
    return 0


def cmd_verify(dev, a):
    with Smp(dev) as smp:
        valid = smp.verify(a.image, a.slot)
    print("valid" if valid else "INVALID (hash, signature or board type)")
    return 0 if valid else 1


def cmd_hash(dev, a):
    length, local = a.len, None
    if a.file:
        data = _read(a.file)[a.off:]
        length = length or len(data)
        local = file_hash(data, 0, length)
    with Smp(dev) as smp:
        sha, n = smp.hash(a.image, a.slot, a.off, length)
    print(f"device sha256 {sha.hex()} ({n} bytes)")
    if local is None:
        return 0
    print(f"file   sha256 {local.hex()}")
    print("MATCH" if local == sha else "MISMATCH")
    return 0 if local == sha else 1


def cmd_read(dev, a):
    with Smp(dev) as smp:
        out = smp.read(a.image, a.slot, a.off, a.len, _progress)
    print(file=sys.stderr)
    if a.out:
        open(a.out, "wb").write(out)
    else:
        sys.stdout.buffer.write(out)
    return 0


def cmd_serve(dev, a):
    """A proxy for standard SMP clients (smpmgr, mcumgr): a pseudo terminal or a
    local UDP port; requests go to the device (link: addressed, SECURE: authenticated)."""
    if dev.identity:
        i = dev.identity
        print(f"connected to node {i['addr']} uid {i['uid']} "
              f"({LINK_NAMES.get(i['link'], '?')}{', encrypted' if i['enc'] else ''})", flush=True)

    def forward(pkt):
        dev.send(KIND_SMP, pkt)
        return dev.recv(KIND_SMP, a.timeout)

    if a.udp_listen:
        host, port = a.udp_listen.rsplit(":", 1)
        srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        srv.bind((host, int(port)))
        print(f"SMP over UDP: mcumgr --conntype udp --connstring={host}:{port}", flush=True)
        while True:
            pkt, peer = srv.recvfrom(4096)
            rsp = forward(pkt)
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
            rsp = forward(pkt)
            if rsp:
                os.write(master, nlip_encode(rsp))


def cmd_sniff(a):
    port, _, baud = a.device.removeprefix("serial:").partition(",baud=")
    print(f"# listening on {port} (no transmit), Ctrl-C to stop", flush=True)
    sniff(port, int(baud or 115200), a.raw, lambda text, ts: print(f"{ts:8.3f}  {text}", flush=True))


def parser():
    ap = argparse.ArgumentParser(prog="umcub", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default=os.environ.get("UMCUB_DEVICE"),
                    help="serial port, IP address or can:<interface>:<channel> (default $UMCUB_DEVICE)")
    sel = ap.add_mutually_exclusive_group()
    sel.add_argument("--node", type=int, help="node address: umcub link device (serial, UDP) or CAN IDs")
    sel.add_argument("--uid", help="UID of an unassigned umcub link device (hex)")
    ap.add_argument("--admin-key", help="link SECURE: private admin key (PEM)")
    ap.add_argument("--device-key", help="link SECURE: device key (PEM, the public half is enough)")
    ap.add_argument("--timeout", type=float, default=3.0, help="seconds per request")
    sub = ap.add_subparsers(dest="cmd", required=True, metavar="command")

    p = sub.add_parser("discover", help="umcub link devices on the bus")
    p.add_argument("--rounds", type=int, default=8, help="at most; stops after a quiet round")
    p.add_argument("--slots", type=int, default=16)
    p.add_argument("--slot-ms", type=int, default=20)
    p = sub.add_parser("info", help="identity of a link node (otherwise: text command i)")
    p.add_argument("--wait", type=float, default=1.0)
    p = sub.add_parser("cmd", help="a text command, prints the answer")
    p.add_argument("text")
    p.add_argument("--wait", type=float, default=1.0, help="seconds to collect the answer")
    p = sub.add_parser("upload", help="write a signed image")
    p.add_argument("file")
    p.add_argument("--image", type=int, default=0, help="image number (SMP: the image field)")
    p.add_argument("--lite", action="store_true", help="lite upload (bootloaders without SMP)")
    p.add_argument("--slot", choices=("primary", "secondary"), default="primary", help="lite: target slot")
    p.add_argument("--test", action="store_true", help="lite, secondary: mark for a test boot")
    p.add_argument("--permanent", action="store_true", help="lite, secondary: mark as permanent")
    p.add_argument("--chunk", type=int, help=f"lite: data bytes per frame (default {lite.CHUNK_SERIAL} on serial, "
                   f"{lite.CHUNK_PACKET} otherwise; up to 3/4 of UMCUB_CFG_SMP_MTU - 8 on serial, MTU - 8 otherwise)")
    p.add_argument("--reset", action="store_true", help="reset the device afterwards")
    p = sub.add_parser("reset", help="reset the device")
    p.add_argument("--lite", action="store_true", help="through the lite protocol")
    sub.add_parser("list", help="image slots (SMP image state-read)")
    p = sub.add_parser("echo", help="SMP echo")
    p.add_argument("text")
    for name, what in (("verify", "MCUboot validation of a slot"), ("hash", "SHA-256 of a slot"),
                       ("read", "raw readback of a slot (UMCUB_CFG_READBACK)")):
        p = sub.add_parser(name, help=what)
        p.add_argument("image", type=int)
        p.add_argument("slot", type=int, help="0 primary, 1 secondary")
        if name != "verify":
            p.add_argument("--off", type=lambda s: int(s, 0), default=0)
            p.add_argument("--len", type=lambda s: int(s, 0), default=0, help="0 = whole stored image")
    sub.choices["hash"].add_argument("--file", help="compare with this file (e.g. the .signed.bin that was sent)")
    sub.choices["read"].add_argument("--out", help="output file (default stdout)")
    p = sub.add_parser("serve", help="proxy for smpmgr / mcumgr (pty or UDP)")
    p.add_argument("--pty-link", help="also create this symlink to the pty")
    p.add_argument("--udp-listen", help="local HOST:PORT for SMP over UDP clients")
    p = sub.add_parser("sniff", help="listen only: decode every link frame on a serial bus")
    p.add_argument("--raw", action="store_true", help="also print lines that are not link frames")
    return ap


COMMANDS = {"info": cmd_info, "cmd": cmd_cmd, "upload": cmd_upload, "reset": cmd_reset, "list": cmd_list,
            "echo": cmd_echo, "verify": cmd_verify, "hash": cmd_hash, "read": cmd_read, "serve": cmd_serve}


def main(argv=None):
    a = parser().parse_args(argv)
    if not a.device:
        sys.exit("umcub: no device (-d or $UMCUB_DEVICE)")
    try:
        if a.cmd == "discover":
            return cmd_discover(a)
        if a.cmd == "sniff":
            return cmd_sniff(a)
        with connect(a.device, a.node, a.uid, a.admin_key, a.device_key, a.timeout) as dev:
            return COMMANDS[a.cmd](dev, a)
    except UmcubError as e:
        print(file=sys.stderr)
        sys.exit(f"umcub: {e}")
    except KeyboardInterrupt:
        return 130
