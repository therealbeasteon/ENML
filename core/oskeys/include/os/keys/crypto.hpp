#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/span.hpp>

namespace os::keys {

enum class CryptoProfileId : std::uint32_t {
    aes_256_gcm_v1 = 1U,
};

inline constexpr std::size_t aead_nonce_bytes = 12U;
inline constexpr std::size_t aead_tag_bytes = 16U;
inline constexpr std::size_t max_key_aad_bytes = 1024U;
inline constexpr std::size_t max_key_plaintext_bytes = 60U * 1024U;

struct AeadNonce final {
    std::array<std::byte, aead_nonce_bytes> bytes {};
};

struct AeadTag final {
    std::array<std::byte, aead_tag_bytes> bytes {};
};

[[nodiscard]] constexpr bool valid_crypto_profile(CryptoProfileId profile) noexcept {
    return profile == CryptoProfileId::aes_256_gcm_v1;
}

} // namespace os::keys
