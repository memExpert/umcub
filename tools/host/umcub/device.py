"""A connected bootloader: the connection, and on a umcub link bus the selected node."""
import time

from .connection import IsoTp, open_connection
from .errors import LinkError
from .link import Link

# A SECURE session ends on the device after a while without traffic; the
# next request then starts a new one.
SESSION_IDLE = 20.0


class Device:
    """send(kind, payload) / recv(kind, timeout) / text(cmd, wait) to one bootloader,
    directly or as DATA of a umcub link node (`link`, `identity`)."""

    def __init__(self, conn, link=None, timeout=3.0):
        self.conn, self.link, self.timeout = conn, link, timeout
        self.identity = link.ident if link else None
        self._last = time.monotonic()

    @property
    def name(self):
        return self.conn.name + (f" node {self.identity['addr']}" if self.identity else "")

    @property
    def stream(self):
        """Payloads go as lines of a plain stream (frames limited by the line buffer)."""
        return self.conn.stream and not self.link

    @property
    def mtu(self):
        return self.conn.mtu

    def send(self, kind, payload):
        if self.link:
            if self.link.session and time.monotonic() - self._last > SESSION_IDLE:
                self.link.hello()
            self._last = time.monotonic()
            self.link.send_data(payload)
        else:
            self.conn.send(kind, payload)

    def recv(self, kind, timeout=None):
        timeout = self.timeout if timeout is None else timeout
        if self.link:
            r = self.link.recv_data(timeout)
            self._last = time.monotonic()
            return r
        return self.conn.recv(kind, timeout)

    def text(self, cmd, wait=1.0):
        """A text command and what came back."""
        if self.link:
            self.send(None, cmd.encode())
            r = self.recv(None, wait)
            return r.decode(errors="replace") if r else ""
        return self.conn.text(cmd, wait)

    def close(self):
        try:
            if self.link:
                self.link.close()
        finally:
            self.conn.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def connect(spec, node=None, uid=None, admin_key=None, device_key=None, timeout=3.0):
    """Open `spec` (see umcub.connection). On serial and UDP, `node` or `uid`
    selects a device on a umcub link bus (SECURE: with `admin_key` and
    `device_key`, PEM files); on CAN `node` selects the node's IDs."""
    conn = open_connection(spec, node=node or 0)
    try:
        if isinstance(conn, IsoTp):
            if uid is not None:
                raise LinkError("CAN selects a node by its address, not by UID")
            return Device(conn, timeout=timeout)
        if node is None and uid is None:
            return Device(conn, timeout=timeout)
        link = Link(conn, node, uid, admin_key, device_key, timeout)
        link.hello()
        return Device(conn, link, timeout)
    except BaseException:
        conn.close()
        raise


def discover(spec, rounds=8, slots=16, slot_ms=20, timeout=3.0):
    """Identities of the umcub link devices on the bus of `spec` (serial or UDP broadcast)."""
    with open_connection(spec) as conn:
        return Link(conn, timeout=timeout).discover(rounds, slots, slot_ms)
