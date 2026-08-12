#pragma once

#include <cstdint>

#include <os/kernel/aarch64.hpp>
#include <os/kernel/machine.hpp>

namespace os::kernel::aarch64 {

inline constexpr std::uint64_t page_mask = architectural_page_size - 1ULL;
inline constexpr std::uint64_t page_address_mask = 0x0000'FFFF'FFFF'F000ULL;
inline constexpr std::uint8_t stage1_va_bits = 39U;
inline constexpr std::uint8_t stage1_t0sz = 64U - stage1_va_bits;
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
inline constexpr std::uint64_t ap_read_only_el1 = 2ULL << 6U;
inline constexpr std::uint64_t share_outer = 2ULL << 8U;
inline constexpr std::uint64_t share_inner = 3ULL << 8U;
inline constexpr std::uint64_t access_flag = 1ULL << 10U;
inline constexpr std::uint64_t privileged_execute_never = 1ULL << 53U;
inline constexpr std::uint64_t unprivileged_execute_never = 1ULL << 54U;
} // namespace descriptor

[[nodiscard]] constexpr bool page_aligned(std::uint64_t value) noexcept {
    return (value & page_mask) == 0ULL;
}

[[nodiscard]] constexpr bool stage1_virtual_address(std::uint64_t value) noexcept {
    return value < (1ULL << stage1_va_bits);
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

// Initial Cookie mappings are EL1-only. User mappings will get a separate
// reviewed encoding when the first EL0 address space is introduced; doing that
// here would silently broaden access before the syscall/process boundary exists.
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
        value |= descriptor::ap_read_only_el1 |
                 descriptor::privileged_execute_never;
        break;
    case MachinePermissions::read_write:
        value |= descriptor::privileged_execute_never;
        break;
    case MachinePermissions::read_execute:
        if (kind != MachineMemoryKind::normal) return 0ULL;
        value |= descriptor::ap_read_only_el1;
        break;
    default:
        return 0ULL;
    }
    return value;
}

// 39-bit TTBR0_EL1 regime, 4 KiB granule, inner-shareable WBWA table walks.
// IPS is kept as an explicit reviewed input from ID_AA64MMFR0_EL1 rather than
// assuming every future Cookie machine has the same physical-address width.
[[nodiscard]] constexpr std::uint64_t tcr_el1_for_ips(std::uint8_t ips) noexcept {
    if (ips > 6U) return 0ULL;
    constexpr std::uint64_t irgn0_wbwa = 1ULL << 8U;
    constexpr std::uint64_t orgn0_wbwa = 1ULL << 10U;
    constexpr std::uint64_t sh0_inner = 3ULL << 12U;
    return static_cast<std::uint64_t>(stage1_t0sz) |
           irgn0_wbwa | orgn0_wbwa | sh0_inner |
           (static_cast<std::uint64_t>(ips) << 32U);
}

} // namespace os::kernel::aarch64
