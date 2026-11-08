# Host tool and library

`tools/host/umcub` is the host side of umcub: a Python library for your own tools and applications, and the
`umcub` command line tool on top of it. It speaks every upload protocol of the bootloader - SMP, the lite upload
protocol, umcub link (addressing on a shared bus, SECURE sessions) and the inspection group - on every transport a
PC reaches: serial ports (UART and RS485 adapters, USB CDC), UDP and CAN (any python-can interface: SocketCAN,
PCAN, Kvaser, Vector, IXXAT, slcan / CANable, candleLight, ...).

`tools/setup.sh` installs it into `.venv` (`pip install -e tools/host`, esp-idf-kconfig from
`tools/requirements.txt`). Elsewhere `pip install <umcub>/tools/host` is enough for devices (a flasher);
`"<umcub>/tools/host[build]"` adds esp-idf-kconfig for `config`, `image` and `keys`.

## Devices

A device is one string (`-d`, or `$UMCUB_DEVICE`):

| Transport | String |
|---|---|
| serial | `/dev/ttyUSB0`, `COM3`, `serial:/dev/ttyUSB0,baud=921600` (default 115200) |
| UDP | `192.168.1.50`, `udp:192.168.1.50,port=1337` |
| CAN ISO-TP | `can:<python-can interface>:<channel>`, e.g. `can:socketcan:can0`, `can:pcan:PCAN_USBBUS1,bitrate=250000`, `can:slcan:/dev/ttyACM0,fd,data-bitrate=2000000`; also `tx=0x7C0`, `rx=0x7C8` (ID bases), `ext` (29-bit), `mtu=1024` |

| plugin | `<scheme>:<target>[,option[=value]]...`, see [Transport plugins](#transport-plugins); `umcub transports` lists the schemes |

`--node N` selects a device on a [umcub link](link.md) bus (serial, UDP) or, on CAN, the node's IDs (bases + N);
`--uid` selects an unassigned link device. SECURE link transports also need `--admin-key` (private) and
`--device-key` (the public half is enough). In a program `admin_key` can also be a signing function
`(challenge, eph_pub, nonce) -> r || s` (64 bytes; `challenge` is the whole CHALLENGE payload, the device's identity
with its UID and board type, then the 32-byte challenge), e.g. a request to a server that keeps the admin key and
computes the signed hash itself (`umcub.link.transcript()`); `umcub.link.key_signer(path)` is the one of a key file.

`--names boards.json` shows the user's names of board types (`UMCUB_CFG_BOARD_TYPE`) next to the hex id: in
`discover`, `info`, `sniff` and when an image is for another board. The file is a JSON object of ids and names,
`{"0x00010002": "Pump controller"}`; `--names` repeats, a later file wins. In a program
`umcub.load_board_names(*paths)` replaces the catalog with those files (none empties it) and `umcub.board_label(id)`
gives `0x00010002 (Pump controller)`, or the hex alone.

## Adapters

`umcub adapters` (`umcub.adapters()` in a program) lists what is plugged in now, each with a ready device string:

```
/dev/ttyUSB0                 /dev/ttyUSB0                 FT232R USB UART (FTDI) [0403:6001 A5069RR4]
/dev/ttyUSB1                 /dev/ttyUSB1                 USB Serial [1a86:7523]
udp:192.168.0.255            enp10s0 192.168.0.109/24     broadcast 192.168.0.255
                             enp12s0u5u2                  no link (cable?), USB 10/100/1000 LAN (Realtek) [0bda:8153 DF1300E04C151FB0]
can:socketcan:can0           socketcan:can0               Candlelight 2.5 - Multiboard (ElmueSoft (netcult.ch/elmue)) [1d50:606f 20673392363050112]
```

- serial ports with hardware behind them (`--all` adds the empty `/dev/ttyS*` of Linux), with USB vendor / product
  id and serial number: the serial number finds an adapter again after the OS renumbered it (chips without one,
  such as the CH340, only by their USB port, `location`);
- CAN channels: on Linux every SocketCAN interface, also one that is down (with the command that brings it up), with
  the USB facts of the adapter; elsewhere the python-can interfaces that can enumerate their hardware (PCAN, Kvaser,
  Vector, IXXAT, gs_usb / candleLight on Windows, ...); slcan adapters (CANable) show up as serial ports;
- network interfaces that are up, as the broadcast address of their subnet (for `discover`); on Linux also the
  Ethernet ports that cannot be used yet (no link, no IPv4 address: a USB-Ethernet adapter without a cable), with an
  empty device string, the reason and the USB facts;
- whatever transport plugins report (`adapters=` of `register_transport`).

Nothing is opened, so a listed port says nothing about a device answering on it. A source that fails is skipped
(`umcub.adapters(errors=list)` collects why).

## Transport plugins

An adapter the operating system does not show as a serial port or network interface, and python-can does not know,
gets a plugin: a Python module with a `register()` function that registers a scheme. `open(target, node, **options)`
returns a `umcub.Connection` that carries whole payloads (`send(kind, payload)`, `recv(kind, timeout)` returning
`None` on timeout, `close()`, a `name`; the inherited `text(cmd, wait)` sends a text command as one packet):

```python
import umcub

class MyAdapter(umcub.Connection):
    def __init__(self, target, speed):
        self.name = f"myadapter:{target}"
        ...                                  # open the vendor API
    def send(self, kind, payload): ...
    def recv(self, kind, timeout): ...       # one payload, or None
    def close(self): ...

def register():
    umcub.register_transport("myadapter", lambda target, node, speed=1000000: MyAdapter(target, speed),
                             {"speed": int}, "my adapter through its vendor API",
                             adapters=lambda: [umcub.Adapter("myadapter:0", "myadapter", "MyAdapter #0")])
```

`umcub -d myadapter:0,speed=500000 upload app.signed.bin` then works like any other transport, link nodes included.
A connection that already selects the node when it opens (like CAN by its IDs) sets `selects_node = True`. Serial
style transports that frame payloads as lines set `stream = True` and must split the kinds like `Serial` does. For
a CAN adapter a python-can interface plugin is usually less work: `can:<that interface>:<channel>` then works.
`parallel=True` tells programs that several connections to one target may work at once (see below).

Plugins come from installed packages (entry point group `umcub.transports`, naming the module or its `register`) or
from a folder of `.py` files: `--plugins DIR` / `$UMCUB_PLUGINS`, in a program `umcub.load_plugins(folder)`. A plugin
runs with the rights of the program: load only folders you trust. One that fails to load is reported
(`umcub.plugins()`, a warning in the tool) and the others still load; a plugin cannot take over a scheme that is
already registered.

## Commands

```sh
umcub -d /dev/ttyUSB0 upload app.signed.bin --reset          # SMP: into the primary slot, then reset
umcub -d /dev/ttyUSB0 upload app.signed.bin --lite           # bootloaders without SMP
umcub -d /dev/ttyUSB0 upload app.signed.bin --lite --slot secondary --test --reset
umcub -d /dev/ttyUSB0 upload app.signed.bin --slot secondary --test   # SMP, UMCUB_CFG_SMP_DIRECT_UPLOAD
umcub -d /dev/ttyUSB0 mark app.signed.bin --permanent        # SMP: mark an uploaded secondary image
umcub -d /dev/ttyUSB0 list                                   # image slots (SMP image state-read)
umcub -d /dev/ttyUSB0 reset                                  # (--lite on bootloaders without SMP)
umcub -d /dev/ttyUSB0 cmd i                                  # a text command
umcub -d 192.168.1.50 verify 0 1                             # MCUboot validation of image 0, slot 1
umcub -d 192.168.1.50 hash 0 0 --file app.signed.bin         # MATCH / MISMATCH
umcub -d 192.168.1.50 read 0 0 --out dump.bin                # needs UMCUB_CFG_READBACK
umcub -d can:socketcan:can0 --node 5 upload app.signed.bin   # CAN, node 5
umcub -d /dev/ttyUSB1 discover                               # umcub link devices on an RS485 bus
umcub -d can:socketcan:can0 discover                         # CAN: the nodes 0..7 that answer, SMP or lite
umcub -d /dev/ttyUSB1 --node 5 info                          # identity (SECURE: authenticates first)
umcub -d /dev/ttyUSB1 --node 5 --admin-key admin.pem --device-key device.pem upload app.encrypted.bin
umcub -d /dev/ttyUSB1 --node 5 serve --pty-link /tmp/smp     # proxy: smpmgr --port /tmp/smp ... through the link
umcub -d /dev/ttyUSB1 sniff                                  # listen only: every link frame on the bus
```

`umcub <command> --help` lists the options. The protocol is the user's choice (`--lite` for bootloaders without
SMP), the tool does not probe for it. SMP uploads take the packet size from the bootloader
(`UMCUB_CFG_SMP_PARAMS`), otherwise 512 bytes (`mtu=` raises it on UDP and CAN). Over SMP, `--slot` needs
`UMCUB_CFG_SMP_DIRECT_UPLOAD` (the image field then names the slot, see [Recovery mode](recovery.md)) and
`--test` / `--permanent` (or `mark` later) set the image state by its hash, read from the file; MCUboot takes
one mark per upload, so `mark` does not turn a test mark into a permanent one. On a link node the board type of
the image (TLV `0xA0`) is checked against the node's before anything is sent; elsewhere the bootloader refuses a
wrong image after the transfer. After an SMP upload the device checks the image without sending it back: MCUboot's
validation (`UMCUB_CFG_INSPECT_VERIFY`), else the SHA-256 of the slot against the file (`UMCUB_CFG_INSPECT_HASH`;
not for an encrypted image in the primary slot, which is decrypted there); `upload` prints which check passed, or
"not checked" when the bootloader cannot check the image (and after a lite upload). A failed check stops before
`--test` / `--permanent` and `--reset`. The standard tools (`smpmgr`, `mcumgr`,
`dfu-util`) keep working for SMP and DFU without it.

## Images and keys (no device)

The build side lives in the same tool: CMake's `umcub_sign_image()` and post-build steps of other builds sign with it, and
CMake embeds the umcub link keys with it. These commands need neither a device nor the transport libraries
(smpclient, python-can), only imgtool's dependencies and esp-idf-kconfig, and run straight from a checkout as
`python <umcub>/tools/host/umcub ...` - no install needed.

```sh
umcub image info --board nucleo_h755zi_q --core cm7          # where to link the application
umcub image size --board build/h755-cm7/umcub_kconfig build/h755-cm7/umcub_nucleo_h755zi_q_cm7.elf   # bootloader in its region, room for applications
umcub image ld --board nucleo_h755zi_q --core cm7 -o ld/      # MEMORY blocks of the slots (INCLUDE in your .ld)
umcub image sign --board nucleo_h755zi_q --core cm7 --version 1.2.3 app.elf   # -> app.signed.bin / .hex
umcub keys c --device device.pem --admin admin.pem -o umcub_link_keys.c        # link SECURE keys (bootloader)
umcub keys pub admin.pem                                     # public key as hex (X || Y)
```

See [The application side](application.md#applications-built-without-cmake) for the signing options;
the board and its fragments (`--board`, `--core`, `--conf`) are the ones the bootloader was built with.
Outside the checkout (an installed copy of the package) `$UMCUB_ROOT` points to it.

## Library

Nothing in the library prints or exits: errors are exceptions (`UmcubError`: `NoAnswer`, `DeviceError` with the
device's `rc`, `LinkError`, `Cancelled`), progress is a callback, a `threading.Event` cancels. The calls block, so
a GUI runs them in a worker thread.

`tap=` on `connect()`, `discover()` and `open_connection()` (or `umcub.add_tap(conn, tap)`) calls `tap(TapEvent)`
for every payload sent and received and every text command and its answer: time, direction, kind, bytes (link
frames as on the wire; `umcub.link.describe_frame()` decodes them) and the connection's name, from the thread doing
the I/O. An exception in the tap does not stop the transfer; the first one is kept in `conn.tap_error`. The tool's
`--trace` prints them. `umcub.link.sniff(port, out, baud, raw, stop)` listens on a serial bus without writing and calls
`out(SniffEvent)` (time, the parsed frame or a raw line, its text) until the `stop` event is set.

Every error has a stable `code` (`link.auth_failed`, `smp.device`, ...) and its `params`; the English text is
`umcub.errors.MESSAGES[code]` filled with them. A translated program keeps its own text per code and fills in the
same params, with `str(error)` as the fallback; `smp.ERRORS` and `lite.ERRORS` give the meaning of a device's `rc`.
Codes and their params only change with the library's version.

```python
import threading
import umcub

cancel = threading.Event()
with umcub.connect("/dev/ttyUSB1", node=5) as dev:           # same strings and options as the tool
    print(dev.identity)                                       # link nodes: uid, board type, version, mode
    print(dev.text("i"))                                      # a text command
    img = umcub.Image.load("app.signed.bin")                  # version, hash, board_type
    umcub.flash(dev, img, "smp", progress=lambda done, total: print(done, total), cancel=cancel)
    # = umcub upload: board type check, upload, check ("verify" / "hash" / None returned), marks, reset
    with umcub.Smp(dev) as smp:                               # SMP: upload, images, mark, reset, verify, hash, read
        smp.upload(img.data)                                  # the steps one by one
        # direct upload: smp.upload(img.data, umcub.smp.direct_slot(0, 1)); smp.mark(img.hash)
        print(smp.images(), smp.verify(0, 0))
        smp.reset()
    # without SMP: umcub.lite.upload(dev, img.data, slot=1, test=True); umcub.lite.reset(dev)

for d in umcub.discover("/dev/ttyUSB1"):                     # umcub link devices
    print(d["addr"], d["uid"], hex(d["board_type"]))
```

Several devices at once: the connections of one CAN channel share one python-can bus (drivers such as PCAN or
Vector open a channel once), each with its node's IDs, so uploads to several CAN nodes or UDP devices can run in
parallel threads. A serial port is one half-duplex bus (RS485: no arbitration), so its devices take turns.
`umcub.exclusive_key(spec)` tells a program which is which: `None` when connections may run at once, else the key
of the medium to take turns on (`"serial:/dev/ttyUSB1"`). On CAN `discover()` asks every node of `nodes` (default
0..7, what fits the default IDs) an SMP echo, else a lite frame of no known kind (answered "not supported"); it
returns `{"addr": node, "protocol": "smp" | "lite"}` per node that answered.

A board protocol (`UMCUB_CFG_PROTO_USER`) uses the connection directly: `dev.send(umcub.KIND_USER, frame)` and
`dev.recv(umcub.KIND_USER, timeout)` (see `boards/blackpill_f411ce/proto_upload.py`).

| Module | Contents |
|---|---|
| `adapter` | `adapters()`: serial ports, CAN channels, network interfaces, plugins' adapters |
| `board_names` | the board name catalog: `load_board_names()`, `board_label()` |
| `errors` | the exceptions, their codes and English messages (`MESSAGES`) |
| `plugins` | transport plugins: installed packages and plugin folders |
| `connection` | the device strings and the transport registry; serial (SMP as NLIP lines, frames as `05 <kind> base64` lines, text), UDP, CAN ISO-TP |
| `device` | `connect()`, `discover()`, `Device` (directly, or as a link node) |
| `smp` | `Smp` (smpclient on a `Device`), the inspection group requests |
| `lite` | lite upload frames, `upload()`, `reset()` |
| `flasher` | `flash()`: the whole upload of `umcub upload` (checks, marks, reset) |
| `image` | `Image`: a signed image file - version, hash, board type TLV |
| `link` | link frames, DISCOVER, the SECURE handshake (mirrors `src/proto/link.c`), `sniff()` |
| `kconfig`, `layout` | the configuration from Kconfig (defconfig + fragments), the generated files, keys, MCUboot's rule on the slots |
| `config` | a board's generated configuration as values, slot windows |
| `linker` | GNU ld MEMORY blocks of the slots |
| `sign` | `sign()`: signing, for CMake and other builds (imgtool) |
| `keys` | umcub link keys as C arrays |
| `cli` | the `umcub` tool |
