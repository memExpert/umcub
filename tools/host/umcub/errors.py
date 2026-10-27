"""Exceptions of the umcub host library: a tool or an application catches UmcubError."""


class UmcubError(Exception):
    """Anything that went wrong talking to a bootloader."""


class NoAnswer(UmcubError):
    """The device did not answer (not in recovery, wrong port / node, bus problem)."""


class DeviceError(UmcubError):
    """The device answered with an error code."""

    def __init__(self, msg, rc=None):
        super().__init__(msg)
        self.rc = rc


class LinkError(UmcubError):
    """umcub link: selecting the device or the SECURE handshake failed."""


class Cancelled(UmcubError):
    """The operation was cancelled (the `cancel` event was set)."""
