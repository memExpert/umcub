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

# umcub_generate_enc_key(<device.pem> <out.c>)
# The device private key as MCUboot's bootutil_enc_key (encrypted images):
# `imgtool getpriv`, like the public signing key above.
function(umcub_generate_enc_key device out)
  if(NOT EXISTS "${device}")
    message(FATAL_ERROR "umcub: device key ${device} not found (imgtool keygen -t ecdsa-p256 -k ${device})")
  endif()
  if("${device}" MATCHES "/tools/keys/dev-[a-z]+-p256.pem$")
    message(WARNING "umcub: using the development device key ${device} from the repository "
                    "(image decryption / link). Set UMCUB_DEVICE_KEY for production.")
  endif()
  set(_raw "${out}.raw")
  add_custom_command(OUTPUT "${out}"
    COMMAND "${UMCUB_IMGTOOL}" getpriv -k "${device}" > "${_raw}"
    COMMAND ${CMAKE_COMMAND} -DMODE=enc -DRAW=${_raw} -DOUT=${out} -P "${UMCUB_ROOT}/cmake/umcub_keys_wrap.cmake"
    DEPENDS "${device}" "${UMCUB_ROOT}/cmake/umcub_keys_wrap.cmake"
    COMMENT "umcub device key (encrypted images)"
    VERBATIM)
endfunction()

# umcub_generate_link_keys(<device.pem> <admin.pem> <out.c>)
# umcub link SECURE keys for the bootloader (umcub keys c, tools/host/umcub): the private
# device key and the public half of the admin key.
function(umcub_generate_link_keys device admin out)
  foreach(_k "${device}" "${admin}")
    if(NOT EXISTS "${_k}")
      message(FATAL_ERROR "umcub: key ${_k} not found (imgtool keygen -t ecdsa-p256 -k ${_k})")
    endif()
  endforeach()
  # (the device key is reported by umcub_keys_command)
  if("${admin}" MATCHES "/tools/keys/dev-[a-z]+-p256.pem$")
    message(WARNING "umcub: using the development admin key ${admin} from the repository: "
                    "anybody can open a SECURE session. Set UMCUB_HOST_KEY for production.")
  endif()
  umcub_keys_command(c "${device}" "${admin}" "${out}")
endfunction()

# umcub_python(<out_var>): the Python next to imgtool (it has imgtool's
# dependencies: cryptography, intelhex, click), else the first one in PATH.
function(umcub_python out_var)
  get_filename_component(_dir "${UMCUB_IMGTOOL}" DIRECTORY)
  find_program(_py NAMES python3 python HINTS "${_dir}" NO_DEFAULT_PATH NO_CACHE)
  if(NOT _py)
    find_program(_py NAMES python3 python REQUIRED NO_CACHE)
  endif()
  set(${out_var} "${_py}" PARENT_SCOPE)
endfunction()

# umcub_keys_command(<c|host-c> <device.pem> <admin.pem or ""> <out.c>):
# rule running `umcub keys` (tools/host/umcub, run as a directory: no install
# needed) with the Python next to imgtool (it has `cryptography`, an imgtool dependency).
function(umcub_keys_command mode device admin out)
  if("${device}" MATCHES "/tools/keys/dev-[a-z]+-p256.pem$" AND NOT mode STREQUAL "host-c")
    message(WARNING "umcub: using the development device key ${device} from the repository "
                    "(image decryption / link). Set UMCUB_DEVICE_KEY for production.")
  endif()
  set(_admin_args)
  if(admin)
    set(_admin_args --admin "${admin}")
  endif()
  umcub_python(_py)
  add_custom_command(OUTPUT "${out}"
    COMMAND "${_py}" "${UMCUB_ROOT}/tools/host/umcub" keys ${mode} --device "${device}" ${_admin_args} -o "${out}"
    DEPENDS "${device}" ${admin} "${UMCUB_ROOT}/tools/host/umcub/keys.py"
    COMMENT "umcub link keys (${mode})"
    VERBATIM)
endfunction()

# umcub_sign_image(<target>
#                  [IMAGE <n>] [SLOT <0|1>] [VERSION <x.y.z[+build]>]
#                  [KEY <key.pem>] [ENCRYPT_KEY <device.pem>] [CONFIRM] [PAD]
#                  [DEPENDS "(<image>,<version>)"])
# Post-build step producing <target>.signed.bin / .signed.hex next to the ELF.
# With UMCUB_CFG_ENCRYPT_IMAGES also <target>.encrypted.bin (ENCRYPT_KEY, default
# UMCUB_DEVICE_KEY or the development device key) for SMP / DFU / application
# updates.
# SLOT matters only for direct-xip (image linked for that slot). In
# direct-xip-revert an extra <target>.confirmed.{bin,hex} (padded, image_ok
# set) is produced for programmers and serial recovery: an image without a
# confirmed trailer is erased by MCUboot on the next boot in that mode.
# DEPENDS example (multi-image): "(1,1.0.0)" = needs image 1 >= 1.0.0.
# Signing itself is `umcub image sign` (tools/host/umcub, the tool IDE projects use),
# on the same board configuration and overlays: one implementation.
function(umcub_sign_image target)
  cmake_parse_arguments(A "CONFIRM;PAD" "IMAGE;SLOT;VERSION;KEY;ENCRYPT_KEY" "DEPENDS" ${ARGN})
  if(NOT DEFINED A_IMAGE)
    set(A_IMAGE 0)
  endif()
  if(NOT DEFINED A_SLOT)
    set(A_SLOT 0)
  endif()
  if(NOT A_VERSION)
    set(A_VERSION 0.0.0)
  endif()
  if(NOT A_KEY)
    set(A_KEY "${UMCUB_SIGNING_KEY}")
  endif()
  if(UMCUB_CFG_SIGNATURE EQUAL 1 AND A_KEY MATCHES "/tools/keys/dev-ecdsa-p256.pem$")
    message(WARNING "umcub: ${target} is signed with the development key from the repository - anybody can "
                    "sign images for its bootloader. Pass KEY or set UMCUB_SIGNING_KEY for production.")
  endif()
  if(NOT A_ENCRYPT_KEY)
    if(UMCUB_DEVICE_KEY)
      set(A_ENCRYPT_KEY "${UMCUB_DEVICE_KEY}")
    else()
      set(A_ENCRYPT_KEY "${UMCUB_ROOT}/tools/keys/dev-device-p256.pem")
    endif()
  endif()
  if(UMCUB_CFG_ENCRYPT_IMAGES AND A_ENCRYPT_KEY MATCHES "/tools/keys/dev-device-p256.pem$")
    message(WARNING "umcub: ${target} is encrypted for the development device key - anybody can decrypt it. "
                    "Pass ENCRYPT_KEY or set UMCUB_DEVICE_KEY for production.")
  endif()

  set(_args sign --board "${UMCUB_BOARD_DIR}" --cc "${CMAKE_C_COMPILER}" --imgtool "${UMCUB_IMGTOOL}" --quiet
            --image ${A_IMAGE} --slot ${A_SLOT} --version ${A_VERSION}
            --key "${A_KEY}" --encrypt-key "${A_ENCRYPT_KEY}")
  if(UMCUB_CORE MATCHES "^cm[47]$")     # dual-core parts (UMCUB_CORE_CM7 / _CM4)
    list(APPEND _args --core ${UMCUB_CORE})
  endif()
  foreach(_o PRE POST)                  # as resolved by umcub_load_config()
    if(UMCUB_CONFIG_${_o}_ABS)
      string(TOLOWER ${_o} _l)
      list(APPEND _args --${_l} "${UMCUB_CONFIG_${_o}_ABS}")
    endif()
  endforeach()
  if(A_CONFIRM)
    list(APPEND _args --confirm)
  endif()
  if(A_PAD)
    list(APPEND _args --pad)
  endif()
  if(A_DEPENDS)
    list(APPEND _args --depends "${A_DEPENDS}")
  endif()

  umcub_python(_py)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${_py}" "${UMCUB_ROOT}/tools/host/umcub" image ${_args}
            -o $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}> $<TARGET_FILE:${target}>
    COMMENT "Signing ${target}: image ${A_IMAGE} version ${A_VERSION}"
    VERBATIM)
  # What a programmer writes (the flash target): the signed image, in
  # direct-xip-revert the confirmed one (an unconfirmed image is erased there).
  set(_hex "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.signed.hex")
  if(UMCUB_CFG_UPGRADE_MODE EQUAL 6)    # UMCUB_MODE_DIRECT_XIP_REVERT
    set(_hex "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.confirmed.hex")
  endif()
  set_property(GLOBAL APPEND PROPERTY UMCUB_FLASH_IMAGES "${_hex}")
  set_property(GLOBAL APPEND PROPERTY UMCUB_FLASH_TARGETS ${target})
endfunction()
