.section .text.boot, "ax"
.align 7
.global _start
.type _start, %function

// Loader envelope for firmware/emulators that implement the conventional
// AArch64 Image boot contract. This is transport metadata only: Cookie keeps
// its own kernel ABI and does not consume Linux services or Linux runtime state.
//
// code0/code1 occupy the first 8 bytes and branch over the 64-byte header.
// text_offset=0x80000 matches Cookie's linked 0x4008_0000 placement when RAM
// begins at a 2 MiB-aligned 0x4000_0000 base. image_size is linker-derived.
_start:
    b .Lcookie_entry
    nop
    .quad 0x0000000000080000                  // text_offset
    .quad __cookie_image_end - __cookie_image_start // image_size
    .quad 0x0000000000000000                  // flags: LE, page size unspecified
    .quad 0x0000000000000000                  // reserved
    .quad 0x0000000000000000                  // reserved
    .quad 0x0000000000000000                  // reserved
    .long 0x644d5241                           // "ARM\x64"
    .long 0x00000000                           // reserved / PE header offset

// AArch64 Cookie boot contract:
//   x0 = physical pointer to a flattened device tree
//   MMU is disabled
//   entry is at EL1
//
// The machine port owns how the image is loaded. No QEMU MMIO constants are
// encoded here; the 64-byte envelope only makes the raw image self-describing
// to standard AArch64 loaders while preserving the Cookie entry contract.
.Lcookie_entry:
    mrs x1, CurrentEL
    cmp x1, #0x4
    b.ne .Lhalt

    // Interrupt delivery is an explicit Cookie state transition. Firmware/reset
    // DAIF state is not trusted; nothing asynchronous is admitted until GICv3,
    // vectors and the timer are fully established and EL0 ERET clears I.
    msr daifset, #0xf
    isb

    mov x19, x0
    adrp x1, __cookie_bootstrap_stack_top
    add  x1, x1, :lo12:__cookie_bootstrap_stack_top
    mov sp, x1

    adrp x1, __bss_start
    add  x1, x1, :lo12:__bss_start
    adrp x2, __bss_end
    add  x2, x2, :lo12:__bss_end
.Lzero_bss:
    cmp x1, x2
    b.hs .Lbss_done
    str xzr, [x1], #8
    b .Lzero_bss
.Lbss_done:
    mov x0, x19
    bl cookie_aarch64_boot_main

.Lhalt:
    msr daifset, #0xf
.Lhalt_loop:
    wfe
    b .Lhalt_loop

.size _start, .-_start

.section .bss.boot_stack, "aw", %nobits
.align 12
.global __cookie_bootstrap_stack_start
__cookie_bootstrap_stack_start:
    .skip 16384
.global __cookie_bootstrap_stack_top
__cookie_bootstrap_stack_top:
