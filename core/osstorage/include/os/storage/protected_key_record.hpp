#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/provider.hpp>
#include <os/storage/protected_format.hpp>

namespace os::storage {

inline constexpr std::uint32_t protected_key_record_magic = 0x594B4F43U; // "COKY" LE
inline constexpr std::uint16_t protected_key_record_version = 1U;
inline constexpr std::uint16_t protected_key_record_header_bytes = 64U;
// Provider authentication deliberately covers stable identity/policy fields but
// excludes provider_blob_size and reserved flags. Record parsing separately
// enforces exact length and zero flags.
inline constexpr std::size_t protected_key_binding_bytes = 56U;
inline constexpr std::size_t max_protected_key_record_bytes =
    protected_key_record_header_bytes + os::keys::max_persistent_provider_blob_bytes;

struct ProtectedObjectKeyBinding final {
    os::core::PrincipalId principal {};
    os::core::UserId user {};
    ProtectedObjectId object_id {};
    std::uint32_t generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return (principal.high != 0U || principal.low != 0U) &&
            user.value() != 0U && object_id.valid() && generation != 0U;
    }
};

struct ProtectedObjectKeyRecordView final {
    ProtectedObjectKeyBinding binding {};
    os::core::ByteSpan authenticated_binding {};
    os::core::ByteSpan provider_blob {};
};

[[nodiscard]] os::core::Result<std::size_t>
persist_protected_object_key_v1(
    os::keys::PersistentKeyProvider& provider,
    os::keys::ProviderKeyReference key,
    const ProtectedObjectKeyBinding& binding,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<ProtectedObjectKeyRecordView>
parse_protected_object_key_record_v1(os::core::ByteSpan record) noexcept;

// Restores only when trusted Storage object identity matches the authenticated
// record. A valid wrapped key copied to another app/user/object must not become
// authority there.
[[nodiscard]] os::core::Result<os::keys::ProviderKeyReference>
restore_protected_object_key_v1(
    os::keys::PersistentKeyProvider& provider,
    const ProtectedObjectKeyBinding& expected,
    os::core::ByteSpan record) noexcept;

} // namespace os::storage
