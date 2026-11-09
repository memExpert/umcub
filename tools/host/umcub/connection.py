"""Connections to a umcub bootloader: serial (UART, RS485, USB CDC), UDP, CAN ISO-TP.

A connection is opened from one string (what a user types or an application stores):

    /dev/ttyUSB0  COM3  serial:/dev/ttyUSB0,baud=921600
    192.168.1.50  udp:192.168.1.50,port=1337
    can:socketcan:can0  can:pcan:PCAN_USBBUS1,bitrate=250000  can:slcan:/dev/ttyACM0,fd,data-bitrate=2000000
        CAN options: bitrate, fd, data-bitrate, tx (host -> device base ID, 0x7C0), rx (device -> host, 0x7C8),
        ext (29-bit IDs), mtu (largest SMP packet, UMCUB_CFG_SMP_MTU); the interface is a python-can one.
    <scheme>:<target>[,option[=value]]...: a transport a plugin registered (umcub.plugins).

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
import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass

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

    name = "?"          # shown in messages and Device.name
    stream = False      # payloads framed as lines (serial)
    selects_node = False    # the node was chosen when opening (CAN IDs): no umcub link addressing on top
    mtu = MTU_MIN       # largest SMP packet, when the device does not report it
    noise = 0           # lines that were neither ours nor valid (collisions on a shared bus)

    def text(self, cmd, wait):
        """A text command and its answer; by default one packet each way (Serial frames lines itself)."""
        self.send(None, cmd.encode())
        r = self.recv(None, wait)
        return r.decode(errors="replace") if r else ""

    def close(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def open_serial(port, baud, timeout):
    """A pyserial port; failures as UmcubError (serial.open)."""
    import serial
    try:
        return serial.Serial(port, baud, timeout=timeout)
    except serial.SerialException as e:
        raise UmcubError(code="serial.open", port=port, reason=str(e)) from e


class Serial(Connection):
    """Stream transport: SMP as NLIP lines, frames as '05 <kind> base64 \\n' lines, text as is."""

    stream = True

    def __init__(self, port, baud=115200):
        self.s = open_serial(port, baud, 0)
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


class Udp(Connection):
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


CAN_BITRATE, CAN_DATA_BITRATE = 500000, 2000000
_buses = {}                 # (interface, channel) -> _SharedBus
_buses_lock = threading.Lock()


class _SharedBus:
    """One python-can bus and notifier per channel for every connection on it: drivers such as PCAN or Vector open
    a channel once; each connection adds its own ISO-TP stack (its node's IDs)."""

    def __init__(self, key, settings):
        import can
        interface, channel = key
        bitrate, fd, data_bitrate = settings
        try:
            self.bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate, fd=fd,
                               data_bitrate=data_bitrate if fd else None)
        except (can.CanError, OSError, ValueError, ImportError) as e:
            raise UmcubError(code="can.open", interface=interface, channel=channel, reason=str(e)) from e
        # A device that allows back-to-back consecutive frames (STmin 0) fills
        # the interface's TX queue (Linux default qlen 10): wait instead of
        # failing on ENOBUFS.
        send = self.bus.send

        def send_retry(msg, timeout=None):
            for _ in range(1000):
                try:
                    return send(msg, timeout)
                except can.CanOperationError as e:
                    if getattr(e, "error_code", None) != errno.ENOBUFS:
                        raise
                    time.sleep(0.001)
            return send(msg, timeout)
        self.bus.send = send_retry
        self.notifier = can.Notifier(self.bus, [])
        self.key, self.settings, self.users = key, settings, 0

    @classmethod
    def take(cls, interface, channel, bitrate=CAN_BITRATE, fd=False, data_bitrate=CAN_DATA_BITRATE, **_):
        """The channel's bus (opened by the first user); other connection options (IDs, mtu) do not matter."""
        key, settings = (interface, str(channel)), (bitrate, fd, data_bitrate)
        with _buses_lock:
            b = _buses.get(key)
            if b is None:
                b = _buses[key] = cls(key, settings)
            elif b.settings != settings:
                raise UmcubError(code="can.open", interface=interface, channel=channel,
                                 reason="already open with other settings")
            b.users += 1
            return b

    def release(self):
        with _buses_lock:
            self.users -= 1
            if self.users:
                return
            del _buses[self.key]
        self.notifier.stop()
        self.bus.shutdown()


class IsoTp(Connection):
    """CAN ISO-TP; the node's IDs are the bases + its node address (like the bootloader). Connections on one
    channel share its bus."""

    selects_node = True

    def __init__(self, interface, channel, bitrate=CAN_BITRATE, fd=False, data_bitrate=CAN_DATA_BITRATE, tx=CAN_TX,
                 rx=CAN_RX, ext=False, node=0, mtu=1024):
        import isotp
        self.name = f"{interface}:{channel}"
        self.mtu = mtu
        mode = isotp.AddressingMode.Normal_29bits if ext else isotp.AddressingMode.Normal_11bits
        params = {"tx_padding": 0xCC, "can_fd": fd, "tx_data_length": 64 if fd else 8,
                  "blocking_send": True, "max_frame_size": 4095, "bitrate_switch": fd}
        self.shared = _SharedBus.take(interface, channel, bitrate=bitrate, fd=fd, data_bitrate=data_bitrate)
        try:
            self.stack = isotp.NotifierBasedCanStack(self.shared.bus, self.shared.notifier, params=params,
                                                     address=isotp.Address(mode, txid=tx + node, rxid=rx + node))
            self.stack.start()
        except BaseException:
            self.shared.release()
            raise

    def close(self):
        if self.stack:
            self.stack.stop()
            self.shared.release()
            self.stack = None

    def send(self, kind, payload):
        try:
            self.stack.send(payload, send_timeout=10.0)
        except Exception as e:      # isotp.BlockingSendFailure and the like
            raise UmcubError(code="can.send", reason=str(e)) from e

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
            raise UmcubError(code="spec.option", option=k, known=", ".join(sorted(known)))
        conv = known[k]
        opts[k.replace("-", "_")] = True if conv is bool else conv(v)
    return opts


@dataclass
class TapEvent:
    """One payload a connection sent or received (see add_tap)."""
    time: float         # time.monotonic()
    direction: str      # "tx" or "rx"
    kind: object        # KIND_SMP, KIND_LINK, KIND_LITE, KIND_USER, "text", or None (a packet transport's DATA)
    payload: bytes      # as on the connection: link frames still encrypted (umcub.link.describe_frame decodes them)
    connection: str     # Connection.name


def add_tap(conn, tap):
    """Call tap(TapEvent) for every payload `conn` sends or receives and every text
    command and its answer, from the thread doing the I/O. Wraps the methods of this
    one object, so it works for any Connection, plugins included. An exception in
    `tap` does not stop the transfer (a log must not break an upload): the first
    one is kept in conn.tap_error."""
    send, recv, text = conn.send, conn.recv, conn.text
    conn.tap_error = None
    in_text = []            # inside text(): its packets are the text events, not reported again

    def emit(direction, kind, payload):
        if in_text:
            return
        try:
            tap(TapEvent(time.monotonic(), direction, kind, bytes(payload), conn.name))
        except Exception as e:
            if conn.tap_error is None:
                conn.tap_error = e

    def tapped_send(kind, payload):
        emit("tx", kind, payload)
        return send(kind, payload)

    def tapped_recv(kind, timeout):
        r = recv(kind, timeout)
        if r is not None:
            emit("rx", kind, r)
        return r

    def tapped_text(cmd, wait):
        emit("tx", "text", cmd.encode())
        in_text.append(1)
        try:
            r = text(cmd, wait)
        finally:
            in_text.pop()
        if r:
            emit("rx", "text", r.encode())
        return r

    conn.send, conn.recv, conn.text = tapped_send, tapped_recv, tapped_text
    return conn


class Transport:
    """A registered kind of connection: `<scheme>:<target>[,option[=value]]...` opens
    open(target, node, **options). `options` maps each option name to its converter
    (int, bool for a flag, str, ...); `description` is one line for lists and help;
    `adapters` (optional) returns the umcub.Adapter list of what is plugged in now; `parallel`: several
    connections to one target may work at once (see exclusive_key)."""

    def __init__(self, scheme, open, options=None, description="", adapters=None, parallel=False):
        self.scheme, self.open, self.options, self.description = scheme, open, dict(options or {}), description
        self.adapters, self.parallel = adapters, parallel

    def __repr__(self):
        return f"Transport({self.scheme!r})"


_transports = {}


def register_transport(scheme, open, options=None, description="", adapters=None, parallel=False):
    """Add a transport (built-in or a plugin's register()). A scheme is
    registered once: a plugin cannot replace another transport."""
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", scheme or ""):
        raise UmcubError(code="transport.scheme", scheme=scheme)
    if scheme in _transports:
        raise UmcubError(code="transport.duplicate", scheme=scheme)
    _transports[scheme] = Transport(scheme, open, options, description, adapters, parallel)
    return _transports[scheme]


def transports():
    """The registered transports by scheme (installed plugins loaded)."""
    from .plugins import load_installed
    load_installed()
    return dict(_transports)


def _open_serial(target, node, baud=115200):
    return Serial(target, baud)


def _open_udp(target, node, port=UDP_PORT, mtu=None):
    try:
        ipaddress.ip_address(target)
    except ValueError:
        target = socket.gethostbyname(target)
    return Udp(target, port, mtu)


def _can_target(target):
    """(python-can interface, channel) of `<interface>:<channel>`."""
    interface, _, channel = target.partition(":")
    if not channel:
        raise UmcubError(code="can.spec")
    return interface, channel


def _open_can(target, node, **o):
    return IsoTp(*_can_target(target), node=node, **o)


def _adapters(name):
    def find(*args):
        from . import adapter
        return getattr(adapter, name)(*args)
    return find


register_transport("serial", _open_serial, {"baud": int}, "serial port: UART, RS485 adapter, USB CDC",
                   _adapters("serial_adapters"))
register_transport("udp", _open_udp, {"port": int, "mtu": int}, "UDP (Ethernet, any IP network)",
                   _adapters("network_adapters"), parallel=True)
register_transport("can", _open_can, {"bitrate": int, "fd": bool, "data-bitrate": int, "tx": _int, "rx": _int,
                                      "ext": bool, "mtu": int}, "CAN ISO-TP through a python-can interface",
                   _adapters("can_adapters"), parallel=True)


def _parse(spec):
    """(Transport, target, option strings) of a device string."""
    spec = spec.strip()
    known = transports()
    scheme, sep, rest = spec.partition(":")
    if not sep or scheme not in known:
        if re.match(r"^\d+\.\d+\.\d+\.\d+(:\d+)?$", spec):
            scheme, rest = "udp", spec.replace(":", ",port=")
        else:
            scheme, rest = "serial", spec       # /dev/ttyUSB0, COM3
    target, *parts = rest.split(",")
    return known[scheme], target, parts


def exclusive_key(spec):
    """None when connections to `spec` may work at the same time (CAN: shared bus, UDP: one socket each); else the
    key of the medium they must take turns on ("serial:/dev/ttyUSB1": a half-duplex bus, one port)."""
    t, target, _ = _parse(spec)
    return None if t.parallel else f"{t.scheme}:{target}"


@contextmanager
def can_bus(spec):
    """Keep the channel of a CAN device string open while connections to its nodes come and go (reopening a
    channel can lose the first frames, e.g. python-can's serial interface); yields its shared bus."""
    t, target, parts = _parse(spec)
    if t.scheme != "can":
        raise UmcubError(code="can.spec")
    shared = _SharedBus.take(*_can_target(target), **_options(parts, t.options))
    try:
        yield shared
    finally:
        shared.release()


def open_connection(spec, node=0, tap=None):
    """The connection described by `spec` (see the module docstring); `node`
    selects the CAN IDs of a node (base + node address). Without a known
    scheme the string is an IP address (UDP) or a serial port. `tap`: see add_tap."""
    t, target, parts = _parse(spec)
    conn = t.open(target, node, **_options(parts, t.options))
    if not isinstance(conn, Connection):
        raise UmcubError(code="transport.type", scheme=t.scheme, type=type(conn).__name__)
    return add_tap(conn, tap) if tap else conn
