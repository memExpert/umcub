/* Minimal mynewt "os" shim for boot_serial.c. */
#ifndef UMCUB_OS_H
#define UMCUB_OS_H
#include <stdint.h>
#include <string.h>
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif
