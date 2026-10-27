"""Host library of umcub: talk to a umcub bootloader from a tool or an application.

    import umcub
    with umcub.connect("/dev/ttyUSB0", node=5) as dev:         # serial, UDP or CAN; link node / CAN IDs
        with umcub.Smp(dev) as smp:
            smp.upload(open("app.signed.bin", "rb").read(), progress=lambda done, total: ...)
            smp.reset()
    umcub.discover("/dev/ttyUSB1")                              # umcub link devices on a bus
    umcub.lite.upload(dev, data)                                # bootloaders without SMP

Nothing here prints or exits: errors are UmcubError (NoAnswer, DeviceError,
LinkError, Cancelled), progress is a callback, a threading.Event cancels.
The command line tool is `umcub` (umcub.cli).
"""
from . import lite
from .connection import KIND_LINK, KIND_LITE, KIND_SMP, KIND_USER, open_connection
from .device import Device, connect, discover
from .errors import Cancelled, DeviceError, LinkError, NoAnswer, UmcubError
from .image import Image
from .smp import Smp

__all__ = ["Cancelled", "Device", "DeviceError", "Image", "KIND_LINK", "KIND_LITE", "KIND_SMP", "KIND_USER", "LinkError",
           "NoAnswer", "Smp", "UmcubError", "connect", "discover", "lite", "open_connection"]
