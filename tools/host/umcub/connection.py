"""Connections to a umcub bootloader: serial (UART, RS485, USB CDC), UDP, CAN ISO-TP.

A connection is opened from one string (what a user types or an application stores):

    /dev/ttyUSB0  COM3  serial:/dev/ttyUSB0,baud=921600
    192.168.1.50  udp:192.168.1.50,port=1337
    can:socketcan:can0  can:pcan:PCAN_USBBUS1,bitrate=250000  can:slcan:/dev/ttyACM0,fd,data-bitrate=2000000
        CAN options: bitrate, fd, data-bitrate, tx (host -> device base ID, 0x7C0), rx (device -> host, 0x7C8),
        ext (29-bit IDs), mtu (largest SMP packet, UMCUB_CFG_SMP_MTU); the interface is a python-can one.

Every connection carries payloads of a kind: an SMP packet (KIND_SMP), a frame of
a line kind (KIND_LINK, KIND_LITE, KIND_USER) or text. On a stream each kind has
its own framing - SMP as NLIP lines, frames as "05 <kind> base64 \\n" lines, text as
it is - and lines of other kinds are kept apart; packet transports carry one
payload per datagram / ISO-TP message and do not tell the kinds apart.
"""
import base64
import collections
import errno
import ipaddress
import re
import select
import socket
import time

from .errors import UmcubError
from .nlip import NlipDecoder, nlip_encode

KIND_SMP = "smp"
KIND_LINK = 0x0B        # umcub link frame (src/proto/include/umcub_link.h)
KIND_LITE = 0x0C        # lite upload frame (src/proto/include/umcub_lite.h)
KIND_USER = 0x0E        # board protocol frame (UMCUB_CFG_PROTO_USER)
LINE_START = 0x05

UDP_PORT = 1337
CAN_TX, CAN_RX = 0x7C0, 0x7C8
# Smallest UMCUB_CFG_SMP_MTU a build allows: what fits every device that does not
# report its own (SMP parameters).
MTU_MIN = 512


class Connection:
    """send(kind, payload) / recv(kind, timeout) / text(cmd, wait) / close()."""

    stream = False      # payloads framed as lines (serial)
    mtu = MTU_MIN       # largest SMP packet, when the device does not report it
    noise = 0           # lines that were neither ours nor valid (collisions on a shared bus)

    def close(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class Serial(Connection):
    """Stream transport: SMP as NLIP lines, frames as '05 <kind> base64 \\n' lines, text as is."""

    stream = True

    def __init__(self, port, baud=115200):
        import serial
        try:
            self.s = serial.Serial(port, baud, timeout=0)
        except serial.SerialException as e:
            raise UmcubError(f"{port}: {e}") from e
        self.name = port
        self.buf = b""
        self.nlip = NlipDecoder()
        self.queues = collections.defaultdict(lambda: collections.deque(maxlen=16))
        self.lines = collections.deque(maxlen=256)      # text lines (log, command answers)

    def close(self):
        self.s.close()

    def send(self, kind, payload):
        if kind == KIND_SMP:
            self.s.write(nlip_encode(payload))
        else:
            self.s.write(bytes([LINE_START, kind]) + base64.b64encode(payload) + b"\n")
        self.s.flush()

    def _line(self, line):
        if len(line) >= 2 and line[0] == LINE_START:
            try:
                self.queues[line[1]].append(base64.b64decode(line[2:].strip(), validate=True))
                return
            except ValueError:
                pass
        elif line[:2] in (b"\x06\x09", b"\x04\x14"):
            for pkt in self.nlip.feed(line + b"\n"):
                self.queues[KIND_SMP].append(pkt)
            return
        if line.strip():
            self.noise += 1
            self.lines.append(line)

    def _read(self, timeout):
        self.s.timeout = timeout
        chunk = self.s.read(max(1, self.s.in_waiting))     # returns as soon as anything arrived
        if chunk:
            self.buf += chunk
            *lines, self.buf = self.buf.split(b"\n")
            for line in lines:
                self._line(line)

    def recv(self, kind, timeout):
        end = time.monotonic() + timeout
        while True:
            if self.queues[kind]:
                return self.queues[kind].popleft()
            left = end - time.monotonic()
            if left <= 0:
                if self.buf.strip() and kind != KIND_SMP:
                    self.noise += 1         # a line cut short (its newline lost in a collision)
                    self.buf = b""
                return None
            self._read(min(left, 0.05))

    def text(self, cmd, wait):
        self.s.reset_input_buffer()
        self.buf = b""
        self.lines.clear()
        self.s.write(cmd.encode() + b"\r")
        end = time.monotonic() + wait
        while time.monotonic() < end:
            self._read(min(end - time.monotonic(), 0.05))
        out = b"".join(line + b"\n" for line in self.lines) + self.buf
        self.buf = b""
        return out.replace(b"\r", b"").decode(errors="replace")


class _Packets(Connection):
    """One payload per packet, whatever its kind; a text command is one packet, the answer the next."""

    def text(self, cmd, wait):
        self.send(None, cmd.encode())
        r = self.recv(None, wait)
        return r.decode(errors="replace") if r else ""


class Udp(_Packets):
    def __init__(self, host, port=UDP_PORT, mtu=None):
        self.name = f"{host}:{port}"
        self.addr = (host, port)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        if mtu:
            self.mtu = mtu

    def close(self):
        self.s.close()

    def send(self, kind, payload):
        self.s.sendto(payload, self.addr)

    def recv(self, kind, timeout):
        r, _, _ = select.select([self.s], [], [], max(timeout, 0))
        return self.s.recv(4096) if r else None


def isotp_stack(interface, channel, bitrate, tx_id, rx_id, fd=False, data_bitrate=2000000, extended=False):
    """A started python-can-isotp stack with the umcub frame settings; returns (bus, notifier, stack)."""
    import can
    import isotp
    try:
        bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate, fd=fd,
                      data_bitrate=data_bitrate if fd else None)
    except (can.CanError, OSError, ValueError, ImportError) as e:
        raise UmcubError(f"CAN {interface}:{channel}: {e}") from e
    # A device that allows back-to-back consecutive frames (STmin 0) fills
    # the interface's TX queue (Linux default qlen 10): wait instead of
    # failing on ENOBUFS.
    send = bus.send

    def send_retry(msg, timeout=None):
        for _ in range(1000):
            try:
                return send(msg, timeout)
            except can.CanOperationError as e:
                if getattr(e, "error_code", None) != errno.ENOBUFS:
                    raise
                time.sleep(0.001)
        return send(msg, timeout)
    bus.send = send_retry
    mode = isotp.AddressingMode.Normal_29bits if extended else isotp.AddressingMode.Normal_11bits
    addr = isotp.Address(mode, txid=tx_id, rxid=rx_id)
    params = {"tx_padding": 0xCC, "can_fd": fd, "tx_data_length": 64 if fd else 8,
              "blocking_send": True, "max_frame_size": 4095, "bitrate_switch": fd}
    notifier = can.Notifier(bus, [])
    stack = isotp.NotifierBasedCanStack(bus, notifier, address=addr, params=params)
    stack.start()
    return bus, notifier, stack


class IsoTp(_Packets):
    """CAN ISO-TP; the node's IDs are the bases + its node address (like the bootloader)."""

    def __init__(self, interface, channel, bitrate=500000, fd=False, data_bitrate=2000000, tx=CAN_TX, rx=CAN_RX,
                 ext=False, node=0, mtu=1024):
        self.name = f"{interface}:{channel}"
        self.mtu = mtu
        self.bus, self.notifier, self.stack = isotp_stack(interface, channel, bitrate, tx + node, rx + node, fd=fd,
                                                          data_bitrate=data_bitrate, extended=ext)

    def close(self):
        if self.stack:
            self.stack.stop()
            self.notifier.stop()
            self.bus.shutdown()
            self.stack = None

    def send(self, kind, payload):
        try:
            self.stack.send(payload, send_timeout=10.0)
        except Exception as e:      # isotp.BlockingSendFailure and the like
            raise UmcubError(f"CAN send: {e}") from e

    def recv(self, kind, timeout):
        r = self.stack.recv(block=True, timeout=max(timeout, 0))
        return bytes(r) if r is not None else None


def _int(s):
    return int(s, 0)


def _options(parts, known):
    opts = {}
    for p in parts:
        k, _, v = p.partition("=")
        k = k.strip().replace("_", "-")
        if k not in known:
            raise UmcubError(f"unknown option '{k}' (known: {', '.join(sorted(known))})")
        conv = known[k]
        opts[k.replace("-", "_")] = True if conv is bool else conv(v)
    return opts


def open_connection(spec, node=0):
    """The connection described by `spec` (see the module docstring); `node`
    selects the CAN IDs of a node (base + node address)."""
    spec = spec.strip()
    scheme, sep, rest = spec.partition(":")
    if not sep or scheme not in ("serial", "udp", "can"):
        if re.match(r"^\d+\.\d+\.\d+\.\d+(:\d+)?$", spec):
            scheme, rest = "udp", spec.replace(":", ",port=")
        else:
            scheme, rest = "serial", spec       # /dev/ttyUSB0, COM3
    target, *parts = rest.split(",")
    if scheme == "serial":
        return Serial(target, **_options(parts, {"baud": int}))
    if scheme == "udp":
        try:
            ipaddress.ip_address(target)
        except ValueError:
            target = socket.gethostbyname(target)
        o = _options(parts, {"port": int, "mtu": int})
        return Udp(target, o.get("port", UDP_PORT), o.get("mtu"))
    interface, _, channel = target.partition(":")
    if not channel:
        raise UmcubError("CAN: can:<python-can interface>:<channel>, e.g. can:socketcan:can0")
    o = _options(parts, {"bitrate": int, "fd": bool, "data-bitrate": int, "tx": _int, "rx": _int, "ext": bool,
                         "mtu": int})
    return IsoTp(interface, channel, node=node, **o)
