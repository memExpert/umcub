# TODO

## Hardware verification (NUCLEO-H755ZI-Q)

- [ ] **CAN-FD on a bus** — 64-byte frames, data bit rate switch. Needs a CAN-FD transceiver on PD0/PD1 (classic CAN
  with ISO-TP + SMP is verified on a real bus).
- [ ] **IDE workflow** (README "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
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

## Code review leftovers (refactoring, size, speed)

Larger changes:
- [ ] Image signing in one place: `umcub_sign_image()` (cmake/umcub_imgtool.cmake) calls `tools/host/umcub_image.py`
  instead of mirroring its imgtool arguments; the matrix comparison of both outputs then goes away.
- [ ] Zero-copy receive paths: ISO-TP reassembles straight into the mux packet buffer (saves an MTU of RAM:
  ~0.5 K on the F1, ~1.5 K on the H7); Ethernet RX parses in the DMA buffer and TX builds in it (~3.3 K RAM on
  the H7, two frame copies per packet); link DATA frames decrypt into the mux buffer (~0.5 K on SECURE builds).
- [ ] proto/mux.c: one line assembler for NLIP, frame, link and text lines instead of `pump_stream` +
  `pump_link_stream`; NLIP lines decoded in the mux and fed to boot_serial like packets, so `line_rename_params()`
  goes away. Better: upstream MCUboot lets unknown group-0 commands reach the per-user hook
  (`MCUBOOT_PERUSER_MGMT_GROUP_ENABLED`), which removes both rename functions (see the MCUboot upstream item).
- [ ] image/: one slot -> flash area helper with the range checks (umcub.c `slot_area`, inspect.c, slot_writer.c,
  hooks.c, lite.c) and one header -> version conversion (boot/main.c, image/umcub.c); the app library's
  running-slot tests (`running_slot`, `running_image`) on `executing_from()`.
- [ ] port/: `umcub_cm_periph_on(enr, rstr, bit)` for the ~15 clock enable + reset sequences; a table instead of the
  `switch (instance)` in the F1 UART init; shared IWDG start and UART RX ring in port/arch/cortexm.
- [ ] Host tools: shared CAN / serial / UDP argument helpers (`add_can_arguments()` like `probe.add_arguments()`,
  gives umcub_inspect.py CAN support); one serial / UDP line bus class for umcub_lite.py and umcub_link.py;
  shared helpers for tests/host test_link.c and test_link_secure.c; one source list for the three host test
  targets in tests/host/CMakeLists.txt; link_e2e.py on `umcub_link.parse_frame()`.
- [ ] Byte order helpers (BE + LE get/put) in one L0 header instead of the copies in link.c, lite.c, mux.c,
  isotp.c and net.h.

Behaviour / security trade-offs (as options):
- [ ] Validate the primary slot once: remember the hash of a fully validated image and check only the hash on
  later boots (like MCUboot's `MCUBOOT_VALIDATE_PRIMARY_SLOT_ONCE`, in the hooks). Saves ~0.6 s per boot on the F1.
- [ ] H7 ETH at 10 Mbit (ES0445 2.25.10): keep the FCS of errored frames and check it with `umcub_crc32()` instead of
  relying on the IP / UDP checksums (ARP and UDP without checksum are not covered now). Check RM0399 first.
- [ ] ISO-TP STmin 0xF1-0xF9 (100-900 us) waits 1-2 ms per frame: microsecond wait on SysTick / DWT.
- [ ] Boot log: TX ring drained by interrupt (overlaps the signature check, ~35 ms at 115200 on the F1) or MCUboot
  INF lines mapped to DBG (also saves their strings).
- [ ] Ethernet in the entry window: autonegotiation does not finish in a short window, so skip the PHY bring-up when
  `UMCUB_CFG_ENTRY_WAIT_MS` is below ~3 s.
- [ ] mux: release the stream lock on any `\n` in boot_serial's output, not only a separate 1-byte write.

Config / API cleanup:
- [ ] `net_if.gateway` is never read (replies go to the sender's MAC): drop it, or document
  `UMCUB_CFG_ETH_GATEWAY` as unused.
- [ ] `UMCUB_CFG_USB_DFU_IMAGE` is only range-checked (DFU alt N = image N), and its template comment about
  direct-xip is stale.
- [ ] `umcub_flash_write_align()` / `umcub_flash_erased_val()` return constants: use `UMCUB_FAMILY_WRITE_ALIGN` and
  0xFF.
- [ ] hooks.c re-implements MCUboot's `MUST_DECRYPT` test (`vector_ok`).

Tooling speed:
- [ ] tools/build/build_matrix.sh: build the configs in parallel; link_e2e.py scenarios in threads.
- [ ] tools/hw/powerfail_test.py with stm32cube: one CLI call per step (several -e/-d/-w32/-r32) or pyOCD, and erase
  only what is not rewritten (~15-25 s per fault point now).
- [ ] umcub_lite.py: default chunk = device MTU or several frames in flight (stop-and-wait with 256 B chunks now).
