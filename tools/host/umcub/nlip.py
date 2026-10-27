"""SMP over a stream (NLIP): packets as base64 lines, as MCUboot's boot_serial expects them."""
import binascii

from smp import packet as smppacket
from smp.exceptions import SMPBadContinueDelimiter, SMPBadCRC, SMPBadStartDelimiter

# boot_serial accepts lines of 2 delimiter bytes + 124 base64 characters + '\n'.
LINE_LENGTH = 128


def nlip_encode(pkt):
    """One SMP packet as NLIP lines."""
    return b"".join(smppacket.encode(pkt, line_length=LINE_LENGTH))


class NlipDecoder:
    """Bytes in, complete SMP packets out; other lines (log, text) are skipped."""

    def __init__(self):
        self.line = b""
        self.dec = None

    def feed(self, chunk):
        pkts = []
        *lines, self.line = (self.line + bytes(chunk)).split(b"\n")
        for line in lines:
            line = line.replace(b"\r", b"") + b"\n"
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
