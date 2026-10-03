# Host (x86) tests for umcub: real MCUboot + umcub transports on emulated
# STM32H7 flash.   cmake -S tests/host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.20)
project(umcub_host_tests C)
enable_testing()

set(UMCUB_BOARD nucleo_h755zi_q)
set(UMCUB_CORE cm7)
set(UMCUB_CONFIG_POST ${CMAKE_CURRENT_LIST_DIR}/test_post.h)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/umcub_config.cmake)
set(UMCUB_SIGNING_KEY ${UMCUB_ROOT}/tools/keys/dev-ecdsa-p256.pem)
set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_swap_scratch.h)
umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
include(${UMCUB_ROOT}/cmake/umcub_imgtool.cmake)

set(MCUBOOT ${UMCUB_ROOT}/third_party/mcuboot)
set(TINYCRYPT ${MCUBOOT}/ext/tinycrypt/lib)
umcub_generate_pubkey("${UMCUB_SIGNING_KEY}" "${CMAKE_BINARY_DIR}/umcub_keys.c")
add_custom_target(pubkey DEPENDS ${CMAKE_BINARY_DIR}/umcub_keys.c)
# umcub link SECURE: the device side (embedded) and the host side (test as host).
set(_dev_key ${UMCUB_ROOT}/tools/keys/dev-device-p256.pem)
set(_adm_key ${UMCUB_ROOT}/tools/keys/dev-admin-p256.pem)
umcub_keys_command(c ${_dev_key} ${_adm_key} ${CMAKE_BINARY_DIR}/umcub_link_keys.c)
umcub_keys_command(host-c ${_dev_key} ${_adm_key} ${CMAKE_BINARY_DIR}/umcub_link_host_keys.c)
umcub_keys_command(enc-c ${_dev_key} "" ${CMAKE_BINARY_DIR}/umcub_enc_key.c)

file(GLOB BOOTUTIL_SRCS ${MCUBOOT}/boot/bootutil/src/*.c)
list(FILTER BOOTUTIL_SRCS EXCLUDE REGEX "(encrypted|ed25519|image_rsa|ram_load|boot_record|_psa|_mbedtls)")

set(_bin ${CMAKE_BINARY_DIR}/payload.bin)
add_custom_command(OUTPUT ${_bin}
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py ${_bin} 150000)

set_source_files_properties(${BOOTUTIL_SRCS} ${MCUBOOT}/boot/boot_serial/src/boot_serial.c
  ${MCUBOOT}/boot/zcbor/src/zcbor_common.c ${MCUBOOT}/boot/zcbor/src/zcbor_decode.c
  ${MCUBOOT}/boot/zcbor/src/zcbor_encode.c PROPERTIES COMPILE_OPTIONS "-w")

option(UMCUB_HOST_SANITIZE "Build host tests with ASan/UBSan (needs libasan/libubsan)" OFF)

# One test executable per upgrade mode, each with its own probed config.
function(umcub_host_variant name)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_${name}.h)
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  set(t umcub_host_${name})
  add_executable(${t}
    test_main.c
    fake_port.c
    ${UMCUB_ROOT}/transport/mux.c
    ${UMCUB_ROOT}/transport/can/isotp.c
    ${UMCUB_ROOT}/transport/net/net.c
    ${UMCUB_ROOT}/transport/net/dhcp.c
    ${UMCUB_ROOT}/boot/log.c
    ${UMCUB_ROOT}/boot/cmd.c
    ${UMCUB_ROOT}/boot/inspect.c
    ${UMCUB_ROOT}/boot/smp_inspect.c
    ${UMCUB_ROOT}/mcuboot_port/src/compat.c
    ${UMCUB_ROOT}/mcuboot_port/src/flash_map_backend.c
    ${UMCUB_ROOT}/mcuboot_port/src/hooks.c
    ${UMCUB_ROOT}/lib/umcub_app/src/umcub.c
    ${UMCUB_ROOT}/lib/umcub_app/src/slot_writer.c
    ${BOOTUTIL_SRCS}
    ${MCUBOOT}/boot/boot_serial/src/boot_serial.c
    ${MCUBOOT}/boot/boot_serial/src/zcbor_bulk.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_common.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_decode.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_encode.c
    ${TINYCRYPT}/source/sha256.c
    ${TINYCRYPT}/source/utils.c
    ${TINYCRYPT}/source/ecc.c
    ${TINYCRYPT}/source/ecc_dsa.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/asn1parse.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/platform_util.c
    ${CMAKE_BINARY_DIR}/umcub_keys.c)
  if(UMCUB_CFG_ENCRYPT_IMAGES)
    target_sources(${t} PRIVATE
      ${MCUBOOT}/boot/bootutil/src/encrypted.c
      ${UMCUB_ROOT}/mcuboot_port/src/enc_image.c
      ${TINYCRYPT}/source/ecc_dh.c
      ${TINYCRYPT}/source/hmac.c
      ${TINYCRYPT}/source/aes_encrypt.c
      ${TINYCRYPT}/source/ctr_mode.c
      ${CMAKE_BINARY_DIR}/umcub_enc_key.c)
    set_source_files_properties(${MCUBOOT}/boot/bootutil/src/encrypted.c PROPERTIES COMPILE_OPTIONS "-w")
  endif()
  add_dependencies(${t} pubkey)
  target_include_directories(${t} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${UMCUB_CONFIG_INCLUDES}
    ${UMCUB_FAMILY_DIR}/include
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
    ${MCUBOOT}/ext/mbedtls-asn1/include)
  target_compile_definitions(${t} PRIVATE
    ${UMCUB_CONFIG_DEFS} UMCUB_HOST_TEST=1 MBEDTLS_CONFIG_FILE="umcub_mbedtls_cfg.h")
  target_compile_options(${t} PRIVATE -g -O1 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections)
  target_link_options(${t} PRIVATE -Wl,--gc-sections)
  if(UMCUB_HOST_SANITIZE)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endif()

  # Signed images sized like umcub_sign_image() does for this mode.
  set(_slot ${UMCUB_CFG_IMG0_PRIMARY_SIZE})
  set(_max ${UMCUB_CFG_IMG0_PRIMARY_SIZE})
  if(UMCUB_CFG_IMG0_SECONDARY_SIZE LESS _slot)
    set(_slot ${UMCUB_CFG_IMG0_SECONDARY_SIZE})
  else()
    set(_max ${UMCUB_CFG_IMG0_SECONDARY_SIZE})
  endif()
  math(EXPR _sectors "${_max} / ${UMCUB_CFG_FAMILY_MIN_SECTOR}")
  # 1.0.0 and 1.1.0 for this board type, 1.2.0 signed with the same key but for another board.
  umcub_board_type_tlv_args(_tlv "${UMCUB_CFG_BOARD_TYPE}")
  math(EXPR _other "${UMCUB_CFG_BOARD_TYPE} + 1")
  umcub_board_type_tlv_args(_tlv_other "${_other}")
  # Encrypted variant: the same three encrypted with the device key, then
  # 1.0.0 and 1.1.0 in plain text as the reference for what lands in flash.
  set(_imgs)
  set(_enc)
  if(UMCUB_CFG_ENCRYPT_IMAGES)
    set(_enc --encrypt ${_dev_key})
  endif()
  foreach(v 1.0.0 1.1.0 1.2.0 plain-1.0.0 plain-1.1.0)
    set(_o ${CMAKE_BINARY_DIR}/${name}_${v}.bin)
    string(REPLACE "plain-" "" _ver ${v})
    set(_e ${_enc})
    if(v MATCHES "^plain-")
      if(NOT UMCUB_CFG_ENCRYPT_IMAGES)
        continue()
      endif()
      set(_e)
    endif()
    if(v STREQUAL "1.2.0")
      set(_t ${_tlv_other})
    else()
      set(_t ${_tlv})
    endif()
    add_custom_command(OUTPUT ${_o} DEPENDS ${_bin}
      COMMAND ${UMCUB_IMGTOOL} sign -k ${UMCUB_SIGNING_KEY} --version ${_ver} --header-size 0x400 --pad-header
              --slot-size ${_slot} --align 32 --max-align 32 --max-sectors ${_sectors} ${_t} ${_e} ${_bin} ${_o})
    list(APPEND _imgs ${_o})
  endforeach()
  add_custom_target(${t}_images ALL DEPENDS ${_imgs})
  add_test(NAME ${t} COMMAND ${t} ${_imgs})
endfunction()

umcub_host_variant(swap_scratch)
umcub_host_variant(swap_move)
umcub_host_variant(swap_offset)
umcub_host_variant(swap_scratch_enc)

# Board-supplied drivers (UMCUB_DRIVER_BOARD) and board transport
# (UMCUB_CFG_TRANSPORT_USER) through the real registry, CAN/ETH transports, mux.
function(umcub_host_board)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_swap_scratch.h)
  set(UMCUB_CONFIG_POST ${CMAKE_CURRENT_LIST_DIR}/test_board_post.h)
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  set(t umcub_host_board)
  add_executable(${t}
    test_board.c
    fake_port.c
    ${UMCUB_ROOT}/transport/registry.c
    ${UMCUB_ROOT}/transport/mux.c
    ${UMCUB_ROOT}/transport/can/can_transport.c
    ${UMCUB_ROOT}/transport/can/isotp.c
    ${UMCUB_ROOT}/transport/net/net_transport.c
    ${UMCUB_ROOT}/transport/net/net.c
    ${UMCUB_ROOT}/transport/net/dhcp.c
    ${UMCUB_ROOT}/boot/log.c
    ${UMCUB_ROOT}/boot/cmd.c
    ${UMCUB_ROOT}/boot/inspect.c
    ${UMCUB_ROOT}/boot/smp_inspect.c
    ${UMCUB_ROOT}/mcuboot_port/src/compat.c
    ${UMCUB_ROOT}/mcuboot_port/src/flash_map_backend.c
    ${UMCUB_ROOT}/mcuboot_port/src/hooks.c
    ${UMCUB_ROOT}/lib/umcub_app/src/umcub.c
    ${UMCUB_ROOT}/lib/umcub_app/src/slot_writer.c
    ${BOOTUTIL_SRCS}
    ${MCUBOOT}/boot/boot_serial/src/boot_serial.c
    ${MCUBOOT}/boot/boot_serial/src/zcbor_bulk.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_common.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_decode.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_encode.c
    ${TINYCRYPT}/source/sha256.c
    ${TINYCRYPT}/source/utils.c
    ${TINYCRYPT}/source/ecc.c
    ${TINYCRYPT}/source/ecc_dsa.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/asn1parse.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/platform_util.c
    ${CMAKE_BINARY_DIR}/umcub_keys.c)
  add_dependencies(${t} pubkey)
  target_include_directories(${t} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${UMCUB_CONFIG_INCLUDES}
    ${UMCUB_FAMILY_DIR}/include
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
    ${MCUBOOT}/ext/mbedtls-asn1/include)
  target_compile_definitions(${t} PRIVATE
    ${UMCUB_CONFIG_DEFS} UMCUB_HOST_TEST=1 MBEDTLS_CONFIG_FILE="umcub_mbedtls_cfg.h")
  target_compile_options(${t} PRIVATE -g -O1 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections)
  target_link_options(${t} PRIVATE -Wl,--gc-sections)
  if(UMCUB_HOST_SANITIZE)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ${t} COMMAND ${t})
endfunction()

umcub_host_board()

# umcub link through the mux on stream and packet transports: ADDRESSED
# (test_link.c) and SECURE with encryption (test_link_secure.c, extra sources).
function(umcub_host_link t src post)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_swap_scratch.h)
  set(UMCUB_CONFIG_POST ${CMAKE_CURRENT_LIST_DIR}/${post})
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  add_executable(${t}
    ${src}
    ${ARGN}
    ${UMCUB_ROOT}/transport/link.c
    ${UMCUB_ROOT}/boot/random.c
    fake_port.c
    ${UMCUB_ROOT}/transport/mux.c
    ${UMCUB_ROOT}/boot/log.c
    ${UMCUB_ROOT}/boot/cmd.c
    ${UMCUB_ROOT}/boot/inspect.c
    ${UMCUB_ROOT}/boot/smp_inspect.c
    ${UMCUB_ROOT}/mcuboot_port/src/compat.c
    ${UMCUB_ROOT}/mcuboot_port/src/flash_map_backend.c
    ${UMCUB_ROOT}/mcuboot_port/src/hooks.c
    ${UMCUB_ROOT}/lib/umcub_app/src/umcub.c
    ${UMCUB_ROOT}/lib/umcub_app/src/slot_writer.c
    ${BOOTUTIL_SRCS}
    ${MCUBOOT}/boot/boot_serial/src/boot_serial.c
    ${MCUBOOT}/boot/boot_serial/src/zcbor_bulk.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_common.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_decode.c
    ${MCUBOOT}/boot/zcbor/src/zcbor_encode.c
    ${TINYCRYPT}/source/sha256.c
    ${TINYCRYPT}/source/utils.c
    ${TINYCRYPT}/source/ecc.c
    ${TINYCRYPT}/source/ecc_dsa.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/asn1parse.c
    ${MCUBOOT}/ext/mbedtls-asn1/src/platform_util.c
    ${CMAKE_BINARY_DIR}/umcub_keys.c)
  add_dependencies(${t} pubkey)
  target_include_directories(${t} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${UMCUB_CONFIG_INCLUDES}
    ${UMCUB_FAMILY_DIR}/include
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
    ${MCUBOOT}/ext/mbedtls-asn1/include)
  target_compile_definitions(${t} PRIVATE
    ${UMCUB_CONFIG_DEFS} UMCUB_HOST_TEST=1 MBEDTLS_CONFIG_FILE="umcub_mbedtls_cfg.h")
  target_compile_options(${t} PRIVATE -g -O1 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections)
  target_link_options(${t} PRIVATE -Wl,--gc-sections)
  if(UMCUB_HOST_SANITIZE)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ${t} COMMAND ${t})
endfunction()

umcub_host_link(umcub_host_link test_link.c test_link_post.h)
umcub_host_link(umcub_host_link_secure test_link_secure.c test_link_secure_post.h
  ${MCUBOOT}/boot/bootutil/src/encrypted.c
  ${UMCUB_ROOT}/mcuboot_port/src/enc_image.c
  ${CMAKE_BINARY_DIR}/umcub_enc_key.c
  ${TINYCRYPT}/source/ecc_dh.c
  ${TINYCRYPT}/source/hmac.c
  ${TINYCRYPT}/source/aes_encrypt.c
  ${TINYCRYPT}/source/ctr_mode.c
  ${CMAKE_BINARY_DIR}/umcub_link_keys.c
  ${CMAKE_BINARY_DIR}/umcub_link_host_keys.c)
