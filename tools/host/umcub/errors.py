"""Exceptions of the umcub host library: a tool or an application catches UmcubError.

Every error has a stable `code` and its `params`; the English text is MESSAGES[code]
filled with the params. A translated program looks the code up in its own table and
fills in the same params, falling back to str(error). Device errors also carry the
device's `rc`; smp.ERRORS and lite.ERRORS have their English meaning. Errors of the
build helpers (configuration, layout, signing, keys) only have the code "error".
"""

MESSAGES = {
    "error": "{message}",
    # connections
    "file.read": "{path}: {reason}",
    "serial.open": "{port}: {reason}",
    "can.open": "CAN {interface}:{channel}: {reason}",
    "can.send": "CAN send: {reason}",
    "can.spec": "CAN: can:<python-can interface>:<channel>, e.g. can:socketcan:can0",
    "spec.option": "unknown option '{option}' (known: {known})",
    "transport.scheme": "transport scheme '{scheme}': lower-case letters, digits, '-' and '_'",
    "transport.duplicate": "transport '{scheme}' is already registered",
    "transport.type": "transport '{scheme}' returned {type}, not a umcub Connection",
    # umcub link
    "link.uid_not_addressed": "{name}: the node is selected by its address, not by UID",
    "link.select": "select the device by node address or UID",
    "link.no_hello": "no answer to HELLO (wrong node, not in recovery, or not a link transport)",
    "link.closed": "device refuses SECURE sessions (readout protection off, UMCUB_CFG_LINK_REQUIRE_RDP)",
    "link.keys_needed": "SECURE transport: the admin key and the device key are needed",
    "link.auth_failed": "authentication failed (wrong admin key, device key or no answer)",
    "key.private_needed": "{path}: a private key is needed",
    "key.invalid": "{path}: not a PEM key",
    # SMP
    "smp.no_answer": "no SMP answer (not in recovery, no SMP in this build, wrong port / node?)",
    "smp.client": "SMP: {reason}",
    "smp.device": "device: {reason} (rc {rc})",
    "smp.upload_refused": "upload refused: {reason}",
    # lite upload
    "lite.no_answer": "no answer from the bootloader (lite upload)",
    "lite.begin_refused": "begin refused: {reason}",
    "lite.data_refused": "data at {offset} refused: {reason}",
    "lite.end_refused": "end refused (not a valid image for this device?): {reason}",
    # cancel
    "cancelled.upload": "upload cancelled",
    "cancelled.read": "read cancelled",
    # images
    "image.not_signed": "not a signed MCUboot image (no header magic)",
    "image.no_hash": "signed image without a hash TLV",
    "image.no_board_type": "the image has no board type, the device is 0x{device:08x}",
    "image.board_type": "the image is for board type 0x{image:08x}, the device is 0x{device:08x}",
}


class UmcubError(Exception):
    """Anything that went wrong talking to a bootloader.

    UmcubError(code="...", **params) takes its text from MESSAGES; UmcubError("text")
    is a plain message with the code "error"."""

    code = "error"

    def __init__(self, message=None, *, code=None, **params):
        if code is not None:
            self.code = code
        if message is None:
            message = MESSAGES[self.code].format(**params)
        elif code is None and type(self).code == "error":
            params = {"message": message, **params}
        self.params = params
        super().__init__(message)


class NoAnswer(UmcubError):
    """The device did not answer (not in recovery, wrong port / node, bus problem)."""


class DeviceError(UmcubError):
    """The device answered with an error code (`rc`)."""

    def __init__(self, message=None, rc=None, **kw):
        if rc is not None:
            kw["rc"] = rc
        super().__init__(message, **kw)
        self.rc = rc


class LinkError(UmcubError):
    """umcub link: selecting the device or the SECURE handshake failed."""


class Cancelled(UmcubError):
    """The operation was cancelled (the `cancel` event was set)."""
