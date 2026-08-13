#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/keys/crypto.hpp>

namespace os::storage {

inline constexpr std::uint32_t protected_chunk_magic = 0x4B4F4F43U; // "COOK" little-endian
inline constexpr std::uint16_t protected_chunk_version = 1U;
inline constexpr std::uint16_t protected_chunk_header_bytes = 52U;
inline constexpr std::size_t protected_chunk_plaintext_bytes = 48U * 1024U;

struct ProtectedObjectId final {
    std::uint64_t high {0U};
    std::uint64_t low {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return high != 0U || low != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const ProtectedObjectId&,
        const ProtectedObjectId&) = default;
};

// This header is authenticated as AEAD associated data. It is encoded field by
// field in little-endian order; the in-memory C++ layout is never persisted.
// Pathnames are deliberately absent: rename is namespace policy, while object_id
// is the stable cryptographic identity of the protected object.
struct ProtectedChunkHeaderV1 final {
    os::keys::CryptoProfileId crypto_profile {os::keys::CryptoProfileId::aes_256_gcm_v1};
    os::core::UserId user {};
    ProtectedObjectId object_id {};
    std::uint64_t chunk_index {0U};
    std::uint32_t plaintext_size {0U};
    std::uint32_t flags {0U};
};

[[nodiscard]] constexpr bool valid_protected_chunk_header(
    const ProtectedChunkHeaderV1& header) noexcept {
    return os::keys::valid_crypto_profile(header.crypto_profile) &&
        header.user.value() != 0U &&
        header.object_id.valid() &&
        header.plaintext_size <= protected_chunk_plaintext_bytes &&
        header.flags == 0U;
}

[[nodiscard]] os::core::Result<std::size_t>
encode_protected_chunk_header_v1(
    const ProtectedChunkHeaderV1& header,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<ProtectedChunkHeaderV1>
decode_protected_chunk_header_v1(os::core::ByteSpan input) noexcept;

} // namespace os::storage
