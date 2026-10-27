# TODO

## Ease of use (next big step)

Goal: a user adds a board without reading the internals. Today, a board on a ported family (H7, F1, F4) is one
~40-line `umcub_config.h` (pins checked at build time, alternate functions filled in); a new series is a port on the shared drivers (clock,
flash, sys, tables): the F411 took about 470 lines of C/H (2026-10-22).

- [ ] **1. Build matrix on CTest.** `tools/build/build_matrix.sh` (bash) drives 50 bootloader build trees, presets,
  examples, the IDE compile, `check_layers`, the pin check cases, host tests (already CTest) and the link e2e. A
  `tests/CMakeLists.txt` with one test per configuration (`ctest --build-and-test` into its own tree) plus the checks
  as tests gives `ctest -j`, `-R`, `--rerun-failed`, `--output-junit` and IDE test lists; the warning / flash fill
  checks of one configuration move to a small wrapper script. After the open points of the simplification report.

- [ ] **2. Production.** Keys, RDP / WRP, option bytes and the log are spread over a few sentences in four pages,
  without commands for the F1 / F4, and the development keys only warn. A `docs/production.md` checklist (own
  keys, log off, board type, watchdog, WRP on the bootloader sectors, RDP 1, H755 option bytes, how to check;
  the `STM32_Programmer_CLI -ob` lines checked against the reference manuals) and `-DUMCUB_PRODUCTION=ON`, which
  turns the development keys, `UMCUB_CFG_LOG_LEVEL > 0`, `UMCUB_CFG_LINK_REQUIRE_RDP 0`, `UMCUB_CFG_LINK_MIXED_OK`
  and `UMCUB_CFG_USB_DFU_ALLOW_UNAUTH` into build errors (no firmware change).

## Hardware verification (NUCLEO-H755ZI-Q)

- [ ] **CAN-FD on a bus** — 64-byte frames, data bit rate switch. Needs a CAN-FD transceiver on PD0/PD1 (classic CAN
  with ISO-TP + SMP is verified on a real bus).
- [ ] **IDE workflow** (docs/application.md "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`), armclang as preprocessor. So far
  verified: `umcub_app_all.c` compiles with the documented include list (GCC, clang); `umcub image sign` gives
  the same image as CMake (header, payload, hash) from ELF and HEX; a tool-signed image boots on the board; the
  bootloader builds (F1, H7) with only the GCC inside STM32CubeIDE 2.2 (`UMCUB_TOOLCHAIN_PREFIX`); in CubeIDE 2.2
  the "STM32 CMake Project" wizard + `tools/ide/umcub_cubeide.py` build every preset and the `custom` one (own
  board through *Other options*), single targets, Build Analyzer on the clicked ELF (Linux). Open: debugging from
  that project, CubeIDE on Windows, VS Code (CMake Tools shows no targets / build analysis yet).
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
