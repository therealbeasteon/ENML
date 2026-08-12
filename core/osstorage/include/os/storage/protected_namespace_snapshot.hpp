#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/keys/hierarchy.hpp>
#include <os/storage/protected_namespace.hpp>

namespace os::storage {

inline constexpr std::uint32_t protected_namespace_snapshot_magic = 0x534E4B43U; // "CKNS"
inline constexpr std::uint16_t protected_namespace_snapshot_version = 1U;

struct ProtectedNamespaceSnapshotHeaderV1 final {
    os::core::UserId user {};
    os::keys::SecurityEpoch security_epoch {};
    std::uint64_t sequence {0U};
    std::uint32_t entry_count {0U};
    std::uint32_t flags {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return user.value() != 0U && security_epoch.valid() && sequence != 0U &&
            entry_count <= max_protected_namespace_entries && flags == 0U;
    }
};

struct ProtectedNamespaceFreshnessEvidence final {
    os::core::UserId user {};
    os::keys::SecurityEpoch current_security_epoch {};
    std::uint64_t minimum_sequence {0U};
};

namespace protected_namespace_snapshot_errors {
inline constexpr std::uint32_t invalid = 1U;
inline constexpr std::uint32_t wrong_user = 2U;
inline constexpr std::uint32_t stale_epoch = 3U;
inline constexpr std::uint32_t stale_sequence = 4U;
} // namespace protected_namespace_snapshot_errors

[[nodiscard]] constexpr os::core::Error protected_namespace_snapshot_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::storage, 0x760U + code);
}

[[nodiscard]] inline os::core::Result<void>
validate_namespace_snapshot_freshness(
    const ProtectedNamespaceSnapshotHeaderV1& header,
    const ProtectedNamespaceFreshnessEvidence& evidence) noexcept {
    if (!header.valid() || evidence.user.value() == 0U ||
        !evidence.current_security_epoch.valid() || evidence.minimum_sequence == 0U) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }
    if (header.user != evidence.user) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::wrong_user);
    }
    if (header.security_epoch != evidence.current_security_epoch) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::stale_epoch);
    }
    if (header.sequence < evidence.minimum_sequence) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::stale_sequence);
    }
    return {};
}

} // namespace os::storage
