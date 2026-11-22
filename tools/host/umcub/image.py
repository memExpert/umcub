"""Signed MCUboot images as files: header, version, hash and the umcub board type
(the TLVs MCUboot and umcub check), read without imgtool."""
import struct

from .board_names import board_label
from .errors import UmcubError

IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC, TLV_PROT_INFO_MAGIC = 0x6907, 0x6908
TLV_SHA256, TLV_SHA384, TLV_SHA512 = 0x10, 0x11, 0x12
HASH_LEN = {TLV_SHA256: 32, TLV_SHA384: 48, TLV_SHA512: 64}
TLV_BOARD_TYPE = 0xA0           # UMCUB_TLV_BOARD_TYPE: u32 little endian, signed
F_ENCRYPTED = 0x04 | 0x08     # IMAGE_F_ENCRYPTED_AES128 / _AES256


def _area(data, off, magic):
    """The TLVs of the area at off ({type: value}, the first of a type) and its end; it must be one with this magic,
    inside the data, and end exactly with its last TLV."""
    if off + 4 > len(data):
        raise UmcubError(code="image.truncated")
    m, total = struct.unpack_from("<HH", data, off)
    if m != magic or total < 4 or off + total > len(data):
        raise UmcubError(code="image.truncated")
    tlvs, end, off = {}, off + total, off + 4
    while off < end:
        if off + 4 > end:
            raise UmcubError(code="image.truncated")
        t, n = struct.unpack_from("<HH", data, off)
        if off + 4 + n > end:
            raise UmcubError(code="image.truncated")
        tlvs.setdefault(t, data[off + 4:off + 4 + n])
        off += 4 + n
    return tlvs, end


class Image:
    """A signed image (`.signed.bin` / `.encrypted.bin`): version, hash (what SMP
    `image state-read` lists and `state-write` selects), board_type (None without
    the signed, protected TLV), size, encrypted."""

    def __init__(self, data):
        if len(data) < 32 or struct.unpack_from("<I", data)[0] != IMAGE_MAGIC:
            raise UmcubError(code="image.not_signed")
        hdr_size, prot_size, img_size, flags = struct.unpack_from("<HHII", data, 8)
        if hdr_size < 32:                       # MCUboot refuses it (IMAGE_HEADER_SIZE)
            raise UmcubError(code="image.bad_header", size=hdr_size)
        major, minor, rev, build = struct.unpack_from("<BBHI", data, 20)
        self.data = bytes(data)
        self.version = f"{major}.{minor}.{rev}" + (f"+{build}" if build else "")
        self.encrypted = bool(flags & F_ENCRYPTED)
        # The TLV areas as MCUboot's iterator reads them (bootutil tlv.c): a protected one exactly when the header
        # states its size, then the unprotected one; each ends with its last TLV. Anything else: not this image.
        off, prot = hdr_size + img_size, {}
        if prot_size:
            start = off
            prot, off = _area(data, off, TLV_PROT_INFO_MAGIC)
            if off - start != prot_size:
                raise UmcubError(code="image.truncated")
        self.tlvs = {**_area(data, off, TLV_INFO_MAGIC)[0], **prot}       # the protected one wins
        t = next((t for t in HASH_LEN if t in self.tlvs), None)
        if t is None:
            raise UmcubError(code="image.no_hash")
        self.hash = self.tlvs[t]
        if len(self.hash) != HASH_LEN[t]:
            raise UmcubError(code="image.truncated")
        bt = prot.get(TLV_BOARD_TYPE)           # the bootloader checks the signed one only
        self.board_type = struct.unpack("<I", bt)[0] if bt and len(bt) == 4 else None
        self.size = len(data)

    @classmethod
    def load(cls, path):
        try:
            return cls(open(path, "rb").read())
        except OSError as e:
            raise UmcubError(code="file.read", path=str(path), reason=e.strerror or str(e)) from e

    def check_board(self, board_type):
        """Raise if the bootloader of `board_type` would refuse this image (umcub
        checks the board type unless it is 0)."""
        if not board_type:
            return
        if self.board_type is None:
            raise UmcubError(code="image.no_board_type", device=board_label(board_type))
        if self.board_type != board_type:
            raise UmcubError(code="image.board_type", image=board_label(self.board_type),
                             device=board_label(board_type))
