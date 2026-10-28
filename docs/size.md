# Bootloader size

Flash used by the bootloader (`.text` + `.data`, arm-none-eabi-gcc 15.2, Release `-Os`, swap-scratch,
ECDSA-P256), and what every transport and feature adds. Regenerate with `tools/build/size_table.py`.

- The first table: **base** (MCUboot with signature check, upgrade/revert, the jump to the application, handoff and
  info block; no transport, no log), then what each item adds, then **all on** (every transport and feature the
  port supports).
- **SMP** is MCUboot's serial recovery (`boot_serial`, zcbor) and the multiplexer: it comes once, with the first
  SMP transport (UART, USB CDC, CAN, Ethernet). USB DFU and the lite upload need no SMP: a DFU-only build is base +
  USB DFU. No transport is required either: any one of them (or none) can be the only one.
- Each transport and feature is measured on top of base + SMP + UART (UART itself: UART + CAN minus CAN alone).
  The rows add up to "all on" within a few hundred bytes; code that two items share (USB CDC and DFU: the USB core)
  is counted in both rows.
- The second table: the bootloader size for a set of transports on each ported family, and the region it needs
  from the start of flash (`UMCUB_CFG_BOOT_SIZE`: the size plus the 256-byte info block, rounded up to whole erase
  sectors / pages).
- The third table repeats base, base + SMP + UART and "all on" with link-time optimisation (experimental, see the
  notes below): absolute size and the saving against the same build without it.

**Rule: the bootloader region is a whole number of erase sectors, and on parts with large sectors at most three.**
Every sector given to the bootloader is lost for the slots, and on parts whose first sectors are small (F2/F4/F7) the
fourth sector is already 64 or 128 KiB. Choose the transports so that the build fits: on the H7 everything fits into
one 128 KiB sector; on the F4 one SMP transport takes two 16 KiB sectors and USB a third. Page flash (F1, G4, L4:
1-4 KiB pages) has no such limit: size the region in pages with some reserve, the slots take the rest. A series
without a port yet: the sizes follow the core (Cortex-M4: the F411 column, Cortex-M7: the H7 one, Cortex-M3: the
F103 one) - round them up to that part's sector layout and check the real size after porting.

<!-- size-table:begin -->
| | STM32H755 CM7 (2 images, SINGLE_BOOT) | STM32H755 CM4 (PER_CORE) | STM32H743 / H753 (single core) | STM32F103 (Blue Pill, overwrite) | STM32F411 (Black Pill, overwrite) |
|---|---:|---:|---:|---:|---:|
| **base**: MCUboot, signature check, jump | 18.6 K | 16.2 K | 16.9 K | 12.5 K | 13.4 K |
| + SMP (recovery protocol: once, with the first SMP transport) | +5.7 K | +5.6 K | +5.6 K | +5.6 K | +5.6 K |
| + UART | +1.3 K | +1.3 K | +1.3 K | +1.3 K | +1.2 K |
| + USB CDC | +11.2 K | +11.2 K | +11.2 K | +9.7 K | +10.9 K |
| + USB DFU (needs no SMP) | +10.7 K | +10.7 K | +10.7 K | +9.4 K | +10.5 K |
| + CAN | +2.9 K | +2.8 K | +2.9 K | +2.6 K | — |
| + CAN FD (on top of CAN) | 0 | 0 | 0 | — | — |
| + Ethernet (IPv4, UDP, DHCP) | +4.2 K | +4.2 K | +4.2 K | — | — |
| + lite upload (instead of SMP) | +2.5 K | +2.5 K | +2.5 K | +2.6 K | +2.6 K |
| + log (level 3) | +3.6 K | +3.4 K | +3.5 K | +3.0 K | +3.0 K |
| + text commands | +2.2 K | +2.2 K | +2.2 K | +2.0 K | +2.0 K |
| + status LED | +0.2 K | +0.2 K | +0.2 K | +0.2 K | +0.2 K |
| + verify + hash | +1.0 K | +0.9 K | +0.9 K | +0.9 K | +0.9 K |
| + readback (on top of verify + hash) | +0.2 K | +0.2 K | +0.2 K | +0.2 K | +0.2 K |
| + umcub link (addressed) | +2.0 K | +2.0 K | +2.0 K | +1.9 K | +1.9 K |
| + umcub link SECURE | +4.9 K | +4.9 K | +4.9 K | +4.9 K | +4.8 K |
| + link encryption (on top of SECURE) | +1.5 K | +1.5 K | +1.5 K | +1.5 K | +1.5 K |
| + encrypted images | +7.2 K | +6.5 K | +6.8 K | +6.4 K | +6.1 K |
| **all on** | 52.7 K | 50.0 K | 50.7 K | 37.2 K | 39.0 K |

| Bootloader size → `UMCUB_CFG_BOOT_SIZE` | UART, lite (no SMP) | UART | USB CDC | CAN | Ethernet | UART, link SECURE | all on |
|---|---:|---:|---:|---:|---:|---:|---:|
| STM32H7 (128 KiB sectors) | 22.4 K → 128 K | 25.6 K → 128 K | 35.4 K → 128 K | 27.1 K → 128 K | 28.5 K → 128 K | 30.5 K → 128 K | 52.7 K → 128 K |
| STM32F4 (16, 16, 16, 16, 64, 128 KiB ...) | 17.1 K → 32 K | 20.1 K → 32 K | 29.8 K → 32 K | — | — | 25.0 K → 32 K | 39.0 K → 48 K |
| STM32F1 (1 KiB pages; 2 KiB on high-density parts) | 16.4 K → 17 K | 19.4 K → 20 K | 27.8 K → 29 K | 20.7 K → 21 K | — | 24.3 K → 25 K | 37.2 K → 38 K |

| with LTO | STM32H755 CM7 (2 images, SINGLE_BOOT) | STM32H755 CM4 (PER_CORE) | STM32H743 / H753 (single core) | STM32F103 (Blue Pill, overwrite) | STM32F411 (Black Pill, overwrite) |
|---|---:|---:|---:|---:|---:|
| base | 15.6 K (-2.9 K) | 13.2 K (-2.9 K) | 14.0 K (-2.9 K) | 10.0 K (-2.5 K) | 10.7 K (-2.7 K) |
| base + SMP + UART | 21.7 K (-3.9 K) | 19.2 K (-3.9 K) | 19.9 K (-3.9 K) | 15.7 K (-3.7 K) | 16.5 K (-3.6 K) |
| all on | 46.6 K (-6.1 K) | 43.9 K (-6.1 K) | 44.6 K (-6.1 K) | 31.9 K (-5.3 K) | 33.9 K (-5.2 K) |
<!-- size-table:end -->

Notes:
- The H755 CM4 column is the CM4 bootloader of `PER_CORE` mode (Cortex-M4 code). On the board it uses CAN only;
  the other rows are measured with borrowed pins.
- The H7 bootloader gets one 128 KiB sector, so even "all on" uses less than half of it.
- STM32F103: UART, USB (CDC, DFU) and bxCAN (classic CAN, not together with USB: they share their SRAM) are
  ported, no Ethernet on this part; "all on" is without CAN.
  The column is measured in the 44 KiB region of the USB layout. The Blue Pill default
  (UART, log, text commands, status LED) is 26.9 K of its 32 K region; without log, commands and LED 21.4 K.
- USB is mostly tinyUSB. Ethernet is the own IPv4/ARP/ICMP/UDP/DHCP stack plus the MAC driver. CAN FD only changes
  configuration, not code size.
- Debug builds (`-Og`) are about 18 % larger.
- **Link-time optimisation** (`UMCUB_CFG_LTO 1` or `-DUMCUB_LTO=ON`, overlay `tests/configs/feature/lto.h`, third
  table): 3-4 K less on the Cortex-M3 / M4, 6-7 K on the H7. Verified on the F1 and the F4, experimental on the H7
  (not run there yet). Inlining makes single stack frames larger (MCUboot's image validation 248 -> 848 bytes), the
  peak grows little: Blue Pill about 1.5-2.9 KiB of the 4 KiB reserve (the deepest path is an SMP upload of an
  encrypted image), Black Pill 2416 instead of 2144 bytes (umcub link upload, verify, hash). Check the stack of your
  part (`tests/hw/stack_usage.py`) when you add board code. Verified on the Blue Pill: boot, SMP recovery upload,
  overwrite upgrade from the application, SECURE + encrypted images; on the Black Pill: umcub link recovery upload,
  verify / hash, boot, the application's request back into the bootloader, overwrite upgrade, the ENC28J60 board
  driver and a board override of a weak function (`umcub_cmd_user`).
- Every byte counts on small parts: the bootloader keeps compiler-generated tables (`-fno-optimize-crc`), 64-bit
  division and MCUboot's assert paths out of the image; disabled features are not compiled.
