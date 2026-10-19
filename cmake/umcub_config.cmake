# umcub configuration loader shared by the bootloader build and umcub::app.
#
# umcub_load_config(<board_dir> <core>)
#   Runs the C preprocessor over the board's umcub_config.h (plus family and
#   generic defaults) and exports every UMCUB_CFG_* macro as a CMake variable
#   in the caller's scope. Numeric expressions are evaluated; the raw text is
#   kept in UMCUB_CFG_<NAME>_RAW. Also loads the family description
#   (cmake/families/<family>.cmake) and sets:
#     UMCUB_MCU, UMCUB_CORE, UMCUB_FAMILY*, UMCUB_CPU_FLAGS_SOFT/HARD,
#     UMCUB_CONFIG_INCLUDES, UMCUB_CONFIG_DEFS
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
  get_filename_component(UMCUB_BOARD_DIR "${board_dir}" ABSOLUTE BASE_DIR "${UMCUB_ROOT}/boards")
  if(NOT EXISTS "${UMCUB_BOARD_DIR}/umcub_config.h")
    message(FATAL_ERROR "umcub: ${UMCUB_BOARD_DIR}/umcub_config.h not found")
  endif()
  set(UMCUB_CORE "${core}")
  set(_probe_dir "${CMAKE_BINARY_DIR}/umcub_probe")
  file(MAKE_DIRECTORY "${_probe_dir}")

  set(UMCUB_CONFIG_DEFS)
  if(UMCUB_CORE STREQUAL "cm7")
    list(APPEND UMCUB_CONFIG_DEFS UMCUB_CORE_CM7=1)
  elseif(UMCUB_CORE STREQUAL "cm4")
    list(APPEND UMCUB_CONFIG_DEFS UMCUB_CORE_CM4=1)
  endif()

  if(UMCUB_CONFIG_PRE)
    get_filename_component(_f "${UMCUB_CONFIG_PRE}" ABSOLUTE)
    list(APPEND UMCUB_CONFIG_DEFS "UMCUB_CONFIG_PRE=\"${_f}\"")
  endif()
  if(UMCUB_CONFIG_POST)
    get_filename_component(_f "${UMCUB_CONFIG_POST}" ABSOLUTE)
    list(APPEND UMCUB_CONFIG_DEFS "UMCUB_CONFIG_POST=\"${_f}\"")
  endif()

  # Step 1: which MCU? (board config + constants only)
  set(UMCUB_CONFIG_INCLUDES "${UMCUB_ROOT}/config" "${UMCUB_BOARD_DIR}")
  file(WRITE "${_probe_dir}/mcu.c"
    "#include \"umcub_config_types.h\"\n#ifdef UMCUB_CONFIG_PRE\n#include UMCUB_CONFIG_PRE\n#endif\n#include \"umcub_config.h\"\n#ifdef UMCUB_CONFIG_POST\n#include UMCUB_CONFIG_POST\n#endif\n@@MCU@@ UMCUB_CFG_MCU\n")
  _umcub_cpp(_out "${_probe_dir}/mcu.c")
  if(NOT _out MATCHES "@@MCU@@ (STM32[A-Za-z0-9_]+)")
    message(FATAL_ERROR "umcub: UMCUB_CFG_MCU must be set to a CMSIS device define (e.g. STM32H755xx)")
  endif()
  set(UMCUB_MCU "${CMAKE_MATCH_1}")

  # Step 2: family description
  string(SUBSTRING "${UMCUB_MCU}" 5 2 _fam)
  string(TOLOWER "stm32${_fam}" _fam)
  if(NOT EXISTS "${UMCUB_ROOT}/cmake/families/${_fam}.cmake")
    message(FATAL_ERROR "umcub: no port for family ${_fam} (${UMCUB_MCU}) yet - see docs/porting.md, \"Adding a series\"")
  endif()
  include("${UMCUB_ROOT}/cmake/families/${_fam}.cmake")
  list(APPEND UMCUB_CONFIG_DEFS ${UMCUB_FAMILY_DEFS})
  set(UMCUB_CONFIG_INCLUDES "${UMCUB_ROOT}/config" "${UMCUB_BOARD_DIR}" "${UMCUB_FAMILY_DIR}/include")

  # Step 3: every UMCUB_CFG_* known anywhere
  set(_names)
  foreach(f "${UMCUB_ROOT}/config/umcub_config_template.h"
            "${UMCUB_ROOT}/config/umcub_config_defaults.h"
            "${UMCUB_FAMILY_DIR}/include/umcub_family_defaults.h"
            "${UMCUB_BOARD_DIR}/umcub_config.h")
    # Whole-file regex, not file(STRINGS): a line ending in '\\' (macro
    # continuation) would escape the list separator and glue lines together.
    file(READ "${f}" _text)
    string(REGEX MATCHALL "#[ \t]*define[ \t]+UMCUB_CFG_[A-Z0-9_]+" _defs "${_text}")
    foreach(d IN LISTS _defs)
      string(REGEX MATCH "UMCUB_CFG_[A-Z0-9_]+" _n "${d}")
      list(APPEND _names ${_n})
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _names)

  set(_probe "#include \"umcub_cfg.h\"\n")
  foreach(n IN LISTS _names)
    string(APPEND _probe "@@ \"${n}\" ${n}\n")
  endforeach()
  # Family facts needed by CMake (imgtool arguments).
  string(APPEND _probe "@@ \"UMCUB_CFG_FAMILY_WRITE_ALIGN\" UMCUB_FAMILY_WRITE_ALIGN\n")
  string(APPEND _probe "@@ \"UMCUB_CFG_FAMILY_MIN_SECTOR\" UMCUB_FAMILY_MIN_SECTOR\n")
  file(WRITE "${_probe_dir}/cfg.c" "${_probe}")
  _umcub_cpp(_out "${_probe_dir}/cfg.c")
  string(REPLACE ";" "\\;" _out "${_out}")
  string(REPLACE "\n" ";" _out_lines "${_out}")
  foreach(l IN LISTS _out_lines)
    if(l MATCHES "^@@ \"(UMCUB_CFG_[A-Z0-9_]+)\" (.*)$")
      set(_n "${CMAKE_MATCH_1}")
      string(STRIP "${CMAKE_MATCH_2}" _v)
      if(_v STREQUAL _n)
        continue()   # not defined
      endif()
      set(${_n}_RAW "${_v}")
      # Evaluate integer expressions (strip C integer suffixes first).
      string(REGEX REPLACE "([0-9a-fA-F])[uUlL]+" "\\1" _e "${_v}")
      if(_e MATCHES "^[-+*/%()<>&|^~ \t0-9a-fA-Fx]+$" AND NOT _e MATCHES "\\|\\||&&")
        math(EXPR _num "${_e}" OUTPUT_FORMAT DECIMAL)
        set(${_n} "${_num}")
      else()
        set(${_n} "${_v}")
      endif()
    endif()
  endforeach()
  set(UMCUB_CONFIG_DEFS ${UMCUB_CONFIG_DEFS})
endmacro()

function(umcub_preprocess template output)
  _umcub_cpp(_out "${template}")
  file(WRITE "${output}.tmp" "${_out}")
  configure_file("${output}.tmp" "${output}" COPYONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${template}" "${UMCUB_BOARD_DIR}/umcub_config.h")
endfunction()

# Hex string helper: umcub_hex(<out_var> <decimal>)
function(umcub_hex out_var value)
  math(EXPR _h "${value}" OUTPUT_FORMAT HEXADECIMAL)
  set(${out_var} "${_h}" PARENT_SCOPE)
endfunction()

# The configured pins against ST's pin data (tools/build/check_pins.py): a pin
# without the signal / alternate function the option needs stops the
# configuration. Bootloader builds only; the probe prints every pin as
# "PIN '<port>' <num> <af>".
function(umcub_check_pins)
  set(_names UMCUB_CFG_TRANSPORT_UART UMCUB_CFG_UART_INSTANCE UMCUB_CFG_UART_TX_PIN UMCUB_CFG_UART_RX_PIN
             UMCUB_CFG_UART_DE_PIN UMCUB_CFG_TRANSPORT_CAN UMCUB_CFG_CAN_DRIVER UMCUB_CFG_CAN_INSTANCE
             UMCUB_CFG_CAN_TX_PIN UMCUB_CFG_CAN_RX_PIN UMCUB_CFG_TRANSPORT_ETH UMCUB_CFG_ETH_DRIVER
             UMCUB_CFG_ETH_RMII_PINS)
  set(_probe "#include \"umcub_cfg.h\"\n#undef UMCUB_PIN\n#define UMCUB_PIN(port, pin, af) PIN port pin af\n")
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
    message(STATUS "umcub: pin check skipped (no Python)")
    return()
  endif()
  execute_process(COMMAND "${_py}" "${UMCUB_ROOT}/tools/build/check_pins.py" "${_dir}/pins.txt"
                  OUTPUT_VARIABLE _msg ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  string(STRIP "${_msg}${_err}" _msg)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${_msg}")
  elseif(_msg)
    message(STATUS "${_msg}")
  endif()
endfunction()
