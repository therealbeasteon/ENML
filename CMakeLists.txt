cmake_minimum_required(VERSION 3.25)
project(emnl_os VERSION 0.0.1 LANGUAGES CXX)

include(CTest)

option(EMNL_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(EMNL_BUILD_FUZZING "Build libFuzzer targets" OFF)
option(EMNL_BUILD_OPENSSL_TEST_PROVIDER "Build the OpenSSL-backed Key Service CI provider" OFF)
option(EMNL_BUILD_LINUX_TEXT_BACKEND "Build the renderer-private FreeType/HarfBuzz Linux text backend" OFF)
option(EMNL_ARM64_QEMU_USER "Run AArch64 target tests with QEMU user mode" OFF)
set(EMNL_OSIDLC_EXECUTABLE "" CACHE FILEPATH "Host osidlc executable used while cross-compiling")

add_library(emnl_warnings INTERFACE)
target_compile_features(emnl_warnings INTERFACE cxx_std_20)

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
