# imgtool integration: public key embedding (bootloader) and image signing
# (applications). Requires umcub_config.cmake to be loaded first.

get_filename_component(UMCUB_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT UMCUB_IMGTOOL)
  find_program(UMCUB_IMGTOOL_FOUND imgtool
    HINTS "${UMCUB_ROOT}/.venv/bin" "${UMCUB_ROOT}/.venv/Scripts" NO_CACHE)
  set(UMCUB_IMGTOOL "${UMCUB_IMGTOOL_FOUND}")
endif()
if(NOT UMCUB_IMGTOOL)
  message(FATAL_ERROR "umcub: imgtool not found. Run tools/setup.sh (creates .venv) "
                      "or pass -DUMCUB_IMGTOOL=<path>.")
endif()

# imgtool arguments for the board type TLV (protected): "--custom-tlv 0xa0 0x<u32 LE>",
# empty when UMCUB_CFG_BOARD_TYPE is 0. Same encoding as tools/umcub_image.py.
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

# umcub_generate_pubkey(<key.pem> <out.c>)
# Embeds the public half of <key.pem> as MCUboot's bootutil_keys[] table.
function(umcub_generate_pubkey key out)
  if(NOT EXISTS "${key}")
    message(FATAL_ERROR "umcub: signing key ${key} not found (imgtool keygen -t ecdsa-p256 -k ${key})")
  endif()
  if(key MATCHES "dev-ecdsa-p256.pem$")
    message(WARNING "umcub: using the development signing key from the repository. "
                    "Anybody can sign images for this bootloader. Set UMCUB_SIGNING_KEY for production.")
  endif()
  set(_raw "${out}.raw")
  add_custom_command(OUTPUT "${out}"
    COMMAND "${UMCUB_IMGTOOL}" getpub -k "${key}" --lang c > "${_raw}"
    COMMAND ${CMAKE_COMMAND} -DRAW=${_raw} -DOUT=${out} -P "${UMCUB_ROOT}/cmake/umcub_keys_wrap.cmake"
    DEPENDS "${key}"
    COMMENT "Embedding public key of ${key}"
    VERBATIM)
endfunction()

# umcub_generate_link_keys(<device.pem> <admin.pem> <out.c>)
# umcub link SECURE keys for the bootloader (tools/umcub_keys.py): the private
# device key and the public half of the admin key.
function(umcub_generate_link_keys device admin out)
  foreach(_k "${device}" "${admin}")
    if(NOT EXISTS "${_k}")
      message(FATAL_ERROR "umcub: key ${_k} not found (imgtool keygen -t ecdsa-p256 -k ${_k})")
    endif()
    if(_k MATCHES "/tools/keys/dev-[a-z]+-p256.pem$")
      message(WARNING "umcub: using the development link key ${_k} from the repository. "
                      "Set UMCUB_DEVICE_KEY / UMCUB_HOST_KEY for production.")
    endif()
  endforeach()
  umcub_keys_command(c "${device}" "${admin}" "${out}")
endfunction()

# umcub_keys_command(<c|host-c> <device.pem> <admin.pem> <out.c>): rule running
# tools/umcub_keys.py with the Python next to imgtool (it has `cryptography`,
# an imgtool dependency).
function(umcub_keys_command mode device admin out)
  get_filename_component(_dir "${UMCUB_IMGTOOL}" DIRECTORY)
  find_program(_py NAMES python3 python HINTS "${_dir}" NO_DEFAULT_PATH NO_CACHE)
  if(NOT _py)
    find_program(_py NAMES python3 python REQUIRED NO_CACHE)
  endif()
  add_custom_command(OUTPUT "${out}"
    COMMAND "${_py}" "${UMCUB_ROOT}/tools/umcub_keys.py" ${mode} --device "${device}" --admin "${admin}" -o "${out}"
    DEPENDS "${device}" "${admin}" "${UMCUB_ROOT}/tools/umcub_keys.py"
    COMMENT "umcub link keys (${mode})"
    VERBATIM)
endfunction()

# umcub_sign_image(<target>
#                  [IMAGE <n>] [SLOT <0|1>] [VERSION <x.y.z[+build]>]
#                  [KEY <key.pem>] [CONFIRM] [PAD] [DEPENDS "(<image>,<version>)"])
# Post-build step producing <target>.signed.bin / .signed.hex next to the ELF.
# SLOT matters only for direct-xip (image linked for that slot). In
# direct-xip-revert an extra <target>.confirmed.{bin,hex} (padded, image_ok
# set) is produced for programmers and serial recovery: an image without a
# confirmed trailer is erased by MCUboot on the next boot in that mode.
# DEPENDS example (multi-image): "(1,1.0.0)" = needs image 1 >= 1.0.0.
function(umcub_sign_image target)
  cmake_parse_arguments(A "CONFIRM;PAD" "IMAGE;SLOT;VERSION;KEY" "DEPENDS" ${ARGN})
  if(NOT DEFINED A_IMAGE)
    set(A_IMAGE 0)
  endif()
  if(NOT DEFINED A_SLOT OR UMCUB_CFG_UPGRADE_MODE LESS 5)
    set(A_SLOT 0)   # swap/overwrite: images are always built for the primary slot
  endif()
  if(NOT A_VERSION)
    set(A_VERSION 0.0.0)
  endif()
  if(NOT A_KEY)
    set(A_KEY "${UMCUB_SIGNING_KEY}")
  endif()

  if(A_IMAGE EQUAL 0 AND A_SLOT EQUAL 0)
    set(_slot_size ${UMCUB_CFG_IMG0_PRIMARY_SIZE})
    set(_slot_addr ${UMCUB_CFG_IMG0_PRIMARY_ADDR})
  elseif(A_IMAGE EQUAL 0)
    set(_slot_size ${UMCUB_CFG_IMG0_SECONDARY_SIZE})
    set(_slot_addr ${UMCUB_CFG_IMG0_SECONDARY_ADDR})
  elseif(A_SLOT EQUAL 0)
    set(_slot_size ${UMCUB_CFG_IMG1_PRIMARY_SIZE})
    set(_slot_addr ${UMCUB_CFG_IMG1_PRIMARY_ADDR})
  else()
    set(_slot_size ${UMCUB_CFG_IMG1_SECONDARY_SIZE})
    set(_slot_addr ${UMCUB_CFG_IMG1_SECONDARY_ADDR})
  endif()
  # Secondary slot may be smaller (swap-move) - the image must fit both.
  if(A_IMAGE EQUAL 0)
    set(_min ${UMCUB_CFG_IMG0_SECONDARY_SIZE})
  else()
    set(_min ${UMCUB_CFG_IMG1_SECONDARY_SIZE})
  endif()
  if(_min LESS _slot_size AND NOT UMCUB_CFG_UPGRADE_MODE GREATER_EQUAL 5)
    set(_slot_size ${_min})
  endif()

  set(_args sign --version ${A_VERSION}
            --header-size ${UMCUB_CFG_IMAGE_HEADER_SIZE} --pad-header
            --slot-size ${_slot_size}
            --align ${UMCUB_CFG_FAMILY_WRITE_ALIGN}
            --max-align ${UMCUB_CFG_FAMILY_WRITE_ALIGN})
  if(UMCUB_CFG_SIGNATURE EQUAL 1)
    list(APPEND _args -k "${A_KEY}")
  endif()
  if(UMCUB_CFG_UPGRADE_MODE EQUAL 1)   # overwrite
    list(APPEND _args --overwrite-only)
  elseif(UMCUB_CFG_UPGRADE_MODE LESS_EQUAL 4)   # swap modes
    # The bootloader sizes the trailer with MCUBOOT_MAX_IMG_SECTORS, i.e. the
    # largest slot of any image (see mcuboot_config.h).
    set(_max 0)
    foreach(_v IMG0_PRIMARY_SIZE IMG0_SECONDARY_SIZE IMG1_PRIMARY_SIZE IMG1_SECONDARY_SIZE)
      if(UMCUB_CFG_${_v} GREATER _max)
        set(_max ${UMCUB_CFG_${_v}})
      endif()
    endforeach()
    math(EXPR _sectors "${_max} / ${UMCUB_CFG_FAMILY_MIN_SECTOR}")
    list(APPEND _args --max-sectors ${_sectors})
  endif()
  if(UMCUB_CFG_UPGRADE_MODE GREATER_EQUAL 5)  # direct-xip: header carries the slot address
    umcub_hex(_rom ${_slot_addr})
    list(APPEND _args --rom-fixed ${_rom})
  endif()
  umcub_board_type_tlv_args(_tlv "${UMCUB_CFG_BOARD_TYPE}")
  list(APPEND _args ${_tlv})
  if(A_CONFIRM)
    list(APPEND _args --confirm)
  endif()
  if(A_PAD)
    list(APPEND _args --pad)
  endif()
  if(A_DEPENDS)
    list(APPEND _args --dependencies "${A_DEPENDS}")
  endif()

  set(_dir $<TARGET_FILE_DIR:${target}>)
  set(_base $<TARGET_FILE_BASE_NAME:${target}>)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${_dir}/${_base}.bin
    COMMAND "${UMCUB_IMGTOOL}" ${_args} ${_dir}/${_base}.bin ${_dir}/${_base}.signed.bin
    # .hex from the very same signed binary: ECDSA signatures are randomized,
    # signing twice would give two different (both valid) images.
    COMMAND ${CMAKE_OBJCOPY} -I binary -O ihex --change-addresses ${_slot_addr}
            ${_dir}/${_base}.signed.bin ${_dir}/${_base}.signed.hex
    COMMENT "Signing ${target}: image ${A_IMAGE} slot ${A_SLOT} version ${A_VERSION}"
    VERBATIM)
  if(UMCUB_CFG_UPGRADE_MODE EQUAL 6 AND NOT A_CONFIRM)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${UMCUB_IMGTOOL}" ${_args} --confirm --pad ${_dir}/${_base}.bin ${_dir}/${_base}.confirmed.bin
      COMMAND ${CMAKE_OBJCOPY} -I binary -O ihex --change-addresses ${_slot_addr}
              ${_dir}/${_base}.confirmed.bin ${_dir}/${_base}.confirmed.hex
      VERBATIM)
  endif()
endfunction()
