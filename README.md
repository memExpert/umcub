# umcub — a portable MCUboot-based bootloader for STM32

umcub (**U**niversal **MCU**boot **B**ootloader) is a bootloader for STM32 microcontrollers built on
[MCUboot](https://github.com/mcu-tools/mcuboot):

- **Not tied to a series.** The boot logic, transports and application library use one hardware API
  (`port/include/umcub_port*.h`); everything series-specific lives in `port/stm32<fam>/`. Ports: STM32H7
  (reference board NUCLEO-H755ZI-Q, both cores, all transports) and STM32F1 (Blue Pill: UART, USB CDC/DFU).
- **One configuration file.** `boards/<board>/umcub_config.h` is the single source of truth. CMake runs it through
  the preprocessor to decide what to build: a disabled transport (e.g. USB together with tinyUSB) is not compiled at all.
- **Transports** (each enabled separately), all speaking SMP/mcumgr:
  - UART — SMP serial (`smpmgr`, `mcumgr --conntype serial`);
  - USB (tinyUSB) — CDC-ACM with SMP and/or DFU 1.1 (`dfu-util`);
  - CAN / CAN-FD — SMP over ISO-TP (`tools/smp_can.py`);
  - Ethernet — SMP over UDP on a minimal IPv4 stack with DHCP (`mcumgr --conntype udp`);
  - writing an image from the application itself (`umcub_slot_*`) into a selectable slot.
- **MCUboot upgrade modes**: overwrite, swap-scratch, swap-move, swap-offset, direct-xip, direct-xip with revert.
- **Signing**: ECDSA-P256 (tinycrypt), keys and signing with `imgtool`.
- **Text commands** next to SMP, defined in the config (e.g. press `a` to start the application).
- **Slot inspection**: verify / hash, and optional readback, over every transport.
- **Application library** `umcub::app`: bootloader version, boot information, jump to the bootloader,
  confirm/revert, writing an image into a slot.

## Quick start (NUCLEO-H755ZI-Q)

```sh
git submodule update --init
tools/setup.sh                       # .venv: imgtool, smpmgr, python-can, ...

cmake --preset h755-cm7              # CM7 bootloader (one bootloader for both cores)
cmake --build --preset h755-cm7      # build/h755-cm7/umcub_nucleo_h755zi_q_cm7.{elf,hex,bin}; h755-cm7-debug: -Og

cmake -S examples/h755_cm7_app -B build/ex-cm7 -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build/ex-cm7
cmake -S examples/h755_cm4_app -B build/ex-cm4 -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build/ex-cm4

tools/h755_option_bytes.sh single    # the CM4 starts from the bootloader vector table and waits
tools/flash.sh build/h755-cm7/umcub_nucleo_h755zi_q_cm7.hex \
               build/ex-cm7/h755_cm7_app.signed.hex build/ex-cm4/h755_cm4_app.signed.hex
```

On Windows (no `setup.sh`): `git submodule update --init`, `py -m venv .venv`,
`.venv\Scripts\pip install -r tools\requirements.txt -e third_party\mcuboot\scripts`; CMake finds imgtool in
`.venv\Scripts`. The IDE workflow is described in [Using umcub from an IDE](#using-umcub-from-an-ide-stm32cubeide-keil-mdk).

The bootloader logs to the ST-LINK virtual COM port (USART3, 115200). It enters recovery mode when B1 is held during
reset, when the application asks for it (key `b` in the example), or when there is no valid image.

```sh
smpmgr --port /dev/ttyACM0 image state-read                 # or: mcumgr --conntype serial ...
smpmgr --port /dev/ttyACM0 --line-buffers 8 image upload build/ex-cm7/h755_cm7_app.signed.bin
smpmgr --port /dev/ttyACM0 --line-buffers 8 image upload --slot 1 build/ex-cm4/h755_cm4_app.signed.bin
smpmgr --port /dev/ttyACM0 os reset
mcumgr --conntype udp --connstring=<ip>:1337 image upload build/ex-cm7/h755_cm7_app.signed.bin
dfu-util -a 0 -D build/ex-cm7/h755_cm7_app.signed.bin -R    # DFU: alt N = image N
tools/smp_can.py --channel can0 upload build/ex-cm7/h755_cm7_app.signed.bin
```

SMP serial recovery writes straight into the **primary** slot (this is how MCUboot works); the image number is the
`image` field (`smpmgr --slot`, `mcumgr -n`; 1 = CM4). `--line-buffers 8` matches `UMCUB_CFG_SMP_MTU` = 1024
(8 lines of 128 bytes). DFU and `umcub_slot_*` write into the **secondary** slot and mark the image for a test boot.

## Quick start (Blue Pill, STM32F103C8T6)

Minimal build in 64 KiB: SMP over USART1 (PA9 TX, PA10 RX, the pins of the ROM bootloader), overwrite mode,
bootloader 32 KiB + two 16 KiB slots ([`boards/bluepill_f103c8/umcub_config.h`](boards/bluepill_f103c8/umcub_config.h)).

```sh
cmake --preset bluepill && cmake --build --preset bluepill
cmake -S examples/bluepill_app -B build/ex-bp -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build/ex-bp
STM32_Programmer_CLI -c port=SWD -d build/bluepill/umcub_bluepill_f103c8_cm3.hex -v
STM32_Programmer_CLI -c port=SWD -d build/ex-bp/bluepill_app.signed.hex -v -rst

# USB-UART adapter on PA9/PA10, 115200: log, text commands and SMP on the same port
smpmgr --port /dev/ttyUSB0 --line-buffers 4 image upload build/ex-bp/bluepill_app.signed.bin   # in recovery
tools/app_upload.py /dev/ttyUSB0 build/ex-bp/bluepill_app.signed.bin   # from the running application ('u')
```

There is no user button. Recovery mode starts on a request from the application (key `b` in the example), when there
is no valid image, or when `b` arrives within 300 ms after reset (`UMCUB_CFG_ENTRY_WAIT_MS`).
`--line-buffers 4` matches `UMCUB_CFG_SMP_MTU` = 512 (20 KiB SRAM).

USB (CDC with SMP and DFU, micro-USB connector) needs more room: preset `bluepill-usb` (overlay
`tools/config/bluepill_usb.h`: bootloader 44 KiB, two 10 KiB slots; the example in the same tree uses the same
layout):

```sh
cmake --preset bluepill-usb && cmake --build --preset bluepill-usb
smpmgr --port /dev/ttyACM0 --line-buffers 4 image upload build/bluepill-usb/examples/bluepill_app/bluepill_app.signed.bin
dfu-util -a 0 -D build/bluepill-usb/examples/bluepill_app/bluepill_app.signed.bin -R
```

The F1 has no switchable D+ pull-up and the Blue Pill ties D+ to 3.3 V, so the board looks attached whenever it is
powered, and a host would try to enumerate a device that does not answer. The STM32F1 port therefore drives D+ low
(detached) from reset on, and whenever its USB is not running. That includes the application: an application that
uses USB itself must make PA12 an input before enabling its USB peripheral. USB is not started for the 300 ms entry
window, only in recovery mode. While a debugger holds the MCU in reset (`mode=UR`) D+ floats high again: flash with
`mode=HOTPLUG`, or with the USB cable unplugged.

## Build trees and IDEs (CMake presets)

Every preset in `CMakePresets.json` builds the bootloader target `umcub_<board>_<core>` and, with
`UMCUB_BUILD_EXAMPLES=ON` (set in the presets), the example applications of that board in the same tree:
`build/<preset>/examples/<app>/<app>.{elf,map,signed.bin,signed.hex}`. An IDE using CMake presets (VS Code with
CMake Tools, CLion, ...) therefore lists the bootloader and the applications as targets, and every `.elf` has its
`.map` next to it for memory analysis tools. The examples remain standalone projects (`cmake -S examples/<app>`).
In your own build that adds `lib/umcub_app` for two cores, link each application with `${UMCUB_APP_LIB}` (one target
per core); with a single application `umcub::app` is enough.

## Configuration

All options are documented in [`config/umcub_config_template.h`](config/umcub_config_template.h); defaults are in
`config/umcub_config_defaults.h`. Example: [`boards/nucleo_h755zi_q/umcub_config.h`](boards/nucleo_h755zi_q/umcub_config.h).

| Group | Main options |
|---|---|
| Target | `UMCUB_CFG_MCU`, `UMCUB_CFG_DUALCORE_MODE` (`NONE` / `SINGLE_BOOT` / `PER_CORE`) |
| Clocks / power | `UMCUB_CFG_CLOCK_SOURCE`, `UMCUB_CFG_HSE_HZ`, `UMCUB_CFG_HSE_BYPASS`, `UMCUB_CFG_PWR_SUPPLY` |
| Flash layout | `UMCUB_CFG_BOOT_*`, `UMCUB_CFG_IMG{0,1}_{PRIMARY,SECONDARY}_{ADDR,SIZE}`, `UMCUB_CFG_SCRATCH_*`, `UMCUB_CFG_IMAGE_HEADER_SIZE` |
| MCUboot | `UMCUB_CFG_UPGRADE_MODE`, `UMCUB_CFG_SIGNATURE`, `UMCUB_CFG_DOWNGRADE_PREVENTION`, `UMCUB_CFG_VALIDATE_PRIMARY` |
| Slot inspection | `UMCUB_CFG_INSPECT_VERIFY`, `UMCUB_CFG_INSPECT_HASH`, `UMCUB_CFG_READBACK`, `UMCUB_CFG_SMP_INSPECT_GROUP` |
| Text commands | `UMCUB_CFG_CMD_ENABLE`, `UMCUB_CFG_CMD_IMMEDIATE`, `UMCUB_CFG_CMD_REPLY`, `UMCUB_CFG_CMD_TABLE` |
| Bootloader entry | `UMCUB_CFG_ENTRY_ON_REQUEST`, `..._ON_NO_IMAGE`, `..._GPIO(_PIN/_ACTIVE/_PULL)`, `..._WAIT_MS`, `UMCUB_CFG_RECOVERY_TIMEOUT_MS` |
| Transports | `UMCUB_CFG_TRANSPORT_{UART,USB_CDC,USB_DFU,CAN,ETH,USER}` plus their parameters, `UMCUB_CFG_{CAN,ETH}_DRIVER` |
| Misc | `UMCUB_CFG_LOG_LEVEL`, `UMCUB_CFG_WATCHDOG_MS`, `UMCUB_CFG_SHARED_RAM_ADDR`, `UMCUB_CFG_APP_WRITE_SLOT` |

The configuration can be extended without editing the board file through overlay headers:
`-DUMCUB_CONFIG_PRE=<file>` is included before the board config (for `#ifndef`-guarded options),
`-DUMCUB_CONFIG_POST=<file>` after it (`#undef`/`#define`). Ready-made variants are in `tools/config/`.

Configuration errors (overlapping slots, slots not on sector boundaries, missing scratch area, conflicting modes) are
caught at compile time (`config/umcub_config_check.h`).

### Dual-core H745/H755/H747/H757

- **`UMCUB_DUALCORE_SINGLE_BOOT`** (board default): one bootloader on the CM7 manages two MCUboot images — image 0 is
  the CM7 application (bank 1), image 1 the CM4 application (bank 2). Option bytes:
  `BOOT_CM4_ADD0 = BOOT_CM7_ADD0 = 0x0800`. Both cores start from the bootloader vector table; the CM4 parks in WFE
  until the CM7 has validated its image, put its vector table address into SRAM4 and released an HSEM. If the CM4
  image is missing or broken the CM7 still boots and the CM4 stays parked.
- **`UMCUB_DUALCORE_PER_CORE`**: two independent bootloaders (`--preset h755-per-core-cm7` and `h755-per-core-cm4`),
  the CM4 one in bank 2 (`BOOT_CM4_ADD0 = 0x0810`). Only the CM7 configures power and clocks; the two cores must not
  share transports.

STM32CubeProgrammer 2.23 does not expose `BOOT_CM4_ADD0`/`BCM4` for the H755, so `tools/h755_option_bytes.sh` writes
them through a small RAM helper run by GDB (`tools/h7_ob/`).

## Bootloader size

Flash used by the bootloader (`.text` + `.data`, arm-none-eabi-gcc 15.2, Release `-Os`, swap-scratch,
ECDSA-P256), and what every transport and feature adds. Regenerate with `tools/size_table.py`.

- **base**: MCUboot with signature check, upgrade/revert, the jump to the application, handoff and info block.
  No transport, no log.
- **all on**: every transport and feature the port supports (the `nucleo_h755zi_q` default with all interfaces).
- **UART** is counted on top of base. It includes the SMP core that every SMP transport needs (`boot_serial`,
  zcbor, the multiplexer), so the first SMP transport always costs about 10 K.
- The other columns are counted on top of base + UART.
- **DFU only** is USB DFU as the only transport, on top of base. Without an SMP transport, `boot_serial` is not
  compiled at all.

<!-- size-table:begin -->
| MCU | base | all on | UART (+SMP) | USB CDC | USB DFU | USB CDC+DFU | CAN | CAN FD | Ethernet (+DHCP) | DFU only |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| STM32H755 CM7 (2 images, SINGLE_BOOT) | 19.5 K | 56.7 K | +10.5 K | +11.2 K | +10.6 K | +13.2 K | +2.3 K | +2.3 K | +4.1 K | +10.9 K |
| STM32H755 CM4 (PER_CORE) | 17.0 K | 53.8 K | +10.3 K | +11.2 K | +10.6 K | +13.2 K | +2.3 K | +2.3 K | +4.0 K | +10.9 K |
| STM32H743 / H753 (single core) | 17.7 K | 54.5 K | +10.3 K | +11.2 K | +10.6 K | +13.2 K | +2.3 K | +2.3 K | +4.1 K | +10.9 K |
| STM32F103 (Blue Pill, overwrite) | 13.3 K | 41.7 K | +10.7 K | +9.8 K | +9.1 K | +11.7 K | — | — | — | +9.7 K |

| MCU | log (level 3) | text commands | verify + hash | readback | umcub link (addressed) | umcub link SECURE | + link encryption | encrypted images |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| STM32H755 CM7 (2 images, SINGLE_BOOT) | +2.8 K | +2.9 K | +1.0 K | +0.2 K | +1.6 K | +4.7 K | +1.6 K | +6.8 K |
| STM32H755 CM4 (PER_CORE) | +2.6 K | +2.9 K | +1.0 K | +0.2 K | +1.6 K | +4.7 K | +1.6 K | +6.2 K |
| STM32H743 / H753 (single core) | +2.6 K | +2.8 K | +1.0 K | +0.2 K | +1.6 K | +4.7 K | +1.6 K | +6.4 K |
| STM32F103 (Blue Pill, overwrite) | +2.2 K | +2.7 K | +1.0 K | +0.2 K | +1.6 K | +4.7 K | +1.6 K | +6.1 K |
<!-- size-table:end -->

Notes:
- The H755 CM4 row is the CM4 bootloader of `PER_CORE` mode (Cortex-M4 code). On the board it uses CAN only; the
  other columns are measured with borrowed pins.
- The H7 bootloader gets one 128 KiB sector, so even "all on" uses less than half of it.
- STM32F103: UART and USB (CDC, DFU) are ported, no CAN driver yet (board drivers work), no Ethernet on this part.
  The row is measured in the 44 KiB region of the USB layout. The Blue Pill default
  (UART, log, text commands, verify + hash) is 28.3 K of its 32 K region; without log and commands about 24 K.
- USB is mostly tinyUSB. Ethernet is the own IPv4/ARP/ICMP/UDP/DHCP stack plus the MAC driver. CAN FD only changes
  configuration, not code size.
- Base + the columns adds up to "all on" within a few hundred bytes. CDC and DFU together cost less than separately:
  they share the USB core.
- Debug builds (`-Og`) are about 18 % larger.

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
                                 UMCUB_CMD_INSPECT_DEFAULTS     /* verify, hash (+ read) */
```

| Action | What it does |
|---|---|
| `UMCUB_CMD_BOOT_APP` | leave the bootloader and start the application (from recovery through a reset; entry pin and wait window are skipped) |
| `UMCUB_CMD_STAY` | stay in the bootloader (meaningful during `UMCUB_CFG_ENTRY_WAIT_MS`) |
| `UMCUB_CMD_RESET` | reset |
| `UMCUB_CMD_INFO` | bootloader version, mode, versions and state of the images in all slots |
| `UMCUB_CMD_VERIFY` / `HASH` / `READ` | slot inspection, see below |
| `UMCUB_CMD_USER(n)` | calls `bool umcub_cmd_user(unsigned n, const char *text, umcub_cmd_reply_t reply)`; define it in `boards/<board>/umcub_board.c`, which is added to the build automatically |

- Commands are accepted in recovery mode and, over every transport except USB, during the `UMCUB_CFG_ENTRY_WAIT_MS` window (e.g. 300 ms: "press `b`
  right after reset to stay in the bootloader").
- UART and USB CDC: typed text. A command is recognised only at the start of a line; a command that is the beginning
  of another one (`r` / `read`) waits for Enter; an unknown line is answered with `? text` after Enter.
- UDP and CAN: a packet that is not an SMP frame is a command; the reply comes back as one packet
  (`echo -n i | nc -u <ip> 1337`).
- SMP (mcumgr / smpmgr / dfu-util) keeps working in parallel.
- `ok ...` / `? ...` replies can be turned off with `UMCUB_CFG_CMD_REPLY 0`.

## Board identity: board type and node address

**Board type.** `UMCUB_CFG_BOARD_TYPE` (u32 product id) and `UMCUB_CFG_BOARD_REV` (hardware revision) are reported
by the text command `i` and to the application (`umcub_boot_info()->board_type`, `->board_rev`).

With a board type other than 0, every image must carry the same value in a signed TLV (tag `0xA0`, u32 little
endian). `umcub_sign_image()` and `tools/umcub_image.py sign` add it automatically from the board configuration.
MCUboot rejects an image with another board type, or without one, before installing it. This holds even when the
image is signed with the right key, so firmware of another product cannot be booted:

```
E: image 0 slot 0 is for board type 0x46103002, this is 0x46103001
E: Image in the primary slot is not valid!
```

`verify` applies the same rule. Images signed before a board type was set lack the TLV and have to be re-signed.

**Node address.** The address of a device on a shared bus (RS485, CAN, ...) comes from the application:
`umcub_set_node_address(addr)` stores it in the handoff RAM, where it survives resets but not power cycles, so
call it on every start. A board can also supply it with `bool umcub_board_node_address(uint16_t *addr)` in
`umcub_board.c` (DIP switches, EEPROM). 0 means unassigned. The bootloader reports the address it uses
(`i`, `umcub_boot_info()->node_addr`).

## Shared buses: umcub link (addressing, SECURE mode)

SMP over a serial line is point to point: on a shared RS485 bus every bootloader would answer at once, and anybody
on the bus could drive them. The umcub link puts an envelope around SMP and text commands, per transport:

| `UMCUB_CFG_<UART\|USB_CDC\|CAN\|ETH\|USER>_LINK` | What the transport accepts |
|---|---|
| `UMCUB_LINK_PLAIN` (default) | SMP and text as before; standard `mcumgr` / `smpmgr` |
| `UMCUB_LINK_ADDRESSED` | only umcub link frames: a device answers frames for its node address (or, unassigned, for its UID after a `HELLO` with that UID), discovery with random back-off |
| `UMCUB_LINK_SECURE` | addressed, and nothing is executed before the host has authenticated; every frame of the session carries a MAC |

Frames: magic, version, type, flags, destination, source, sequence number, length, payload, optional 16-byte tag.
Packet transports (CAN ISO-TP, UDP, board packet transports) carry them as they are, stream transports (UART, USB
CDC) as a line `0x05 0x0B <base64> \n`. The format and the handshake are described in
[`transport/include/umcub_link.h`](transport/include/umcub_link.h). RS485 needs the driver-enable pin
(`UMCUB_CFG_UART_DE_PIN`; hardware DE on the H7, software on the F1) and no log on that UART.

**SECURE mode.** The host answers a fresh 32-byte challenge with an ephemeral EC P-256 key, its own nonce and an
ECDSA signature made with the **admin key**. The bootloader checks the signature with the embedded public half,
derives the session keys from ECDH with its **device key** (HKDF-SHA256) and proves in `AUTH_OK` that it holds that
key. After that every frame has an HMAC-SHA256 tag (16 bytes) and a strictly increasing sequence number, so a
recorded frame or `AUTH` cannot be replayed and a frame cannot be changed. `UMCUB_CFG_LINK_ENCRYPT` also encrypts
the payloads (AES-128-CTR), so a sniffer does not see the firmware either. A session belongs to one host on one
transport and ends with `CLOSE` or after `UMCUB_CFG_LINK_SESSION_MS` without a valid frame.

Keys are files, like the signing key:

```sh
imgtool keygen -t ecdsa-p256 -k keys/device.pem     # stays in the bootloader (private)
imgtool keygen -t ecdsa-p256 -k keys/admin.pem      # stays on the hosts allowed to update
cmake ... -DUMCUB_DEVICE_KEY=$PWD/keys/device.pem -DUMCUB_HOST_KEY=$PWD/keys/admin.pem
```

CMake embeds them with `tools/umcub_keys.py` (`c`: device private key and admin public key for the bootloader;
`host-c`: the host side for C tools and tests); the device key for [encrypted images](#encrypted-images) with
`imgtool getpriv`. `tools/keys/dev-device-p256.pem` and `dev-admin-p256.pem` are
development keys from the repository, the build warns about them.

Limits:
- The device key is the same in every device of a product. With flash readout protection off it can be read out
  with a debugger, so a SECURE transport stays closed (`HELLO` gets an `ANNOUNCE` flagged closed) while RDP is at
  level 0, unless `UMCUB_CFG_LINK_REQUIRE_RDP` is 0 (development only).
- USB DFU cannot authenticate the host: together with a SECURE transport it is refused at build time unless
  `UMCUB_CFG_USB_DFU_ALLOW_UNAUTH` is set.
- The F1 has no true RNG: nonces come from ADC noise and clock jitter, hashed with the UID, a boot counter kept
  in RAM over resets and (SECURE) keyed with the device key. Randomness is fail-closed: if the entropy source fails
  or its output looks stuck, `HELLO` gets no challenge.
- A failed `AUTH` blocks further attempts for one second, a challenge is valid for ten seconds. An `AUTH` costs two
  ECC operations (about 1.3 s on a 72 MHz Cortex-M3), so with SECURE `UMCUB_CFG_WATCHDOG_MS` must be at least 2000
  and `UMCUB_CFG_VALIDATE_PRIMARY` must stay on (both checked at build time).
- The policy is per transport: a PLAIN transport next to a SECURE one is a way around it, the build warns unless
  `UMCUB_CFG_LINK_MIXED_OK` is set. A UART with a link never carries the log.
- A bus can always be jammed; the link protects authenticity, integrity, replay and (with encryption)
  confidentiality.
- Firmware confidentiality needs two things: `UMCUB_CFG_LINK_ENCRYPT` for the session and
  [encrypted images](#encrypted-images) for what is stored and sent outside it.

**Host side: `tools/umcub_link.py`.** Standard SMP clients do not speak the link. The tool finds the devices, selects
one and, as a proxy, serves `mcumgr` / `smpmgr` unchanged:

```sh
umcub_link.py --port /dev/ttyUSB0 discover                     # all devices on the bus (random back-off rounds)
umcub_link.py --port /dev/ttyUSB0 --addr 5 info                # identity; SECURE: authenticates first
umcub_link.py --port /dev/ttyUSB0 --uid 53ff7206... cmd i      # unassigned node, selected by UID
umcub_link.py --port /dev/ttyUSB0 --addr 5 --admin-key admin.pem --device-key device.pem serve --pty-link /tmp/smp
smpmgr --port /tmp/smp --line-buffers 4 image upload app.encrypted.bin    # through the proxy
umcub_link.py --udp 192.168.1.50 --addr 5 serve --udp-listen 127.0.0.1:1337   # mcumgr --conntype udp
```

SECURE transports need `--admin-key` (private) and `--device-key` (the public half is enough). Your own host
software can do the same: the frames and the handshake are in `umcub_link.h`, `tools/umcub_link.py` is a complete
reference (Python `cryptography`).

## Encrypted images

`UMCUB_CFG_ENCRYPT_IMAGES` makes MCUboot accept images encrypted for the **device key** (ECIES-P256, AES-128-CTR;
the same `UMCUB_DEVICE_KEY` as the SECURE link). An image file then does not show the firmware, wherever it is
stored or sent. The signature still covers the plain image, so encryption adds confidentiality, not trust.

`umcub_sign_image()` and `tools/umcub_image.py sign` then write two files:

| File | Use |
|---|---|
| `<app>.signed.bin` / `.hex` | plain: programmers and debuggers write it straight into the primary slot |
| `<app>.encrypted.bin` | updates: SMP, USB DFU, `umcub_slot_*` from the application; distribution |

(`ENCRYPT_KEY` / `--encrypt-key` select the device key; the default is the development key with a warning.)

How an encrypted image is installed:
- **through the secondary slot** (DFU, application, SMP with `UMCUB_CFG_SMP_DIRECT_UPLOAD`): MCUboot decrypts it
  while it copies or swaps it into the primary slot. In the swap modes the old image is encrypted again on its way
  into the secondary slot, so a revert works and the plain code is only ever in the primary slot;
- **SMP upload into the primary slot** (recovery, the default): after the last chunk the bootloader decrypts the
  image in place, sector by sector through one sector of RAM (`UMCUB_CFG_ENC_INPLACE`; 128 KiB on the H7). Like any
  upload into the primary slot this is not power-fail safe: an interrupted decryption leaves an invalid image and
  the bootloader in recovery, upload again. The H7 CM4 bootloader has no room for the buffer (`UMCUB_CFG_ENC_INPLACE`
  is 0 there): use the secondary slot.

Installed images are plain text in flash, so `read` / SMP read / DFU upload (`UMCUB_CFG_READBACK`) answer only
inside an encrypted umcub link session (`UMCUB_LINK_SECURE` + `UMCUB_CFG_LINK_ENCRYPT`) and refuse everywhere else
(`? readback only in an encrypted session`, SMP rc 11). `verify` checks an encrypted image in the secondary slot by
decrypting it on the fly; `hash` is computed over what is in flash (ciphertext in the secondary slot, plain text in
the primary slot after installation). Outside an encrypted session only whole-image hashes are answered: hashes of
small ranges would reveal the plain code piece by piece. Not available in the direct-xip modes, where images run from both slots.
Protect the device key like the SECURE link: RDP on, see [umcub link](#shared-buses-umcub-link-addressing-secure-mode).

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
tools/umcub_inspect.py --port /dev/ttyACM1 verify 0 1
tools/umcub_inspect.py --port /dev/ttyACM1 hash 0 1 --file app.signed.bin     # MATCH / MISMATCH
tools/umcub_inspect.py --udp 192.168.1.50 read 0 0 --out dump.bin              # needs UMCUB_CFG_READBACK
```

Note: the `hash` shown by the standard `smpmgr image state-read` is read from the TLV inside the image and is **not**
recomputed from flash — use `verify` / `hash` above to check what is really stored.

## Application library

```cmake
set(UMCUB_BOARD nucleo_h755zi_q)   # same configuration as the bootloader
set(UMCUB_CORE cm7)
add_subdirectory(path/to/umcub/lib/umcub_app umcub)
target_link_libraries(app PRIVATE umcub::app)
umcub_app_linker_script(app IMAGE 0)        # or your own script with UMCUB_APP_FLASH_ORIGIN_0_0
umcub_sign_image(app VERSION 1.2.3)         # -> app.signed.bin / app.signed.hex (same signed image)
```

```c
#include "umcub.h"
umcub_version_t v;
umcub_boot_version(&v);                         /* bootloader version */
const umcub_handoff_t *bi = umcub_boot_info();  /* boot reason, image versions, update transport */
umcub_confirm();                                /* swap modes: otherwise reverted after reset */
umcub_enter_bootloader(0);                      /* reboot into recovery mode */

umcub_slot_writer_t w;                          /* the application writes an image itself */
umcub_slot_begin(&w, 0, UMCUB_SLOT_DEFAULT, size);   /* slot: UMCUB_CFG_APP_WRITE_SLOT or explicit */
umcub_slot_write(&w, chunk, n);
umcub_slot_finish(&w, true, false);             /* check the header, mark for a test boot */
```

The bootloader passes a `umcub_handoff_t` to the application (at `UMCUB_CFG_SHARED_RAM_ADDR`, protected by a CRC-32)
and keeps an info block in the last 256 bytes of its own flash, so the bootloader version is readable even after
the RAM was cleared.

Writing from the application:
- target slot `UMCUB_SLOT_SECONDARY` (default), `UMCUB_SLOT_PRIMARY` or `UMCUB_SLOT_INACTIVE` (direct-xip: the slot the
  application is not running from); it can be overridden per call;
- writing the slot the code is running from is refused (`UMCUB_EBUSY`);
- in swap modes the secondary slot is refused (`UMCUB_EBUSY`) while the running image is still on test: the secondary
  then holds the previous image, the only way back;
- if the slot is in the same flash bank as the running code, the CPU stalls during a sector erase (up to ~2 s on the
  H7). Applications that write their own updates should keep the secondary slot in the other bank.

## External controllers and custom transports

Interfaces are often attached through a separate chip: a CAN controller on SPI (MCP2515, MCP2518FD), an Ethernet
MAC on SPI (ENC28J60, LAN9250), a W5500 with its own TCP/IP, an RS-485 line with its own framing, a BLE module.
The bootloader supports this at two levels. Both live in `boards/<board>/umcub_board.c`, which is added to the
build automatically. Board code may use LL / registers: it is tied to the MCU anyway.

### 1. Board driver under a built-in transport

ISO-TP + SMP (CAN) and the IPv4/ARP/DHCP/UDP stack + SMP (Ethernet) sit on a small driver interface. Select the
board driver and implement that interface:

```c
/* umcub_config.h */
#define UMCUB_CFG_TRANSPORT_CAN   1
#define UMCUB_CFG_CAN_DRIVER      UMCUB_DRIVER_BOARD   /* the on-chip FDCAN driver is not compiled */
#define UMCUB_CFG_TRANSPORT_ETH   1
#define UMCUB_CFG_ETH_DRIVER      UMCUB_DRIVER_BOARD   /* the on-chip MAC driver is not compiled */
```

| Transport | Functions to implement (`port/include/...`) | Contract |
|---|---|---|
| CAN | `umcub_port_can_init(cfg)`, `_deinit()`, `_send(id, data, len)`, `_recv(&id, data, &len)` (`umcub_port_can.h`) | receive only `cfg->rx_id`; `send` returns `UMCUB_EBUSY` when the TX buffer is full; `len` up to 8 (classic) or 64 (`cfg->fd`) |
| Ethernet | `umcub_port_eth_init(mac, phy, pins, npins)`, `_deinit()`, `_link()`, `_tx(frame, len)`, `_rx(buf, max)` (`umcub_port_eth.h`) | whole Ethernet frames without FCS; `pins` is `NULL` when `UMCUB_CFG_ETH_RMII_PINS` is not set; `rx` returns 0 when nothing arrived |

All CAN settings from the config (`UMCUB_CFG_CAN_BITRATE`, `_FD`, `_RX_ID`, `_TX_ID`, `_EXT_ID`, ...) reach the
driver through `umcub_can_cfg_t`. Everything above the driver works as with the on-chip controller, including
`tools/smp_can.py`, `mcumgr --conntype udp`, text commands and verify/hash.

### 2. Board transport

For anything that is not CAN or raw Ethernet, write a whole transport. It is polled together with the built-in
ones; responses go back to the transport the request came from.

```c
/* umcub_config.h */
#define UMCUB_CFG_TRANSPORT_USER  1

/* umcub_board.c - a packet transport, e.g. a W5500 UDP socket */
#include "umcub_transport.h"
#include "umcub_handoff.h"

extern const umcub_transport_t umcub_transport_user;
static uint8_t rx[UMCUB_CFG_SMP_MTU];

static int  w5500_init(void)   { /* SPI + chip setup, open UDP socket */ return 0; }
static void w5500_deinit(void) { /* close socket, SPI/GPIO/EXTI back to reset state */ }
static void w5500_poll(void)
{
    size_t n = /* non-blocking: datagram waiting? read it into rx */ 0;
    if (n) {
        umcub_smp_packet_rx(&umcub_transport_user, rx, n);   /* raw SMP or a text command */
    }
}
static int w5500_send(const uint8_t *pkt, size_t len) { /* send to the last sender */ return 0; }

const umcub_transport_t umcub_transport_user = {
    .id = UMCUB_TRANSPORT_USER, .name = "w5500",
    .init = w5500_init, .deinit = w5500_deinit, .poll = w5500_poll, .send_packet = w5500_send,
};
```

There are two kinds of transport, matching the built-in ones:
- **packet** (`poll` + `send_packet`, like CAN and UDP): whole SMP packets in and out. A packet that is not SMP is
  treated as a text command.
- **stream** (`read` + `write`, like UART and USB CDC): bytes of the SMP serial protocol (NLIP lines, base64) plus
  typed text commands. `read` is non-blocking and returns what is available; `write` may block until sent, with a
  timeout.

After an update over it, `umcub_boot_info()->last_transport` in the application is `UMCUB_TRANSPORT_USER`.

### Rules for board drivers and transports

- The bootloader is a single polled loop without an RTOS. Nothing may block without a bound: use
  `umcub_port_millis()` timeouts.
- Interrupts are optional. If you use them, the ISR only moves data into a buffer.
- `deinit` must put every peripheral, pin, EXTI line and DMA channel it touched back into reset state (RCC reset is
  the simplest way). It runs right before the jump into the application.
- No heap, static buffers only.
- A transparent UART↔CAN or UART↔RS-485 bridge that just forwards bytes needs nothing: for the bootloader it is
  the UART transport.

Checked by the build matrix (`tests/boards/custom_drivers`: board CAN + Ethernet drivers + board transport, the
on-chip drivers left out) and by the host test `umcub_host_board` (SMP over a board CAN driver, SMP and commands
over a board transport, DHCP through a board Ethernet driver, deinit of all three). Not yet run with a real
external controller.

## Using umcub from an IDE (STM32CubeIDE, Keil MDK)

|  | Bootloader | Application with `umcub::app` |
|---|---|---|
| CMake (command line, VS Code, CLion) | build, flash, debug | yes |
| STM32CubeIDE (GCC) | build with CMake; CubeIDE can drive the build and debug the ELF | yes |
| Keil MDK (Arm Compiler 6) | build with CMake (GCC); Keil can flash the .hex and debug the ELF | yes |

The bootloader is written once and rarely changes, so it does not have to live in the IDE. Build it with CMake (see
Quick start) and program the `.hex` with STM32CubeProgrammer or the IDE. Building the bootloader with Arm Compiler 6
is not supported: it depends on GCC startup code, GCC linker script templates and its own newlib stdio overrides.

The application is a normal CubeMX / IDE project. It needs four things: the right flash address, the library, a
signing step after the build and a debug setup that flashes the signed image.

**1. Where to link the application.** Ask the tool, with the same board configuration (and overlays) as the
bootloader:

```sh
python tools/umcub_image.py info --board nucleo_h755zi_q --core cm7
```
```
image 0 (CM7 application): slot 0x08020000 size 0x60000
  link at   ORIGIN 0x08020400  LENGTH 0x5DC00   (vector table = ORIGIN)
  GCC .ld   FLASH (rx) : ORIGIN = 0x08020400, LENGTH = 0x5DC00
  Keil      Target > IROM1: Start 0x08020400  Size 0x5DC00
...
keep free: 0x3800FF00..0x38010000 (bootloader handoff, kept over reset)
```

- STM32CubeIDE: change the `FLASH` line in the project's `STM32xxxx_FLASH.ld`.
- Keil: *Options for Target → Target*: IROM1 start/size as printed, IROM2 unchecked (or the same region in your
  scatter file).
- Both: do not place variables in the handoff area. CubeMX projects do not use SRAM4 by default.
- VTOR is set by the bootloader. Leave `USER_VECT_TAB_ADDRESS` undefined in `system_stm32*.c` (the CubeMX default).
- Also needed:
  - the application must select the same power supply as `UMCUB_CFG_PWR_SUPPLY` (on the H7 it can only be set
    once per power-up; `HAL_PWREx_ConfigSupply` otherwise fails);
  - it must feed the watchdog if `UMCUB_CFG_WATCHDOG_MS` is set.

  The clocks are back in the reset state when the application starts, so the CubeMX `SystemClock_Config()` works
  unchanged.

**2. The library.** Add one source file, `lib/umcub_app/umcub_app_all.c` (the whole library, family part included),
and these include paths (relative to the umcub checkout):

```
lib/umcub_app/include
config
boards/<board>                         # the bootloader's umcub_config.h
port/<family>/include                  # e.g. port/stm32h7/include
port/include
mcuboot_port/include
third_party/mcuboot/boot/bootutil/include
```

CMSIS and LL headers come from the project (`Drivers/CMSIS/...`, `Drivers/STM32H7xx_HAL_Driver/Inc`); umcub's copies
in `third_party/` work as well. Defines: the device define (`STM32H755xx`) and, on dual-core parts,
`CORE_CM7` / `CORE_CM4`. CubeMX projects already have them. If the bootloader was built with
`UMCUB_CONFIG_PRE/POST` overlays, define the same macros, e.g. `UMCUB_CONFIG_POST="path/to/overlay.h"`. The
matrix build compiles this file with exactly this include list, using GCC and clang (Arm Compiler 6 is clang-based).

**3. Signing after the build.** `tools/umcub_image.py sign` takes the ELF / AXF (or an Intel HEX / raw `.bin`). It
checks that the image is linked at the slot origin and fits the slot, and writes `<name>.signed.bin` and
`<name>.signed.hex` next to it. It uses the same imgtool arguments as `umcub_sign_image()` in CMake; the matrix build
checks that both give the same image.

It reads `umcub_config.h` through a C preprocessor. The search order is `--cc`, then `$UMCUB_CC`, then
`arm-none-eabi-gcc`, then `armclang` (also in the default Keil folders), then `gcc` / `clang`. It needs Python 3
with imgtool's dependencies: `pip install -r tools/requirements.txt` (on Windows `py -m pip install ...`).

- **STM32CubeIDE**: *Project → Properties → C/C++ Build → Settings → Build Steps → Post-build steps*. The command
  runs in the build folder, with the CubeIDE toolchain in `PATH`:
  ```
  python <umcub>/tools/umcub_image.py sign --board nucleo_h755zi_q --core cm7 --image 0 --version 1.2.3 ${ProjName}.elf
  ```
- **Keil MDK**: *Options for Target → User → After Build/Rebuild → Run #1*:
  ```
  python <umcub>\tools\umcub_image.py sign --board nucleo_h755zi_q --core cm7 --image 0 --version 1.2.3 "#L"
  ```
  `#L` is the `.axf` with its full path. If armclang is not found automatically, add
  `--cc "$KARM\ARMCLANG\bin\armclang.exe"`.

Other `sign` options:
- `--key prod.pem`;
- `--slot 1` (direct-xip: the slot the image is linked for);
- `--confirm` and `--pad`;
- `--depends "(1,1.0.0)"`.

In direct-xip-revert mode it also writes `<name>.confirmed.{bin,hex}`.

**4. Flashing and debugging.** The IDE downloads the unsigned ELF by default. The bootloader rejects it (no image
header) and stays in recovery mode. Download the signed `.hex` instead and take only the symbols from the ELF:

- **STM32CubeIDE**: *Debug Configurations → Startup → Load Image and Symbols*. Add `<name>.signed.hex` with
  *Download* on and *Load symbols* off, and keep `<name>.elf` with *Download* off and *Load symbols* on. If the session
  does not start from reset, add `monitor reset` to *Run Commands*. Execution then goes reset → bootloader →
  application, and breakpoints in the application work as usual.
- **Keil MDK**: *Options for Target → Utilities → Use External Tool for Flash Programming*:
  - Command: `<CubeProgrammer>\bin\STM32_Programmer_CLI.exe`
  - Arguments: `-c port=SWD mode=UR -d "$L@L.signed.hex" -v -rst`

  *Update Target before Debugging* then programs the signed image. On the *Debug* tab, *Load Application at Startup*
  only loads the `.axf` symbols.

Updates through the bootloader (`smpmgr`, `mcumgr`, `dfu-util`, CAN, UDP) use `<name>.signed.bin` from any IDE.

**Dual-core H745/H755 with CubeMX dual-core projects.** Each core's project signs its own image. With `SINGLE_BOOT`
that is `--core cm7 --image 0` for the CM7 and `--core cm4 --image 1` for the CM4. Program the option bytes with
`tools/h755_option_bytes.sh`, not the CubeMX defaults. The bootloader starts the CM4 application first, then jumps
to the CM7 one, so the CubeMX start-up handshake should find the CM4 in STOP as it expects. This is not yet verified
on hardware. Without a valid CM4 image, the CM4 stays parked in the bootloader. In that case the CubeMX CM7 code that
waits for `RCC_FLAG_D2CKRDY` times out into `Error_Handler()`; make that wait non-fatal if the CM7 must run alone.

**Bootloader inside STM32CubeIDE (optional).** *File → New → Makefile Project with Existing Code* on the umcub
folder (toolchain *MCU ARM GCC*). Then, under *C/C++ Build*, set the build command to `cmake --build build/h755-cm7`
after running `cmake --preset h755-cm7` once (CMake, Ninja and `.venv` are needed). Debug
`build/h755-cm7/umcub_nucleo_h755zi_q_cm7.elf` with an *STM32 C/C++ Application* configuration.

## Your own board

Ported families: STM32H7 (H743/H753 and the dual-core H745/H755/H747/H757; not H7A3/B0/H72x/H73x yet) and
STM32F1 (F103x8/xB). For another series the port comes first, see the next section.

**1. Describe the board.** The board directory can live in your own repository (umcub as a submodule):
`boards/my_board/umcub_config.h`. Start from [`config/umcub_config_template.h`](config/umcub_config_template.h).
Everything not set takes the default from `config/umcub_config_defaults.h`. A complete minimal example:

```c
/* umcub configuration: my_board (STM32H743, 8 MHz crystal, RS-232 on USART1). */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

#define UMCUB_CFG_MCU                   STM32H743xx
#define UMCUB_CFG_CLOCK_SOURCE          UMCUB_CLK_HSE
#define UMCUB_CFG_HSE_HZ                8000000
#define UMCUB_CFG_PWR_SUPPLY            UMCUB_H7_SUPPLY_LDO

/* 2 MiB, 128 KiB sectors: bootloader 1 sector, swap-move needs the primary
 * slot one sector larger than the secondary. */
#define UMCUB_CFG_UPGRADE_MODE          UMCUB_MODE_SWAP_MOVE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(128)
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     0x08020000
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     UMCUB_KB(512)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   0x080A0000
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   UMCUB_KB(384)

/* Recovery: button on PC13 (low = pressed) or a request from the application. */
#define UMCUB_CFG_ENTRY_GPIO            1
#define UMCUB_CFG_ENTRY_GPIO_PIN        UMCUB_PIN('C', 13, 0)
#define UMCUB_CFG_ENTRY_GPIO_ACTIVE     0
#define UMCUB_CFG_ENTRY_GPIO_PULL       UMCUB_PULL_UP

#define UMCUB_CFG_TRANSPORT_UART        1
#define UMCUB_CFG_UART_INSTANCE         1
#define UMCUB_CFG_UART_BAUD             115200
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('A', 9, 7)    /* AF7 */
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('A', 10, 7)

#endif /* UMCUB_CONFIG_H */
```

**2. Make your own signing key.** The repository key is public and for development only:

```sh
imgtool keygen -t ecdsa-p256 -k keys/prod.pem      # keep it out of the repository
```

**3. Build the bootloader.**

```sh
cmake -S umcub -B build/boot -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DUMCUB_BOARD=$PWD/boards/my_board -DUMCUB_SIGNING_KEY=$PWD/keys/prod.pem
cmake --build build/boot                # build/boot/umcub_my_board_cm7.{elf,hex,bin}
```

Configuration mistakes stop the build with a message: slots overlapping or off sector boundaries, the wrong slot
sizes for the mode, a missing scratch area, an HSE the port cannot use, and so on.

**4. Adapt the application.** Use the same board directory and key: CMake with `umcub::app`,
`umcub_app_linker_script()` and `umcub_sign_image()` (see [Application library](#application-library)), or an IDE
project with `tools/umcub_image.py` (see [Using umcub from an IDE](#using-umcub-from-an-ide-stm32cubeide-keil-mdk)).
`tools/umcub_image.py info --board boards/my_board` prints where to link it.

**5. Program and protect.** Program the bootloader `.hex` and the signed application `.hex`. On dual-core H7 parts,
also program the option bytes (`tools/h755_option_bytes.sh`). For production, write-protect the bootloader sector
(WRP) and enable RDP.

**6. Optional: `boards/my_board/umcub_board.c`.** It is compiled automatically when present. Use it for your own text
commands (`UMCUB_CMD_USER`), drivers for external controllers (`UMCUB_DRIVER_BOARD`) or a transport of your own
(`UMCUB_CFG_TRANSPORT_USER`); see [External controllers and custom transports](#external-controllers-and-custom-transports).

Things that are easy to get wrong:
- **Slots.** Slots start and end on sector boundaries. swap-move needs a primary slot one sector larger than the
  secondary, swap-offset the other way round. Only swap-scratch needs a scratch sector. The last
  `UMCUB_CFG_TRAILER_RESERVE` bytes of a slot belong to MCUboot.
- **Clocks and power.**
  - STM32H7: HSE must be a multiple of 2 MHz (8, 12, 16, 24 MHz; not 25 MHz). The application must select the same
    power supply (`UMCUB_CFG_PWR_SUPPLY`): it can be set only once per power-up.
  - STM32F1: HSE of 8, 12 or 16 MHz, otherwise HSI.
- **Pins.** Alternate-function numbers come from the datasheet of your part. F1 has no AF numbers: the remap is
  chosen from the TX pin.
- **RAM.** The handoff area (`UMCUB_CFG_SHARED_RAM_ADDR`, 256 bytes) must stay out of the application's RAM. The
  generated application linker scripts already exclude it.
- **Image header.** `UMCUB_CFG_IMAGE_HEADER_SIZE` is also the offset of the application vector table: it must meet
  the VTOR alignment of the core (0x200 on Cortex-M3/M4 with up to 128 vectors, 0x400 on the H7).
- **Watchdog.** If `UMCUB_CFG_WATCHDOG_MS` is set, the application must feed the IWDG: once started it cannot be
  stopped.

## Adding a series

1. `cmake/families/stm32<fam>.cmake`: CPU flags, sources (with `port/common/cortexm_sys.c` and
   `cortexm_common.c`), the `cmsis_device_<fam>` and LL driver submodules.
2. `port/stm32<fam>/`: `include/umcub_family_defaults.h` (flash write unit, sector size),
   `include/umcub_family_cmsis.h` (the device header), `include/umcub_family_app.inc` (port sources of the
   application library), register-level flash driver (LL has none), clocks (including restoring the reset state
   before the jump), reset cause, GPIO / IWDG, linker templates, then the optional peripherals (UART, CAN, ETH, USB).
   The Cortex-M part is shared (`port/common/`: SysTick time base, deinit of NVIC and the peripherals the drivers
   registered, reset, jump, UID): `umcub_port_init()` calls `umcub_cm_save_clocks()` first and `umcub_cm_start()`
   after the clock setup, `umcub_port_deinit()` calls `umcub_cm_deinit()`.
3. `boards/<board>/umcub_config.h` (and optionally `umcub_board.c`).
4. Add the new configuration to `tools/build_matrix.sh`.

Rules: no `stm32*.h` / LL includes outside `port/<family>/`; all series differences stay in the port; RAM that must
survive a reset (handoff area) is written with 32-bit stores only (ECC SRAM loses narrower writes on reset).
Check pin mappings against the board manual, alternate functions against the datasheet, and the errata for every
peripheral a driver touches.

## Testing

```sh
tools/build_matrix.sh                 # 26 bootloader configurations, examples, IDE checks, host tests; warning-free
ctest --test-dir build/matrix/host    # host tests only
tests/host/link_e2e.py build/matrix/host   # simulated devices: umcub link bus, smpmgr, host tools
tools/check_docs.py                   # README still matches the repository (size tables, boards, tools, ...)
```

Host tests (`tests/host`) run the real MCUboot (boot_go, boot_serial, ECDSA) on an emulated H7 flash (32-byte
words, no double programming) through the umcub transport layer. Covered: SMP upload / echo / list over the packet
and stream paths (including two interleaved streams), swap → revert → confirm in scratch / move / offset modes, text
commands, verify / hash / read, ISO-TP classic/FD, DHCP / ARP / ICMP, board-supplied drivers and transports, the
board-type check, the umcub link in ADDRESSED mode and in SECURE mode with encryption (handshake, wrong admin key,
replayed / tampered / reordered frames, session end, RDP level 0), encrypted images (SMP upload into the primary
slot decrypted in place, swap upgrade / revert / confirm with re-encryption, verify of an encrypted secondary,
readback only inside an encrypted session). `link_e2e.py` runs device simulators (the real mux, link, boot_serial and
CAN transport on emulated flash) against the host tools: `umcub_link.py` with three nodes on one bus,
`umcub_inspect.py` on plain SMP, `smp_can.py` on the CAN transport (python-can `serial` bus on a pty).

Hardware tests: `tools/hw/powerfail_test.py` resets the MCU in the middle of the K-th flash operation (build with
`tools/config/fault_inject.h`, test only) and checks that an interrupted upgrade or revert always completes. See
`TODO.md` for what has been verified on hardware.

## Layout

```
boot/            boot core: main (entry decision, boot_go, jump), startup, handoff, info, log, commands, inspection
mcuboot_port/    MCUboot glue: mcuboot_config.h, flash map backend, shims
port/include/    hardware API (umcub_port.h, _uart, _can, _eth, _usb)
port/common/     Cortex-M part shared by the ports (SysTick, deinit, jump, waits) + common linker sections
port/stm32h7/    STM32H7 port + linker templates
port/stm32f1/    STM32F1 port (flash, clocks, GPIO, USART) + linker templates
transport/       mux (SMP), umcub link, uart, usb (tinyUSB CDC/DFU), can (ISO-TP), net (IPv4/UDP/DHCP)
lib/umcub_app/   application library (umcub::app); umcub_app_all.c = the whole library as one file for IDEs
config/          template, defaults, compile-time checks
boards/          board configurations
examples/        CM7 / CM4 applications for NUCLEO-H755ZI-Q, Blue Pill application
tools/           setup, build matrix, flashing, option bytes, host tools, keys, hardware tests;
                 umcub_image.py = slot addresses and signing for IDE projects; size_table.py = size tables;
                 check_docs.py = README consistency check; umcub_keys.py = umcub link keys as C arrays;
                 umcub_smp.py = SMP helpers of the host tools (on smp / smpclient)
tests/host/      host tests
tests/tools/     helpers for the build matrix
tests/boards/    build-matrix boards (custom_drivers: board drivers + board transport)
```

## Mode notes and limitations

- **direct-xip-revert**: MCUboot erases an image without a confirmed trailer on the next boot. For programmers and
  serial recovery `umcub_sign_image()` therefore also produces `<app>.confirmed.{bin,hex}` (`--confirm --pad`). The
  plain `.signed.bin` is meant for updates from the application or over DFU, followed by `umcub_confirm()`.
- **direct-xip / direct-xip-revert**: an SMP upload always names the slot (`image`: 0/1 = image 0 primary,
  2 = image 0 secondary, 3/4 = image 1), and the image must be linked for that slot
  (`umcub_app_linker_script(... SLOT n)`, `umcub_sign_image(... SLOT n)`). Downgrade prevention is not available in
  these modes.
- **Recovery over the network / CAN is not authenticated** unless the transport uses `UMCUB_LINK_SECURE` (see
  [umcub link](#shared-buses-umcub-link-addressing-secure-mode)): otherwise any node on the LAN (UDP 1337) or the
  CAN bus can upload any image signed with your key in recovery mode, including an older one. Signatures are
  always checked.
  `UMCUB_CFG_DOWNGRADE_PREVENTION` (swap modes only) protects updates through the secondary slot (DFU, application);
  SMP recovery writes straight into the primary slot and is not covered. If this matters, allow recovery only by the
  entry pin (`UMCUB_CFG_ENTRY_GPIO`, no `ENTRY_WAIT_MS`) or do not enable network transports.
- **swap-scratch wear**: the scratch sector is erased once per moved sector on every update and wears fastest;
  swap-offset (recommended by MCUboot) or overwrite spread the wear better.

## Security

`tools/keys/dev-ecdsa-p256.pem` is a **development key** stored in the repository — anybody can sign images with it.
For a product generate your own (`imgtool keygen -t ecdsa-p256 -k prod.pem`) and pass `-DUMCUB_SIGNING_KEY=prod.pem`
when building the bootloader and the applications. Also write-protect the bootloader sector (WRP) and enable RDP.
For transports on a shared bus see the SECURE mode of the [umcub link](#shared-buses-umcub-link-addressing-secure-mode)
and its own keys.

## License

Apache License 2.0, see `LICENSE`. Third-party components keep their own licenses, see `THIRD_PARTY_NOTICES.md`.
