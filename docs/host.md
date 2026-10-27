# Host tool and library

`tools/host/umcub` is the host side of umcub: a Python library for your own tools and applications, and the
`umcub` command line tool on top of it. It speaks every upload protocol of the bootloader - SMP, the lite upload
protocol, umcub link (addressing on a shared bus, SECURE sessions) and the inspection group - on every transport a
PC reaches: serial ports (UART and RS485 adapters, USB CDC), UDP and CAN (any python-can interface: SocketCAN,
PCAN, Kvaser, Vector, IXXAT, slcan / CANable, candleLight, ...).

`tools/setup.sh` installs it into `.venv` (`pip install -e tools/host`); elsewhere `pip install <umcub>/tools/host`.

## Devices

A device is one string (`-d`, or `$UMCUB_DEVICE`):

| Transport | String |
|---|---|
| serial | `/dev/ttyUSB0`, `COM3`, `serial:/dev/ttyUSB0,baud=921600` (default 115200) |
| UDP | `192.168.1.50`, `udp:192.168.1.50,port=1337` |
| CAN ISO-TP | `can:<python-can interface>:<channel>`, e.g. `can:socketcan:can0`, `can:pcan:PCAN_USBBUS1,bitrate=250000`, `can:slcan:/dev/ttyACM0,fd,data-bitrate=2000000`; also `tx=0x7C0`, `rx=0x7C8` (ID bases), `ext` (29-bit), `mtu=1024` |

`--node N` selects a device on a [umcub link](link.md) bus (serial, UDP) or, on CAN, the node's IDs (bases + N);
`--uid` selects an unassigned link device. SECURE link transports also need `--admin-key` (private) and
`--device-key` (the public half is enough).

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
wrong image after the transfer. The standard tools (`smpmgr`, `mcumgr`, `dfu-util`) keep working for SMP and
DFU without it.

## Library

Nothing in the library prints or exits: errors are exceptions (`UmcubError`: `NoAnswer`, `DeviceError` with the
device's `rc`, `LinkError`, `Cancelled`), progress is a callback, a `threading.Event` cancels. The calls block, so
a GUI runs them in a worker thread.

```python
import threading
import umcub

cancel = threading.Event()
with umcub.connect("/dev/ttyUSB1", node=5) as dev:           # same strings and options as the tool
    print(dev.identity)                                       # link nodes: uid, board type, version, mode
    print(dev.text("i"))                                      # a text command
    img = umcub.Image.load("app.signed.bin")                  # version, hash, board_type
    if dev.identity:
        img.check_board(dev.identity["board_type"])
    with umcub.Smp(dev) as smp:                               # SMP: upload, images, mark, reset, verify, hash, read
        smp.upload(img.data, progress=lambda done, total: print(done, total), cancel=cancel)
        # direct upload: smp.upload(img.data, umcub.smp.direct_slot(0, 1)); smp.mark(img.hash)
        print(smp.images(), smp.verify(0, 0))
        smp.reset()
    # without SMP: umcub.lite.upload(dev, img.data, slot=1, test=True); umcub.lite.reset(dev)

for d in umcub.discover("/dev/ttyUSB1"):                     # umcub link devices
    print(d["addr"], d["uid"], hex(d["board_type"]))
```

A board protocol (`UMCUB_CFG_PROTO_USER`) uses the connection directly: `dev.send(umcub.KIND_USER, frame)` and
`dev.recv(umcub.KIND_USER, timeout)` (see `boards/blackpill_f411ce/proto_upload.py`).

| Module | Contents |
|---|---|
| `connection` | the device strings; serial (SMP as NLIP lines, frames as `05 <kind> base64` lines, text), UDP, CAN ISO-TP |
| `device` | `connect()`, `discover()`, `Device` (directly, or as a link node) |
| `smp` | `Smp` (smpclient on a `Device`), the inspection group requests |
| `lite` | lite upload frames, `upload()`, `reset()` |
| `image` | `Image`: a signed image file - version, hash, board type TLV |
| `link` | link frames, DISCOVER, the SECURE handshake (mirrors `src/proto/link.c`), `sniff()` |
| `cli` | the `umcub` tool |
