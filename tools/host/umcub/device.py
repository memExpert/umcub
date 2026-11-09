"""A connected bootloader: the connection, and on a umcub link bus the selected node."""
import logging
import time
from concurrent.futures import ThreadPoolExecutor

from . import lite
from .connection import KIND_LITE, _parse, can_bus, open_connection
from .errors import LinkError, UmcubError
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


def connect(spec, node=None, uid=None, admin_key=None, device_key=None, timeout=3.0, tap=None):
    """Open `spec` (see umcub.connection). On serial and UDP, `node` or `uid`
    selects a device on a umcub link bus (SECURE: with `admin_key`, a PEM file
    or a signing function (umcub.link.key_signer), and `device_key`, a PEM
    file); on CAN `node` selects the node's IDs. `tap` sees every payload on
    the connection (umcub.connection.add_tap)."""
    conn = open_connection(spec, node=node or 0, tap=tap)
    try:
        if conn.selects_node:
            if uid is not None:
                raise LinkError(code="link.uid_not_addressed", name=conn.name)
            return Device(conn, timeout=timeout)
        if node is None and uid is None:
            return Device(conn, timeout=timeout)
        link = Link(conn, node, uid, admin_key, device_key, timeout)
        link.hello()
        return Device(conn, link, timeout)
    except BaseException:
        conn.close()
        raise


CAN_NODES = range(8)        # node addresses that fit the default IDs (host TX 0x7C0 + n, RX 0x7C8 + n)
CAN_PROBE_TIMEOUT = 1.0     # seconds per probe of a node (all nodes are probed at once)


def discover(spec, rounds=8, slots=16, slot_ms=20, timeout=3.0, tap=None, nodes=CAN_NODES):
    """The devices on the bus of `spec`. Serial or UDP broadcast: the identities of the umcub link devices. CAN:
    each of `nodes` is asked a lite frame of no known kind (answered "not supported", nothing changes), else an SMP
    echo; the ones that answer as {"addr": node, "protocol": "smp" | "lite"}, by node."""
    if _parse(spec)[0].scheme != "can":
        with open_connection(spec, tap=tap) as conn:
            return Link(conn, timeout=timeout).discover(rounds, slots, slot_ms)
    # All nodes at once, each probe with its node's IDs on the channel's shared bus; a silent node makes
    # python-can-isotp warn ("Reception of FlowControl timed out") through logging.
    log = logging.getLogger("isotp")
    level = log.level
    log.setLevel(logging.ERROR)
    try:
        with can_bus(spec), ThreadPoolExecutor() as pool:
            found = list(pool.map(lambda n: _probe_can(spec, n, tap), nodes))
    finally:
        log.setLevel(level)
    return [{"addr": n, "protocol": p} for n, p in zip(nodes, found) if p]


def _probe_can(spec, node, tap):
    # lite first: one single frame. An SMP echo is several ISO-TP frames, and a device left waiting for the rest of
    # it after the probe gives up takes no new frame for a while.
    from .smp import Smp
    with connect(spec, node=node, timeout=CAN_PROBE_TIMEOUT, tap=tap) as dev:
        dev.send(KIND_LITE, lite.frame("?"))
        r = dev.recv(KIND_LITE, CAN_PROBE_TIMEOUT)
        if r and lite.parse_answer(r):
            return "lite"
        try:
            with Smp(dev) as smp:
                smp.echo("umcub")
            return "smp"
        except UmcubError:
            return None
