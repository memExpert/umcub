/* Overlay (POST), Black Pill F411: Ethernet through an ENC28J60 module on SPI1
 * (driver: boards/blackpill_f411ce/enc28j60.c) under the built-in IPv4 / DHCP /
 * UDP transport (SMP on port 1337); SMP and log also on USART2 PA2 / PA3.
 * About 30 K: still in the default 32 KiB bootloader (sectors 0-1).
 * Wiring: SCK PA5, SO (MISO) PA6, SI (MOSI) PA7, CS PA4, RESET PB0; INT
 * unused (polled). The ENC28J60 draws up to 180 mA: the 5 V modules have their
 * own regulator. */
#include "usart2.h"

#define UMCUB_CFG_TRANSPORT_ETH         1
#define UMCUB_CFG_ETH_DRIVER            UMCUB_DRIVER_BOARD

/* Board driver pins (enc28j60.c) */
#define ENC28J60_SCK_PIN                UMCUB_PIN('A', 5, 5)    /* AF5: SPI1 */
#define ENC28J60_MISO_PIN               UMCUB_PIN('A', 6, 5)
#define ENC28J60_MOSI_PIN               UMCUB_PIN('A', 7, 5)
#define ENC28J60_CS_PIN                 UMCUB_PIN('A', 4)
#define ENC28J60_RESET_PIN              UMCUB_PIN('B', 0)
