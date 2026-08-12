.text
.align 4
.global cookie_aarch64_enter_el0
.type cookie_aarch64_enter_el0, %function

// [[noreturn]] void cookie_aarch64_enter_el0(uint64_t entry, uint64_t stack)
//
// M7.5f enters AArch64 EL0t with DAIF masked until GIC/timer delivery is a
// separately reviewed milestone. x0-x30 begin zeroed so the first userspace
// instruction cannot inherit kernel register contents. ELR_EL1 and SP_EL0 are
// the only caller-provided execution state.
cookie_aarch64_enter_el0:
    msr sp_el0, x1
    msr elr_el1, x0

    // SPSR_EL1.M[3:0] = 0000 -> EL0t.
    // D/A/I/F are masked for this first controlled syscall proof.
    mov x2, #0x3c0
    msr spsr_el1, x2
    isb

    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr
    eret

.size cookie_aarch64_enter_el0, .-cookie_aarch64_enter_el0
