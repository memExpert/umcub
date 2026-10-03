# umcub — a portable MCUboot-based bootloader for STM32

umcub is a bootloader for STM32 microcontrollers built on [MCUboot](https://github.com/mcu-tools/mcuboot):

- **Not tied to a series.** The boot logic, transports and application library use one hardware API
  (`port/include/umcub_port*.h`); everything series-specific lives in `port/stm32<fam>/`. The first port is STM32H7
  (reference board NUCLEO-H755ZI-Q, both cores).
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
cmake --build --preset h755-cm7      # build/h755-cm7/umcub_nucleo_h755zi_q_cm7.{elf,hex,bin}

cmake -S examples/h755_cm7_app -B build/ex-cm7 -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build/ex-cm7
cmake -S examples/h755_cm4_app -B build/ex-cm4 -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build/ex-cm4

tools/h755_option_bytes.sh single    # the CM4 starts from the bootloader vector table and waits
tools/flash.sh build/h755-cm7/umcub_nucleo_h755zi_q_cm7.hex \
               build/ex-cm7/h755_cm7_app.signed.hex build/ex-cm4/h755_cm4_app.signed.hex
```

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
| Transports | `UMCUB_CFG_TRANSPORT_{UART,USB_CDC,USB_DFU,CAN,ETH}` plus their parameters |
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

- Commands are accepted in recovery mode and during the `UMCUB_CFG_ENTRY_WAIT_MS` window (e.g. 300 ms: "press `b`
  right after reset to stay in the bootloader").
- UART and USB CDC: typed text. A command is recognised only at the start of a line; a command that is the beginning
  of another one (`r` / `read`) waits for Enter; an unknown line is answered with `? text` after Enter.
- UDP and CAN: a packet that is not an SMP frame is a command; the reply comes back as one packet
  (`echo -n i | nc -u <ip> 1337`).
- SMP (mcumgr / smpmgr / dfu-util) keeps working in parallel.
- `ok ...` / `? ...` replies can be turned off with `UMCUB_CFG_CMD_REPLY 0`.

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

## Adding a series / board

1. `cmake/families/stm32<fam>.cmake`: CPU flags, sources, the `cmsis_device_<fam>` and LL driver submodules.
2. `port/stm32<fam>/`: `include/umcub_family_defaults.h` (flash write unit, sector size), register-level flash driver
   (LL has none), clocks (including restoring the reset state before the jump), SysTick / GPIO / IWDG, linker
   templates, then the optional peripherals (UART, CAN, ETH, USB).
3. `boards/<board>/umcub_config.h` (and optionally `umcub_board.c`).
4. Add the new configuration to `tools/build_matrix.sh`.

Rules: no `stm32*.h` / LL includes outside `port/<family>/`; all series differences stay in the port; RAM that must
survive a reset (handoff area) is written with 32-bit stores only (ECC SRAM loses narrower writes on reset).
Check pin mappings against the board manual, alternate functions against the datasheet, and the errata for every
peripheral a driver touches.

## Testing

```sh
tools/build_matrix.sh                 # 13 bootloader configurations + examples + host tests, warning-free
ctest --test-dir build/matrix/host    # host tests only
```

Host tests (`tests/host`) run the real MCUboot (boot_go, boot_serial, ECDSA) on an emulated H7 flash (32-byte
words, no double programming) through the umcub transport layer. Covered: SMP upload / echo / list over the packet
and stream paths (including two interleaved streams), swap → revert → confirm in scratch / move / offset modes, text
commands, verify / hash / read, ISO-TP classic/FD, DHCP / ARP / ICMP.

Hardware tests: `tools/hw/powerfail_test.py` resets the MCU in the middle of the K-th flash operation (build with
`tools/config/fault_inject.h`, test only) and checks that an interrupted upgrade or revert always completes. See
`TODO.md` for what has been verified on hardware.

## Layout

```
boot/            boot core: main (entry decision, boot_go, jump), startup, handoff, info, log, commands, inspection
mcuboot_port/    MCUboot glue: mcuboot_config.h, flash map backend, shims
port/include/    hardware API (umcub_port.h, _uart, _can, _eth, _usb)
port/stm32h7/    STM32H7 port + linker templates
transport/       mux (SMP), uart, usb (tinyUSB CDC/DFU), can (ISO-TP), net (IPv4/UDP/DHCP)
lib/umcub_app/   application library (umcub::app)
config/          template, defaults, compile-time checks
boards/          board configurations
examples/        CM7 / CM4 applications for NUCLEO-H755ZI-Q
tools/           setup, build matrix, flashing, option bytes, host tools, keys, hardware tests
tests/host/      host tests
```

## Mode notes and limitations

- **direct-xip-revert**: MCUboot erases an image without a confirmed trailer on the next boot. For programmers and
  serial recovery `umcub_sign_image()` therefore also produces `<app>.confirmed.{bin,hex}` (`--confirm --pad`). The
  plain `.signed.bin` is meant for updates from the application or over DFU, followed by `umcub_confirm()`.
- **direct-xip / direct-xip-revert**: an SMP upload always names the slot (`image`: 0/1 = image 0 primary,
  2 = image 0 secondary, 3/4 = image 1), and the image must be linked for that slot
  (`umcub_app_linker_script(... SLOT n)`, `umcub_sign_image(... SLOT n)`). Downgrade prevention is not available in
  these modes.
- **Recovery over the network / CAN is not authenticated**: any node on the LAN (UDP 1337) or the CAN bus can upload
  any image signed with your key in recovery mode, including an older one. Signatures are always checked.
  `UMCUB_CFG_DOWNGRADE_PREVENTION` (swap modes only) protects updates through the secondary slot (DFU, application);
  SMP recovery writes straight into the primary slot and is not covered. If this matters, allow recovery only by the
  entry pin (`UMCUB_CFG_ENTRY_GPIO`, no `ENTRY_WAIT_MS`) or do not enable network transports.
- **swap-scratch wear**: the scratch sector is erased once per moved sector on every update and wears fastest;
  swap-offset (recommended by MCUboot) or overwrite spread the wear better.

## Security

`tools/keys/dev-ecdsa-p256.pem` is a **development key** stored in the repository — anybody can sign images with it.
For a product generate your own (`imgtool keygen -t ecdsa-p256 -k prod.pem`) and pass `-DUMCUB_SIGNING_KEY=prod.pem`
when building the bootloader and the applications. Also write-protect the bootloader sector (WRP) and enable RDP.

## License

Apache License 2.0, see `LICENSE`. Third-party components keep their own licenses, see `THIRD_PARTY_NOTICES.md`.
