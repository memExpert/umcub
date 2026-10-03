#ifndef UMCUB_VERSION_H
#define UMCUB_VERSION_H

/* umcub bootloader version (semantic versioning). */
#define UMCUB_VERSION_MAJOR     0
#define UMCUB_VERSION_MINOR     1
#define UMCUB_VERSION_PATCH     0

#ifndef UMCUB_GIT_HASH
#define UMCUB_GIT_HASH          0x00000000u   /* set by CMake */
#endif
#ifndef UMCUB_BOARD_NAME
#define UMCUB_BOARD_NAME        "unknown"
#endif

#endif
