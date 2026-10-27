"""Device commands of the umcub tool (see umcub.cli)."""
import os
import select
import socket
import sys
import time

from . import lite
from .connection import KIND_SMP
from .device import connect, discover
from .errors import UmcubError
from .image import Image
from .link import LINK_NAMES, describe_identity, sniff
from .nlip import NlipDecoder, nlip_encode
from .smp import Smp, direct_slot, file_hash


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
    img = Image.load(a.file)
    if dev.identity:
        img.check_board(dev.identity["board_type"])     # the bootloader would refuse it after the transfer
    if (a.test or a.permanent) and a.slot != "secondary":
        raise UmcubError("--test / --permanent mark an image in the secondary slot (--slot secondary)")
    t0 = time.monotonic()
    if a.lite:
        lite.upload(dev, img.data, a.image, 1 if a.slot == "secondary" else 0, a.test, a.permanent, a.chunk,
                    _progress)
    else:
        # Without --slot SMP's image field is the image number (upload into its primary slot); with --slot
        # it names the slot (UMCUB_CFG_SMP_DIRECT_UPLOAD).
        field = direct_slot(a.image, 1 if a.slot == "secondary" else 0) if a.slot else a.image
        with Smp(dev) as smp:
            smp.upload(img.data, field, _progress)
            if a.test or a.permanent:
                smp.mark(img.hash, a.permanent)
    print(file=sys.stderr)
    print(f"uploaded {img.size} bytes ({img.version}) in {time.monotonic() - t0:.1f} s"
          + (", marked " + ("permanent" if a.permanent else "for a test boot") if a.test or a.permanent else ""))
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


def cmd_mark(dev, a):
    h = Image.load(a.image_or_hash).hash if os.path.isfile(a.image_or_hash) else bytes.fromhex(a.image_or_hash)
    with Smp(dev) as smp:
        smp.mark(h, a.permanent)
    print("marked " + ("permanent" if a.permanent else "for a test boot"))
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


COMMANDS = {"info": cmd_info, "cmd": cmd_cmd, "upload": cmd_upload, "reset": cmd_reset, "list": cmd_list,
            "mark": cmd_mark,
            "echo": cmd_echo, "verify": cmd_verify, "hash": cmd_hash, "read": cmd_read, "serve": cmd_serve}


def run(a):
    if a.cmd == "discover":
        return cmd_discover(a)
    if a.cmd == "sniff":
        return cmd_sniff(a)
    with connect(a.device, a.node, a.uid, a.admin_key, a.device_key, a.timeout) as dev:
        return COMMANDS[a.cmd](dev, a)
