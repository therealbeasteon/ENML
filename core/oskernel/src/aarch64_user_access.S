.section .text.cookie_user_access, "ax"

.global cookie_aarch64_ldtrb_user_byte
.type cookie_aarch64_ldtrb_user_byte, %function
cookie_aarch64_ldtrb_user_byte:
    // x0 = user address, x1 = kernel destination byte
    ldtrb w2, [x0]
    strb w2, [x1]
    mov w0, #1
    ret
.size cookie_aarch64_ldtrb_user_byte, . - cookie_aarch64_ldtrb_user_byte

.global cookie_aarch64_ldtrb_user_byte_recovery
.type cookie_aarch64_ldtrb_user_byte_recovery, %function
cookie_aarch64_ldtrb_user_byte_recovery:
    // Same-EL data-abort dispatcher redirects ELR_EL1 here only while a
    // matching Cookie user-copy guard is armed.
    mov w0, #0
    ret
.size cookie_aarch64_ldtrb_user_byte_recovery, . - cookie_aarch64_ldtrb_user_byte_recovery

.global cookie_aarch64_sttrb_user_byte
.type cookie_aarch64_sttrb_user_byte, %function
cookie_aarch64_sttrb_user_byte:
    // x0 = user address, w1 = byte value
    sttrb w1, [x0]
    mov w0, #1
    ret
.size cookie_aarch64_sttrb_user_byte, . - cookie_aarch64_sttrb_user_byte

.global cookie_aarch64_sttrb_user_byte_recovery
.type cookie_aarch64_sttrb_user_byte_recovery, %function
cookie_aarch64_sttrb_user_byte_recovery:
    mov w0, #0
    ret
.size cookie_aarch64_sttrb_user_byte_recovery, . - cookie_aarch64_sttrb_user_byte_recovery
