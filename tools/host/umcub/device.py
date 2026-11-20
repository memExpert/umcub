"""A connected bootloader: the connection, and on a umcub link bus the selected node."""
import logging
import time
from concurrent.futures import ThreadPoolExecutor

from . import lite
from .connection import Connection, _parse, can_bus, open_connection
from .errors import LinkError, UmcubError
from .link import Link

# A SECURE session idle this long starts again: the minimum of UMCUB_CFG_LINK_SESSION_MS.
SESSION_IDLE = 20.0


class Device:
    """send(kind, payload) / recv(kind, timeout) / text(cmd, wait) to one bootloader,
    directly or as DATA of a umcub link node (`link`, `identity`)."""

    def __init__(self, conn, link=None, timeout=3.0, owns_conn=True):
        self.conn, self.link, self.timeout, self._owns_conn = conn, link, timeout, owns_conn
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
            return self.link.recv_data(timeout)     # waiting is no traffic for the device's session timer
        return self.conn.recv(kind, timeout)

    def text(self, cmd, wait=1.0):
        """A text command and what came back."""
        if self.link:
            self.send(None, cmd.encode())
            r = self.recv(None, wait)
            return r.decode(errors="replace") if r else ""
        return self.conn.text(cmd, wait)

    def ping(self, protocol="smp"):
        """A request that changes nothing - keeps a SECURE session alive when sent within SESSION_IDLE, shows the
        device still answers: an SMP echo, or a lite frame of no known kind ('P', answered "not supported"). Raises
        UmcubError (NoAnswer when silent)."""
        if protocol == "lite":
            lite.request(self, lite.frame("P"), retries=1)
        else:
            from .smp import Smp
            with Smp(self) as smp:
                smp.echo("umcub")

    def close(self):
        try:
            if self.link:
                self.link.close()
        finally:
            if self._owns_conn:          # a connection the program opened stays open for its other devices
                self.conn.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def connect(spec, node=None, uid=None, admin_key=None, device_key=None, timeout=3.0, tap=None):
    """Open `spec` (see umcub.connection). On serial and UDP, `node` or `uid`
    selects a device on a umcub link bus (SECURE: with `admin_key`, a PEM file
    or a signing function (umcub.link.key_signer), and `device_key`, a PEM
    file); on CAN `node` selects the node's IDs, and the keys open a SECURE
    session with that node. `tap` sees every payload on
    the connection (umcub.connection.add_tap). `spec` can also be a Connection
    the program opened (open_connection): the device then uses it and close()
    leaves it open, so several link nodes share one port (one at a time)."""
    owns = not isinstance(spec, Connection)
    if tap is not None and not owns:
        raise UmcubError(code="tap.open_connection")    # it would wrap the program's port for good: add_tap() once
    conn = open_connection(spec, node=node or 0, tap=tap) if owns else spec
    try:
        if conn.selects_node and uid is not None:
            raise LinkError(code="link.uid_not_addressed", name=conn.name)
        keys = bool(admin_key or device_key)
        if (conn.selects_node and not (keys and node)) or (node is None and uid is None):  # no link session
            if keys:
                # keys no SECURE session would use: an error, never a silent unprotected device
                raise LinkError(code="link.keys_unused", name=conn.name)
            return Device(conn, timeout=timeout, owns_conn=owns)
        link = Link(conn, node, uid, admin_key, device_key, timeout)
        link.hello()
        return Device(conn, link, timeout, owns)
    except BaseException:
        if owns:
            conn.close()
        raise


CAN_NODES = range(8)        # node addresses that fit the default IDs (host TX 0x7C0 + n, RX 0x7C8 + n)
CAN_PROBE_TIMEOUT = 1.0     # seconds per probe of a node (all nodes are probed at once)


def discover(spec, rounds=8, slots=16, slot_ms=20, timeout=3.0, tap=None, nodes=CAN_NODES):
    """The devices on the bus of `spec` (a device string, or an open Connection of a link bus). Serial or UDP
    broadcast: the identities of the umcub link devices. CAN: each of `nodes` is asked a lite frame of no known kind
    (answered "not supported", nothing changes), else a link HELLO (a SECURE node answers its identity, nothing is
    executed), else an SMP echo (Device.ping); the ones that answer as {"addr": node, "protocol": "smp" | "lite"},
    or {"protocol": "link", **identity}, by node."""
    if isinstance(spec, Connection):         # a port the program keeps open
        if tap is not None:
            raise UmcubError(code="tap.open_connection")
        return Link(spec, timeout=timeout).discover(rounds, slots, slot_ms)
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
    return [p if isinstance(p, dict) else {"addr": n, "protocol": p} for n, p in zip(nodes, found) if p]


def _probe_can(spec, node, tap):
    # lite first: one single frame. An SMP echo is several ISO-TP frames, and a device left waiting for the rest of
    # it after the probe gives up takes no new frame for a while.
    # A link frame is several ISO-TP frames too; a plain device takes it whole, as a frame that is no SMP (a text
    # command it does not know, or a board protocol's frame - such protocols ignore what they do not know).
    with connect(spec, node=node, timeout=CAN_PROBE_TIMEOUT, tap=tap) as dev:
        if _ping(dev, "lite"):
            return "lite"
        if node:                                # node 0 is never a link node
            link = Link(dev.conn, node, timeout=CAN_PROBE_TIMEOUT)
            try:
                link.hello()
            except UmcubError:
                pass                            # a SECURE node: link.keys_needed, its identity known
            if link.ident:
                return {"protocol": "link", **link.ident}
        return "smp" if _ping(dev, "smp") else None


def _ping(dev, protocol):
    try:
        dev.ping(protocol)
        return True
    except UmcubError:
        return False
