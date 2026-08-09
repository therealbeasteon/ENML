#pragma once

#include <cstdint>

#include <os/core/error.hpp>

namespace os::storage {

namespace errors {
inline constexpr std::uint32_t invalid_path = 1U;
inline constexpr std::uint32_t path_too_long = 2U;
inline constexpr std::uint32_t segment_too_long = 3U;
inline constexpr std::uint32_t path_escape = 4U;
inline constexpr std::uint32_t not_found = 5U;
inline constexpr std::uint32_t already_exists = 6U;
inline constexpr std::uint32_t not_directory = 7U;
inline constexpr std::uint32_t is_directory = 8U;
inline constexpr std::uint32_t not_regular_file = 9U;
inline constexpr std::uint32_t access_denied = 10U;
inline constexpr std::uint32_t read_only_filesystem = 11U;
inline constexpr std::uint32_t no_space = 12U;
inline constexpr std::uint32_t resource_exhausted = 13U;
inline constexpr std::uint32_t io_failure = 14U;
inline constexpr std::uint32_t too_large = 15U;
inline constexpr std::uint32_t invalid_offset = 16U;
inline constexpr std::uint32_t invalid_options = 17U;
inline constexpr std::uint32_t invalid_root = 18U;
} // namespace errors

[[nodiscard]] constexpr os::core::Error storage_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::storage, code);
}

} // namespace os::storage
