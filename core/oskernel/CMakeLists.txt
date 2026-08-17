add_library(oskernel STATIC
    src/abi.cpp
    src/syscall_outcome.cpp
    src/rendezvous.cpp
    src/capability.cpp
    src/ipc_endpoint.cpp
    src/ipc_syscall.cpp
    src/address_space_syscall.cpp
    src/memory_grant.cpp
    src/fault_delivery.cpp
    src/thread_admission.cpp
    src/ipc_continuation.cpp
    src/interrupt.cpp
    src/interrupt_delivery.cpp
    src/scheduler.cpp
    src/scheduler_deadline.cpp
    src/address_space_epoch.cpp
    src/process_translation.cpp
    src/fault_region.cpp
    src/user_access.cpp
    src/aarch64_user_frames.cpp
    src/aarch64_preemption.cpp
    src/aarch64_execution_universe.cpp
    src/kernel.cpp
    src/fdt.cpp
    src/hardware_inventory.cpp
    src/gic_v3_discovery.cpp
    src/arch_timer_discovery.cpp
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
# QEMU virt reserves 0x40000000..0x40100000 for its generated DTB when using
# the generic bare-metal loader. Keep Cookie's linked image outside that region.
# The address remains a machine-port cache variable so physical boards can
# supply their own non-overlapping placement without changing kernel code.
set(EMNL_AARCH64_BOOT_LOAD_ADDRESS "0x40200000" CACHE STRING
    "Machine-port load address for the standalone AArch64 image")
# The base of RAM on the same machine port. It exists so the ARM64 Image
# header's text_offset can be *derived* rather than written by hand: the header
# and the linker script were previously free to disagree, and did - the header
# said 0x80000 while the image was linked 2 MiB above the base. A bootloader
# honouring the header would have placed Cookie where none of its absolute
# references resolve. A port now sets these two numbers and the header follows.
set(EMNL_AARCH64_BOOT_RAM_BASE "0x40000000" CACHE STRING
    "Machine-port base of RAM, used to derive the Image header's text_offset")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    enable_language(ASM)
    add_library(oskernel_machine_aarch64 STATIC
        src/machine_aarch64.cpp
        src/aarch64_translation.cpp
        src/aarch64_asid.cpp
        src/aarch64_user_copy_guard.cpp
        src/aarch64_user_access.cpp
        src/aarch64_ipc_syscall.cpp
        src/aarch64_interrupt_syscall.cpp
        src/aarch64_execution_universe_machine.cpp
        src/aarch64_entry.cpp
        src/aarch64_gic_v3.cpp
        src/aarch64_vectors.S
        src/aarch64_user_access.S
        src/aarch64_context_switch.S
        src/aarch64_el0.S
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
            boot/first_program_image.S
            boot/freestanding_memory.cpp
            src/abi.cpp
            src/syscall_outcome.cpp
            src/rendezvous.cpp
            src/capability.cpp
            src/ipc_endpoint.cpp
            src/ipc_syscall.cpp
            src/address_space_syscall.cpp
            src/memory_grant.cpp
            src/fault_region.cpp
            src/fault_delivery.cpp
            src/thread_admission.cpp
            src/ipc_continuation.cpp
            src/interrupt.cpp
            src/interrupt_delivery.cpp
            src/scheduler.cpp
            src/scheduler_deadline.cpp
            src/address_space_epoch.cpp
            src/process_translation.cpp
            src/user_access.cpp
            src/aarch64_user_frames.cpp
            src/aarch64_preemption.cpp
            src/aarch64_execution_universe.cpp
            src/kernel.cpp
            src/fdt.cpp
            src/hardware_inventory.cpp
            src/gic_v3_discovery.cpp
            src/arch_timer_discovery.cpp
            src/boot_memory.cpp
            src/boot_memory_plan.cpp
            src/machine_aarch64.cpp
            src/aarch64_translation.cpp
            src/aarch64_asid.cpp
            src/aarch64_user_copy_guard.cpp
            src/aarch64_user_access.cpp
            src/aarch64_ipc_syscall.cpp
            src/aarch64_interrupt_syscall.cpp
            src/aarch64_execution_universe_machine.cpp
            src/aarch64_entry.cpp
            src/aarch64_gic_v3.cpp
            src/aarch64_vectors.S
            src/aarch64_user_access.S
            src/aarch64_context_switch.S
            src/aarch64_el0.S
        )
        target_include_directories(cookie_kernel_aarch64_boot PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/core/oscore/include
            # For cookie/first_witness.hpp. The kernel checks the witness the
            # first program computes, so both sides must share one definition of
            # the fold - the header is that definition and neither side restates
            # it. Nothing else of the program's is visible from here.
            ${PROJECT_SOURCE_DIR}/system/programs/first/include)

        # The three facts the boot image needs about the program it carries: its
        # bytes, where they are mapped, and where its real entry sits. All three
        # come from the top-level CMakeLists.txt, which is their only definition.
        # The program's linker script is handed the same two addresses and
        # carries no default of its own, so an image that maps the region
        # somewhere the program was not linked cannot be built.
        #
        # The path is a plain string, deliberately, and not
        # $<TARGET_FILE:cookie_first_program>.bin. That spelling works here and
        # not in the OBJECT_DEPENDS that has to name the same file, because
        # source properties do not expand generator expressions - so the two
        # references to one path would have to be written two different ways, and
        # only one of them would be checked. The build-order dependency is
        # declared in system/programs/first, where both targets exist.
        target_compile_definitions(cookie_kernel_aarch64_boot PRIVATE
            COOKIE_FIRST_PROGRAM_IMAGE="${COOKIE_FIRST_PROGRAM_BIN}"
            COOKIE_FIRST_PROGRAM_BASE=${COOKIE_FIRST_PROGRAM_BASE}
            COOKIE_FIRST_ENTRY_OFFSET=${COOKIE_FIRST_ENTRY_OFFSET})
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
        #
        # -mgeneral-regs-only is required for the same window, for a different
        # reason. Cookie does not enable or context-switch FP/SIMD state during
        # early EL1 boot yet. Prevent the compiler from silently using vector
        # registers for structure copies before that contract exists; otherwise
        # such instructions trap as undefined at EL1.
        #
        # Both flags are load-bearing and independent. Neither substitutes for
        # the other, and dropping either one produces an image that links but
        # faults during early boot.
        #
        # -fpie, and the image is linked -pie. A phone's bootloader chooses
        # where the image goes; an image that only runs at its link address is
        # not portable to a machine whose RAM starts somewhere else, however
        # honest its header is about which address that is.
        #
        # The interesting part is what this does *not* need. Cookie has no
        # virtual functions, so no vtables; no absolute pointers in initialised
        # data; and no global constructors - the linker script already asserts
        # the last of those. So position independence costs a compiler flag and
        # PC-relative addressing rather than a relocation processor running
        # before anything is verified. The build asserts that the relocation
        # count is zero, which is what turns that from an observation into a
        # property: the day something introduces an absolute pointer in static
        # data, the build says so instead of the phone doing.
        #
        # -fvisibility=hidden is load-bearing rather than tidiness. With default
        # visibility, -fpie makes the compiler assume any global might be
        # interposed by another object at run time, so it reaches symbols
        # through a global offset table - and a GOT entry is exactly the
        # absolute pointer in static data this is trying not to have, one
        # relocation each. Hidden visibility says nothing outside this image can
        # interpose, which is true because there is no dynamic linker here for
        # interposition to come from, and the compiler then emits PC-relative
        # adrp/add instead.
        target_compile_options(cookie_kernel_aarch64_boot PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -fpie -fvisibility=hidden -mstrict-align -mgeneral-regs-only>
            $<$<COMPILE_LANGUAGE:ASM>:-ffreestanding -fpie>)
        target_link_options(cookie_kernel_aarch64_boot PRIVATE
            -nostdlib
            -pie
            -Wl,--no-dynamic-linker
            -Wl,--build-id=none
            -Wl,-T,${CMAKE_CURRENT_BINARY_DIR}/cookie_aarch64_boot.ld)
        set_target_properties(cookie_kernel_aarch64_boot PROPERTIES
            POSITION_INDEPENDENT_CODE ON
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
