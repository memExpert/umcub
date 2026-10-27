"""Signed MCUboot images as files: header, version, hash and the umcub board type
(the TLVs MCUboot and umcub check), read without imgtool."""
import struct

from .errors import UmcubError

IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC, TLV_PROT_INFO_MAGIC = 0x6907, 0x6908
TLV_SHA256, TLV_SHA384, TLV_SHA512 = 0x10, 0x11, 0x12
TLV_BOARD_TYPE = 0xA0           # UMCUB_TLV_BOARD_TYPE: u32 little endian, signed
F_ENCRYPTED = 0x04 | 0x08     # IMAGE_F_ENCRYPTED_AES128 / _AES256


class Image:
    """A signed image (`.signed.bin` / `.encrypted.bin`): version, hash (what SMP
    `image state-read` lists and `state-write` selects), board_type (None without
    the TLV), size, encrypted."""

    def __init__(self, data):
        if len(data) < 32 or struct.unpack_from("<I", data)[0] != IMAGE_MAGIC:
            raise UmcubError("not a signed MCUboot image (no header magic)")
        hdr_size, prot_size, img_size, flags = struct.unpack_from("<HHII", data, 8)
        major, minor, rev, build = struct.unpack_from("<BBHI", data, 20)
        self.data = bytes(data)
        self.version = f"{major}.{minor}.{rev}" + (f"+{build}" if build else "")
        self.encrypted = bool(flags & F_ENCRYPTED)
        self.tlvs = {}
        off = hdr_size + img_size
        for magic in (TLV_PROT_INFO_MAGIC, TLV_INFO_MAGIC):
            if off + 4 > len(data):
                break
            m, total = struct.unpack_from("<HH", data, off)
            if m != magic:
                continue
            end, off = off + total, off + 4
            while off + 4 <= end:
                t, n = struct.unpack_from("<HH", data, off)
                self.tlvs.setdefault(t, data[off + 4:off + 4 + n])
                off += 4 + n
            off = end
        self.hash = next((self.tlvs[t] for t in (TLV_SHA256, TLV_SHA384, TLV_SHA512) if t in self.tlvs), None)
        if self.hash is None:
            raise UmcubError("signed image without a hash TLV")
        bt = self.tlvs.get(TLV_BOARD_TYPE)
        self.board_type = struct.unpack("<I", bt)[0] if bt and len(bt) == 4 else None
        self.size = len(data)

    @classmethod
    def load(cls, path):
        try:
            return cls(open(path, "rb").read())
        except OSError as e:
            raise UmcubError(f"{path}: {e.strerror}") from e

    def check_board(self, board_type):
        """Raise if the bootloader of `board_type` would refuse this image (umcub
        checks the board type unless it is 0)."""
        if not board_type:
            return
        if self.board_type is None:
            raise UmcubError(f"the image has no board type, the device is 0x{board_type:08x}")
        if self.board_type != board_type:
            raise UmcubError(f"the image is for board type 0x{self.board_type:08x}, the device is 0x{board_type:08x}")
