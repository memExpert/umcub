# TODO

## Ease of use (next big step)

Goal: a user adds a board without reading the internals. Today, a board on a ported family (H7, F1) is one
~40-line `umcub_config.h` (pins checked at build time); a new series is a port of about 300-500 lines on the
shared drivers (clock, flash, sys, tables), not yet proven on a series that was not ported before.

- [ ] **1. Proof port on the shared drivers.** The drivers per ST IP block are in `port/arch/stm32/` (USART v1/v2,
  bxCAN, FDCAN, RNG, IWDG, rings; USB is tinyUSB plus a small glue file); F1 and H7 keep only clock, flash, sys and
  small tables (F1 port about 620 lines of C). Open: a series that was not ported before, on these drivers: STM32F411
  when the board arrives (USART v1, USB OTG FS = the H7 dwc2 glue, no CAN, no RNG -> ADC entropy like the F1, flash
  sectors 16/16/16/16/64/128 KiB -> bootloader in at most 3 sectors = 48 KiB). G4 later: needs a branch in `fdcan.c`
  for its fixed message RAM layout.

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
- [ ] MCUboot submodule is v2.5.0-rc1: move it to the first release after v2.5.0-rc2 (our fixes mcu-tools/mcuboot#2912
  and #2913 were merged into main on 2026-10-21, after rc2). Then:
  - drop the refusal of an encrypted upload into the primary slot of image 1+ (`image > 0` in
    `umcub_enc_upload_check()`, src/image/mcuboot/enc_image.c): MCUboot now decrypts it (#2912);
  - tests/host/test_dual_enc.c: expect that upload to be decrypted (rc 0, both images boot) instead of rc 3;
  - docs/security.md ("Encrypted images"): image 1 works like image 0.

  `src/image/mcuboot/enc_image.c` still replaces MCUboot's in-place decryption (`boot_serial_encryption.c`): a
  sector-sized VLA on the stack (rejected upstream in #2631; 128 KiB on the H7), uniform sectors assumed, no
  validation first ("expected" in #2770). #2913 fixed only its key data cleanup.
- [ ] Recovery over the network / CAN is not authenticated without `UMCUB_LINK_SECURE` (see docs/configuration.md, "Mode notes
  and limitations").
- [ ] When the application writes a slot in the same flash bank it executes from, the CPU stalls for the duration of a
  sector erase (~2 s on the H7).
- [ ] Ports for other series (G4, F7, G0, L4).
- [ ] Bootloader build with Arm Compiler 6 / Keil (now GCC only: startup, linker templates, newlib overrides).

## Code review leftovers (refactoring, size, speed)

Larger changes:
- [ ] Zero-copy receive paths: ISO-TP reassembles straight into the mux packet buffer (saves an MTU of RAM:
  ~0.5 K on the F1, ~1.5 K on the H7); Ethernet RX parses in the DMA buffer and TX builds in it (~3.3 K RAM on
  the H7, two frame copies per packet); link DATA frames decrypt into the mux buffer (~0.5 K on SECURE builds).
