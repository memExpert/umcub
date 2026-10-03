# umcub application library (target umcub::app).
#
# In the application's CMakeLists.txt (after project()):
#
#   set(UMCUB_BOARD nucleo_h755zi_q)          # board dir name or path
#   set(UMCUB_CORE  cm7)                      # dual-core parts only
#   add_subdirectory(path/to/umcub/lib/umcub_app umcub)
#
#   target_link_libraries(app PRIVATE umcub::app)
#   umcub_app_linker_script(app IMAGE 0)      # or use your own script with
#                                             # UMCUB_APP_FLASH_ORIGIN/LENGTH
#   umcub_sign_image(app VERSION 1.0.0)       # -> app.signed.bin/.hex
#
# Optional: UMCUB_APP_CPU_FLAGS (defaults to the family's hard-float flags),
#           UMCUB_SIGNING_KEY, UMCUB_IMGTOOL, UMCUB_CONFIG_PRE/POST.

cmake_minimum_required(VERSION 3.20)

get_filename_component(_umcub_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include(${_umcub_root}/cmake/umcub_config.cmake)

if(NOT UMCUB_BOARD)
  message(FATAL_ERROR "umcub::app: set UMCUB_BOARD before add_subdirectory()")
endif()
if(NOT DEFINED UMCUB_SIGNING_KEY)
  set(UMCUB_SIGNING_KEY "${UMCUB_ROOT}/tools/keys/dev-ecdsa-p256.pem")
endif()
umcub_load_config("${UMCUB_BOARD}" "${UMCUB_CORE}")
include(${UMCUB_ROOT}/cmake/umcub_imgtool.cmake)
include(${UMCUB_ROOT}/cmake/umcub_app.cmake)

if(NOT UMCUB_APP_CPU_FLAGS)
  set(UMCUB_APP_CPU_FLAGS ${UMCUB_CPU_FLAGS_HARD})
endif()

set(MCUBOOT ${UMCUB_ROOT}/third_party/mcuboot)

add_library(umcub_app STATIC
  src/umcub.c
  src/slot_writer.c
  ${UMCUB_ROOT}/mcuboot_port/src/flash_map_backend.c
  ${MCUBOOT}/boot/bootutil/src/bootutil_public.c
  ${UMCUB_FAMILY_APP_SOURCES}
)
add_library(umcub::app ALIAS umcub_app)

target_include_directories(umcub_app
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${UMCUB_ROOT}/config
  PRIVATE
    ${UMCUB_CONFIG_INCLUDES}
    ${UMCUB_FAMILY_INCLUDES}
    ${UMCUB_ROOT}/port/include
    ${UMCUB_ROOT}/mcuboot_port/include
    ${MCUBOOT}/boot/bootutil/include
)
target_compile_definitions(umcub_app PRIVATE ${UMCUB_CONFIG_DEFS} UMCUB_BUILDING_APP=1)
target_compile_options(umcub_app PRIVATE
  ${UMCUB_APP_CPU_FLAGS} -ffunction-sections -fdata-sections -Os -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(${MCUBOOT}/boot/bootutil/src/bootutil_public.c PROPERTIES COMPILE_OPTIONS "-w")

# Make the configuration visible to the application's directory (signing,
# linker script helpers, UMCUB_CFG_* values).
get_cmake_property(_umcub_vars VARIABLES)
foreach(_v IN LISTS _umcub_vars)
  if(_v MATCHES "^UMCUB_")
    set(${_v} "${${_v}}" PARENT_SCOPE)
  endif()
endforeach()
