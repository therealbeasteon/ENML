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
# is migrating off Linux. On a native AArch64 builder, also compile the real
# architecture backend as a separate archive. It is intentionally not linked
# into the hosted tests yet: unsupported machine operations fail closed there,
# and the backend becomes the runtime implementation only once exception entry,
# address spaces and context switching are complete enough to boot the emulator.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    add_library(oskernel_machine_aarch64 STATIC
        src/machine_aarch64.cpp
    )
    add_library(emnl::oskernel_machine_aarch64 ALIAS oskernel_machine_aarch64)
    target_include_directories(oskernel_machine_aarch64
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(oskernel_machine_aarch64 PUBLIC emnl::oscore emnl_warnings)
    target_compile_options(oskernel_machine_aarch64 PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
endif()
