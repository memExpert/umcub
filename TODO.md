# TODO

## Ease of use (next big step)

Goal: a user adds a board without reading the internals. Today, a board on a ported family (H7, F1) is one
~40-line `umcub_config.h` (pins checked at build time), but a new series (e.g. G4) means
writing a whole port (900-1600 lines).

- [ ] **1. Drivers per ST IP block, not per family.** Stage A done (port/arch/stm32: USART v1/v2, bxCAN, FDCAN,
  RNG, IWDG, rings; verified on both boards). Open: the G4 (or the coming F411) port on it as the proof. Before: The same peripherals repeat across series: USART v2
  (H7 = G4 = L4 = F7: ISR/ICR/RDR/PRESC), USB FS device (F1 = G4 = L4), bxCAN (F1 = F4 = L4), FDCAN (H7 and G4:
  same core, G4 has a fixed message RAM layout), RNG. Move them from `port/stm32h7/`, `port/stm32f1/` to
  `port/arch/stm32/` (`usart_v2.c`, `usb_fs.c`, `bxcan.c`, `fdcan.c`, `rng.c`; IWDG and the RX ring are already
  there), each family keeps clock, flash, sys (reset cause, GPIO, UID), defaults, linker templates and a small
  table (instance -> registers, IRQ, bus clock). Target: a new series ~300-400 lines. Proof: port the G4 on it
  (stm32-port-engineer + manual-checker against the RM / errata), with a NUCLEO-G474 if one is available.

## Hardware verification (NUCLEO-H755ZI-Q)

- [ ] **CAN-FD on a bus** — 64-byte frames, data bit rate switch. Needs a CAN-FD transceiver on PD0/PD1 (classic CAN
  with ISO-TP + SMP is verified on a real bus).
- [ ] **IDE workflow** (docs/application.md "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`), armclang as preprocessor. So far
  verified: `umcub_app_all.c` compiles with the documented include list (GCC, clang); `tools/host/umcub_image.py` gives
  the same image as CMake (header, payload, hash) from ELF and HEX; a tool-signed image boots on the board.
- [ ] **Bluetooth UART bridge** (HC-05/HC-06 SPP on USART1 D0/D1, PC over RFCOMM): SMP upload, text commands,
  verify / hash over a slow, packetizing link without flow control.
- [ ] Board drivers / board transport with a real external controller (e.g. MCP2518FD, ENC28J60, W5500).
  So far: build matrix (`tests/boards/custom_drivers`) and host test `umcub_host_board`.

## Hardware verification (Blue Pill, STM32F103C8T6)

- [ ] F105/F107 (PREDIV1, 25 MHz HSE, USB OTG FS, CAN2); XL-density bank 2.

## Known limitations / ideas

- [ ] Security review leftovers: (a) one device key per product - per-device keys (provisioning, key derived from
  the UID + a master on the host), H7 PCROP / secure-access area for the key; on the F1, RDP level 1 can be
  defeated by known attacks, so the key there is only as safe as the product's physical access;
  (b) F1 entropy quality not measured (ADC temperature noise + jitter).
- [ ] MCUboot submodule is v2.5.0-rc1 (move to the final v2.5.0 when it is out). Its in-place decryption
  (`boot_serial_encryption.c`) needs a sector-sized VLA on the stack, assumes uniform sectors and does not validate
  first, so `src/image/mcuboot/enc_image.c` replaces it (VLA rejected upstream in #2631, success before decryption
  "expected" in #2770). Two bugs confirmed on main e19ad039 (ASan / host): `boot_enc_zeroize(&enc_data)` writes past
  one `enc_key_data`; an image without its (unprotected) key TLV is decrypted without a key. Reported: #2911, fix
  PR #2913.
- [ ] Encrypted SMP upload into the primary slot of image 1 (H755 CM4) stays ciphertext: MCUboot's `bs_upload()`
  decrypts only for `FLASH_AREA_IMAGE_PRIMARY(0)`; then no image boots in stock MCUboot (umcub retries image 0, the
  CM4 stays parked). Reproduced on the host on main e19ad039 and fixed by comparing with every image's primary slot
  (both upload numberings). Reported: mcu-tools/mcuboot#2910, fix PR #2912.
  Until it is merged: encrypted CM4 updates through the secondary slot (DFU alt 1, SMP direct upload, app, lite).
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
