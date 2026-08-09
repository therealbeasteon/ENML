#pragma once

#include <cstdint>

#include <os/core/error.hpp>

namespace os::keys {

namespace errors {
inline constexpr std::uint32_t invalid_key = 1U;
inline constexpr std::uint32_t not_found = 2U;
inline constexpr std::uint32_t access_denied = 3U;
inline constexpr std::uint32_t destroyed = 4U;
inline constexpr std::uint32_t registry_full = 5U;
inline constexpr std::uint32_t provider_failure = 6U;
inline constexpr std::uint32_t unsupported_purpose = 7U;
inline constexpr std::uint32_t duplicate_key_id = 8U;
inline constexpr std::uint32_t invalid_rights = 9U;
inline constexpr std::uint32_t object_limit = 10U;
inline constexpr std::uint32_t io_failure = 11U;
inline constexpr std::uint32_t id_generation_failed = 12U;
inline constexpr std::uint32_t unsupported_crypto_profile = 13U;
inline constexpr std::uint32_t authentication_failed = 14U;
inline constexpr std::uint32_t malformed_ciphertext = 15U;
inline constexpr std::uint32_t output_too_small = 16U;
inline constexpr std::uint32_t key_version_mismatch = 17U;
inline constexpr std::uint32_t key_id_mismatch = 18U;
inline constexpr std::uint32_t too_large = 19U;
} // namespace errors

[[nodiscard]] constexpr os::core::Error key_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::key, code);
}

} // namespace os::keys
