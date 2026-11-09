"""Host library of umcub: talk to a umcub bootloader from a tool or an application.

    import umcub
    with umcub.connect("/dev/ttyUSB0", node=5) as dev:         # serial, UDP or CAN; link node / CAN IDs
        with umcub.Smp(dev) as smp:
            smp.upload(open("app.signed.bin", "rb").read(), progress=lambda done, total: ...)
            smp.reset()
        umcub.flash(dev, umcub.Image.load("app.signed.bin"), reset=True)   # the same with the checks
    umcub.discover("/dev/ttyUSB1")                              # umcub link devices on a bus
    umcub.adapters()                                            # serial ports, CAN, network, plugins
    umcub.load_board_names("boards.json")                       # names of board types, shown with them
    umcub.lite.upload(dev, data)                                # bootloaders without SMP

Nothing here prints or exits: errors are UmcubError (NoAnswer, DeviceError,
LinkError, Cancelled), progress is a callback, a threading.Event cancels.
Signing and keys (umcub.config, umcub.sign, umcub.keys) are for builds and IDE
post-build steps. The command line tool is `umcub` (umcub.cli).
"""
from . import lite
from .adapter import Adapter, adapters
from .board_names import board_label, load_board_names
from .connection import (KIND_LINK, KIND_LITE, KIND_SMP, KIND_USER, Connection, TapEvent, Transport, add_tap,
                         exclusive_key, open_connection, register_transport, transports)
from .device import Device, connect, discover
from .errors import Cancelled, DeviceError, LinkError, NoAnswer, UmcubError
from .flasher import flash
from .image import Image
from .link import LINK_NAMES
from .plugins import Plugin, load_plugins, plugins


def __getattr__(name):
    # SMP (smpclient) only when used: signing and keys work without it.
    if name == "Smp":
        from .smp import Smp
        return Smp
    raise AttributeError(f"module 'umcub' has no attribute '{name}'")


__all__ = ["Adapter", "Cancelled", "Connection", "Device", "DeviceError", "Image", "KIND_LINK", "KIND_LITE",
           "KIND_SMP", "KIND_USER", "LINK_NAMES", "LinkError", "NoAnswer", "Plugin", "Smp", "TapEvent", "Transport",
           "UmcubError", "adapters", "add_tap", "board_label", "connect", "discover", "exclusive_key", "flash", "lite",
           "load_board_names", "load_plugins", "open_connection", "plugins", "register_transport", "transports"]
