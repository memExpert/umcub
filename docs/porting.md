# Porting

Adding an STM32 series and the source tree.

## Adding a series

1. `cmake/families/stm32<fam>.cmake`: CPU flags, sources (with `port/arch/cortexm/cortexm_sys.c` and
   `cortexm_common.c`), the `cmsis_device_<fam>` and LL driver submodules.
2. `port/stm32<fam>/`: `include/umcub_family_defaults.h` (flash write unit, sector size - `UMCUB_FAMILY_UNIFORM_SECTOR`,
   or `UMCUB_FAMILY_SECTOR_AT(addr)` where sectors differ in size, see `port/stm32f4` - erased value if not 0xFF),
   `include/umcub_family_cmsis.h` (the device header), `include/umcub_family_app.inc` (port sources of the
   application library), register-level flash driver (LL has none), clocks (including restoring the reset state
   before the jump), reset cause, GPIO / IWDG, linker templates, then the optional peripherals (UART, CAN, ETH, USB).
   The Cortex-M part is shared (`port/arch/cortexm/`: SysTick time base, deinit of NVIC and the peripherals the drivers
   registered, reset, jump, UID): `umcub_port_init()` calls `umcub_cm_save_clocks()` first and `umcub_cm_start()`
   after the clock setup, `umcub_port_deinit()` calls `umcub_cm_deinit()`. Peripherals that are the same on every
   STM32 come from `port/arch/stm32/`: `stm32_common.h` (IWDG with the family's LSI frequency, UART rings) and the
   drivers of peripherals several series share. UART: add `usart_v2.c` (ISR/ICR/RDR/TDR: H7, G4, L4, F7, ...) or
   `usart_v1.c` (SR/DR: F1, F2, F4, L1) to the family's sources and give `stm32_usart_get()` (the instance's
   registers, IRQ, clock bit and kernel clock, its TX / RX pins set up) plus the vectors calling `stm32_usart_isr()`
   (see `port/stm32h7/uart.c`, `port/stm32f1/uart.c`). bxCAN (classic CAN: F1, F2, F4, F7, L4, F0, F3): add
   `bxcan.c` and give `stm32_bxcan_get()` (CAN1 with its pins set up, see `port/stm32f1/can.c`). FDCAN (H7, G4,
   H5, U5, L5): add `fdcan.c` and give `stm32_fdcan_get()` (controller, kernel clock selected, pins set up, see
   `port/stm32h7/can.c`); the message RAM layout comes from the device header (H7: configurable; a part with the
   fixed layout, e.g. G4, needs its branch in `fdcan.c`). Entropy (SECURE link, encrypted images): with an RNG add
   `rng.c` and give `stm32_rng_clock_on()` (see `port/stm32h7/rng.c`); without one, `umcub_port_entropy()` of
   your own (the F1 and F4 sample ADC noise, `port/stm32f1/entropy.c`, measured with `tests/hw/entropy_test.py`); a family without one sets
   `UMCUB_FAMILY_UNSUPPORTED_ENTROPY` in its CMake file (like `_CAN`, `_ETH`, `_USB`) so configure refuses the
   features that need it. USB: give `umcub_port_usb_init()` / `_deinit()` (48 MHz clock, pins, controller clock;
   returns the tinyUSB rhport) and the IRQ vector calling `umcub_usb_irq_handler()`, and name the tinyUSB driver
   in the CMake file (`UMCUB_FAMILY_TUSB_MCU`, `UMCUB_FAMILY_TUSB_SOURCES`): OTG FS (dwc2) see `port/stm32f4/usb.c`,
   `port/stm32h7/usb.c`, the USB FS device (fsdev) `port/stm32f1/usb.c`. Big sectors: see `UMCUB_CFG_ERASE_PROGRESSIVELY` / `UMCUB_CFG_TRAILER_RESERVE`
   ([Your own board](board.md#adding-a-board), "Slots").
3. `boards/<board>/umcub_config.h` (and optionally `umcub_board.c`).
4. Add the new configuration to `tools/build/build_matrix.sh`; an example application goes into `examples/`, the
   board's `umcub_examples.cmake` and the `examples` list of the build matrix (`examples/blackpill_app` on
   `examples/common/umcub_example.c` is the one to start from, see [Your own board](board.md#adding-a-board), step 7).

Rules: no `stm32*.h` / LL includes outside `port/<family>/`; all series differences stay in the port; RAM that must
survive a reset (handoff area) is written with 32-bit stores only (ECC SRAM loses narrower writes on reset).
Check pin mappings against the board manual, alternate functions against the datasheet, and the errata for every
peripheral a driver touches.

## Layout

Layers, bottom to top; a directory uses only its own and lower layers (checked by `tools/build/check_layers.py`
in the build matrix):

```
include/         L0  ABI shared with the application: umcub.h (app API), umcub_handoff.h
config/          L0  template, defaults, types, compile-time checks
port/            L1  hardware: include/ (port API umcub_port*.h), arch/cortexm/ (SysTick, deinit, jump, waits,
                     common linker sections), arch/stm32/ (drivers of ST peripherals that several series share: USART v1
                     and v2, bxCAN, FDCAN, RNG; IWDG, UART rings),
                     port/stm32h7/, port/stm32f1/, port/stm32f4/ (family ports + linker templates)
src/             the bootloader itself:
  util/          L2  log, random (CSPRNG), state (handoff RAM, node address), libc (snprintf, assert), crc16/base64,
                     byte order, version, link keys (generated)
  image/         L3  image slots: inspect (verify/hash/read), slot writer + app API (shared with umcub::app),
                     mcuboot/ = MCUboot glue (mcuboot_config.h, flash map backend, hooks, image encryption, shims)
  transport/     L4  channels: uart, usb (tinyUSB CDC/DFU), can (ISO-TP), net (IPv4/UDP/DHCP), registry
  proto/         L5  mux (SMP, lite upload, board frames, recovery loop), umcub link, lite upload, text commands,
                     SMP inspection group
  boot/          L6  main (entry decision, boot_go, jump), startup, info block
boards/          board configurations (umcub_config.h, optional umcub_board.c, umcub_examples.cmake, variants/)
lib/umcub_app/   application library (umcub::app); umcub_app_all.c = the whole library as one file for IDEs
examples/        CM7 / CM4 applications for NUCLEO-H755ZI-Q, Blue Pill and Black Pill applications; common/ = the shared console
tools/           setup.sh, requirements.txt, keys/ (development keys)
tools/host/      host tools: umcub_image.py (slot addresses, signing for IDE projects), umcub_inspect.py,
                 umcub_link.py, umcub_lite.py (lite upload), smp_can.py, umcub_keys.py
                 (umcub link keys as C arrays), umcub_smp.py (SMP helpers of the host tools, on smp / smpclient),
                 umcub_bus.py (their connections: --port / --udp / --can options, serial lines, UDP, ISO-TP)
tools/ide/       umcub_cubeide.py (filters and build targets for an STM32CubeIDE CMake project)
tools/build/     build_matrix.sh, size_table.py (size tables), check_docs.py (README / docs consistency),
                 check_layers.py (layering), check_pins.py (configured pins vs ST pin data, fills in AFs, run by CMake)
tools/hw/        SWD through probe.py (STM32_Programmer_CLI or pyOCD): probe.py (flash / read / write / erase /
                 reset), h755_option_bytes.py, udev/
tests/hw/        hardware tests on the bench: powerfail_test.py, stack_usage.py, entropy_test.py
                 (+ entropy_sampler_f1.c / _f4.c: ADC entropy measured from SRAM), dhcp_server.py (DHCP server
                 for one direct link)
tests/host/      host tests
tests/configs/   overlays of the build matrix: mode/, transport/, feature/ (board variants: boards/<b>/variants/)
tests/tools/     helpers for the build matrix (image comparison, check_pins_test.py: pin check cases)
tests/boards/    build-matrix boards (custom_drivers: board drivers + board transport)
```

Moved since (code written against an older tree): `boot/`, `image/`, `proto/`, `transport/`, `util/` -> `src/...`;
by the layer restructure: `lib/umcub_app/include` -> `include`,
`mcuboot_port/include` -> `src/image/mcuboot/include` (IDE include list above); in board files `umcub_boot.h` ->
`umcub_state.h`, and `umcub_proto_user()` / `umcub_proto_reply()` are declared in `umcub_proto.h` instead of
`umcub_transport.h`; config overlays `tools/config/<x>.h` -> `tests/configs/<group>/<x>.h`; host tools
`tools/<x>.py` -> `tools/host/<x>.py`.
