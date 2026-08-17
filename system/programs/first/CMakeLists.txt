# Cookie's first compiled program.
#
# Built only when the freestanding AArch64 image is, because it targets EL0 on
# that machine and nothing else can run it. Guarded the same way
# cookie_kernel_aarch64_boot is, and for the same reason: a target that cannot
# link on a host build must not be part of a host build.
#
# The two conditions are written to match core/oskernel/CMakeLists.txt's guard on
# cookie_kernel_aarch64_boot exactly, including the anchoring, and that is a
# requirement rather than tidiness: the boot image names
# $<TARGET_FILE:cookie_first_program> in a compile definition, so a configuration
# that builds the image without this target fails at generate time with "No
# target". A looser pattern here happens to be safe today only because it is a
# superset of the kernel's - which is a fact about two regular expressions, not
# a property anyone should have to re-derive.
if(NOT EMNL_BUILD_AARCH64_BOOT_IMAGE)
    return()
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    return()
endif()

# The two addresses this program is linked at come from the top-level
# CMakeLists.txt, which declares them once for both consumers - this target and
# the kernel boot image that maps the region and seals the entry. They are not
# defaulted here, and the linker script no longer defaults them either: a build
# that fails to supply one must fail, not link at a second opinion.
if(NOT DEFINED COOKIE_FIRST_PROGRAM_BASE OR NOT DEFINED COOKIE_FIRST_ENTRY_OFFSET)
    message(FATAL_ERROR
        "COOKIE_FIRST_PROGRAM_BASE and COOKIE_FIRST_ENTRY_OFFSET are declared by "
        "the top-level CMakeLists.txt and are what keep this program's link "
        "address and the kernel's mapped region from being two statements of one "
        "address. Building this directory without them would produce a program "
        "linked somewhere the kernel does not map.")
endif()

# osabi's sources are compiled into this target rather than linked from the
# `osabi` library, which is the pattern cookie_kernel_aarch64_boot already uses.
# The library build carries the host hardening flags - stack protector among
# them - and a stack protector needs __stack_chk_fail, which does not exist at
# EL0 on a system with no libc. Compiling the sources here gives them this
# target's freestanding flags instead.
# abi.cpp is here because `encode_call` calls `describe_call`, and the ABI table
# it looks up lives in a kernel .cpp rather than in the header.
#
# That is not an accident of layout and it is worth stating: the table *is* the
# contract, so a program encoding a call has to carry it. What a user program
# must not link is kernel *behaviour*, and it does not - abi.cpp is sixteen
# descriptors and three lookups over them, with no state and no privilege.
add_executable(cookie_first_program
    first_program.cpp
    ${PROJECT_SOURCE_DIR}/core/osabi/src/syscall.cpp
    ${PROJECT_SOURCE_DIR}/core/osabi/src/syscall_aarch64.cpp
    ${PROJECT_SOURCE_DIR}/core/oskernel/src/abi.cpp
    # memcpy and friends. -Os emits a memcpy call for the struct copy inside
    # Result's construct_at, and there is no libc at EL0 to satisfy it. Reused
    # from the kernel image rather than written again here: two definitions of
    # memcpy in one repository is two things that can differ, and this file is
    # already the answer to exactly this question one privilege level up.
    ${PROJECT_SOURCE_DIR}/core/oskernel/boot/freestanding_memory.cpp
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
    # -Os and section-per-function, so --gc-sections below can drop what is not
    # reached. The program must fit the one page the boot proof maps as a code
    # region, and linking the ABI table brought in more than the first attempt
    # left room for - the linker script's ASSERT is what said so, which is the
    # point of having it.
    -Os -ffunction-sections -fdata-sections
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
if(EMNL_WARNINGS_AS_ERRORS)
    target_compile_options(cookie_first_program PRIVATE -Werror)
endif()

target_link_options(cookie_first_program PRIVATE
    -nostdlib
    -static
    -no-pie
    -Wl,--build-id=none
    -Wl,--gc-sections
    -Wl,--defsym,COOKIE_FIRST_PROGRAM_BASE=${COOKIE_FIRST_PROGRAM_BASE}
    -Wl,--defsym,COOKIE_FIRST_ENTRY_OFFSET=${COOKIE_FIRST_ENTRY_OFFSET}
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
# The path the top-level CMakeLists.txt told the kernel image to embed has to be
# the path this target actually writes. It assumes CMake's default layout - a
# target's file lands in its own directory's binary dir - and that assumption is
# checked here rather than left to be discovered as a build embedding nothing.
if(NOT COOKIE_FIRST_PROGRAM_BIN STREQUAL
   "${CMAKE_CURRENT_BINARY_DIR}/cookie_first_program.bin")
    message(FATAL_ERROR
        "the boot image is told to embed ${COOKIE_FIRST_PROGRAM_BIN}, and this "
        "directory writes ${CMAKE_CURRENT_BINARY_DIR}/cookie_first_program.bin")
endif()

add_custom_command(TARGET cookie_first_program POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:cookie_first_program>
        ${COOKIE_FIRST_PROGRAM_BIN}
    # Declarable now, and it was not before. The earlier note here was right that
    # BYPRODUCTS cannot name $<TARGET_FILE:x> of the target it decorates - "No
    # target x" at generate time - but that was an argument against the
    # *generator expression*, not against declaring the byproduct. With a plain
    # path there is no such problem, and the declaration is what lets the build
    # graph know this file has a producer: without it, the boot image's
    # dependency on these bytes is a file ninja has no rule to make.
    BYPRODUCTS ${COOKIE_FIRST_PROGRAM_BIN}
    COMMENT "Flattening the first program to the bytes a loader would place")

# The boot image carries those bytes: core/oskernel/boot/first_program_image.S
# `.incbin`s the .bin above, so the program travels inside the one artefact boot
# places and is covered by whatever measures it - docs/M7_12_FIRST_PROGRAM.md,
# "boot places exactly one image".
#
# Declared here rather than in core/oskernel/CMakeLists.txt because this is the
# first point at which both targets exist: the kernel's directory is added
# first, so `add_dependencies` there would name a target CMake has not seen. The
# compile definition that carries the *path* does live over there, because a
# generator expression is resolved after every directory has been read, and
# because the file list that gate .github/scripts/kernel-line-count.sh reads must
# stay in one place.
if(NOT TARGET cookie_kernel_aarch64_boot)
    # Not reachable through the guards at the top of this file, which are the
    # kernel image's own. Checked anyway rather than assumed, because the failure
    # it would otherwise produce is a generate-time "No target
    # cookie_first_program" from a compile definition three directories away,
    # which says nothing about the guard that drifted.
    message(FATAL_ERROR
        "cookie_kernel_aarch64_boot does not exist, so nothing would carry this "
        "program into an image. The guards at the top of this file and the ones "
        "on that target have diverged.")
endif()
add_dependencies(cookie_kernel_aarch64_boot cookie_first_program)
# Ordering alone is not enough: without this the assembler's output is not
# rebuilt when the program changes, and the boot image would keep embedding
# whichever version of the program it first saw - an incremental build would then
# boot yesterday's program and report today's as having run.
set_property(
    SOURCE ${PROJECT_SOURCE_DIR}/core/oskernel/boot/first_program_image.S
    DIRECTORY ${PROJECT_SOURCE_DIR}/core/oskernel
    APPEND PROPERTY OBJECT_DEPENDS ${COOKIE_FIRST_PROGRAM_BIN})
