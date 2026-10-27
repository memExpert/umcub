"""umcub: the command line tool of the umcub host library.

  umcub -d /dev/ttyUSB0 upload app.signed.bin --reset          SMP upload (primary slot), then reset
  umcub -d /dev/ttyUSB0 upload app.signed.bin --lite           bootloaders without SMP (lite upload)
  umcub -d /dev/ttyUSB1 discover                               umcub link devices on an RS485 bus
  umcub -d /dev/ttyUSB1 --node 5 upload app.signed.bin         one of them (SECURE: --admin-key, --device-key)
  umcub -d can:socketcan:can0 --node 5 list                    image slots over CAN (IDs base + node)
  umcub -d 192.168.1.50 verify 0 1                             MCUboot validation of image 0, slot 1
  umcub -d /dev/ttyUSB0 cmd i                                  a text command
  umcub image info --board nucleo_h755zi_q --core cm7          where to link the application
  umcub image sign --board nucleo_h755zi_q --core cm7 --version 1.2.3 app.elf    sign it (IDE post-build step)
  umcub keys c --device device.pem --admin admin.pem -o keys.c umcub link SECURE keys for the bootloader

The device (-d, or $UMCUB_DEVICE): a serial port (/dev/ttyUSB0, COM3,
serial:<port>,baud=921600), an IP address (udp:<ip>,port=1337) or a CAN bus
(can:<python-can interface>:<channel>[,bitrate=500000][,fd][,data-bitrate=N][,tx=0x7C0][,rx=0x7C8][,ext]).
The bootloader must be in recovery mode. `image` and `keys` need no device.
"""
import argparse
import os
import sys

from .errors import UmcubError


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
    p.add_argument("--image", type=int, default=0, help="image number")
    p.add_argument("--lite", action="store_true", help="lite upload (bootloaders without SMP)")
    p.add_argument("--slot", choices=("primary", "secondary"),
                   help="target slot (default primary; SMP: needs UMCUB_CFG_SMP_DIRECT_UPLOAD)")
    p.add_argument("--test", action="store_true", help="secondary: mark for a test boot")
    p.add_argument("--permanent", action="store_true", help="secondary: mark as permanent")
    p.add_argument("--chunk", type=int, help="lite: data bytes per frame (default 373 on serial, "
                   f"504 otherwise; up to 3/4 of UMCUB_CFG_SMP_MTU - 8 on serial, MTU - 8 otherwise)")
    p.add_argument("--reset", action="store_true", help="reset the device afterwards")
    p = sub.add_parser("reset", help="reset the device")
    p.add_argument("--lite", action="store_true", help="through the lite protocol")
    sub.add_parser("list", help="image slots (SMP image state-read)")
    p = sub.add_parser("mark", help="SMP: mark an uploaded secondary image for a test boot (UMCUB_CFG_SMP_DIRECT_UPLOAD)")
    p.add_argument("image_or_hash", help="the signed image file, or its hash (hex, as `list` shows it)")
    p.add_argument("--permanent", action="store_true", help="for good instead of a test boot")
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

    from .cli_build import add_image_arguments, add_keys_arguments
    p = sub.add_parser("image", help="application images: where to link them, signing (no device)")
    add_image_arguments(p.add_subparsers(dest="sub", required=True, metavar="action"))
    p = sub.add_parser("keys", help="umcub link SECURE keys as C arrays (no device)")
    add_keys_arguments(p.add_subparsers(dest="sub", required=True, metavar="action"))
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    try:
        if a.cmd in ("image", "keys"):
            from . import cli_build
            return cli_build.run(a)
        if not a.device:
            sys.exit("umcub: no device (-d or $UMCUB_DEVICE)")
        from . import cli_device
        return cli_device.run(a)
    except UmcubError as e:
        print(file=sys.stderr)
        sys.exit(f"umcub: {e}")
    except KeyboardInterrupt:
        return 130
