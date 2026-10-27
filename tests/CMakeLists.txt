# The build matrix as CTest tests: every bootloader configuration
# (matrix/configs.txt), the presets, the example applications, the IDE
# library compile, the checks, the host tests and the link e2e.
#
#   cmake -S tests -B build/matrix -G Ninja && ctest --test-dir build/matrix -j4
#   ctest --test-dir build/matrix -R bluepill        (some), --rerun-failed, --output-junit report.xml
#
# tools/build/build_matrix.sh does the same and prints the size summary.
# Logs: build/matrix/<name>.*.log; build trees: build/matrix/<name>/.
cmake_minimum_required(VERSION 3.22)
project(umcub_matrix NONE)
enable_testing()

get_filename_component(UMCUB_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_program(UMCUB_PY NAMES python3 python HINTS "${UMCUB_ROOT}/.venv/bin" NO_DEFAULT_PATH)
if(NOT UMCUB_PY)
  find_program(UMCUB_PY NAMES python3 python REQUIRED)
endif()
set(RUN "${UMCUB_PY}" "${UMCUB_ROOT}/tests/matrix/run.py" --out "${CMAKE_BINARY_DIR}")
set(_order)       # test names in matrix order, for the summary of build_matrix.sh
set(_builds)      # tests leaving a build tree (config-check reads them)

macro(_umcub_test name)
  add_test(NAME ${name} COMMAND ${ARGN} WORKING_DIRECTORY "${UMCUB_ROOT}")
  list(APPEND _order ${name})
endmacro()

_umcub_test(layers ${RUN} check layers -- "${UMCUB_PY}" tools/build/check_layers.py)

# Bootloader configurations: configure, build, no warnings.
file(STRINGS matrix/configs.txt _lines REGEX "^[^#]")
foreach(_l IN LISTS _lines)
  string(REGEX MATCH "^[^|]+" _name "${_l}")
  _umcub_test(${_name} ${RUN} config "${_l}")
  list(APPEND _builds ${_name})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS matrix/configs.txt)

# CMake presets (bootloader + the board's example applications in one tree):
# every target links, warning-free, a .map next to each .elf.
foreach(_p h755-cm7 h755-per-core-cm4 bluepill-usb blackpill)
  _umcub_test(preset-${_p} ${RUN} preset ${_p})
  list(APPEND _builds preset-${_p})
endforeach()

# Example applications: name, board, core, image. Signed, and the ELF signs
# like its objcopy binary.
foreach(_e "h755_cm7_app|nucleo_h755zi_q|cm7|0" "h755_cm4_app|nucleo_h755zi_q|cm4|1"
           "bluepill_app|bluepill_f103c8||0" "blackpill_app|blackpill_f411ce||0")
  string(REGEX MATCH "^[^|]+" _name "${_e}")
  _umcub_test(${_name} ${RUN} example "${_e}")
  list(APPEND _builds ${_name})
endforeach()

_umcub_test(ide-library ${RUN} ide)
_umcub_test(pin-check ${RUN} check pin-check -- "${UMCUB_PY}" tests/tools/check_pins_test.py)

# CMake and the host tools (umcub/config.py) read the same configuration: after
# every build above.
_umcub_test(config-check ${RUN} check config-check -- "${UMCUB_PY}" tools/build/check_config.py "${CMAKE_BINARY_DIR}")
set_tests_properties(config-check PROPERTIES DEPENDS "${_builds}")

# Host tests (real MCUboot on emulated flash, their own CTest project), then
# simulated devices on one bus against the umcub tool and smpmgr.
_umcub_test(host-tests ${RUN} check host-tests --grep "tests passed" -- "${CMAKE_CTEST_COMMAND}" --build-and-test
  "${UMCUB_ROOT}/tests/host" "${CMAKE_BINARY_DIR}/host" --build-generator Ninja
  --test-command "${CMAKE_CTEST_COMMAND}" --output-on-failure)
set_tests_properties(host-tests PROPERTIES FIXTURES_SETUP host)
_umcub_test(link-e2e ${RUN} check link-e2e -- "${UMCUB_PY}" tests/host/link_e2e.py "${CMAKE_BINARY_DIR}/host"
  --python "${UMCUB_PY}")
set_tests_properties(link-e2e PROPERTIES FIXTURES_REQUIRED host)

list(JOIN _order "\n" _order)
file(WRITE "${CMAKE_BINARY_DIR}/summary_order.txt" "${_order}\n")
