# The application side

What the application links (umcub::app), how it confirms, updates and enters the bootloader, and how to build an
application without CMake.

## Application library

```cmake
set(UMCUB_BOARD nucleo_h755zi_q)   # same configuration as the bootloader
set(UMCUB_CORE cm7)
add_subdirectory(path/to/umcub/lib/umcub_app umcub)
umcub_add_application(app IMAGE 0 VERSION 1.2.3)   # -> app.signed.bin / app.signed.hex (same signed image)
```

`umcub_add_application()` links the library, links `app` for its slot and signs it. With a linker script of your own
use the steps it is made of:

```cmake
target_link_libraries(app PRIVATE umcub::app)
umcub_app_linker_script(app IMAGE 0)        # or your own script: INCLUDE umcub_app_img0_slot0.ld
umcub_sign_image(app VERSION 1.2.3)
```

A build with applications for both cores adds `lib/umcub_app` for each; `umcub_add_application()` links each
application with its core's library (`${UMCUB_APP_LIB}`).

What the library costs the application (with `--gc-sections` only what it calls is linked): `umcub_confirm()` and
`umcub_enter_bootloader()` add 92 bytes on the Blue Pill and the Black Pill (overwrite: nothing to confirm) and
2.5 K on the H755 CM7 (swap-scratch: confirming writes the slot trailer through MCUboot's bootutil and the flash
driver). Writing an image from the application (`umcub_slot_*`) adds the rest of the flash driver.

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
- writing a slot code is running from is refused (`UMCUB_EBUSY`): the application's own, and any slot the bootloader
  started an image from according to the handoff - on dual-core parts the CM7 application cannot erase the running
  CM4 image and vice versa (`UMCUB_SLOT_INACTIVE` picks the other slot of that image);
- in swap modes the secondary slot is refused (`UMCUB_EBUSY`) while the running image is still on test: the secondary
  then holds the previous image, the only way back;
- if the slot is in the same flash bank as the running code, the CPU stalls during a sector erase (up to ~2 s on the
  H7). Applications that write their own updates should keep the secondary slot in the other bank.

## Applications built without CMake

The bootloader builds with CMake and GCC only (it depends on GCC startup code and its own newlib stdio overrides); the
application can be a normal CubeMX / Makefile / Keil project. It needs four things from the bootloader's build tree
(`build/<x>/umcub_kconfig/`, or the same files from `python tools/host/umcub config generate --board <b> [--core c]
[--conf <fragment>]... --out <dir>`): the flash address, the library with the generated configuration, a signing step
after the build and flashing the signed image.

**1. Where to link the application.** The directory has `umcub_app_img<n>_slot<m>.ld`, the slot's `MEMORY` block with
CubeMX's region names (H7 CM7: `FLASH`, `DTCMRAM`, `RAM_D1`, `RAM_D2`, `RAM_D3`, `ITCMRAM`; otherwise `FLASH`, `RAM`),
the handoff area left out. Replace the `MEMORY { ... }` block of the project's linker script (`STM32xxxx_FLASH.ld`) with

```
INCLUDE umcub_app_img0_slot0.ld
```

and add the directory to the library paths (`-L`): the script then follows the bootloader's configuration. Signing
refuses an image whose vector table is not at the slot's `ORIGIN`, so a stale script does not go unnoticed. For other
linkers `umcub image info --board <b>` prints the values:

```
image 0 (CM7 application): slot 0x08020000 size 0x60000
  link at   ORIGIN 0x08020400  LENGTH 0x5DC00   (vector table = ORIGIN)
  GCC .ld   FLASH (rx) : ORIGIN = 0x08020400, LENGTH = 0x5DC00
  Keil      Target > IROM1: Start 0x08020400  Size 0x5DC00
...
keep free: 0x3800FF00..0x38010000 (bootloader handoff, kept over reset)
```

- Keil: *Options for Target → Target*: IROM1 start/size as printed, IROM2 unchecked (or the same region in your
  scatter file).
- In any case: do not place variables in the handoff area. CubeMX projects do not use SRAM4 by default.
- VTOR is set by the bootloader. Leave `USER_VECT_TAB_ADDRESS` undefined in `system_stm32*.c` (the CubeMX default).
- Also needed:
  - a console that reacts to single keys, as the examples do, needs its RX pin pulled up and must drop bytes with a
    framing or noise error: an unconnected pin reads garbage, and sooner or later that is a key (a Blue Pill went back
    to the bootloader within seconds this way);
  - the application must select the same power supply as `UMCUB_CFG_PWR_SUPPLY` (on the H7 it can only be set
    once per power-up; `HAL_PWREx_ConfigSupply` otherwise fails);
  - it must feed the watchdog if `UMCUB_CFG_WATCHDOG_MS` is set.

  The clocks are back in the reset state when the application starts, so the CubeMX `SystemClock_Config()` works
  unchanged. Until it runs the core is on the internal HSI oscillator, which is only calibrated to about 1 % at
  25 °C and drifts by several percent over temperature (more after soldering; one F411 measured -4.85 %): a UART at
  115200 baud needs the crystal (or a trimmed HSI), as the examples switch to the crystal first (`examples/bluepill_app`: 8 MHz, `examples/blackpill_app`: 25 MHz).

**2. The library.** Add one source file, `lib/umcub_app/umcub_app_all.c` (the whole library, family part included),
and these include paths (relative to the umcub checkout):

```
include
config
<build tree>/umcub_kconfig             # the generated umcub_config.h
port/<family>/include                  # e.g. port/stm32h7/include
port/include
src/image/mcuboot/include
third_party/mcuboot/boot/bootutil/include
```

CMSIS and LL headers come from the project (`Drivers/CMSIS/...`, `Drivers/STM32H7xx_HAL_Driver/Inc`); umcub's copies
in `third_party/` work as well. Defines: the device define (`STM32H755xx`) and, on dual-core parts,
`CORE_CM7` / `CORE_CM4`. CubeMX projects already have them. The
matrix build compiles this file with exactly this include list, using GCC and clang (Arm Compiler 6 is clang-based).

**3. Signing after the build.** `umcub image sign` takes the ELF / AXF (or an Intel HEX / raw `.bin`). It
checks that the image is linked at the slot origin and fits the slot, and writes `<name>.signed.bin` and
`<name>.signed.hex` next to it (and `<name>.bin`, the unsigned flat image, e.g. for `tests/hw/powerfail_test.py`). `umcub_sign_image()` in CMake runs the same tool on the ELF, so every build signs identically; the key is the configuration's.

It needs Python 3 with imgtool's dependencies and esp-idf-kconfig: `pip install -r tools/requirements.txt` (on Windows `py -m pip install ...`). The tool
runs straight from the checkout as `python <umcub>/tools/host/umcub ...` (installed: `umcub ...`); Intel HEX
input and output need nothing more.

```
python <umcub>/tools/host/umcub image sign --board <build tree>/umcub_kconfig --core cm7 --image 0 --version 1.2.3 app.elf
```

Other `sign` options:
- `--key other.pem` (default: the configuration's);
- `--slot 1` (direct-xip: the slot the image is linked for);
- `--confirm` and `--pad`;
- `--depends "(1,1.0.0)"`.

In direct-xip-revert mode it also writes `<name>.confirmed.{bin,hex}`.

**4. Flashing and debugging.** Debuggers download the unsigned ELF by default. The bootloader rejects it (no image
header) and stays in recovery mode. Download the signed `.hex` instead and take only the symbols from the ELF; execution
then goes reset → bootloader → application, and breakpoints in the application work as usual. Updates through the
bootloader (`smpmgr`, `mcumgr`, `dfu-util`, CAN, UDP) use `<name>.signed.bin`.

**Dual-core H745/H755 with CubeMX dual-core projects.** Each core's project signs its own image. With `SINGLE_BOOT`
that is `--core cm7 --image 0` for the CM7 and `--core cm4 --image 1` for the CM4. Program the option bytes with
`tools/hw/h755_option_bytes.py`, not the CubeMX defaults. The bootloader starts the CM4 application first, then jumps
to the CM7 one, so the CubeMX start-up handshake finds the CM4 in STOP as it expects (CM4 enters STOP, the CM7
waits for `RCC_FLAG_D2CKRDY` to drop, configures the clocks and wakes the CM4 through HSEM 0). Verified on the
NUCLEO-H755ZI-Q with ST's `BootCM4_CM7` template (STM32CubeH7, NUCLEO-H745ZI-Q) and the CubeMX system file
`system_stm32h7xx_dualcore_boot_cm4_cm7.c`, signed with `umcub image sign`, in `SINGLE_BOOT` and in `PER_CORE`
(there: `--core cm4 --image 0` with the fragment `variants/per_core.conf`). Without a valid CM4 image, the CM4 stays parked in the bootloader. In that case the CubeMX CM7 code that
waits for `RCC_FLAG_D2CKRDY` times out into `Error_Handler()`; make that wait non-fatal if the CM7 must run alone.

## Build trees (CMake presets) and Windows

Every preset in `CMakePresets.json` builds the bootloader target `umcub_<board>_<core>` and, with
`UMCUB_BUILD_EXAMPLES=ON` (set in the presets), the example applications of that board in the same tree:
`build/<preset>/examples/<app>/<app>.{elf,map,signed.bin,signed.hex}`. Every `.elf` has its `.map` next to it. The examples remain standalone projects (`cmake -S examples/<app>`).

Presets: `h755-cm7` (one bootloader for both cores; `h755-cm7-debug`: the same at -Og), `h755-per-core-cm7` +
`h755-per-core-cm4` (a bootloader per core), `bluepill` (UART), `bluepill-usb` (USB CDC + DFU, larger bootloader), `custom` (your board:
`-DUMCUB_BOARD=<board directory>` or the environment variable `UMCUB_BOARD`; no examples). Presets of your own go
into `CMakeUserPresets.json` (not in git).

On Windows (no `setup.sh`): `git submodule update --init`, `py -m venv .venv`,
`.venv\Scripts\pip install -r tools\requirements.txt -e third_party\mcuboot\scripts`; CMake finds imgtool in
`.venv\Scripts`.
