"""Lite upload protocol: bootloaders built without SMP (UMCUB_CFG_LITE_UPLOAD;
frames: src/proto/include/umcub_lite.h). Every frame is answered; lost frames are repeated."""
import binascii
import struct

from .connection import KIND_LITE
from .errors import Cancelled, DeviceError, NoAnswer

MAGIC = 0xA6
# Data bytes per 'D' frame that fit every build (UMCUB_CFG_SMP_MTU >= 512,
# checked at build time): a line of base64 text in the 512-byte line buffer on
# serial (2 prefix bytes + newline), one 512-byte packet otherwise; 8 bytes of
# each frame are header and CRC.
CHUNK_SERIAL = (512 - 3) // 4 * 3 - 8      # 373
CHUNK_PACKET = 512 - 8                      # 504
ERRORS = {-1: "invalid (EINVAL)", -2: "flash / image error (EIO)", -3: "timeout", -4: "not supported",
          -5: "busy (slot in use or the running image is not confirmed)"}


def frame(kind, payload=b""):
    body = bytes([MAGIC, ord(kind)]) + payload
    return body + struct.pack(">H", binascii.crc_hqx(body, 0))       # CRC-16/XMODEM


def parse_answer(f):
    if len(f) != 9 or f[0] != MAGIC or f[1] != ord("k") or binascii.crc_hqx(f[:7], 0) != struct.unpack(">H", f[7:])[0]:
        return None
    return struct.unpack("<bI", f[2:7])


def request(dev, f, timeout=None, retries=5):
    """Send a frame until it is answered; returns (rc, taken)."""
    for _ in range(retries):
        dev.send(KIND_LITE, f)
        r = dev.recv(KIND_LITE, timeout)
        ans = parse_answer(r) if r else None
        if ans:
            return ans
    raise NoAnswer("no answer from the bootloader (lite upload)")


def _check(rc, what):
    if rc:
        raise DeviceError(f"{what}: {ERRORS.get(rc, rc)}", rc)


def upload(dev, data, image=0, slot=0, test=False, permanent=False, chunk=None, progress=None, cancel=None):
    """Write a signed image into slot 0 (primary) or 1 (secondary; `test` /
    `permanent` mark it for the next boot). progress(done, total) after every
    frame; setting the `cancel` event stops the upload."""
    chunk = chunk or (CHUNK_SERIAL if dev.stream else CHUNK_PACKET)
    t = dev.timeout
    rc, _ = request(dev, frame("B", struct.pack("<BBI", image, slot, len(data))), t + 10)   # erases
    _check(rc, "begin refused")
    off = 0
    while off < len(data):
        if cancel is not None and cancel.is_set():
            raise Cancelled("upload cancelled")
        rc, taken = request(dev, frame("D", struct.pack("<I", off) + data[off:off + chunk]), t)
        _check(rc, f"data at {off} refused")
        off = taken
        if progress:
            progress(off, len(data))
    flags = (1 if test or permanent else 0) | (2 if permanent else 0)
    rc, _ = request(dev, frame("E", bytes([flags])), t + 30)     # in-place decryption takes a while
    _check(rc, "end refused (not a valid image for this device?)")


def reset(dev):
    request(dev, frame("R"))
