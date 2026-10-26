# Recovery mode

How images get into the bootloader over SMP, what it understands besides SMP (text commands, the lite upload, a
board protocol), and checking what was written.

## Updating over SMP

SMP (mcumgr) works the same over every SMP transport; only the connection differs:

```sh
smpmgr --port /dev/ttyACM0 image state-read                 # or: mcumgr --conntype serial ...
smpmgr --port /dev/ttyACM0 image upload build/h755-cm7/examples/h755_cm7_app/h755_cm7_app.signed.bin
smpmgr --port /dev/ttyACM0 image upload --slot 1 build/h755-cm7/examples/h755_cm4_app/h755_cm4_app.signed.bin
smpmgr --port /dev/ttyACM0 os reset
mcumgr --conntype udp --connstring=<ip>:1337 image upload build/h755-cm7/examples/h755_cm7_app/h755_cm7_app.signed.bin
smpmgr --ip <ip> image upload build/h755-cm7/examples/h755_cm7_app/h755_cm7_app.signed.bin              # UDP (default --mtu 1500)
dfu-util -a 0 -D build/h755-cm7/examples/h755_cm7_app/h755_cm7_app.signed.bin -R    # DFU: alt N = image N
tools/host/smp_can.py --channel can0 upload build/h755-cm7/examples/h755_cm7_app/h755_cm7_app.signed.bin
```

SMP serial recovery writes straight into the **primary** slot (this is how MCUboot works); the image number is the
`image` field (`smpmgr --slot`, `mcumgr -n`; 1 = CM4). With `UMCUB_CFG_SMP_DIRECT_UPLOAD 1` (always in direct-xip)
the field names a slot instead: 0/1 image 0 primary, 2 image 0 secondary, 3 image 1 primary, 4 image 1 secondary;
`smpmgr image state-write <hash>` then marks an image in the secondary slot for a test boot (`--confirm <hash>`:
for good), and the next reset installs it; MCUboot's slot info command (`image slot-info`) reports
`upload_image_id`, the `image` value to upload each slot with. These commands, and the flags and image hash in
`image state-read`, come only with direct upload (about 2.4 K); without it `state-read` lists the versions. The bootloader answers MCUMGR_PARAMETERS
(`UMCUB_CFG_SMP_PARAMS`), so smpmgr sizes serial packets to `UMCUB_CFG_SMP_MTU` by itself (default 1472 with
Ethernet - one full UDP datagram, what smpmgr sends over UDP - otherwise 1024); other hosts: `--line-buffers`
lines of 128 bytes within it. DFU and `umcub_slot_*` write into the **secondary** slot and mark the image for a test boot.

## Text commands

Besides SMP the bootloader understands simple text commands defined in `umcub_config.h` — handy from a terminal or
a few lines of host code:

```c
#define UMCUB_CFG_CMD_ENABLE     1
#define UMCUB_CFG_CMD_IMMEDIATE  1      /* run as soon as the input matches, no Enter needed */
#define UMCUB_CFG_CMD_TABLE      UMCUB_CMD("a",     UMCUB_CMD_BOOT_APP)  \
                                 UMCUB_CMD("b",     UMCUB_CMD_STAY)      \
                                 UMCUB_CMD("r",     UMCUB_CMD_RESET)     \
                                 UMCUB_CMD("i",     UMCUB_CMD_INFO)      \
                                 UMCUB_CMD("hello", UMCUB_CMD_USER(1))   \
                                 UMCUB_DEFAULT_CMD_INSPECT      /* verify, hash (+ read) without SMP */
```

`UMCUB_DEFAULT_CMD_INSPECT` adds `verify`, `hash` (and `read` with `UMCUB_CFG_READBACK`) only when SMP is off: with
SMP, hosts check slots through the SMP inspect group (`tools/host/umcub_inspect.py`), about 0.9 K less.
`UMCUB_CMD_INSPECT_DEFAULTS` adds them in any case.

| Action | What it does |
|---|---|
| `UMCUB_CMD_BOOT_APP` | leave the bootloader and start the application (from recovery through a reset; entry pin and wait window are skipped) |
| `UMCUB_CMD_STAY` | stay in the bootloader (meaningful during `UMCUB_CFG_ENTRY_WAIT_MS`) |
| `UMCUB_CMD_RESET` | reset |
| `UMCUB_CMD_INFO` | bootloader version, mode, versions and state of the images in all slots |
| `UMCUB_CMD_VERIFY` / `HASH` / `READ` | slot inspection, see below |
| `UMCUB_CMD_USER(n)` | calls `bool umcub_cmd_user(unsigned n, const char *text, umcub_cmd_reply_t reply)`; define it in `boards/<board>/umcub_board.c`, which is added to the build automatically |

- Commands are accepted in recovery mode and, over every enabled transport, during the wait after reset
  (`UMCUB_CFG_ENTRY_WAIT_MS`, e.g. 300 ms: "press `b` right after reset to stay in the bootloader"; how long each
  transport needs: [Getting into the bootloader](../README.md#getting-into-the-bootloader)).
- UART and USB CDC: typed text. A command is recognised only at the start of a line; a command that is the beginning
  of another one (`r` / `read`) waits for Enter; an unknown line is answered with `? text` after Enter.
- UDP and CAN: a packet that is not an SMP frame is a command; the reply comes back as one packet
  (`echo -n i | nc -u <ip> 1337`).
- SMP (mcumgr / smpmgr / dfu-util) keeps working in parallel.
- `ok ...` / `? ...` replies can be turned off with `UMCUB_CFG_CMD_REPLY 0`.

## Recovery without SMP: lite upload and board protocols

SMP (MCUboot serial recovery) is what `mcumgr` / `smpmgr` speak, and it is the biggest part of a bootloader with a
transport: `boot_serial`, zcbor and the SMP framing cost about 6 K (see "Bootloader size"). `UMCUB_CFG_SMP_ENABLE 0`
leaves them out; the transports stay, and images come through the protocols below. With the lite upload protocol a
UART bootloader is about 3 K smaller (F103: 16.4 K instead of 19.4 K, H7: 22.4 K instead of 25.6 K):

- **the lite upload protocol** (`UMCUB_CFG_LITE_UPLOAD`, on by default without SMP; about 2.5 K with the slot writer).
  Begin / data / end frames with a CRC, every frame answered, lost frames repeated; the image goes through the same
  slot writer as from the application (header check, test or permanent mark for the secondary slot, in-place
  decryption and validation of encrypted images in the primary slot). Frames are described in
  [`src/proto/include/umcub_lite.h`](../src/proto/include/umcub_lite.h); the host side is `tools/host/umcub_lite.py`:

  ```sh
  tools/host/umcub_lite.py --port /dev/ttyUSB0 upload app.signed.bin            # primary slot, like SMP recovery
  tools/host/umcub_lite.py --port /dev/ttyUSB0 upload app.signed.bin --slot secondary --test --reset
  tools/host/umcub_lite.py --udp 192.168.1.50 upload app.signed.bin
  tools/host/umcub_lite.py --can --channel can0 upload app.signed.bin
  tools/host/umcub_lite.py --port /dev/ttyUSB0 cmd "verify 0 0"                  # text commands work as before
  ```

  On stream transports a frame is a line `0x05 0x0C <base64> \n` in the line buffer (`UMCUB_CFG_SMP_MTU` bytes),
  packet transports carry one frame per packet. The default `--chunk` (data bytes per frame: 373 on serial, 504
  otherwise) fits the smallest allowed `UMCUB_CFG_SMP_MTU` (512); with a larger MTU, `--chunk` can go up to
  3/4 of the MTU - 8 on serial and the MTU - 8 otherwise.
- **the board's own protocol** (`UMCUB_CFG_PROTO_USER`, also next to SMP): every frame that is neither SMP, lite
  nor a text command goes to `umcub_proto_user(t, data, len)` in `boards/<b>/umcub_board.c` - a packet of a
  packet transport, a umcub link DATA payload, or a line `0x05 0x0D <base64> \n` on a stream. Both are declared
  in `umcub_proto.h` (`#include "umcub_proto.h"`). Answer with `umcub_proto_reply()`; write images with `umcub_slot_*` (`umcub.h`), which is part of the bootloader then.
  Experimental: exercised by the host test with the fixture in `tests/boards/custom_drivers` only, no board uses it yet.
- USB DFU and updates from the application, as with SMP.

Text commands (`verify`, `hash`, `read`, `i`, ...), the umcub link (ADDRESSED and SECURE: DATA payloads are lite
frames then) and encrypted images work without SMP; the SMP inspection group and `mcumgr` / `smpmgr` do not.

## Checking what was written (verify / hash / read)

The host can confirm that flash holds exactly what it sent, over any transport (UART, USB CDC, UDP, CAN):

| Command | What it does | Option |
|---|---|---|
| `verify <image> <slot>` | full MCUboot validation from flash: SHA-256 + signature → `ok valid` / `bad ...` | `UMCUB_CFG_INSPECT_VERIFY` (on) |
| `hash <image> <slot> [<off> <len>]` | SHA-256 of a slot range (default: the whole stored image, i.e. the `.signed.bin` that was sent) | `UMCUB_CFG_INSPECT_HASH` (on) |
| `read <image> <slot> <off> <len>` | hex dump, plus SMP read and `dfu-util -U` | `UMCUB_CFG_READBACK` (**off**: exposes the firmware) |

Slot 0 is primary, 1 is secondary. Addressing is only by (image, slot, offset): the bootloader region is never
reachable, whatever the configuration.

The same operations are available in SMP group `UMCUB_CFG_SMP_INSPECT_GROUP` (default 100: id 0 verify, 1 hash,
2 read) for your own host code. Ready-made tool:

```sh
tools/host/umcub_inspect.py --port /dev/ttyACM1 verify 0 1
tools/host/umcub_inspect.py --port /dev/ttyACM1 hash 0 1 --file app.signed.bin     # MATCH / MISMATCH
tools/host/umcub_inspect.py --udp 192.168.1.50 read 0 0 --out dump.bin              # needs UMCUB_CFG_READBACK
tools/host/umcub_inspect.py --can --channel can0 --addr 5 verify 0 0                # CAN ISO-TP
```

Note: the `hash` shown by the standard `smpmgr image state-read` (with direct upload) is read from the TLV inside the
image and is **not** recomputed from flash — it identifies the image for `state-write`. Use `verify` / `hash` above
to check what is really stored.
