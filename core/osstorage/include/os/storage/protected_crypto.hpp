#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/provider.hpp>
#include <os/storage/protected_format.hpp>

namespace os::storage {

inline constexpr std::size_t protected_chunk_overhead_bytes =
    protected_chunk_header_bytes + os::keys::aead_nonce_bytes + os::keys::aead_tag_bytes;
inline constexpr std::size_t max_protected_chunk_record_bytes =
    protected_chunk_overhead_bytes + protected_chunk_plaintext_bytes;

struct ProtectedChunkAddress final {
    os::core::UserId user {};
    ProtectedObjectId object_id {};
    std::uint64_t chunk_index {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return user.value() != 0U && object_id.valid();
    }
};

// Storage-owned cryptographic boundary. The provider owns key material and nonce
// generation; this class owns the canonical persistent envelope and validates
// that authenticated on-disk identity matches the independently trusted object
// and chunk address selected by system.storage.
class ProtectedChunkCrypto final {
public:
    explicit ProtectedChunkCrypto(os::keys::KeyProvider& provider) noexcept
        : provider_(&provider) {}

    [[nodiscard]] os::core::Result<std::size_t>
    seal(
        os::keys::ProviderKeyReference key,
        const ProtectedChunkHeaderV1& header,
        os::core::ByteSpan plaintext,
        os::core::MutableByteSpan output) noexcept;

    [[nodiscard]] os::core::Result<std::size_t>
    open(
        os::keys::ProviderKeyReference key,
        const ProtectedChunkAddress& expected,
        os::core::ByteSpan record,
        os::core::MutableByteSpan plaintext) noexcept;

private:
    os::keys::KeyProvider* provider_ {nullptr};
};

} // namespace os::storage
