# TODO

## Ease of use (next big step)

Goal: a user adds a board without reading the internals. Today, a board on a ported family (H7, F1, F4) is one
~40-line `umcub_config.h` (pins checked at build time); a new series is a port on the shared drivers (clock,
flash, sys, tables): the F411 took about 470 lines of C/H (2026-10-22).

- [ ] **1. Rest of the F4 port.** Other F4 parts (sector map and SRAM per part).
  G4 later: needs a branch in `fdcan.c` for its fixed message RAM layout. Examples: let a board declare its
  example applications instead of the `elseif` chain in CMakeLists.txt, and move what the Blue Pill and Black Pill
  examples share (info printing, the `u` upload protocol of tools/host/app_upload.py) into examples/common
  (needs both boards to test). The Blue Pill example still runs its UART on HSI (F103: +-1 % at 25 °C, more over
  temperature): switch it to the 8 MHz crystal like the Black Pill one.
- [ ] **2. Pins without AF numbers.** Today a board writes `UMCUB_PIN('A', 9, 7)` with the alternate function
  looked up in the datasheet. The configure-time pin check already has ST's pin data for the package
  (`UMCUB_CFG_MCU_PART`), so `UMCUB_PIN('A', 9)` could get its AF from the peripheral instance and signal
  (USART1_TX -> AF7) and pass it to the build; an explicit AF stays possible and is still checked. Configure-time
  only, no firmware size change. Open: how the value gets from CMake into the C config (generated header?), F1
  (no AF numbers, remap chosen by the port), IDE builds without CMake (`umcub_app_all.c` does not use pins).

## Hardware verification (NUCLEO-H755ZI-Q)

- [ ] **CAN-FD on a bus** — 64-byte frames, data bit rate switch. Needs a CAN-FD transceiver on PD0/PD1 (classic CAN
  with ISO-TP + SMP is verified on a real bus).
- [ ] **IDE workflow** (docs/application.md "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`), armclang as preprocessor. So far
  verified: `umcub_app_all.c` compiles with the documented include list (GCC, clang); `tools/host/umcub_image.py` gives
  the same image as CMake (header, payload, hash) from ELF and HEX; a tool-signed image boots on the board; the
  bootloader builds (F1, H7) with only the GCC inside STM32CubeIDE 2.2 (`UMCUB_TOOLCHAIN_PREFIX`); in CubeIDE 2.2
  the "STM32 CMake Project" wizard + `tools/host/umcub_cubeide.py` build every preset and the `custom` one (own
  board through *Other options*), single targets, Build Analyzer on the clicked ELF (Linux). Open: debugging from
  that project, CubeIDE on Windows, VS Code (CMake Tools shows no targets / build analysis yet).
- [ ] **Bluetooth UART bridge** (HC-05/HC-06 SPP on USART1 D0/D1, PC over RFCOMM): SMP upload, text commands,
  verify / hash over a slow, packetizing link without flow control.
- [ ] Board drivers / board transport with a real external controller (e.g. MCP2518FD, ENC28J60, W5500).
  So far: build matrix (`tests/boards/custom_drivers`) and host test `umcub_host_board`.

## Hardware verification (Blue Pill, STM32F103C8T6)

- [ ] F105/F107 (PREDIV1, 25 MHz HSE, USB OTG FS, CAN2); XL-density bank 2.

## Known limitations / ideas

- [ ] Security review leftovers: one device key per product - per-device keys. Not derived on the device from
  the UID + a master key (the master would then sit in every device): each device gets its own key at provisioning
  and the port hands it to MCUboot, which already has the hooks - `MCUBOOT_ENC_BUILTIN_KEY`
  (`boot_enc_retrieve_private_key()`, the host encrypts for that device's public key) and `MCUBOOT_HW_KEY` (hash
  of the signing key on the device, the key in the image TLV; several keys in open PR mcu-tools/mcuboot#2915).
  Key storage: H7 PCROP / secure-access area; on the F1, RDP level 1 can be defeated by known attacks, so the key
  there is only as safe as the product's physical access.
- [ ] MCUboot in-place decryption: `src/image/mcuboot/enc_image.c` still replaces MCUboot's
  (`boot_serial_encryption.c`): a sector-sized VLA on the stack (rejected upstream in #2631; 128 KiB on the H7),
  uniform sectors assumed, no validation first ("expected" in #2770). #2913 fixed only its key data cleanup.
- [ ] mcu-tools/mcuboot#2920 (sent 2026-10-22): SMP upload erases the last sector of a slot again after the upload
  when the image reached it (regression of bcc17b45, #1862). Until a release has it: the H7 trailer reserve is
  one sector + 1 KiB for the TLVs (`port/stm32h7` defaults), the Blue Pill's 2 KiB, the F4 has
  `UMCUB_CFG_ERASE_PROGRESSIVELY 0` and `config/umcub_config_check.h` refuses layouts where an image can end in
  the last sector. With the fix: back to 0x2000 on the H7 and 0x400 on the Blue Pill, progressive erase on the F4,
  drop that check.
- [ ] MCUboot v2.5.0 `boot_serial.c` includes `"../bootutil/src/bootutil_loader.h"` (direct-xip / RAM load), found
  only through the stopgap include directory `${MCUBOOT}/boot/boot_serial` in CMakeLists.txt. Fix prepared, not sent
  yet (branch boot-serial-loader-include: `"../src/bootutil_loader.h"`); drop the stopgap with the release that has it.
- [ ] Recovery over the network / CAN is not authenticated without `UMCUB_LINK_SECURE` (see docs/configuration.md, "Mode notes
  and limitations").
- [ ] When the application writes a slot in the same flash bank it executes from, the CPU stalls for the duration of a
  sector erase (~2 s on the H7).
- [ ] `umcub_port_millis()` loses the time of a flash erase in the bank the bootloader runs from: the CPU stalls,
  SysTick_Handler (port/arch/cortexm/cortexm_sys.c) cannot run, at most one tick stays pending. Seen on the F411
  (2026-10-23): a 128 KiB sector erase (~1 s) showed as 6 ms in the log. Waits still end (BSY is polled), but
  timeouts and log times around erases run short (recovery timeout, link session, SMP pacing). Probably the same on
  the H7 and F1. Fix idea: catch up from the SysTick counter (COUNTFLAG / VAL) or a free-running timer after an
  erase, or let the flash driver add the erase time it waited.
- [ ] Ports for other series (G4, F7, G0, L4).
- [ ] Later: TrustZone (ARMv8-M: STM32L5, U5, H5, WBA). Bootloader, keys and crypto in the Secure world, the
  application Non-secure, so even a compromised application cannot read the device key (today RDP/WRP/PCROP only
  keep a debugger out). A new kind of port: separate Secure / Non-secure builds and linker scripts, SAU and GTZC
  setup, option byte TZEN, the application API (`umcub_confirm()` ...) through non-secure-callable entries.
  ST covers these parts with OEMiRoT / STiRoT (also MCUboot); worth it if users want our transports and bus
  update (CAN, RS485 link, Ethernet) with hardware key isolation - start with one H5 or U5 board.
- [ ] Bootloader build with Arm Compiler 6 / Keil (now GCC only: startup, linker templates, newlib overrides).

## Code review leftovers (refactoring, size, speed)

Larger changes:
- [ ] Zero-copy receive paths: ISO-TP reassembles straight into the mux packet buffer (saves an MTU of RAM:
  ~0.5 K on the F1, ~1.5 K on the H7); Ethernet RX parses in the DMA buffer and TX builds in it (~3.3 K RAM on
  the H7, two frame copies per packet); link DATA frames decrypt into the mux buffer (~0.5 K on SECURE builds).
