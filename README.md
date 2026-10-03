# umcub — a portable MCUboot-based bootloader for STM32

umcub (**U**niversal **MCU**boot **B**ootloader) is a bootloader for STM32 microcontrollers built on
[MCUboot](https://github.com/mcu-tools/mcuboot):

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
- **all on**: every transport and feature (the `nucleo_h755zi_q` default with all interfaces).
- **UART** is counted on top of base. It includes the SMP core that every SMP transport needs (`boot_serial`,
  zcbor, the multiplexer), so the first SMP transport always costs about 10 K.
- The other columns are counted on top of base + UART.
- **DFU only** is USB DFU as the only transport, on top of base. Without an SMP transport, `boot_serial` is not
  compiled at all.

| MCU | base | all on | UART (+SMP) | USB CDC | USB DFU | USB CDC+DFU | CAN | CAN FD | Ethernet (+DHCP) | DFU only |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| STM32H755 CM7 (2 images, SINGLE_BOOT) | 18.9 K | 54.8 K | +10.0 K | +11.1 K | +10.5 K | +13.2 K | +2.2 K | +2.2 K | +4.0 K | +10.9 K |
| STM32H755 CM4 (PER_CORE) | 16.4 K | 51.9 K | +9.8 K | +11.1 K | +10.5 K | +13.1 K | +2.2 K | +2.2 K | +4.0 K | +10.9 K |
| STM32H743 / H753 (single core) | 17.1 K | 52.6 K | +9.7 K | +11.1 K | +10.5 K | +13.1 K | +2.2 K | +2.2 K | +4.0 K | +10.9 K |

| MCU | log (level 3) | text commands | verify + hash | readback |
|---|---:|---:|---:|---:|
| STM32H755 CM7 (2 images, SINGLE_BOOT) | +2.6 K | +2.7 K | +0.9 K | +0.2 K |
| STM32H755 CM4 (PER_CORE) | +2.4 K | +2.6 K | +0.9 K | +0.2 K |
| STM32H743 / H753 (single core) | +2.4 K | +2.6 K | +0.9 K | +0.2 K |

Notes:
- Only the STM32H7 port exists so far. The H755 CM4 row is the CM4 bootloader of `PER_CORE` mode (Cortex-M4 code).
  On the board it uses CAN only; the other columns are measured with borrowed pins.
- The H7 bootloader gets one 128 KiB sector, so even "all on" uses less than half of it.
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

## Adding a series / board

1. `cmake/families/stm32<fam>.cmake`: CPU flags, sources, the `cmsis_device_<fam>` and LL driver submodules.
2. `port/stm32<fam>/`: `include/umcub_family_defaults.h` (flash write unit, sector size),
   `include/umcub_family_app.inc` (port sources of the application library), register-level flash driver
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
tools/build_matrix.sh                 # 15 bootloader configurations, examples, IDE checks, host tests; warning-free
ctest --test-dir build/matrix/host    # host tests only
```

Host tests (`tests/host`) run the real MCUboot (boot_go, boot_serial, ECDSA) on an emulated H7 flash (32-byte
words, no double programming) through the umcub transport layer. Covered: SMP upload / echo / list over the packet
and stream paths (including two interleaved streams), swap → revert → confirm in scratch / move / offset modes, text
commands, verify / hash / read, ISO-TP classic/FD, DHCP / ARP / ICMP, board-supplied drivers and transports.

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
lib/umcub_app/   application library (umcub::app); umcub_app_all.c = the whole library as one file for IDEs
config/          template, defaults, compile-time checks
boards/          board configurations
examples/        CM7 / CM4 applications for NUCLEO-H755ZI-Q
tools/           setup, build matrix, flashing, option bytes, host tools, keys, hardware tests;
                 umcub_image.py = slot addresses and signing for IDE projects; size_table.py = size tables
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
