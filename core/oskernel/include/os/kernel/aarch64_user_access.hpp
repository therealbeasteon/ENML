#pragma once

#include <cstddef>
#include <span>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/user_access.hpp>

namespace os::kernel::aarch64 {

namespace user_copy_errors {
inline constexpr std::uint32_t wrong_intent = 220U;
inline constexpr std::uint32_t size_mismatch = 221U;
inline constexpr std::uint32_t inactive_translation = 222U;
inline constexpr std::uint32_t wrong_current_translation = 223U;
} // namespace user_copy_errors

// Cookie does not temporarily make ordinary EL1 loads/stores capable of
// touching user mappings. The AArch64 implementation uses LDTRB/STTRB, whose
// accesses are checked with unprivileged permissions, while the software ticket
// binds the operation to one live process-memory incarnation.
//
// These routines are deliberately limited to one already-prepared page-contained
// ticket. Fault recovery is added by the exception-fixup layer before these are
// exposed through EL0 syscalls; until then they are machine primitives only.
[[nodiscard]] os::core::Result<void> copy_from_user_current(
    const UserAccessTicket& ticket,
    std::span<std::byte> destination,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

[[nodiscard]] os::core::Result<void> copy_to_user_current(
    const UserAccessTicket& ticket,
    std::span<const std::byte> source,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

} // namespace os::kernel::aarch64
