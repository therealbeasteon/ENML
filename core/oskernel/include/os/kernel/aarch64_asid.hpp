#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/aarch64_translation.hpp>

namespace os::kernel::aarch64 {

// Cookie's first ASID regime intentionally uses only the low 8-bit architectural
// space. TCR_EL1.AS remains clear until 16-bit ASID support is separately
// negotiated and reviewed. ASID zero is reserved for bootstrap/kernel state.
inline constexpr AddressSpaceAsid first_user_asid = 1U;
inline constexpr AddressSpaceAsid max_initial_user_asid = 255U;
inline constexpr std::uint64_t ttbr_asid_shift = 48U;

[[nodiscard]] constexpr bool supported_initial_asid(AddressSpaceAsid asid) noexcept {
    return asid >= first_user_asid && asid <= max_initial_user_asid;
}

[[nodiscard]] constexpr std::uint64_t ttbr0_el1_value(
    std::uint64_t level1_root_physical,
    AddressSpaceAsid asid) noexcept {
    if (!stage1_physical_address(level1_root_physical) || !supported_initial_asid(asid)) {
        return 0ULL;
    }
    return level1_root_physical | (static_cast<std::uint64_t>(asid) << ttbr_asid_shift);
}

// TLBI ASIDE1IS takes the ASID in the architectural operand's ASID field.
[[nodiscard]] constexpr std::uint64_t aside1is_operand(AddressSpaceAsid asid) noexcept {
    return supported_initial_asid(asid)
        ? (static_cast<std::uint64_t>(asid) << ttbr_asid_shift)
        : 0ULL;
}

// Install an already-authorized process translation root. This operation makes
// no scheduling or ownership decision; those remain above the machine layer.
[[nodiscard]] os::core::Result<void> install_process_translation(
    std::uint64_t level1_root_physical,
    AddressSpaceEpoch epoch) noexcept;

// Retire all stage-1 TLB entries associated with one quarantined ASID. The
// AddressSpaceEpochAuthority may complete retirement only after this succeeds.
[[nodiscard]] os::core::Result<void> retire_process_asid(
    RetiringAddressSpaceEpoch retiring) noexcept;

} // namespace os::kernel::aarch64
