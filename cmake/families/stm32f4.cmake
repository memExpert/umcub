# STM32F4 family description for umcub (Cortex-M4F).
# Input:  UMCUB_MCU (e.g. STM32F411xE), UMCUB_CORE (ignored, single core)
# Output: UMCUB_FAMILY_* variables used by the bootloader and umcub::app.

set(UMCUB_FAMILY stm32f4)
set(UMCUB_FAMILY_DIR ${UMCUB_ROOT}/port/stm32f4)
set(UMCUB_CORE cm4)

string(TOLOWER "${UMCUB_MCU}" _mcu_lc)

# The bootloader does no floating point: soft float, the FPU stays off for the application.
set(UMCUB_CPU_FLAGS_SOFT -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)
set(UMCUB_CPU_FLAGS_HARD -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

set(UMCUB_FAMILY_INCLUDES
  ${UMCUB_FAMILY_DIR}/include
  ${UMCUB_FAMILY_DIR}
  ${UMCUB_ROOT}/third_party/st/cmsis_device_f4/Include
  ${UMCUB_ROOT}/third_party/st/stm32f4xx_hal_driver/Inc
  ${UMCUB_ROOT}/third_party/cmsis_core/Include
)
set(UMCUB_FAMILY_DEFS ${UMCUB_MCU} USE_FULL_LL_DRIVER)

# Linked into the bootloader.
set(UMCUB_FAMILY_BOOT_SOURCES
  ${UMCUB_FAMILY_DIR}/sys.c
  ${UMCUB_ROOT}/port/arch/cortexm/cortexm_common.c
  ${UMCUB_ROOT}/port/arch/cortexm/cortexm_sys.c
  ${UMCUB_FAMILY_DIR}/clock.c
  ${UMCUB_FAMILY_DIR}/flash.c
  ${UMCUB_ROOT}/third_party/st/cmsis_device_f4/Source/Templates/gcc/startup_${_mcu_lc}.s
)
set(UMCUB_FAMILY_UART_SOURCES ${UMCUB_FAMILY_DIR}/uart.c ${UMCUB_ROOT}/port/arch/stm32/usart_v1.c)
# Nonces / random back-off (umcub link), encrypted images: ADC noise, no RNG on the F411.
set(UMCUB_FAMILY_ENTROPY_SOURCES ${UMCUB_FAMILY_DIR}/entropy.c)
# Not ported yet: USB OTG FS, bxCAN (not on the F411).
set(UMCUB_FAMILY_USB_SOURCES  "")
set(UMCUB_FAMILY_UNSUPPORTED_USB 1)
set(UMCUB_FAMILY_CAN_SOURCES  "")
set(UMCUB_FAMILY_UNSUPPORTED_CAN 1)
set(UMCUB_FAMILY_ETH_SOURCES  "")
set(UMCUB_FAMILY_UNSUPPORTED_ETH 1)

# Application library sources: include/umcub_family_app.inc.

# Linker script templates (C preprocessed with umcub_cfg.h).
set(UMCUB_FAMILY_BOOT_LD ${UMCUB_FAMILY_DIR}/ld/boot_cm4.ld.in)
set(UMCUB_FAMILY_APP_LD  ${UMCUB_FAMILY_DIR}/ld/app_cm4.ld.in)
