# Configuration

Where the options live and the notes on the upgrade modes; how big the bootloader gets: [Bootloader size](size.md).

## Options

Everything is configured with Kconfig ([esp-idf-kconfig](https://github.com/espressif/esp-idf-kconfig)): a board's
`defconfig`, fragments (`UMCUB_CONF`) and menuconfig, see [Kconfig](#kconfig) below. The options with their help texts
are in [`config/Kconfig`](../config/Kconfig) and menuconfig, the rarely changed ones also in
[Advanced options](#advanced-options). Example: [`boards/nucleo_h755zi_q/defconfig`](../boards/nucleo_h755zi_q/defconfig)
and its [`Kconfig`](../boards/nucleo_h755zi_q/Kconfig).

| Group | Main options |
|---|---|
| Target | `UMCUB_CFG_PART_*`, `UMCUB_CFG_DUALCORE_NONE` / `_SINGLE_BOOT` / `_PER_CORE`, `UMCUB_CFG_CORE2_DONE_TIMEOUT_MS` |
| Clocks / power | `UMCUB_CFG_CLOCK_HSI` / `_HSE`, `UMCUB_CFG_HSE_HZ`, `UMCUB_CFG_HSE_BYPASS`, `UMCUB_CFG_PWR_SUPPLY_*` |
| Flash layout | `UMCUB_CFG_BOOT_*`, `UMCUB_CFG_IMG{0,1}_{PRIMARY,SECONDARY}_{ADDR,SIZE}`, `UMCUB_CFG_SCRATCH_*`, `UMCUB_CFG_IMAGE_HEADER_SIZE` |
| MCUboot | `UMCUB_CFG_UPGRADE_MODE_*`, `UMCUB_CFG_SIGNATURE_*`, `UMCUB_CFG_DOWNGRADE_PREVENTION`, `UMCUB_CFG_VALIDATE_PRIMARY` |
| Slot inspection | `UMCUB_CFG_INSPECT_VERIFY`, `UMCUB_CFG_INSPECT_HASH`, `UMCUB_CFG_READBACK`, `UMCUB_CFG_SMP_INSPECT_GROUP` |
| Text commands | `UMCUB_CFG_CMD_ENABLE`, `UMCUB_CFG_CMD_IMMEDIATE`, `UMCUB_CFG_CMD_REPLY`, `UMCUB_CFG_CMD_TABLE` |
| Image checks | `UMCUB_CFG_BOARD_TYPE`, `UMCUB_CFG_BOARD_REV`, `UMCUB_CFG_IMAGE_VECTOR_CHECK` |
| Bootloader entry | `UMCUB_CFG_ENTRY_ON_REQUEST`, `..._ON_NO_IMAGE`, `..._GPIO(_PIN/_ACTIVE/_PULL)`, `..._WAIT_MS`, `UMCUB_CFG_RECOVERY_TIMEOUT_MS` |
| Transports | `UMCUB_CFG_TRANSPORT_{UART,USB_CDC,USB_DFU,CAN,ETH,USER}` plus their parameters, `UMCUB_CFG_{CAN,ETH}_DRIVER_*` |
| Recovery protocols | `UMCUB_CFG_SMP_ENABLE`, `UMCUB_CFG_SMP_MTU`, `UMCUB_CFG_SMP_PARAMS`, `UMCUB_CFG_LITE_UPLOAD`, `UMCUB_CFG_PROTO_USER` |
| Misc | `UMCUB_CFG_LTO` (experimental on the H7), `UMCUB_CFG_LOG_LEVEL`, `UMCUB_CFG_WATCHDOG_MS`, `UMCUB_CFG_APP_WRITE_SLOT_*` |

Ready-made variants of a board are fragments in `boards/<board>/variants/` (USB, RS485, CAN, umcub link, encryption, ...); those the build matrix uses for modes, transports and features are in `tests/configs/` (`mode/`, `transport/`, `feature/`, `part/`).

Configuration errors (overlapping slots, slots not on sector boundaries, missing scratch area, conflicting modes) are
caught at compile time (`config/umcub_config_check.h`).

### Advanced options

Their defaults fit almost every board, and some of them weaken the bootloader when changed.

| Option | Default | What it does |
|---|---|---|
| `UMCUB_CFG_ENTRY_ON_REQUEST` | 1 | Stay in recovery when the application asked for it (`umcub_enter_bootloader()`). **0 can lock the device out**: without a button or a wait window nothing else brings it back. |
| `UMCUB_CFG_ENTRY_ON_NO_IMAGE` | 1 | Stay in recovery when no image is bootable. **0 = a device without a valid image is unreachable** (debugger only). |
| `UMCUB_CFG_LINK_REQUIRE_RDP` | 1 | Keep SECURE transports closed while flash readout protection is off (RDP level 0, the device key can be read with a debugger). 0 only for development. |
| `UMCUB_CFG_USB_DFU_ALLOW_UNAUTH` | 0 | USB DFU cannot authenticate the host; with a SECURE transport it is refused at build time unless this is 1. |
| `UMCUB_CFG_LINK_SESSION_MS` | 60000 | An umcub link session ends after this long without a valid frame. |
| `UMCUB_CFG_USER_LINK_*` | plain | umcub link mode of the board transport (`UMCUB_CFG_TRANSPORT_USER`), like `UMCUB_CFG_UART_LINK_*`. |
| `UMCUB_CFG_LITE_UPLOAD` | 1 without SMP | Lite upload protocol (`umcub upload --lite`): begin / data / end frames with CRC, written with `umcub_slot_*` (test or permanent mark, in-place decryption). |
| `UMCUB_CFG_MCUBOOT_LOG_LEVEL` | `UMCUB_CFG_LOG_LEVEL` | Level of MCUboot's own messages (`Image index: 0, Swap type: none`, ...), at most the umcub level: 1 (errors only) saves about 0.9 K on the F1/F4 and 1.1 K on the H7 next to an info log, 0 about 1.2-1.6 K. Fragment `tests/configs/feature/mcuboot_log_err.conf`. |
| `UMCUB_CFG_ASSERT_TRAP` | 0 | A failed assertion in MCUboot becomes one `udf` (HardFault, the bootloader's handler resets; so does every other fault then) instead of logging `assert <file>:<line>` and resetting: about 0.1 K smaller without a log, 0.4-0.5 K with one, but silent. Fragment `tests/configs/feature/assert_trap.conf`. |
| `UMCUB_CFG_CMD_REPLY` | 1 | Text commands answer `ok <cmd>` / `? <text>`. |
| `UMCUB_CFG_SMP_INSPECT_GROUP` | 100 | SMP group id of verify (0) / hash (1) / read (2). |
| `UMCUB_CFG_IMAGE_VECTOR_CHECK` | 1 | Refuse an image whose reset vector lies outside the slot it runs from (wrong image number, built for the other core), about 200 bytes. |
| `UMCUB_CFG_ERASE_PROGRESSIVELY` | 1 | SMP upload erases sector by sector (1) or the whole slot on the first chunk (0; seconds on big sectors). |
| `UMCUB_CFG_ENC_INPLACE` | 1 (H7 CM4, F4: 0) | In-place decryption of an encrypted SMP upload into the primary slot, buffering one sector in RAM. Without it encrypted images go through the secondary slot. |
| `UMCUB_CFG_SWAP_SAVE_ENCTLV` | 1 | Swap modes with encrypted images: the slot trailers keep the encrypted key TLVs of both images until the swap is over (signing passes `--save-enctlv`). **0 stores the plain AES keys there**, readable with a debugger or readback, for a slightly smaller trailer. |
| `UMCUB_CFG_UART_TX_RING` | 0 | Transmit through a RAM ring of this many bytes (power of two) drained by the UART interrupt, so the log does not stall the boot (drained before the jump); the last lines can be lost on a hang. Not with RS485. Fragment `tests/configs/feature/uart_tx_ring.conf`, verified on the Black Pill (USART2). |
| `UMCUB_CFG_ETH_DHCP_TIMEOUT_MS` | 10000 | Fall back to `UMCUB_CFG_ETH_IP` when no DHCP lease comes in this time. |
| `UMCUB_CFG_ETH_SMP_PORT` | 1337 | UDP port of SMP. |
| `UMCUB_CFG_CORE2_DONE_TIMEOUT_MS` | 3000 | `UMCUB_CFG_DUALCORE_PER_CORE`, main core: how long to wait for the second core's bootloader before handing the clocks to the application. |
| `UMCUB_CFG_APP_WRITE_SLOT_*` | from the mode | Where `umcub_slot_*()` in the application writes by default: `_SECONDARY`, `_PRIMARY`, `_INACTIVE` (direct-xip). |

Derived, not options: `UMCUB_CFG_BOOT_INFO_ADDR` (the last 256 bytes of the bootloader region: the info block with version, mode, slots
and board type, `umcub_boot_version()`, `i`; part of the application ABI, the application finds the block there) and
`UMCUB_CFG_SHARED_RAM_ADDR` (the handoff area).

### Dual-core H745/H755/H747/H757

- **`UMCUB_CFG_DUALCORE_SINGLE_BOOT`** (board default): one bootloader on the CM7 manages two MCUboot images — image 0 is
  the CM7 application (bank 1), image 1 the CM4 application (bank 2). Option bytes:
  `BOOT_CM4_ADD0 = BOOT_CM7_ADD0 = 0x0800`. Both cores start from the bootloader vector table; the CM4 parks in WFE
  until the CM7 has validated its image, put its vector table address into SRAM4 and released an HSEM. If the CM4
  image is missing or broken the CM7 still boots and the CM4 stays parked.
- **`UMCUB_CFG_DUALCORE_PER_CORE`**: two independent bootloaders (`--preset h755-per-core-cm7` and `h755-per-core-cm4`),
  the CM4 one in bank 2 (`BOOT_CM4_ADD0 = 0x0810`). Only the CM7 configures power and clocks; the two cores must not
  share transports.

STM32CubeProgrammer 2.23 does not expose `BOOT_CM4_ADD0`/`BCM4` for the H755, so `tools/hw/h755_option_bytes.py` writes
the FLASH option registers directly in one pyOCD session (`show` prints them). It does not run on the stm32cube
backend, which reconnects for every register access: an option byte change must run in one debug session, otherwise
the option bytes can end up corrupted (RDP level 1 and PCROP areas; undoing that needs an RDP regression, which erases
the flash).

## Kconfig

The options are described in [`Kconfig`](../Kconfig), [`config/Kconfig`](../config/Kconfig), `port/Kconfig` and the
board's own `Kconfig`, with the same names as above (`UMCUB_CFG_<NAME>` in `.config` and in the generated
`umcub_config.h`). The fragments of a build are also what `umcub image sign --conf` takes.

```sh
cmake -S . -B build/bp -G Ninja -DUMCUB_BOARD=blackpill_f411ce \
      -DUMCUB_CONF="boards/blackpill_f411ce/variants/usart2.conf;boards/blackpill_f411ce/variants/enc28j60.conf"
cmake --build build/bp --target menuconfig       # change the build tree's .config, then build again
cmake --build build/bp --target savedefconfig    # build/bp/defconfig: only what differs from the defaults
```

- Relative paths in `UMCUB_CONF` are taken from the source directory of the top project (`-S`), for the bootloader and
  for the applications built with it.
- Without a board (`UMCUB_BOARD` empty, the default) the configuration starts from the Kconfig defaults (an
  STM32F103C8 with SMP on USART1 PA9/PA10, no slots) and the fragments; pick the part, the slots and everything else in
  menuconfig, then keep `savedefconfig`'s output as your own defconfig or fragment. The binaries are named after the part. Board
  code (drivers, hooks) needs a board directory: its `defconfig` and `.c` files.
- The slots are yours to choose (`UMCUB_CFG_IMG0_PRIMARY_ADDR` ..., menu *Flash layout*): whole erase sectors behind the
  bootloader, sized for your application; what is left stays free for its data. Configure stops on slots MCUboot would
  refuse to upgrade between ([Slots](board.md#adding-a-board)) and on a primary slot without a secondary one. Until they
  are set a tree offers only `menuconfig` and `savedefconfig`; configure prints the part's sectors.
- Configure runs `umcub config generate`: the defconfig, then the fragments of `UMCUB_CONF` in order (a later
  assignment wins) -> in `build/<x>/umcub_kconfig/`: `.config`, `umcub_config.h` (C; do not edit), `umcub_config.cmake`
  and `umcub_config.json` (CMake, the host tools), the linker fragments. An assignment that does not take effect
  (unknown option, dependency not met, out of range) stops the configuration, unless a later fragment switched off
  what it depends on.
- The build tree keeps its `.config` (menuconfig changes) as long as the defconfig and the fragments stay the same;
  when one of them changes, `.config` is made again from them. If that drops edits, configure warns and names the
  options that differ, the old file stays as `.config.old`; `savedefconfig` first keeps them.
- When configure stops on the configuration, the `menuconfig` target cannot run (CMake configures first): the error
  prints the command that opens the tree's `.config` directly.
- The part is chosen by its part number (`UMCUB_CFG_PART_STM32F411CEUX=y`: package, flash and RAM follow). The
  menus then offer only what that part has: its USART instances, USB / CAN / Ethernet where the port drives them,
  and for every pin option only the pins that carry the signal on that package (`UMCUB_CFG_UART_TX_PA9=y`; the
  alternate function is filled in). The part list and the pin menus are generated from ST's pin data into
  `port/kconfig/` by `tools/build/gen_kconfig.py`.
- Values: bools `=y` / `# UMCUB_CFG_<NAME> is not set`; choices by their member (`UMCUB_CFG_UPGRADE_MODE_SWAP_OFFSET=y`);
  IPv4 addresses as `"192.168.1.50"`; pins of board options (`boards/<b>/Kconfig`) as strings, `"PA4"` or `"PA5:5"`
  with the alternate function.
- Signing: `umcub image sign --board blackpill_f411ce [--conf <fragment>]...`, or `--board build/<x>/umcub_kconfig`.
- Linker fragments: next to `umcub_config.h` the build tree has `umcub_app_img<n>_slot<m>.ld`, the MEMORY block of
  every slot (`umcub image ld` writes the same without a build tree). Applications built with `umcub_app_linker_script()`
  include it with the family's `port/<fam>/ld/app_<core>_sections.ld`; others `INCLUDE` it in their own script
  ([application](application.md)).
- Defaults of a board: its own `Kconfig` can define an option again with `default` lines (and a `choice` by its
  name), read before `config/Kconfig`, so they win over the generic defaults and can depend on the core and the mode:
  [`boards/nucleo_h755zi_q/Kconfig`](../boards/nucleo_h755zi_q/Kconfig) has the layout and the transports per core and
  dual-core mode. Variants stay plain fragments (`variants/per_core.conf`, `mode/swap_offset.conf`, ...).
- Text commands: `UMCUB_CFG_CMD_TABLE="a=BOOT_APP i=INFO hello=USER1 +inspect"` ([recovery](recovery.md)).
- Keys (menu *Keys*): the signing key, the device key (encrypted images, SECURE link) and the admin key (SECURE
  link) each come from `..._KEY_GENERATE` (default), `..._KEY_FILE` + `..._KEY_PATH`, or `..._KEY_DEV` (the
  public development key of `tools/keys/`, for tests). Generated keys are made at configure time in `keys/<board>/`
  (no board: `keys/<part>/`; `UMCUB_CFG_KEYS_DIR` changes it; relative paths are taken from the umcub root), never
  replaced, and git-ignored: keep them and a backup, the devices accept only what they sign. Both cores of a
  dual-core board use the same keys. A new key is a configure warning, once; a clean checkout or CI workspace makes a
  different one each time, so CI takes `..._KEY_FILE` or a `UMCUB_CFG_KEYS_DIR` outside the tree.
  `-DUMCUB_SIGNING_KEY` and the like stop configure: the keys are options.

## Bootloader size

What every transport and feature adds, the sectors the bootloader takes per family and the size levers:
[Bootloader size](size.md).

## Mode notes and limitations

- **direct-xip-revert**: MCUboot erases an image without a confirmed trailer on the next boot. For programmers and
  serial recovery `umcub_sign_image()` therefore also produces `<app>.confirmed.{bin,hex}` (`--confirm --pad`). The
  plain `.signed.bin` is meant for updates from the application or over DFU, followed by `umcub_confirm()`.
- **direct-xip / direct-xip-revert**: an SMP upload always names the slot (`image`: 0/1 = image 0 primary,
  2 = image 0 secondary, 3/4 = image 1), and the image must be linked for that slot
  (`umcub_app_linker_script(... SLOT n)`, `umcub_sign_image(... SLOT n)`; the CM7 example takes `-DAPP_SLOT=1`).
  Downgrade prevention is not available in these modes.
- **Recovery over the network / CAN is not authenticated** unless the transport uses the SECURE mode (`UMCUB_CFG_<transport>_LINK_SECURE`, see
  [umcub link](link.md)): otherwise any node on the LAN (UDP 1337) or the
  CAN bus can upload any image signed with your key in recovery mode, including an older one. Signatures are
  always checked.
  `UMCUB_CFG_DOWNGRADE_PREVENTION` (not in the direct-xip modes) protects updates through the secondary slot (DFU, application);
  SMP recovery writes straight into the primary slot and is not covered. If this matters, allow recovery only by the
  entry pin (`UMCUB_CFG_ENTRY_GPIO`, no `ENTRY_WAIT_MS`) or do not enable network transports.
- **swap-scratch wear**: the scratch sector is erased once per moved sector on every update and wears fastest;
  swap-offset (recommended by MCUboot) or overwrite spread the wear better.
