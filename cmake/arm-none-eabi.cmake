# Toolchain file for GNU Arm Embedded (arm-none-eabi-gcc).
# CPU flags are added per target by umcub (they depend on the MCU/core).

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED UMCUB_TOOLCHAIN_PREFIX)
  set(UMCUB_TOOLCHAIN_PREFIX arm-none-eabi-)
endif()

set(CMAKE_C_COMPILER   ${UMCUB_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${UMCUB_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${UMCUB_TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY      ${UMCUB_TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "")
set(CMAKE_SIZE         ${UMCUB_TOOLCHAIN_PREFIX}size CACHE FILEPATH "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXECUTABLE_SUFFIX_C   ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

set(CMAKE_C_FLAGS_INIT   "-ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "-x assembler-with-cpp")
set(CMAKE_CXX_FLAGS_INIT "-ffunction-sections -fdata-sections -fno-rtti -fno-exceptions")
set(CMAKE_C_FLAGS_DEBUG          "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE        "-Os -g")
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -g3")
