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

// The bootstrap stack is the only stack in existence until the guarded stack
// is mapped, and cookie_aarch64_boot_main is a large frame: the debug build
// the QEMU gate uses allocates every local in that function at entry -
// hardware inventory, memory plan, page arena, table builder, address space,
// physical ledger and two machine contexts - before the first call is made.
// Below this region lies .rodata and then .text, so a frame that does not fit
// does not fault. With translation still off it writes over the kernel's own
// code and the machine executes whatever that leaves behind.
//
// 64 KiB rather than 16. This is sized for the debug build deliberately: the
// gate that has to catch a stack problem is the one that runs the largest
// frames, so the release build being comfortable is not the question.
.section .bss.boot_stack, "aw", %nobits
.align 12
.global __cookie_bootstrap_stack_start
__cookie_bootstrap_stack_start:
    .skip 65536
.global __cookie_bootstrap_stack_top
__cookie_bootstrap_stack_top:
