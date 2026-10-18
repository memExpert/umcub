#ifndef UMCUB_BASE64_H
#define UMCUB_BASE64_H
#include <stdint.h>
#define BASE64_ENCODE_SIZE(__size) (((((__size) - 1) / 3) * 4) + 4)
/* mynewt compatible API */
int base64_encode(const void *data, int size, char *s, uint8_t should_pad);
int base64_decode(const char *str, void *data);
int base64_decode_len(const char *str);
#endif
