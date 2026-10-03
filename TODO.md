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

Open:

- [ ] **Ethernet** — DHCP (DISCOVER/OFFER/REQUEST/ACK, renewal, static fallback), ARP, ping, SMP over UDP
  (`mcumgr --conntype udp`). JP6 and JP7 must be fitted (UM2408 §7.12). Test 100 and 10 Mbit/s separately (the
  ES0445 2.25.10 workaround is active at 10 Mbit/s only). Needs a cable.
- [ ] **CAN / CAN-FD on a bus** — ISO-TP + SMP with `tools/smp_can.py`. Needs a transceiver on PD0/PD1 and a USB-CAN
  adapter. Only the FDCAN initialisation (internal loopback, timing registers) is verified.
- [ ] Power-loss test for **swap-move and swap-offset**.
- [ ] Upgrade modes **overwrite, swap-move, swap-offset, direct-xip, direct-xip-revert** on hardware (only
  swap-scratch so far; move/offset are covered by host tests, the others are build-tested).
- [ ] Text commands and verify / hash / read over **UDP and CAN** (host tests only).
- [ ] Downgrade prevention (`UMCUB_CFG_DOWNGRADE_PREVENTION`) in swap modes.
- [ ] **IDE workflow** (README "Using umcub from an IDE") in real STM32CubeIDE and Keil MDK projects: post-build
  signing, debug download of the signed image, Keil key codes (`#L`, `$L@L`), armclang as preprocessor. So far
  verified: `umcub_app_all.c` compiles with the documented include list (GCC, clang); `tools/umcub_image.py` gives
  the same image as CMake (header, payload, hash) from ELF and HEX; a tool-signed image boots on the board.
- [ ] Board drivers / board transport with a real external controller (e.g. MCP2518FD, ENC28J60, W5500).
  So far: build matrix (`tests/boards/custom_drivers`) and host test `umcub_host_board`.
- [ ] CubeMX dual-core start-up handshake (CM7 waits for the CM4 to enter STOP, HSEM 0) together with SINGLE_BOOT
  and PER_CORE.

## Known limitations / ideas

- [ ] Recovery over the network / CAN is not authenticated (see README, "Mode notes and limitations").
- [ ] An application on one core can erase the running image of the other core (`umcub_slot_*` only checks its own core).
- [ ] When the application writes a slot in the same flash bank it executes from, the CPU stalls for the duration of a
  sector erase (~2 s on the H7).
- [ ] Ports for other series (G4, F7, F1, G0, L4).
- [ ] Bootloader build with Arm Compiler 6 / Keil (now GCC only: startup, linker templates, newlib overrides).
