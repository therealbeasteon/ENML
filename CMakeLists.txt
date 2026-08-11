cmake_minimum_required(VERSION 3.25)
project(emnl_os VERSION 0.0.1 LANGUAGES CXX)

include(CTest)

option(EMNL_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(EMNL_BUILD_FUZZING "Build libFuzzer targets" OFF)
option(EMNL_BUILD_OPENSSL_TEST_PROVIDER "Build the OpenSSL-backed Key Service CI provider" OFF)
option(EMNL_BUILD_LINUX_TEXT_BACKEND "Build the renderer-private FreeType/HarfBuzz Linux text backend" OFF)
option(EMNL_ARM64_QEMU_USER "Run AArch64 target tests with QEMU user mode" OFF)
set(EMNL_OSIDLC_EXECUTABLE "" CACHE FILEPATH "Host osidlc executable used while cross-compiling")

# Binary hardening.
#
# The warning set below is compile-time correctness; it does nothing to an
# attacker who finds a bug anyway. These are the runtime mitigations, which cost
# a little size and almost no speed and remove whole classes of exploitation
# technique. An OS that calls itself hardened and ships without them is relying
# entirely on having written no memory-safety bugs.
#
# Every flag is feature-tested rather than assumed: this tree builds under GCC
# and Clang, on x86-64 and AArch64, and the available mitigations differ across
# all of those. A flag the compiler does not understand must not become a build
# failure on some platform we have not tried yet.
option(EMNL_HARDENED_BINARIES "Enable runtime exploit mitigations" ON)

add_library(emnl_hardening INTERFACE)

if(EMNL_HARDENED_BINARIES AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    include(CheckCXXCompilerFlag)
    include(CheckLinkerFlag)

    # Stack canaries on any function with a local buffer or address-taken local.
    check_cxx_compiler_flag(-fstack-protector-strong EMNL_HAS_STACK_PROTECTOR)
    if(EMNL_HAS_STACK_PROTECTOR)
        target_compile_options(emnl_hardening INTERFACE -fstack-protector-strong)
    endif()

    # Probes each stack page so a large allocation cannot skip the guard page
    # and land in another mapping.
    check_cxx_compiler_flag(-fstack-clash-protection EMNL_HAS_STACK_CLASH)
    if(EMNL_HAS_STACK_CLASH)
        target_compile_options(emnl_hardening INTERFACE -fstack-clash-protection)
    endif()

    # Forward-edge control flow integrity. The mechanism is architecture
    # specific: branch target identification and pointer authentication on
    # AArch64, indirect branch tracking on x86-64.
    check_cxx_compiler_flag(-mbranch-protection=standard EMNL_HAS_BRANCH_PROTECTION)
    if(EMNL_HAS_BRANCH_PROTECTION)
        target_compile_options(emnl_hardening INTERFACE -mbranch-protection=standard)
    else()
        check_cxx_compiler_flag(-fcf-protection=full EMNL_HAS_CF_PROTECTION)
        if(EMNL_HAS_CF_PROTECTION)
            target_compile_options(emnl_hardening INTERFACE -fcf-protection=full)
        endif()
    endif()

    # Bounds and precondition checks inside the standard library. This matters
    # more here than it would elsewhere: every wire-format parser in the tree
    # indexes a span, and a violation aborts rather than reading out of bounds,
    # which is the same fail-closed answer os::core::invariant_violated gives.
    target_compile_definitions(emnl_hardening INTERFACE _GLIBCXX_ASSERTIONS)

    # Fortified libc calls need the optimiser to compute object sizes, and
    # emit a #warning when it is not running - which -Werror would turn into a
    # build failure. Enabled only where it can actually do something.
    target_compile_definitions(emnl_hardening INTERFACE
        $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)

    # Full RELRO, a non-executable stack, and position independence so the
    # loader can place the image anywhere.
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
# Carried by the warning target so every module picks the mitigations up
# without each one having to remember to ask for them. Forgetting to harden one
# module is exactly the kind of omission nobody notices.
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
add_subdirectory(tools/osidlc)
add_subdirectory(interfaces/echo)
add_subdirectory(interfaces/storage)
add_subdirectory(system/services/echo)
add_subdirectory(system/services/storage)
add_subdirectory(system/services/keys)
add_subdirectory(system/services/compositor)
add_subdirectory(system/services/accessibility)
add_subdirectory(system/supervisor)
add_subdirectory(system/app_manager)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(fuzz)
