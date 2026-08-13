.section .text.cookie_vectors, "ax"
.align 11
.global cookie_aarch64_vector_table
.type cookie_aarch64_vector_table, %function

.macro VECTOR target
    b \target
    .space 124
.endm

cookie_aarch64_vector_table:
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SP0, sync
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SP0, IRQ
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SP0, FIQ
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SP0, SError
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SPx, sync
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SPx, IRQ
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SPx, FIQ
    VECTOR cookie_aarch64_unhandled_vector   // current EL, SPx, SError
    VECTOR cookie_aarch64_sync_lower_a64     // lower EL, AArch64, sync
    VECTOR cookie_aarch64_irq_lower_a64      // lower EL, AArch64, IRQ
    VECTOR cookie_aarch64_unhandled_vector   // lower EL, AArch64, FIQ
    VECTOR cookie_aarch64_unhandled_vector   // lower EL, AArch64, SError
    VECTOR cookie_aarch64_unhandled_vector   // lower EL, AArch32, sync
    VECTOR cookie_aarch64_unhandled_vector   // lower EL, AArch32, IRQ
    VECTOR cookie_aarch64_unhandled_vector   // lower EL, AArch32, FIQ
    VECTOR cookie_aarch64_unhandled_vector   // lower EL, AArch32, SError
.size cookie_aarch64_vector_table, . - cookie_aarch64_vector_table

.equ COOKIE_FRAME_SIZE,    288
.equ COOKIE_X30,           240
.equ COOKIE_SP_EL0,        248
.equ COOKIE_ELR_EL1,       256
.equ COOKIE_SPSR_EL1,      264
.equ COOKIE_ESR_EL1,       272
.equ COOKIE_FAR_EL1,       280

.macro COOKIE_SAVE_FRAME
    sub sp, sp, #COOKIE_FRAME_SIZE
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #COOKIE_X30]

    mrs x9, sp_el0
    str x9, [sp, #COOKIE_SP_EL0]
    mrs x9, elr_el1
    str x9, [sp, #COOKIE_ELR_EL1]
    mrs x9, spsr_el1
    str x9, [sp, #COOKIE_SPSR_EL1]
    mrs x9, esr_el1
    str x9, [sp, #COOKIE_ESR_EL1]
    mrs x9, far_el1
    str x9, [sp, #COOKIE_FAR_EL1]
.endm

.macro COOKIE_RESTORE_FRAME
    ldr x9, [sp, #COOKIE_SP_EL0]
    msr sp_el0, x9
    ldr x9, [sp, #COOKIE_ELR_EL1]
    msr elr_el1, x9
    ldr x9, [sp, #COOKIE_SPSR_EL1]
    msr spsr_el1, x9

    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #COOKIE_X30]
    add sp, sp, #COOKIE_FRAME_SIZE
    eret
.endm

.type cookie_aarch64_sync_lower_a64, %function
cookie_aarch64_sync_lower_a64:
    COOKIE_SAVE_FRAME
    mov x0, sp
    bl cookie_aarch64_sync_exception_dispatch
    COOKIE_RESTORE_FRAME
.size cookie_aarch64_sync_lower_a64, . - cookie_aarch64_sync_lower_a64

.type cookie_aarch64_irq_lower_a64, %function
cookie_aarch64_irq_lower_a64:
    COOKIE_SAVE_FRAME
    mov x0, sp
    bl cookie_aarch64_irq_dispatch
    COOKIE_RESTORE_FRAME
.size cookie_aarch64_irq_lower_a64, . - cookie_aarch64_irq_lower_a64

.type cookie_aarch64_unhandled_vector, %function
cookie_aarch64_unhandled_vector:
    bl cookie_aarch64_unhandled_exception
1:
    wfe
    b 1b
.size cookie_aarch64_unhandled_vector, . - cookie_aarch64_unhandled_vector
