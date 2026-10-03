# umcub - universal MCUboot-based bootloader for STM32.
#
#   cmake --preset <preset>            (see CMakePresets.json)
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DUMCUB_BOARD=nucleo_h755zi_q -DUMCUB_CORE=cm7
#
# Everything else is configured in boards/<board>/umcub_config.h.

cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
endif()

project(umcub VERSION 0.1.0 LANGUAGES C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

set(UMCUB_BOARD "nucleo_h755zi_q" CACHE STRING "Board directory name under boards/ or absolute path")
set(UMCUB_CORE "" CACHE STRING "Core to build for on dual-core parts (cm7|cm4)")
set(UMCUB_CONFIG_PRE "" CACHE FILEPATH "Optional header included before the board config")
set(UMCUB_CONFIG_POST "" CACHE FILEPATH "Optional header included after the board config")
set(UMCUB_SIGNING_KEY "${CMAKE_CURRENT_LIST_DIR}/tools/keys/dev-ecdsa-p256.pem" CACHE FILEPATH
    "ECDSA-P256 key whose public part is embedded in the bootloader")
set(UMCUB_IMGTOOL "" CACHE FILEPATH "imgtool executable (default: .venv/bin/imgtool or PATH)")

include(cmake/umcub_config.cmake)
umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
get_filename_component(UMCUB_BOARD_NAME "${UMCUB_BOARD_DIR}" NAME)

message(STATUS "umcub: board ${UMCUB_BOARD_NAME}, ${UMCUB_MCU} (${UMCUB_FAMILY}/${UMCUB_CORE}), "
               "mode ${UMCUB_CFG_UPGRADE_MODE}, images ${UMCUB_CFG_IMAGE_NUMBER}, "
               "transports: uart=${UMCUB_CFG_TRANSPORT_UART} usb_cdc=${UMCUB_CFG_TRANSPORT_USB_CDC} "
               "usb_dfu=${UMCUB_CFG_TRANSPORT_USB_DFU} can=${UMCUB_CFG_TRANSPORT_CAN} eth=${UMCUB_CFG_TRANSPORT_ETH}")

include(cmake/umcub_imgtool.cmake)

# ----------------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------------
set(MCUBOOT ${UMCUB_ROOT}/third_party/mcuboot)
set(TINYCRYPT ${MCUBOOT}/ext/tinycrypt/lib)

set(BOOT_SOURCES
  boot/main.c
  boot/startup.c
  boot/log.c
  boot/handoff.c
  boot/info.c
  boot/cmd.c
  boot/inspect.c
  mcuboot_port/src/flash_map_backend.c
  mcuboot_port/src/compat.c
  transport/registry.c
  transport/mux.c
  ${UMCUB_FAMILY_BOOT_SOURCES}
)

set(MCUBOOT_SOURCES
  ${MCUBOOT}/boot/bootutil/src/bootutil_area.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_find_key.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_img_hash.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_img_security_cnt.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_loader.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_misc.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_public.c
  ${MCUBOOT}/boot/bootutil/src/caps.c
  ${MCUBOOT}/boot/bootutil/src/fault_injection_hardening.c
  ${MCUBOOT}/boot/bootutil/src/image_validate.c
  ${MCUBOOT}/boot/bootutil/src/loader.c
  ${MCUBOOT}/boot/bootutil/src/swap_misc.c
  ${MCUBOOT}/boot/bootutil/src/swap_scratch.c
  ${MCUBOOT}/boot/bootutil/src/swap_move.c
  ${MCUBOOT}/boot/bootutil/src/swap_offset.c
  ${MCUBOOT}/boot/bootutil/src/tlv.c
  ${TINYCRYPT}/source/sha256.c
  ${TINYCRYPT}/source/utils.c
)
if(UMCUB_CFG_SMP)                 # some transport speaks SMP: MCUboot serial recovery
  list(APPEND BOOT_SOURCES boot/smp_inspect.c)
  list(APPEND MCUBOOT_SOURCES
    ${MCUBOOT}/boot/boot_serial/src/boot_serial.c
    ${MCUBOOT}/boot/boot_serial/src/zcbor_bulk.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_common.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_decode.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_encode.c
  )
endif()
if(UMCUB_CFG_SIGNATURE EQUAL 1)   # UMCUB_SIGN_EC256
  list(APPEND MCUBOOT_SOURCES
    ${MCUBOOT}/boot/bootutil/src/image_ecdsa.c
    ${TINYCRYPT}/source/ecc.c
    ${TINYCRYPT}/source/ecc_dsa.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/asn1parse.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/platform_util.c
    ${CMAKE_BINARY_DIR}/umcub_keys.c
  )
  umcub_generate_pubkey("${UMCUB_SIGNING_KEY}" "${CMAKE_BINARY_DIR}/umcub_keys.c")
endif()

set(BOOT_INCLUDES
  ${UMCUB_CONFIG_INCLUDES}
  ${UMCUB_FAMILY_INCLUDES}
  ${UMCUB_ROOT}/port/include
  ${UMCUB_ROOT}/boot/include
  ${UMCUB_ROOT}/lib/umcub_app/include
  ${UMCUB_ROOT}/mcuboot_port/include
  ${UMCUB_ROOT}/transport/include
  ${MCUBOOT}/boot/bootutil/include
  ${MCUBOOT}/boot/bootutil/src
  ${MCUBOOT}/boot/boot_serial/include
  ${MCUBOOT}/boot/boot_serial/src
  ${MCUBOOT}/boot/zcbor/include
  ${TINYCRYPT}/include
  ${MCUBOOT}/ext/mbedtls-asn1/include
)

# Optional board hooks: umcub_cmd_user() for UMCUB_CMD_USER commands, board
# drivers (UMCUB_DRIVER_BOARD), the board transport (UMCUB_CFG_TRANSPORT_USER).
if(EXISTS "${UMCUB_BOARD_DIR}/umcub_board.c")
  list(APPEND BOOT_SOURCES "${UMCUB_BOARD_DIR}/umcub_board.c")
elseif(UMCUB_CFG_TRANSPORT_USER OR (UMCUB_CFG_TRANSPORT_CAN AND UMCUB_CFG_CAN_DRIVER EQUAL 1)
       OR (UMCUB_CFG_TRANSPORT_ETH AND UMCUB_CFG_ETH_DRIVER EQUAL 1))
  message(FATAL_ERROR "umcub: the configuration needs ${UMCUB_BOARD_DIR}/umcub_board.c "
                      "(UMCUB_CFG_TRANSPORT_USER or a UMCUB_DRIVER_BOARD driver)")
endif()

# --- transports -------------------------------------------------------------
if(UMCUB_CFG_TRANSPORT_UART)
  list(APPEND BOOT_SOURCES transport/uart/uart_transport.c ${UMCUB_FAMILY_UART_SOURCES})
endif()
if(UMCUB_CFG_TRANSPORT_USB_CDC OR UMCUB_CFG_TRANSPORT_USB_DFU)
  set(TUSB ${UMCUB_ROOT}/third_party/tinyusb/src)
  list(APPEND BOOT_SOURCES
    transport/usb/usb_transport.c
    transport/usb/usb_descriptors.c
    ${UMCUB_FAMILY_USB_SOURCES}
    ${UMCUB_FAMILY_TUSB_SOURCES}
    ${TUSB}/tusb.c
    ${TUSB}/common/tusb_fifo.c
    ${TUSB}/device/usbd.c)
  if(UMCUB_CFG_TRANSPORT_USB_CDC)
    list(APPEND BOOT_SOURCES ${TUSB}/class/cdc/cdc_device.c)
  endif()
  if(UMCUB_CFG_TRANSPORT_USB_DFU)
    list(APPEND BOOT_SOURCES ${TUSB}/class/dfu/dfu_device.c transport/usb/usb_dfu.c
      lib/umcub_app/src/slot_writer.c lib/umcub_app/src/umcub.c)
  endif()
  list(APPEND BOOT_INCLUDES ${TUSB} ${UMCUB_ROOT}/transport/usb)
endif()
if(UMCUB_CFG_TRANSPORT_CAN)
  list(APPEND BOOT_SOURCES transport/can/can_transport.c transport/can/isotp.c)
  if(UMCUB_CFG_CAN_DRIVER EQUAL 0)      # UMCUB_DRIVER_PORT; BOARD: umcub_port_can_* in umcub_board.c
    list(APPEND BOOT_SOURCES ${UMCUB_FAMILY_CAN_SOURCES})
  endif()
endif()
if(UMCUB_CFG_TRANSPORT_ETH)
  list(APPEND BOOT_SOURCES
    transport/net/net_transport.c
    transport/net/net.c
    transport/net/dhcp.c)
  if(UMCUB_CFG_ETH_DRIVER EQUAL 0)      # UMCUB_DRIVER_PORT; BOARD: umcub_port_eth_* in umcub_board.c
    list(APPEND BOOT_SOURCES ${UMCUB_FAMILY_ETH_SOURCES})
  endif()
endif()

# ----------------------------------------------------------------------------
# Target
# ----------------------------------------------------------------------------
add_executable(umcub_boot ${BOOT_SOURCES} ${MCUBOOT_SOURCES})
set_target_properties(umcub_boot PROPERTIES OUTPUT_NAME "umcub_${UMCUB_BOARD_NAME}_${UMCUB_CORE}")

target_include_directories(umcub_boot PRIVATE ${BOOT_INCLUDES})

execute_process(COMMAND git -C ${UMCUB_ROOT} rev-parse --short=8 HEAD
                OUTPUT_VARIABLE UMCUB_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT UMCUB_GIT MATCHES "^[0-9a-f]+$")
  set(UMCUB_GIT 0)
endif()

target_compile_definitions(umcub_boot PRIVATE
  ${UMCUB_CONFIG_DEFS}
  UMCUB_GIT_HASH=0x${UMCUB_GIT}u
  UMCUB_BOARD_NAME="${UMCUB_BOARD_NAME}"
  MBEDTLS_CONFIG_FILE="umcub_mbedtls_cfg.h"
  $<$<BOOL:${UMCUB_CFG_USB}>:CFG_TUSB_MCU=${UMCUB_FAMILY_TUSB_MCU}>
)

target_compile_options(umcub_boot PRIVATE
  ${UMCUB_CPU_FLAGS_SOFT}
  -Wall -Wextra -Wno-unused-parameter
  $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>
)
# Third-party code: compile, but do not drown our warnings in theirs.
set_source_files_properties(${MCUBOOT_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
if(UMCUB_CFG_USB)
  file(GLOB_RECURSE _tusb_srcs ${UMCUB_ROOT}/third_party/tinyusb/src/*.c)
  set_source_files_properties(${_tusb_srcs} PROPERTIES COMPILE_OPTIONS "-w")
endif()

umcub_preprocess("${UMCUB_FAMILY_BOOT_LD}" "${CMAKE_BINARY_DIR}/umcub_boot.ld")
set_target_properties(umcub_boot PROPERTIES LINK_DEPENDS "${CMAKE_BINARY_DIR}/umcub_boot.ld")

target_link_options(umcub_boot PRIVATE
  ${UMCUB_CPU_FLAGS_SOFT}
  -T${CMAKE_BINARY_DIR}/umcub_boot.ld
  -Wl,--gc-sections
  -Wl,-Map=$<TARGET_FILE_DIR:umcub_boot>/$<TARGET_FILE_BASE_NAME:umcub_boot>.map
  -Wl,--print-memory-usage
  -Wl,--no-warn-rwx-segments
  --specs=nano.specs
  --specs=nosys.specs
  -nostartfiles
)

add_custom_command(TARGET umcub_boot POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:umcub_boot> $<TARGET_FILE_DIR:umcub_boot>/$<TARGET_FILE_BASE_NAME:umcub_boot>.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:umcub_boot> $<TARGET_FILE_DIR:umcub_boot>/$<TARGET_FILE_BASE_NAME:umcub_boot>.hex
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:umcub_boot>
  VERBATIM)
