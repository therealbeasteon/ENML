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
// AP[2:1] encodings used by Cookie's stage-1 leafs.
inline constexpr std::uint64_t ap_el1_rw_el0_none = 0ULL << 6U;
inline constexpr std::uint64_t ap_el1_rw_el0_rw = 1ULL << 6U;
inline constexpr std::uint64_t ap_el1_ro_el0_none = 2ULL << 6U;
inline constexpr std::uint64_t ap_el1_ro_el0_ro = 3ULL << 6U;
inline constexpr std::uint64_t share_outer = 2ULL << 8U;
inline constexpr std::uint64_t share_inner = 3ULL << 8U;
inline constexpr std::uint64_t access_flag = 1ULL << 10U;
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

// EL1-only mapping encoding. This remains deliberately separate from user_page_descriptor():
// adding EL0 access must never silently broaden a kernel mapping.
[[nodiscard]] constexpr std::uint64_t page_descriptor(
    std::uint64_t physical_address,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    if (!stage1_physical_address(physical_address)) return 0ULL;

    std::uint64_t value = (physical_address & page_address_mask) |
                          descriptor::valid | descriptor::table_or_page |
                          descriptor::access_flag |
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

// Explicit EL0 mapping encoding for ordinary memory only. Device memory is never
// user-mappable through this primitive. Executable pages are read-only at both
// EL0 and EL1 and PXN prevents the kernel from executing user code while it is
// privileged; writable pages are UXN and PXN. MachinePermissions has no RWX
// state, so writable+executable user memory is unrepresentable.
[[nodiscard]] constexpr std::uint64_t user_page_descriptor(
    std::uint64_t physical_address,
    MachinePermissions permissions) noexcept {
    if (!stage1_physical_address(physical_address)) return 0ULL;

    std::uint64_t value = (physical_address & page_address_mask) |
                          descriptor::valid | descriptor::table_or_page |
                          descriptor::access_flag |
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
        // PXN remains set while UXN is deliberately clear: EL0 may execute,
        // EL1 may not execute the same user-controlled bytes.
        return value | descriptor::ap_el1_ro_el0_ro;
    default:
        return 0ULL;
    }
}

// 39-bit TTBR0_EL1 regime, 4 KiB granule, inner-shareable WBWA table walks.
// Cookie v1 intentionally caps the configured physical-address width at 48 bits
// even if hardware reports 52-bit support; wider descriptor layouts get their
// own review rather than silently changing this format.
[[nodiscard]] constexpr std::uint8_t cookie_ips(std::uint8_t hardware_parange) noexcept {
    return hardware_parange <= 5U ? hardware_parange : 5U;
}

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

[[nodiscard]] os::core::Result<void>
activate_stage1_translation(std::uint64_t level1_root_physical) noexcept;

} // namespace os::kernel::aarch64;
