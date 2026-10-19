# umcub — a portable MCUboot-based bootloader for STM32

umcub (**U**niversal **MCU**boot **B**ootloader) is a bootloader for STM32 microcontrollers built on
[MCUboot](https://github.com/mcu-tools/mcuboot):

- **Not tied to a series.** The boot logic, transports and application library use one hardware API
  (`port/include/umcub_port*.h`); everything series-specific lives in `port/stm32<fam>/`. Ports: STM32H7
  (reference board NUCLEO-H755ZI-Q, both cores, all transports) and STM32F1 (Blue Pill: UART, USB CDC/DFU, bxCAN).
- **One configuration file.** `boards/<board>/umcub_config.h` is the single source of truth. CMake runs it through
  the preprocessor to decide what to build: a disabled transport (e.g. USB together with tinyUSB) is not compiled at all.
- **Transports** (each enabled separately), all speaking SMP/mcumgr:
  - UART — SMP serial (`smpmgr`, `mcumgr --conntype serial`);
  - USB (tinyUSB) — CDC-ACM with SMP and/or DFU 1.1 (`dfu-util`);
  - CAN / CAN-FD — SMP over ISO-TP (`tools/host/smp_can.py`);
  - Ethernet — SMP over UDP on a minimal IPv4 stack with DHCP (`mcumgr --conntype udp`);
  - writing an image from the application itself (`umcub_slot_*`) into a selectable slot;
  - without SMP (about 4.5 K smaller): a lite upload protocol on the same transports (`tools/host/umcub_lite.py`) and/or
    the board's own protocol.
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

tools/hw/h755_option_bytes.py single    # the CM4 starts from the bootloader vector table and waits (pyOCD)
tools/hw/probe.py flash build/h755-cm7/umcub_nucleo_h755zi_q_cm7.hex \
               build/ex-cm7/h755_cm7_app.signed.hex build/ex-cm4/h755_cm4_app.signed.hex
```

On Windows (no `setup.sh`): `git submodule update --init`, `py -m venv .venv`,
`.venv\Scripts\pip install -r tools\requirements.txt -e third_party\mcuboot\scripts`; CMake finds imgtool in
`.venv\Scripts`. The IDE workflow is described in [Using umcub from an IDE](docs/application.md#using-umcub-from-an-ide-stm32cubeide-keil-mdk).

The bootloader logs to the ST-LINK virtual COM port (USART3, 115200). It enters recovery mode when B1 is held during
reset, when the application asks for it (key `b` in the example), or when there is no valid image.

```sh
smpmgr --port /dev/ttyACM0 image state-read                 # or: mcumgr --conntype serial ...
smpmgr --port /dev/ttyACM0 image upload build/ex-cm7/h755_cm7_app.signed.bin
smpmgr --port /dev/ttyACM0 image upload --slot 1 build/ex-cm4/h755_cm4_app.signed.bin
smpmgr --port /dev/ttyACM0 os reset
mcumgr --conntype udp --connstring=<ip>:1337 image upload build/ex-cm7/h755_cm7_app.signed.bin
smpmgr --ip <ip> image upload build/ex-cm7/h755_cm7_app.signed.bin              # UDP (default --mtu 1500)
dfu-util -a 0 -D build/ex-cm7/h755_cm7_app.signed.bin -R    # DFU: alt N = image N
tools/host/smp_can.py --channel can0 upload build/ex-cm7/h755_cm7_app.signed.bin
```

SMP serial recovery writes straight into the **primary** slot (this is how MCUboot works); the image number is the
`image` field (`smpmgr --slot`, `mcumgr -n`; 1 = CM4). The bootloader answers MCUMGR_PARAMETERS
(`UMCUB_CFG_SMP_PARAMS`), so smpmgr sizes serial packets to `UMCUB_CFG_SMP_MTU` by itself (default 1472 with
Ethernet - one full UDP datagram, what smpmgr sends over UDP - otherwise 1024); other hosts: `--line-buffers`
lines of 128 bytes within it. DFU and `umcub_slot_*` write into the **secondary** slot and mark the image for a test boot.

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
tools/host/app_upload.py /dev/ttyUSB0 build/ex-bp/bluepill_app.signed.bin   # from the running application ('u')
# (H755 CM7 example: --image 1 updates the CM4 image; --slot primary asks for the primary slot, refused while it runs)
```

There is no user button. Recovery mode starts on a request from the application (key `b` in the example), when there
is no valid image, or when `b` arrives within 300 ms after reset (`UMCUB_CFG_ENTRY_WAIT_MS`).
`--line-buffers 4` matches `UMCUB_CFG_SMP_MTU` = 512 (20 KiB SRAM).

USB (CDC with SMP and DFU, micro-USB connector) needs more room: preset `bluepill-usb` (overlay
`tests/configs/bluepill/usb.h`: bootloader 44 KiB, two 10 KiB slots; the example in the same tree uses the same
layout):

```sh
cmake --preset bluepill-usb && cmake --build --preset bluepill-usb
smpmgr --port /dev/ttyACM0 --line-buffers 4 image upload build/bluepill-usb/examples/bluepill_app/bluepill_app.signed.bin
dfu-util -a 0 -D build/bluepill-usb/examples/bluepill_app/bluepill_app.signed.bin -R
```

The F1 has no switchable D+ pull-up and the Blue Pill ties D+ to 3.3 V, so the board looks attached whenever it is
powered, and a host would try to enumerate a device that does not answer. The STM32F1 port therefore drives D+ low
(detached) from reset on, and whenever its USB is not running. That includes the application: an application that
uses USB itself must make PA12 an input before enabling its USB peripheral. The USB preset has no wait window
(`UMCUB_CFG_ENTRY_WAIT_MS 0`): USB would attach and vanish again on every boot (see the option in the template).
While a debugger holds the MCU in reset (`mode=UR`) D+ floats high again: flash with `mode=HOTPLUG`, or with the
USB cable unplugged.

CAN (bxCAN, classic CAN, ISO-TP like on the H7) on PB9 TX / PB8 RX with an external transceiver: overlay
`tests/configs/bluepill/can.h` (+2.4 K, still in the 32 KiB region), host side `tools/host/smp_can.py`. Not together with
USB: on the F103 both share the same 512 bytes of SRAM (the build refuses it).

## Build trees and IDEs (CMake presets)

Every preset in `CMakePresets.json` builds the bootloader target `umcub_<board>_<core>` and, with
`UMCUB_BUILD_EXAMPLES=ON` (set in the presets), the example applications of that board in the same tree:
`build/<preset>/examples/<app>/<app>.{elf,map,signed.bin,signed.hex}`. An IDE using CMake presets (VS Code with
CMake Tools, CLion, ...) therefore lists the bootloader and the applications as targets, and every `.elf` has its
`.map` next to it for memory analysis tools. The examples remain standalone projects (`cmake -S examples/<app>`).
In your own build that adds `lib/umcub_app` for two cores, link each application with `${UMCUB_APP_LIB}` (one target
per core); with a single application `umcub::app` is enough.

## License

Apache License 2.0, see `LICENSE`. Third-party components keep their own licenses, see `THIRD_PARTY_NOTICES.md`.
