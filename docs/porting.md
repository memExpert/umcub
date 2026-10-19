# Porting

Adding an STM32 series and the source tree.

## Adding a series

1. `cmake/families/stm32<fam>.cmake`: CPU flags, sources (with `port/arch/cortexm/cortexm_sys.c` and
   `cortexm_common.c`), the `cmsis_device_<fam>` and LL driver submodules.
2. `port/stm32<fam>/`: `include/umcub_family_defaults.h` (flash write unit, sector size, erased value if not 0xFF),
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
   your own (the F1 samples ADC noise, `port/stm32f1/entropy.c`).
3. `boards/<board>/umcub_config.h` (and optionally `umcub_board.c`).
4. Add the new configuration to `tools/build/build_matrix.sh`.

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
                     port/stm32h7/, port/stm32f1/ (family ports + linker templates)
src/             the bootloader itself:
  util/          L2  log, random (CSPRNG), state (handoff RAM, node address), libc (snprintf, assert), crc16/base64,
                     byte order, version, link keys (generated)
  image/         L3  image slots: inspect (verify/hash/read), slot writer + app API (shared with umcub::app),
                     mcuboot/ = MCUboot glue (mcuboot_config.h, flash map backend, hooks, image encryption, shims)
  transport/     L4  channels: uart, usb (tinyUSB CDC/DFU), can (ISO-TP), net (IPv4/UDP/DHCP), registry
  proto/         L5  mux (SMP, lite upload, board frames, recovery loop), umcub link, lite upload, text commands,
                     SMP inspection group
  boot/          L6  main (entry decision, boot_go, jump), startup, info block
boards/          board configurations (umcub_config.h, optional umcub_board.c)
lib/umcub_app/   application library (umcub::app); umcub_app_all.c = the whole library as one file for IDEs
examples/        CM7 / CM4 applications for NUCLEO-H755ZI-Q, Blue Pill application
tools/           setup.sh, requirements.txt, keys/ (development keys)
tools/host/      host tools: umcub_image.py (slot addresses, signing for IDE projects), umcub_inspect.py,
                 umcub_link.py, umcub_lite.py (lite upload), smp_can.py, app_upload.py, umcub_keys.py
                 (umcub link keys as C arrays), umcub_smp.py (SMP helpers of the host tools, on smp / smpclient),
                 umcub_bus.py (their connections: --port / --udp / --can options, serial lines, UDP, ISO-TP)
tools/build/     build_matrix.sh, size_table.py (size tables), check_docs.py (README / docs consistency),
                 check_layers.py (layering), check_pins.py (configured pins vs ST pin data, run by CMake)
tools/hw/        bench (SWD through probe.py: STM32_Programmer_CLI or pyOCD): probe.py (flash / read / write /
                 erase / reset), h755_option_bytes.py, dhcp_server.py (DHCP server for one direct link),
                 powerfail_test.py, stack_usage.py, udev/
tests/host/      host tests
tests/configs/   configuration overlays: mode/, transport/, feature/, bluepill/
tests/tools/     helpers for the build matrix (image comparison, check_pins_test.py: pin check cases)
tests/boards/    build-matrix boards (custom_drivers: board drivers + board transport)
```

Moved since (code written against an older tree): `boot/`, `image/`, `proto/`, `transport/`, `util/` -> `src/...`;
by the layer restructure: `lib/umcub_app/include` -> `include`,
`mcuboot_port/include` -> `src/image/mcuboot/include` (IDE include list above); in board files `umcub_boot.h` ->
`umcub_state.h`, and `umcub_proto_user()` / `umcub_proto_reply()` are declared in `umcub_proto.h` instead of
`umcub_transport.h`; config overlays `tools/config/<x>.h` -> `tests/configs/<group>/<x>.h`; host tools
`tools/<x>.py` -> `tools/host/<x>.py`.
