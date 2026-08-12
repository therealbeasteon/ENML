.section .text.boot, "ax"
.align 7
.global _start
.type _start, %function

// AArch64 Cookie boot contract:
//   x0 = physical pointer to a flattened device tree
//   MMU is disabled
//   entry is at EL1
//
// The machine port owns how the image is loaded. This entry contains no QEMU
// MMIO constants and no Linux ABI assumptions.
_start:
    // Cookie's first native image currently requires EL1. A future EL2 shim may
    // deliberately drop to EL1, but silently continuing at the wrong level
    // would make every system-register assumption unsafe.
    mrs x1, CurrentEL
    cmp x1, #0x4
    b.ne .Lhalt

    // Preserve the firmware DTB handoff while establishing a known bootstrap
    // stack. This stack is only used while the real guarded stack is mapped.
    mov x19, x0
    adrp x1, __cookie_bootstrap_stack_top
    add  x1, x1, :lo12:__cookie_bootstrap_stack_top
    mov sp, x1

    // Freestanding C++ expects zero-initialized BSS. Do this before calling any
    // C++ code, without using the stack while it is being cleared.
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

// Returning from the kernel bootstrap is always an invariant violation. Keep
// interrupts masked and wait forever rather than falling into image data.
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
