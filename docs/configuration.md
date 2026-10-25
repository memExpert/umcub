# Configuration

Where the options live, how big the bootloader gets for each transport and feature, and the notes on the upgrade
modes.

## Options

The options are documented in [`config/umcub_config_template.h`](../config/umcub_config_template.h), the rarely
changed ones in [Advanced options](#advanced-options) below; defaults are in `config/umcub_config_defaults.h`. Example: [`boards/nucleo_h755zi_q/umcub_config.h`](../boards/nucleo_h755zi_q/umcub_config.h).

| Group | Main options |
|---|---|
| Target | `UMCUB_CFG_MCU`, `UMCUB_CFG_DUALCORE_MODE` (`NONE` / `SINGLE_BOOT` / `PER_CORE`), `UMCUB_CFG_CORE2_DONE_TIMEOUT_MS` |
| Clocks / power | `UMCUB_CFG_CLOCK_SOURCE`, `UMCUB_CFG_HSE_HZ`, `UMCUB_CFG_HSE_BYPASS`, `UMCUB_CFG_PWR_SUPPLY` |
| Flash layout | `UMCUB_CFG_BOOT_*`, `UMCUB_CFG_IMG{0,1}_{PRIMARY,SECONDARY}_{ADDR,SIZE}`, `UMCUB_CFG_SCRATCH_*`, `UMCUB_CFG_IMAGE_HEADER_SIZE` |
| MCUboot | `UMCUB_CFG_UPGRADE_MODE`, `UMCUB_CFG_SIGNATURE`, `UMCUB_CFG_DOWNGRADE_PREVENTION`, `UMCUB_CFG_VALIDATE_PRIMARY` |
| Slot inspection | `UMCUB_CFG_INSPECT_VERIFY`, `UMCUB_CFG_INSPECT_HASH`, `UMCUB_CFG_READBACK`, `UMCUB_CFG_SMP_INSPECT_GROUP` |
| Text commands | `UMCUB_CFG_CMD_ENABLE`, `UMCUB_CFG_CMD_IMMEDIATE`, `UMCUB_CFG_CMD_REPLY`, `UMCUB_CFG_CMD_TABLE` |
| Image checks | `UMCUB_CFG_BOARD_TYPE`, `UMCUB_CFG_BOARD_REV`, `UMCUB_CFG_IMAGE_VECTOR_CHECK` |
| Bootloader entry | `UMCUB_CFG_ENTRY_ON_REQUEST`, `..._ON_NO_IMAGE`, `..._GPIO(_PIN/_ACTIVE/_PULL)`, `..._WAIT_MS`, `UMCUB_CFG_RECOVERY_TIMEOUT_MS` |
| Transports | `UMCUB_CFG_TRANSPORT_{UART,USB_CDC,USB_DFU,CAN,ETH,USER}` plus their parameters, `UMCUB_CFG_{CAN,ETH}_DRIVER` |
| Recovery protocols | `UMCUB_CFG_SMP_ENABLE`, `UMCUB_CFG_SMP_MTU`, `UMCUB_CFG_SMP_PARAMS`, `UMCUB_CFG_LITE_UPLOAD`, `UMCUB_CFG_PROTO_USER` |
| Misc | `UMCUB_CFG_LTO` (experimental), `UMCUB_CFG_LOG_LEVEL`, `UMCUB_CFG_WATCHDOG_MS`, `UMCUB_CFG_SHARED_RAM_ADDR`, `UMCUB_CFG_APP_WRITE_SLOT` |

The configuration can be extended without editing the board file through overlay headers:
`-DUMCUB_CONFIG_PRE=<file>` is included before the board config (for `#ifndef`-guarded options),
`-DUMCUB_CONFIG_POST=<file>` after it (`#undef`/`#define`). Ready-made variants are in `tests/configs/` (`mode/`, `transport/`, `feature/`, `bluepill/`).

Configuration errors (overlapping slots, slots not on sector boundaries, missing scratch area, conflicting modes) are
caught at compile time (`config/umcub_config_check.h`).

### Advanced options

Not in the template: their defaults fit almost every board, and some of them weaken the bootloader when changed. They
are set the same way in `umcub_config.h`.

| Option | Default | What it does |
|---|---|---|
| `UMCUB_CFG_ENTRY_ON_REQUEST` | 1 | Stay in recovery when the application asked for it (`umcub_enter_bootloader()`). **0 can lock the device out**: without a button or a wait window nothing else brings it back. |
| `UMCUB_CFG_ENTRY_ON_NO_IMAGE` | 1 | Stay in recovery when no image is bootable. **0 = a device without a valid image is unreachable** (debugger only). |
| `UMCUB_CFG_LINK_REQUIRE_RDP` | 1 | Keep SECURE transports closed while flash readout protection is off (RDP level 0, the device key can be read with a debugger). 0 only for development. |
| `UMCUB_CFG_USB_DFU_ALLOW_UNAUTH` | 0 | USB DFU cannot authenticate the host; with a SECURE transport it is refused at build time unless this is 1. |
| `UMCUB_CFG_LINK_SESSION_MS` | 60000 | An umcub link session ends after this long without a valid frame. |
| `UMCUB_CFG_USER_LINK` | `UMCUB_LINK_PLAIN` | umcub link mode of the board transport (`UMCUB_CFG_TRANSPORT_USER`), like `UMCUB_CFG_UART_LINK`. |
| `UMCUB_CFG_LITE_UPLOAD` | 1 without SMP | Lite upload protocol (`tools/host/umcub_lite.py`): begin / data / end frames with CRC, written with `umcub_slot_*` (test or permanent mark, in-place decryption). |
| `UMCUB_CFG_MCUBOOT_LOG_LEVEL` | `UMCUB_CFG_LOG_LEVEL` | Level of MCUboot's own messages (`Image index: 0, Swap type: none`, ...), at most the umcub level: 1 (errors only) saves about 0.9 K on the F1/F4 and 1.1 K on the H7 next to an info log, 0 about 1.2-1.6 K. Overlay `tests/configs/feature/mcuboot_log_err.h`. |
| `UMCUB_CFG_CMD_REPLY` | 1 | Text commands answer `ok <cmd>` / `? <text>`. |
| `UMCUB_CFG_SMP_INSPECT_GROUP` | 100 | SMP group id of verify (0) / hash (1) / read (2). |
| `UMCUB_CFG_IMAGE_VECTOR_CHECK` | 1 | Refuse an image whose reset vector lies outside the slot it runs from (wrong image number, built for the other core), about 200 bytes. |
| `UMCUB_CFG_ERASE_PROGRESSIVELY` | 1 (F4: 0) | SMP upload erases sector by sector (1) or the whole slot on the first chunk (0; seconds on big sectors). With 1, `UMCUB_CFG_TRAILER_RESERVE` must cover the last sector of every slot plus 1 KiB (mcu-tools/mcuboot#2920, see [Your own board](board.md#adding-a-board)). |
| `UMCUB_CFG_ENC_INPLACE` | 1 (H7 CM4, F4: 0) | In-place decryption of an encrypted SMP upload into the primary slot, buffering one sector in RAM. Without it encrypted images go through the secondary slot. |
| `UMCUB_CFG_SWAP_SAVE_ENCTLV` | 1 | Swap modes with encrypted images: the slot trailers keep the encrypted key TLVs of both images until the swap is over (signing passes `--save-enctlv`). **0 stores the plain AES keys there**, readable with a debugger or readback, for a slightly smaller trailer. |
| `UMCUB_CFG_UART_TX_RING` | 0 | Transmit through a RAM ring of this many bytes (power of two) drained by the UART interrupt, so the log does not stall the boot (drained before the jump); the last lines can be lost on a hang. Not with RS485. Overlay `tests/configs/feature/uart_tx_ring.h`, verified on the Black Pill (USART2). |
| `UMCUB_CFG_ETH_DHCP_TIMEOUT_MS` | 10000 | Fall back to `UMCUB_CFG_ETH_IP` when no DHCP lease comes in this time. |
| `UMCUB_CFG_ETH_SMP_PORT` | 1337 | UDP port of SMP. |
| `UMCUB_CFG_CORE2_DONE_TIMEOUT_MS` | 3000 | `UMCUB_DUALCORE_PER_CORE`, main core: how long to wait for the second core's bootloader before handing the clocks to the application. |
| `UMCUB_CFG_APP_WRITE_SLOT` | from the mode | Where `umcub_slot_*()` in the application writes by default: `UMCUB_SLOT_SECONDARY`, `UMCUB_SLOT_PRIMARY`, `UMCUB_SLOT_INACTIVE` (direct-xip). |

### Dual-core H745/H755/H747/H757

- **`UMCUB_DUALCORE_SINGLE_BOOT`** (board default): one bootloader on the CM7 manages two MCUboot images — image 0 is
  the CM7 application (bank 1), image 1 the CM4 application (bank 2). Option bytes:
  `BOOT_CM4_ADD0 = BOOT_CM7_ADD0 = 0x0800`. Both cores start from the bootloader vector table; the CM4 parks in WFE
  until the CM7 has validated its image, put its vector table address into SRAM4 and released an HSEM. If the CM4
  image is missing or broken the CM7 still boots and the CM4 stays parked.
- **`UMCUB_DUALCORE_PER_CORE`**: two independent bootloaders (`--preset h755-per-core-cm7` and `h755-per-core-cm4`),
  the CM4 one in bank 2 (`BOOT_CM4_ADD0 = 0x0810`). Only the CM7 configures power and clocks; the two cores must not
  share transports.

STM32CubeProgrammer 2.23 does not expose `BOOT_CM4_ADD0`/`BCM4` for the H755, so `tools/hw/h755_option_bytes.py` writes
the FLASH option registers directly in one pyOCD session (`show` prints them). It does not run on the stm32cube
backend, which reconnects for every register access: an option byte change must run in one debug session, otherwise
the option bytes can end up corrupted (RDP level 1 and PCROP areas; undoing that needs an RDP regression, which erases
the flash).

## Bootloader size

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
- The second table answers how much flash the bootloader region needs for a set of transports: whole erase
  sectors from the start of flash (absolute size in brackets, including the 256-byte info block).
- The third table repeats base, base + SMP + UART and "all on" with link-time optimisation (experimental, see the
  notes below): absolute size and the saving against the same build without it.

**Rule: the bootloader region is a whole number of erase sectors, and on parts with large sectors at most three.**
Every sector given to the bootloader is lost for the slots, and on parts whose first sectors are small (F2/F4/F7) the
fourth sector is already 64 or 128 KiB. Choose the transports so that the build fits: on the H7 everything fits into
one 128 KiB sector; on 16 KiB sectors one SMP transport takes two, USB a third, and "all on" does not fit into
three. Page flash (F1, G4, L4: 1-4 KiB pages) has no such limit: size the region in pages with some
reserve, the slots take the rest. Rows marked "estimate" are series without a port yet, sized from a build for the
same core; check the real size after porting.

<!-- size-table:begin -->
| | STM32H755 CM7 (2 images, SINGLE_BOOT) | STM32H755 CM4 (PER_CORE) | STM32H743 / H753 (single core) | STM32F103 (Blue Pill, overwrite) |
|---|---:|---:|---:|---:|
| **base**: MCUboot, signature check, jump | 18.6 K | 16.2 K | 16.9 K | 12.5 K |
| + SMP (recovery protocol: once, with the first SMP transport) | +5.7 K | +5.6 K | +5.6 K | +5.6 K |
| + UART | +1.3 K | +1.3 K | +1.3 K | +1.3 K |
| + USB CDC | +11.2 K | +11.2 K | +11.2 K | +9.7 K |
| + USB DFU (needs no SMP) | +10.7 K | +10.7 K | +10.7 K | +9.4 K |
| + CAN | +2.9 K | +2.8 K | +2.9 K | +2.6 K |
| + CAN FD (on top of CAN) | 0 | 0 | 0 | — |
| + Ethernet (IPv4, UDP, DHCP) | +4.2 K | +4.2 K | +4.2 K | — |
| + lite upload (instead of SMP) | +2.5 K | +2.5 K | +2.5 K | +2.6 K |
| + log (level 3) | +3.6 K | +3.4 K | +3.5 K | +3.0 K |
| + text commands | +2.2 K | +2.2 K | +2.2 K | +2.0 K |
| + status LED | +0.2 K | +0.2 K | +0.2 K | +0.2 K |
| + verify + hash | +1.0 K | +0.9 K | +0.9 K | +0.9 K |
| + readback (on top of verify + hash) | +0.2 K | +0.2 K | +0.2 K | +0.2 K |
| + umcub link (addressed) | +2.0 K | +2.0 K | +2.0 K | +1.9 K |
| + umcub link SECURE | +4.9 K | +4.9 K | +4.9 K | +4.9 K |
| + link encryption (on top of SECURE) | +1.5 K | +1.5 K | +1.5 K | +1.5 K |
| + encrypted images | +7.2 K | +6.5 K | +6.8 K | +6.4 K |
| **all on** | 53.8 K | 51.1 K | 51.8 K | 37.1 K |

| Flash layout | UART, lite (no SMP) | UART | USB CDC | CAN | Ethernet | UART, link SECURE | all on |
|---|---:|---:|---:|---:|---:|---:|---:|
| STM32H7 (128 KiB sectors) | 1 (22.4 K) | 1 (25.6 K) | 1 (35.4 K) | 1 (27.1 K) | 1 (28.5 K) | 1 (30.5 K) | 1 (53.8 K) |
| STM32F2 / F4, F72x / F73x (16, 16, 16, 16, 64, 128 KiB ...) — estimate: Cortex-M4 build | 2 (20.0 K) | 2 (23.1 K) | 3 (32.9 K) | 2 (24.6 K) | 2 (26.0 K) | 2 (28.0 K) | 4 (51.1 K) > 3 |
| STM32F74x ... F77x (32, 32, 32, 32, 128, 256 KiB ...) — estimate: Cortex-M7 build | 1 (22.4 K) | 1 (25.6 K) | 2 (35.4 K) | 1 (27.1 K) | 1 (28.5 K) | 1 (30.5 K) | 2 (53.8 K) |
| STM32F1 (1 / 2 KiB pages) | 16.4 K | 19.4 K | 27.8 K | 20.7 K | — | 24.3 K | 37.1 K |
| STM32G4 / L4 (2 KiB pages) — estimate: Cortex-M4 build | 20.0 K | 23.1 K | 32.9 K | 24.6 K | 26.0 K | 28.0 K | 51.1 K |

| with LTO | STM32H755 CM7 (2 images, SINGLE_BOOT) | STM32H755 CM4 (PER_CORE) | STM32H743 / H753 (single core) | STM32F103 (Blue Pill, overwrite) |
|---|---:|---:|---:|---:|
| base | 15.6 K (-2.9 K) | 13.2 K (-2.9 K) | 14.0 K (-2.9 K) | 10.0 K (-2.5 K) |
| base + SMP + UART | 21.7 K (-3.9 K) | 19.2 K (-3.9 K) | 19.9 K (-3.9 K) | 15.7 K (-3.7 K) |
| all on | 47.6 K (-6.2 K) | 44.8 K (-6.3 K) | 45.5 K (-6.3 K) | 31.9 K (-5.3 K) |
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
  table): 3-4 K less on the Cortex-M3, 6-7 K on the H7. It is experimental (verified on the Blue Pill only, not on
  the H7) and up to you: inlining makes single stack frames larger (MCUboot's image validation 248 -> 848 bytes); measured on the
  Blue Pill the peak stays the same (about 1.5-2.9 KiB of the 4 KiB reserve, the deepest path is an SMP upload of
  an encrypted image), but check the stack of your part (`tools/hw/stack_usage.py`) and weak functions you
  override in the board file. Verified on the Blue Pill: boot, SMP recovery upload, overwrite upgrade from the
  application, SECURE + encrypted images.
- Every byte counts on small parts: the bootloader keeps compiler-generated tables (`-fno-optimize-crc`), 64-bit
  division and MCUboot's assert paths out of the image; disabled features are not compiled.

## Mode notes and limitations

- **direct-xip-revert**: MCUboot erases an image without a confirmed trailer on the next boot. For programmers and
  serial recovery `umcub_sign_image()` therefore also produces `<app>.confirmed.{bin,hex}` (`--confirm --pad`). The
  plain `.signed.bin` is meant for updates from the application or over DFU, followed by `umcub_confirm()`.
- **direct-xip / direct-xip-revert**: an SMP upload always names the slot (`image`: 0/1 = image 0 primary,
  2 = image 0 secondary, 3/4 = image 1), and the image must be linked for that slot
  (`umcub_app_linker_script(... SLOT n)`, `umcub_sign_image(... SLOT n)`; the CM7 example takes `-DAPP_SLOT=1`).
  Downgrade prevention is not available in these modes.
- **Recovery over the network / CAN is not authenticated** unless the transport uses `UMCUB_LINK_SECURE` (see
  [umcub link](link.md)): otherwise any node on the LAN (UDP 1337) or the
  CAN bus can upload any image signed with your key in recovery mode, including an older one. Signatures are
  always checked.
  `UMCUB_CFG_DOWNGRADE_PREVENTION` (swap modes only) protects updates through the secondary slot (DFU, application);
  SMP recovery writes straight into the primary slot and is not covered. If this matters, allow recovery only by the
  entry pin (`UMCUB_CFG_ENTRY_GPIO`, no `ENTRY_WAIT_MS`) or do not enable network transports.
- **swap-scratch wear**: the scratch sector is erased once per moved sector on every update and wears fastest;
  swap-offset (recommended by MCUboot) or overwrite spread the wear better.
