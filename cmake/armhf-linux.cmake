# Cross build for 32-bit ARM Linux (armv7-a, hard float) with clang and a Debian sysroot.
# Sysroot: KENNEL_SYSROOT (cache or environment), default build/sysroot-armhf from scripts/sysroot.sh.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT KENNEL_SYSROOT)
    if(DEFINED ENV{KENNEL_SYSROOT})
        set(KENNEL_SYSROOT $ENV{KENNEL_SYSROOT})
    else()
        set(KENNEL_SYSROOT ${CMAKE_CURRENT_LIST_DIR}/../build/sysroot-armhf)
    endif()
endif()
set(KENNEL_SYSROOT ${KENNEL_SYSROOT} CACHE PATH "armhf sysroot")

set(triple arm-linux-gnueabihf)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_SYSROOT ${KENNEL_SYSROOT})
set(CMAKE_LIBRARY_ARCHITECTURE ${triple})

set(arch_flags "-march=armv7-a -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${arch_flags}")
set(CMAKE_CXX_FLAGS_INIT "${arch_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")

set(CMAKE_FIND_ROOT_PATH ${KENNEL_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
