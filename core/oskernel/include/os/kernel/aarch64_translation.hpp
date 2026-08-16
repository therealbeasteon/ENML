#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/aarch64.hpp>
#include <os/kernel/machine.hpp>

namespace os::kernel::aarch64 {

inline constexpr std::uint64_t page_mask = architectural_page_size - 1ULL;
inline constexpr std::uint64_t page_address_mask = 0x0000'FFFF'FFFF'F000ULL;
inline constexpr std::uint8_t stage1_va_bits = 39U;
inline constexpr std::uint8_t stage1_t0sz = 64U - stage1_va_bits;
inline constexpr std::uint8_t stage1_t1sz = 64U - stage1_va_bits;
inline constexpr std::uint64_t user_virtual_limit = 1ULL << stage1_va_bits;
inline constexpr std::uint64_t kernel_virtual_base =
    UINT64_MAX - (user_virtual_limit - 1ULL);
inline constexpr std::uint16_t table_entries = 512U;

inline constexpr std::uint8_t mair_normal_index = 0U;
inline constexpr std::uint8_t mair_device_index = 1U;
inline constexpr std::uint64_t mair_normal_wb_ra_wa = 0xFFULL;
inline constexpr std::uint64_t mair_device_ngnre = 0x04ULL;
inline constexpr std::uint64_t default_mair_el1 =
    (mair_normal_wb_ra_wa << (mair_normal_index * 8U)) |
    (mair_device_ngnre << (mair_device_index * 8U));

namespace descriptor {
inline constexpr std::uint64_t valid = 1ULL << 0U;
inline constexpr std::uint64_t table_or_page = 1ULL << 1U;
inline constexpr std::uint64_t attr_index_shift = 2U;
inline constexpr std::uint64_t ap_el1_rw_el0_none = 0ULL << 6U;
inline constexpr std::uint64_t ap_el1_rw_el0_rw = 1ULL << 6U;
inline constexpr std::uint64_t ap_el1_ro_el0_none = 2ULL << 6U;
inline constexpr std::uint64_t ap_el1_ro_el0_ro = 3ULL << 6U;
inline constexpr std::uint64_t share_outer = 2ULL << 8U;
inline constexpr std::uint64_t share_inner = 3ULL << 8U;
inline constexpr std::uint64_t access_flag = 1ULL << 10U;
// nG. The bit that makes a TLB entry belong to one address space.
//
// Every leaf Cookie installs sets this, and the reason is not performance. An
// entry filled with nG clear is *global*: it matches regardless of the ASID in
// TTBR0_EL1, so it keeps satisfying translations after the root is switched.
// install_process_translation switches roots without flushing - deliberately,
// that is what ASIDs are for - and every Cookie process maps its code and stack
// at the same virtual addresses. A global entry therefore lets the incoming
// process reach the outgoing one's memory at the same address, which is the
// isolation the whole address-space model exists to provide.
//
// It is also what makes retire_process_asid mean anything. `tlbi aside1is`
// invalidates entries *tagged with an ASID*; global entries carry no tag and
// survive it, so a destroyed space's translations would outlive the space.
//
// Set on kernel leaves too, not only user ones, and that is deliberate while
// Cookie translates through TTBR0 only: kernel mappings live inside each
// process root, so a global kernel entry is one an ASID retirement cannot
// reach either. When M7.7's TTBR1 split lands, upper-region kernel entries can
// legitimately be global - they are the same in every space by construction -
// and that is the point at which to revisit this, not before.
inline constexpr std::uint64_t not_global = 1ULL << 11U;
inline constexpr std::uint64_t privileged_execute_never = 1ULL << 53U;
inline constexpr std::uint64_t unprivileged_execute_never = 1ULL << 54U;
} // namespace descriptor

[[nodiscard]] constexpr bool page_aligned(std::uint64_t value) noexcept {
    return (value & page_mask) == 0ULL;
}

// TTBR0_EL1 owns Cookie's lower canonical region. This remains the process
// universe: it is disposable, generation-bound and carries the process ASID.
[[nodiscard]] constexpr bool stage1_virtual_address(std::uint64_t value) noexcept {
    return value < user_virtual_limit;
}

[[nodiscard]] constexpr bool user_stage1_virtual_address(std::uint64_t value) noexcept {
    return stage1_virtual_address(value);
}

// TTBR1_EL1 will own Cookie's upper canonical region. Kernel mappings are kept
// out of per-process TTBR0 roots so switching a process cannot replace EL1's
// translation authority.
[[nodiscard]] constexpr bool kernel_stage1_virtual_address(std::uint64_t value) noexcept {
    return value >= kernel_virtual_base;
}

[[nodiscard]] constexpr bool stage1_regions_disjoint() noexcept {
    return user_virtual_limit <= kernel_virtual_base;
}

[[nodiscard]] constexpr bool stage1_physical_address(std::uint64_t value) noexcept {
    return page_aligned(value) && (value & ~page_address_mask) == 0ULL;
}

[[nodiscard]] constexpr std::uint16_t level1_index(std::uint64_t va) noexcept {
    return static_cast<std::uint16_t>((va >> 30U) & 0x1FFULL);
}
[[nodiscard]] constexpr std::uint16_t level2_index(std::uint64_t va) noexcept {
    return static_cast<std::uint16_t>((va >> 21U) & 0x1FFULL);
}
[[nodiscard]] constexpr std::uint16_t level3_index(std::uint64_t va) noexcept {
    return static_cast<std::uint16_t>((va >> 12U) & 0x1FFULL);
}

[[nodiscard]] constexpr std::uint64_t table_descriptor(std::uint64_t next_table_pa) noexcept {
    if (!stage1_physical_address(next_table_pa)) return 0ULL;
    return (next_table_pa & page_address_mask) |
           descriptor::valid | descriptor::table_or_page;
}

[[nodiscard]] constexpr std::uint64_t page_descriptor(
    std::uint64_t physical_address,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    if (!stage1_physical_address(physical_address)) return 0ULL;

    std::uint64_t value = (physical_address & page_address_mask) |
                          descriptor::valid | descriptor::table_or_page |
                          descriptor::access_flag | descriptor::not_global |
                          descriptor::unprivileged_execute_never;

    if (kind == MachineMemoryKind::normal) {
        value |= static_cast<std::uint64_t>(mair_normal_index) << descriptor::attr_index_shift;
        value |= descriptor::share_inner;
    } else if (kind == MachineMemoryKind::device) {
        value |= static_cast<std::uint64_t>(mair_device_index) << descriptor::attr_index_shift;
        value |= descriptor::share_outer;
    } else {
        return 0ULL;
    }

    switch (permissions) {
    case MachinePermissions::read:
        value |= descriptor::ap_el1_ro_el0_none |
                 descriptor::privileged_execute_never;
        break;
    case MachinePermissions::read_write:
        value |= descriptor::ap_el1_rw_el0_none |
                 descriptor::privileged_execute_never;
        break;
    case MachinePermissions::read_execute:
        if (kind != MachineMemoryKind::normal) return 0ULL;
        value |= descriptor::ap_el1_ro_el0_none;
        break;
    default:
        return 0ULL;
    }
    return value;
}

[[nodiscard]] constexpr std::uint64_t user_page_descriptor(
    std::uint64_t physical_address,
    MachinePermissions permissions) noexcept {
    if (!stage1_physical_address(physical_address)) return 0ULL;

    std::uint64_t value = (physical_address & page_address_mask) |
                          descriptor::valid | descriptor::table_or_page |
                          descriptor::access_flag | descriptor::not_global |
                          (static_cast<std::uint64_t>(mair_normal_index) << descriptor::attr_index_shift) |
                          descriptor::share_inner |
                          descriptor::privileged_execute_never;

    switch (permissions) {
    case MachinePermissions::read:
        return value | descriptor::ap_el1_ro_el0_ro |
               descriptor::unprivileged_execute_never;
    case MachinePermissions::read_write:
        return value | descriptor::ap_el1_rw_el0_rw |
               descriptor::unprivileged_execute_never;
    case MachinePermissions::read_execute:
        return value | descriptor::ap_el1_ro_el0_ro;
    default:
        return 0ULL;
    }
}

[[nodiscard]] constexpr std::uint8_t cookie_ips(std::uint8_t hardware_parange) noexcept {
    return hardware_parange <= 5U ? hardware_parange : 5U;
}

// Bring-up TCR: lower 39-bit TTBR0 only. TTBR1 walks remain disabled until the
// kernel has been relocated into the separately reviewed upper region.
[[nodiscard]] constexpr std::uint64_t tcr_el1_for_ips(std::uint8_t ips) noexcept {
    if (ips > 5U) return 0ULL;
    constexpr std::uint64_t irgn0_wbwa = 1ULL << 8U;
    constexpr std::uint64_t orgn0_wbwa = 1ULL << 10U;
    constexpr std::uint64_t sh0_inner = 3ULL << 12U;
    constexpr std::uint64_t epd1_disable_ttbr1_walks = 1ULL << 23U;
    return static_cast<std::uint64_t>(stage1_t0sz) |
           irgn0_wbwa | orgn0_wbwa | sh0_inner |
           epd1_disable_ttbr1_walks |
           (static_cast<std::uint64_t>(ips) << 32U);
}

// Mature split contract: TTBR0 is the disposable user domain and TTBR1 is the
// stable kernel domain. Both use 39-bit, 4 KiB, inner-shareable WBWA walks.
// A1 remains clear so the current ASID comes from TTBR0_EL1; EPD1 remains clear
// so upper-region walks are enabled. This function is pure/testable and is not
// used by activate_stage1_translation() until high-kernel relocation is ready.
[[nodiscard]] constexpr std::uint64_t split_tcr_el1_for_ips(std::uint8_t ips) noexcept {
    if (ips > 5U) return 0ULL;
    constexpr std::uint64_t irgn0_wbwa = 1ULL << 8U;
    constexpr std::uint64_t orgn0_wbwa = 1ULL << 10U;
    constexpr std::uint64_t sh0_inner = 3ULL << 12U;
    constexpr std::uint64_t t1sz_shift = 16U;
    constexpr std::uint64_t irgn1_wbwa = 1ULL << 24U;
    constexpr std::uint64_t orgn1_wbwa = 1ULL << 26U;
    constexpr std::uint64_t sh1_inner = 3ULL << 28U;
    constexpr std::uint64_t tg1_4k = 2ULL << 30U;
    return static_cast<std::uint64_t>(stage1_t0sz) |
           irgn0_wbwa | orgn0_wbwa | sh0_inner |
           (static_cast<std::uint64_t>(stage1_t1sz) << t1sz_shift) |
           irgn1_wbwa | orgn1_wbwa | sh1_inner | tg1_4k |
           (static_cast<std::uint64_t>(ips) << 32U);
}

// The EL1 control state Cookie runs under, constructed rather than inherited.
//
// SCTLR_EL1 used to be read-modify-written: three bits set, every other bit
// left at whatever firmware or a bootloader happened to leave. That is not a
// small thing to inherit. Endianness for EL0 (E0E), hardware write-implies-XN
// (WXN), stack-alignment checking (SA, SA0), whether EL0 may execute cache
// maintenance (UCI) or read cache geometry (UCT) or zero a line (DZE), and
// whether EL0 may touch DAIF (UMA) all live here. A kernel whose isolation
// depends on those has to set them.
//
// Three of the choices are security decisions rather than configuration:
//
//   * WXN makes "writable implies not executable" an architectural property of
//     the translation regime rather than a rule the mapping code enforces.
//     docs/M7_4_KERNEL_HARDENING.md's W^X requirement is checked in software on
//     every map; this is the same rule stated where it cannot be forgotten.
//
//   * UCI, UCT and DZE are cleared, which denies EL0 the cache-maintenance
//     instructions, the cache geometry to aim them with, and DC ZVA. Those are
//     the standard Flush+Reload construction kit -
//     docs/REFERENCE_NOTES_2026_08_16_CACHE_CHANNELS.md describes the attack
//     they enable on ARM. Cookie has no userland that needs them, so the cost
//     of withholding them is zero today and the cost of granting them by
//     inheritance was a side channel nobody chose.
//
//   * UMA is cleared, so EL0 cannot read or write DAIF and cannot mask
//     interrupts to extend its own turn.
//
// nTWI and nTWE are set - WFI and WFE from EL0 are *not* trapped - because
// trapping them requires a handler that does not exist, and a trap with no
// handler is a halt rather than a policy. Denying EL0 those instructions is
// worth doing when something can answer them.
// `privileged_access_never` must come from ID_AA64MMFR1_EL1, never from a
// guess: bit 23 is SPAN only where FEAT_PAN exists and is RES1 everywhere else,
// so clearing it on a core without the feature is CONSTRAINED UNPREDICTABLE.
//
// Clearing it is worth the check. SPAN=0 means PSTATE.PAN is set on every
// exception entry to EL1, so an ordinary load or store from the kernel to any
// page EL0 can reach *faults*. That turns a whole defect class - a kernel bug
// that dereferences an address userspace chose - from a silent read of user
// memory into an exception. Cookie can afford it outright because the hard half
// is already built: every legitimate access to user memory goes through
// cookie_aarch64_ldtrb_user_byte / sttrb, and unprivileged loads and stores are
// exactly what PAN leaves working.
[[nodiscard]] constexpr std::uint64_t cookie_sctlr_el1(bool privileged_access_never) noexcept {
    // ARMv8.0 RES1 bits: 11, 20, 22, 23, 28, 29.
    constexpr std::uint64_t res1 = 0x30D0'0800ULL;
    constexpr std::uint64_t span = 1ULL << 23U;
    constexpr std::uint64_t mmu_enable = 1ULL << 0U;
    constexpr std::uint64_t data_cache_enable = 1ULL << 2U;
    constexpr std::uint64_t stack_alignment_el1 = 1ULL << 3U;
    constexpr std::uint64_t stack_alignment_el0 = 1ULL << 4U;
    constexpr std::uint64_t instruction_cache_enable = 1ULL << 12U;
    constexpr std::uint64_t wfi_not_trapped = 1ULL << 16U;
    constexpr std::uint64_t wfe_not_trapped = 1ULL << 18U;
    constexpr std::uint64_t write_implies_execute_never = 1ULL << 19U;
    const std::uint64_t established =
        res1 | mmu_enable | data_cache_enable | stack_alignment_el1 |
        stack_alignment_el0 | instruction_cache_enable | wfi_not_trapped |
        wfe_not_trapped | write_implies_execute_never;
    return privileged_access_never ? (established & ~span) : established;
}

// Advanced SIMD, floating point, SVE and SME, all trapped at EL0 and EL1.
//
// This is a privacy decision and it closes a hole that was described in a
// comment and enforced nowhere. aarch64_exception.hpp already says of the
// exception frame: "Silently letting userspace touch V0-V31 without preserving
// them would create cross-thread information leakage." That is exactly right,
// and Cookie never wrote CPACR_EL1 - whose FPEN field resets to an
// architecturally UNKNOWN value and is routinely left enabled by firmware that
// used floating point itself. A process could have written secrets into V0-V31
// and the next process could have read them, because nothing saves or restores
// those registers across a switch and nothing stopped anyone using them.
//
// Trapped rather than saved, because trapping is the honest state: Cookie has
// no FP context-switch policy, and the choice is between refusing the registers
// and sharing them. An EL0 program that uses FP now takes an exception the
// kernel reports instead of silently joining a shared register file.
//
// Trapped at EL1 as well. The kernel builds with -mgeneral-regs-only and CI
// rejects any FP register in the image, so this cannot fire today - which is
// the point: if a future compiler emits one anyway, it stops the kernel loudly
// rather than corrupting a register file nothing is preserving.
//
// TTA traps trace-register access. Cookie uses no trace, and a debug facility
// EL0 can reach is one it can measure the kernel with.
[[nodiscard]] constexpr std::uint64_t cookie_cpacr_el1() noexcept {
    constexpr std::uint64_t trap_trace_access = 1ULL << 28U;
    // FPEN (21:20), ZEN (17:16) and SMEN (25:24) are all left zero, which is
    // "trapped at EL0 and EL1" for each. They are named here rather than
    // omitted so a reader can see that the zero is a decision.
    return trap_trace_access;
}

// Whether this core implements FEAT_PAN, read from ID_AA64MMFR1_EL1.
//
// Exposed rather than kept private to activate_stage1_translation because the
// boot proof reports which path it took. A hardening that is silently absent on
// half the machines it runs on is one nobody notices the absence of.
[[nodiscard]] bool privileged_access_never_available() noexcept;

[[nodiscard]] os::core::Result<void>
activate_stage1_translation(std::uint64_t level1_root_physical) noexcept;

// Writes the execution controls above and verifies they took.
//
// Read back rather than assumed, the same way the GICv3 code re-reads SRE: a
// field that silently refused a write would leave the kernel believing it had
// denied something it had not, which is worse than never having tried.
[[nodiscard]] os::core::Result<void> establish_execution_controls() noexcept;

} // namespace os::kernel::aarch64
