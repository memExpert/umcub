# Example application for the Blue Pill (STM32F103C8T6) running under umcub.
#   cmake -B build -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build
# Produces build/bluepill_app.signed.bin (upload through the bootloader:
# smpmgr --port /dev/ttyUSB0 image upload ...).

cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../cmake/arm-none-eabi.cmake)
project(bluepill_app C ASM)

set(APP_VERSION "1.0.0" CACHE STRING "Image version written by imgtool")
set(UMCUB_BOARD bluepill_f103c8)
add_subdirectory(../../lib/umcub_app umcub)

set(ST ${UMCUB_ROOT}/third_party/st/cmsis_device_f1)
add_executable(bluepill_app
  main.c
  ${ST}/Source/Templates/gcc/startup_stm32f103xb.s
  ${ST}/Source/Templates/system_stm32f1xx.c)
target_include_directories(bluepill_app PRIVATE ${ST}/Include ${UMCUB_ROOT}/third_party/cmsis_core/Include)
target_compile_definitions(bluepill_app PRIVATE STM32F103xB APP_VERSION_STR="${APP_VERSION}")
target_compile_options(bluepill_app PRIVATE ${UMCUB_CPU_FLAGS_HARD} -Os -g -Wall -Wextra
  -ffunction-sections -fdata-sections $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)
target_link_options(bluepill_app PRIVATE ${UMCUB_CPU_FLAGS_HARD} --specs=nano.specs --specs=nosys.specs -nostartfiles
  -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments -Wl,-Map=$<TARGET_FILE_DIR:bluepill_app>/bluepill_app.map)
target_link_libraries(bluepill_app PRIVATE ${UMCUB_APP_LIB})   # = umcub::app in a single-application project

umcub_app_linker_script(bluepill_app IMAGE 0)
umcub_sign_image(bluepill_app IMAGE 0 VERSION ${APP_VERSION})
