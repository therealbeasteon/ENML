.text
.align 4
.global cookie_aarch64_switch_context
.type cookie_aarch64_switch_context, %function

// void cookie_aarch64_switch_context(MachineContext* from,
//                                    const MachineContext* to)
//
// MachineContext layout is frozen by static_asserts in machine_aarch64.hpp:
//   0..80  x19..x29
//   88     x30/LR
//   96     SP
//
// x0/x1 are ordinary caller-clobbered argument registers and therefore are not
// part of the saved kernel-thread context. This function itself obeys AAPCS64:
// when the old thread eventually resumes, all callee-saved state appears exactly
// as it did at the original call site.
cookie_aarch64_switch_context:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    str x29, [x0, #80]
    str x30, [x0, #88]
    mov x9, sp
    str x9, [x0, #96]

    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldr x29, [x1, #80]
    ldr x30, [x1, #88]
    ldr x9, [x1, #96]
    mov sp, x9
    ret

.size cookie_aarch64_switch_context, .-cookie_aarch64_switch_context
