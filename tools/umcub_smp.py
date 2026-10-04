"""SMP helpers shared by the umcub host tools, on top of `smp` / `smpclient`.

- serial (NLIP) framing for SMP over a stream: `nlip_encode()`, `NlipDecoder`
  (smp.packet, tolerant of log and text lines between frames);
- the umcub inspection group (SMP group 100, boot/smp_inspect.c) as smpclient
  requests: `InspectVerify`, `InspectHash`, `InspectRead`;
- `SMPIsoTpTransport`: an smpclient transport over CAN ISO-TP (python-can-isotp),
  with the umcub CAN IDs (base + node address).
"""
import asyncio
import binascii
import logging
from enum import IntEnum, unique

from smp import error as smperr
from smp import header as smphdr
from smp import message as smpmsg
from smp import packet as smppacket
from smp.exceptions import SMPBadContinueDelimiter, SMPBadCRC, SMPBadStartDelimiter

# boot_serial has no MCUmgr parameters command (rc 8): smpclient then keeps its
# default sizes, which is what the tools want - not worth a warning.
logging.getLogger("smpclient").setLevel(logging.ERROR)

# boot_serial accepts lines of 2 delimiter bytes + 124 base64 characters + '\n'.
LINE_LENGTH = 128
INSPECT_GROUP = 100


def nlip_encode(pkt):
    """One SMP packet as NLIP lines (SMP over a serial line)."""
    return b"".join(smppacket.encode(pkt, line_length=LINE_LENGTH))


class NlipDecoder:
    """Bytes in, complete SMP packets out; other lines (log, text) are skipped."""

    def __init__(self):
        self.line = b""
        self.dec = None

    def feed(self, chunk):
        pkts = []
        for c in chunk:
            self.line += bytes([c])
            if c != 0x0A:
                continue
            line, self.line = self.line.replace(b"\r", b""), b""
            if line[:2] == smppacket.START_DELIMITER:
                self.dec = smppacket.decode()
                next(self.dec)
            elif line[:2] != smppacket.CONTINUE_DELIMITER or self.dec is None:
                continue
            try:
                self.dec.send(line)
            except StopIteration as done:
                pkts.append(done.value)
                self.dec = None
            except (SMPBadStartDelimiter, SMPBadContinueDelimiter, SMPBadCRC, binascii.Error, ValueError):
                self.dec = None
        return pkts


# --- SMP group 100: slot inspection (boot/smp_inspect.c) --------------------

@unique
class InspectErr(IntEnum):
    OK = 0
    EINVAL = 3
    ENOENT = 5
    ENOTSUP = 8
    EACCESSDENIED = 11


INSPECT_ERRORS = {3: "invalid argument", 5: "no image in that slot", 8: "not supported (feature disabled?)",
                  11: "refused outside an encrypted umcub link session"}


class _InspectErrorV1(smperr.ErrorV1):
    _GROUP_ID = INSPECT_GROUP


class _InspectErrorV2(smperr.ErrorV2[InspectErr]):
    _GROUP_ID = INSPECT_GROUP


class _InspectRequest(smpmsg.ReadRequest):
    _GROUP_ID = INSPECT_GROUP
    _ErrorV1 = _InspectErrorV1
    _ErrorV2 = _InspectErrorV2

    image: int
    slot: int


class _InspectResponse(smpmsg.ReadResponse):
    _GROUP_ID = INSPECT_GROUP

    rc: int = 0


class InspectVerifyResponse(_InspectResponse):
    _COMMAND_ID = 0
    valid: bool


class InspectVerify(_InspectRequest):
    """Full MCUboot validation of the image in a slot."""
    _COMMAND_ID = 0
    _Response = InspectVerifyResponse


class InspectHashResponse(_InspectResponse):
    _COMMAND_ID = 1
    sha: bytes
    len: int


class InspectHash(_InspectRequest):
    """SHA-256 over the stored image (or a range)."""
    _COMMAND_ID = 1
    _Response = InspectHashResponse
    off: int | None = None
    len: int | None = None


class InspectReadResponse(_InspectResponse):
    _COMMAND_ID = 2
    off: int
    data: bytes


class InspectRead(_InspectRequest):
    """Raw readback (UMCUB_CFG_READBACK)."""
    _COMMAND_ID = 2
    _Response = InspectReadResponse
    off: int
    len: int


# --- smpclient transport over CAN ISO-TP ------------------------------------

class SMPIsoTpTransport:
    """SMP packets as ISO-TP messages (umcub CAN transport, tools/smp_can.py).

    `connect()` takes no address: the IDs are given here (umcub: request ID =
    RX base + node address, response ID = TX base + node address)."""

    _smp_server_transport_buffer_size = None

    def __init__(self, interface, channel, bitrate, tx_id, rx_id, fd=False, data_bitrate=2000000,
                 extended=False, mtu=1024):
        self._args = dict(interface=interface, channel=channel, bitrate=bitrate, fd=fd,
                          data_bitrate=data_bitrate if fd else None)
        self._tx_id, self._rx_id, self._fd, self._ext, self._mtu = tx_id, rx_id, fd, extended, mtu
        self._bus = self._stack = None

    async def connect(self, address, timeout_s):
        import can
        import isotp
        self._bus = can.Bus(**self._args)
        mode = isotp.AddressingMode.Normal_29bits if self._ext else isotp.AddressingMode.Normal_11bits
        addr = isotp.Address(mode, txid=self._tx_id, rxid=self._rx_id)
        params = {"tx_padding": 0xCC, "can_fd": self._fd, "tx_data_length": 64 if self._fd else 8,
                  "blocking_send": True, "max_frame_size": 4095, "bitrate_switch": self._fd}
        self._notifier = can.Notifier(self._bus, [])
        self._stack = isotp.NotifierBasedCanStack(self._bus, self._notifier, address=addr, params=params)
        self._stack.start()

    async def disconnect(self):
        if self._stack:
            self._stack.stop()
            self._notifier.stop()
            self._bus.shutdown()
            self._stack = self._bus = None

    async def send(self, data):
        await asyncio.to_thread(self._stack.send, data, send_timeout=10.0)

    async def receive(self):
        while True:
            rsp = await asyncio.to_thread(self._stack.recv, True, 0.2)
            if rsp is not None:
                return bytes(rsp)

    async def send_and_receive(self, data):
        await self.send(data)
        return await self.receive()

    def initialize(self, smp_server_transport_buffer_size):
        self._smp_server_transport_buffer_size = smp_server_transport_buffer_size

    @property
    def mtu(self):
        return self._mtu

    @property
    def max_unencoded_size(self):
        return self._smp_server_transport_buffer_size or self._mtu
