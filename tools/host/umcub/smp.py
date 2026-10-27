"""SMP (MCUmgr) to a umcub bootloader on any connection, also through umcub link.

The SMP side is smpclient (the library under smpmgr); `Smp` runs it
synchronously on a `Device`, so a tool or a GUI worker thread just calls
upload() / images() / reset() / verify(). The umcub inspection group (SMP group
100, src/proto/smp_inspect.c) is here as smpclient requests.
"""
import asyncio
import hashlib
import logging
from enum import IntEnum, unique

from smp import error as smperr
from smp import message as smpmsg
from smpclient import SMPClient
from smpclient.exceptions import SMPClientException
from smpclient.generics import error, success
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import EchoWrite, ResetWrite

from .connection import KIND_SMP
from .errors import Cancelled, DeviceError, NoAnswer, UmcubError

# boot_serial has no MCUmgr parameters command when UMCUB_CFG_SMP_PARAMS is off
# (rc 8): smpclient then keeps the transport's size - not worth a warning.
logging.getLogger("smpclient").setLevel(logging.CRITICAL)

INSPECT_GROUP = 100


@unique
class InspectErr(IntEnum):
    OK = 0
    EINVAL = 3
    ENOENT = 5
    ENOTSUP = 8
    EACCESSDENIED = 11


ERRORS = {3: "invalid argument", 5: "no image in that slot",
          6: "bad state (an image is already marked for the next boot, or not the newest)",
          8: "not supported (feature disabled?)",
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


class Transport:
    """smpclient transport on a Device: NLIP lines on a plain stream, one packet
    otherwise (UDP, CAN ISO-TP, umcub link DATA). Answers of other requests
    (sequence) are dropped; a request without an answer is sent again."""

    _smp_server_transport_buffer_size = None
    RECV_POLL = 0.1

    def __init__(self, dev):
        self.dev = dev

    async def connect(self, address, timeout_s):
        pass

    async def disconnect(self):
        pass

    async def send(self, data):
        await asyncio.to_thread(self.dev.send, KIND_SMP, data)

    async def receive(self):
        while True:
            r = await asyncio.to_thread(self.dev.recv, KIND_SMP, self.RECV_POLL)
            if r is not None:
                return r

    async def send_and_receive(self, data):
        seq = data[6]
        while True:
            await self.send(data)
            waited = 0.0
            while waited < self.dev.timeout:     # until smpclient's own timeout ends it
                r = await asyncio.to_thread(self.dev.recv, KIND_SMP, self.RECV_POLL)
                waited += self.RECV_POLL
                if r is not None and len(r) >= 8 and r[6] == seq:
                    return r

    def initialize(self, smp_server_transport_buffer_size):
        self._smp_server_transport_buffer_size = smp_server_transport_buffer_size

    @property
    def mtu(self):
        return self.dev.mtu

    @property
    def max_unencoded_size(self):
        size = self._smp_server_transport_buffer_size or self.mtu
        # NLIP: length and CRC share the buffer, and boot_serial reserves room
        # for base64 padding when it checks each line (base64_decode_len()).
        return size - 8 if self.dev.stream else size


class Smp:
    """SMP requests to `dev`, synchronous; raises NoAnswer / DeviceError."""

    def __init__(self, dev, timeout=None):
        self.dev = dev
        self.timeout = dev.timeout if timeout is None else timeout
        self.loop = asyncio.new_event_loop()
        self.client = SMPClient(Transport(dev), dev.name, timeout_s=self.timeout)
        self._run(self.client.connect())

    def close(self):
        self.loop.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _run(self, coro):
        try:
            return self.loop.run_until_complete(coro)
        except TimeoutError as e:
            raise NoAnswer("no SMP answer (not in recovery, no SMP in this build, wrong port / node?)") from e
        except SMPClientException as e:
            raise UmcubError(f"SMP: {e}") from e

    def request(self, req, timeout=None):
        """One request; the typed response, or DeviceError."""
        r = self._run(self.client.request(req, timeout))
        if error(r):
            rc = int(r.rc) if hasattr(r, "rc") else int(r.err.rc)
            if rc == 0:
                return r                    # boot_serial answers e.g. reset with {"rc": 0}
            raise DeviceError(f"device: {ERRORS.get(rc, 'error')} (rc {rc})", rc)
        if success(r) and getattr(r, "rc", 0):
            raise DeviceError(f"device: {ERRORS.get(r.rc, 'error')} (rc {r.rc})", r.rc)
        return r

    def echo(self, text):
        return self.request(EchoWrite(d=text)).r

    def images(self):
        """Image slots: dicts with image, slot, version, hash and the flags."""
        out = []
        for img in self.request(ImageStatesRead()).images:
            d = dict(image=img.image or 0, slot=img.slot, version=str(img.version), hash=img.hash or b"")
            for k in ("active", "confirmed", "pending", "permanent", "bootable"):
                d[k] = bool(getattr(img, k))
            out.append(d)
        return out

    def upload(self, data, image=0, progress=None, cancel=None):
        """Image upload into the primary slot of `image` (SMP's image field; with
        UMCUB_CFG_SMP_DIRECT_UPLOAD it names a slot, see direct_slot());
        progress(done, total), `cancel` event."""

        async def run():
            gen = self.client.upload(data, slot=image, first_timeout_s=max(40.0, self.timeout),
                                     subsequent_timeout_s=self.timeout)
            try:
                async for off in gen:
                    if progress:
                        progress(off, len(data))
                    if cancel is not None and cancel.is_set():
                        raise Cancelled("upload cancelled")
            finally:
                await gen.aclose()
        try:
            self._run(run())
        except SMPClientException as e:
            raise DeviceError(f"upload refused: {e}") from e

    def mark(self, image_hash, permanent=False):
        """Mark the image with this hash (Image.hash) in a secondary slot for a test
        boot, or for good (`permanent`); the next reset installs it. Needs
        UMCUB_CFG_SMP_DIRECT_UPLOAD (not in direct-xip)."""
        self.request(ImageStatesWrite(hash=image_hash, confirm=permanent))

    def reset(self):
        self.request(ResetWrite())

    def verify(self, image, slot):
        """Full MCUboot validation of the image in a slot (hash, signature, board type)."""
        return bool(self.request(InspectVerify(image=image, slot=slot)).valid)

    def hash(self, image, slot, off=0, length=0):
        """(SHA-256, length) of the stored image or of a range."""
        r = self.request(InspectHash(image=image, slot=slot, off=off or None, len=length or None))
        return bytes(r.sha), r.len

    def read(self, image, slot, off=0, length=0, progress=None, cancel=None):
        """Raw readback (UMCUB_CFG_READBACK); length 0: the whole stored image."""
        if not length:
            length = self.hash(image, slot)[1] - off
        out = bytearray()
        while len(out) < length:
            if cancel is not None and cancel.is_set():
                raise Cancelled("read cancelled")
            n = min(128, length - len(out))     # the bootloader returns what fits its response buffer
            out += self.request(InspectRead(image=image, slot=slot, off=off + len(out), len=n)).data
            if progress:
                progress(len(out), length)
        return bytes(out)


def direct_slot(image, slot):
    """SMP image field of a slot with UMCUB_CFG_SMP_DIRECT_UPLOAD (MCUboot's
    numbering: 1 + 2 * image + slot; slot 0 primary, 1 secondary)."""
    return 1 + 2 * image + slot


def file_hash(data, off=0, length=0):
    """What hash() reports for the same range of a local file."""
    data = data[off:]
    return hashlib.sha256(data[:length or len(data)]).digest()
