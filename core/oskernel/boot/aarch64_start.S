.section .text.boot, "ax"
.align 7
.global _start
.type _start, %function

// Loader envelope for firmware/emulators that implement the conventional
// AArch64 Image boot contract. This is transport metadata only: Cookie keeps
// its own kernel ABI and does not consume Linux services or Linux runtime state.
//
// code0/code1 occupy the first 8 bytes and branch over the 64-byte header.
//
// Both size fields are linker-derived and neither is written here, which is the
// point rather than a convenience. text_offset used to be the literal 0x80000
// while the image linked at 0x40200000 - so the header told a bootloader to
// place Cookie 1.5 MiB below where every absolute reference in it points, and
// nothing caught it because the boot proof loads the ELF by program headers and
// never reads this field. The one field a phone would actually use was the one
// no gate exercised. It is now __cookie_text_offset, computed by the linker
// script from the port's load address and RAM base, so the header cannot
// disagree with the link.
//
// This remains transport metadata only: Cookie keeps its own kernel ABI and
// consumes no Linux services or runtime state.
_start:
    b .Lcookie_entry
    nop
    .quad __cookie_text_offset                 // derived; see cookie_aarch64_boot.ld
    .quad __cookie_image_size                 // image_size, resolved by linker script
    // Little-endian, 4 KiB pages. The page-size field was previously
    // "unspecified", which is a bootloader's licence to place the image for a
    // granule Cookie does not use - docs/M7_13_HARDWARE_NEUTRALITY.md records
    // that 4 KiB is assumed throughout, and this is the one place the assumption
    // is visible to anything outside the image.
    .quad 0x0000000000000002                  // flags: LE, 4 KiB granule
    .quad 0x0000000000000000                  // reserved
    .quad 0x0000000000000000                  // reserved
    .quad 0x0000000000000000                  // reserved
    .long 0x644d5241                           // "ARM\x64"
    .long 0x00000000                           // reserved / PE header offset

// AArch64 Cookie boot contract:
//   x0 = physical pointer to a flattened device tree when supplied by loader
//   bare-metal QEMU virt fallback = DTB at RAM base 0x4000_0000
//   MMU is disabled
//   entry is at EL1
//
// The fallback is only for the QEMU virt standalone image. QEMU documents the
// generated DTB at the start of RAM for bare-metal boot; real platform loaders
// should continue to provide x0 explicitly.
.Lcookie_entry:
    // Interrupt delivery is an explicit Cookie state transition. Firmware/reset
    // DAIF state is not trusted; nothing asynchronous is admitted until GICv3,
    // vectors and the timer are fully established and EL0 ERET clears I.
    //
    // Masked before the exception level is even examined, because the EL2 drop
    // below is a window in which an asynchronous exception would vector through
    // whatever EL2 state firmware left behind.
    msr daifset, #0xf
    isb

    mrs x1, CurrentEL
    cmp x1, #0x4                               // EL1: already where Cookie runs
    b.eq .Lcookie_at_el1
    cmp x1, #0x8                               // EL2: drop, see below
    b.eq .Lcookie_drop_from_el2
    b .Lhalt                                   // EL3 or EL0: not Cookie's job

// Cookie runs at EL1 and owns the machine. A phone's bootloader commonly hands
// off at EL2 - it is where a hypervisor would go, and nothing is there - so a
// kernel that only accepts EL1 does not start on the machines Cookie is for.
// Dropping is the kernel's own responsibility and there is nobody else to do
// it. EL3 is refused rather than dropped from: secure firmware owns that level
// and a kernel that took it would be replacing the trust root, not booting.
//
// Every register written here is one whose reset value is not architecturally
// guaranteed to be what Cookie needs, so each is set rather than assumed.
.Lcookie_drop_from_el2:
    // EL1 executes AArch64. Without RW the eret below delivers Cookie to
    // AArch32 and the first instruction is nonsense.
    mov x1, #(1 << 31)
    msr hcr_el2, x1

    // EL1's system control register starts from the architecturally required
    // RES1 bits and nothing else: MMU off, caches off, little-endian,
    // alignment checking off. Firmware may have left EL1 state configured for
    // its own use, and the MMU bit in particular must be clear because Cookie
    // enables translation itself, later, against tables that do not exist yet.
    mov x1, #0x0800
    movk x1, #0x30D0, lsl #16
    msr sctlr_el1, x1

    // The architected timer has to be usable from EL1. CNTHCTL_EL2 bits 0 and 1
    // stop EL2 trapping EL1's counter and timer accesses, and CNTVOFF_EL2 must
    // be zero or the virtual counter Cookie schedules against is offset from
    // the physical one by whatever firmware left there.
    mrs x1, cnthctl_el2
    orr x1, x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr

    // No stage-2 translation, no coprocessor trapping. VTTBR_EL2 is zeroed
    // because a non-zero VMID with stage 2 disabled still tags TLB entries.
    msr vttbr_el2, xzr
    msr hstr_el2, xzr
    // RES1 pattern. TFP (bit 10) is deliberately clear in it: trapping FP to an
    // EL2 that has no vectors installed would turn a future program's first
    // FP instruction into a silent hang, where leaving it untrapped makes it an
    // ordinary EL1 exception the kernel can report.
    mov x1, #0x33FF
    msr cptr_el2, x1

    // GICv3 system-register access has to be permitted for EL1 before Cookie
    // gets there. SRE enables the system-register interface at EL2; Enable is
    // what stops EL1's own ICC_SRE_EL1 write from trapping here. Without this
    // the GIC initialisation refuses - correctly, because it re-reads SRE and
    // fails closed rather than assuming the write took - and the machine has no
    // interrupt controller.
    //
    // This instruction only exists on a machine that implements the GICv3
    // system registers, which Cookie already requires; see
    // docs/M7_13_HARDWARE_NEUTRALITY.md for what that excludes.
    mov x1, #0x9
    msr S3_4_C12_C9_5, x1                      // ICC_SRE_EL2
    isb

    // EL1h with all four DAIF bits masked, so the state Cookie arrives in is
    // the state it would have set for itself.
    mov x1, #0x3C5
    msr spsr_el2, x1
    adr x1, .Lcookie_at_el1
    msr elr_el2, x1
    eret

.Lcookie_at_el1:

    cbnz x0, .Ldtb_ready
    movz x0, #0x4000, lsl #16                  // QEMU virt RAM/DTB base 0x40000000
.Ldtb_ready:
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
