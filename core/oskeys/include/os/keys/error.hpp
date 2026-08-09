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
} // namespace errors

[[nodiscard]] constexpr os::core::Error key_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::key, code);
}

} // namespace os::keys
