# Your own board

Adding a board to umcub: its configuration, its identity on a bus, external controllers and transports of your own.

## Adding a board

Ported families: STM32H7 (H743/H753 and the dual-core H745/H755/H747/H757; not H7A3/B0/H72x/H73x yet) and
STM32F1 (F103x8/xB). For another series the port comes first, see [Porting](porting.md).

**1. Describe the board.** The board directory can live in your own repository (umcub as a submodule):
`boards/my_board/umcub_config.h`. Copy [`config/umcub_config_minimal.h`](../config/umcub_config_minimal.h) (the part,
the clock, the flash layout, a way into the bootloader and one transport - it builds as it is, the build matrix checks
that) and change the lines marked "yours". Every other option takes its default from
`config/umcub_config_defaults.h`; [`config/umcub_config_template.h`](../config/umcub_config_template.h) lists all of
them with their meaning, [Configuration](configuration.md) explains the groups.

**2. Make your own signing key.** The repository key is public and for development only:

```sh
imgtool keygen -t ecdsa-p256 -k keys/prod.pem      # keep it out of the repository
```

**3. Build the bootloader.**

```sh
cmake -S umcub -B build/boot -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DUMCUB_BOARD=$PWD/boards/my_board -DUMCUB_SIGNING_KEY=$PWD/keys/prod.pem
cmake --build build/boot                # build/boot/umcub_my_board_cm7.{elf,hex,bin}
```

`arm-none-eabi-gcc` is taken from `PATH`. A toolchain that is not there, e.g. the one inside STM32CubeIDE, is set
with `-DUMCUB_TOOLCHAIN_PREFIX=<path>/arm-none-eabi-` (see
[Bootloader inside STM32CubeIDE](application.md#using-umcub-from-an-ide-stm32cubeide-keil-mdk)).

Configuration mistakes stop the build with a message: slots overlapping or off sector boundaries, the wrong slot
sizes for the mode, a missing scratch area, an HSE the port cannot use, and so on.

**4. Adapt the application.** Use the same board directory and key: CMake with `umcub::app`,
`umcub_app_linker_script()` and `umcub_sign_image()` (see [Application library](application.md#application-library)), or an IDE
project with `tools/host/umcub_image.py` (see [Using umcub from an IDE](application.md#using-umcub-from-an-ide-stm32cubeide-keil-mdk)).
`tools/host/umcub_image.py info --board boards/my_board` prints where to link it.

**5. Program and protect.** Program the bootloader `.hex` and the signed application `.hex`. On dual-core H7 parts,
also program the option bytes (`tools/hw/h755_option_bytes.py`). For production, write-protect the bootloader sector
(WRP) and enable RDP.

**6. Optional: `boards/my_board/umcub_board.c`.** It is compiled automatically when present. Use it for your own text
commands (`UMCUB_CMD_USER`), drivers for external controllers (`UMCUB_DRIVER_BOARD`) or a transport of your own
(`UMCUB_CFG_TRANSPORT_USER`); see [External controllers and custom transports](#external-controllers-and-custom-transports).

Things that are easy to get wrong:
- **Slots.** Slots start and end on sector boundaries. swap-move needs a primary slot one sector larger than the
  secondary, swap-offset the other way round. Only swap-scratch needs a scratch sector. The last
  `UMCUB_CFG_TRAILER_RESERVE` bytes of a slot belong to MCUboot.
- **Clocks and power.**
  - STM32H7: HSE must be a multiple of 2 MHz (8, 12, 16, 24 MHz; not 25 MHz). The application must select the same
    power supply (`UMCUB_CFG_PWR_SUPPLY`): it can be set only once per power-up.
  - STM32F1: HSE of 8, 12 or 16 MHz, otherwise HSI.
- **Pins.** Alternate-function numbers come from the datasheet of your part. F1 has no AF numbers: the remap is
  chosen from the TX pin. Both are checked when CMake configures the bootloader: UART (TX, RX, hardware DE), CAN
  and Ethernet RMII pins against ST's pin data (`third_party/st/stm32_open_pin_data`, `tools/build/check_pins.py`:
  a wrong pin or AF number stops the configuration and lists the pins that carry the signal), F1 pin pairs against
  the port's remap tables. With `UMCUB_CFG_MCU_PART` (the part number on the chip, e.g. `"STM32F103C8T6"`) every
  pin in use - including the entry button and a software DE pin - must also be brought out by that package
  (LQFP48 has no port E, LQFP100 no port G); without it only the pins of the whole line are known. A pin used by
  two options (the status LED on the UART TX pin, the entry button on USB D+ PA11/PA12, ...) stops the
  configuration too, with or without the pin data.
- **RAM.** The handoff area (`UMCUB_CFG_SHARED_RAM_ADDR`, 256 bytes) must stay out of the application's RAM. The
  generated application linker scripts already exclude it.
- **Image header.** `UMCUB_CFG_IMAGE_HEADER_SIZE` is also the offset of the application vector table: it must meet
  the VTOR alignment of the core (0x200 on Cortex-M3/M4 with up to 128 vectors, 0x400 on the H7).
- **Watchdog.** If `UMCUB_CFG_WATCHDOG_MS` is set, the application must feed the IWDG: once started it cannot be
  stopped.
- **Log.** The boot log is off by default. For bring-up set `UMCUB_CFG_LOG_LEVEL 3` (info, about 3.5 K) and watch
  the UART; switch it off again for production (it also never shares a UART with a umcub link). Without a log, the
  status LED (`UMCUB_CFG_STATUS_LED`) still shows a transport that did not come up (N blinks, see the template).

## Board identity: board type and node address

**Board type.** `UMCUB_CFG_BOARD_TYPE` (u32 product id) and `UMCUB_CFG_BOARD_REV` (hardware revision) are reported
by the text command `i` and to the application (`umcub_boot_info()->board_type`, `->board_rev`).

With a board type other than 0, every image must carry the same value in a signed TLV (tag `0xA0`, u32 little
endian). `umcub_sign_image()` and `tools/host/umcub_image.py sign` add it automatically from the board configuration.
MCUboot rejects an image with another board type, or without one, before installing it. This holds even when the
image is signed with the right key, so firmware of another product cannot be booted:

```
E: image 0 slot 0 is for board type 0x46103002, this is 0x46103001
E: Image in the primary slot is not valid!
```

`verify` applies the same rule. Images signed before a board type was set lack the TLV and have to be re-signed.

**Image in its slot.** With `UMCUB_CFG_IMAGE_VECTOR_CHECK` (default on, about 200 bytes) an image is also rejected
when the reset vector of its vector table lies outside the slot it runs from (the primary slot; the slot itself with
direct-xip). This catches a valid, signed image under the wrong image number - e.g. the CM7 application uploaded as
image 1 of a dual-core part - and applications not linked for the slot. An encrypted image is checked once it is
plain in the primary slot. On the dual-core H7 the CM4 then stays parked and the CM7 application runs:

```
E: image 1 slot 0: reset vector 0x08020ba5 not in slot
W: boot_go failed, trying image 0 alone
W: second core stays parked (no valid image 1)
```

**Node address.** The address of a device on a shared bus (RS485, CAN, ...). The bootloader takes the first of:

1. the application: `umcub_set_node_address(addr)` stores it in the handoff RAM, where it survives resets but not
   power cycles, so call it on every start;
2. `UMCUB_CFG_NODE_ADDR_FLASH`: the address of a 32-bit word in flash holding `UMCUB_NODE_ADDR_WORD(addr)`
   (`umcub.h`, `addr | ~addr << 16`). Put it into a field of the application's own settings structure at a fixed
   address and the node is addressable right after power-up, before the application has run - no flash page of
   its own. The word must lie outside the image slots (checked at compile time); erased flash means "no address";
3. the board hook `bool umcub_board_node_address(uint16_t *addr)` in `umcub_board.c` (DIP switches, EEPROM).

0 means unassigned: the node is then reachable by its UID only. The bootloader reports the address it uses (`i`,
`umcub_boot_info()->node_addr`).

## External controllers and custom transports

Interfaces are often attached through a separate chip: a CAN controller on SPI (MCP2515, MCP2518FD), an Ethernet
MAC on SPI (ENC28J60, LAN9250), a W5500 with its own TCP/IP, an RS-485 line with its own framing, a BLE module.
The bootloader supports this at two levels. Both live in `boards/<board>/umcub_board.c`, which is added to the
build automatically. Board code may use LL / registers: it is tied to the MCU anyway.

### 1. Board driver under a built-in transport

ISO-TP + SMP (CAN) and the IPv4/ARP/DHCP/UDP stack + SMP (Ethernet) sit on a small driver interface. Select the
board driver and implement that interface:

```c
/* umcub_config.h */
#define UMCUB_CFG_TRANSPORT_CAN   1
#define UMCUB_CFG_CAN_DRIVER      UMCUB_DRIVER_BOARD   /* the on-chip FDCAN driver is not compiled */
#define UMCUB_CFG_TRANSPORT_ETH   1
#define UMCUB_CFG_ETH_DRIVER      UMCUB_DRIVER_BOARD   /* the on-chip MAC driver is not compiled */
```

| Transport | Functions to implement (`port/include/...`) | Contract |
|---|---|---|
| CAN | `umcub_port_can_init(cfg)`, `_deinit()`, `_send(id, data, len)`, `_tx_idle()`, `_recv(&id, data, &len)` (`umcub_port_can.h`) | receive only `cfg->rx_id`; `send` returns `UMCUB_EBUSY` when the TX buffer is full; `tx_idle` is true once every sent frame is on the bus (ISO-TP STmin); `len` up to 8 (classic) or 64 (`cfg->fd`) |
| Ethernet | `umcub_port_eth_init(mac, phy, pins, npins)`, `_deinit()`, `_link()`, `_tx(frame, len)`, `_rx(buf, max)` (`umcub_port_eth.h`) | whole Ethernet frames without FCS; `pins` is `NULL` when `UMCUB_CFG_ETH_RMII_PINS` is not set; `rx` returns 0 when nothing arrived |

All CAN settings from the config (`UMCUB_CFG_CAN_BITRATE`, `_FD`, `_RX_ID`, `_TX_ID`, `_EXT_ID`, ...) reach the
driver through `umcub_can_cfg_t`. Everything above the driver works as with the on-chip controller, including
`tools/host/smp_can.py`, `mcumgr --conntype udp`, text commands and verify/hash.

### 2. Board transport

For anything that is not CAN or raw Ethernet, write a whole transport. It is polled together with the built-in
ones; responses go back to the transport the request came from.

```c
/* umcub_config.h */
#define UMCUB_CFG_TRANSPORT_USER  1

/* umcub_board.c - a packet transport, e.g. a W5500 UDP socket */
#include "umcub_transport.h"      /* also declares umcub_transport_user */
#include "umcub_handoff.h"

/* a full SMP packet; on a umcub link (UMCUB_CFG_USER_LINK) a link frame around one */
static uint8_t rx[UMCUB_CFG_USER_LINK ? UMCUB_TRANSPORT_LINK_PACKET_MAX : UMCUB_CFG_SMP_MTU];

static int  w5500_init(void)   { /* SPI + chip setup, open UDP socket */ return 0; }
static void w5500_deinit(void) { /* close socket, SPI/GPIO/EXTI back to reset state */ }
static void w5500_poll(void)
{
    size_t n = /* non-blocking: datagram waiting? read it into rx */ 0;
    if (n) {
        umcub_smp_packet_rx(&umcub_transport_user, rx, n);   /* raw SMP or a text command */
    }
}
static int w5500_send(const uint8_t *pkt, size_t len) { /* send to the last sender */ return 0; }

const umcub_transport_t umcub_transport_user = {
    .id = UMCUB_TRANSPORT_USER,
    .init = w5500_init, .deinit = w5500_deinit, .poll = w5500_poll, .send_packet = w5500_send,
};
```

There are two kinds of transport, matching the built-in ones:
- **packet** (`poll` + `send_packet`, like CAN and UDP): whole SMP packets in and out. A packet that is not SMP is
  treated as a text command.
- **stream** (`read` + `write`, like UART and USB CDC): bytes of the SMP serial protocol (NLIP lines, base64) plus
  typed text commands. `read` is non-blocking and returns what is available; `write` may block until sent, with a
  timeout.

After an update over it, `umcub_boot_info()->last_transport` in the application is `UMCUB_TRANSPORT_USER`.

### Rules for board drivers and transports

- The bootloader is a single polled loop without an RTOS. Nothing may block without a bound: use
  `umcub_port_millis()` timeouts.
- Interrupts are optional. If you use them, the ISR only moves data into a buffer.
- `deinit` must put every peripheral, pin, EXTI line and DMA channel it touched back into reset state (RCC reset is
  the simplest way). It runs right before the jump into the application.
- No heap, static buffers only.
- A transparent UART↔CAN or UART↔RS-485 bridge that just forwards bytes needs nothing: for the bootloader it is
  the UART transport.

Checked by the build matrix (`tests/boards/custom_drivers`: board CAN + Ethernet drivers + board transport, the
on-chip drivers left out) and by the host test `umcub_host_board` (SMP over a board CAN driver, SMP and commands
over a board transport, DHCP through a board Ethernet driver, deinit of all three). Not yet run with a real
external controller.

## Reference boards

**NUCLEO-H755ZI-Q** ([`boards/nucleo_h755zi_q/umcub_config.h`](../boards/nucleo_h755zi_q/umcub_config.h)): one
bootloader for both cores (CM7 and CM4 images), all transports, log on the ST-LINK virtual COM port (USART3,
115200), recovery by B1 held during reset. Before the first start the option bytes must let the CM4 wait in the
bootloader: `tools/hw/h755_option_bytes.py single` (see [Configuration](configuration.md#dual-core-h745h755h747h757)).

**Blue Pill** ([`boards/bluepill_f103c8/umcub_config.h`](../boards/bluepill_f103c8/umcub_config.h)): STM32F103C8T6,
SMP over USART1 (PA9 TX, PA10 RX, the pins of the ROM bootloader), overwrite mode, bootloader 32 KiB + two 16 KiB
slots.

There is no user button. Recovery mode starts on a request from the application (key `b` in the example), when there
is no valid image, or when `b` arrives within 300 ms after reset (`UMCUB_CFG_ENTRY_WAIT_MS`).
`--line-buffers 4` matches `UMCUB_CFG_SMP_MTU` = 512 (20 KiB SRAM).

USB (CDC with SMP and DFU, micro-USB connector) needs more room: preset `bluepill-usb` (overlay
`tests/configs/bluepill/usb.h`: bootloader 44 KiB, two 10 KiB slots; the example in the same tree uses the same
layout):

```sh
cmake --preset bluepill-usb && cmake --build --preset bluepill-usb
smpmgr --port /dev/ttyACM0 --line-buffers 4 image upload build/bluepill-usb/examples/bluepill_app/bluepill_app.signed.bin
dfu-util -a 0 -D build/bluepill-usb/examples/bluepill_app/bluepill_app.signed.bin -R
```

The F1 has no switchable D+ pull-up and the Blue Pill ties D+ to 3.3 V, so the board looks attached whenever it is
powered, and a host would try to enumerate a device that does not answer. The STM32F1 port therefore drives D+ low
(detached) from reset on, and whenever its USB is not running. That includes the application: an application that
uses USB itself must make PA12 an input before enabling its USB peripheral. The USB preset has no wait window
(`UMCUB_CFG_ENTRY_WAIT_MS 0`): USB would attach and vanish again on every boot (see the option in the template).
While a debugger holds the MCU in reset (`mode=UR`) D+ floats high again: flash with `mode=HOTPLUG`, or with the
USB cable unplugged.

CAN (bxCAN, classic CAN, ISO-TP like on the H7) on PB9 TX / PB8 RX with an external transceiver: overlay
`tests/configs/bluepill/can.h` (+2.4 K, still in the 32 KiB region), host side `tools/host/smp_can.py`. Not together with
USB: on the F103 both share the same 512 bytes of SRAM (the build refuses it).
