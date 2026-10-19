"""Connections of the umcub host tools: how they reach a bootloader.

- `add_bus_arguments()` / `add_can_arguments()`: the same --port / --udp / --can
  options (and CAN settings) in every tool; `can_ids()` gives the CAN IDs of a
  node (base + node address, like the bootloader);
- `open_bus()`: one object per connection with send(payload), recv(timeout)
  and text(cmd, wait) (a text command and what came back):
    `SerialLines`  stream transports (UART, RS485, USB CDC): payloads as
                   "<start> base64 \\n" lines, anything else on the line is
                   ignored and counted as noise;
    `Udp`          one payload per datagram;
    `IsoTp`        one payload per ISO-TP message (python-can-isotp);
- `isotp_stack()`: a started ISO-TP stack with the umcub frame settings.
"""
import base64
import errno
import select
import socket
import time


def _int(s):
    return int(s, 0)


def add_can_arguments(ap, smp=False):
    """CAN bus and the node's IDs; smp: also --mtu for SMP clients."""
    ap.add_argument("--interface", default="socketcan", help="python-can interface")
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--fd", action="store_true", help="CAN FD (64-byte frames, bit rate switch)")
    ap.add_argument("--data-bitrate", type=int, default=2000000)
    ap.add_argument("--tx-id", type=_int, default=0x7C0, help="host -> device (base)")
    ap.add_argument("--rx-id", type=_int, default=0x7C8, help="device -> host (base)")
    ap.add_argument("--addr", type=int, default=0,
                    help="node address of the device: its IDs are tx-id + addr and rx-id + addr")
    ap.add_argument("--ext", action="store_true", help="29-bit identifiers")
    if smp:
        ap.add_argument("--mtu", type=int, default=1024, help="largest SMP packet (UMCUB_CFG_SMP_MTU)")


def add_bus_arguments(ap, can=True, smp=False):
    """--port / --udp (/ --can) and their settings; smp: see add_can_arguments()."""
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port (UART, RS485 adapter, USB CDC)")
    g.add_argument("--udp", help="device IP address")
    if can:
        g.add_argument("--can", action="store_true", help="CAN ISO-TP (python-can)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--udp-port", type=int, default=1337)
    if can:
        add_can_arguments(ap, smp)


def can_ids(a):
    """(host -> device, device -> host) IDs of the node selected by --addr."""
    return a.tx_id + a.addr, a.rx_id + a.addr


def isotp_stack(interface, channel, bitrate, tx_id, rx_id, fd=False, data_bitrate=2000000, extended=False):
    """A started python-can-isotp stack with the umcub frame settings; returns
    (bus, notifier, stack)."""
    import can
    import isotp
    bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate, fd=fd,
                  data_bitrate=data_bitrate if fd else None)
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


class SerialLines:
    """Stream transport: payloads as '<start> base64 \\n' lines."""

    def __init__(self, port, baud, start):
        import serial
        self.s = serial.Serial(port, baud, timeout=0)
        self.start = start
        self.buf = b""
        self.noise = 0          # lines that were not ours: on a shared bus, answers that collided

    def send(self, payload):
        self.s.write(self.start + base64.b64encode(payload) + b"\n")
        self.s.flush()

    def recv(self, timeout):
        end = time.monotonic() + timeout
        while True:
            nl = self.buf.find(b"\n")
            while nl >= 0:
                line, self.buf = self.buf[:nl], self.buf[nl + 1:]
                i = line.find(self.start)
                if i >= 0:
                    try:
                        return base64.b64decode(line[i + len(self.start):].strip(), validate=True)
                    except ValueError:
                        pass
                if line.strip():
                    self.noise += 1
                nl = self.buf.find(b"\n")
            left = end - time.monotonic()
            if left <= 0:
                if self.buf.strip():
                    self.noise += 1         # a line cut short (its newline lost in a collision)
                    self.buf = b""
                return None
            self.s.timeout = min(left, 0.05)
            chunk = self.s.read(max(1, self.s.in_waiting))     # return as soon as anything arrived
            if chunk:
                self.buf += chunk

    def text(self, cmd, wait):
        self.s.reset_input_buffer()
        self.buf = b""
        self.s.write(cmd.encode() + b"\r")
        time.sleep(wait)
        return self.s.read(self.s.in_waiting).decode(errors="replace")


class _Packets:
    """A text command is one packet, the answer the next one."""

    def text(self, cmd, wait):
        self.send(cmd.encode())
        r = self.recv(wait)
        return r.decode(errors="replace") if r else ""


class Udp(_Packets):
    def __init__(self, host, port):
        self.addr = (host, port)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.noise = 0          # datagrams do not collide

    def send(self, payload):
        self.s.sendto(payload, self.addr)

    def recv(self, timeout):
        r, _, _ = select.select([self.s], [], [], max(timeout, 0))
        return self.s.recv(4096) if r else None


class IsoTp(_Packets):
    def __init__(self, a):
        tx, rx = can_ids(a)
        self.bus, self.notifier, self.stack = isotp_stack(a.interface, a.channel, a.bitrate, tx, rx, fd=a.fd,
                                                          data_bitrate=a.data_bitrate, extended=a.ext)
        self.noise = 0

    def send(self, payload):
        self.stack.send(payload, send_timeout=5.0)

    def recv(self, timeout):
        r = self.stack.recv(block=True, timeout=timeout)
        return bytes(r) if r is not None else None


def open_bus(a, start):
    """The connection chosen by add_bus_arguments(); `start`: line prefix on serial."""
    if a.port:
        return SerialLines(a.port, a.baud, start)
    if a.udp:
        return Udp(a.udp, a.udp_port)
    return IsoTp(a)
