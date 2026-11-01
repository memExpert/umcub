# umcub configuration loader shared by the bootloader build and umcub::app.
#
# umcub_load_config(<board_dir> <core>)
#   The configuration of a board (its defconfig + the fragments of UMCUB_CONF, or
#   without a board the Kconfig defaults + fragments): `umcub config generate`
#   writes it to the build tree (.config, umcub_config.h, the slots' linker
#   fragments, ...), every UMCUB_CFG_* becomes a CMake variable in the caller's
#   scope. Also loads the family description (cmake/families/<family>.cmake) and sets:
#     UMCUB_MCU, UMCUB_CORE, UMCUB_FAMILY*, UMCUB_CPU_FLAGS_SOFT/HARD,
#     UMCUB_CONFIG_DIR, UMCUB_CONFIG_INCLUDES, UMCUB_CONFIG_DEFS
#
# umcub_preprocess(<template> <output>)
#   Preprocesses a linker script template with the same configuration.

get_filename_component(UMCUB_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Header directories of the layers (one flat namespace, see the
# hw-abstraction-architecture skill): L0 include, L1 port, L2 util,
# L3 image (+ MCUboot glue), L4 transport, L5 proto.
set(UMCUB_LAYER_INCLUDES
  ${UMCUB_ROOT}/include
  ${UMCUB_ROOT}/port/include
  ${UMCUB_ROOT}/src/util/include
  ${UMCUB_ROOT}/src/image/include
  ${UMCUB_ROOT}/src/image/mcuboot/include
  ${UMCUB_ROOT}/src/transport/include
  ${UMCUB_ROOT}/src/proto/include
)

function(_umcub_cpp out_var probe_file)
  set(_defs)
  foreach(d IN LISTS UMCUB_CONFIG_DEFS)
    list(APPEND _defs "-D${d}")
  endforeach()
  set(_incs)
  foreach(i IN LISTS UMCUB_CONFIG_INCLUDES)
    list(APPEND _incs "-I${i}")
  endforeach()
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -E -P -x c ${_defs} ${_incs} ${probe_file}
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "umcub: configuration error\n${_err}")
  endif()
  if(_err)
    message(WARNING "${_err}")
  endif()
  set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

macro(umcub_load_config board_dir core)
  if("${board_dir}" STREQUAL "")
    # No board: Kconfig from its defaults and the UMCUB_CONF fragments (menuconfig);
    # no board code. The binaries are named after the part.
    set(UMCUB_BOARD_DIR "${CMAKE_BINARY_DIR}/umcub_kconfig/board")
    file(MAKE_DIRECTORY "${UMCUB_BOARD_DIR}")
  else()
    get_filename_component(UMCUB_BOARD_DIR "${board_dir}" ABSOLUTE BASE_DIR "${UMCUB_ROOT}/boards")
    if(NOT EXISTS "${UMCUB_BOARD_DIR}/defconfig")
      message(FATAL_ERROR "umcub: ${UMCUB_BOARD_DIR}: no defconfig (docs/board.md)")
    endif()
  endif()
  set(UMCUB_CORE "${core}")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/umcub_probe")
  _umcub_kconfig("${board_dir}")
  set(UMCUB_MCU "${UMCUB_CFG_MCU}")

  set(UMCUB_CONFIG_DEFS)
  if(UMCUB_CORE STREQUAL "cm7")
    list(APPEND UMCUB_CONFIG_DEFS UMCUB_CORE_CM7=1)
  elseif(UMCUB_CORE STREQUAL "cm4")
    list(APPEND UMCUB_CONFIG_DEFS UMCUB_CORE_CM4=1)
  endif()

  # The family description
  string(SUBSTRING "${UMCUB_MCU}" 5 2 _fam)
  string(TOLOWER "stm32${_fam}" _fam)
  if(NOT EXISTS "${UMCUB_ROOT}/cmake/families/${_fam}.cmake")
    message(FATAL_ERROR "umcub: no port for family ${_fam} (${UMCUB_MCU}) yet - see docs/porting.md, \"Adding a series\"")
  endif()
  include("${UMCUB_ROOT}/cmake/families/${_fam}.cmake")
  list(APPEND UMCUB_CONFIG_DEFS ${UMCUB_FAMILY_DEFS})
  set(UMCUB_CONFIG_INCLUDES "${UMCUB_ROOT}/config" "${UMCUB_CONFIG_DIR}" "${UMCUB_FAMILY_DIR}/include")
endmacro()

# .config, umcub_config.h and the rest from boards/<b>/defconfig and the
# fragments of UMCUB_CONF (umcub.kconfig, `umcub config generate`), made again
# when a Kconfig file, the defconfig, a fragment or the .config (menuconfig)
# changes. The keys come from the configuration too (generated ones are made in
# keys/<board>), and the slots' linker fragments (umcub_app_img<n>_slot<m>.ld)
# are written next to umcub_config.h. Targets: menuconfig, savedefconfig.
macro(_umcub_kconfig board)
  # One directory per core: a tree can hold an application of the other core.
  get_property(_first_core GLOBAL PROPERTY UMCUB_KCONFIG_CORE SET)
  if(NOT _first_core)
    set_property(GLOBAL PROPERTY UMCUB_KCONFIG_CORE "${UMCUB_CORE}")
  endif()
  get_property(_first_core GLOBAL PROPERTY UMCUB_KCONFIG_CORE)
  if(UMCUB_KCONFIG_TAG)     # several configurations in one tree (tests/host)
    set(UMCUB_CONFIG_DIR "${CMAKE_BINARY_DIR}/umcub_kconfig_${UMCUB_KCONFIG_TAG}")
  elseif("${_first_core}" STREQUAL "${UMCUB_CORE}")
    set(UMCUB_CONFIG_DIR "${CMAKE_BINARY_DIR}/umcub_kconfig")
  else()
    set(UMCUB_CONFIG_DIR "${CMAKE_BINARY_DIR}/umcub_kconfig_${UMCUB_CORE}")
  endif()
  find_program(_umcub_py NAMES python3 python HINTS "${UMCUB_ROOT}/.venv/bin" NO_CACHE)
  if(NOT _umcub_py)
    message(FATAL_ERROR "umcub: Python with esp-idf-kconfig needed (tools/setup.sh)")
  endif()
  set(_conf_args)
  foreach(f IN LISTS UMCUB_CONF)
    get_filename_component(_f "${f}" ABSOLUTE)
    list(APPEND _conf_args --conf "${_f}")
  endforeach()
  set(_umcub_config_cmd "${_umcub_py}" "${UMCUB_ROOT}/tools/host/umcub" config)
  if("${board}" STREQUAL "")
    set(_board "")
  else()
    set(_board "${UMCUB_BOARD_DIR}")
  endif()
  execute_process(COMMAND ${_umcub_config_cmd} generate --board "${_board}" --core "${UMCUB_CORE}"
                          ${_conf_args} --out "${UMCUB_CONFIG_DIR}"
                  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "umcub: ${_err}${_out}")
  endif()
  string(STRIP "${_out}" _out)
  if(_out)
    message(STATUS "${_out}")
  endif()
  include("${UMCUB_CONFIG_DIR}/umcub_config.cmake")
  include("${UMCUB_CONFIG_DIR}/keys.cmake")     # UMCUB_SIGNING_KEY etc.: Kconfig "Keys", over the cache
  file(STRINGS "${UMCUB_CONFIG_DIR}/kconfig.deps" _deps)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_deps} "${UMCUB_CONFIG_DIR}/.config")
  if(NOT TARGET menuconfig)
    add_custom_target(menuconfig COMMAND ${_umcub_config_cmd} menu --out "${UMCUB_CONFIG_DIR}" USES_TERMINAL
                      COMMENT "umcub: menuconfig (build again afterwards)")
    add_custom_target(savedefconfig COMMAND ${_umcub_config_cmd} save --out "${UMCUB_CONFIG_DIR}"
                                            -o "${CMAKE_BINARY_DIR}/defconfig" USES_TERMINAL)
  endif()
endmacro()

function(umcub_preprocess template output)
  _umcub_cpp(_out "${template}")
  file(WRITE "${output}.tmp" "${_out}")
  configure_file("${output}.tmp" "${output}" COPYONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${template}" "${UMCUB_CONFIG_DIR}/umcub_config.h")
endfunction()

# Hex string helper: umcub_hex(<out_var> <decimal>)
function(umcub_hex out_var value)
  math(EXPR _h "${value}" OUTPUT_FORMAT HEXADECIMAL)
  set(${out_var} "${_h}" PARENT_SCOPE)
endfunction()

# The configured pins against ST's pin data (tools/build/check_pins.py): a pin
# without the signal / alternate function the option needs stops the
# configuration, as does a pin the package (UMCUB_CFG_MCU_PART) does not bring
# out. A pin written without its AF (UMCUB_PIN('A', 9)) gets it from the pin
# data: umcub_pins.h in the build tree redefines those options, included by
# umcub_cfg.h through UMCUB_CONFIG_PINS (added to UMCUB_CONFIG_DEFS here).
# Bootloader builds only; the probe prints every pin as "PIN '<port>', <num>[, <af>]".
# umcub_check_pins([<suffix>]): umcub_pins<suffix>.h, for several configurations
# in one build tree (host tests).
macro(umcub_check_pins)
  set(_umcub_pins_h "${CMAKE_BINARY_DIR}/umcub_probe/umcub_pins${ARGN}.h")
  _umcub_check_pins("${_umcub_pins_h}")
  list(APPEND UMCUB_CONFIG_DEFS "UMCUB_CONFIG_PINS=\"${_umcub_pins_h}\"")
endmacro()

function(_umcub_check_pins header)
  set(_names UMCUB_CFG_TRANSPORT_UART UMCUB_CFG_UART_INSTANCE UMCUB_CFG_UART_TX_PIN UMCUB_CFG_UART_RX_PIN
             UMCUB_CFG_UART_DE_PIN UMCUB_CFG_TRANSPORT_CAN UMCUB_CFG_CAN_DRIVER UMCUB_CFG_CAN_INSTANCE
             UMCUB_CFG_CAN_TX_PIN UMCUB_CFG_CAN_RX_PIN UMCUB_CFG_TRANSPORT_ETH UMCUB_CFG_ETH_DRIVER
             UMCUB_CFG_ETH_RMII_PINS UMCUB_CFG_ENTRY_GPIO UMCUB_CFG_ENTRY_GPIO_PIN UMCUB_CFG_MCU_PART
             UMCUB_CFG_STATUS_LED UMCUB_CFG_STATUS_LED_PIN UMCUB_CFG_TRANSPORT_USB_CDC UMCUB_CFG_TRANSPORT_USB_DFU)
  set(_probe "#include \"umcub_cfg.h\"\n#undef UMCUB_PIN\n#define UMCUB_PIN(...) PIN __VA_ARGS__\n")
  string(APPEND _probe "@@MCU \"${UMCUB_MCU}\"\n")
  foreach(n IN LISTS _names)
    string(APPEND _probe "@@ \"${n}\" ${n}\n")
  endforeach()
  set(_dir "${CMAKE_BINARY_DIR}/umcub_probe")
  file(WRITE "${_dir}/pins.c" "${_probe}")
  _umcub_cpp(_out "${_dir}/pins.c")
  file(WRITE "${_dir}/pins.txt" "${_out}")
  find_program(_py NAMES python3 python HINTS "${UMCUB_ROOT}/.venv/bin" NO_CACHE)
  if(NOT _py)
    file(WRITE "${header}" "/* no Python: pins not checked, AF not filled in */\n")
    message(STATUS "umcub: pin check skipped (no Python)")
    return()
  endif()
  execute_process(COMMAND "${_py}" "${UMCUB_ROOT}/tools/build/check_pins.py" "${_dir}/pins.txt"
                          --header "${header}"
                  OUTPUT_VARIABLE _msg ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  string(STRIP "${_msg}${_err}" _msg)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${_msg}")
  elseif(_msg)
    message(STATUS "${_msg}")
  endif()
endfunction()
