# STM32H7 family description for umcub.
# Input:  UMCUB_MCU (e.g. STM32H755xx), UMCUB_CORE (cm7|cm4|"")
# Output: UMCUB_FAMILY_* variables used by the bootloader and umcub::app.

set(UMCUB_FAMILY stm32h7)
set(UMCUB_FAMILY_DIR ${UMCUB_ROOT}/port/stm32h7)

string(TOLOWER "${UMCUB_MCU}" _mcu_lc)
set(_dual_core_parts STM32H745xx STM32H745xG STM32H747xx STM32H747xG STM32H755xx STM32H757xx)
if(UMCUB_MCU IN_LIST _dual_core_parts)
  if(NOT UMCUB_CORE)
    set(UMCUB_CORE cm7)
  endif()
  if(UMCUB_CORE STREQUAL "cm7")
    set(_core_def CORE_CM7)
  elseif(UMCUB_CORE STREQUAL "cm4")
    set(_core_def CORE_CM4)
  else()
    message(FATAL_ERROR "umcub: UMCUB_CORE must be cm7 or cm4 for ${UMCUB_MCU}")
  endif()
else()
  set(UMCUB_CORE cm7)
  set(_core_def "")
endif()

if(UMCUB_CORE STREQUAL "cm4")
  set(UMCUB_CPU_FLAGS_SOFT -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)
  set(UMCUB_CPU_FLAGS_HARD -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
else()
  # Bootloader code must also run on the CM4 (dual-core park loop): stay
  # within ARMv7E-M + soft float.
  set(UMCUB_CPU_FLAGS_SOFT -mcpu=cortex-m7 -mthumb -mfloat-abi=soft)
  set(UMCUB_CPU_FLAGS_HARD -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
endif()

set(UMCUB_FAMILY_INCLUDES
  ${UMCUB_FAMILY_DIR}/include
  ${UMCUB_FAMILY_DIR}
  ${UMCUB_ROOT}/third_party/st/cmsis_device_h7/Include
  ${UMCUB_ROOT}/third_party/st/stm32h7xx_hal_driver/Inc
  ${UMCUB_ROOT}/third_party/cmsis_core/Include
)
set(UMCUB_FAMILY_DEFS ${UMCUB_MCU} USE_FULL_LL_DRIVER ${_core_def})

# Linked into the bootloader.
set(UMCUB_FAMILY_BOOT_SOURCES
  ${UMCUB_FAMILY_DIR}/sys.c
  ${UMCUB_FAMILY_DIR}/common.c
  ${UMCUB_FAMILY_DIR}/clock.c
  ${UMCUB_FAMILY_DIR}/flash.c
  ${UMCUB_FAMILY_DIR}/dualcore.c
  ${UMCUB_ROOT}/third_party/st/cmsis_device_h7/Source/Templates/gcc/startup_${_mcu_lc}.s
)
set(UMCUB_FAMILY_UART_SOURCES ${UMCUB_FAMILY_DIR}/uart.c)
# Nonces / random back-off (umcub link).
set(UMCUB_FAMILY_ENTROPY_SOURCES ${UMCUB_FAMILY_DIR}/entropy.c)
set(UMCUB_FAMILY_CAN_SOURCES  ${UMCUB_FAMILY_DIR}/fdcan.c)
set(UMCUB_FAMILY_ETH_SOURCES  ${UMCUB_FAMILY_DIR}/eth.c)
set(UMCUB_FAMILY_USB_SOURCES  ${UMCUB_FAMILY_DIR}/usb.c)
# tinyUSB device controller driver + MCU option for tusb_config.h
set(UMCUB_FAMILY_TUSB_MCU OPT_MCU_STM32H7)
set(UMCUB_FAMILY_TUSB_SOURCES ${UMCUB_ROOT}/third_party/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
                              ${UMCUB_ROOT}/third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c)

# Application library sources: include/umcub_family_app.inc (pulled into
# lib/umcub_app/umcub_app_all.c, so IDE projects need no family source list).

# Linker script templates (C preprocessed with umcub_cfg.h).
set(UMCUB_FAMILY_BOOT_LD ${UMCUB_FAMILY_DIR}/ld/boot_${UMCUB_CORE}.ld.in)
set(UMCUB_FAMILY_APP_LD  ${UMCUB_FAMILY_DIR}/ld/app_${UMCUB_CORE}.ld.in)
