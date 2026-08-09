#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/crypto.hpp>
#include <os/keys/key.hpp>

namespace os::keys {

inline constexpr std::uint32_t ciphertext_magic = 0x59454B45U; // "EKEY" little-endian
inline constexpr std::uint16_t ciphertext_envelope_version = 1U;
inline constexpr std::uint16_t ciphertext_header_bytes = 44U;
inline constexpr std::size_t ciphertext_fixed_overhead =
    static_cast<std::size_t>(ciphertext_header_bytes) + aead_nonce_bytes + aead_tag_bytes;
inline constexpr std::size_t max_ciphertext_envelope_bytes =
    ciphertext_fixed_overhead + max_key_plaintext_bytes;

struct CiphertextHeaderV1 final {
    CryptoProfileId profile {CryptoProfileId::aes_256_gcm_v1};
    KeyId key_id {};
    std::uint32_t key_version {0U};
    std::uint32_t ciphertext_size {0U};
};

struct CiphertextEnvelopeView final {
    CiphertextHeaderV1 header {};
    os::core::ByteSpan authenticated_header {};
    os::core::ByteSpan nonce {};
    os::core::ByteSpan tag {};
    os::core::ByteSpan ciphertext {};
};

[[nodiscard]] os::core::Result<std::size_t>
encode_ciphertext_header_v1(
    const CiphertextHeaderV1& header,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<CiphertextEnvelopeView>
parse_ciphertext_envelope_v1(os::core::ByteSpan envelope) noexcept;

} // namespace os::keys
