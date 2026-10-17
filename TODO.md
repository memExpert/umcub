# TODO

## Hardware verification (NUCLEO-H755ZI-Q)

- [ ] **CAN / CAN-FD on a bus** — ISO-TP + SMP with `tools/host/smp_can.py`. Needs a transceiver on PD0/PD1 and a USB-CAN
  adapter. Only the FDCAN initialisation (internal loopback, timing registers) is verified. Also text commands and
  verify / hash / read over CAN (verified over UDP).
- [ ] **IDE workflow** (README "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`), armclang as preprocessor. So far
  verified: `umcub_app_all.c` compiles with the documented include list (GCC, clang); `tools/host/umcub_image.py` gives
  the same image as CMake (header, payload, hash) from ELF and HEX; a tool-signed image boots on the board.
- [ ] **Bluetooth UART bridge** (HC-05/HC-06 SPP on USART1 D0/D1, PC over RFCOMM): SMP upload, text commands,
  verify / hash over a slow, packetizing link without flow control.
- [ ] Board drivers / board transport with a real external controller (e.g. MCP2518FD, ENC28J60, W5500).
  So far: build matrix (`tests/boards/custom_drivers`) and host test `umcub_host_board`.

## Hardware verification (Blue Pill, STM32F103C8T6)

- [ ] bxCAN on a real bus (transceiver on PB9/PB8 + USB-CAN adapter, `tools/host/smp_can.py`). Verified so far in
  loopback + silent mode on the Blue Pill: initialisation (BTR 0xC11E0003 = 500 kbit/s at 36 MHz, sample point
  88.9 %), a request frame injected over SWD passes the filter, ISO-TP and the text command "i" run, the answer is
  sent with the TX id; registers checked against RM0008 / ES096 (manual-checker: 9 of 9 match).
- [ ] F105/F107 (PREDIV1, 25 MHz HSE, USB OTG FS, CAN2); XL-density bank 2.

## Known limitations / ideas

- [ ] Security review leftovers: (a) one device key per product - per-device keys (provisioning, key derived from
  the UID + a master on the host), H7 PCROP / secure-access area for the key; on the F1, RDP level 1 can be
  defeated by known attacks, so the key there is only as safe as the product's physical access;
  (b) `MCUBOOT_SWAP_SAVE_ENCTLV` (keep the wrapped key in the trailer instead of the plain AES key) for the swap
  modes; (c) F1 entropy quality not measured (ADC temperature noise + jitter).
- [ ] MCUboot submodule is v2.4.0: `main` already wipes the AES key on every path of `boot_serial_encryption.c`.
  Its in-place decryption still needs a sector-sized VLA on the stack, assumes uniform sectors and does not
  validate first, so `image/mcuboot/enc_image.c` replaces it (issue draft prepared). Drop our copy once upstream
  takes a port-provided buffer; update the submodule after the next release.
- [ ] Recovery over the network / CAN is not authenticated without `UMCUB_LINK_SECURE` (see README, "Mode notes
  and limitations").
- [ ] When the application writes a slot in the same flash bank it executes from, the CPU stalls for the duration of a
  sector erase (~2 s on the H7).
- [ ] Ports for other series (G4, F7, G0, L4).
- [ ] Bootloader build with Arm Compiler 6 / Keil (now GCC only: startup, linker templates, newlib overrides).
