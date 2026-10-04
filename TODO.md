# TODO

## Hardware verification (NUCLEO-H755ZI-Q)

Done:

- [x] **UMCUB_DUALCORE_SINGLE_BOOT** — both cores start, the CM4 image is updated over SMP (slot 1), and the CM7 still
  boots with a broken CM4 image (the CM4 stays parked). Option bytes are written with `tools/h755_option_bytes.sh`
  through the RAM helper `tools/h7_ob`: STM32CubeProgrammer 2.23 hides `BOOT_CM4_ADD0`/`BCM4` for the H755.
- [x] **UMCUB_DUALCORE_PER_CORE** — CM4 bootloader validates its image, HSEM handshake, both applications run.
- [x] **Power loss during swap (swap-scratch)** — `tools/hw/powerfail_test.py` with a `UMCUB_CFG_TEST_FAULT_INJECT`
  build: reset in the middle of a sector erase / flash-word program at 88 points (44 upgrade, 44 revert, spread over
  all ~24 600 flash operations), 0 failures, both slots compared byte by byte.
- [x] **Watchdog** (`UMCUB_CFG_WATCHDOG_MS` = 2 s) — an application that feeds it runs without resets, one that does
  not is reset every ~2 s (cause: watchdog); a 200 KiB swap and an 8 s DFU download complete without a reset; idle
  recovery mode is not reset. The bootloader always feeds the IWDG (hardware watchdog option byte case).
- [x] **Recovery timeout** (`UMCUB_CFG_RECOVERY_TIMEOUT_MS` = 5 s) — no activity: reset into the application after
  exactly 5 s; a command every 2 s keeps the bootloader in recovery. Entry window `UMCUB_CFG_ENTRY_WAIT_MS` verified
  as well (`b` within the first 300 ms stays in the bootloader).
- [x] **verify / hash / read** over UART and USB CDC (`tools/umcub_inspect.py` and text commands); SMP readback of
  200 KiB in ~2 s; `dfu-util -U`.
- [x] UART (USART3 VCP and USART1 on Arduino D0/D1), USB CDC, USB DFU, entry pin B1, application request,
  swap-scratch upgrade / revert / confirm, text commands over UART and USB CDC.
- [x] Power-loss test for **swap-move and swap-offset** (`tools/hw/powerfail_test.py`, swap-offset with
  `--revert-copy`): 16 points each over upgrade and revert, 0 failures.
- [x] Upgrade modes **overwrite, swap-move, swap-offset, direct-xip, direct-xip-revert** on hardware: application
  writes 1.1.0 (`umcub_slot_*`), text `i` / `verify 0 1` show the pending update and the revert copy, test boot,
  revert without confirm, confirm; direct-xip(-revert) between both slots (images linked per slot, `APP_SLOT`).
  Found and fixed: overwrite showed the installed image as "NOT confirmed"; the single-file library did not build
  in the direct-xip modes (name clash) and MCUboot's fallthrough warning broke `-Werror` builds - the matrix now
  builds `umcub_app_all.c` in every upgrade mode.
- [x] H755 regression after the F1 work and the duplication cleanup: signed examples with the board-type TLV,
  both cores start (`port/common` init / deinit / jump, CM4 park loop and release), USB CDC and DFU in recovery
  (shared `h7_hsi48_on()`, ULPI sleep clock left to tinyUSB), RNG for the SECURE handshake, USB not enumerated in
  the 300 ms entry window (`b` in the window stays), DFU download 1.5.0 in 6.4 s -> test boot, `last update via
  usb-dfu / app / uart`. CM4 updated through image 1's secondary slot from the CM7 application; writing image 1's
  primary (running CM4 code) from the CM7 application is refused with `UMCUB_EBUSY`.

Open:

- [ ] **Ethernet** — DHCP (DISCOVER/OFFER/REQUEST/ACK, renewal, static fallback), ARP, ping, SMP over UDP
  (`mcumgr --conntype udp`). JP6 and JP7 must be fitted (UM2408 §7.12). Test 100 and 10 Mbit/s separately (the
  ES0445 2.25.10 workaround is active at 10 Mbit/s only). Needs a cable.
- [ ] **CAN / CAN-FD on a bus** — ISO-TP + SMP with `tools/smp_can.py`. Needs a transceiver on PD0/PD1 and a USB-CAN
  adapter. Only the FDCAN initialisation (internal loopback, timing registers) is verified.
- [ ] Text commands and verify / hash / read over **UDP and CAN** (host tests only).
- [ ] Downgrade prevention (`UMCUB_CFG_DOWNGRADE_PREVENTION`) in swap modes.
- [ ] **IDE workflow** (README "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`), armclang as preprocessor. So far
  verified: `umcub_app_all.c` compiles with the documented include list (GCC, clang); `tools/umcub_image.py` gives
  the same image as CMake (header, payload, hash) from ELF and HEX; a tool-signed image boots on the board.
- [ ] **Bluetooth UART bridge** (HC-05/HC-06 SPP on USART1 D0/D1, PC over RFCOMM): SMP upload, text commands,
  verify / hash over a slow, packetizing link without flow control.
- [ ] Board drivers / board transport with a real external controller (e.g. MCP2518FD, ENC28J60, W5500).
  So far: build matrix (`tests/boards/custom_drivers`) and host test `umcub_host_board`.
- [ ] CubeMX dual-core start-up handshake (CM7 waits for the CM4 to enter STOP, HSEM 0) together with SINGLE_BOOT
  and PER_CORE.

## Hardware verification (Blue Pill, STM32F103C8T6)

Done (bootloader 28.3 K in 32 K, two 16 K slots, overwrite, USART1 PA9/PA10 115200 via FT232R):

- [x] Clocks (HSE 8 MHz -> 72 MHz), SysTick, USART1 log, MCUboot ECDSA validation (boot to jump ~0.4 s incl.
  ECDSA, + 300 ms entry window), jump to the application, handoff + info block read by the application.
- [x] Recovery entry: application request, `b` within the 300 ms window, no valid image; `a` boots the application.
- [x] SMP serial recovery: `state-read`, upload (4.9 KB in 1.3 s), `os reset`; text command `i`;
  `verify` / `hash` via `tools/umcub_inspect.py`.
- [x] Overwrite upgrade from the application (`umcub_slot_*`, `tools/app_upload.py`): secondary -> primary,
  `boot reason: upgraded`, `last update via app`.
- [x] USB (overlay `tools/config/bluepill_usb.h`, 40.7 K in 44 K): enumeration in recovery, text command and SMP
  over CDC (4.9 KB in 0.7 s), verify / hash, DFU download with `-R` (2.1 s) -> overwrite upgrade, clean detach
  (D+ held low outside the bootloader's USB; no enumeration attempts in the kernel log during boot or in the app).
- [x] Watchdog (`UMCUB_CFG_WATCHDOG_MS` = 2 s, IWDG on LSI): an application that feeds it runs without resets, one
  that stops (example key `w`) is reset after 1.99 s (3 of 3, reset cause watchdog); boot incl. ECDSA (0.65 s),
  12 s idle recovery, SMP upload over CDC, DFU download and the overwrite copy complete without a reset.
- [x] Power-loss test of the overwrite copy (`tools/hw/powerfail_test.py --mode overwrite`, fault record in the
  last 16 bytes of the handoff area): 57 resets at erase / half-word program operations spread over the 6266 of a
  12 KiB upgrade, every one completed with primary == v2.
- [x] Recovery timeout (`UMCUB_CFG_RECOVERY_TIMEOUT_MS` 5000): reset 5.0 s after the last command, a text command
  restarts the timer, the application starts after the reset.
- [x] Board-type rejection: an image signed for another board type is refused through the secondary slot (old
  image keeps running, `boot reason: normal`) and in the primary slot (SMP upload: "is for board type ...",
  recovery); `verify` reports it invalid. Found and fixed: the boot reason said `upgraded` although MCUboot had
  refused the update - now confirmed by the primary image having changed.
- [x] Blue Pill after the duplication cleanup (`port/common/`, mux line buffers per stream transport, smpclient
  host tools): boot and jump, application request -> recovery, `smpmgr` echo and upload (5.2 KB in 1.4 s),
  `umcub_inspect.py` hash (MATCH) / verify over smpclient, text command `i`, boot of the uploaded 1.1.0.

Open:

- [ ] F1 port of bxCAN; F105/F107 (PREDIV1, 25 MHz HSE, USB OTG FS); XL-density bank 2.

## Shared buses: umcub link (plan stages)

- [x] 1. Board type (signed TLV, checked by MCUboot), handoff v2, node address from the application.
- [x] 2. UART config struct, RS485 DE (H7 hardware, F1 software), entropy and RDP level in the ports.
- [x] 3. umcub link ADDRESSED (frames, node address / UID selection, discovery) through the mux; host test.
- [x] 4. SECURE: challenge signed with the admin key, ECDH with the device key, HKDF, per-frame HMAC, replay
  protection, AES-CTR payload encryption, RDP policy, idle timeout, key embedding (`tools/umcub_keys.py`);
  host test `umcub_host_link_secure` (also under ASan/UBSan). Builds: H7 `link-secure` (all transports, with
  encryption) 60.3 K, Blue Pill `bluepill-rs485-secure` 31.8 K in a 36 K region (36 K + 2 x 14 K slots) - on the
  F1 SECURE needs more than the default 32 K region.
- [x] 5. Image encryption (MCUboot ENC_EC256 with the device key): `<app>.encrypted.bin` from `umcub_sign_image()` /
  `umcub_image.py`; own in-place decryption after an SMP upload into the primary slot (MCUboot's keeps a whole
  sector on the stack), off on the H7 CM4 bootloader (no RAM for a 128 KiB sector); verify of an encrypted
  secondary; readback only inside an encrypted link session. Host tests `umcub_host_swap_scratch_enc`,
  `umcub_host_link_secure`; builds `encrypt-images` (H7) 64.3 K, `bluepill-encrypt` 35.7 K in a 37.75 K region,
  `per-core-cm4-encrypt`.
- [x] 6. `tools/umcub_link.py`: discover, info, cmd, `serve` proxy (pty / UDP) for mcumgr/smpmgr; end-to-end test
  `tests/host/link_e2e.py` (part of the build matrix): 3 simulated SECURE devices (`umcub_sim_secure`) on one bus -
  discovery, only the addressed node answers, selection by UID, wrong admin key refused, smpmgr echo / upload of an
  encrypted image (decrypted in place) / state-read through the proxy.
- [x] 7. README / size table final pass; manual check of DE / RNG / RDP register use (F1 ADC sampling time, H7 RNG
  seed-error recovery per RM0399 fixed); security review fixes: fail-closed RNG with a health test and a retained
  boot counter (HMAC-keyed with the device key), AUTH back-off and challenge lifetime, session bound to the peer
  (UDP source / accepted frames only), range hashes refused outside an encrypted session, ECDH with random-Z
  blinding, build checks (VALIDATE_PRIMARY, watchdog >= 2 s, mixed PLAIN+SECURE warning), no log on link UARTs,
  CAN ID range with the node address.
- [x] Hardware, Blue Pill (UART SECURE + payload encryption, 44 K region): discover, authentication (~0.8 s with
  the tool), encrypted `cmd i`, smpmgr upload 5 KB in 2.2 s through `umcub_link.py serve`, reboot into 1.1.0;
  wrong admin key refused, no keys refused, plain text and unauthenticated DATA get no answer;
  `UMCUB_CFG_LINK_REQUIRE_RDP 1` at RDP 0: ANNOUNCE flagged closed, handshake refused. Board-type rejection
  (stage 1) verified earlier.
- [x] Hardware, H755 (SECURE on every transport + link encryption + encrypted images): discover, authentication
  (0.43 s incl. a command), wrong admin key refused, plain SMP unanswered; through `umcub_link.py serve` over UART
  and USB CDC: `smpmgr` upload of an encrypted image (validated, decrypted in place with the 128 KiB buffer, boots),
  a tampered one refused with rc 3, `umcub_inspect.py` read / verify / hash inside the encrypted session.
- [ ] Hardware: RS485 bus; encrypted image over DFU and from the application; stack depth of the AUTH check and the
  watchdog on the F1.

## Known limitations / ideas

- [ ] Security review leftovers: (a) one device key per product - per-device keys (provisioning, key derived from
  the UID + a master on the host), H7 PCROP / secure-access area for the key; on the F1, RDP level 1 can be
  defeated by known attacks, so the key there is only as safe as the product's physical access;
  (b) `MCUBOOT_SWAP_SAVE_ENCTLV` (keep the wrapped key in the trailer instead of the plain AES key) for the swap
  modes; (c) F1 entropy quality not measured (ADC temperature noise + jitter). Done: an encrypted SMP upload into
  the primary slot is validated before the in-place decryption.
- [ ] MCUboot submodule is v2.4.0: `main` already wipes the AES key on every path of `boot_serial_encryption.c`.
  Its in-place decryption still needs a sector-sized VLA on the stack, assumes uniform sectors and does not
  validate first, so `mcuboot_port/src/enc_image.c` replaces it (issue draft prepared). Drop our copy once upstream
  takes a port-provided buffer; update the submodule after the next release.
- [ ] Recovery over the network / CAN is not authenticated without `UMCUB_LINK_SECURE` (see README, "Mode notes
  and limitations").
- [ ] When the application writes a slot in the same flash bank it executes from, the CPU stalls for the duration of a
  sector erase (~2 s on the H7).
- [ ] Ports for other series (G4, F7, G0, L4).
- [ ] Bootloader build with Arm Compiler 6 / Keil (now GCC only: startup, linker templates, newlib overrides).
