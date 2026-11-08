# TODO

## Ease of use: small flash

Seen with a user's F103CB (128 KiB, 52 K application): whether it fits took about 15 builds and knowing the F1
trailer reserve, the 256-byte info block and page rounding, and which options save how much (LTO, inspect,
SMP_PARAMS, lite).

- [ ] **LTO on the H7** (verified on the F1 and the F4): boot, SMP / DFU / Ethernet recovery, swap upgrade, both
  cores, stack (`tests/hw/stack_usage.py`), then drop "experimental" for it.

## Hardware verification (NUCLEO-H755ZI-Q)

- [ ] **CAN-FD on a bus** — 64-byte frames, data bit rate switch. Needs a CAN-FD transceiver on PD0/PD1 (classic CAN
  with ISO-TP + SMP is verified on a real bus).
- [ ] **Applications built without CMake** (docs/application.md) in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`). So far verified: `umcub_app_all.c` compiles
  with the documented include list (GCC, clang); `umcub image sign` gives the same image as CMake (header, payload,
  hash) from ELF and HEX; a tool-signed image boots on the board; the bootloader builds (F1, H7) with only the GCC
  inside STM32CubeIDE 2.2 (`UMCUB_TOOLCHAIN_PREFIX`). VS Code (CMake Tools, presets, the build matrix in the Testing
  view) works.
- [ ] CAN board driver and board transport with a real external controller (e.g. MCP2518FD, W5500). So far: build
  matrix (`tests/boards/custom_drivers`) and host test `umcub_host_board`; the Ethernet board driver is verified
  (ENC28J60 on the Black Pill).

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
- [ ] MCUboot v2.5.0 `boot_serial.c` includes `"../bootutil/src/bootutil_loader.h"` (direct-xip / RAM load), found
  only through the stopgap include directory `${MCUBOOT}/boot/boot_serial` in CMakeLists.txt. Fix prepared, not sent
  yet (branch boot-serial-loader-include: `"../src/bootutil_loader.h"`); drop the stopgap with the release that has it.
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

## GUI flasher

- [ ] **GUI** in its own repository (github.com/memExpert/umcub-gui, PySide6; its docs/plan.md holds the plan), a
  flasher only. It depends on the host library by the source archive of a tag
  (`umcub @ https://github.com/memExpert/umcub/archive/refs/tags/<tag>.zip#subdirectory=tools/host`: a git URL would
  clone every submodule, and on Windows some of their paths are too long) and holds no protocol or device logic: what
  it lacks goes into the library, so the CLI and the GUI behave the same. Library changes the plan still asks for:
  a signing function for SECURE sessions, board name catalog (JSON),
  build-only dependencies (`esp-idf-kconfig`) in an extra such as `umcub[build]`, bus hub for parallel uploads on one
  bus, CAN discovery.
- [ ] **SECURE link sessions signed by a server**, for users who log in instead of holding key files. The admin
  private key stays on the server (preferably in an HSM) and is never downloaded: one shared key cannot be revoked
  without reflashing the bootloaders, and one leak exposes every device built with it.
  - Flow: the GUI gets the device's challenge and sends the server the challenge, its ephemeral public key and its
    nonce. The server checks the account and its rights to that device, computes `th` itself, signs it, logs the
    session and returns the signature; the GUI sends `AUTH` as usual.
  - Library: `Link.authenticate()` takes a signing function next to `admin_key` as a file. No firmware change
    (size delta 0): the device sees an ordinary signature. The public half of the device key can come from the
    server over verified TLS.
  - Limits: blocking an account stops new sessions, an open one lasts until it ends. Needs internet while connecting;
    the request must finish within the challenge's 10 s lifetime. Rights per product or customer need a separate admin
    key in their bootloaders. Compromising the server equals leaking the key, though image signing still blocks
    foreign firmware while the signing key is kept elsewhere. Offline work would need short-lived certificates, a
    firmware change with a size cost: only if really required.
- [ ] Admin key with a passphrase or loaded from memory (OS key store): `load_key()` reads an unencrypted PEM by path.
  Needed once the GUI goes to operators with local key files.

## Not verified since the move to Kconfig

- [ ] **Windows**: configure, build and signing on a real Windows machine (docs/application.md); finding Python and
  imgtool in `.venv/Scripts` was only simulated.
- [ ] **H7 application RAM regions** (`tools/host/umcub/linker.py`): one set of sizes for every H7 part, taken from the
  H755 / H743, not compared with the datasheets of the others (H742). Only the application's linker fragments use them.
