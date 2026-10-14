#ifndef UMCUB_CRC16_H
#define UMCUB_CRC16_H
#include <stdint.h>
#define CRC16_INITIAL_CRC 0
/* CRC-16/XMODEM (poly 0x1021, no reflection) as used by the SMP serial framing. */
uint16_t crc16_ccitt(uint16_t initial_crc, const void *buf, int len);
#endif
