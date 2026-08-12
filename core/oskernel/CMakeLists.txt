add_library(oskernel STATIC
    src/abi.cpp
    src/rendezvous.cpp
    src/capability.cpp
    src/interrupt.cpp
    src/scheduler.cpp
    src/kernel.cpp
    src/fdt.cpp
    src/hardware_inventory.cpp
    src/boot_memory.cpp
    src/boot_memory_plan.cpp
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

option(EMNL_BUILD_AARCH64_BOOT_IMAGE "Build the freestanding Cookie AArch64 kernel image" OFF)
set(EMNL_AARCH64_BOOT_LOAD_ADDRESS "0x40080000" CACHE STRING
    "Machine-port load address for the standalone AArch64 image")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    enable_language(ASM)
    add_library(oskernel_machine_aarch64 STATIC
        src/machine_aarch64.cpp
        src/aarch64_translation.cpp
        src/aarch64_entry.cpp
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

    if(EMNL_BUILD_AARCH64_BOOT_IMAGE)
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/boot/aarch64_qemu.ld.in
            ${CMAKE_CURRENT_BINARY_DIR}/cookie_aarch64_boot.ld
            @ONLY)

        add_executable(cookie_kernel_aarch64_boot
            boot/aarch64_start.S
            boot/aarch64_boot.cpp
            boot/freestanding_memory.cpp
            src/fdt.cpp
            src/hardware_inventory.cpp
            src/boot_memory.cpp
            src/boot_memory_plan.cpp
            src/machine_aarch64.cpp
            src/aarch64_translation.cpp
            src/aarch64_entry.cpp
            src/aarch64_vectors.S
            src/aarch64_context_switch.S
        )
        target_include_directories(cookie_kernel_aarch64_boot PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/core/oscore/include)
        target_compile_features(cookie_kernel_aarch64_boot PRIVATE cxx_std_20)
        # -mstrict-align is required, not a hardening preference.
        #
        # This image runs with translation disabled from _start until
        # activate_stage1_translation. While it is off, every data access takes
        # the Device-nGnRnE attribute regardless of what the page tables will
        # later say, and Device memory does not permit unaligned access. SCTLR
        #_EL1.A is irrelevant here: that bit governs alignment checking for
        # Normal memory, so clearing it does not buy tolerance the memory type
        # never had.
        #
        # Without this flag the compiler is free to emit an unaligned load or a
        # multi-register copy for an ordinary struct assignment, and one did -
        # inside FdtView::parse, building its Result return value. The fault
        # was taken before any of this code could report it.
        target_compile_options(cookie_kernel_aarch64_boot PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -fno-pie -mstrict-align>
            $<$<COMPILE_LANGUAGE:ASM>:-ffreestanding -fno-pie>)
        target_link_options(cookie_kernel_aarch64_boot PRIVATE
            -nostdlib
            -static
            -no-pie
            -Wl,--build-id=none
            -Wl,-T,${CMAKE_CURRENT_BINARY_DIR}/cookie_aarch64_boot.ld)
        set_target_properties(cookie_kernel_aarch64_boot PROPERTIES
            POSITION_INDEPENDENT_CODE OFF
            LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cookie_aarch64_boot.ld)

        if(CMAKE_OBJCOPY)
            add_custom_command(TARGET cookie_kernel_aarch64_boot POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} -O binary
                    $<TARGET_FILE:cookie_kernel_aarch64_boot>
                    ${CMAKE_CURRENT_BINARY_DIR}/cookie_kernel_aarch64_boot.bin
                BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/cookie_kernel_aarch64_boot.bin
                COMMENT "Creating freestanding Cookie AArch64 kernel binary")
        endif()
    endif()
endif()
