# Cookie's first compiled program.
#
# Built only when the freestanding AArch64 image is, because it targets EL0 on
# that machine and nothing else can run it. Guarded the same way
# cookie_kernel_aarch64_boot is, and for the same reason: a target that cannot
# link on a host build must not be part of a host build.
if(NOT EMNL_BUILD_AARCH64_BOOT_IMAGE)
    return()
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|AARCH64|ARM64")
    return()
endif()

# The base address is passed to the linker rather than written in the script, so
# that the day the boot proof maps a process's code somewhere else there is one
# place to change. docs/M7_16_FIRST_PROGRAM_CONTRACT.md: the link address and the
# plan's region address come from one definition.
set(COOKIE_FIRST_PROGRAM_BASE "0x10000000" CACHE STRING
    "Virtual address the boot proof maps a process's code region at")

# osabi's sources are compiled into this target rather than linked from the
# `osabi` library, which is the pattern cookie_kernel_aarch64_boot already uses.
# The library build carries the host hardening flags - stack protector among
# them - and a stack protector needs __stack_chk_fail, which does not exist at
# EL0 on a system with no libc. Compiling the sources here gives them this
# target's freestanding flags instead.
add_executable(cookie_first_program
    first_program.cpp
    ${PROJECT_SOURCE_DIR}/core/osabi/src/syscall.cpp
    ${PROJECT_SOURCE_DIR}/core/osabi/src/syscall_aarch64.cpp
)

target_include_directories(cookie_first_program PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/core/osabi/include
    ${PROJECT_SOURCE_DIR}/core/oskernel/include
    ${PROJECT_SOURCE_DIR}/core/oscore/include)

target_compile_features(cookie_first_program PRIVATE cxx_std_20)

# -mgeneral-regs-only for the reason the kernel image documents: Cookie does not
# save or restore FP/SIMD across a switch, and M7.13 traps CPACR at EL0, so a
# vector register used for a structure copy would take an undefined-instruction
# trap the moment this program touched one.
#
# -mstrict-align is *not* here, and that difference is deliberate. The kernel
# image needs it because it runs with translation off, where every access takes
# the Device-nGnRnE attribute and unaligned access is not permitted. This program
# only ever runs with translation on, mapped Normal, so the constraint does not
# apply to it and asserting otherwise would misdescribe why the kernel has it.
target_compile_options(cookie_first_program PRIVATE
    -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
    -fno-threadsafe-statics -fno-stack-protector -fno-asynchronous-unwind-tables
    -fno-unwind-tables -fvisibility=hidden -mgeneral-regs-only
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
if(EMNL_WARNINGS_AS_ERRORS)
    target_compile_options(cookie_first_program PRIVATE -Werror)
endif()

target_link_options(cookie_first_program PRIVATE
    -nostdlib
    -static
    -no-pie
    -Wl,--build-id=none
    -Wl,--defsym,COOKIE_FIRST_PROGRAM_BASE=${COOKIE_FIRST_PROGRAM_BASE}
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/first_program.ld)
set_target_properties(cookie_first_program PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/first_program.ld)

# The flat image, which is what a loader places. The ELF is kept for symbols.
#
# No BYPRODUCTS: naming `$<TARGET_FILE:...>.bin` there fails to generate with
# "No target cookie_first_program", because BYPRODUCTS of a POST_BUILD command is
# evaluated in a scope that cannot resolve the target it is attached to. Nothing
# consumes the .bin as a build input - the CI step reads it as a path - so
# declaring it buys nothing that would justify working around that.
if(NOT CMAKE_OBJCOPY)
    message(FATAL_ERROR
        "CMAKE_OBJCOPY is empty, so the first program cannot be flattened to the "
        "bytes a loader places. A silent skip here would leave the verification "
        "step checking a file that was never produced.")
endif()
add_custom_command(TARGET cookie_first_program POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:cookie_first_program>
        $<TARGET_FILE:cookie_first_program>.bin
    COMMENT "Flattening the first program to the bytes a loader would place")
