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
    // Little-endian, 4 KiB pages, placeable anywhere.
    //
    // Bit 3 is the claim that matters and it was previously false. It says the
    // 2 MiB-aligned base may be anywhere in physical memory rather than needing
    // to sit as close as possible to the start of DRAM - which is what a phone's
    // bootloader wants to hear, and which Cookie could not honestly say while
    // it was linked -no-pie and ran only at one address. The image is now built
    // -fpie and the linker asserts it needs no runtime relocations, so the
    // claim is true and is checked rather than declared.
    //
    // The page-size field was previously "unspecified", which is a licence to
    // place the image for a granule Cookie does not use.
    .quad 0x000000000000000A                  // flags: LE, 4 KiB granule, anywhere
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
    // Guarded, because that instruction does not exist everywhere.
    //
    // It used to be unconditional, on the stated premise that Cookie "already
    // requires" the GICv3 system registers. docs/M7_18_GIC_V2.md removes that
    // premise: a phone-class SoC with a GICv2 reports ID_AA64PFR0_EL1.GIC == 0
    // and has no system-register interface, so the msr is UNDEFINED. It runs at
    // EL2, before VBAR_EL2 has been given a handler, so the machine takes an
    // exception to a vector that does not exist and dies before the first
    // instruction that could say why.
    //
    // No gate would have caught it, and no gate will catch it soon. Nothing in
    // CI boots a GICv2 at all today, and the GICv2 runs M7.18 proposes enter at
    // EL1, so they would not execute this path either; every run that does
    // execute it uses a GICv3. GICv2 *and* an EL2 handoff is exactly the phone
    // case and the one combination no configuration produces - the
    // emulator-leniency class docs/SECURITY_AUDIT_2026_08_16.md names, arriving
    // in the one place where the failure has no way to report itself.
    mrs x1, id_aa64pfr0_el1
    ubfx x1, x1, #24, #4                       // ID_AA64PFR0_EL1.GIC
    cbz x1, .Lcookie_no_gic_sysreg
    mov x1, #0x9
    msr S3_4_C12_C9_5, x1                      // ICC_SRE_EL2
    isb
.Lcookie_no_gic_sysreg:

    // EL1h with all four DAIF bits masked, so the state Cookie arrives in is
    // the state it would have set for itself.
    mov x1, #0x3C5
    msr spsr_el2, x1
    adr x1, .Lcookie_at_el1
    msr elr_el2, x1
    eret

.Lcookie_at_el1:
    // Apply the image's own R_AARCH64_RELATIVE relocations, before anything
    // reads a global.
    //
    // Cookie is linked -pie so a phone's bootloader can place it anywhere. Two
    // kinds of thing still hold absolute addresses at link time: a file-scope
    // object constructed with a pointer to another file-scope object, and the
    // global offset table the compiler builds for symbols defined in assembly
    // that C++ only declares. Fifteen entries, measured rather than assumed.
    //
    // Eliminating the sources was the other option and was rejected. It would
    // mean no file-scope object may ever hold a pointer to another one - a
    // standing constraint on every future line of kernel code, enforced by a
    // link failure far from whatever introduced it. Twenty instructions here
    // costs once and constrains nothing.
    //
    // The delta is the runtime address of _start minus its link-time address.
    // The runtime one comes from adrp, which is PC-relative and therefore
    // already correct. The link-time one is read out of __cookie_link_base,
    // which is a relocation target itself - so at this instant it still holds
    // the unrelocated value, which is exactly the one needed. It is relocated
    // along with everything else by the loop below.
    adrp x8, _start
    add  x8, x8, :lo12:_start
    adrp x9, __cookie_link_base
    ldr  x9, [x9, :lo12:__cookie_link_base]
    subs x10, x8, x9
    b.eq .Lcookie_relocated                    // loaded where linked: nothing to do

    adrp x11, __cookie_rela_start
    add  x11, x11, :lo12:__cookie_rela_start
    adrp x12, __cookie_rela_end
    add  x12, x12, :lo12:__cookie_rela_end
.Lcookie_reloc_loop:
    cmp  x11, x12
    b.hs .Lcookie_relocated
    ldr  x13, [x11]                            // Elf64_Rela.r_offset
    ldr  x14, [x11, #8]                        // Elf64_Rela.r_info
    ldr  x15, [x11, #16]                       // Elf64_Rela.r_addend
    add  x11, x11, #24
    // Only R_AARCH64_RELATIVE (1027) is handled. Anything else is skipped here
    // and refused by the build - see the relocation-type check in the kernel
    // workflow. A loop that silently ignored a type it did not implement would
    // leave one pointer unrelocated, which is worse than not relocating at all
    // because everything else would work.
    and  x16, x14, #0xffffffff
    cmp  x16, #1027
    b.ne .Lcookie_reloc_loop
    add  x13, x13, x10
    add  x15, x15, x10
    str  x15, [x13]
    b .Lcookie_reloc_loop
.Lcookie_relocated:

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
// The link-time address of _start, so the relocation loop above can work out
// how far the image was moved. It lives in .data because the loop relocates it
// like any other pointer; reading it before that happens is the point.
.section .data.cookie_link_base, "aw"
.align 3
.global __cookie_link_base
__cookie_link_base:
    .quad _start

.section .bss.boot_stack, "aw", %nobits
.align 12
.global __cookie_bootstrap_stack_start
__cookie_bootstrap_stack_start:
    .skip 65536
.global __cookie_bootstrap_stack_top
__cookie_bootstrap_stack_top:
