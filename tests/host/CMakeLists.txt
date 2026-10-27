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
umcub_check_pins()
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
umcub_generate_enc_key(${_dev_key} ${CMAKE_BINARY_DIR}/umcub_enc_key.c)

file(GLOB BOOTUTIL_SRCS ${MCUBOOT}/boot/bootutil/src/*.c)
list(FILTER BOOTUTIL_SRCS EXCLUDE REGEX "(encrypted|ed25519|image_rsa|ram_load|boot_record|_psa|_mbedtls)")

# Payloads with a vector table: the reset vector points into image 0's primary
# slot, or (payload_wrong.bin) to the start of flash - an application not linked for the slot.
set(_bin ${CMAKE_BINARY_DIR}/payload.bin)
set(_bin_wrong ${CMAKE_BINARY_DIR}/payload_wrong.bin)
math(EXPR _reset "${UMCUB_CFG_IMG0_PRIMARY_ADDR} + 0x400 + 0x200" OUTPUT_FORMAT HEXADECIMAL)
set(_reset_wrong 0x08000200)
add_custom_command(OUTPUT ${_bin} DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py ${_bin} 150000 ${_reset})
add_custom_command(OUTPUT ${_bin_wrong} DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py ${_bin_wrong} 150000 ${_reset_wrong})
# direct-xip: an application linked for image 0's secondary slot.
set(_bin_slot1 ${CMAKE_BINARY_DIR}/payload_slot1.bin)
math(EXPR _reset_slot1 "${UMCUB_CFG_IMG0_SECONDARY_ADDR} + 0x400 + 0x200" OUTPUT_FORMAT HEXADECIMAL)
add_custom_command(OUTPUT ${_bin_slot1} DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py ${_bin_slot1} 150000 ${_reset_slot1})

set_source_files_properties(${BOOTUTIL_SRCS} ${MCUBOOT}/boot/boot_serial/src/boot_serial.c
  ${MCUBOOT}/boot/zcbor/src/zcbor_common.c ${MCUBOOT}/boot/zcbor/src/zcbor_decode.c
  ${MCUBOOT}/boot/zcbor/src/zcbor_encode.c PROPERTIES COMPILE_OPTIONS "-w")

option(UMCUB_HOST_SANITIZE "Build host tests with ASan/UBSan (needs libasan/libubsan)" OFF)

# What every host test target links: the fake port, the mux and the image
# side with MCUboot (boot_serial, ECDSA); each target adds its transports.
set(HOST_SOURCES
    fake_port.c
    ${UMCUB_ROOT}/src/proto/mux.c
    ${UMCUB_ROOT}/src/util/log.c
    ${UMCUB_ROOT}/src/proto/cmd.c
    ${UMCUB_ROOT}/src/image/inspect.c
    ${UMCUB_ROOT}/src/proto/smp_inspect.c
    ${UMCUB_ROOT}/src/util/libc.c
    ${UMCUB_ROOT}/src/util/codec.c
    ${UMCUB_ROOT}/src/util/led.c
    ${UMCUB_ROOT}/src/proto/smp_os.c
    ${UMCUB_ROOT}/src/image/mcuboot/flash_map_backend.c
    ${UMCUB_ROOT}/src/image/mcuboot/hooks.c
    ${UMCUB_ROOT}/src/image/umcub.c
    ${UMCUB_ROOT}/src/image/slot_writer.c
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

# Includes, the probed configuration and the compiler options of a host test target.
function(umcub_host_setup t)
  add_dependencies(${t} pubkey)
  target_include_directories(${t} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${UMCUB_CONFIG_INCLUDES}
    ${UMCUB_FAMILY_DIR}/include
    ${UMCUB_LAYER_INCLUDES}
    ${MCUBOOT}/boot/bootutil/include
    ${MCUBOOT}/boot/bootutil/src
    ${MCUBOOT}/boot/boot_serial/include
    ${MCUBOOT}/boot/boot_serial/src
    ${MCUBOOT}/boot/boot_serial     # stopgap as in CMakeLists.txt: v2.5.0 "../bootutil/src/bootutil_loader.h" (direct-xip)
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
endfunction()

# Test images signed with imgtool directly: arguments for the board type TLV (protected): "--custom-tlv 0xa0 0x<u32 LE>",
# empty when UMCUB_CFG_BOARD_TYPE is 0. Same encoding as tools/host/umcub_image.py.
function(umcub_board_type_tlv_args out_var board_type)
  set(_args)
  if(board_type)
    set(_hex "")
    foreach(_i 0 1 2 3)
      math(EXPR _b "(${board_type} >> (8 * ${_i})) & 0xFF" OUTPUT_FORMAT HEXADECIMAL)
      string(SUBSTRING "${_b}" 2 -1 _b)
      string(LENGTH "${_b}" _n)
      if(_n EQUAL 1)
        set(_b "0${_b}")
      endif()
      string(APPEND _hex "${_b}")
    endforeach()
    set(_args --custom-tlv 0xa0 0x${_hex})
  endif()
  set(${out_var} ${_args} PARENT_SCOPE)
endfunction()

# One test executable per upgrade mode, each with its own probed config.
function(umcub_host_variant name)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_${name}.h)
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  umcub_check_pins(_${name})
  set(t umcub_host_${name})
  add_executable(${t}
    test_main.c
    ${UMCUB_ROOT}/src/transport/can/isotp.c
    ${UMCUB_ROOT}/src/transport/net/net.c
    ${UMCUB_ROOT}/src/transport/net/dhcp.c
    ${HOST_SOURCES})
  if(UMCUB_CFG_ENCRYPT_IMAGES)
    target_sources(${t} PRIVATE
      ${MCUBOOT}/boot/bootutil/src/encrypted.c
      ${UMCUB_ROOT}/src/image/mcuboot/enc_image.c
      ${TINYCRYPT}/source/ecc_dh.c
      ${TINYCRYPT}/source/hmac.c
      ${TINYCRYPT}/source/aes_encrypt.c
      ${TINYCRYPT}/source/ctr_mode.c
      ${CMAKE_BINARY_DIR}/umcub_enc_key.c)
    set_source_files_properties(${MCUBOOT}/boot/bootutil/src/encrypted.c PROPERTIES COMPILE_OPTIONS "-w")
  endif()
  umcub_host_setup(${t})

  # Signed images sized like umcub_sign_image() does for this mode.
  set(_slot ${UMCUB_CFG_IMG0_PRIMARY_SIZE})
  set(_max ${UMCUB_CFG_IMG0_PRIMARY_SIZE})
  if(UMCUB_CFG_IMG0_SECONDARY_SIZE LESS _slot)
    set(_slot ${UMCUB_CFG_IMG0_SECONDARY_SIZE})
  else()
    set(_max ${UMCUB_CFG_IMG0_SECONDARY_SIZE})
  endif()
  math(EXPR _sectors "${_max} / ${UMCUB_CFG_FAMILY_MIN_SECTOR}")
  # 1.0.0 and 1.1.0 for this board type, 1.2.0 signed with the same key but for another board,
  # 1.3.0 (plain variants only) linked for the start of flash.
  umcub_board_type_tlv_args(_tlv "${UMCUB_CFG_BOARD_TYPE}")
  math(EXPR _other "${UMCUB_CFG_BOARD_TYPE} + 1")
  umcub_board_type_tlv_args(_tlv_other "${_other}")
  # Encrypted variant: the same three encrypted with the device key, then
  # 1.0.0 and 1.1.0 in plain text as the reference for what lands in flash.
  set(_imgs)
  set(_enc)
  if(UMCUB_CFG_ENCRYPT_IMAGES)
    set(_enc --encrypt ${_dev_key})
    if(UMCUB_CFG_SWAP_SAVE_ENCTLV)
      list(APPEND _enc --save-enctlv)
    endif()
  endif()
  set(_mode)
  if(UMCUB_CFG_UPGRADE_MODE EQUAL 1)    # UMCUB_MODE_OVERWRITE: smaller trailer, as umcub_image.py signs
    set(_mode --overwrite-only)
  endif()
  set(_key -k ${UMCUB_SIGNING_KEY})
  if(UMCUB_CFG_SIGNATURE EQUAL 0)   # UMCUB_SIGN_NONE: SHA-256 TLV only
    set(_key)
  endif()
  set(_xip_only)
  if(UMCUB_CFG_UPGRADE_MODE EQUAL 5)    # UMCUB_MODE_DIRECT_XIP: 1.4.0 linked for the secondary slot
    set(_xip_only 1.4.0)
  endif()
  foreach(v 1.0.0 1.1.0 1.2.0 1.3.0 plain-1.0.0 plain-1.1.0 ${_xip_only})
    set(_o ${CMAKE_BINARY_DIR}/${name}_${v}.bin)
    string(REPLACE "plain-" "" _ver ${v})
    set(_e ${_enc})
    set(_in ${_bin})
    if(v STREQUAL "1.3.0")
      if(UMCUB_CFG_ENCRYPT_IMAGES)
        continue()
      endif()
      set(_in ${_bin_wrong})
    endif()
    if(v STREQUAL "1.4.0")
      set(_in ${_bin_slot1})
    endif()
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
    add_custom_command(OUTPUT ${_o} DEPENDS ${_in}
      COMMAND ${UMCUB_IMGTOOL} sign ${_key} ${_mode} --version ${_ver} --header-size 0x400 --pad-header
              --slot-size ${_slot} --align 32 --max-align 32 --max-sectors ${_sectors} ${_t} ${_e} ${_in} ${_o})
    list(APPEND _imgs ${_o})
  endforeach()
  add_custom_target(${t}_images ALL DEPENDS ${_imgs})
  add_test(NAME ${t} COMMAND ${t} ${_imgs})
endfunction()

umcub_host_variant(swap_scratch)
umcub_host_variant(swap_offset)
umcub_host_variant(sign_none)
umcub_host_variant(downgrade)
umcub_host_variant(overwrite)
umcub_host_variant(direct_xip)
umcub_host_variant(swap_scratch_enc)
umcub_host_variant(swap_scratch_enc_nobt)
umcub_host_variant(swap_scratch_enc_rawkey)
umcub_host_variant(swap_offset_direct)
umcub_host_variant(swap_scratch_enc_direct)

# Board-supplied drivers (UMCUB_DRIVER_BOARD) and board transport
# (UMCUB_CFG_TRANSPORT_USER) through the real registry, CAN/ETH transports, mux.
function(umcub_host_board)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_swap_scratch.h)
  set(UMCUB_CONFIG_POST ${CMAKE_CURRENT_LIST_DIR}/test_board_post.h)
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  umcub_check_pins(_board)
  set(t umcub_host_board)
  add_executable(${t}
    test_board.c
    ${UMCUB_ROOT}/src/transport/registry.c
    ${UMCUB_ROOT}/src/transport/can/can_transport.c
    ${UMCUB_ROOT}/src/transport/can/isotp.c
    ${UMCUB_ROOT}/src/transport/net/net_transport.c
    ${UMCUB_ROOT}/src/transport/net/net.c
    ${UMCUB_ROOT}/src/transport/net/dhcp.c
    ${HOST_SOURCES})
  umcub_host_setup(${t})
  add_test(NAME ${t} COMMAND ${t})
endfunction()

umcub_host_board()

# umcub link through the mux on stream and packet transports: ADDRESSED
# (test_link.c) and SECURE with encryption (test_link_secure.c, extra sources).
function(umcub_host_link t src post)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_swap_scratch.h)
  set(UMCUB_CONFIG_POST ${CMAKE_CURRENT_LIST_DIR}/${post})
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  umcub_check_pins(_${t})
  add_executable(${t}
    ${src}
    ${ARGN}
    ${UMCUB_ROOT}/src/proto/link.c
    ${UMCUB_ROOT}/src/proto/lite.c
    ${UMCUB_ROOT}/src/util/random.c
    ${HOST_SOURCES})
  umcub_host_setup(${t})
  if(NOT UMCUB_SIM)
    add_test(NAME ${t} COMMAND ${t})
  endif()
endfunction()

# SECURE: session crypto, encrypted images, the link keys.
set(LINK_SECURE_SOURCES
  ${MCUBOOT}/boot/bootutil/src/encrypted.c
  ${UMCUB_ROOT}/src/image/mcuboot/enc_image.c
  ${CMAKE_BINARY_DIR}/umcub_enc_key.c
  ${TINYCRYPT}/source/ecc_dh.c
  ${TINYCRYPT}/source/hmac.c
  ${TINYCRYPT}/source/aes_encrypt.c
  ${TINYCRYPT}/source/ctr_mode.c
  ${CMAKE_BINARY_DIR}/umcub_link_keys.c)
umcub_host_link(umcub_host_link test_link.c test_link_proto_post.h link_test.c)
umcub_host_link(umcub_host_link_secure test_link_secure.c test_link_secure_post.h link_test.c ${LINK_SECURE_SOURCES}
  ${CMAKE_BINARY_DIR}/umcub_link_host_keys.c)

# Two images with encrypted uploads (test_dual_enc.c); images signed like an IDE
# project does it, with tools/host/umcub_image.py on the variant's configuration.
umcub_python(_py)
function(umcub_host_dual name)
  set(UMCUB_CONFIG_PRE ${CMAKE_CURRENT_LIST_DIR}/cfg_${name}.h)
  umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
  umcub_check_pins(_${name})
  set(t umcub_host_${name})
  add_executable(${t} test_dual_enc.c ${HOST_SOURCES} ${LINK_SECURE_SOURCES})
  umcub_host_setup(${t})
  set(_imgs)
  foreach(spec "0;0x0;2.0.0;img0" "1;0x1;2.0.0;img1v0" "1;0x1;2.1.0;img1v1")
    list(GET spec 0 _img)
    list(GET spec 2 _ver)
    list(GET spec 3 _base)
    if(_img EQUAL 0)
      set(_slot ${UMCUB_CFG_IMG0_PRIMARY_ADDR})
    else()
      set(_slot ${UMCUB_CFG_IMG1_PRIMARY_ADDR})
    endif()
    math(EXPR _reset "${_slot} + 0x400 + 0x200" OUTPUT_FORMAT HEXADECIMAL)
    set(_b ${CMAKE_BINARY_DIR}/${name}_${_base})
    add_custom_command(OUTPUT ${_b}.signed.bin ${_b}.encrypted.bin
      DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py ${UMCUB_ROOT}/tools/host/umcub_image.py
      COMMAND ${_py} ${CMAKE_CURRENT_LIST_DIR}/gen_payload.py ${_b}.payload.bin 150000 ${_reset}
      COMMAND ${_py} ${UMCUB_ROOT}/tools/host/umcub_image.py sign --board ${UMCUB_BOARD} --core ${UMCUB_CORE} --quiet
              --pre ${UMCUB_CONFIG_PRE_ABS} --post ${UMCUB_CONFIG_POST_ABS} --imgtool ${UMCUB_IMGTOOL}
              --image ${_img} --version ${_ver} ${_b}.payload.bin -o ${_b})
    list(APPEND _imgs ${_b}.signed.bin ${_b}.encrypted.bin)
  endforeach()
  add_custom_target(${t}_images ALL DEPENDS ${_imgs})
  set(_d ${CMAKE_BINARY_DIR}/${name})
  add_test(NAME ${t} COMMAND ${t} ${_d}_img0.signed.bin ${_d}_img0.encrypted.bin ${_d}_img1v0.signed.bin
           ${_d}_img1v1.signed.bin ${_d}_img1v1.encrypted.bin)
endfunction()

umcub_host_dual(dual_enc)
umcub_host_dual(dual_enc_direct)

# Device simulators for tests/host/link_e2e.py (several devices on one bus with
# the umcub tool and smpmgr): ADDRESSED and SECURE (+ encryption); plain SMP;
# CAN (ISO-TP).
set(UMCUB_SIM 1)
umcub_host_link(umcub_sim_plain sim_device.c sim_plain_post.h)
umcub_host_link(umcub_sim_can sim_can.c sim_can_post.h ${UMCUB_ROOT}/src/transport/registry.c
  ${UMCUB_ROOT}/src/transport/can/can_transport.c ${UMCUB_ROOT}/src/transport/can/isotp.c)
# Without SMP: lite upload protocol (umcub upload --lite) on a stream and on CAN.
umcub_host_link(umcub_sim_lite sim_device.c sim_lite_post.h)
umcub_host_link(umcub_sim_can_lite sim_can.c sim_can_lite_post.h ${UMCUB_ROOT}/src/transport/registry.c
  ${UMCUB_ROOT}/src/transport/can/can_transport.c ${UMCUB_ROOT}/src/transport/can/isotp.c)
umcub_host_link(umcub_sim_addressed sim_device.c test_link_post.h)
umcub_host_link(umcub_sim_secure sim_device.c test_link_secure_post.h ${LINK_SECURE_SOURCES})
unset(UMCUB_SIM)
