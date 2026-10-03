# STM32F1 family description for umcub (Cortex-M3, no FPU).
# Input:  UMCUB_MCU (e.g. STM32F103xB), UMCUB_CORE (ignored, single core)
# Output: UMCUB_FAMILY_* variables used by the bootloader and umcub::app.

set(UMCUB_FAMILY stm32f1)
set(UMCUB_FAMILY_DIR ${UMCUB_ROOT}/port/stm32f1)
set(UMCUB_CORE cm3)

string(TOLOWER "${UMCUB_MCU}" _mcu_lc)

set(UMCUB_CPU_FLAGS_SOFT -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
set(UMCUB_CPU_FLAGS_HARD ${UMCUB_CPU_FLAGS_SOFT})

set(UMCUB_FAMILY_INCLUDES
  ${UMCUB_FAMILY_DIR}/include
  ${UMCUB_FAMILY_DIR}
  ${UMCUB_ROOT}/third_party/st/cmsis_device_f1/Include
  ${UMCUB_ROOT}/third_party/st/stm32f1xx_hal_driver/Inc
  ${UMCUB_ROOT}/third_party/cmsis_core/Include
)
set(UMCUB_FAMILY_DEFS ${UMCUB_MCU} USE_FULL_LL_DRIVER)

# Linked into the bootloader.
set(UMCUB_FAMILY_BOOT_SOURCES
  ${UMCUB_FAMILY_DIR}/sys.c
  ${UMCUB_FAMILY_DIR}/common.c
  ${UMCUB_FAMILY_DIR}/clock.c
  ${UMCUB_FAMILY_DIR}/flash.c
  ${UMCUB_ROOT}/third_party/st/cmsis_device_f1/Source/Templates/gcc/startup_${_mcu_lc}.s
)
set(UMCUB_FAMILY_UART_SOURCES ${UMCUB_FAMILY_DIR}/uart.c)
# Nonces / random back-off (umcub link): ADC noise, no TRNG on F1.
set(UMCUB_FAMILY_ENTROPY_SOURCES ${UMCUB_FAMILY_DIR}/entropy.c)
set(UMCUB_FAMILY_USB_SOURCES  ${UMCUB_FAMILY_DIR}/usb.c)
# tinyUSB device controller driver (USB FS, packet memory) + MCU option for tusb_config.h
set(UMCUB_FAMILY_TUSB_MCU OPT_MCU_STM32F1)
set(UMCUB_FAMILY_TUSB_SOURCES ${UMCUB_ROOT}/third_party/tinyusb/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
                              ${UMCUB_ROOT}/third_party/tinyusb/src/portable/st/stm32_fsdev/fsdev_common.c)
# Not ported yet: bxCAN (no Ethernet on F103). Board drivers (UMCUB_DRIVER_BOARD) work.
set(UMCUB_FAMILY_CAN_SOURCES  "")
set(UMCUB_FAMILY_ETH_SOURCES  "")
set(UMCUB_FAMILY_UNSUPPORTED_CAN 1)
set(UMCUB_FAMILY_UNSUPPORTED_ETH 1)

# Application library sources: include/umcub_family_app.inc.

# Linker script templates (C preprocessed with umcub_cfg.h).
set(UMCUB_FAMILY_BOOT_LD ${UMCUB_FAMILY_DIR}/ld/boot_cm3.ld.in)
set(UMCUB_FAMILY_APP_LD  ${UMCUB_FAMILY_DIR}/ld/app_cm3.ld.in)
