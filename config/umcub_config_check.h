/*
 * Compile-time validation of the effective configuration.
 */
#ifndef UMCUB_CONFIG_CHECK_H
#define UMCUB_CONFIG_CHECK_H

#if !defined(UMCUB_CFG_BOOT_ADDR) || !defined(UMCUB_CFG_BOOT_SIZE)
#error "umcub: UMCUB_CFG_BOOT_ADDR / UMCUB_CFG_BOOT_SIZE not defined"
#endif
#if !defined(UMCUB_CFG_IMG0_PRIMARY_ADDR) || !defined(UMCUB_CFG_IMG0_PRIMARY_SIZE)
#error "umcub: image 0 primary slot not defined (UMCUB_CFG_IMG0_PRIMARY_ADDR/SIZE)"
#endif
#if !defined(UMCUB_CFG_IMG0_SECONDARY_ADDR) || !defined(UMCUB_CFG_IMG0_SECONDARY_SIZE)
#error "umcub: image 0 secondary slot not defined (UMCUB_CFG_IMG0_SECONDARY_ADDR/SIZE)"
#endif

/* Host tools size their frames for at least this (tools/host/umcub). */
#if UMCUB_CFG_SMP_MTU < 512
#error "umcub: UMCUB_CFG_SMP_MTU must be at least 512"
#endif

#if UMCUB_CFG_IMAGE_NUMBER < 1 || UMCUB_CFG_IMAGE_NUMBER > 2
#error "umcub: UMCUB_CFG_IMAGE_NUMBER must be 1 or 2"
#endif
#if UMCUB_CFG_IMAGE_NUMBER == 2 && UMCUB_CFG_IMG1_PRIMARY_SIZE == 0
#error "umcub: UMCUB_CFG_IMAGE_NUMBER is 2 but image 1 slots are not defined"
#endif
#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT && UMCUB_CFG_IMAGE_NUMBER != 2
#error "umcub: UMCUB_DUALCORE_SINGLE_BOOT needs UMCUB_CFG_IMAGE_NUMBER 2 (image 1 = second core)"
#endif
#if UMCUB_CFG_DUALCORE_MODE != UMCUB_DUALCORE_NONE && !defined(UMCUB_FAMILY_DUALCORE)
#error "umcub: dual-core mode selected but this family port has no dual-core support"
#endif

#if UMCUB_CFG_UPGRADE_MODE < UMCUB_MODE_OVERWRITE || UMCUB_CFG_UPGRADE_MODE > UMCUB_MODE_DIRECT_XIP_REVERT
#error "umcub: invalid UMCUB_CFG_UPGRADE_MODE"
#endif
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_SCRATCH && UMCUB_CFG_SCRATCH_SIZE == 0
#error "umcub: UMCUB_MODE_SWAP_SCRATCH needs a scratch area (UMCUB_CFG_SCRATCH_ADDR/SIZE)"
#endif

#if (UMCUB_CFG_IMG0_PRIMARY_ADDR < UMCUB_CFG_BOOT_ADDR + UMCUB_CFG_BOOT_SIZE) && \
    (UMCUB_CFG_IMG0_PRIMARY_ADDR + UMCUB_CFG_IMG0_PRIMARY_SIZE > UMCUB_CFG_BOOT_ADDR)
#error "umcub: image 0 primary slot overlaps the bootloader"
#endif
#if (UMCUB_CFG_IMG0_SECONDARY_ADDR < UMCUB_CFG_IMG0_PRIMARY_ADDR + UMCUB_CFG_IMG0_PRIMARY_SIZE) && \
    (UMCUB_CFG_IMG0_SECONDARY_ADDR + UMCUB_CFG_IMG0_SECONDARY_SIZE > UMCUB_CFG_IMG0_PRIMARY_ADDR)
#error "umcub: image 0 primary and secondary slots overlap"
#endif

#if UMCUB_CFG_SCRATCH_SIZE > 0 && \
    ((UMCUB_CFG_SCRATCH_ADDR < UMCUB_CFG_IMG0_PRIMARY_ADDR + UMCUB_CFG_IMG0_PRIMARY_SIZE && \
      UMCUB_CFG_SCRATCH_ADDR + UMCUB_CFG_SCRATCH_SIZE > UMCUB_CFG_IMG0_PRIMARY_ADDR) || \
     (UMCUB_CFG_SCRATCH_ADDR < UMCUB_CFG_IMG0_SECONDARY_ADDR + UMCUB_CFG_IMG0_SECONDARY_SIZE && \
      UMCUB_CFG_SCRATCH_ADDR + UMCUB_CFG_SCRATCH_SIZE > UMCUB_CFG_IMG0_SECONDARY_ADDR))
#error "umcub: scratch area overlaps an image 0 slot"
#endif

#if UMCUB_CFG_UPGRADE_MODE == 3
#error "umcub: swap-move was removed - use UMCUB_MODE_SWAP_OFFSET (the secondary slot one sector larger instead of the primary)"
#endif
/* swap-offset needs the extra sector in the secondary slot (MCUboot docs/design.md). */
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_OFFSET && \
    (UMCUB_CFG_IMG0_SECONDARY_SIZE < UMCUB_CFG_IMG0_PRIMARY_SIZE || \
     UMCUB_CFG_IMG1_SECONDARY_SIZE < UMCUB_CFG_IMG1_PRIMARY_SIZE)
#error "umcub: swap-offset: secondary slot must be >= primary slot (ideally + 1 sector)"
#endif

/* A region [a, a + n) of whole erase sectors (n = 0: unused). */
#define UMCUB_SECTORS_OK(a, n) \
    ((n) == 0 || ((a) % UMCUB_FAMILY_SECTOR_AT(a) == 0 && ((a) + (n)) % UMCUB_FAMILY_SECTOR_AT((a) + (n) - 1) == 0))
#if !UMCUB_SECTORS_OK(UMCUB_CFG_BOOT_ADDR, UMCUB_CFG_BOOT_SIZE) || \
    !UMCUB_SECTORS_OK(UMCUB_CFG_IMG0_PRIMARY_ADDR, UMCUB_CFG_IMG0_PRIMARY_SIZE) || \
    !UMCUB_SECTORS_OK(UMCUB_CFG_IMG0_SECONDARY_ADDR, UMCUB_CFG_IMG0_SECONDARY_SIZE) || \
    !UMCUB_SECTORS_OK(UMCUB_CFG_IMG1_PRIMARY_ADDR, UMCUB_CFG_IMG1_PRIMARY_SIZE) || \
    !UMCUB_SECTORS_OK(UMCUB_CFG_IMG1_SECONDARY_ADDR, UMCUB_CFG_IMG1_SECONDARY_SIZE) || \
    !UMCUB_SECTORS_OK(UMCUB_CFG_SCRATCH_ADDR, UMCUB_CFG_SCRATCH_SIZE)
#error "umcub: bootloader, slots and scratch must start and end on erase-sector boundaries"
#endif
/* Progressive erase: MCUboot up to v2.5.0 erases the last sector of a slot
 * again after an SMP upload (mcu-tools/mcuboot#2920), so no image may end in
 * it - the trailer reserve must cover it. */
/* imgtool appends the TLVs (hash, signature, board type, encryption key) behind
 * the application, inside the reserve: the reserve covers them and the sector. */
#define UMCUB_TLV_ROOM 0x400
#define UMCUB_LAST_SECTOR_FREE(a, n) \
    ((n) == 0 || UMCUB_CFG_TRAILER_RESERVE >= UMCUB_FAMILY_SECTOR_AT((a) + (n) - 1) + UMCUB_TLV_ROOM)
#if UMCUB_CFG_ERASE_PROGRESSIVELY && \
    (!UMCUB_LAST_SECTOR_FREE(UMCUB_CFG_IMG0_PRIMARY_ADDR, UMCUB_CFG_IMG0_PRIMARY_SIZE) || \
     !UMCUB_LAST_SECTOR_FREE(UMCUB_CFG_IMG0_SECONDARY_ADDR, UMCUB_CFG_IMG0_SECONDARY_SIZE) || \
     !UMCUB_LAST_SECTOR_FREE(UMCUB_CFG_IMG1_PRIMARY_ADDR, UMCUB_CFG_IMG1_PRIMARY_SIZE) || \
     !UMCUB_LAST_SECTOR_FREE(UMCUB_CFG_IMG1_SECONDARY_ADDR, UMCUB_CFG_IMG1_SECONDARY_SIZE))
#error "umcub: UMCUB_CFG_ERASE_PROGRESSIVELY: UMCUB_CFG_TRAILER_RESERVE must cover the last sector of every slot + 0x400 for the TLVs (mcu-tools/mcuboot#2920), or set UMCUB_CFG_ERASE_PROGRESSIVELY 0"
#endif
#define UMCUB_SLOT_ROOM(n) ((n) == 0 || UMCUB_CFG_TRAILER_RESERVE + UMCUB_CFG_IMAGE_HEADER_SIZE < (n))
#if !UMCUB_SLOT_ROOM(UMCUB_CFG_IMG0_PRIMARY_SIZE) || !UMCUB_SLOT_ROOM(UMCUB_CFG_IMG0_SECONDARY_SIZE) || \
    !UMCUB_SLOT_ROOM(UMCUB_CFG_IMG1_PRIMARY_SIZE) || !UMCUB_SLOT_ROOM(UMCUB_CFG_IMG1_SECONDARY_SIZE)
#error "umcub: no room for the application in a slot (UMCUB_CFG_TRAILER_RESERVE, UMCUB_CFG_IMAGE_HEADER_SIZE)"
#endif
/* The watchdog cannot be fed while a sector is erased (flash reads stall);
 * the LSI may run up to 1.5x faster than nominal. */
#if UMCUB_CFG_WATCHDOG_MS > 0 && UMCUB_CFG_WATCHDOG_MS < UMCUB_FAMILY_SECTOR_ERASE_MS * 3 / 2
#error "umcub: UMCUB_CFG_WATCHDOG_MS must be >= 1.5 x UMCUB_FAMILY_SECTOR_ERASE_MS (one sector erase stalls the CPU)"
#endif

#if UMCUB_CFG_ENCRYPT_IMAGES && UMCUB_CFG_ENC_INPLACE && !defined(UMCUB_FAMILY_UNIFORM_SECTOR)
#error "umcub: UMCUB_CFG_ENC_INPLACE needs sectors of one size (it buffers one in RAM): set it 0"
#endif

#if (UMCUB_CFG_IMAGE_HEADER_SIZE & (UMCUB_CFG_IMAGE_HEADER_SIZE - 1)) != 0 || UMCUB_CFG_IMAGE_HEADER_SIZE < 0x200
#error "umcub: UMCUB_CFG_IMAGE_HEADER_SIZE must be a power of two >= 0x200 (VTOR alignment)"
#endif

#if defined(UMCUB_FAMILY_CAN_NO_FD) && UMCUB_CFG_TRANSPORT_CAN && UMCUB_CFG_CAN_FD && \
    UMCUB_CFG_CAN_DRIVER == UMCUB_DRIVER_PORT
#error "umcub: the CAN controller of this family has no CAN FD (UMCUB_CFG_CAN_FD)"
#endif
#if defined(UMCUB_FAMILY_CAN_USB_SHARED_RAM) && UMCUB_CFG_TRANSPORT_CAN && UMCUB_CFG_USB && \
    UMCUB_CFG_CAN_DRIVER == UMCUB_DRIVER_PORT
#error "umcub: CAN and USB share their SRAM on this part - enable only one of them"
#endif
#if UMCUB_CFG_CAN_DRIVER != UMCUB_DRIVER_PORT && UMCUB_CFG_CAN_DRIVER != UMCUB_DRIVER_BOARD
#error "umcub: UMCUB_CFG_CAN_DRIVER must be UMCUB_DRIVER_PORT or UMCUB_DRIVER_BOARD"
#endif
#if UMCUB_CFG_ETH_DRIVER != UMCUB_DRIVER_PORT && UMCUB_CFG_ETH_DRIVER != UMCUB_DRIVER_BOARD
#error "umcub: UMCUB_CFG_ETH_DRIVER must be UMCUB_DRIVER_PORT or UMCUB_DRIVER_BOARD"
#endif
#if UMCUB_CFG_TRANSPORT_ETH && UMCUB_CFG_ETH_DRIVER == UMCUB_DRIVER_PORT && !defined(UMCUB_CFG_ETH_RMII_PINS)
#error "umcub: UMCUB_CFG_ETH_RMII_PINS is required for the on-chip Ethernet MAC"
#endif

#if UMCUB_CFG_UART_TX_RING & (UMCUB_CFG_UART_TX_RING - 1)
#error "umcub: UMCUB_CFG_UART_TX_RING must be 0 or a power of two"
#endif
#if UMCUB_CFG_UART_TX_RING && UMCUB_CFG_UART_DE_PIN != UMCUB_PIN_NONE
#error "umcub: UMCUB_CFG_UART_TX_RING does not work with RS485 (DE is switched per write)"
#endif
#if UMCUB_CFG_UART_DE_PIN != UMCUB_PIN_NONE && UMCUB_CFG_LOG_LEVEL > 0
#error "umcub: RS485 (UMCUB_CFG_UART_DE_PIN): the log would share the bus - set UMCUB_CFG_LOG_LEVEL 0"
#endif
#if UMCUB_CFG_UART_LINK > UMCUB_LINK_SECURE || UMCUB_CFG_USB_CDC_LINK > UMCUB_LINK_SECURE || \
    UMCUB_CFG_CAN_LINK > UMCUB_LINK_SECURE || UMCUB_CFG_ETH_LINK > UMCUB_LINK_SECURE || \
    UMCUB_CFG_USER_LINK > UMCUB_LINK_SECURE
#error "umcub: UMCUB_CFG_<transport>_LINK must be UMCUB_LINK_PLAIN, _ADDRESSED or _SECURE"
#endif
#if UMCUB_CFG_LINK_SECURE_ANY && UMCUB_CFG_TRANSPORT_USB_DFU && !UMCUB_CFG_USB_DFU_ALLOW_UNAUTH
#error "umcub: USB DFU cannot authenticate the host - disable it next to UMCUB_LINK_SECURE or set UMCUB_CFG_USB_DFU_ALLOW_UNAUTH"
#endif
#if UMCUB_CFG_ENCRYPT_IMAGES && UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP
#error "umcub: UMCUB_CFG_ENCRYPT_IMAGES is not available in the direct-xip modes (images run from both slots)"
#endif
#if UMCUB_CFG_LINK_ENCRYPT && !UMCUB_CFG_LINK_SECURE_ANY
#error "umcub: UMCUB_CFG_LINK_ENCRYPT needs a UMCUB_LINK_SECURE transport"
#endif

/* Without validation of the primary slot, an SMP upload into it is booted
 * unchecked (MCUboot): no signature, no board type, no decryption check. */
#if !UMCUB_CFG_VALIDATE_PRIMARY && UMCUB_CFG_SMP && \
    (UMCUB_CFG_BOARD_TYPE != 0 || UMCUB_CFG_LINK_SECURE_ANY || UMCUB_CFG_ENCRYPT_IMAGES)
#error "umcub: UMCUB_CFG_VALIDATE_PRIMARY 0 lets SMP uploads bypass signature / board type checks"
#endif
/* SECURE AUTH costs two ECC operations (~0.65 s each on a 72 MHz Cortex-M3). */
#if UMCUB_CFG_LINK_SECURE_ANY && UMCUB_CFG_WATCHDOG_MS > 0 && UMCUB_CFG_WATCHDOG_MS < 2000
#error "umcub: with UMCUB_LINK_SECURE, UMCUB_CFG_WATCHDOG_MS must be >= 2000 (ECC checks)"
#endif
/* Per-transport policy: a PLAIN transport next to a SECURE one gives full
 * unauthenticated access through that transport. */
#if UMCUB_CFG_LINK_SECURE_ANY && \
    ((UMCUB_CFG_TRANSPORT_UART && UMCUB_CFG_UART_LINK == UMCUB_LINK_PLAIN) || \
     (UMCUB_CFG_TRANSPORT_USB_CDC && UMCUB_CFG_USB_CDC_LINK == UMCUB_LINK_PLAIN) || \
     (UMCUB_CFG_TRANSPORT_CAN && UMCUB_CFG_CAN_LINK == UMCUB_LINK_PLAIN) || \
     (UMCUB_CFG_TRANSPORT_ETH && UMCUB_CFG_ETH_LINK == UMCUB_LINK_PLAIN) || \
     (UMCUB_CFG_TRANSPORT_USER && UMCUB_CFG_USER_LINK == UMCUB_LINK_PLAIN)) && \
    !UMCUB_CFG_LINK_MIXED_OK
#warning "umcub: a PLAIN transport next to a SECURE one is not authenticated (set UMCUB_CFG_LINK_MIXED_OK 1 if intended)"
#endif

#if !UMCUB_CFG_SMP_ENABLE && !UMCUB_CFG_LITE_UPLOAD && !UMCUB_CFG_PROTO_USER && \
    (UMCUB_CFG_TRANSPORT_UART || UMCUB_CFG_TRANSPORT_USB_CDC || UMCUB_CFG_TRANSPORT_CAN || UMCUB_CFG_TRANSPORT_ETH || \
     UMCUB_CFG_TRANSPORT_USER)
#warning "umcub: no SMP, lite upload or board protocol - recovery over UART/CDC/CAN/UDP cannot install images"
#endif

#if !UMCUB_CFG_TRANSPORT_UART && !UMCUB_CFG_USB && !UMCUB_CFG_TRANSPORT_CAN && !UMCUB_CFG_TRANSPORT_ETH && \
    !UMCUB_CFG_TRANSPORT_USER
#warning "umcub: no transport enabled - updates only via the application (umcub_slot_*)"
#endif

#define UMCUB_IN_AREA_(a, base, size) ((size) > 0 && (a) + 4 > (base) && (a) < (base) + (size))
#if UMCUB_CFG_NODE_ADDR_FLASH != 0 && \
    ((UMCUB_CFG_NODE_ADDR_FLASH % 4) || \
     UMCUB_IN_AREA_(UMCUB_CFG_NODE_ADDR_FLASH, UMCUB_CFG_IMG0_PRIMARY_ADDR, UMCUB_CFG_IMG0_PRIMARY_SIZE) || \
     UMCUB_IN_AREA_(UMCUB_CFG_NODE_ADDR_FLASH, UMCUB_CFG_IMG0_SECONDARY_ADDR, UMCUB_CFG_IMG0_SECONDARY_SIZE) || \
     UMCUB_IN_AREA_(UMCUB_CFG_NODE_ADDR_FLASH, UMCUB_CFG_IMG1_PRIMARY_ADDR, UMCUB_CFG_IMG1_PRIMARY_SIZE) || \
     UMCUB_IN_AREA_(UMCUB_CFG_NODE_ADDR_FLASH, UMCUB_CFG_IMG1_SECONDARY_ADDR, UMCUB_CFG_IMG1_SECONDARY_SIZE) || \
     UMCUB_IN_AREA_(UMCUB_CFG_NODE_ADDR_FLASH, UMCUB_CFG_SCRATCH_ADDR, UMCUB_CFG_SCRATCH_SIZE))
#error "umcub: UMCUB_CFG_NODE_ADDR_FLASH must be 4-byte aligned and outside the image slots and the scratch area"
#endif

#endif /* UMCUB_CONFIG_CHECK_H */
