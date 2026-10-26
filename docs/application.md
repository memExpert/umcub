# The application side

What the application links (umcub::app), how it confirms, updates and enters the bootloader, and how to use umcub
from an IDE.

## Application library

```cmake
set(UMCUB_BOARD nucleo_h755zi_q)   # same configuration as the bootloader
set(UMCUB_CORE cm7)
add_subdirectory(path/to/umcub/lib/umcub_app umcub)
target_link_libraries(app PRIVATE umcub::app)
umcub_app_linker_script(app IMAGE 0)        # or your own script with UMCUB_APP_FLASH_ORIGIN_0_0
umcub_sign_image(app VERSION 1.2.3)         # -> app.signed.bin / app.signed.hex (same signed image)
```

A build with applications for both cores adds `lib/umcub_app` for each and links each application with
`${UMCUB_APP_LIB}` (one target per core); with a single application `umcub::app` is enough.

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

## Using umcub from an IDE (STM32CubeIDE, Keil MDK)

|  | Bootloader | Application with `umcub::app` |
|---|---|---|
| CMake (command line, VS Code, CLion) | build, flash, debug | yes |
| STM32CubeIDE (GCC) | build with CMake; CubeIDE can drive the build and debug the ELF | yes |
| Keil MDK (Arm Compiler 6) | build with CMake (GCC); Keil can flash the .hex and debug the ELF | yes |

The bootloader is written once and rarely changes, so it does not have to live in the IDE. Build it with CMake (see the
[quick start](../README.md#quick-start-nucleo-h755zi-q)) and program the `.hex` with STM32CubeProgrammer or the IDE. Building the bootloader with Arm Compiler 6
is not supported: it depends on GCC startup code, GCC linker script templates and its own newlib stdio overrides.

The application is a normal CubeMX / IDE project. It needs four things: the right flash address, the library, a
signing step after the build and a debug setup that flashes the signed image.

**1. Where to link the application.** Ask the tool, with the same board configuration (and overlays) as the
bootloader:

```sh
python tools/host/umcub_image.py info --board nucleo_h755zi_q --core cm7
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
  unchanged. Until it runs the core is on the internal HSI oscillator, which is only calibrated to about 1 % at
  25 °C and drifts by several percent over temperature (more after soldering; one F411 measured -4.85 %): a UART at
  115200 baud needs the crystal (or a trimmed HSI), as the examples switch to the crystal first (`examples/bluepill_app`: 8 MHz, `examples/blackpill_app`: 25 MHz).

**2. The library.** Add one source file, `lib/umcub_app/umcub_app_all.c` (the whole library, family part included),
and these include paths (relative to the umcub checkout):

```
include
config
boards/<board>                         # the bootloader's umcub_config.h
port/<family>/include                  # e.g. port/stm32h7/include
port/include
src/image/mcuboot/include
third_party/mcuboot/boot/bootutil/include
```

CMSIS and LL headers come from the project (`Drivers/CMSIS/...`, `Drivers/STM32H7xx_HAL_Driver/Inc`); umcub's copies
in `third_party/` work as well. Defines: the device define (`STM32H755xx`) and, on dual-core parts,
`CORE_CM7` / `CORE_CM4`. CubeMX projects already have them. If the bootloader was built with
`UMCUB_CONFIG_PRE/POST` overlays, define the same macros, e.g. `UMCUB_CONFIG_POST="path/to/overlay.h"`. The
matrix build compiles this file with exactly this include list, using GCC and clang (Arm Compiler 6 is clang-based).

**3. Signing after the build.** `tools/host/umcub_image.py sign` takes the ELF / AXF (or an Intel HEX / raw `.bin`). It
checks that the image is linked at the slot origin and fits the slot, and writes `<name>.signed.bin` and
`<name>.signed.hex` next to it (and `<name>.bin`, the unsigned flat image, e.g. for `tests/hw/powerfail_test.py`). `umcub_sign_image()` in CMake runs the same tool on the ELF, so an IDE project and a
CMake build sign identically.

It reads `umcub_config.h` through a C preprocessor. The search order is `--cc`, then `$UMCUB_CC`, then
`arm-none-eabi-gcc`, then `armclang` (also in the default Keil folders), then `gcc` / `clang`. It needs Python 3
with imgtool's dependencies: `pip install -r tools/requirements.txt` (on Windows `py -m pip install ...`); Intel HEX
input and output need nothing more.

- **STM32CubeIDE**: *Project → Properties → C/C++ Build → Settings → Build Steps → Post-build steps*. The command
  runs in the build folder, with the CubeIDE toolchain in `PATH`:
  ```
  python <umcub>/tools/host/umcub_image.py sign --board nucleo_h755zi_q --core cm7 --image 0 --version 1.2.3 ${ProjName}.elf
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
`tools/hw/h755_option_bytes.py`, not the CubeMX defaults. The bootloader starts the CM4 application first, then jumps
to the CM7 one, so the CubeMX start-up handshake finds the CM4 in STOP as it expects (CM4 enters STOP, the CM7
waits for `RCC_FLAG_D2CKRDY` to drop, configures the clocks and wakes the CM4 through HSEM 0). Verified on the
NUCLEO-H755ZI-Q with ST's `BootCM4_CM7` template (STM32CubeH7, NUCLEO-H745ZI-Q) and the CubeMX system file
`system_stm32h7xx_dualcore_boot_cm4_cm7.c`, signed with `umcub_image.py`, in `SINGLE_BOOT` and in `PER_CORE`
(there: `--core cm4 --image 0` with the per-core overlay). Without a valid CM4 image, the CM4 stays parked in the bootloader. In that case the CubeMX CM7 code that
waits for `RCC_FLAG_D2CKRDY` times out into `Error_Handler()`; make that wait non-fatal if the CM7 must run alone.

**Bootloader inside STM32CubeIDE (optional).** CubeIDE 2.x builds the bootloader from the CMake presets with the GCC,
CMake and Ninja it ships. Needed besides CubeIDE: Python with the `.venv` (`tools/setup.sh`; Windows: see
[below](#build-trees-cmake-presets-and-windows); imgtool embeds the keys into the bootloader) and the submodules.

1. *File → New → STM32 CMake Project* → *Project with existing CMake sources*. Untick *Use default location* and
   put the project **outside** the umcub folder (e.g. next to it), *Source directory* = the umcub folder; the build
   directory does not matter (the presets set it). On the next page pick the MCU of the board: one project per
   board, it is what the debugger uses.
2. Every preset of `CMakePresets.json` becomes a build configuration (*Project → Build Configurations → Set
   Active*). For your own board use the preset `custom`: *Properties → C/C++ Build → CMake Settings*, *CMake
   Preset* `custom`, *Other options* `-DUMCUB_BOARD=/path/to/boards/my_board -DUMCUB_SIGNING_KEY=/path/to/keys/prod.pem`
   (it builds in `build/custom`). CubeIDE reads only `CMakePresets.json`, not `CMakeUserPresets.json`.
3. Build each configuration you use once, close the project and run
   ```sh
   python tools/host/umcub_cubeide.py <project folder>
   ```
   then open the project again. The wizard shows the whole source tree and every build tree; the script adds
   resource filters (no `.venv`, `.git` or build trees other than those of the configurations, and in those only
   `.elf .map .hex .bin .ld`), which keeps refreshing fast, and one *Build Targets* entry per executable
   ("bluepill: bluepill_app" builds only that application). Run it again after adding a configuration; it
   replaces only what it added before.

The *Build Analyzer* shows the ELF selected in the project tree (under `umcub/build/<preset>`), so click the
bootloader or the application to see its memory use. Debug that ELF with an *STM32 C/C++ Application*
configuration.

## Build trees (CMake presets) and Windows

Every preset in `CMakePresets.json` builds the bootloader target `umcub_<board>_<core>` and, with
`UMCUB_BUILD_EXAMPLES=ON` (set in the presets), the example applications of that board in the same tree:
`build/<preset>/examples/<app>/<app>.{elf,map,signed.bin,signed.hex}`. An IDE using CMake presets (VS Code with
CMake Tools, CLion, ...) therefore lists the bootloader and the applications as targets, and every `.elf` has its
`.map` next to it for memory analysis tools. The examples remain standalone projects (`cmake -S examples/<app>`).

Presets: `h755-cm7` (one bootloader for both cores; `h755-cm7-debug`: the same at -Og), `h755-per-core-cm7` +
`h755-per-core-cm4` (a bootloader per core), `bluepill` (UART), `bluepill-usb` (USB CDC + DFU, larger bootloader), `custom` (your board:
`-DUMCUB_BOARD=<board directory>` or the environment variable `UMCUB_BOARD`; no examples). Presets of your own go
into `CMakeUserPresets.json` (not in git); CMake, VS Code and CLion read it, STM32CubeIDE does not.

On Windows (no `setup.sh`): `git submodule update --init`, `py -m venv .venv`,
`.venv\Scripts\pip install -r tools\requirements.txt -e third_party\mcuboot\scripts`; CMake finds imgtool in
`.venv\Scripts`. The IDE workflow is described in [Using umcub from an IDE](#using-umcub-from-an-ide-stm32cubeide-keil-mdk).
