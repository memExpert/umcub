# Example application for the Black Pill (STM32F411CEU6) running under umcub.
#   cmake -B build -G Ninja -DAPP_VERSION=1.0.0 && cmake --build build
# Produces build/blackpill_app.signed.bin (upload through the bootloader:
# smpmgr --port /dev/ttyUSB0 image upload ...).

cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../cmake/arm-none-eabi.cmake)
project(blackpill_app C ASM)

set(APP_VERSION "1.0.0" CACHE STRING "Image version written by imgtool")
set(APP_NODE_ADDR "0" CACHE STRING "Node address handed to the bootloader (0 = unassigned, none = keep the bootloader's own)")
set(UMCUB_BOARD blackpill_f411ce CACHE STRING "Board (boards/<name> or a path) whose layout the image is linked for")
add_subdirectory(../../lib/umcub_app umcub)

set(ST ${UMCUB_ROOT}/third_party/st/cmsis_device_f4)
add_executable(blackpill_app
  main.c
  ../common/umcub_example.c
  ${ST}/Source/Templates/gcc/startup_stm32f411xe.s
  ${ST}/Source/Templates/system_stm32f4xx.c)
target_include_directories(blackpill_app PRIVATE ../common ${ST}/Include ${UMCUB_ROOT}/third_party/cmsis_core/Include)
target_compile_definitions(blackpill_app PRIVATE STM32F411xE APP_VERSION_STR="${APP_VERSION}" $<$<NOT:$<STREQUAL:${APP_NODE_ADDR},none>>:APP_NODE_ADDR=${APP_NODE_ADDR}>)
target_compile_options(blackpill_app PRIVATE ${UMCUB_CPU_FLAGS_HARD} -Os -g -Wall -Wextra
  -ffunction-sections -fdata-sections $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)
target_link_options(blackpill_app PRIVATE ${UMCUB_CPU_FLAGS_HARD} --specs=nano.specs --specs=nosys.specs -nostartfiles
  -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments -Wl,-Map=$<TARGET_FILE_DIR:blackpill_app>/blackpill_app.map)
target_link_libraries(blackpill_app PRIVATE ${UMCUB_APP_LIB})   # = umcub::app in a single-application project

umcub_app_linker_script(blackpill_app IMAGE 0)
umcub_sign_image(blackpill_app IMAGE 0 VERSION ${APP_VERSION})
