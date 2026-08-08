set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

# Debian/Ubuntu cross packages install the AArch64 runtime beneath this root.
# QEMU user mode uses the same root for the target dynamic loader/libraries.
set(EMNL_AARCH64_SYSROOT "/usr/aarch64-linux-gnu" CACHE PATH "AArch64 GNU/Linux runtime root")
set(CMAKE_FIND_ROOT_PATH "${EMNL_AARCH64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(EMNL_QEMU_AARCH64 qemu-aarch64)
if(EMNL_QEMU_AARCH64)
    set(CMAKE_CROSSCOMPILING_EMULATOR
        "${EMNL_QEMU_AARCH64};-L;${EMNL_AARCH64_SYSROOT}"
        CACHE STRING "AArch64 userspace emulator" FORCE)
endif()
