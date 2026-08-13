#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/hierarchy.hpp>
#include <os/keys/provider.hpp>
#include <os/storage/protected_namespace.hpp>

namespace os::storage {

inline constexpr std::uint32_t protected_namespace_snapshot_magic = 0x534E4B43U; // "CKNS"
inline constexpr std::uint16_t protected_namespace_snapshot_version = 1U;
inline constexpr std::uint16_t protected_namespace_snapshot_header_bytes = 40U;
inline constexpr std::size_t protected_namespace_snapshot_overhead_bytes =
    protected_namespace_snapshot_header_bytes + os::keys::aead_nonce_bytes + os::keys::aead_tag_bytes;
inline constexpr std::size_t max_protected_namespace_snapshot_plaintext_bytes =
    os::keys::max_key_plaintext_bytes;
inline constexpr std::size_t max_protected_namespace_snapshot_record_bytes =
    protected_namespace_snapshot_overhead_bytes + max_protected_namespace_snapshot_plaintext_bytes;

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
inline constexpr std::uint32_t too_large = 5U;
inline constexpr std::uint32_t malformed_entry = 6U;
inline constexpr std::uint32_t provider_failure = 7U;
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

// Canonical encrypted snapshot codec. Header bytes are AEAD associated data;
// bounded variable-length entries are ciphertext. Caller-provided scratch keeps
// the 60 KiB plaintext ceiling out of fixed kernel/service stacks.
class ProtectedNamespaceSnapshotCrypto final {
public:
    explicit ProtectedNamespaceSnapshotCrypto(os::keys::KeyProvider& provider) noexcept
        : provider_(&provider) {}

    [[nodiscard]] os::core::Result<std::size_t>
    seal(
        os::keys::ProviderKeyReference metadata_key,
        const ProtectedNamespaceSnapshotHeaderV1& header,
        const ProtectedNamespaceRegistry& registry,
        os::core::MutableByteSpan plaintext_scratch,
        os::core::MutableByteSpan output) noexcept;

    [[nodiscard]] os::core::Result<ProtectedNamespaceSnapshotHeaderV1>
    open_and_restore(
        os::keys::ProviderKeyReference metadata_key,
        const ProtectedNamespaceFreshnessEvidence& freshness,
        os::core::ByteSpan record,
        os::core::MutableByteSpan plaintext_scratch,
        ProtectedNamespaceRegistry& registry) noexcept;

private:
    os::keys::KeyProvider* provider_ {nullptr};
};

} // namespace os::storage
