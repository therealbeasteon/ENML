add_library(oskernel STATIC
    src/abi.cpp
    src/rendezvous.cpp
    src/capability.cpp
    src/interrupt.cpp
    src/scheduler.cpp
    src/kernel.cpp
    src/machine_host.cpp
)

add_library(emnl::oskernel ALIAS oskernel)

target_include_directories(oskernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(oskernel PUBLIC emnl::oscore emnl_warnings)

target_compile_options(oskernel PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

# The host machine remains the executable contract implementation while Cookie
# is migrating off Linux. On a native AArch64 builder, compile the real machine
# backend, translation activation, vector entry and context-switch assembly as a
# separate archive. Assembly is enabled only here so architecture code cannot
# leak into portable targets by accident.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    enable_language(ASM)
    add_library(oskernel_machine_aarch64 STATIC
        src/machine_aarch64.cpp
        src/aarch64_translation.cpp
        src/aarch64_vectors.S
        src/aarch64_context_switch.S
    )
    add_library(emnl::oskernel_machine_aarch64 ALIAS oskernel_machine_aarch64)
    target_include_directories(oskernel_machine_aarch64
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(oskernel_machine_aarch64 PUBLIC emnl::oscore emnl_warnings)
    target_compile_options(oskernel_machine_aarch64 PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>>
    )
endif()
