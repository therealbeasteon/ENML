cmake_minimum_required(VERSION 3.25)
project(emnl_os VERSION 0.0.1 LANGUAGES CXX)

include(CTest)

option(EMNL_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(EMNL_BUILD_FUZZING "Build libFuzzer targets" OFF)
option(EMNL_BUILD_OPENSSL_TEST_PROVIDER "Build the OpenSSL-backed Key Service CI provider" OFF)
option(EMNL_BUILD_LINUX_TEXT_BACKEND "Build the renderer-private FreeType/HarfBuzz Linux text backend" OFF)
option(EMNL_ARM64_QEMU_USER "Run AArch64 target tests with QEMU user mode" OFF)
set(EMNL_OSIDLC_EXECUTABLE "" CACHE FILEPATH "Host osidlc executable used while cross-compiling")

option(EMNL_HARDENED_BINARIES "Enable runtime exploit mitigations" ON)
add_library(emnl_hardening INTERFACE)

if(EMNL_HARDENED_BINARIES AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    include(CheckCXXCompilerFlag)
    include(CheckLinkerFlag)
    check_cxx_compiler_flag(-fstack-protector-strong EMNL_HAS_STACK_PROTECTOR)
    if(EMNL_HAS_STACK_PROTECTOR)
        target_compile_options(emnl_hardening INTERFACE -fstack-protector-strong)
    endif()
    check_cxx_compiler_flag(-fstack-clash-protection EMNL_HAS_STACK_CLASH)
    if(EMNL_HAS_STACK_CLASH)
        target_compile_options(emnl_hardening INTERFACE -fstack-clash-protection)
    endif()
    check_cxx_compiler_flag(-mbranch-protection=standard EMNL_HAS_BRANCH_PROTECTION)
    if(EMNL_HAS_BRANCH_PROTECTION)
        target_compile_options(emnl_hardening INTERFACE -mbranch-protection=standard)
    else()
        check_cxx_compiler_flag(-fcf-protection=full EMNL_HAS_CF_PROTECTION)
        if(EMNL_HAS_CF_PROTECTION)
            target_compile_options(emnl_hardening INTERFACE -fcf-protection=full)
        endif()
    endif()
    target_compile_definitions(emnl_hardening INTERFACE _GLIBCXX_ASSERTIONS)
    target_compile_definitions(emnl_hardening INTERFACE
        $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
    foreach(emnl_link_flag "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack")
        string(MAKE_C_IDENTIFIER "EMNL_HAS_LINK${emnl_link_flag}" emnl_link_flag_var)
        check_linker_flag(CXX "${emnl_link_flag}" ${emnl_link_flag_var})
        if(${emnl_link_flag_var})
            target_link_options(emnl_hardening INTERFACE "${emnl_link_flag}")
        endif()
    endforeach()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    message(STATUS
        "EMNL hardening: stack-protector=${EMNL_HAS_STACK_PROTECTOR} "
        "stack-clash=${EMNL_HAS_STACK_CLASH} "
        "branch-protection=${EMNL_HAS_BRANCH_PROTECTION} "
        "cf-protection=${EMNL_HAS_CF_PROTECTION}")
endif()

add_library(emnl_warnings INTERFACE)
target_compile_features(emnl_warnings INTERFACE cxx_std_20)
target_link_libraries(emnl_warnings INTERFACE emnl_hardening)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(emnl_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
    )
    if(EMNL_WARNINGS_AS_ERRORS)
        target_compile_options(emnl_warnings INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(emnl_warnings INTERFACE /W4 /permissive-)
    if(EMNL_WARNINGS_AS_ERRORS)
        target_compile_options(emnl_warnings INTERFACE /WX)
    endif()
endif()

# Where Cookie's first compiled program is linked, and how far into its region
# the real entry sits. Declared here rather than in either consumer, because
# there are two and they must not disagree: system/programs/first links the
# program at these addresses, and core/oskernel's boot image maps its region and
# seals its entry at them.
#
# docs/M7_16_FIRST_PROGRAM_CONTRACT.md makes this a requirement rather than a
# tidiness preference - "the link address and the plan are derived from one
# source" - because PR #145 already found the defect it prevents once: the ARM64
# Image header declared a text_offset the image was not linked at, and it
# survived because only one of the two statements was ever read. Neither
# consumer carries a default, so a build that fails to pass one of these fails to
# link rather than quietly using a second opinion.
#
# The base is the address the boot proof already maps a created space's code
# region at. It is deliberately one no other space in the image maps, which is
# what makes "this program ran in the space created for it" provable by the
# address it executed from.
set(COOKIE_FIRST_PROGRAM_BASE "0x21000000" CACHE STRING
    "Virtual address the boot proof maps the first program's code region at")
# Not tuned. It only has to be larger than the decoy compiles to, and the linker
# script's own ASSERT is what enforces that rather than the number being trusted.
set(COOKIE_FIRST_ENTRY_OFFSET "0x200" CACHE STRING
    "Offset into the first program's region at which its real entry sits")

add_subdirectory(core/oscore)
add_subdirectory(core/osipc)
add_subdirectory(core/osservice)
add_subdirectory(core/ossandbox)
add_subdirectory(core/ospkg)
add_subdirectory(core/osapp)
add_subdirectory(core/osstorage)
add_subdirectory(core/oskeys)
add_subdirectory(core/osdisplay)
add_subdirectory(core/osui)
add_subdirectory(core/osaccessibility)
add_subdirectory(core/oscollection)
add_subdirectory(core/osshell)
add_subdirectory(core/osboot)
add_subdirectory(core/osdevice)
add_subdirectory(core/ostime)
add_subdirectory(core/osnetwork)
add_subdirectory(core/osimage)
add_subdirectory(core/oskernel)
# After oskernel: osabi consumes its include directories (the ABI declarations)
# without linking its library. See core/osabi/CMakeLists.txt.
add_subdirectory(core/osabi)
add_subdirectory(tools/osidlc)
add_subdirectory(interfaces/echo)
add_subdirectory(interfaces/storage)
add_subdirectory(system/services/echo)
add_subdirectory(system/services/storage)
add_subdirectory(system/services/keys)
add_subdirectory(system/services/compositor)
add_subdirectory(system/services/accessibility)
add_subdirectory(system/services/shell)
add_subdirectory(system/supervisor)
add_subdirectory(system/app_manager)
# Cookie's first compiled program. Guards itself on EMNL_BUILD_AARCH64_BOOT_IMAGE
# and on an AArch64 target, because EL0 on that machine is the only place it can
# run - see system/programs/first/CMakeLists.txt.
add_subdirectory(system/programs/first)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(fuzz)
