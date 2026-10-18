/*
 * Flash area ids used by umcub:
 *   0 bootloader, 1/2 image 0 primary/secondary, 3/4 image 1, 5 scratch
 */
#ifndef UMCUB_SYSFLASH_H
#define UMCUB_SYSFLASH_H

#define FLASH_AREA_BOOTLOADER          0
#define FLASH_AREA_IMAGE_0             1
#define FLASH_AREA_IMAGE_1             2
#define FLASH_AREA_IMAGE_2             3
#define FLASH_AREA_IMAGE_3             4
#define FLASH_AREA_IMAGE_SCRATCH       5

#define FLASH_AREA_IMAGE_PRIMARY(x)    ((x) == 0 ? FLASH_AREA_IMAGE_0 : (x) == 1 ? FLASH_AREA_IMAGE_2 : 255)
#define FLASH_AREA_IMAGE_SECONDARY(x)  ((x) == 0 ? FLASH_AREA_IMAGE_1 : (x) == 1 ? FLASH_AREA_IMAGE_3 : 255)

#endif
