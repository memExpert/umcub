# umcub — a portable MCUboot-based bootloader for STM32

umcub (**U**niversal **MCU**boot **B**ootloader) is a bootloader for STM32 microcontrollers built on
[MCUboot](https://github.com/mcu-tools/mcuboot). It checks the signature of your application on every boot, installs
updates (and reverts a failed one), and takes new images over the interface your board has: UART, USB, CAN or
Ethernet. A board is one configuration file; what you do not enable is not compiled.

- **Transports**, each enabled separately, any one of them can be the only one:
  - UART (also RS485);
  - USB — CDC-ACM and/or DFU 1.1 (`dfu-util`, no other tool needed);
  - CAN / CAN FD over ISO-TP (CAN FD verified in loopback only, not on a bus yet);
  - Ethernet — UDP with DHCP;
  - from the application itself (`umcub_slot_*`).
- **Upload protocols** on these transports - SMP is not required:
  - SMP, MCUboot's serial recovery: the standard tools (`smpmgr`, `mcumgr`; over CAN `tools/host/smp_can.py`);
  - the **lite upload protocol**: begin / data / end frames with CRC, about 3 K smaller than SMP for small parts,
    host side `tools/host/umcub_lite.py` ([without SMP](docs/recovery.md#recovery-without-smp-lite-upload-and-board-protocols));
  - text commands next to either (`i`, `b`, `verify`, ...), or a protocol of your own.
- **MCUboot upgrade modes**: overwrite, swap-scratch, swap-offset, direct-xip (with or without revert).
- **Images** signed with ECDSA-P256 (`imgtool`), optionally encrypted for the device.
- **Several devices on one bus** (RS485, CAN, Ethernet): node addresses, a SECURE mode with authentication and
  encryption ([umcub link](docs/link.md)).
- **Application library** `umcub::app`: what the bootloader did, confirm / revert, enter the bootloader, write an
  update yourself ([application](docs/application.md)).

| Family | Reference board | Transports |
|---|---|---|
| STM32H7 (H743/H753, dual-core H745/H755/H747/H757) | NUCLEO-H755ZI-Q (both cores) | UART, USB CDC / DFU, FDCAN (CAN FD), Ethernet |
| STM32F1 (F103x8/xB) | Blue Pill (STM32F103C8T6) | UART, USB CDC / DFU, bxCAN (not together with USB) |
| STM32F4 (F411xC/xE) | Black Pill (STM32F411CEU6) | UART (also RS485 with umcub link), USB CDC / DFU |

Size: from 12.5 K (F103) / 16.9 K (H743) without a transport, about 19 K with one UART on the F103 (20 K on the F411); every transport
and feature is listed in [Bootloader size](docs/size.md).

## Quick start

Once:

```sh
git submodule update --init
tools/setup.sh                                 # .venv: imgtool, smpmgr, python-can, ...
source .venv/bin/activate                      # smpmgr and the host tools on PATH
```

Then for the board (`<preset>` from the table): build the bootloader and its example application, program both
over SWD, update the application over the bootloader's serial port.

```sh
cmake --preset <preset> && cmake --build --preset <preset>
cmake --build --preset <preset> --target flash      # bootloader + signed example(s), then reset
smpmgr --port <port> image upload build/<preset>/examples/<app>/<app>.signed.bin
smpmgr --port <port> os reset
```

| Board | `<preset>` | `<app>` | `<port>`: SMP and log | Stay in the bootloader |
|---|---|---|---|---|
| NUCLEO-H755ZI-Q | `h755-cm7` | `h755_cm7_app` (CM4: `h755_cm4_app`, `--slot 1`) | ST-LINK VCP `/dev/ttyACM0` | hold B1 during reset |
| Blue Pill (STM32F103C8T6) | `bluepill` | `bluepill_app` | USB-UART on PA9/PA10 `/dev/ttyUSB0`, `--line-buffers 4` | send `b` within 300 ms after reset |
| Black Pill (STM32F411CEU6) | `blackpill` | `blackpill_app` | USB-UART on PA9/PA10 `/dev/ttyUSB0` | hold KEY (PA0) during reset |

On every board `b` in the running example application reboots into the bootloader, and a board without a valid
image stays there. The H755 needs its option bytes once before the first flash: `tools/hw/h755_option_bytes.py single`
(the CM4 waits in the bootloader). The `flash` target uses `tools/hw/probe.py` (STM32_Programmer_CLI on PATH or in
`$STM32_PROGRAMMER_CLI`; `-DUMCUB_PROBE=pyocd` for pyOCD, `-DUMCUB_PROBE_ARGS=...` for more options). USB, CAN,
RS485 and the other variants of each board: [Reference boards](docs/board.md#reference-boards).

## Your own board in five steps

1. **Describe the board**: copy the minimal configuration of your family -
   [F1](config/umcub_config_minimal_f1.h), [F4](config/umcub_config_minimal_f4.h) or
   [H7](config/umcub_config_minimal_h7.h) - to `boards/<your_board>/umcub_config.h` (the board directory can live in your own repository) and change the lines
   marked "yours": the part, the clock, the flash layout, how to get into the bootloader, one transport with its
   pins. Pins and slots are checked when CMake configures; a mistake stops it with a hint. SMP is on by default;
   `#define UMCUB_CFG_SMP_ENABLE 0` switches the transports to the lite upload protocol (smaller bootloader).
2. **Make your own signing key**: `imgtool keygen -t ecdsa-p256 -k keys/prod.pem` (the key in the repository is
   public, for development only).
3. **Build the bootloader**: `cmake -S umcub -B build/boot -DUMCUB_BOARD=$PWD/boards/<your_board>
   -DUMCUB_SIGNING_KEY=$PWD/keys/prod.pem && cmake --build build/boot`.
4. **Adapt the application**: link it for the primary slot (`tools/host/umcub_image.py info --board ...` prints
   where) and sign it with the same key - with CMake (`umcub::app`) or from an IDE
   ([application](docs/application.md)).
5. **Program both** `.hex` files; for production protect the bootloader sector (WRP) and enable readout protection.

Everything about boards, including external controllers and transports of your own: [Your own board](docs/board.md).
A series that has no port yet: [Porting](docs/porting.md).

## Getting into the bootloader

The bootloader starts the application unless one of these says otherwise:

- **a button** held during reset (`UMCUB_CFG_ENTRY_GPIO`);
- **a request from the application** (`umcub_enter_bootloader()`), e.g. on a command it receives;
- **no valid image** - nothing to start, so it waits for one;
- **a wait after reset** (`UMCUB_CFG_ENTRY_WAIT_MS`, off by default): every enabled interface listens that long for
  an SMP request or a text command such as `b`. Pick the length for the slowest interface you want to use: UART and
  CAN answer at once, Ethernet needs about 2 s for the link plus DHCP, USB needs the host to enumerate the device
  (several seconds, or leave the window off with USB); every boot is delayed by the window.

In the bootloader (recovery mode) every enabled interface accepts updates until a reset (or, with
`UMCUB_CFG_RECOVERY_TIMEOUT_MS`, until it has been idle that long).

A status LED (`UMCUB_CFG_STATUS_LED`, about 230 bytes) shows it without a log: slow blink (1 Hz) while the bootloader
listens, fast flicker while data arrives, N blinks and a pause when transport N did not come up (1 UART, 2 USB,
4 CAN, 5 Ethernet, 7 board transport: no transceiver, no PHY clock, ...). On a plain boot it stays dark.

## Updating the application

| From | Tool | Slot |
|---|---|---|
| recovery, SMP (UART, USB CDC, UDP, CAN) | `smpmgr`, `mcumgr`, `tools/host/smp_can.py` | primary by default; secondary with `UMCUB_CFG_SMP_DIRECT_UPLOAD` |
| recovery, USB DFU | `dfu-util -a <image> -D app.signed.bin -R` | secondary, test boot |
| recovery without SMP | `tools/host/umcub_lite.py` | either (`--slot`) |
| the running application | `umcub_slot_*` (your own protocol) | secondary (default) or any slot it does not run from |

Two ways to install an image:
- **into the primary slot**: it replaces the old image at once - quick, nothing to swap, but no way back if the
  new image misbehaves (it still has to be signed and valid, or the bootloader stays in recovery);
- **into the secondary slot**: the next boot installs it (swap, or a copy in overwrite mode). In the swap modes it
  runs as a test once: the application confirms it (`umcub_confirm()`) or the next reset brings the old one back.

Details: [Recovery mode](docs/recovery.md), [application](docs/application.md).

## Documentation

Using umcub:

| Page | What is in it |
|---|---|
| [Your own board](docs/board.md) | board configuration, board type and node address, reference boards and their variants |
| [The application side](docs/application.md) | `umcub::app`, `umcub_add_application()`, IDE projects (STM32CubeIDE, Keil MDK), CMake presets |
| [Recovery mode](docs/recovery.md) | updating over SMP, text commands, lite upload and board protocols, verify / hash / read |
| [umcub link](docs/link.md) | several devices on one bus: addressing, SECURE mode, `tools/host/umcub_link.py` |
| [Encrypted images and security](docs/security.md) | images encrypted for the device, what is and is not protected |
| [Configuration](docs/configuration.md) | where the options live, advanced options, dual-core, upgrade mode notes |

Reference:

| Page | What is in it |
|---|---|
| [Bootloader size](docs/size.md) | what every transport and feature adds, sectors per family, size levers |
| [Your own board: external controllers](docs/board.md#external-controllers-and-custom-transports) | board drivers and board transports (experimental) |
| [Porting](docs/porting.md) | adding an STM32 series, the source tree |
| [Testing](docs/testing.md) | build matrix, host tests, simulated devices, hardware tests |

## License

Apache License 2.0, see `LICENSE`. Third-party components keep their own licenses, see `THIRD_PARTY_NOTICES.md`.
