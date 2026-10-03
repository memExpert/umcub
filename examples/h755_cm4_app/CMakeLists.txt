# Example CM4 application for NUCLEO-H755ZI-Q running under umcub.
#   cmake -B build -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build
# Produces build/h755_cm4_app.signed.bin (flash at the image 1 primary slot
# or upload through the bootloader: smpmgr / mcumgr image upload).

cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../cmake/arm-none-eabi.cmake)
project(h755_cm4_app C ASM)

set(APP_VERSION "1.0.0" CACHE STRING "Image version written by imgtool")
set(UMCUB_BOARD nucleo_h755zi_q)
set(UMCUB_CORE cm4)
add_subdirectory(../../lib/umcub_app umcub)

set(ST ${UMCUB_ROOT}/third_party/st/cmsis_device_h7)
add_executable(h755_cm4_app
  main.c
  ../common/h7_example.c
  ${ST}/Source/Templates/gcc/startup_stm32h755xx.s
  ${ST}/Source/Templates/system_stm32h7xx_dualcore_boot_cm4_cm7.c)
target_include_directories(h755_cm4_app PRIVATE
  ../common ${ST}/Include ${UMCUB_ROOT}/third_party/cmsis_core/Include)
target_compile_definitions(h755_cm4_app PRIVATE
  STM32H755xx CORE_CM4 USE_PWR_DIRECT_SMPS_SUPPLY APP_VERSION_STR="${APP_VERSION}")
target_compile_options(h755_cm4_app PRIVATE ${UMCUB_CPU_FLAGS_HARD} -Os -g -Wall -Wextra
  $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)
target_link_options(h755_cm4_app PRIVATE ${UMCUB_CPU_FLAGS_HARD} --specs=nano.specs --specs=nosys.specs -nostartfiles
  -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments -Wl,-Map=h755_cm4_app.map)
target_link_libraries(h755_cm4_app PRIVATE umcub::app)

# SINGLE_BOOT: image 1 of the CM7 bootloader; PER_CORE: image 0 of the CM4 one.
if(UMCUB_CFG_DUALCORE_MODE EQUAL 2)
  set(APP_IMAGE 0)
else()
  set(APP_IMAGE 1)
endif()
umcub_app_linker_script(h755_cm4_app IMAGE ${APP_IMAGE})
umcub_sign_image(h755_cm4_app IMAGE ${APP_IMAGE} VERSION ${APP_VERSION})
